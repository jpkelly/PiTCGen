# PiTCGen — Project Plan

Broadcast-quality LTC SMPTE timecode generator appliance for Raspberry Pi.

---

## Vision

A professional Linear Timecode (LTC) generator that runs on a Raspberry Pi and boots as a self-contained appliance. The feature set targets the Ambient Lockit / Denecke SB-T class of hardware generators. Control via web browser and local CLI. Multi-source clock disciplining: GPS + PTPv2 + NTP, with seamless fallback. Local OLED/LCD display showing live timecode and clock-source status. Support from Pi 1 (700 MHz, 512 MB) through Pi 5.

For hardware selection, wiring, and BOM see **[HARDWARE.md](HARDWARE.md)**.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           PiTCGen Appliance                               │
│                                                                          │
│  Clock sources (chronyd → CLOCK_REALTIME):                               │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐                             │
│  │ GPS+PPS  │   │ PTPv2    │   │ NTP pool │                             │
│  │ u-blox   │   │ ptp4l    │   │ chronyd  │                             │
│  │ M8T      │   │ ST 2059  │   │ pool     │                             │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘                             │
│       │              │              │                                     │
│       └──────────────┼──────────────┘                                     │
│                      │                                                    │
│                      ▼  CLOCK_REALTIME (PTP/GPS/NTP disciplined)          │
│                      │                                                    │
│  ┌───────────────────┼────────────────────────────────────┐              │
│  │                   ▼                                     │              │
│  │  ┌──────────┐  shared memory   ┌──────────────────┐   │              │
│  │  │ pitcgen   │ ◄─── mmap ────► │  pitcgen-server   │   │              │
│  │  │(generator)│                  │  (HTTP daemon)    │   │              │
│  │  │           │ ◄─ Unix socket ─►│                   │   │              │
│  │  │ - libltc  │   (commands)     │  - libmicrohttpd  │   │              │
│  │  │ - ALSA    │                  │  - serves SPA     │   │              │
│  │  └─────┬─────┘                  └────────┬──────────┘   │              │
│  │        │ USB audio / GPIO                  │ port 80        │              │
│  │        ▼                                  ▼                │              │
│  │  ┌─────────┐                    ┌──────────────────┐      │              │
│  │  │LTC out  │                    │ Web browser (LAN)│      │              │
│  │  └─────────┘                    └──────────────────┘      │              │
│  │                                                            │              │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                │              │
│  │  │pitcgenctl│  │ gpsmon   │  │ ptpmon   │                │              │
│  │  │(CLI ctl) │  │(GPS stat)│  │(PTP stat)│                │              │
│  │  └──────────┘  └────┬─────┘  └────┬─────┘                │              │
│  │                     │             │                        │              │
│  │                     └──────┬──────┘                        │              │
│  │                            ▼  shared memory status         │              │
│  │                     ┌──────────────┐                       │              │
│  │                     │  displayd     │                       │              │
│  │                     │ (OLED/LCD)    │                       │              │
│  │                     └──────────────┘                       │              │
│  └────────────────────────────────────────────────────────────┘              │
└──────────────────────────────────────────────────────────────────────────┘
```

### IPC design

| Mechanism | Purpose | Details |
|---|---|---|
| **Shared memory** | Live TC readout + clock-source status | mmap'd at `/dev/shm/pitcgen-status`. Lock-free struct written by generator every frame, read by HTTP server, displayd, ptpmon, and gpsmon. Contains: TC, fps, mode, hold state, user bits, active clock source, GPS lock/sats/PPS offset, PTP lock/GM identity/offset/delay. |
| **Unix socket** | Commands | Datagram socket at `/run/pitcgen.sock`. Generator listens; `pitcgenctl` and HTTP server send commands (start, stop, hold, set-fps, set-ub, set-tc, jam). |

### Clock source hierarchy

PiTCGen reads `CLOCK_REALTIME` for its timecode source — it is **clock-source agnostic**. chronyd disciplines `CLOCK_REALTIME` from multiple sources with automatic priority and fallback:

| Priority | Source | Accuracy | Hardware | Fallback triggers |
|---|---|---|---|---|
| 1 (best) | GPS + PPS | ±50 ns | u-blox NEO-M8T via GPIO PPS | Sky view lost, <4 satellites |
| 2 | PTPv2 (ST 2059) | ±100 ns | Pi 4/5 native Ethernet HW timestamping | Network down, grandmaster lost |

See [HARDWARE.md](HARDWARE.md) for GPS module selection, antenna recommendations, wiring, and kernel PPS configuration.
| 3 | NTP pool | ±1–10 ms | Internet | Network down |
| 4 (last) | Free-run | ±1–2 s/day drift file holdover | Pi RTC/oscillator | All external sources lost |

**Key design property:** the generator (`pitcgen.c`) never knows which source is active. It just reads `CLOCK_REALTIME` — same code path for all sources. This means PTP → LTC translation and GPS → LTC translation are zero-code-change features. The `tc_from_clock()` call and the 1-second resync loop already handle it.

### PTPv2 integration

- **Daemon:** `ptp4l` (from linuxptp) with `--profile=smpte` for SMPTE ST 2059-2 broadcast profile
- **Clock sync:** `phc2sys` copies the PTP Hardware Clock (PHC) → `CLOCK_REALTIME`
- **Monitoring:** `ptpmon` daemon reads PTP stats via `pmc` and writes to shared memory (lock state, GM identity, mean path delay, offset from master)
- **Hardware requirement:** Pi 4 or Pi 5 for hardware timestamping. Pi 1–3 route Ethernet through USB — software timestamping only (~10–100 µs jitter, unacceptable for broadcast PTP). See [HARDWARE.md](HARDWARE.md) for Pi model comparison and carrier board notes for CM5.
- **ST 2059 TAI path:** Optional `tc_from_tai()` function for SMPTE-epoch-aligned timecode without timezone/leap-second distortion

### Frame rate support

| Name | FPS | Drop? | SMPTE standard |
|---|---|---|---|
| 24 | 24.0 | No | LTC_TV_FILM_24 |
| 25 | 25.0 | No | LTC_TV_625_50 |
| 29.97 | 30000/1001 | No | LTC_TV_525_60 |
| 29.97df | 30000/1001 | Yes | LTC_TV_525_60 |
| 30 | 30.0 | No | LTC_TV_525_60 |
| 30df | 30.0 | Yes | LTC_TV_525_60 |

---

## Phases

### Phase 1 — Core generator ✅ (written, not yet compiled)

- Timer-based pacing via `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`
- Wall-clock resync every 1s via `CLOCK_REALTIME` (clock-source agnostic — works with NTP, PTP, or GPS)
- 6 frame rates per ANSI/SMPTE 12M-1986: 24, 25, 29.97, 29.97df, 30, 30df
- Drop-frame logic confirmed against Cipher Digital *Time Code Handbook* (1987) and Philip Rees article
- User bits via `-u` flag (hex or `"date"`)
- Source structured into `src/tc.h`, `src/tc.c`, `src/pitcgen.c`

**Needs:** Pi build host to compile and test.

### Phase 2 — IPC layer

- Shared memory status block in `/dev/shm/pitcgen-status`
- Extended status struct with clock-source fields (active source, GPS lock, PTP offset, etc.)
- Unix datagram socket at `/run/pitcgen.sock`
- Generator acts as IPC server
- `pitcgenctl` rewritten to send commands via UDS instead of env file + systemctl restart
- `ipc.h` / `ipc.c` in `src/`

### Phase 3 — Web interface

- HTTP server using `libmicrohttpd`
- Single-page HTML + CSS + JS, served on port 80
- Live TC readout via polling shared memory
- Controls: start, stop, hold, set FPS, set user bits, set start TC
- Clock-source status display (GPS lock, PTP offset, active source)
- Lightweight — no frameworks, no Python, no Node

### Phase 4 — Display

- I2C OLED (SSD1306, 128×64) or character LCD (HD44780, 20×4) driver
- `displayd` daemon reads shared memory, drives display
- Shows: live TC, frame rate, active clock source with icon, GPS sats, PTP offset
- Source status icons: GPS ● (green), PTP ◇ (yellow), NTP ○ (white), free-run ~ (flashing)

See [HARDWARE.md](HARDWARE.md) for display comparison (SSD1306 vs HD44780 vs ST7789), wiring, and library options.

### Phase 5 — GPS integration

- u-blox NEO-M8T timing module via UART + PPS GPIO
- Kernel PPS via `dtoverlay=pps-gpio`
- `gpsmon` daemon reads `gpsd` socket + `/dev/pps0`, writes status to shared memory
- chronyd configured with `refclock SHM` + `refclock PPS` as primary source

See [HARDWARE.md](HARDWARE.md) for GPS module comparison (M8T vs M9N vs M6M), antenna selection, wiring diagram, and `/boot/firmware/config.txt` setup.

### Phase 6 — PTPv2 integration

- `ptp4l` + `phc2sys` from linuxptp, SMPTE ST 2059-2 profile
- `ptpmon` daemon reads PTP stats via `pmc`, writes to shared memory
- chronyd configured with `refclock PHC` as secondary source
- Requires Pi 4/5 for hardware timestamping

### Phase 7 — Buildroot appliance

- External Buildroot tree (`buildroot/`)
- Two defconfigs: Pi 1 (armv6) and Pi 5 (aarch64)
- Custom package recipe `package/pitcgen/pitcgen.mk`
- Filesystem overlay: systemd units, chronyd multi-source config, ptp4l profile, gpsd config, pps-gpio overlay
- Builds a bootable SD card image

### Component breakdown — individually testable

Each component is its own build target with its own test suite:

| # | Component | Deps | Testable on | Test strategy |
|---|---|---|---|---|
| 1 | **libtc** | libc only | Any machine (macOS, Linux, CI) | Pure unit tests: frame advance, DF skip rules, parse/format round-trip, UB encoding, wall-clock init |
| 2 | **libipc** | libc + Linux | Any Linux machine | Integration tests: mock client/server over `/tmp` socket, shmem read/write |
| 3 | **core** | libtc, libltc, ALSA | Pi only (HW); Linux CI (mock) | Mock-ALSA timer tests; real LTC output verified by external decoder |
| 4 | **server** | libipc, libmicrohttpd | Any Linux machine | HTTP integration tests: curl against test instance, TC via shmem polling |
| 5 | **displayd** | libipc, I2C lib | Pi only | Visual verification; mock-I2C unit tests for text formatting |
| 6 | **gpsmon** | gpsd, libipc | Any Linux with GPS | Reads gpsd JSON, writes shmem — testable with recorded NMEA replay |
| 7 | **ptpmon** | pmc, libipc | Any Linux with ptp4l | Reads `pmc` output, writes shmem — testable with recorded PTP stats |
| 8 | **ctl** | shell + systemd | Pi only | bats shell-spec tests: flag parsing, config read/write, service lifecycle |
| 9 | **buildroot** | all of the above | Pi only | Full appliance integration: build → boot → smoke test |

---

## Build system

### Native (Pi OS)

```bash
# One-time deps
sudo apt-get install -y gcc make libltc-dev libasound2-dev libmicrohttpd-dev

# Build everything
make

# Install
sudo make install
sudo make install-service
```

### Cross (Buildroot)

```bash
cd buildroot
make pitcgen_pi5_defconfig
make
# outputs: output/images/sdcard.img
```

### Makefile targets

| Target | Description |
|---|---|
| `all` | Build `pitcgen` (and later `pitcgen-server`) |
| `pitcgen` | Generator binary only |
| `deps` | Install apt dependencies |
| `install` | Copy binaries to `/usr/local/bin/` |
| `install-service` | Install systemd unit + enable service |
| `check` | Syntax-check files that don't need ALSA headers |
| `clean` | Remove build artifacts |

---

## Git & GitHub best practices

### Branch strategy

```
main          — stable, deployable code
├── dev       — integration branch for active work
├── phase-1-core
├── phase-2-ipc
├── phase-3-web
├── phase-4-display
├── phase-5-gps
├── phase-6-ptp
└── phase-7-buildroot
```

- `main` is always deployable (or image-buildable)
- Feature work happens on short-lived branches off `dev`
- Merge via pull requests with linear history (rebase + merge, not merge commit)

### Commits

- **Atomic:** one logical change per commit. A commit should leave the project in a working state (even if incomplete).
- **Messages:** `type: short description` (e.g. `feat: add 30df frame rate`, `fix: correct ALSA xrun recovery`). Conventional Commits style.
- **Scope:** prefix with scope when relevant (`tc:`, `pitcgen:`, `ipc:`, `server:`, `build:`, `docs:`).

### Workflow

1. Create feature branch from `dev`
2. Code, compile, test on Pi
3. Push branch, open PR into `dev`
4. Review, squash-merge into `dev`
5. When `dev` is stable, fast-forward merge into `main`
6. Tag releases from `main` (e.g. `v1.0.0`)

### After Pi access is set up

```bash
# Initial clone on Pi
git clone git@github.com:jpkelly/PiTCGen.git
cd PiTCGen
git checkout dev

# Iterate
# ... edit on Mac, push to GitHub ...
git pull origin dev   # on Pi
make clean && make
```

---

## File layout

```
PiTCGen/
├── src/
│   ├── tc.h            ← timecode types + API
│   ├── tc.c            ← timecode math
│   ├── pitcgen.c       ← main generator (ALSA + libltc + timer)
│   ├── ipc.h           ← IPC types + API (Phase 2)
│   ├── ipc.c           ← shared memory + Unix socket (Phase 2)
│   └── server.c        ← HTTP server (Phase 3)
├── components/              ← Phase 2+: per-component dirs with tests
│   ├── libtc/               ← timecode math library + unit tests
│   ├── libipc/              ← IPC library + integration tests
│   ├── core/                ← generator daemon
│   ├── server/              ← HTTP control server
│   ├── display/             ← OLED/LCD display daemon (Phase 4)
│   ├── gpsmon/              ← GPS status monitor (Phase 5)
│   └── ptpmon/              ← PTP status monitor (Phase 6)
├── config/                  ← appliance config files
│   ├── chrony.conf          ← multi-source: GPS + PTP + NTP
│   ├── ptp4l.conf           ← PTP ST 2059-2 profile
│   └── gpsd.conf
├── Makefile
├── CMakeLists.txt           ← planned: per-component builds + CTest
├── pitcgenctl               ← CLI control script
├── pitcgen.service          ← systemd unit
├── pitcgen.env.example      ← env config template
├── HANDOFF.md               ← build/setup notes
├── PLAN.md                  ← this file
├── buildroot/               ← external Buildroot tree (Phase 7)
│   ├── configs/
│   │   ├── pitcgen_pi1_defconfig
│   │   └── pitcgen_pi5_defconfig
│   ├── package/pitcgen/
│   │   ├── Config.in
│   │   └── pitcgen.mk
│   └── overlay/
│       ├── etc/systemd/system/pitcgen.service
│       ├── etc/chrony/chrony.conf
│       ├── etc/linuxptp/ptp4l.conf
│       ├── etc/default/pitcgen
│       └── etc/default/gpsd
└── README.md               ← project overview + feature list
```

---

## Key decisions & rationale

| Decision | Choice | Why |
|---|---|---|
| Timer strategy | `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` + periodic CLOCK_REALTIME resync | Avoids drift accumulation. ALSA write errors don't break timing. Clock-source agnostic — works with NTP, PTP, or GPS. |
| Clock disciplining | chronyd multi-source: GPS+PPS → PTP → NTP → free-run | Seamless fallback. Generator never knows which source is active. chronyd handles priority, weighting, and holdover. |
| PTP | linuxptp (`ptp4l` + `phc2sys`), SMPTE ST 2059-2 profile | Industry-standard broadcast PTP. Pi 4/5 native HW timestamping. No custom kernel modules. |
| GPS module | u-blox NEO-M8T (timing-grade) | ±20ns PPS jitter. Stationary/survey-in mode. ~$25 breakout. Active antenna for indoor use. |
| Display | I2C SSD1306 OLED (128×64) or HD44780 LCD (20×4) | I2C needs only 4 wires, doesn't conflict with audio/Ethernet. OLED draws ~20mA. LCD gives classic broadcast look. |
| Web stack | C + libmicrohttpd | Pi 1 compatible (700 MHz, 512 MB). No Python/Node overhead. Single binary. |
| IPC | Shared memory (read) + Unix socket (commands) | Zero-copy live TC readout. No serialisation overhead. POSIX-native. Extended struct carries clock-source status for display/monitoring. |
| LTC output (dev) | USB audio dongle | Plug-and-play, no hardware mods, test timer/IPC logic |
| LTC output (final) | Direct GPIO biphase-mark encoding | No DAC, no dongle, minimal BoM, clean appliance |
| Buildroot | External tree in repo | Self-contained build. Easy to reproduce. Tracks with the source. |
| Branch model | `main` + `dev` + feature branches | Main is always releasable. Dev is integration. PRs for review. |
| Dependencies | libltc, libasound2, libmicrohttpd (dev); libltc, libgpiod (final); linuxptp, gpsd, chronyd (appliance) | All available in apt and Buildroot. No custom porting. |
| Reference spec | ANSI/SMPTE 12M-1986 (Cipher Digital *Time Code Handbook*, 1987) | Authoritative bit-level LTC specification. Confirms DF rules, status flag bits, residual error characteristics. |

---

---

## LTC output architecture

### Dev track — USB audio dongle (Phase 1–3)

```
pitcgen
├── libltc: encode LTCFrame
├── ltc_encoder_encode_frame → PCM samples
└── ALSA: snd_pcm_writei → USB dongle → 3.5mm → XLR adapter
```

Existing `src/pitcgen.c` works as-is. Just pass `-d hw:Device,0` to select the dongle.

### Production track — direct GPIO (Phase 4)

```
pitcgen
├── libltc: encode LTCFrame (reused — same timecode math)
├── biphase_encode() → GPIO toggle (new module, ~50 lines)
└── clock_nanosleep timer (reused — same deadline scheduling)
```

The Pi 5 GPIO toggles at microsecond precision — easily enough for 2000 baud LTC. The output is a clean 0/3.3V digital biphase-mark waveform. A simple resistive divider knocks it down to ~1Vpp for XLR/BNC line level.

**Hardware chain:**
```
Pi 5 GPIO ──┬─ 1kΩ ──┬── XLR pin 2 (hot)
            │        │
            └─ 680Ω ─┤── XLR pin 3 (cold)
                     │
                    ─── XLR pin 1 (GND)
```

This gives a balanced ~1Vpp signal meeting the SMPTE LTC standard. No DAC, no audio driver, no I2S — the whole generator runs off a single GPIO pin.

See [HARDWARE.md](HARDWARE.md) for complete wiring reference, XLR connector part numbers, and production BOM.

### Code structure

Both paths share the same `main()` timer loop, wall-clock resync, and IPC. Only the `output_frame()` function differs:

```c
// In src/output-alsa.c  (dev)
int output_frame(LTCEncoder *enc, const TC *tc, ...);

// In src/output-gpio.c   (production)
int output_frame(LTCEncoder *enc, const TC *tc, ...);
```

Selected at build time via `make OUTPUT=alsa` or `make OUTPUT=gpio`, or linked automatically depending on available libraries.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| ALSA xrun on Pi 1 at high sample rates | Test with different period/buffer sizes. Fallback to `plughw` if `default` gives high latency. |
| Pi 1 (ARMv6) no longer supported by latest Raspberry Pi OS | Pin to Debian Bookworm (last release with armv6 kernel). Buildroot bypasses this entirely. |
| Pi 1/2/3 Ethernet via USB — no HW PTP timestamping | Accept software timestamping (~10–100 µs) for non-broadcast use. Pi 4/5 required for ST 2059 compliance. |
| GPS loses lock indoors | chronyd falls back to PTP → NTP → drift file. Free-run holds ±1ms for hours. Display shows warning icon. |
| PTP grandmaster unavailable | chronyd falls back to NTP or GPS. No generator restart needed — CLOCK_REALTIME just switches source silently. |
| Shared memory polling adds latency to TC display | Poll at 10 Hz (100 ms) — more than enough for human-readable display. Generator is unaffected. |
| Drop-Frame residual error (±75ms/24h, ANSI/SMPTE 12M-1986) | PiTCGen's wall-clock resync loop corrects this automatically. Vanilla DF generators accumulate ~2s/month without external reset; PiTCGen stays locked to the reference clock. |
| NTP not available on isolated network | GPS or local PTP grandmaster provides reference. chronyd free-runs on drift file as last resort. |
