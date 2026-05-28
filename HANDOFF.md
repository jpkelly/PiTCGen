# PiTCGen — Handoff

## Status
Phase 1 core generator is written but **not compiled** — needs a Raspberry Pi with ALSA + libltc.
Architecture, component breakdown, and phases through Phase 7 are fully planned.

## What exists

| File | Purpose |
|---|---|
| `src/tc.h` | Timecode types, frame rate table, user bits API |
| `src/tc.c` | Timecode math (advance, parse, format, wall-clock init, ub_parse) |
| `src/pitcgen.c` | Main generator: timer-paced loop, ALSA output, wall-clock resync |
| `Makefile` | Builds `pitcgen` from `src/tc.c` + `src/pitcgen.c`, `-lasound -lltc` |
| `pitcgenctl` | Shell script to control the service (env-file based; will be UDS in Phase 2) |
| `pitcgen.service` | systemd unit |
| `pitcgen.env.example` | Environment config template |
| `PLAN.md` | Full architecture, 9-component breakdown, clock source hierarchy (GPS→PTP→NTP→free-run) |
| `README.md` | Project overview with feature list |
| `HARDWARE.md` | Compute modules (Pi 5, CM5), GPS modules, displays, wiring, BOM, enclosures |

## Reference materials collected

| Reference | Key content |
|---|---|
| **Cipher Digital *Time Code Handbook* (1987)** | Reproduces ANSI/SMPTE 12M-1986. Authoritative 80-bit LTC frame layout, status flag bits (10, 11, 27, 43, 58, 59), DF residual error (±75ms/24h), EBU comparison. In repo as PDF. |
| **Philip Rees — SMPTE timecode article** | Drop-frame boundary examples, bi-phase mark modulation, signal levels, rise time (25µs ±5µs). Online at philrees.co.uk. |

## New vs original PiTG
- **Timer-based pacing:** `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` instead of ALSA blocking
- **Wall-clock resync:** Every 1s, checks CLOCK_REALTIME against TC counter, corrects if drift > ±1 frame
- **Clock-source agnostic:** Same code path works with NTP, PTPv2 (ST 2059), or GPS+PPS — just discipline CLOCK_REALTIME with chronyd
- **6 frame rates:** 24, 25, 29.97, 29.97df, 30, 30df
- **User bits** via `-u` flag (hex string or "date")
- Source moved to `src/` directory

## Planned components (Phases 2–7)

| # | Component | Phase | Deps | Testable on |
|---|---|---|---|---|
| 1 | **libtc** — timecode math library + tests | 1+ | libc only | macOS, Linux CI |
| 2 | **libipc** — shared memory + Unix socket | 2 | libc + Linux | Any Linux |
| 3 | **core** — generator daemon | 1 | libtc, libltc, ALSA | Pi hardware |
| 4 | **server** — HTTP control server | 3 | libipc, libmicrohttpd | Any Linux |
| 5 | **displayd** — OLED/LCD display driver | 4 | libipc, I2C lib | Pi hardware |
| 6 | **gpsmon** — GPS status monitor | 5 | gpsd, libipc | Any Linux + GPS |
| 7 | **ptpmon** — PTP status monitor | 6 | pmc, libipc | Any Linux + ptp4l |
| 8 | **ctl** — CLI control (pitcgenctl) | 2+ | shell + systemd | Pi hardware |
| 9 | **buildroot** — appliance image | 7 | all above | Pi hardware |

## Hardware notes

For full hardware details — GPS modules, displays, Pi model comparison, wiring diagrams, enclosures, and complete BOM — see **[HARDWARE.md](HARDWARE.md)**.

### Pi model requirements
| Feature | Pi 1 | Pi 3 | Pi 4 | Pi 5 |
|---|---|---|---|---|
| LTC generation (ALSA) | ✅ | ✅ | ✅ | ✅ |
| LTC generation (GPIO) | ✅ | ✅ | ✅ | ✅ |
| PTPv2 HW timestamping | ❌ (USB Eth) | ❌ (USB Eth) | ✅ | ✅ |
| GPS PPS GPIO | ✅ | ✅ | ✅ | ✅ |
| Display (I2C) | ✅ | ✅ | ✅ | ✅ |
| Web UI | ✅ | ✅ | ✅ | ✅ |

### Recommended BOM for production appliance
- Raspberry Pi 5 (or Pi 4 for budget)
- u-blox NEO-M8T GPS module + active antenna (~$25 + $10)
- SSD1306 OLED 128×64 I2C (~$5) or HD44780 20×4 LCD (~$8)
- USB audio dongle (dev) or 1kΩ+680Ω resistors + XLR connector (production GPIO output)

## Next steps
1. **Set up Pi 5 build host** — install deps, clone repo, run `make`
2. **Fix any build errors** — the code hasn't been compiled yet
3. **Test generator** — run pitcgen, verify LTC output with external reader
4. **Write libtc tests** — pure C, runs on Mac without Pi. Test DF boundaries, parse/format, UB encoding using Cipher Digital reference vectors
5. **Phase 2** — Implement IPC layer (shared memory + Unix socket)
6. **Phase 3** — Web interface (libmicrohttpd HTTP server + single-page UI)
7. **Phase 4** — Display driver (I2C OLED/LCD)
8. **Phase 5** — GPS integration (gpsmon + chronyd config)
9. **Phase 6** — PTPv2 integration (ptpmon + linuxptp config)
10. **Phase 7** — Buildroot external tree for bootable SD card appliance image

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
