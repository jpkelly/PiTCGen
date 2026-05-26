/* tc.c — Timecode math implementation */

#include "tc.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Frame rate table ────────────────────────────────────────────── */

const FrameRate FRAME_RATES[] = {
    /* name       fps               ifps  drop  frame_ns */
    { "24",       24.0,              24,   0,    41666666ULL },
    { "25",       25.0,              25,   0,    40000000ULL },
    { "29.97",    30000.0 / 1001.0,  30,   0,    33366666ULL },
    { "29.97df",  30000.0 / 1001.0,  30,   1,    33366666ULL },
    { "30",       30.0,              30,   0,    33333333ULL },
    { "30df",     30.0,              30,   1,    33333333ULL },
    { NULL, 0, 0, 0, 0 }  /* sentinel */
};

const int N_FRAME_RATES = (int)(sizeof(FRAME_RATES) / sizeof(FRAME_RATES[0])) - 1;

const FrameRate *fr_find(const char *name)
{
    for (int i = 0; FRAME_RATES[i].name; i++)
        if (strcmp(name, FRAME_RATES[i].name) == 0)
            return &FRAME_RATES[i];
    return NULL;
}

/* ── Timecode advance ────────────────────────────────────────────── */

void tc_advance(TC *tc, const FrameRate *fr)
{
    tc->f++;
    if (tc->f < fr->ifps)
        return;

    tc->f = 0;
    tc->s++;
    if (tc->s < 60)
        return;

    tc->s = 0;
    tc->m++;

    /*
     * Drop-frame: skip frames 00 and 01 at the start of every minute
     * EXCEPT multiples of 10 minutes.
     */
    if (fr->drop && (tc->m % 10) != 0)
        tc->f = 2;

    if (tc->m < 60)
        return;

    tc->m = 0;
    tc->h = (tc->h + 1) % 24;
}

/* ── Parse "HH:MM:SS:FF" ─────────────────────────────────────────── */

int tc_parse(TC *tc, const char *s)
{
    if (sscanf(s, "%d:%d:%d:%d", &tc->h, &tc->m, &tc->s, &tc->f) != 4)
        return 0;
    return (tc->h >= 0 && tc->h < 24 &&
            tc->m >= 0 && tc->m < 60 &&
            tc->s >= 0 && tc->s < 60 &&
            tc->f >= 0);
}

/* ── Initialise from wall clock ──────────────────────────────────── */

void tc_from_clock(TC *tc, const FrameRate *fr)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);

    tc->h = t->tm_hour;
    tc->m = t->tm_min;
    tc->s = t->tm_sec;
    tc->f = (int)((ts.tv_nsec / 1.0e9) * fr->fps);

    /* Clamp to valid range */
    int maxf = fr->ifps;
    if (tc->f >= maxf) tc->f = maxf - 1;
    if (tc->f < 0)     tc->f = 0;
}

/* ── Format into static buffer ───────────────────────────────────── */

const char *tc_format(const TC *tc)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d:%02d",
             tc->h, tc->m, tc->s, tc->f);
    return buf;
}

/* ── User bits parsing ───────────────────────────────────────────── */

int ub_parse(const char *s,
             unsigned char *ubg1, unsigned char *ubg2,
             unsigned char *ubg3, unsigned char *ubg4)
{
    /* "date" — encode today's date as BCD YYMMDD */
    if (strcmp(s, "date") == 0) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int yr  = t->tm_year % 100;
        int mon = t->tm_mon + 1;
        int day = t->tm_mday;

        *ubg1 = (unsigned char)(((yr  / 10) << 4) | (yr  % 10));
        *ubg2 = (unsigned char)(((mon / 10) << 4) | (mon % 10));
        *ubg3 = (unsigned char)(((day / 10) << 4) | (day % 10));
        *ubg4 = 0x00;                   /* spare */
        return 0;
    }

    /* Hex string: accept "0A1B2C3D" or "0a1b2c3d" or "0x…" prefix */
    const char *p = s;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;

    unsigned int v = 0;
    if (sscanf(p, "%8x", &v) != 1)
        return -1;

    /* LTC stores user bits little-endian across 4 groups */
    *ubg1 = (unsigned char)((v >> 24) & 0xFF);
    *ubg2 = (unsigned char)((v >> 16) & 0xFF);
    *ubg3 = (unsigned char)((v >>  8) & 0xFF);
    *ubg4 = (unsigned char)( v        & 0xFF);
    return 0;
}
