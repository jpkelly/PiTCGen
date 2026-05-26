# PiTCGen — Project Plan

Broadcast-quality LTC SMPTE timecode generator appliance for Raspberry Pi.

---

## Vision

A professional Linear Timecode (LTC) generator that runs on a Raspberry Pi and boots as a self-contained appliance. The feature set targets the Ambient Lockit / Denecke SB-T class of hardware generators. Control via web browser and local CLI. Support from Pi 1 (700 MHz, 512 MB) through Pi 5.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      PiTCGen Appliance                              │
│                                                                     │
│  ┌──────────────┐   shared memory    ┌────────────────┐            │
│  │  pitcgen      │ ◄──── mmap ─────► │  pitcgen-server │            │
│  │  (generator)  │                    │  (HTTP daemon)  │            │
│  │               │ ◄─ Unix socket ──► │                 │            │
│  │  - libltc     │    (commands)      │  - libmicrohttpd│            │
│  │  - ALSA out   │                    │  - serves SPA   │            │
│  └──────┬───────┘                    └────────┬───────┘            │
│         │ 3.5mm audio                         │ port 80             │
│         ▼                                      ▼                     │
│  ┌────────────┐                     ┌──────────────────┐            │
│  │ LTC signal │                     │ Web browser (LAN)│            │
│  └────────────┘                     └──────────────────┘            │
│                                                                     │
│  ┌──────────────┐                                                  │
│  │ pitcgenctl    │ ← CLI control (UDS client)                      │
│  └──────────────┘                                                  │
└─────────────────────────────────────────────────────────────────────┘
```

### IPC design

| Mechanism | Purpose | Details |
|---|---|---|
| **Shared memory** | Live TC readout | mmap'd at `/dev/shm/pitcgen-status`. Lock-free struct written by generator every frame, read by HTTP server on each poll. Contains: TC, fps, mode, hold state, user bits. |
| **Unix socket** | Commands | Datagram socket at `/run/pitcgen.sock`. Generator listens; `pitcgenctl` and HTTP server send commands (start, stop, hold, set-fps, set-ub, set-tc, jam). |

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
- Wall-clock resync every 1s via `CLOCK_REALTIME` (NTP-disciplined)
- 30df frame rate
- User bits via `-u` flag (hex or `"date"`)
- Source structured into `src/tc.h`, `src/tc.c`, `src/pitcgen.c`

**Needs:** Pi build host to compile and test.

### Phase 2 — IPC layer

- Shared memory status block in `/dev/shm/pitcgen-status`
- Unix datagram socket at `/run/pitcgen.sock`
- Generator acts as IPC server
- `pitcgenctl` rewritten to send commands via UDS instead of env file + systemctl restart
- `ipc.h` / `ipc.c` in `src/`

### Phase 3 — Web interface

- HTTP server using `libmicrohttpd`
- Single-page HTML + CSS + JS, served on port 80
- Live TC readout via polling shared memory
- Controls: start, stop, hold, set FPS, set user bits, set start TC
- NTP status display
- Lightweight — no frameworks, no Python, no Node

### Phase 4 — Buildroot appliance

- External Buildroot tree (`buildroot/`)
- Two defconfigs: Pi 1 (armv6) and Pi 5 (aarch64)
- Custom package recipe `package/pitcgen/pitcgen.mk`
- Filesystem overlay: systemd units, NTP config, env defaults
- Builds a bootable SD card image

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
└── phase-4-buildroot
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
├── Makefile
├── pitcgenctl          ← CLI control script (updated for IPC in Phase 2)
├── pitcgen.service      ← systemd unit
├── pitcgen.env.example  ← env config template
├── HANDOFF.md           ← build/setup notes
├── PLAN.md              ← this file
├── buildroot/           ← external Buildroot tree (Phase 4)
│   ├── configs/
│   │   ├── pitcgen_pi1_defconfig
│   │   └── pitcgen_pi5_defconfig
│   ├── package/pitcgen/
│   │   ├── Config.in
│   │   └── pitcgen.mk
│   └── overlay/
│       ├── etc/systemd/system/pitcgen.service
│       ├── etc/default/pitcgen
│       └── etc/ntp.conf
└── README.md           ← project overview (to be written)
```

---

## Key decisions & rationale

| Decision | Choice | Why |
|---|---|---|
| Timer strategy | `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` + periodic CLOCK_REALTIME resync | Avoids drift accumulation. ALSA write errors don't break timing. NTP keeps wall clock accurate. |
| Web stack | C + libmicrohttpd | Pi 1 compatible (700 MHz, 512 MB). No Python/Node overhead. Single binary. |
| IPC | Shared memory (read) + Unix socket (commands) | Zero-copy live TC readout. No serialisation overhead. POSIX-native. |
| LTC output (dev) | USB audio dongle | Plug-and-play, no hardware mods, test timer/IPC logic |
| LTC output (final) | Direct GPIO biphase-mark encoding | No DAC, no dongle, minimal BoM, clean appliance |
| Buildroot | External tree in repo | Self-contained build. Easy to reproduce. Tracks with the source. |
| Branch model | `main` + `dev` + feature branches | Main is always releasable. Dev is integration. PRs for review. |
| Dependencies | libltc, libasound2, libmicrohttpd (dev); libltc, libgpiod (final) | All available in apt and Buildroot. No custom porting. |

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
| Shared memory polling adds latency to TC display | Poll at 10 Hz (100 ms) — more than enough for human-readable display. Generator is unaffected. |
| NTP not available on isolated network | Support local PTP (IEEE 1588) or fallback to free-run with monotonic-only timing. |
