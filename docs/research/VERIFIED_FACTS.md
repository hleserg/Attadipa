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

That promise was half true until 2026-08-22. The Waveshare peripheral table had
been written without the `I2C addr` and `Power rail` columns the T-Watch table
carries, so for that board neither existed while this sentence said they did —
which sends a reader looking for data rather than for its absence. The addresses
are there now, each cited. The rails are still D13.

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
  must not offer them". An Attadipa node supplies both to the *device*
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

### The T-Watch radio chip is a purchase-time variant, and two of the five are not LoRa

- **Claim:** the T-Watch S3 Plus ships with one of **five** radio chips —
  SX1262 (default), SX1280, CC1101, LR1121, or SI4432 — selected as a board
  revision at build time. The SPI pin assignment is shared across them.
  **CC1101 and Si4432 have no LoRa modulator**; they are FSK/OOK-family parts.
  **SX1280 is LoRa at 2.4 GHz only.** At MeshCore `d929643` exactly one of the
  five — the SX1262 — is a supported radio.
- **Source:** vendor documentation build table (S1) and the conditional
  compilation in `src/LilyGoWatchS3.h` (S2) for the variant list. For the
  modulation and support claims: RadioLib 7.7.1 (`510e00c`) — the `setSpreadingFactor`
  API is absent from `CC1101` and `Si443x`, and the module list calls them FSK
  parts — and MeshCore `d929643`, whose `RADIO_CLASS` set across 87 variants
  contains only `SX1262` of the five, with `RADIOLIB_EXCLUDE_CC1101=1` in the
  root `platformio.ini`.
- **Evidence level: PARTIAL.** The modulation, band and power figures are read
  from driver source, not from the TI and Silicon Labs datasheets, which refused
  automated retrieval. Confirming them from primary sources is **R1**.
- **Impact:** "T-Watch S3 Plus" does not identify the radio, and "it has a
  radio" does not mean "it can join the mesh". The product capability
  `MeshMessaging` is derived from the fitted chip's modulation, band and
  upstream support — never asserted from presence
  ([ADR-0003](../adr/0003-radio-not-lora.md),
  [ADR-0007](../adr/0007-two-capability-layers.md)).

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
  are usable. Attadipa cannot take the BSP as a complete abstraction; it must
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
  PlatformIO constraint likely does not bind Attadipa, which is ESP-IDF-native
  and does not use the Arduino layer.

### Vendor-published power figures exist for the T-Watch

- **Claim:** the vendor publishes current draw per sleep mode (light sleep
  2.38 mA; deep sleep 460–530 µA depending on backup power; deep sleep with
  touch wake 1.08 mA; power off 50 µA) and a 940 mAh battery.
- **Source:** S1.
- **Impact:** these are **vendor numbers under vendor firmware**, useful as an
  order of magnitude and as a target to reproduce. They are not evidence about
  Attadipa, and must never be reported as Attadipa's measured consumption.
  Note that waking on touch costs roughly twice waking on button — a real
  design trade-off, once confirmed.

### The BMA423 counts steps, and its datasheet does not say how

- **Claim:** the BMA423 has a **32-bit hardware step counter** in registers
  `0x1E`–`0x21` (`STEP_COUNTER_0`…`_3`), and the datasheet documents those four
  registers with one line each: `DESCRIPTION: Application note – Wearable
  feature set`. The behaviour — power mode, required ODR, whether the count
  survives a soft reset, whether it accumulates while the host sleeps — is in a
  **separate document**, Bosch's *Wearable Feature Set* application note
  `BST-MAS-AN032`, which returned HTTP 403 on two attempts.
- **Source:** BMA423 Data Sheet, revision 2.0, `BST-BMA423-DS004-00`, August
  2019, p. 53 and p. 1 (*"Plug 'n' Play Step-Counter solution with watermark
  functionality"*). Confirmed against Bosch's own reference driver v1.1.4:
  `bma423_step_counter_output()` reads four bytes from
  `BMA4_STEP_CNT_OUT_0_ADDR = 0x1E` into a `uint32_t`.
- **Checked:** 2026-08-22, for the T-Watch S3 Plus.
- **Impact:** the pedometer is mandatory ([OD-6](OWNER_DECISIONS.md)).
  **Superseded 2026-08-22 by the entry below** — the behaviour is documented,
  in a revision of this same datasheet that Bosch withdrew. Full reading in
  [PEDOMETER_PARTS](PEDOMETER_PARTS.md).

### Bosch deleted the step-counter chapter from the BMA423 datasheet between revisions

- **Claim:** the behaviour revision 2.0 defers to application note
  `BST-MAS-AN032` was **printed in the datasheet itself** until three months
  earlier. Revision 1.0 (`BST-BMA423-DS000-00`, August 2017) and revision 1.1
  (`BST-BMA423-DS000-01`, May 2019) both carry a *"Step Detector / Step
  Counter"* chapter, a *"Minimum Bandwidth Settings"* section, the phone/wrist
  preset tables and the per-field configuration list, pp. 32–37 — and the text
  is byte-identical between the two. Revision 2.0 (`BST-BMA423-DS004-00`,
  August 2019) replaces all of it with a pointer, and changes the document
  number series from `DS000` to `DS004`.
- **Source:** revision 1.1 retrieved 2026-08-22 from the Watchy project's
  mirror, `watchy.sqfmi.com/assets/files/BST-BMA423-DS000-1509600-950150f51058597a6234dd3eaafbb1f0.pdf`,
  SHA-256 `98b85747bd983435b2921266401cbeb095a57e2274b1f5c49f7f04145f22de04`,
  2 363 646 bytes. Revision 1.0 from `opensourceinstruments.com`, used only to
  confirm the chapter is unchanged. Revision 2.0 from DigiKey. Bosch's own site
  returned **HTTP 403** for both the note and the datasheet.
- **Checked:** 2026-08-22.
- **Impact:** four questions this file recorded as `UNKNOWN` are answered, and
  one claim it recorded is **wrong** — see the two entries below. The general
  lesson is the one [ADR-0003](../adr/0003-radio-not-lora.md) already teaches in
  another subsystem: *"the datasheet"* is not a document, it is a document **at
  a revision**, and the newest is not always the most complete. Where a current
  datasheet defers to something unobtainable, look backwards before declaring
  the fact unknowable.

### The BMA423 step counter runs in low-power mode, and 50 Hz is the floor

- **Claim:** the feature engine takes acceleration samples *"acquired at 50Hz"*.
  In performance mode (`ACC_CONF.acc_perf_mode = 0b1`) the features work at any
  ODR; in low-power mode (`0b0`) *"the ODR must be set to minimum 50 Hz for the
  most features except Double Tap/Tap"*, and 200 Hz for tap. Violating it sets
  `INTERNAL_STATUS.odr_50hz_error` — it is detectable, not silent. Counting
  itself needs no host transaction: the sensor duty-cycles itself, and *"in all
  global power configurations both register contents and FIFO contents are
  retained."*
- **Source:** BMA423 Data Sheet revision 1.1, pp. 20–21 and 32; register `0x2A`.
- **Checked:** 2026-08-22.
- **Impact:** the power line for step counting is the **50 Hz low-power figure,
  13–14 µA `ESTIMATED`** — not 42 µA and not 150 µA. Wanting double-tap as well
  costs 3×. Whether the counter survives *the board's* sleep is now a rail
  question about the AXP2101, not a sensor question.

### CORRECTION — the BMA423 watermark field carries an implicit ×20

- **Claim:** `BMA423_STEP_CNTR_WM_MSK = 0x03FF` is 10 bits, but the field
  *"holds implicitly a 20x factor, so the range is 0 to 20460, with resolution
  of 20 steps"*. A written 10 interrupts every 200 steps, and *"as the steps are
  buffered internally, the output may be triggered between 200-210 steps."*
  Bosch's driver does **not** apply the factor —
  `bma423_step_counter_set_watermark()` writes the argument raw.
- **Source:** BMA423 Data Sheet revision 1.1, p. 36; `bma423.c` v1.1.4 l. 1049.
- **Checked:** 2026-08-22.
- **Impact:** **this corrects an earlier reading in this repository.** LilyGo's
  `setStepCounterWatermark(1)` was recorded as an interrupt *per step*; it is an
  interrupt every **20** steps. Roughly every 15 s at walking cadence, not
  ~1 Hz — an order of magnitude, and it lands in the T-061 power arithmetic.

### The BMA423's step counter lives in a 6 144-byte blob the host uploads at every boot

- **Claim:** the feature engine is not resident. `BMA4_CONFIG_STREAM_SIZE = 6144`
  bytes are streamed to `BMA4_FEATURE_CONFIG_ADDR` (`0x5E`), after which the host
  must **wait 150 ms** and then read `BMA4_INTERNAL_STAT` (`0x2A`) expecting
  `BMA4_ASIC_INITIALIZED` (`0x01`). The step-counter watermark is **10 bits**
  (`0x03FF`, so 0–1023 **as written; the sensor multiplies by 20** — see the
  correction above), and **value 0 does not mean "every step"** — it selects
  the separate *step detector* interrupt.
- **Source:** Bosch BMA423 reference driver v1.1.4 —
  `bma4_write_config_file()` in `bma4.c`, and the masks in `bma423.h` /
  `bma4_defs.h`.
- **Checked:** 2026-08-22.
- **Impact:** 150 ms of the boot budget is spent before a step can be counted,
  every time. A soft reset (`0xB6` → `0x7E`) **does** drop it — revision 1.1
  §4.2: *"Initialization has to be performed as well after every POR or soft
  reset"*, the reset being *"largely equivalent to a power cycle"* — so every
  reset is a hole in the day's total that OD-6's *no interpolation* rule
  requires be reported rather than filled. The block is also read–modify–write
  as a whole, so two callers cannot configure two features independently.

### The QMI8658A's datasheet has deleted its pedometer

- **Claim:** the pedometer is a **variant-and-revision** question, not a part
  question.
  **QMI8658C** (`13-52-27`, Rev A, 20 June 2022) documents it fully: feature
  list p. 1, chapter 11, a **24-bit** count in `STEP_CNT_LOW/MIDL/HIGH`
  (`0x5A`–`0x5C`), `CTRL8.Pedo_EN`, and CTRL9 commands `0x0D` (configure) and
  `0x0F` (reset count).
  **QMI8658A Rev A** (`13-52-25`, 20 June 2022) documents the identical feature.
  **QMI8658A Rev D** (`QST-PD-B002-22`, current) **does not**: its feature list
  reads *"Integrated Tap, Any-Motion, No-Motion, Significant-Motion detection"*,
  there is no chapter on the pedometer, and a search of the whole document finds
  **no `STEP_CNT` register and no `Pedo_EN` bit**. The feature is not marked
  deprecated or reserved — it is gone from the document, registers included.
- **Source:** the three QST datasheets named above, read 2026-08-22.
- **Impact:** [HARDWARE_MATRIX](HARDWARE_MATRIX.md) records the Waveshare board's
  IMU as *"QMI8658 / QMI8658C"* — the variant is **`UNKNOWN`**, the vendor BSP
  does not touch the IMU so there is no code to read the answer from, and the two
  variants differ on precisely the feature OD-6 makes mandatory. Reading a step
  count out of a QMI8658A and believing it would be relying on a feature its
  current datasheet does not admit to having. This is the ADR-0003 pattern —
  the part number does not tell you what you have — arriving in a second
  subsystem.

### The QMI8658 costs at least three times the BMA423

- **Claim:** accelerometer-only, gyroscope disabled, typical at 1.8 V and 25 °C:
  the QMI8658C draws **30 / 35 / 42 / 55 µA** at low-power ODRs of 3 / 11 / 21 /
  128 Hz, and 132–182 µA in high-resolution mode. Its idle states are 15 µA
  (power-on default), 8 µA (low power) and 6 µA (power-down, configuration and
  output registers preserved). Low-power mode is available **only with the
  gyroscope disabled**. The BMA423, for comparison, is **13 µA at 50 Hz** in
  low-power mode, 150 µA in performance mode and 3.5 µA suspended — though its
  low-power table is marked by Bosch itself as *"based on limited lab
  measurements. Only for reference."*
- **Source:** QMI8658C datasheet §3.8 tables 15 and 22, and table 31; BMA423
  datasheet electrical characteristics and the low-power current table.
- **Checked:** 2026-08-22.
- **Impact:** both sets are **vendor typicals, not measurements on our boards**.
  They are the budget to design against and the target to reproduce, never a
  figure to report as Attadipa's. A 6-axis IMU is not a cheaper accelerometer.

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

- **Claim:** a vibration motor on pads `P1`/`P2`, driven from **GPIO 18** through
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
  the product capability `Haptics` is available on both and means materially
  different things at the hardware layer — the clearest live justification for
  keeping a typed descriptor below the service boundary
  ([ADR-0007](../adr/0007-two-capability-layers.md)).

### The `R8` in ESP32-S3R8 means octal PSRAM, and the datasheet says so

- **Claim:** the PSRAM in an `ESP32-S3R8` is octal, not quad.
- **Source:** ESP32-S3 Series Datasheet v2.2, §1.2 Table 1-1 "ESP32-S3 Series
  Comparison", p. 13: `ESP32-S3R8 | — | 8 MB (Octal SPI) | -40 ~ 65 °C | 3.3 V`.
  The table contains **no 8 MB quad in-package variant at all** — the only quad
  in-package parts are the 2 MB `RH2`, `R2` (EOL) and `FH4R2`. Footnote 3 names
  the octal set outright: "For chips with Octal SPI PSRAM (ESP32-S3R8,
  ESP32-S3R8V, and ESP32-S3R16V)…". `R8` and `R8V` differ by `VDD_SPI` voltage,
  3.3 V against 1.8 V, not by bus width.
- **Corroboration:** five of the six vendor examples for the Waveshare board ship
  `CONFIG_SPIRAM_MODE_OCT=y` with `CONFIG_SPIRAM_IGNORE_NOTFOUND` unset — a build
  that aborts at boot if octal PSRAM is not found. And GPIO33-37, which Datasheet
  Table 2-14 populates as DQ4-DQ7 and DQS **only** in the Octal SPI column, sit
  unrouted on the schematic with no-connect markers. That is a falsification test
  the board passed: any of those five routed to a peripheral would have refuted
  octal.
- **Status:** VERIFIED for the Waveshare (D12a). **Not transferred to the
  T-Watch** (D12b): the same marking implies the same answer, but a LilyGO
  document describing that board's PSRAM as QSPI has not been re-read against
  Table 1-1 and stands as a live conflict.
- **Why it is written down:** OPEN_QUESTIONS recorded this as recollection —
  Espressif's scheme is "*understood* to use the `R8` suffix for octal PSRAM —
  that last part is recollection and must itself be checked against the
  datasheet". It has been.

### The Waveshare main I2C bus carries six devices, not four

- **Claim:** the ES8311 audio codec and the ES7210 microphone ADC are I2C control
  slaves on the same bus as the touch, PMU, IMU and RTC.
- **Source:** the vendor BSP creates one `i2c_master_bus`
  (`esp32_s3_touch_amoled_2_06.c:93`) and hands that same handle to the ES8311
  (`:262`), the ES7210 (`:310`) and the touch IO (`:494`).
- **Why it matters:** both parts appear in HARDWARE_MATRIX as "I2S", which is
  their *data* path. Their control path is two more addresses on SDA 15 / SCL 14,
  and a board profile that omits them is wrong about the bus.
- **Status:** VERIFIED from vendor source. Each address is in HARDWARE_MATRIX
  with its own citation; `0x18` and `0x40` are both schematic-strapped.

### Waveshare memory: 32 MB flash, 8 MB PSRAM

- **Claim:** external flash is `GD25Q256EYIGR` (U3) — 256 Mbit quad SPI, i.e.
  **32 MB**. The SoC is a bare `ESP32-S3R8`, not a module.
- **Source:** S6.
- **Impact:** resolves D1. Twice the T-Watch's flash, on the board with 3.57×
  the pixels.
- **What this does NOT settle, and an earlier version of this entry said it
  did:** both boards carry the `R8` marking, and it is tempting to read that as
  one question with one answer for both. It is not. `R8` is verified as octal on
  the Waveshare — see *The R8 in ESP32-S3R8 means octal PSRAM* above — and that
  is **D12a**. **D12b**, the T-Watch, stays `CONFLICTING`: a LilyGO document
  describes that board's PSRAM as QSPI, and the marking implying otherwise is an
  inference, not a reading. This paragraph used to assert the transfer, twenty-
  five lines below the section that splits it, so the answer a reader got
  depended on which one they landed on first — which is the exact propagation
  failure this file exists to prevent, committed inside the change that fixed
  three others.

### The Waveshare board has buttons; its BSP does not

- **Claim:** at least two tactile keys on the drawing (`Key1` adjacent to `BOOT`,
  and `Key3`), plus `PWRON` on the PMU.
- **Source:** S6.
- **Impact:** the vendor BSP declares no buttons. This is now a fourth item in
  the same pattern as `BSP_CAPS_IMU 0` — the BSP describes what the BSP drives,
  never what the board carries. Which GPIO each key uses is not resolved from
  text extraction and remains D5.
- **Updated 2026-08-23 from the physical unit (S9): there are exactly two
  pressable buttons on the assembled case.** The owner counted them
  ([#99](https://github.com/hleserg/Attadipa/issues/99)). This settles the
  schematic's *"at least two"* and immediately raises the question that matters
  more: the drawing names **three** candidate inputs — `Key1`, `Key3` and
  `PWRON` — so **at most two of the three reach a finger, and which two is
  `UNKNOWN`**.
- **Why that residue is a design constraint rather than a wiring detail.**
  `PWRON` is an AXP2101 input, not an SoC GPIO, and it can bring the system up
  from a state in which the SoC is not running at all. A key on a GPIO cannot
  always do that. So *which* of the two physical buttons is which decides what
  the wake story can be, and whether Child Mode can have a physical control that
  works when the screen does not. One long-press on each button answers it, and
  no instrument is required — the watch either turns off or it does not.

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

---

## Read off a physical Waveshare unit (S9)

One `ESP32-S3-Touch-AMOLED-2.06` arrived on 2026-08-22 and was opened. Everything
below is silkscreen, a printed label, or an empty footprint — the three things a
photograph is actually good for. Nothing here rests on a marking that needed
magnification the camera did not have, and the items that do need one are in
[WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §3 as bench readings
still to take.

### The Waveshare cell is *marked* 400 mAh — and probably does not hold it

- **Claim:** the battery is a `402728` pouch cell manufactured 2026-07-11,
  **labelled 3.7 V, 400 mAh**. `402728` is the geometry: 4.0 mm × 27 mm × 28 mm.
  **What is verified is the reading of the label, not the capacity behind it.**
  400 mAh at 3.7 V in 3.024 cm³ implies 132.3 mAh/cm³, against an 87–102 band
  observed across 51 datasheet cells from four manufacturers at footprints
  ≤ 32 mm — +22 % on the densest cell in that sample. Honest expectation
  **250–310 mAh**, `ESTIMATED` —
  [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1. One weighing settles it (T-106 M3):
  6.0–6.5 g is consistent with 280–330 mAh, and only 7.5–8 g with a real 400.
- **Source:** S9 — printed on the cell's own label.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `UNKNOWN` — the schematic shows the cell on `BAT1` through the AXP2101
  charge path and states no capacity, and the vendor README does not either.
- **Impact, and it is the largest single thing the unit told us.** The T-Watch
  S3 Plus carries 940 mAh (S1). This board carries 400 and drives an **emissive**
  panel, where what is drawn decides what is drawn *from*. The day theme's
  gamma-decoded emissive load is 13.9× the night theme's on the same pixels
  (`ESTIMATED`, [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §1). The expensive
  theme and the small cell are on the same board. That does not by itself yield
  a runtime — that needs a measured panel current at a known APL, which is
  `UNKNOWN` — but it makes "which theme is default here" a power decision rather
  than a taste one. T-095.

### The ten-pad expansion row, and two of its pads are the I2C bus

- **Claim:** ten plated pads along the board's bottom edge, silkscreened
  `VBUS · GND · D+/IO20 · D-/IO19 · IO15 · IO14 · RXD · TXD · GND · 3V3`.
- **Source:** S9 — each pad is individually labelled in silkscreen.
- **Board revision:** as above.
- **Impact:** `IO15` and `IO14` are printed as bare GPIO numbers and are **the
  main I2C bus** (S6: `SDA 15, SCL 14`), carrying the AXP2101, the PCF85063ATL,
  the FT3168, the QMI8658, the ES8311 and the ES7210. Driving them as
  general-purpose pins takes down power management, the clock, touch and motion
  at once. The only genuinely free channel on this row for an attached Attadipa
  node is `RXD`/`TXD`. T-096.
- **Not the same thing as `J3`** — the 29-pin header D3 is still open about. This
  row is separate and is now fully known.

### The IMU's board-frame axes are printed next to it

- **Claim:** a silkscreened axis triad beside the IMU: **X** toward the battery
  edge, **Y** toward the USB-C edge, **Z** drawn as ⊙ — out of the face the part
  is mounted on, which is the face turned away from the display.
- **Source:** S9.
- **Board revision:** as above.
- **Impact:** half of OPEN_QUESTIONS **H15**. The board frame is now known; how
  the board is rotated inside the case is not, and a wrist-raise gesture needs
  both. Cheap to finish: tilt the assembled watch through known angles and read
  raw axes.

### The vibration motor is not fitted

- **Claim:** the `MOTOR` pads (`P1`/`P2`) are bare — no solder, no wire, no part — and
  the coin-motor footprint beside them is empty, on the unit received. The drive
  circuit S6 describes (GPIO 18 → R12 → Q1 → BLDO2) is present and correct.
- **Source:** S9. **Designator corrected 2026-08-22** — these pads were recorded
  as `J1`, which is in fact the *battery* connector; see
  [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1.1. The correction also resolves an
  internal contradiction: the battery plug is visibly mated to a two-pin header on
  this unit, so `J1` cannot have been two bare pads. **The finding is unchanged.**
- **Board revision:** as above. **`OBSERVED` on one unit, not `VERIFIED` for the
  product** — whether Waveshare ships a motor loose, whether another production
  run populates it, and what the listing promises are three unanswered questions.
- **Impact:** `Capability::Haptics` resolves to `Unsupported` on this unit, and
  `Unsupported` is the terminal value in the `Availability` enum — the one that
  must be stable at runtime and must never be offered to the user as fixable.
  Nothing in firmware can tell an NPN driving an absent motor from one driving a
  present motor, so this cannot be detected and must be configured. T-097.

### The flash is a separate package, and it is GigaDevice

- **Claim:** a GigaDevice-branded SOP-8 sits beside the SoC. The brand is legible;
  the part number is not.
- **Source:** S9, corroborating S6's `GD25Q256EYIGR` at `U3`.
- **Impact:** modest but structural. Whatever is in the SoC package is **not
  flash**, which is what an `R8` suffix means. It corroborates the octal-PSRAM
  conclusion without re-deriving it. Capacity remains the schematic's 32 MB,
  unconfirmed on silicon — `esptool.py flash_id` settles it.

### Both microphones are populated

- **Claim:** two MEMS microphones, silkscreened `MIC1` and `MIC2`, at opposite
  ends of the board's left edge, both fitted.
- **Source:** S9, confirming S6's "dual digital microphones" on the ES7210.

### The speaker is an AAC part on wires, not a connector

- **Claim:** `AAC210602A1`, lot `15771`, a metal-can micro-speaker in the back
  cover, its red/black pair soldered to `+`/`−` pads at the board's bottom-right.
  Impedance and rated power are not published for this part number — `UNKNOWN`.
- **Source:** S9.
- **Impact:** small and practical. Both the speaker and any future motor attach
  by solder, so opening this watch twice means desoldering twice.

## Read off the silicon of that unit (S10)

`espefuse v5.3.1 summary` and `esptool v5.3.1 flash-id`, run over the board's own
USB-Serial/JTAG port on 2026-08-22. This is the first evidence in this repository
that came from neither a document nor a camera. The full reading and its
redactions are in [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md); the three
facts that change what may be written are here.

### The die is fused as 8 MB AP Memory PSRAM at 3.3 V — so the part is `R8`, not `R8V`

- **Claim:** `PSRAM_CAP = 8M`, `PSRAM_CAP_3 = False`, `PSRAM_VENDOR = AP_3v3`,
  `PSRAM_TEMP = 85C`. `esptool` renders the same fuses as
  `Embedded PSRAM 8MB (AP_3v3)`. `PIN_POWER_SELECTION = VDD_SPI` puts GPIO33–37
  on the memory rail.
- **Source:** S10.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** D12a was `RESOLVED` by inference from the package marking against
  ESP32-S3 Series Datasheet v2.2 Table 1-1.
- **What it does and does not prove.** It proves the capacity, the vendor and the
  3.3 V rail on *this die*, which eliminates `ESP32-S3R8V` (1.8 V) and every 2 MB
  quad variant. It does **not** state bus width. The step from "8 MB in package"
  to "octal" is still Table 1-1's, and it holds because that table contains no
  8 MB quad in-package part. Both legs of D12a are now supported, one by document
  and one by silicon.
- **Impact:** GPIO33–37 are confirmed unavailable to any application — not
  argued from an unrouted schematic net, but fused. The GPIO budget loses five
  pins for good.

### The flash is outside the package, and the fuses say so

- **Claim:** JEDEC ID `0xC8 0x4019` — GigaDevice, `0x40` = GD25Q SPI family,
  `0x19` = 2^25 bytes = 32 MB. `FLASH_TYPE = 4 data lines` (quad).
  `VDD_SPI_FORCE` and `VDD_SPI_XPD` are set and `VDD_SPI_TIEH` reads
  `VDD_SPI connects to VDD3P3_RTC_IO`, i.e. 3.3 V. `FLASH_CAP`, `FLASH_TEMP` and
  `FLASH_VENDOR` in BLOCK1 are all unprogrammed.
- **Source:** S10.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `VERIFIED` from the schematic alone (`GD25Q256EYIGR` at U3).
- **Impact:** the schematic and the silicon agree, and the three unprogrammed
  BLOCK1 fields independently confirm there is no in-package flash competing for
  the bus. The combination the board actually is — **octal PSRAM in package,
  quad flash outside it, both at 3.3 V** — is now settled from two directions.

### The chip is revision v0.2, and a build must not ask for more

- **Claim:** `WAFER_VERSION_MAJOR = 0`, `WAFER_VERSION_MINOR = 2`; `esptool`
  reports `ESP32-S3 (QFN56) (revision v0.2)`. Crystal 40 MHz. ADC calibration
  (`BLK_VERSION_MAJOR = ADC calib V1`, `ADC1_INIT_CODE_*`, `ADC1_CAL_VOL_*` and
  the ADC2 counterparts) and `TEMP_CALIB` are burned.
- **Source:** S10.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `UNKNOWN` — no document states which revision a shipped board carries,
  and it is not a property of the board design.
- **Impact:** ESP-IDF's `CONFIG_ESP32S3_REV_MIN_*` gates boot. A build whose
  minimum revision exceeds 0 will be **refused by the bootloader on this unit**.
  Nothing sets it higher today; this is recorded so nobody raises it blind. The
  burned calibration fuses mean ESP-IDF's ADC calibration works rather than
  falling back to a nominal curve. **Which errata apply to v0.2 is no longer
  `UNKNOWN`**: the ESP32-S3 Errata sheet was read on 2026-08-22 —
  **v1.3, released 2025-03-31, md5 `64ffc580e78b5ab3c6c5d990e0500e38`** — and
  **all eight apply to v0.2**, seven of them with `No fix scheduled`. There is no
  revision beyond v0.2 in existence, so being on v0.2 is not being behind
  anything ([ESP32S3_ERRATA_V02](ESP32S3_ERRATA_V02.md)). The cost of the
  CACHE-126 workaround is the part that stays `UNKNOWN`, and it is `NOT
  MEASURED`.

### Nothing has been burned — every recovery path is open

- **Claim:** `WR_DIS = 0`, `RD_DIS = 0`, `SPI_BOOT_CRYPT_CNT = Disable`,
  `SECURE_BOOT_EN = False`, all three `SECURE_BOOT_KEY_REVOKE*` false, all six
  `KEY_PURPOSE_*` = `USER` with `BLOCK_KEY0..5` zero, `DIS_DOWNLOAD_MODE = False`,
  `ENABLE_SECURITY_DOWNLOAD = False`, `DIS_PAD_JTAG = False`,
  `SOFT_DIS_JTAG = 0`, `DIS_USB_SERIAL_JTAG = False`, `CUSTOM_MAC` zero,
  `SECURE_VERSION = 0`.
- **Source:** S10. `espefuse summary` reads; it burns nothing, and
  `espefuse burn_efuse` was not run.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Impact:** the unit is in the state the "never irreversible without being
  asked" rule exists to preserve. Download mode, USB-Serial/JTAG and pad JTAG are
  all available, so there is no way yet to brick this board that a reflash cannot
  undo. Recorded as a baseline: a future reading that differs from this one means
  something was burned, and this file says when it was not.

### The panel's native asset format, decoded from the vendor's own files

- **Claim:** the three `/image/*.bin` files in the Waveshare `storage` partition
  are each **exactly 411 652 bytes**, being a **12-byte header** followed by
  **410 × 502 RGB565, little-endian**, row-major, uncompressed, with no palette
  and no alpha. The header is `u32` magic `0x00001219` (constant across all
  three, meaning `UNKNOWN`), `u16` width `410`, `u16` height `502`, `u32` stride
  `820` = width × 2. `12 + width × height × 2` equals the file length exactly.
- **Source:** S11 — the `storage` partition of the received unit, extracted with
  `tools/flash/spiffs_extract.py`.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `LIKELY` RGB565 — an inference from "raw binary on a QSPI AMOLED".
- **How the byte order was settled, because it is the part an argument cannot
  settle.** Decoded little-endian the files are coherent artwork. Decoded
  big-endian they are noise. That is the whole test.
- **Impact:** T-034's target format is no longer a preference. The panel's native
  pixel format and byte order are facts about the hardware; the vendor's *header*
  is not, and is worth noticing rather than copying — it carries width, height
  and stride but no format field, which is the field needed the moment a second
  format exists. Also: three full frames cost 1.18 MB, which is what an
  uncompressed full-screen asset costs on this panel.

### The Waveshare `storage` partition holds six files, not three, and three are music

- **Claim:** alongside the three images there is a `/music/` directory holding
  `BGM_1.mp3` (207 713 B, MPEG-1 Layer III, 112 kbps, 44.1 kHz, **mono**),
  `BGM_2.mp3` (199 664 B, 112 kbps, stereo) and `BGM_3.mp3` (380 917 B, 128 kbps,
  stereo, with a 139 756-byte ID3v2.4 tag that is mostly embedded artwork).
- **Source:** S11, same extraction.
- **Was:** a parallel reading of the same partition recorded *"only three real
  files, all raw binaries in an `/image/` dir"*. That was `strings`-derived and
  incomplete.
- **Impact, and it is not about audio formats.** The board ships 788 kB of music
  and a `MusicPlayer` app to play it. Taken with the grille slot in the case wall
  and the separate motor pads at `P1`/`P2`, this makes the reading that
  `AAC210602A1` is a *haptic actuator* very hard to sustain — see T-105, which
  now has a strong prior. It is **not** `VERIFIED` by this alone: stereo source
  material decoded to one transducer is still mono output, and only tracing the
  pads settles it.

### The factory image carries somebody else's licensed music

- **Claim:** `BGM_1.mp3`'s ID3 frames read verbatim
  `All Rights Reserved to www.Art-list.io` and `Levitate by Ryefield`.
- **Source:** S11, same extraction.
- **Impact:** the factory flash image contains **commercially licensed
  third-party audio under an all-rights-reserved grant**, on top of Waveshare's
  own proprietary binary. Keeping the dump off the repository was until now a
  convention rather than a written rule — no prior change has committed a vendor
  binary and none had needed to say why — and review on
  [#80](https://github.com/hleserg/Attadipa/pull/80) was right that "the
  existing rule" cited a document that did not exist. **It is a rule as of this
  record**, and this is the second and sharper reason for it,
  because republishing the dump would redistribute somebody else's licensed
  audio. The extracted files and the rendered PNGs are **not committed** either.
  What is committed is the extractor and the measurements.

### The factory backup of the received unit is verified against the device

- **Claim:** the 33 554 432-byte image of this unit's flash hashes to
  `2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5`, and
  `esptool verify-flash 0x0` — which has the **device** compute the MD5 — returns
  `Verification successful` over the whole 32 MB.
- **Source:** S12 — three complete reads of the received unit's flash: the
  owner's on Windows 11 over native USB, and two here on Linux over USB/IP.
  All three agree byte for byte, per chunk. Method and chunk map in
  [WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §2.2.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Impact:** **the first flash of our own firmware is reversible.** That is the
  precondition every bench task on this unit was waiting for, and the reason the
  balance of risk between the two diagnostic routes has shifted.
- **Not committed, and this is the rule not a preference** — the image is
  Waveshare's proprietary binary plus third-party all-rights-reserved audio. It
  lives on the owner's machine.

### Two complete applications ship on this flash, both built with ESP-IDF v5.5.1

- **Claim:** `factory` at `0x100000` holds **`phone_s3_box_3`**
  `v0.4.2-92-g5c6be6c-dirty`, 5 175 184 B, built 4 Nov 2025; `ota_0` at
  `0xa00000` holds **`xiaozhi`** version **`1.8.5`**, 5 481 872 B, built
  31 Oct 2025. Both descriptors give `idf_ver` **`v5.5.1-dirty`**. `ota_1` is
  erased and `otadata` is blank, so `factory` is what runs.
- **Source:** S12 — the `esp_app_desc_t` in each image, parsed at its slot
  offset. Read out of the binaries, not inferred from the wake-word model as an
  earlier record did.
- **Impact:** T-104 must read xiaozhi at **tag `1.8.5`**; reading `HEAD` is
  research into a different program. And `v5.5.1` is the vendor's own answer to
  the IDF-version question T-004 asks — one version about which something is
  *known*, not a recommendation. Note `-dirty` on both: they built from modified
  trees, so it names a starting point, not a reproducible one.

### The stock firmware does not rewrite its own configuration partitions on boot

- **Claim:** `nvs`, `otadata` and `phy_init` (`0x9000`–`0x12000`) are byte-for-byte
  identical across three reads separated by hard resets and ~90 s of running,
  hashing to
  `803798ee52013c09e9dd55a72226d0195ec6a3582f85af3b43315f9247b3e26e`.
- **Source:** S12, plus a direct observation by the owner on 2026-08-22 — the
  device was cycled six times through download mode and back, and the panel
  blinked dark-then-launcher on every cycle. That is what makes the reads a test
  of a *running* firmware rather than of flash in download mode.
- **Impact:** modest but real — a bench procedure on this unit can reset it
  repeatedly without the stock firmware quietly changing the bytes underneath.
  It says nothing about what the firmware writes when a user touches it.
