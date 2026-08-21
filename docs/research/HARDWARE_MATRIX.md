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
| Sub-GHz radio | yes — **five possible chips, and only some of them do LoRa** ([ADR-0003](../adr/0003-radio-not-lora.md)) | **absent** |
| GNSS | yes — **two possible modules** | **absent** |
| IMU | BMA423 — accelerometer only | QMI8658 — 6-axis |
| Magnetometer | **absent** | **absent** |
| RTC | PCF8563 | PCF85063 |
| Haptic | DRV2605L — waveform library over I2C | **bare motor on a GPIO** — no driver IC |
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
not a driver. Heading on real hardware currently comes from GNSS course alone.
Only the T-Watch has GNSS **on the board** — a Firefly node supplies it to
either, so that is a statement about boards and not about what a device can do.
Course-over-ground also only works while the user is moving, which is what makes
it a different product from a compass rather than a lesser one.

---

## LilyGO T-Watch S3 Plus

Revision: **CONFLICTING.** The two vendor PDFs are published as
`T_WATCH-S3 25-03-24.pdf` and `T-Watch-S3-Plus-GPS V1.0 2025-04-29.pdf`, but the
title block *inside* the main schematic reads `T_WATCH-2020&GPS_V08`, Rev V1.4,
dated Friday 8 January 2021 — a 2020-era title block the vendor never updated.
The contents are unambiguously S3-class (`ESP32-S3-R8`, `W25Q128JW`), so the
drawing is the right board with the wrong nameplate. **Cite the filename for
provenance, never the title block for revision.** Board revision of a *physical*
unit is still unknown — see OPEN_QUESTIONS A1.

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
| Display | ST7789V3, 240×240 IPS, 450 cd/m², 262K. **Diagonal is CONFLICTING — 1.3" or 1.54"; see below and OPEN_QUESTIONS D15.** Everything else in this row is verified | SPI: CS 12, MOSI 13, SCK 18, DC 38, BL 45; MISO and RESET not connected | — | ALDO3 (panel), ALDO2 (backlight) | VERIFIED except the diagonal |
| Touch | FT6336U | **separate I2C**: SDA 39, SCL 40, INT 16; **RESET not connected** | 0x38 | ALDO3 | VERIFIED |
| PMU | AXP2101 | main I2C, INT 21 | 0x34 | — | VERIFIED |
| RTC | PCF8563 | main I2C, INT 17 | 0x51 | VBACKUP (coin cell) | VERIFIED |
| Accelerometer | BMA423 — **no gyroscope** | main I2C, INT1 → GPIO 14. **INT2 is bonded out but not routed** (R12, R15 not fitted) | 0x19 | +3V3 | VERIFIED |
| Haptic | DRV2605 | main I2C | 0x5A | **BLDO2 (enable)** | VERIFIED |
| Radio | Schematic fits **HPD16B3** (SX1262-class pinout); vendor header builds **SX1280 / CC1101 / LR1121 / SI4432** variants by order. Only the SX1262 path is MeshCore-supported at the pinned revision, and CC1101/SI4432 cannot do LoRa at all — [ADR-0003](../adr/0003-radio-not-lora.md) | SPI: SCK 3, MISO 4, MOSI 1, CS 5, RST 8, BUSY 7, DIO1 9, **DIO3 6** | — | ALDO4 via R61 0 Ω (net `GPS_VDD`) | VERIFIED |
| GNSS | **u-blox MIA-M10Q or Quectel LS550G**, on a 13-pin 0.3 mm FPC daughterboard | UART: TX 42, RX 41; **PPS not connected** — the net exists on the daughterboard but `PPS` appears nowhere in the main-board schematic | — | BLDO1 (+ DC4 @850 mV for LS550G); enable net `GPS_LDO` on FPC pin 3 | VERIFIED |
| Microphone | SPM1423HM4H-B, PDM | CLK 44, DATA 47. **`SELECT` is resistor-strapped (R80, R81 not fitted)** — channel fixed in hardware | — | +3V3 | VERIFIED |
| Amplifier | MAX98357A, 3.2 W class-D | I2S: BCLK 48, WCLK 15, DIN 46. **`SD_MODE` is resistor-strapped (R14 = 1 MΩ; R74, R76 not fitted) — no GPIO reaches it** | — | `DLDO1` pin (DLDO1/DC1SW) via R18 0 Ω → `SPK_VDD` | VERIFIED |
| IR transmitter | IR12-21C | GPIO 2 → R64 0 Ω → base of Q15 (MMBT3904, NPN low-side); LED anode at +3V3. **GPIO 2 high = LED conducts; inactive level is LOW** | — | +3V3 | VERIFIED |
| Main I2C bus | — | SDA 10, SCL 11 | — | — | VERIFIED |
| Charge indicator LED | driven by the AXP2101 `CHGLED` pin through R182 100 Ω | **no GPIO** — configured over I2C in the PMU | via 0x34 | — | VERIFIED |
| USB device | D− and D+ land on GPIO 19 / GPIO 20 — the ESP32-S3 native USB pins | USB-Serial-JTAG and USB-OTG are both physically available | — | VBUS | VERIFIED |
| RTC backup cell | MS412FE rechargeable coin cell, charged through D14 (1N4148) + 10 kΩ | holds `VCC_RTC` across a battery swap | — | VBACKUP | VERIFIED |
| RTC square-wave out | PCF8563 `CLKOUT` → net `RTC_CLKOUT` | present as a net; **R126 not fitted** | via 0x51 | — | VERIFIED |
| Battery disconnect | MSK12C02-HB slide switch in series between the cell and `BAT` | mechanical only — firmware cannot sense or override it | — | — | VERIFIED |
| Buttons | BOOT (GPIO 0) and RST both sit **on the GNSS daughterboard**, reaching the main board on FPC pins 2 and 6. PWR (SW7) wires to the AXP2101 `PWRON` pin — **it never reaches a GPIO**, so every press arrives as a PMU interrupt | — | — | — | VERIFIED |

### The radio is one of five chips, and they are not equivalent

The vendor header builds five variants by order. Recording them as "five LoRa
chips" was wrong in a way that reached five documents, and
[ADR-0003](../adr/0003-radio-not-lora.md) carries the correction and the
evidence. The short version, because this is the table people will look at:

| Chip | LoRa? | Bands (RadioLib 7.7.1 driver limits) | MeshCore `d929643` |
|---|---|---|---|
| **SX1262** | yes | 150 – 960 MHz | **supported** |
| **SX1280** | yes, **2.4 GHz only** | 2400 – 2500 MHz | absent |
| **LR1121** | yes | 150 – 960 · 1900 – 2200 · 2400 – 2500 MHz | needs driver work |
| **CC1101** | **no** — FSK/OOK family | 300 – 348 · 387 – 464 · 779 – 928 MHz | compiled out (`RADIOLIB_EXCLUDE_CC1101=1`) |
| **Si4432** | **no** — FSK/OOK | 240 – 930 MHz | absent |

There is also **no T-Watch variant in MeshCore** — 87 upstream board variants,
several LilyGO, not one T-Watch.

Status **PARTIAL**, not VERIFIED: the modulation, band and power figures are read
from RadioLib's drivers and MeshCore's build configuration, not from the TI and
Silicon Labs datasheets, which refused automated retrieval. Confirming them from
primary sources is open question **R1**. The schematic itself fits an
SX1262-class module (HPD16B3), so the most likely fitted part is also the one
that works — but "most likely" is not A2 answered.

### AXP2101 rail map

| Rail | Feeds |
|---|---|
| DC1 | ESP32-S3 |
| DC3 | unused (was GNSS on earlier revisions **without** rear BOOT/RST buttons) |
| DC4 | LS550G GNSS variant only, 850 mV |
| ALDO2 | display backlight |
| ALDO3 | display and touch |
| ALDO4 | Radio |
| BLDO1 | GNSS, 3300 mV |
| BLDO2 | DRV2605 enable |
| VBACKUP | RTC coin cell (MS412FE) |
| DC2, DC5, LDO1, CPUSLDO | unused |

Two rails the vendor document calls unused are loaded on the schematic:

| Rail | Vendor doc (S1) | Schematic (S3) | Status |
|---|---|---|---|
| ALDO1 | unused | pin 18 → net `+3V3`, the rail every always-on part sits on (SoC I/O, BMA423, PCF8563, DRV2605 `VDD`, mic, IR LED anode) | **CONFLICTING** |
| DLDO1 | unused | pin 20 (`DLDO1/DC1SW`) → R18 0 Ω → `SPK_VDD`, the audio amplifier | **CONFLICTING** |

The AXP2101 pin is `DLDO1/DC1SW` — one ball, two possible functions, chosen in
a register. So "DC1 feeds the SoC" and "the amplifier hangs off DC1SW" are
compatible; "ALDO1 is unused" and "ALDO1 is the +3V3 rail" are not. Do not pick
the convenient reading. Resolve it by reading the PMU's own registers on a
powered board — OPEN_QUESTIONS H8.

Consequence if the schematic is right: **`+3V3` is a switchable rail**, and
cutting it takes the accelerometer, the RTC chip, the haptic driver, the
microphone and the IR emitter with it. That is a power state, not a detail.

### Pins firmware cannot control

Three parts have a control line that is strapped in hardware, so the only way to
change their state is to move their rail:

| Part | Strapped pin | Fixed by | What firmware loses |
|---|---|---|---|
| MAX98357A amplifier | `SD_MODE` | R14 = 1 MΩ; R74, R76 not fitted | **No shutdown.** The amplifier is enabled whenever `SPK_VDD` is up. Silence means dropping the rail, not asserting a pin. |
| SPM1423 microphone | `SELECT` | R80, R81 not fitted | Channel assignment is fixed. |
| FT6336U touch | `RESET` (`T_RST`) | pull-up R39 is `4K7/NC` — **not fitted**, no GPIO drives it | No way to recover a wedged controller except cycling ALDO3 — which is shared with the display. This is the mechanism behind the vendor's "touch never wakes again" warning. |

### ESP32-S3 strapping pins carry live signals

Three of the four strapping pins are also functional nets. A driver that asserts
one of these early enough changes how the chip boots.

| Pin | Strapping role at reset | Also wired to |
|---|---|---|
| GPIO 0 | boot mode select | BOOT button, on the GNSS daughterboard via FPC pin 2 |
| GPIO 3 | JTAG signal source | **LoRa `SCK`** |
| GPIO 45 | `VDD_SPI` voltage select | **display backlight** (GPIO 45 high → Q14 conducts → backlight on) |
| GPIO 46 | ROM log enable | **I2S `DIN`** to the amplifier |

GPIO 45 is the sharp one: it selects the flash/PSRAM supply voltage at reset, and
it is the backlight line. Active-high through an NPN means the backlight is dark
at reset, which is the safe direction — but any future change that adds a
pull-up to that net to "keep the screen on" would change `VDD_SPI` and the board
would stop booting. Record it in the board file, not in a driver comment.

### The GNSS daughterboard is not only GNSS

S4 is one sheet: a u-blox `MIA-M10Q`, an IPEX antenna jack, an `MS412FE`
rechargeable cell for hot-start backup, and a 13-pin 0.3 mm FPC to the main
board. What matters is what else rides that connector.

| FPC pin | Net | Meaning |
|---|---|---|
| 1 | `GPIO41 / MTDI` | GNSS UART |
| 2 | `IO0` | **BOOT button** |
| 3 | `GPS_LDO` | GNSS supply / enable |
| 6 | `RST / EN` | **RESET button** |
| 7 | `IO10` | **main I2C `SDA`** |
| 8 | `GPIO42 / MTMS` | GNSS UART |

Two consequences, both structural:

1. **Unplugging the GNSS module also unplugs BOOT and RESET.** A board running
   without the daughterboard has no reset button and no way into download mode
   except over USB. Any bring-up instruction that says "hold BOOT" is wrong for
   that configuration.
2. **The main I2C `SDA` reaches the connector.** The `MIA-M10Q` exposes `SDA` and
   `SCL` (u-blox DDC, address 0x42). Whether the daughterboard actually connects
   them is not established from the dump — but if it does, the GNSS is a *sixth*
   device on the shared bus and a bus scan will find it. Until that is settled,
   an unexpected 0x42 is a discovery, not a fault — OPEN_QUESTIONS D9.

The daughterboard also carries `LNA_EN`, `SAFEBOOT_N` and `RESET_N` on the
module; none of them appear on the main-board schematic.

### Display diagonal — CONFLICTING

240 × 240 is not in doubt; the **physical size** is, and the two sources are
both first-party.

| Says | Source | Weight |
|---|---|---|
| **1.3"** | LilyGoLib `docs/hardware/lilygo-t-watch-s3-plus.md:68` and `lilygo-t-watch-s3.md:62`, both `\| Display Size \| 1.3 Inch \|` | vendor documentation, stated for the Plus **by name** — the only source that names this product |
| **1.54"** | the schematic LCD sheet: part `QT154C2408`, symbol `LCD_1.54-TOUCH` | vendor schematic, and the part number decodes — see below |

**What the part number decodes to.** `QT154C2408` is a 深圳秦唐盛世科技有限公司
(Shenzhen Qintang Shengshi) module. That vendor's published specification for the
sibling part **`QT154H2201`** — the one Adafruit distributes with product 4421 —
reads `LCD Size 1.54" inch`, `Resolution 240x(RGB)x240`, `Driver IC ST7789V`,
`Light Source 3 White LED in Parallel`. So in this vendor's own numbering the
`154` field **is** the diagonal in hundredths of an inch, and the sibling part
matches our panel on resolution, driver and backlight topology. The T-Watch
schematic's `一串三并` backlight annotation is the same three-parallel string.

**What keeps it CONFLICTING anyway.** The sheet carrying `QT154C2408` is in
`T_WATCH-S3 25-03-24.pdf`, whose title block is a stale `T_WATCH-2020&GPS_V08`
nameplate — and the 2020 T-Watch genuinely was 1.54" (TTGO_TWatch_Library reports
`1.54"/240X240/ST7789V` for 2019 and 2020 V1/V2/V3). An inherited LCD symbol is
exactly the artefact that stale nameplate predicts. Against that, the same sheet
was re-pinned for S3 GPIOs (IO12/IO13/IO38/IO45), so it is not simply the 2020
drawing. **There is no S3 Plus main-board schematic published at all** — the file
named for the Plus is one sheet covering the GNSS daughterboard only — so no
document both names this product and shows this panel.

**Working value: 1.3" (261 dpi).** Chosen as the conservative one, not the likely
one: physical minimums converted at the higher dpi produce more pixels, so if the
panel is really 1.54" every touch target comes out physically larger than
designed rather than smaller. Recorded in `platform/src/board_profiles.cpp`.
A ruler on a physical unit settles it — it rides on A1.

### Display detail worth budgeting

The panel is `QT154C2408` on a 24-pin `AXK824145-0.4mm` connector. The backlight
is annotated **one series × three parallel, I_F = 3 × 15 mA, V_F 3.0–3.3 V** —
so **45 mA at full brightness**, fed from ALDO2 through R63 (2 Ω) and switched
by Q14 (MMBT3904) on GPIO 45. That is the single largest continuous load on the
board that firmware controls directly, and it is the first number the power
budget needs.

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

Revision: schematic `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0` — **read**,
3 sheets; pin map from vendor BSP `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0.
Where the two differ, the schematic wins on *what exists* and the BSP wins on
*which pin firmware should use* — the BSP was demonstrably written to a subset
of the board.

### Core

| Item | Value | Status |
|---|---|---|
| SoC | **ESP32-S3R8** — bare chip, not a module | VERIFIED |
| Flash | **GD25Q256EYIGR**, 256 Mbit = **32 MB**, quad SPI, external (U3) | VERIFIED |
| PSRAM | 8 MB (the `R8` suffix) — **quad or octal is unresolved, see D12** | CONFLICTING |
| Battery | present, on connector `BAT1` via the AXP2101 charge path; capacity not stated | UNKNOWN |

The SoC marking is `ESP32-S3R8` on **both** target boards, so D12 — quad or octal
PSRAM — is a single question with a single answer that unblocks both. The flash
is twice the T-Watch's 16 MB, which makes dual-OTA-slot arithmetic comfortable on
the board with 3.57× the pixels. That is a convenient coincidence, not a plan.

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
| **Vibration motor** | **no driver IC** — GPIO 18 → R12 (4.7 kΩ) → Q1 (MMBT3904, NPN) → motor on connector J1 | net `MOTOR`; motor supply from **BLDO2** | VERIFIED |
| Buttons | at least two tactile keys on the board (`Key1` adjacent to `BOOT`, `Key3`) plus `PWRON` on the PMU. **The vendor BSP declares none** | specific GPIO assignment not resolved from the extraction — D5 | PARTIAL |
| SD card | — | SDMMC 1-bit: CLK 2, CMD 1, D0 3 | VERIFIED |
| Main I2C bus | — | SDA 15, SCL 14 | VERIFIED |
| I2S bus | — | MCLK 16, SCLK 41, LCLK/WS 45, DOUT 40, DSIN 42 | VERIFIED |
| Expansion connector | header `J3`, at least 29 pins on the drawing | pinout not resolved from text extraction — D3 | PARTIAL |
| USB | `USB_N` / `USB_P` through 22 Ω series resistors (R19, R20) to the SoC native USB pins | — | VERIFIED |
| Sub-GHz radio | — | **not present** | VERIFIED |
| GNSS | — | **not present** | VERIFIED |

### AXP2101 rail map

Read from the schematic; the vendor BSP does not configure the PMU at all.

| Rail | Net | Feeds |
|---|---|---|
| ALDO1 | `VL1_3.3V` | 3.3 V rail |
| ALDO2 | `VL2_3.3V` | 3.3 V rail |
| ALDO3 | `VCC3V` | 3.3 V rail |
| ALDO4 | `VL3_1.8V` | **1.8 V** rail |
| BLDO2 | — | **vibration motor** |

Which load sits on which of the three 3.3 V rails is not resolved from the text
extraction and needs the sheet read visually. The 1.8 V rail on ALDO4 is worth
noting: something on this board runs at 1.8 V, and identifying it is a
prerequisite for any level-shifting assumption.

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
because that is a real configuration, not a degraded one. It must also present
that board **with a node attached**, and — the case that actually exercises the
contract — **losing the node while an application is open**. None of the three
can be tested on hardware that does not exist yet.

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
| `Radio` | ✅ one of five chips — **mesh-capable only for some of them** | ❌ on the board — a node supplies mesh | simulated, attached and detached |
| `GNSS` | ✅ one of two modules | ❌ on the board — a node supplies it | simulated, attached and detached |
| `HAPTICS` | ✅ DRV2605L, rail-gated, waveform library | ✅ **bare motor, GPIO 18 + transistor** — on/off and PWM only | logged |
| `AUDIO_OUT` | ✅ MAX98357A | ✅ ES8311 | host audio |
| `AUDIO_IN` | ✅ 1× PDM | ✅ 2× via ES7210 | simulated |
| `IR_TRANSMIT` | ✅ IR12-21C | ❌ | logged |
| `SD_CARD` | ❌ | ✅ | host filesystem |
| `BATTERY_SENSE` | ✅ via AXP2101 | ✅ via AXP2101 | simulated |
| `WIFI` / `BLE` | ✅ ESP32-S3 | ✅ ESP32-S3 | simulated |

This table is the **hardware inventory** — what is physically on each board. It
is not what an application sees. An application asks for a product capability
(`Position`, `MeshMessaging`, `Haptics`) and gets an availability state; the
mapping between the two columns and that answer is
[ADR-0007](../adr/0007-two-capability-layers.md), and it is not one-to-one. A
Waveshare board with a Firefly node attached has ❌ in the `LORA` row and can
still send a mesh message.

A plain boolean also cannot express "a radio is present, but which of five
chips" or "IMU present, but no gyroscope", which is why the hardware layer keeps
a typed descriptor rather than a flag.

---

## Sources

| # | Source |
|---|---|
| S1 | `Xinyuan-LilyGO/LilyGoLib`, `docs/hardware/lilygo-t-watch-s3-plus.md` — MIT |
| S2 | `Xinyuan-LilyGO/LilyGoLib`, `src/LilyGoWatchS3.h` — radio build variants, charge defaults |
| S3 | `Xinyuan-LilyGO/LilyGoLib`, `schematic/T_WATCH-S3 25-03-24.pdf` — **read**, 6 sheets; internal title block says `T_WATCH-2020&GPS_V08` Rev V1.4 |
| S4 | `Xinyuan-LilyGO/LilyGoLib`, `schematic/T-Watch-S3-Plus-GPS V1.0 2025-04-29.pdf` — **read**, 1 sheet (GNSS daughterboard) |
| S5 | `waveshareteam/ESP32-S3-Touch-AMOLED-2.06`, `README.md` — Apache-2.0 |
| S6 | `waveshareteam/ESP32-S3-Touch-AMOLED-2.06`, `Schematic/…-V1.0.pdf` — **read**, 3 sheets, by text extraction; pin-to-net adjacency partially recoverable, see D3 and D13 |
| S7 | ESP Component Registry, `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0 — Apache-2.0 |
| S8 | arduino-esp32 variant `lilygo_twatch_s3/pins_arduino.h` (referenced by S1) |

All checked 2026-08-21.
