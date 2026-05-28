# PiTCGen — Hardware Guide

Recommended hardware for building a PiTCGen timecode generator appliance, from development prototype to production-ready unit.

---

## Compute module

### Recommended: Raspberry Pi 5

| Spec | Detail |
|---|---|
| SoC | Broadcom BCM2712 (4× Cortex-A76 @ 2.4 GHz) |
| RAM | 4 GB or 8 GB (2 GB works, overkill not needed) |
| Ethernet | Gigabit with native HW timestamping (BCM54213 PHC) |
| GPIO | 40-pin header, real-time capable |
| Audio | No analog audio jack — HDMI audio, USB dongle, or GPIO only |
| PTP accuracy | ~100 ns with HW timestamping |
| Price | ~$60 (4 GB) / ~$80 (8 GB) |

**Why Pi 5:** Hardware PTP timestamping on the native Ethernet MAC is essential for broadcast-grade PTPv2/ST 2059. Pi 1–3 route Ethernet through USB and can't do hardware timestamping. The Pi 5's GPIO can toggle at microsecond precision for direct biphase-mark LTC output.

### Production option: Compute Module 5 (CM5)

| Spec | Detail |
|---|---|
| SoC | Same BCM2712 as Pi 5 |
| RAM | 4 GB or 8 GB (with or without eMMC) |
| Form factor | SODIMM-260, requires carrier board |
| Ethernet | Via carrier board PHY — HW timestamping depends on carrier |
| PTP | ✅ if carrier exposes native PHC (official IO board does) |
| Price | ~$45 (CM5) + ~$50 (IO board) or custom carrier |

**When to use CM5:**
- Production appliance in a custom enclosure
- Need eMMC storage (no SD card to eject/vibrate loose)
- Want PCIe for NVMe storage or additional Ethernet
- Lower profile than a full Pi 5 board

**Carrier board options:**
- **Official CM5 IO Board** — full-size, all ports exposed, ~$50. Good for prototyping with CM5.
- **Waveshare CM5-IO-BASE-A** — compact, ~$25. Fewer ports but smaller.
- **Custom PCB** — design your own with only the ports you need (Ethernet, I2C for display, UART for GPS, GPIO for LTC output). ~$5–15 in low volume from JLCPCB/PCBWay.

### Budget / secondary: Raspberry Pi 4

Works for everything except broadcast-grade PTP. Good for audio-only or GPS-disciplined setups.

| Feature | Pi 4 | Pi 5 |
|---|---|---|
| LTC generation (ALSA/GPIO) | ✅ | ✅ |
| PTPv2 HW timestamping | ✅ (limited PHC) | ✅ (better) |
| GPS PPS GPIO | ✅ | ✅ |
| Display I2C | ✅ | ✅ |
| Price | ~$35 (used) | ~$60 |

### Legacy support: Pi 1 / Pi Zero

LTC generation only — no PTP, no web UI recommended at 700 MHz. Good for a headless GPS-locked generator in a minimal enclosure. Buildroot target exists for armv6.

---

## GPS modules

### Tier 1 — Timing-grade (recommended)

**u-blox NEO-M8T** — dedicated timing module. This is the right choice for PiTCGen.

| Spec | Detail |
|---|---|
| PPS accuracy | ±20 ns RMS |
| Timing mode | Stationary (position surveyed once, then fixed) or survey-in (auto-averages position over time) |
| Satellites | GPS, GLONASS, Galileo, BeiDou (concurrent 3 constellations) |
| Interface | UART (NMEA + UBX binary) + PPS output |
| Antenna | Active (3.3V or 5V bias via u.FL or SMA) |
| Price | ~$25–35 (AliExpress/eBay breakout) |
| Breakout boards | **Waveshare NEO-M8T GNSS TIMING HAT** (~$35, Pi HAT format, includes battery holder for warm starts), **SparkFun GPS-RTK** (NEO-M8P, RTK capable, ~$75), generic M8T breakouts on AliExpress |

**Why M8T:** Unlike navigation-grade modules (M8N, M9N), the M8T optimizes the PPS edge for timing rather than position. It enters stationary mode after position fix and cleans up the PPS jitter. Navigation modules drift the PPS edge with every position update.

### Tier 2 — Navigation-grade (acceptable)

**u-blox NEO-M9N** — good PPS, cheaper, less timing-specific.

| Spec | Detail |
|---|---|
| PPS accuracy | ±30 ns RMS |
| Satellites | GPS, GLONASS, Galileo, BeiDou (4 concurrent) |
| Price | ~$15–20 (AliExpress breakout) |

**When to use:** Budget-constrained builds. PPS is still good enough for 48 kHz audio LTC. Slightly more jitter on the PPS edge than M8T.

### Tier 3 — Entry-level (not recommended for timing)

**u-blox NEO-6M / NEO-7M** — no dedicated timing mode, PPS jitter ~1 µs or worse. Avoid for PiTCGen. Fine for time-of-day only (NTP stratum 1 without PPS).

### Antenna

**Active GPS antenna required** for timing-grade performance. Passive (ceramic patch) antennas won't cut it indoors.

| Type | Gain | Connector | Price | Notes |
|---|---|---|---|---|
| Generic active puck | 28 dB | SMA male | ~$5 | Works well outdoors, OK near a window |
| u-blox ANN-MB-00 | 28 dB | SMA male | ~$15 | Multi-band, good for M8T/ZED-F9T |
| Marine/pole-mount | 35–40 dB | SMA or TNC | ~$20–40 | For roof/permanent installs |
| Taoglas AA.105 | 28 dB | u.FL direct | ~$10 | Compact, for internal enclosure mounting |

**Placement:** Window sill minimum. Outdoor with sky view is best. GPS L1 (1575.42 MHz) penetrates glass reasonably well but not concrete/metal.

### Wiring to Pi 5

```
GPS Module          Pi 5 GPIO (40-pin header)
──────────          ─────────────────────────
VCC (3.3V)    →    Pin 1  (3.3V)           ← most modules run at 3.3V
GND           →    Pin 6  (GND)
TX (NMEA)     →    Pin 10 (RX  / GPIO 15)  ← GPS → Pi data
RX (config)   →    Pin 8  (TX  / GPIO 14)  ← Pi → GPS config (optional)
PPS           →    Pin 12 (GPIO 18)         ← kernel PPS input
```

In `/boot/firmware/config.txt`:
```
dtoverlay=pps-gpio,gpiopin=18
enable_uart=1
```

After reboot, `/dev/pps0` appears for chronyd's PPS refclock.

---

## Displays

### Option A — SSD1306 OLED (128×64, I2C)

**Best all-around choice.** Small, bright, cheap, easy to wire.

| Spec | Detail |
|---|---|
| Resolution | 128×64 pixels, monochrome (white or blue) |
| Size | 0.96" (common) or 1.3" |
| Interface | I2C (address 0x3C or 0x3D) |
| Power | ~20 mA at 3.3V |
| Price | ~$3–5 (AliExpress/Amazon) |
| Wiring | 4 pins: VCC, GND, SDA (GPIO 2), SCL (GPIO 3) |

**What fits on 128×64:**

```
┌──────────────────────────┐
│ GPS ●  11:42:00:02       │  ← large TC digits (16px font)
│ 29.97df  Sats:12  3D     │  ← small info line (8px font)
│ PPS: +47ns  PTP: +112ns  │  ← status line
│ GM: 00:11:22:ff:fe:33:44 │  ← scrollable detail
└──────────────────────────┘
```

4 lines with 8px font, or 2 large lines + 2 small lines with mixed sizes.

**Libraries:** `ssd1306_i2c` (C, ~200 lines), or use Linux framebuffer via `fbtft`/`ssd1306` kernel drivers (device tree overlay, no userspace library needed).

### Option B — HD44780 Character LCD (20×4, I2C)

Classic broadcast equipment look. Green or blue backlit, very readable.

| Spec | Detail |
|---|---|
| Display | 20 characters × 4 lines |
| Interface | I2C backpack (PCF8574) at 0x27 |
| Power | ~30 mA (backlight on) |
| Price | ~$8–12 (Amazon/eBay) |
| Wiring | 4 pins: VCC, GND, SDA, SCL |

**What fits on 20×4:**

```
GPS ● 11:42:00:02 29.97df
Sats:12 3D  PPS:+47ns
PTP ◇ Off:+112ns  Dly:231
GM:00:11:22:ff:fe:33:44
```

Fixed-width characters give a very clean, readable display. No graphics — text only. The broadcast industry has used these for decades.

### Option C — SSD1309 OLED (2.42", 128×64, I2C/SPI)

Larger version of SSD1306. Same resolution on a bigger panel — easier to read from a distance.

| Spec | Detail |
|---|---|
| Size | 2.42" diagonal |
| Interface | I2C or SPI (SPI faster, uses more pins) |
| Price | ~$8–12 |

### Option D — ST7789 TFT (240×240 or 320×240, SPI)

Full color, more pixels. Overkill for timecode, but allows a richer UI.

| Spec | Detail |
|---|---|
| Resolution | 240×240 or 320×240, 16-bit color |
| Interface | SPI (MOSI, SCLK, CS, DC, RST) + backlight PWM |
| Power | ~50–80 mA |
| Price | ~$10–18 |
| Boards | **Pimoroni Display HAT Mini** (320×240, Pi HAT format, ~$15), **Waveshare 2.8" TFT** (~$18) |

**When to use:** You want color-coded clock-source status, graphical signal meters, or a more polished UI.

### Display comparison

| Display | Resolution | Size | Interface | Power | Price | Best for |
|---|---|---|---|---|---|---|
| SSD1306 | 128×64 | 0.96–1.3" | I2C | ~20 mA | $3–5 | Compact, cheap, simple |
| SSD1309 | 128×64 | 2.42" | I2C/SPI | ~25 mA | $8–12 | Larger, readable at distance |
| HD44780 | 20×4 chars | varies | I2C | ~30 mA | $8–12 | Classic broadcast look |
| ST7789 | 240×240+ | 1.3–2.8" | SPI | ~50 mA | $10–18 | Color UI, richer display |

---

## Audio output

### Development: USB audio dongle

Any $5–10 USB audio adapter works. The Pi sees it as an ALSA device.

```bash
# List devices
aplay -l

# Run generator on specific device
pitcgen -d hw:CARD=Device,DEV=0
```

Output is consumer line level (~1Vpp) on 3.5mm. Feed into an XLR adapter or direct to camera/recorder 3.5mm mic input with attenuation if needed.

### Production: Direct GPIO (planned)

No DAC, no dongle. The Pi GPIO pin outputs a 0/3.3V biphase-mark waveform directly. A resistive divider + DC-blocking capacitor converts to balanced ~1Vpp for XLR output:

```
GPIO pin ──┬─ 1.0kΩ ──┬── XLR pin 2 (hot)
           │          │
           ├─ 680Ω  ──┤── XLR pin 3 (cold)
           │          │
           └─ 10µF ───┘  (DC block, optional)
                      
GND ─────────────────── XLR pin 1 (ground)
```

Meets SMPTE LTC signal level spec. Zero additional active components.

---

## Power

| Setup | Power draw | Recommended supply |
|---|---|---|
| Pi 5 + USB audio dongle | ~5–8W | Official Pi 5 PSU (5.1V/5A) or PoE+ HAT |
| Pi 5 + GPS + OLED | ~6–9W | Same |
| CM5 + carrier + GPS + OLED + LTC board | ~5–8W | 5V/3A via carrier barrel jack |
| PoE (all setups) | As above | 802.3at PoE+ injector or switch |

For field/location use, a USB-C PD power bank (20W+) works. PiTCGen draws well under 10W — a 20,000 mAh bank runs it for ~8–12 hours.

---

## Enclosure suggestions

| Enclosure | Fits | Price | Notes |
|---|---|---|---|
| **Official Pi 5 case** + fan | Pi 5 only | ~$10 | Clean, no cutouts for GPS/display |
| **Flirc Pi 5 case** | Pi 5 only | ~$16 | Aluminum, passive cooling, looks professional |
| **Hammond 1591 series** | Any | ~$8–15 | ABS, easy to drill for connectors. Many sizes. |
| **3D-printed custom** | Any | ~$2 (filament) | Design for exact port layout |
| **19" rack blank + DIN rail** | Any | ~$5 + rail | For rack-mount installations |

For production, a custom 3D-printed enclosure with cutouts for OLED window, GPS antenna SMA, XLR output, Ethernet, and power is the cleanest approach.

---

## Complete BOM — production appliance

| Item | Model | Qty | Unit price | Total |
|---|---|---|---|---|
| Compute | Raspberry Pi 5 (4 GB) | 1 | $60 | $60 |
| GPS | u-blox NEO-M8T breakout | 1 | $30 | $30 |
| GPS antenna | Active puck, 28 dB, SMA | 1 | $10 | $10 |
| Display | SSD1306 OLED 128×64 I2C | 1 | $5 | $5 |
| Audio out | XLR 3-pin male chassis mount | 1 | $3 | $3 |
| Resistors | 1kΩ + 680Ω (for XLR divider) | 2 | $0.10 | $0.20 |
| Storage | Samsung Pro Endurance microSD 32 GB | 1 | $10 | $10 |
| Power | Official Pi 5 PSU (5.1V/5A) | 1 | $12 | $12 |
| Enclosure | 3D printed or Hammond 1591 | 1 | $5–15 | $10 |
| Misc | Header wires, standoffs, heat-set inserts | — | $5 | $5 |
| | | | **Total** | **~$145** |

### Budget BOM (dev bench unit)

| Item | Model | Qty | Price |
|---|---|---|---|
| Compute | Raspberry Pi 4 (used) | 1 | $35 |
| GPS | u-blox NEO-M9N breakout | 1 | $18 |
| GPS antenna | Generic active puck | 1 | $5 |
| Display | SSD1306 OLED | 1 | $4 |
| Audio | USB audio dongle | 1 | $6 |
| Power | USB-C PSU (any) | 1 | $8 |
| | | **Total** | **~$76** |

---

## Wiring reference — Pi 5 40-pin header

```
                   3.3V (1)  (2)  5V
            I2C SDA (3)  (4)  5V
            I2C SCL (5)  (6)  GND
                    (7)  (8)  UART TX  ← GPS RX (config)
                    (9)  (10) UART RX  ← GPS TX (NMEA)
                   (11)  (12) GPIO 18  ← GPS PPS
                   (13)  (14) GND
                   (15)  (16) 
                   3.3V (17) (18) 
            SPI MOSI (19) (20) GND
            SPI MISO (21) (22) 
            SPI SCLK (23) (24) SPI CE0
                   (25) (26) SPI CE1
                           ...          
```

**Conflicts to avoid:**
- I2C (pins 3/5) — shared by display and any I2C sensors. I2C is a bus; multiple devices OK if they have different addresses.
- UART (pins 8/10) — used by GPS. Conflicts with serial console; disable `console=serial0` in `cmdline.txt`.
- GPIO 18 (pin 12) — used by GPS PPS. Conflicts with PWM audio output; not an issue since we don't use the Pi's PWM audio.

---

## Carrier board notes (CM5)

If using a CM5 carrier board, ensure it exposes:
- **Ethernet with PHC** — the PHY must support hardware timestamping (most Gigabit PHYs on carrier boards do, but verify)
- **40-pin GPIO header** — needed for GPS PPS, I2C display, and future GPIO LTC output
- **UART** — needed for GPS NMEA data
- **I2C bus** — needed for display
- **SD card slot or eMMC** — for OS storage

The official CM5 IO Board exposes all of these. Most third-party carrier boards do too, but double-check the Ethernet PHY datasheet for PTP timestamping support.
