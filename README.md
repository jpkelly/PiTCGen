# PiTCGen — Pi Timecode Generator

Broadcast-quality LTC SMPTE timecode generator appliance for Raspberry Pi. Targets the Ambient Lockit / Denecke SB-T class of hardware timecode generators.

---

## Features

### Core generator
- **6 frame rates:** 24, 25, 29.97, 29.97df, 30, 30df — per ANSI/SMPTE 12M-1986
- **Timer-based pacing:** `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` for sample-accurate long-term stability
- **Wall-clock resync:** every 1 second, corrects drift vs `CLOCK_REALTIME` (±1 frame threshold)
- **User bits:** 32-bit hex encoding or `"date"` (BCD YYMMDD)
- **ALSA output:** 3.5mm audio jack via USB dongle (dev); direct GPIO biphase-mark (production)
- **Configurable sample rate:** 8–192 kHz

### Clock source hierarchy — GPS → PTP → NTP → free-run
- **GPS + PPS** (primary): u-blox NEO-M8T timing module, ±50 ns accuracy via kernel PPS
- **PTPv2 / SMPTE ST 2059** (secondary): hardware-timestamped Ethernet on Pi 4/5, ±100 ns
- **NTP** (tertiary): internet pool, ±1–10 ms
- **Free-run holdover** (fallback): chronyd drift file, ±1 ms for hours after loss of all external sources
- Seamless fallback between sources via `chronyd` — the generator never knows which clock is driving it

### PTPv2 → LTC translation
- PTP-disciplined `CLOCK_REALTIME` is read directly by the generator — zero conversion latency
- SMPTE ST 2059-2 broadcast profile compatible (TAI epoch, no timezone distortion)
- Works with any PTPv2 grandmaster: Evertz, Meinberg, Telestream, or open-source `ptp4l`
- PTP status monitoring via `pmc` — grandmaster ID, offset, path delay exposed in shared memory

### Display
- Live timecode + clock-source status on I2C OLED (SSD1306) or character LCD (HD44780)
- Shows: current TC, frame rate, active clock source, GPS satellite count, PTP offset
- Source status icons: GPS lock ● (green), PTP lock ◇ (yellow), NTP ○ (white), free-run ~ (flashing)

### Control & monitoring
- **CLI:** `pitcgenctl` — start/stop, set frame rate, set timecode, set user bits, view status
- **Web UI** (planned): single-page app via `libmicrohttpd`, live TC readout, full control
- **systemd service:** `pitcgen.service` — boots as appliance, auto-restart on failure

### Appliance build
- Buildroot external tree — cross-compiles a bootable SD card image
- Two defconfigs: Pi 1 (armv6) and Pi 5 (aarch64)
- Filesystem overlay: systemd units, chronyd multi-source config, ptp4l profile, gpsd

---

## Documents

| File | What it covers |
|---|---|
| [`PLAN.md`](PLAN.md) | Full architecture, phases, component breakdown, build system, hardware decisions |
| [`HANDOFF.md`](HANDOFF.md) | Current status, next steps, Pi setup, reference materials |
| [`HARDWARE.md`](HARDWARE.md) | Compute modules, GPS, displays, audio output, wiring, BOM, enclosures |

## Quick start (on a Pi)

```bash
sudo apt-get install -y gcc make libltc-dev libasound2-dev
make
sudo make install
```

## Project structure

```
PiTCGen/
├── src/
│   ├── tc.h / tc.c         ← timecode math (standalone, no hardware deps)
│   ├── pitcgen.c            ← main generator (ALSA + libltc + timer loop)
│   ├── ipc.h / ipc.c        ← IPC layer (shared memory + Unix socket)
│   └── server.c             ← HTTP server (libmicrohttpd)
├── components/              ← future: per-component dirs with tests
│   ├── libtc/               ← timecode math library + unit tests
│   ├── libipc/              ← IPC library + integration tests
│   ├── core/                ← generator daemon
│   ├── server/              ← HTTP control server
│   ├── display/             ← OLED/LCD display daemon
│   ├── ptpmon/              ← PTP status monitor
│   └── gpsmon/              ← GPS status monitor
├── buildroot/               ← Buildroot external tree
├── config/                  ← chrony, ptp4l, gpsd configs
├── pitcgenctl               ← CLI control script
├── pitcgen.service          ← systemd unit
└── pitcgen.env.example      ← env config template
```

## Frame rates

| Name | FPS | Drop? | Use case |
|---|---|---|---|
| 24 | 24.0 | No | Film |
| 25 | 25.0 | No | PAL/SECAM video, European audio (no DF complications) |
| 29.97 | 30000/1001 | No | NTSC short-form, drifts ~1.8s/hr vs wall clock |
| 29.97df | 30000/1001 | Yes | NTSC clock-critical, ±75ms/24h residual error |
| 30 | 30.0 | No | US audio-only (true 30fps, not video-compatible) |
| 30df | 30.0 | Yes | Audio timing with DF counting |

User bits supported on all frame rates — hex encoding (8 digits, 32 bits) or date (BCD YYMMDD).

## References

- ANSI/SMPTE 12M-1986 — reproduced in Cipher Digital *Time Code Handbook* (1987)
- [Philip Rees — Synchronisation and SMPTE timecode](https://www.philrees.co.uk/articles/timecode.htm)
- SMPTE ST 2059-2 — PTP profile for broadcast
- [libltc](https://github.com/x42/libltc) — Linear Timecode encoder/decoder library
- [linuxptp](https://linuxptp.sourceforge.net/) — PTPv2 implementation (`ptp4l`, `phc2sys`, `pmc`)

## Origin

Extracted from [PiTG](https://github.com/jpkelly/PiTG), a multi-service LTC + serial bridge project for live production. PiTCGen is the dedicated timecode generator, evolved into a professional appliance.
