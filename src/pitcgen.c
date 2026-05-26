/* pitcgen.c — Pi Timecode Generator
 *
 * Broadcast-quality LTC SMPTE timecode generator for Raspberry Pi.
 * Outputs via 3.5mm audio jack using libltc + ALSA.
 *
 * Timing: clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME) paces frame
 * generation to wall-clock deadlines.  ALSA writes are non-blocking;
 * the timer is the master.  Periodic resync to CLOCK_REALTIME (NTP-
 * disciplined) corrects long-term drift.
 *
 * Usage: pitcgen [-r fps] [-a hz] [-d device] [-s HH:MM:SS:FF] [-u bits]
 *
 * Frame rates: 24 | 25 | 29.97 | 29.97df | 30 | 30df  (default: 25)
 * User bits:   hex value (e.g. -u 0A1B2C3D) or "date"
 *
 * Dependencies: libltc-dev libasound2-dev
 *   sudo apt-get install -y libltc-dev libasound2-dev
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <ltc.h>

#include "tc.h"

/* ── Constants ───────────────────────────────────────────────────── */

#define DEFAULT_SAMPLE_RATE  48000
#define DEFAULT_ALSA_DEVICE  "default"
#define BUFFER_FRAMES        4096    /* ~85 ms of audio headroom */
#define PERIOD_FRAMES        1024    /* ~21 ms per period */
#define RESYNC_INTERVAL_NS   ((uint64_t)1000000000ULL)  /* 1 second */

/* ── Signal handling ─────────────────────────────────────────────── */

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static int parse_sample_rate(const char *s)
{
    char *end = NULL;
    long rate = strtol(s, &end, 10);
    if (!s || *s == '\0' || !end || *end != '\0')
        return -1;
    if (rate < 8000 || rate > 192000)
        return -1;
    return (int)rate;
}

/* ── ALSA ────────────────────────────────────────────────────────── */

static snd_pcm_t *alsa_open(const char *device, unsigned int requested_rate,
                            unsigned int *rate_out)
{
    snd_pcm_t *pcm;
    int err;

    if ((err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "pitcgen: cannot open '%s': %s\n",
                device, snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);

    if ((err = snd_pcm_hw_params_set_access(
                pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
        (err = snd_pcm_hw_params_set_format(
                pcm, hw, SND_PCM_FORMAT_S16_LE)) < 0 ||
        (err = snd_pcm_hw_params_set_channels(pcm, hw, 1)) < 0) {
        fprintf(stderr, "pitcgen: cannot set hw params: %s\n",
                snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }

    unsigned int rate = requested_rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
    *rate_out = rate;

    snd_pcm_uframes_t buf = BUFFER_FRAMES;
    snd_pcm_uframes_t per = PERIOD_FRAMES;
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buf);
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &per, 0);

    if ((err = snd_pcm_hw_params(pcm, hw)) < 0) {
        fprintf(stderr, "pitcgen: snd_pcm_hw_params: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }

    return pcm;
}

/*
 * Write a buffer of int16_t samples to ALSA.
 * Retries on underrun (xrun) and signal interruption.
 * Returns 0 on success, -1 on fatal error.
 */
static int alsa_write_all(snd_pcm_t *pcm, const int16_t *buf, int nframes)
{
    const int16_t *ptr = buf;
    snd_pcm_sframes_t remain = (snd_pcm_sframes_t)nframes;

    while (remain > 0 && g_running) {
        snd_pcm_sframes_t r = snd_pcm_writei(pcm, ptr,
                                              (snd_pcm_uframes_t)remain);
        if (r == -EPIPE) {
            snd_pcm_prepare(pcm);
        } else if (r == -EINTR) {
            continue;
        } else if (r < 0) {
            fprintf(stderr, "pitcgen: ALSA write error: %s\n",
                    snd_strerror((int)r));
            return -1;
        } else {
            ptr    += r;
            remain -= r;
        }
    }
    return (remain == 0) ? 0 : -1;
}

/* ── LTC frame encode + output ───────────────────────────────────── */

/*
 * Encode the current TC into an LTC frame, convert samples to int16_t,
 * and write to ALSA.  Returns number of samples written, or -1 on error.
 */
static int output_frame(LTCEncoder *enc, const TC *tc, const FrameRate *fr,
                        const unsigned char *ubg,
                        int16_t *conv, int max_samples,
                        snd_pcm_t *pcm)
{
    SMPTETimecode st = {
        .hours = (unsigned char)tc->h,
        .mins  = (unsigned char)tc->m,
        .secs  = (unsigned char)tc->s,
        .frame = (unsigned char)tc->f,
    };
    ltc_encoder_set_timecode(enc, &st);

    /* Set drop-frame bit and user bits */
    LTCFrame lf;
    ltc_encoder_get_frame(enc, &lf);
    lf.dfbit = fr->drop ? 1 : 0;
    if (ubg) {
        lf.user0 = ubg[0];
        lf.user1 = ubg[1];
        lf.user2 = ubg[2];
        lf.user3 = ubg[3];
    }
    ltc_encoder_set_frame(enc, &lf);

    /* Encode one LTC frame into internal PCM buffer */
    ltc_encoder_encode_frame(enc);

    /* Retrieve pointer to encoded samples */
    ltcsnd_sample_t *ltcbuf = NULL;
    int n = ltc_encoder_get_bufferptr(enc, &ltcbuf, 1);

    if (n <= 0 || n > max_samples) {
        fprintf(stderr, "pitcgen: unexpected sample count %d\n", n);
        return -1;
    }

    /*
     * Convert ltcsnd_sample_t → int16_t for ALSA S16_LE.
     * libltc samples are unsigned char (0–255), centred at 128.
     * Scale to full int16 range.  Adjust output level with:
     *   amixer sset PCM <percent>%
     */
#ifdef LTC_FLOAT_SAMPLES
    for (int i = 0; i < n; i++)
        conv[i] = (int16_t)(ltcbuf[i] * 32767.0f);
#else
    for (int i = 0; i < n; i++)
        conv[i] = (int16_t)(((int)ltcbuf[i] - 128) * 256);
#endif

    if (alsa_write_all(pcm, conv, n) < 0)
        return -1;

    return n;
}

/* ── Usage ───────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-r fps] [-a hz] [-d device] [-s HH:MM:SS:FF] [-u bits]\n\n"
        "  -r  Frame rate: 24 | 25 | 29.97 | 29.97df | 30 | 30df  (default: 25)\n"
        "  -a  Sample rate in Hz: 8000-192000  (default: 48000)\n"
        "  -d  ALSA device  (default: default)\n"
        "  -s  Start timecode, e.g. 01:00:00:00  (default: system clock)\n"
        "  -u  User bits: hex value (e.g. 0A1B2C3D) or \"date\"\n",
        prog);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const FrameRate *fr = &FRAME_RATES[1];  /* default: 25 fps */
    const char *device = DEFAULT_ALSA_DEVICE;
    unsigned int requested_rate = DEFAULT_SAMPLE_RATE;
    TC tc = {0};
    int use_clock = 1;
    unsigned char ubg[4] = {0};
    int use_ub = 0;
    int opt;

    while ((opt = getopt(argc, argv, "r:a:d:s:u:h")) != -1) {
        switch (opt) {
        case 'r':
            fr = fr_find(optarg);
            if (!fr) {
                fprintf(stderr, "pitcgen: unknown frame rate '%s'\n", optarg);
                usage(argv[0]);
                return 1;
            }
            break;
        case 'a': {
            int r = parse_sample_rate(optarg);
            if (r < 0) {
                fprintf(stderr, "pitcgen: invalid sample rate '%s'\n", optarg);
                usage(argv[0]);
                return 1;
            }
            requested_rate = (unsigned int)r;
            break;
        }
        case 'd':
            device = optarg;
            break;
        case 's':
            if (!tc_parse(&tc, optarg)) {
                fprintf(stderr,
                        "pitcgen: bad timecode '%s' (use HH:MM:SS:FF)\n",
                        optarg);
                return 1;
            }
            use_clock = 0;
            break;
        case 'u':
            if (ub_parse(optarg, &ubg[0], &ubg[1], &ubg[2], &ubg[3]) < 0) {
                fprintf(stderr,
                        "pitcgen: bad user bits '%s' (use hex or \"date\")\n",
                        optarg);
                return 1;
            }
            use_ub = 1;
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    /* Initialise timecode */
    if (use_clock)
        tc_from_clock(&tc, fr);
    else
        tc_from_clock(&tc, fr);  /* still seed sub-frame with clock */

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* ── Open ALSA ─────────────────────────────────────────────── */
    unsigned int actual_rate = 0;
    snd_pcm_t *pcm = alsa_open(device, requested_rate, &actual_rate);
    if (!pcm)
        return 1;

    if (actual_rate != requested_rate)
        fprintf(stderr,
                "pitcgen: sample rate negotiated to %u Hz (wanted %u)\n",
                actual_rate, requested_rate);

    /* ── Create LTC encoder ────────────────────────────────────── */
    LTCEncoder *enc = ltc_encoder_create(
            actual_rate, fr->fps,
            (fr->ifps == 25) ? LTC_TV_625_50 : LTC_TV_525_60,
            LTC_USE_DATE);
    if (!enc) {
        fprintf(stderr, "pitcgen: failed to create LTC encoder\n");
        snd_pcm_close(pcm);
        return 1;
    }

    /*
     * Allocate sample conversion buffer.
     * Worst case: 1 frame at 24 fps at actual sample rate + safety margin.
     */
    int max_frame_samples = (int)((actual_rate / 24.0) + 512.0);
    int16_t *conv = calloc((size_t)max_frame_samples, sizeof(*conv));
    if (!conv) {
        fprintf(stderr, "pitcgen: failed to allocate sample buffer\n");
        snd_pcm_close(pcm);
        ltc_encoder_free(enc);
        return 1;
    }

    fprintf(stderr,
            "pitcgen: %s fps  |  rate=%u Hz  |  device=%s  |  start %s%s\n",
            fr->name, actual_rate, device, tc_format(&tc),
            fr->drop ? " DF" : "");
    if (use_ub)
        fprintf(stderr,
                "pitcgen: user bits %02X %02X %02X %02X\n",
                ubg[0], ubg[1], ubg[2], ubg[3]);
    fprintf(stderr, "pitcgen: Ctrl-C to stop\n");

    /* ── Timer-based main loop ─────────────────────────────────── */

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);

    /*
     * deadline tracks the absolute monotonic time each frame is due.
     * We add frame_ns cumulatively to avoid drift from sleep jitter.
     * This gives sample-accurate long-term pacing.
     */
    uint64_t frame_count  = 0;
    uint64_t next_resync  = 0;   /* monotonic ns of next wall-clock resync */

    while (g_running) {
        uint64_t now_mono;
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            now_mono = (uint64_t)ts.tv_sec * 1000000000ULL
                     + (uint64_t)ts.tv_nsec;
        }

        /* ── Wall-clock resync ───────────────────────────────────
         *
         * Every RESYNC_INTERVAL_NS, check where the wall clock says
         * we SHOULD be and nudge the timecode if we've drifted.
         * This keeps the LTC counter locked to real time even if
         * the monotonic-timer frame schedule accumulates tiny errors.
         */
        if (now_mono >= next_resync) {
            next_resync = now_mono + RESYNC_INTERVAL_NS;

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            struct tm *t = localtime(&ts.tv_sec);

            long expected_total_frames =
                (long)(((t->tm_hour * 3600.0) +
                        (t->tm_min  * 60.0)   +
                         t->tm_sec) * fr->fps)
                + (long)((ts.tv_nsec / 1.0e9) * fr->fps);

            /* Compute current TC as total frames */
            long current_total = (long)(((tc.h * 3600.0) +
                                         (tc.m * 60.0)   +
                                          tc.s) * fr->fps)
                               + tc.f;

            long drift = expected_total_frames - current_total;

            /*
             * If drift exceeds ±1 frame, step-correct.
             * Smaller discrepancies are handled naturally by
             * the cumulative deadline in the timer loop.
             */
            if (drift > 1 || drift < -1) {
                /* Re-derive TC from wall clock */
                TC wall_tc;
                tc_from_clock(&wall_tc, fr);
                tc = wall_tc;

                /* Reset the frame deadline to now so the schedule
                 * re-anchors to the corrected time. */
                struct timespec ts_mono;
                clock_gettime(CLOCK_MONOTONIC, &ts_mono);
                deadline = ts_mono;

                fprintf(stderr,
                        "pitcgen: drift %+ld frames — resynced to %s\n",
                        drift, tc_format(&tc));
            }
        }

        /* ── Output current frame ──────────────────────────────── */
        if (output_frame(enc, &tc, fr, use_ub ? ubg : NULL,
                         conv, max_frame_samples, pcm) < 0)
            break;

        /* Advance timecode */
        tc_advance(&tc, fr);
        frame_count++;

        /* ── Sleep until next frame deadline ─────────────────────
         *
         * Use TIMER_ABSTIME so the deadline is absolute —
         * cumulative error from sleep overrun doesn't accumulate.
         */
        deadline.tv_nsec += (long)fr->frame_ns;
        while (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec++;
        }

        int sleep_ret;
        do {
            sleep_ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                        &deadline, NULL);
        } while (sleep_ret == EINTR && g_running);
    }

    /* ── Cleanup ───────────────────────────────────────────────── */
    snd_pcm_drain(pcm);
    free(conv);
    snd_pcm_close(pcm);
    ltc_encoder_free(enc);

    fprintf(stderr, "\npitcgen: stopped at %s\n", tc_format(&tc));
    return 0;
}
