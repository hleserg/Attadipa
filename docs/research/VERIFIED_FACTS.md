# Verified Facts

Facts that have been traced to a primary source. Nothing here may be recorded
from a plan document, a blog post, or a plausible-looking library — only from a
datasheet, a schematic for a named board revision, vendor documentation, or the
upstream source itself.

Every entry must carry: the claim, the primary source, the date checked, and —
for hardware — the exact board revision it applies to.

An entry that cannot name its source does not belong here. It belongs in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).

---

## Software / upstream

### MeshCore upstream is `github.com/meshcore-dev/MeshCore`

- **Claim:** the canonical MeshCore repository is `meshcore-dev/MeshCore`.
  The older path `ripplebiz/MeshCore` still resolves but redirects there.
- **License:** MIT (as reported by the GitHub API for the repository).
- **Source:** GitHub API `repos/ripplebiz/MeshCore` returns
  `full_name: meshcore-dev/MeshCore`.
- **Checked:** 2026-08-21.
- **Note:** the repository was actively pushed to on 2026-08-20, so the API
  surface should be treated as moving. A specific revision must be pinned
  before integration work starts — see
  [DEPENDENCIES.md](DEPENDENCIES.md).
- **Not yet verified:** protocol details, crypto primitives, threading model,
  memory requirements, LoRa abstraction, or the companion protocol. None of
  these have been read from source yet.

---

## Toolchain / host environment

### Development host currently lacks an embedded toolchain

- **Claim:** on the development machine (WSL2, Ubuntu 24.04) the following are
  present: cmake 3.28.3, gcc/g++ 13.3.0, Python 3.12.3. The following are
  absent: ESP-IDF (`IDF_PATH` unset), ninja, SDL2, clang-format, ccache.
- **Source:** direct probe of the host, 2026-08-21.
- **Impact:** neither an embedded build nor an LVGL simulator build can be run
  until these are installed. The plain-CMake host build works.

---

## Hardware

Both target boards have been surveyed from vendor documentation, vendor board
support code, and published schematics. The full result — every part, pin, I2C
address, and power rail — lives in [HARDWARE_MATRIX.md](HARDWARE_MATRIX.md).
Recorded here are only the findings that change architecture.

**Neither board has been physically inspected.** Nothing requiring measurement
is verified.

### The two boards share almost nothing but the SoC and the PMU

- **Claim:** of the two target boards, only the ESP32-S3 and the AXP2101 PMU
  are common. Display controller, touch controller, IMU, RTC, audio path,
  storage, and the presence of radio, GNSS, haptics and IR all differ.
- **Source:** S1, S5, S7 (see HARDWARE_MATRIX).
- **Impact:** a capability layer is not a nicety here, it is the only way one
  binary-compatible codebase can address both.

### The Waveshare board has no LoRa and no GNSS

- **Claim:** the Waveshare ESP32-S3-Touch-AMOLED-2.06 carries neither a LoRa
  radio nor a GNSS receiver.
- **Source:** vendor README hardware table and vendor BSP v2.0.0 pin
  definitions; no radio or GNSS net appears in either (S5, S7).
- **Impact:** mesh messaging and navigation have no hardware **on this board**.
- **Amended 2026-08-21:** the claim above is sourced and stands; the inference
  originally drawn from it did not. It read "cannot exist on this board … the UI
  must not offer them". A Firefly node supplies both to the *device*
  ([OWNER_DECISIONS](OWNER_DECISIONS.md) OD-1), so the UI offers them with the
  remedy stated, and withholds only what no configuration of the device can do.
  The lesson worth keeping is narrower than the correction: a fact about a board
  and a fact about a device are different claims, and this line turned one into
  the other without noticing.

### Neither board has a magnetometer

- **Claim:** the T-Watch carries a BMA423 (accelerometer only, no gyroscope);
  the Waveshare carries a QMI8658 (6-axis accel + gyro). Neither board has a
  magnetometer.
- **Source:** S1, S5, S6.
- **Impact:** the specification's magnetometer requirements are **architectural
  only** for now — an API that can accept one later. On real hardware today,
  magnetic heading exists nowhere. Heading from GNSS course-over-ground exists
  wherever GNSS does — which, since OD-1, is not only the T-Watch — and only
  while the user is moving. Whether the node carries a magnetometer is
  unresolved and decides what a compass application can honestly be
  ([OPEN_QUESTIONS](OPEN_QUESTIONS.md) A5/Q2,
  [NODE_PROFILE](../node/NODE_PROFILE.md) N3). The
  "haptics disturb the compass" problem the plan is concerned about cannot be
  observed on either board, because there is no compass. It stays a design
  consideration, not a mitigation to implement.

### The T-Watch LoRa chip is a purchase-time variant

- **Claim:** the T-Watch S3 Plus ships with one of **five** radio chips —
  SX1262 (default), SX1280, CC1101, LR1121, or SI4432 — selected as a board
  revision at build time. The SPI pin assignment is shared across them.
- **Source:** vendor documentation build table (S1) and the conditional
  compilation in `src/LilyGoWatchS3.h` (S2).
- **Impact:** "T-Watch S3 Plus" does not identify the radio. The radio must be
  a variant *within* a capability, and the frequency band differs
  fundamentally between them (sub-GHz vs 2.4 GHz), which affects region
  configuration and mesh interoperability.

### The T-Watch GNSS module is also a variant, with different power needs

- **Claim:** either a u-blox MIA-M10Q or a Quectel LS550G. The LS550G variant
  requires the PMU to enable **DC4 at 850 mV *and* BLDO1 at 3300 mV**.
  Additionally, GNSS sits on BLDO1 only on units with rear BOOT/RST buttons;
  earlier units powered it from DC3.
- **Source:** S1.
- **Impact:** the power-up sequence for GNSS is board-revision dependent and
  cannot be inferred from the product name. Getting it wrong means GNSS
  silently never starts. Assisted-GNSS mechanisms also differ between u-blox
  and Quectel, so no assistance work can be designed until the module is known.

### The T-Watch touch panel has no reset line

- **Claim:** the FT6336U RESET pin is not connected. The vendor states that if
  the touch panel is put to sleep, touch will not work again.
- **Source:** S1 (both the pin map and an explicit warning).
- **Impact:** a direct constraint on the low-power state machine, not a driver
  detail. The Waveshare board *does* have a touch reset line, so the two boards
  cannot share a sleep strategy for touch.

### The T-Watch haptic driver is gated behind a PMU rail

- **Claim:** the DRV2605 enable is on AXP2101 rail BLDO2.
- **Source:** S1.
- **Impact:** haptic feedback has a power-sequencing dependency and a wake-up
  latency. Whatever owns hardware coordination must own this rail, not the
  application.

### The Waveshare vendor BSP does not drive the IMU, PMU, or RTC

- **Claim:** BSP v2.0.0 declares `BSP_CAPS_IMU 0` and `BSP_CAPS_BUTTONS 0`,
  and supports only display, touch, audio, and SD card. The QMI8658, AXP2101,
  and PCF85063 present on the board are handled only in standalone examples.
- **Source:** S7, `include/bsp/esp32_s3_touch_amoled_2_06.h`.
- **Impact:** "the vendor supports this board" does not mean the board's parts
  are usable. Firefly cannot take the BSP as a complete abstraction; it must
  cover the remaining parts itself.

### The Waveshare panel is a CO5300 driven by an SH8601-family driver

- **Claim:** the product documents a CO5300 panel controller; the vendor BSP
  depends on the component `waveshare/esp_lcd_sh8601`.
- **Source:** S5 (README hardware table), S7 (`idf_component.yml`).
- **Status:** not a conflict — the vendor drives the CO5300 through the
  SH8601-family driver. Recorded so this is not later "fixed" as a mistake.

### Vendor toolchain support (evidence for choosing versions, not a decision)

- **Claim:** Waveshare states support for **ESP-IDF v5.5.5 and v6.0.2** and
  Arduino-ESP32 3.3.11; its BSP requires `idf >= 5.3` and `lvgl >=8,<10`.
  LilyGO's library targets Arduino-ESP32 >= 3.3.0-alpha1, and its PlatformIO
  path is pinned to the older 2.0.17 (IDF 4.4.7).
- **Source:** S5, S7, S1.
- **Impact:** feeds the ESP-IDF and LVGL version decisions in
  [DEPENDENCIES.md](DEPENDENCIES.md). Nothing is pinned yet. The LilyGO
  PlatformIO constraint likely does not bind Firefly, which is ESP-IDF-native
  and does not use the Arduino layer.

### Vendor-published power figures exist for the T-Watch

- **Claim:** the vendor publishes current draw per sleep mode (light sleep
  2.38 mA; deep sleep 460–530 µA depending on backup power; deep sleep with
  touch wake 1.08 mA; power off 50 µA) and a 940 mAh battery.
- **Source:** S1.
- **Impact:** these are **vendor numbers under vendor firmware**, useful as an
  order of magnitude and as a target to reproduce. They are not evidence about
  Firefly, and must never be reported as Firefly's measured consumption.
  Note that waking on touch costs roughly twice waking on button — a real
  design trade-off, once confirmed.

---

## Read from the T-Watch schematics (S3, S4)

Until this pass the T-Watch rows rested on the vendor's hardware document (S1)
and its board header (S2). Both schematics have now been read. Everything below
is sourced to the drawing itself.

### The T-Watch has no magnetometer — now from the schematic, not from a feature list

- **Claim:** the board carries exactly one motion part, the BMA423.
- **Source:** S3. An exhaustive search of all six sheets for magnetometer part
  families (`BMM*`, `QMC*`, `MMC*`, `AK[0-9]{4}`, `HMC*`, `LIS*M*`, `LSM*`,
  `IST*`) returns nothing. The full active-part inventory of the drawing is
  ESP32-S3-R8, W25Q128JW, AXP2101, PCF8563, BMA423, DRV2605L, HPD16B3,
  SPM1423HM4H-B, MAX98357A, IR12-21C.
- **Impact:** this was previously an argument from absence in a vendor feature
  table, which is weak. It is now an argument from the schematic, which is the
  right kind of evidence for a negative. All compass work stays architectural.

### The GNSS PPS signal never reaches the SoC

- **Claim:** `PPS` exists as a net on the daughterboard and appears nowhere in
  the main-board schematic.
- **Source:** S4 (net present), S3 (string absent from all six sheets).
- **Impact:** no hardware-disciplined time reference. Any design that wanted
  microsecond time alignment — mesh slotting, timestamped logging — must get it
  from the UART sentence and wear the jitter, or not claim it.

### The IR emitter is active-high and idles low

- **Claim:** GPIO 2 → R64 (0 Ω) → base of Q15, an MMBT3904 NPN low-side switch,
  with the IR12-21C anode at +3V3. Conduction requires GPIO 2 high.
- **Source:** S3 sheet 4.
- **Impact:** the inactive level is **LOW**, and the pin is safe at reset. This
  was previously written into the architecture as an unsourced assumption about
  LED polarity; it is now a fact. It is also the one easter-egg-adjacent
  peripheral that can affect other people's equipment, so its idle state being
  provably off matters more than the pin count suggests.

### The audio amplifier cannot be shut down in firmware

- **Claim:** the MAX98357A `SD_MODE` pin is set by R14 = 1 MΩ with R74 and R76
  not fitted. No GPIO is connected to it.
- **Source:** S3 sheet 6.
- **Impact:** the amplifier is enabled whenever `SPK_VDD` is up. "Mute" is a
  rail operation, not a pin operation. Any power state that wants the amplifier
  off must own the rail — which makes the rail service load-bearing rather than
  a convenience.

### The power button never reaches a GPIO

- **Claim:** SW7 wires to the AXP2101 `PWRON` pin.
- **Source:** S3 sheet 1.
- **Impact:** button presses arrive as PMU interrupts over I2C, not as GPIO
  edges. Press duration, long-press and power-off behaviour are PMU register
  policy. An input service that only knows about GPIO edges cannot see the most
  important button on the watch.

### The radio has an eighth line the vendor header omits

- **Claim:** the module fitted on the drawing is an `HPD16B3` with an
  SX1262-class pinout, and `DIO3` is wired to **GPIO 6**.
- **Source:** S3 sheet 5, pin by pin: 1 `VCC`←`GPS_VDD`, 3 `NRESET`←IO8,
  4 `BUSY`←IO7, 5 `DIO1`←IO9, 6 `DIO3`←IO6, 7 `MISO`←IO4, 8 `MOSI`←IO1,
  9 `SCK`←IO3, 10 `NSS`←IO5, 12 `ANT`.
- **Impact:** GPIO 6 was entirely absent from the pin map. On SX1262 designs
  `DIO3` is commonly the TCXO supply and sometimes a second interrupt; which one
  it is here decides whether the radio will get a clock at all. Do not write a
  radio driver before answering it — OPEN_QUESTIONS D10.

### Unplugging the GNSS module removes the BOOT and RESET buttons

- **Claim:** the 13-pin FPC carries `IO0` (pin 2) and `RST/EN` (pin 6) in
  addition to the GNSS UART, `GPS_LDO` and `IO10`.
- **Source:** S3 sheet 2, S4.
- **Impact:** bring-up instructions that say "hold BOOT" are false for a board
  running without the daughterboard. Also puts main-I2C `SDA` on a detachable
  connector.

### Three of four strapping pins carry functional signals

- **Claim:** GPIO 0 = BOOT button, GPIO 3 = LoRa `SCK`, GPIO 45 = display
  backlight, GPIO 46 = I2S `DIN`.
- **Source:** S3 sheets 2, 4, 5, 6.
- **Impact:** GPIO 45 selects `VDD_SPI` voltage at reset. It is currently safe
  because the backlight is active-high through an NPN, so it is dark at reset —
  but the safety is a consequence of the circuit, not of anything the firmware
  does. It belongs in the board file as a constraint.

### Two rails the vendor calls unused are loaded on the schematic

- **Claim:** S1 lists ALDO1 and DLDO1 as unused. S3 shows ALDO1 (pin 18) driving
  the `+3V3` net and the `DLDO1/DC1SW` pin (pin 20) driving `SPK_VDD`.
- **Source:** S1 vs S3 sheet 1.
- **Status:** **CONFLICTING.** The `DLDO1/DC1SW` half is reconcilable — one pin,
  two selectable functions. The ALDO1 half is not.
- **Impact:** if the schematic is right, `+3V3` is switchable and carries the
  accelerometer, RTC, haptic driver, microphone and IR emitter. That is the
  difference between a deep-sleep state that works and one that silently keeps
  five parts alive. Resolve on hardware — OPEN_QUESTIONS H8.

### Smaller findings

- BMA423 `INT2` is bonded out but not routed (R12, R15 not fitted). Only `INT1`
  → GPIO 14 exists, so all accelerometer events share one line.
- The PMU drives a charge-indicator LED on `CHGLED` through R182 (100 Ω). It is
  configured over I2C, not by a GPIO.
- USB `D−`/`D+` land on GPIO 19 / GPIO 20 — the S3 native USB pins — so
  USB-Serial-JTAG and USB-OTG are both physically available.
- An `MS412FE` rechargeable cell backs the RTC through D14 (1N4148) and 10 kΩ;
  the GNSS daughterboard carries a second one for hot start.
- A `MSK12C02-HB` slide switch sits in series with the battery. Firmware can
  neither sense nor override it.
- The microphone `SELECT` pin is strapped (R80, R81 not fitted).
- The backlight is one series × three parallel LEDs, I_F = 3 × 15 mA →
  **45 mA at full brightness**, V_F 3.0–3.3 V. Panel is `QT154C2408`.
- The touch `RESET` pull-up R39 is marked `4K7/NC` — not fitted. This is the
  mechanism behind the vendor's warning that a slept touch panel never wakes.

### The schematic's own title block is wrong

- **Claim:** the file published as the S3 schematic has a title block reading
  `T_WATCH-2020&GPS_V08`, Rev V1.4, Friday 8 January 2021.
- **Source:** S3, all six sheets.
- **Impact:** the contents are unambiguously S3-class, so this is a stale
  nameplate rather than the wrong document. But it means the drawing cannot be
  used to establish which board revision anything applies to. Revision still
  comes from inspecting a physical unit — OPEN_QUESTIONS A1.

---

## Read from the Waveshare schematic (S6)

The same gap the T-Watch had: the schematic was cited but not read, while the
Waveshare part inventory rested entirely on the vendor README and BSP — the same
BSP already demonstrated to be an incomplete description of its own board.

### The Waveshare board **does** have haptics — the earlier entry was wrong

- **Claim:** a vibration motor on connector `J1`, driven from **GPIO 18** through
  R12 (4.7 kΩ) into Q1 (MMBT3904, NPN), with the motor supplied from **BLDO2**.
- **Source:** S6, net `MOTOR`.
- **Correction:** the matrix previously recorded "Haptics — none found", because
  a search for haptic *driver parts* found none. There is no driver IC — the
  motor is switched directly by a GPIO. Searching for the wrong noun produced a
  false negative, and it was recorded with the same weak argument-from-absence
  that the magnetometer claim used to rest on.
- **Impact:** both boards have haptics and the two implementations are not
  interchangeable. The T-Watch has a DRV2605L with a waveform library, an I2C
  interface and a rail-warmup latency; the Waveshare board has on, off and PWM.
  `has(Capability::Haptics)` is true on both and means materially different
  things — the clearest live justification for the typed descriptors in
  [ADR-0001](../adr/0001-capability-model.md).

### Waveshare memory: 32 MB flash, 8 MB PSRAM

- **Claim:** external flash is `GD25Q256EYIGR` (U3) — 256 Mbit quad SPI, i.e.
  **32 MB**. The SoC is a bare `ESP32-S3R8`, not a module.
- **Source:** S6.
- **Impact:** resolves D1. Twice the T-Watch's flash, on the board with 3.57×
  the pixels. Also means **both** boards carry the `R8` marking, so the quad-vs-
  octal PSRAM question (D12) is one question with one answer for both targets.

### The Waveshare board has buttons; its BSP does not

- **Claim:** at least two tactile keys on the drawing (`Key1` adjacent to `BOOT`,
  and `Key3`), plus `PWRON` on the PMU.
- **Source:** S6.
- **Impact:** the vendor BSP declares no buttons. This is now a fourth item in
  the same pattern as `BSP_CAPS_IMU 0` — the BSP describes what the BSP drives,
  never what the board carries. Which GPIO each key uses is not resolved from
  text extraction and remains D5.

### Waveshare AXP2101 rail map, and a 1.8 V rail

- **Claim:** ALDO1 → `VL1_3.3V`, ALDO2 → `VL2_3.3V`, ALDO3 → `VCC3V`,
  ALDO4 → **`VL3_1.8V`**, BLDO2 → the vibration motor.
- **Source:** S6.
- **Impact:** the vendor BSP does not configure the PMU at all, so this map is
  the only description of the board's power topology that exists. The 1.8 V rail
  matters: something on this board is not 3.3 V, and identifying it is a
  prerequisite for any level assumption — D13.

### A conflict about the SD card interface

- **Claim:** the BSP configures SDMMC 1-bit on GPIO 1/2/3. The schematic labels
  those same nets `MOSI`, `SCK` and `MISO`, and shows a chip-select near GPIO 17.
- **Source:** S7 (BSP) vs S6 (schematic).
- **Status:** **CONFLICTING** — or, more likely, one board wiring that supports
  both modes with the BSP choosing one. Either way the chip-select on GPIO 17 is
  a pin the pin map did not have. D14.

### What is still not resolved from this schematic

Text extraction from a schematic PDF recovers part numbers and net names
reliably and pin-to-net adjacency only sometimes. Two things need the sheets read
visually rather than greped:

- the `J3` expansion header pinout — at least 29 pins (D3);
- which loads sit on which of the three 3.3 V rails (D13).

Recorded as PARTIAL rather than left blank, so the gap is visible.
