# Hardware Matrix

What is actually on each board, and how it is wired.

**Status** is the only column that grants permission to write code:

| Status | Meaning | May code against it |
|---|---|---|
| `VERIFIED` | primary source cited, revision named | yes |
| `CONFLICTING` | sources disagree — both recorded | no, resolve first |
| `ASSUMPTION` | plausible, unconfirmed, flagged as such in code | only behind a flag |
| `UNKNOWN` | no source found | no — this is a blocker |

Everything below is `VERIFIED` against vendor documentation, vendor board
support code, or the published schematic, unless the row says otherwise.
**No board has been physically inspected**, so anything requiring measurement —
power draw, GNSS performance, interference — is not here. It is in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).

Sources are listed at the bottom.

---

## The headline: these two boards are not siblings

| | T-Watch S3 Plus | Waveshare AMOLED 2.06 |
|---|---|---|
| LoRa radio | yes — **five possible chips** | **absent** |
| GNSS | yes — **two possible modules** | **absent** |
| IMU | BMA423 — accelerometer only | QMI8658 — 6-axis |
| Magnetometer | **absent** | **absent** |
| RTC | PCF8563 | PCF85063 |
| Haptic | DRV2605 | **none found** |
| Audio in | 1× PDM mic | 2× mics via ES7210 ADC |
| Audio out | MAX98357A (I2S class-D) | ES8311 codec |
| IR transmitter | yes (GPIO2) | **absent** |
| SD card | **absent** | yes |
| Display | 240×240 IPS, SPI | 410×502 AMOLED, QSPI |
| PMU | AXP2101 | AXP2101 |

The only meaningful things they share are the SoC and the PMU. Every other
subsystem differs in part, in bus, or in existence. This table is the
justification for the capability layer: a build that hardcodes either board's
peripheral set cannot run on the other.

**Neither board has a magnetometer.** The magnetometer work the specification
calls for is therefore architectural only — an API that can accept one later,
not a driver. Heading on real hardware currently comes from GNSS course alone,
and only the T-Watch has GNSS at all.

---

## LilyGO T-Watch S3 Plus

Revision: vendor schematics `T_WATCH-S3 25-03-24` and
`T-Watch-S3-Plus-GPS V1.0 2025-04-29`. Board revision of a *physical* unit is
still unknown — see OPEN_QUESTIONS A1.

### Core

| Item | Value | Status |
|---|---|---|
| SoC | ESP32-S3 | VERIFIED |
| Flash | 16 MB QSPI | VERIFIED |
| PSRAM | 8 MB QSPI | VERIFIED |
| Battery | 940 mAh, 3.7 V | VERIFIED |
| Charge current | 0–1024 mA programmable; vendor recommends ≤300–400 mA; vendor header default 125 mA | VERIFIED |
| USB | Micro-USB, charge + programming only, no external supply function | VERIFIED |

### Peripherals

| Peripheral | Part | Bus / pins | I2C addr | Power rail | Status |
|---|---|---|---|---|---|
| Display | ST7789V3, 240×240 IPS 1.3", 450 cd/m², 262K | SPI: CS 12, MOSI 13, SCK 18, DC 38, BL 45; MISO and RESET not connected | — | ALDO3 (panel), ALDO2 (backlight) | VERIFIED |
| Touch | FT6336U | **separate I2C**: SDA 39, SCL 40, INT 16; **RESET not connected** | 0x38 | ALDO3 | VERIFIED |
| PMU | AXP2101 | main I2C, INT 21 | 0x34 | — | VERIFIED |
| RTC | PCF8563 | main I2C, INT 17 | 0x51 | VBACKUP (coin cell) | VERIFIED |
| Accelerometer | BMA423 — **no gyroscope** | main I2C, INT 14 | 0x19 | — | VERIFIED |
| Haptic | DRV2605 | main I2C | 0x5A | **BLDO2 (enable)** | VERIFIED |
| LoRa | SX1262 default; **SX1280 / CC1101 / LR1121 / SI4432 by order** | SPI: SCK 3, MISO 4, MOSI 1, CS 5, RST 8, BUSY 7, IRQ 9 | — | ALDO4 | VERIFIED |
| GNSS | **u-blox MIA-M10Q or Quectel LS550G** | UART: TX 42, RX 41; **PPS not connected** | — | BLDO1 (+ DC4 @850 mV for LS550G) | VERIFIED |
| Microphone | SPM1423HM4H-B, PDM | SCK 44, DATA 47 | — | — | VERIFIED |
| Amplifier | MAX98357A, 3.2 W class-D | I2S: BCLK 48, WCLK 15, DOUT 46 | — | — | VERIFIED |
| IR transmitter | IR12-21C | GPIO 2 | — | — | VERIFIED |
| Main I2C bus | — | SDA 10, SCL 11 | — | — | VERIFIED |
| Buttons | BOOT (GPIO0, usable as a custom button), RST (hardware only), PWR (via AXP2101) | — | — | — | VERIFIED |

### AXP2101 rail map

| Rail | Feeds |
|---|---|
| DC1 | ESP32-S3 |
| DC3 | unused (was GNSS on earlier revisions **without** rear BOOT/RST buttons) |
| DC4 | LS550G GNSS variant only, 850 mV |
| ALDO2 | display backlight |
| ALDO3 | display and touch |
| ALDO4 | LoRa |
| BLDO1 | GNSS, 3300 mV |
| BLDO2 | DRV2605 enable |
| VBACKUP | RTC coin cell |
| DC2, DC5, LDO1, ALDO1, DLDO1, CPUSLDO | unused |

### Vendor-published power figures

Vendor numbers, not Firefly measurements. Useful as an order of magnitude and
as a target to reproduce — not as evidence about Firefly's own firmware.

| Mode | Wake source | Current |
|---|---|---|
| Light sleep | PWR + BOOT + touch | 2.38 mA |
| Deep sleep | PWR + BOOT, backup on | 530 µA |
| Deep sleep | PWR + BOOT, backup off | 460 µA |
| Deep sleep | touch panel | 1.08 mA |
| Deep sleep | timer, backup on | 510 µA |
| Deep sleep | timer, backup off | 460 µA |
| Power off | backup only | 50 µA |

### Traps recorded by the vendor

- **Touch has no RESET line.** The vendor states that if the touch panel is put
  to sleep, touch will not work again. This constrains the power state machine
  directly — it is not a driver detail.
- **GNSS rail differs by revision.** BLDO1 on units with rear BOOT/RST buttons;
  DC3 on earlier units without them. Choosing wrong means GNSS silently never
  powers up.
- **The LS550G variant needs two rails** (DC4 at 850 mV *and* BLDO1 at 3300 mV)
  before it will work at all.

---

## Waveshare ESP32-S3-Touch-AMOLED-2.06

Revision: schematic `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0`; pin map from
vendor BSP `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0.

### Core

| Item | Value | Status |
|---|---|---|
| SoC | ESP32-S3, dual-core LX7 | VERIFIED |
| Flash / PSRAM | not stated by the vendor BSP or README | UNKNOWN |
| Battery | present (AXP2101 charge path); capacity not stated | UNKNOWN |

### Peripherals

| Peripheral | Part | Bus / pins | Status |
|---|---|---|---|
| Display | **CO5300**, 2.06" 410×502 AMOLED, RGB565 | QSPI: CS 12, PCLK 11, D0 4, D1 5, D2 6, D3 7, RST 8 | VERIFIED |
| Touch | FT3168 (driven by the FT5x06-family driver) | INT 38, RST 9, on main I2C | VERIFIED |
| PMU | AXP2101 | main I2C | VERIFIED |
| IMU | QMI8658 / QMI8658C, 6-axis | main I2C | VERIFIED |
| RTC | PCF85063ATL | main I2C | VERIFIED |
| Audio codec | ES8311 | I2S | VERIFIED |
| Mic ADC | ES7210, **dual** digital microphones | I2S | VERIFIED |
| Amplifier enable | — | GPIO 46 | VERIFIED |
| SD card | — | SDMMC 1-bit: CLK 2, CMD 1, D0 3 | VERIFIED |
| Main I2C bus | — | SDA 15, SCL 14 | VERIFIED |
| I2S bus | — | MCLK 16, SCLK 41, LCLK/WS 45, DOUT 40, DSIN 42 | VERIFIED |
| Expansion connector | mentioned in the specification | — | UNKNOWN |
| LoRa | — | **not present** | VERIFIED |
| GNSS | — | **not present** | VERIFIED |

### What the vendor BSP leaves unhandled

BSP v2.0.0 declares its own capabilities as: display ✓, touch ✓, audio ✓
(speaker and mic), SD card ✓ — and **buttons ✗, IMU ✗**.

So the vendor's own board support package does not drive the QMI8658 that is
soldered to the board, and does not touch the AXP2101 or the PCF85063 either —
those appear only in standalone examples.

This is the single clearest argument for the approach this project takes:
*shipped on the board* and *handled by software* are different sets, and the
gap is where capability silently becomes unavailable. Firefly's core is
responsible for every part on the board, whether or not an application asks
for it yet.

### Panel driver nuance

The product is documented as using a **CO5300** panel controller, while the
BSP depends on the component `waveshare/esp_lcd_sh8601`. This is not a
contradiction — the vendor drives the CO5300 through the SH8601-family driver.
Recorded so nobody later "fixes" the apparent mismatch.

---

## Simulator

| Capability | Provided as |
|---|---|
| Display | host window, one preset per real geometry (240×240 and 410×502) |
| Touch | mouse |
| Buttons | keyboard |
| GNSS | scripted fixes with settable quality — including *no fix* |
| Mesh | in-process fake peers |
| Battery | scripted discharge curve |
| IMU / magnetometer | scripted motion and field |
| Haptic / IR | logged, never emitted |

The simulator must be able to present a board with **no** radio and **no** GNSS,
because that is a real configuration, not a degraded one.

---

## Capability matrix

| Capability | T-Watch S3 Plus | Waveshare 2.06 | Simulator |
|---|---|---|---|
| `DISPLAY` | ✅ 240×240 IPS SPI | ✅ 410×502 AMOLED QSPI | ✅ both presets |
| `TOUCH` | ✅ FT6336U, **no reset line** | ✅ FT3168, has reset | ✅ |
| `PMU` | ✅ AXP2101 | ✅ AXP2101 | simulated |
| `RTC` | ✅ PCF8563 | ✅ PCF85063 | host clock |
| `ACCELEROMETER` | ✅ BMA423 | ✅ QMI8658 | simulated |
| `GYROSCOPE` | ❌ | ✅ QMI8658 | simulated |
| `MAGNETOMETER` | ❌ | ❌ | simulated |
| `LORA` | ✅ one of five chips | ❌ | simulated |
| `GNSS` | ✅ one of two modules | ❌ | simulated |
| `HAPTICS` | ✅ DRV2605 (rail-gated) | ❌ | logged |
| `AUDIO_OUT` | ✅ MAX98357A | ✅ ES8311 | host audio |
| `AUDIO_IN` | ✅ 1× PDM | ✅ 2× via ES7210 | simulated |
| `IR_TRANSMIT` | ✅ IR12-21C | ❌ | logged |
| `SD_CARD` | ❌ | ✅ | host filesystem |
| `BATTERY_SENSE` | ✅ via AXP2101 | ✅ via AXP2101 | simulated |
| `WIFI` / `BLE` | ✅ ESP32-S3 | ✅ ESP32-S3 | simulated |

A plain boolean `has(Capability::X)` cannot express "LoRa present, but which of
five chips" or "IMU present, but no gyroscope". How capability carries variant
and degree is an architectural decision — see `docs/adr/`.

---

## Sources

| # | Source |
|---|---|
| S1 | `Xinyuan-LilyGO/LilyGoLib`, `docs/hardware/lilygo-t-watch-s3-plus.md` — MIT |
| S2 | `Xinyuan-LilyGO/LilyGoLib`, `src/LilyGoWatchS3.h` — radio build variants, charge defaults |
| S3 | `Xinyuan-LilyGO/LilyGoLib`, `schematic/T_WATCH-S3 25-03-24.pdf` |
| S4 | `Xinyuan-LilyGO/LilyGoLib`, `schematic/T-Watch-S3-Plus-GPS V1.0 2025-04-29.pdf` |
| S5 | `waveshareteam/ESP32-S3-Touch-AMOLED-2.06`, `README.md` — Apache-2.0 |
| S6 | `waveshareteam/ESP32-S3-Touch-AMOLED-2.06`, `Schematic/…-V1.0.pdf` |
| S7 | ESP Component Registry, `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0 — Apache-2.0 |
| S8 | arduino-esp32 variant `lilygo_twatch_s3/pins_arduino.h` (referenced by S1) |

All checked 2026-08-21.
