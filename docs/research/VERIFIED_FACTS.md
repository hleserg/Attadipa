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
- **Impact:** mesh messaging and navigation — two of the product's headline
  features — cannot exist on this board. Both must degrade honestly rather
  than fail, and the UI must not offer them.

### Neither board has a magnetometer

- **Claim:** the T-Watch carries a BMA423 (accelerometer only, no gyroscope);
  the Waveshare carries a QMI8658 (6-axis accel + gyro). Neither board has a
  magnetometer.
- **Source:** S1, S5, S6.
- **Impact:** the specification's magnetometer requirements are **architectural
  only** for now — an API that can accept one later. On real hardware today,
  heading can come only from GNSS course, and only on the T-Watch. The
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
