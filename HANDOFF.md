# PiTCGen — Handoff

## Status
Phase 1 core is written but **not compiled** — needs a Raspberry Pi with ALSA + libltc.

## What exists

| File | Purpose |
|---|---|
| `src/tc.h` | Timecode types, frame rate table, user bits API |
| `src/tc.c` | Timecode math (advance, parse, format, wall-clock init, ub_parse) |
| `src/pitcgen.c` | Main generator: timer-paced loop, ALSA output, wall-clock resync |
| `Makefile` | Builds `pitcgen` from `src/tc.c` + `src/pitcgen.c`, `-lasound -lltc` |
| `pitcgenctl` | Shell script to control the service (works as-is) |
| `pitcgen.service` | systemd unit |
| `pitcgen.env.example` | Environment config template |

## New vs original PiTG
- **Timer-based pacing:** `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` instead of ALSA blocking
- **Wall-clock resync:** Every 1s, checks CLOCK_REALTIME against TC counter, corrects if drift > ±1 frame
- **30df** frame rate added
- **User bits** via `-u` flag (hex string or "date")
- Source moved to `src/` directory

## Next steps
1. **Set up Pi 5 build host** — install deps, clone repo, run `make`
2. **Fix any build errors** — the code hasn't been compiled yet
3. **Test generator** — run pitcgen, verify LTC output
4. **Phase 2** — Implement IPC layer (shared memory + Unix socket)
5. **Phase 3** — Web interface (libmicrohttpd HTTP server + single-page UI)
6. **Phase 4** — Buildroot external tree for appliance image

## Pi 5 setup
```bash
sudo apt-get update && sudo apt-get install -y gcc make libltc-dev libasound2-dev git
```

## Build
```bash
cd PiTCGen && make clean && make
```

## Key contacts
- Repo: https://github.com/jpkelly/PiTCGen (private)
- Previous project: https://github.com/jpkelly/PiTG
