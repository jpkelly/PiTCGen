# PiTCGen — Pi Timecode Generator

Broadcast-quality LTC SMPTE timecode generator for Raspberry Pi.

Supports Pi 5 (primary target) through Pi 1. Two-track output: USB audio dongle for development, direct GPIO biphase-mark encoding for the production appliance.

---

## Documents

| File | What it covers |
|---|---|
| [`PLAN.md`](PLAN.md) | Full architecture, phases, build system, IPC design, hardware decisions |
| [`HANDOFF.md`](HANDOFF.md) | Current status, next steps, Pi setup commands |

## Quick start (on a Pi)

```bash
sudo apt-get install -y gcc make libltc-dev libasound2-dev
make
sudo make install
```

## Project structure

```
src/
├── tc.h / tc.c         ← timecode math (standalone, no hardware deps)
├── pitcgen.c            ← main generator (ALSA + libltc + timer loop)
├── ipc.h / ipc.c        ← IPC layer (shared memory + Unix socket)
└── server.c             ← HTTP server (libmicrohttpd)
```

## Frame rates

24, 25, 29.97, 29.97df, 30, 30df — all with user bits support.

## Origin

Extracted from [PiTG](https://github.com/jpkelly/PiTG), a multi-service LTC + serial bridge project for live production. PiTCGen is the dedicated timecode generator, evolved into a professional appliance.
