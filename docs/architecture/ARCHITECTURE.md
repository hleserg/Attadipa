# Architecture

This document has one job: make sure **every part soldered to the board has an
owner in the core**, whether or not any application uses it yet.

It is a target, not a description. Nothing here is implemented. Where it
disagrees with [`../master-prompt.md`](../master-prompt.md), the disagreement is
argued from evidence in [`../research/`](../research/) and belongs in an ADR.

---

## 1. Why cover parts nobody uses

The obvious argument is "so we can use it later". That argument is weak, and if
it were the only one, deferring would be correct.

The real arguments are that **an unowned part is not neutral**:

**It costs power.** On the T-Watch, the LoRa radio sits on PMU rail ALDO4 and
GNSS on BLDO1. If nothing owns those rails, nothing turns them off. A radio
nobody uses is a radio nobody powers down — and battery life is a headline
requirement. The most expensive peripheral is the one no code is responsible
for.

**It generates interrupts.** The AXP2101 (GPIO21), PCF8563 (GPIO17) and BMA423
(GPIO14) all have interrupt lines on the T-Watch. An interrupt line nobody
services can hold a level, prevent a sleep transition, or wake the CPU in a
loop. "We're not using the RTC alarm" does not mean the RTC will stay quiet.

**It shares buses.** Five devices share the T-Watch main I2C bus. A driver that
appears only in a diagnostics screen still contends for that bus with the PMU.
Bus ownership has to be decided once, centrally, not by whoever writes the
second driver.

**It floats.** An unconfigured GPIO is in an undefined state. The T-Watch IR
transmitter on GPIO2 drives an NPN low-side switch, so the pin high means the
diode conducts (S3 sheet 4). If nobody drives it low, its state is whatever the
boot ROM left — on a part that can operate other people's equipment across a
room.

**Or it is a strapping pin.** Three of the four ESP32-S3 strapping pins on the
T-Watch carry live signals: GPIO3 is the LoRa clock, GPIO45 is the backlight and
also selects `VDD_SPI` voltage at reset, GPIO46 is I2S data. A driver that
asserts one of those early enough does not misbehave — the board stops booting.
That is not a peripheral concern; it is a board concern, and it needs an owner
above the driver layer.

**And the evidence that this is a real failure mode is already in the repo.**
The Waveshare vendor BSP declares `BSP_CAPS_IMU 0` while a QMI8658 sits on the
board, and it does not touch the AXP2101 or the PCF85063 either. That is a
shipped, maintained, first-party BSP with three unowned parts on the board it
supports. *Vendor-supported* and *handled* are different sets.

So: the core owns the board. Applications own a subset of what the core offers.
Those are different scopes, and conflating them is how peripherals get lost.

What full coverage does **not** mean: writing a complete feature for every
part. The IR transmitter needs an owner, an off state, and a diagnostics entry.
It does not need a remote-control application.

---

## 2. Layers

```
Applications            know capabilities, never hardware
Application framework   lifecycle, navigation, focus
Core services           time, location, mesh, power, audio, storage…
Hardware coordination   arbitration: buses, rails, RF, quiet windows
Platform / HAL          esp32s3 | native
Board support package   pins, rails, parts, capability descriptor
Hardware
```

`#ifdef BOARD_X` is allowed inside `boards/` and `platform/`. It must not appear
in `core/` or `apps/`. This is not stylistic: the two target boards share only
the SoC and the PMU, so a conditional that leaks upward multiplies through
every layer above it.

---

## 3. The capability model

The specification suggests `device.capabilities().has(Capability::GNSS)`. The
board survey shows that is not sufficient, because presence is not the only
question the code has:

- The T-Watch LoRa radio is **one of five chips** — SX1262, SX1280, CC1101,
  LR1121 or SI4432 — chosen at purchase. Sub-GHz and 2.4 GHz are not
  interchangeable; the region rules and the mesh interoperability differ.
- The T-Watch GNSS is **one of two modules**, with different power-up sequences
  and different assistance mechanisms.
- The T-Watch IMU is an accelerometer. The Waveshare IMU is six-axis. Both are
  "IMU present"; only one can report rotation.

So a capability answers three questions, not one:

**Is it there?** — a cheap boolean, for gating UI.

**Which one is it?** — a typed descriptor, for drivers.

**Can I use it right now?** — an availability state, for both.

```cpp
// Cheap, for deciding whether a feature exists at all on this board.
if (!caps.has(Capability::Gnss)) { /* the app is not offered */ }

// Typed, for code that must know the part.
if (auto lora = caps.lora()) {
    // lora->chip, lora->band, lora->max_tx_dbm
}

// Availability is a separate axis from presence.
enum class Availability {
    Absent,       // not on this board — the feature does not exist here
    Failed,       // on the board, but initialisation failed
    Off,          // deliberately powered down; can be brought up
    Ready,        // usable now
};
```

`Absent` and `Failed` must never render the same way. "This watch has no
compass" and "the compass is broken" are different sentences to a user, and
only one of them is worth a diagnostics screen.

The decision and its alternatives belong in ADR-0001.

### Capabilities

`Display · Touch · Buttons · Pmu · BatterySense · Rtc · Accelerometer ·
Gyroscope · Magnetometer · Lora · Gnss · Haptics · AudioOut · AudioIn ·
IrTransmit · SdCard · Wifi · Ble`

Note `Accelerometer` and `Gyroscope` are separate. Neither board has a
magnetometer, but `Magnetometer` exists in the enum — so the day one is added
externally, the answer changes from `Absent` to `Ready` and nothing above the
BSP needs rewriting. That is the whole point of the layer.

---

## 4. Ownership — every part on both boards

Every row has exactly one owning service. "Owns" means: initialises it, holds
its power state, services its interrupt, arbitrates access to it, and reports
it to diagnostics.

### LilyGO T-Watch S3 Plus

| Part | Owner | Notes |
|---|---|---|
| AXP2101 PMU | `PowerService` | root service — owns every rail below |
| Battery sense | `PowerService` | 940 mAh; charge current clamped ≤ vendor recommendation |
| ST7789V3 display | `DisplayService` | rail ALDO3; backlight rail ALDO2 |
| Display backlight | `DisplayService` | brightness = rail + PWM |
| FT6336U touch | `InputService` | **no reset line** — see §6 |
| BOOT button (GPIO0) | `InputService` | usable as a normal button |
| PWR button | `InputService` via `PowerService` | reported by the PMU, not a GPIO |
| PCF8563 RTC | `TimeService` | INT on GPIO17 — serviced even with no alarm set |
| BMA423 accelerometer | `MotionService` | INT on GPIO14; **no gyroscope** |
| DRV2605 haptic | `HapticService` | **enable is PMU rail BLDO2** — see §6 |
| LoRa radio (1 of 5) | `RadioService` → `MeshService` | rail ALDO4; chip is a variant |
| GNSS (1 of 2) | `LocationService` | rail BLDO1 (+DC4 for LS550G); **PPS not connected** |
| SPM1423 PDM mic | `AudioService` | input path; `SELECT` is strapped, channel fixed in hardware |
| MAX98357A amplifier | `AudioService` | I2S output. **`SD_MODE` is strapped — there is no shutdown pin.** Silence is a rail operation on `DLDO1` |
| IR transmitter (GPIO2) | `InfraredService` | **no application uses it** — see §5 |
| Radio `DIO3` (GPIO6) | `RadioService` | purpose unresolved (TCXO supply or second IRQ) — OPEN_QUESTIONS D10. Owned regardless, so it is not left floating next to a radio |
| Charge LED (`CHGLED`) | `PowerService` | a PMU register, not a GPIO. Owned so its blink pattern is a deliberate choice rather than a reset default |
| USB device (GPIO19/20) | `UsbService` | native USB. Console today; enumerating as anything else is a decision, not a default |
| RTC backup cell (MS412FE) | `PowerService` | charge path exists; whether to enable it is a policy with a standby-current cost |
| Battery slide switch | — | **mechanical, unobservable.** Recorded so nobody writes code that assumes the battery is always connected |
| Strapping pins (0, 3, 45, 46) | `BoardService` | reserved and documented; no driver may reconfigure them at will |
| Internal flash 16 MB | `StorageService` | partition layout is an ADR |
| PSRAM 8 MB | platform | LVGL buffers, mesh state — budgeted in RESOURCE_BUDGET |
| Wi-Fi / BLE | `ConnectivityService` | shares RF with LoRa — coordinator concern |

### Waveshare ESP32-S3-Touch-AMOLED-2.06

| Part | Owner | Notes |
|---|---|---|
| AXP2101 PMU | `PowerService` | same part as the T-Watch — shared driver |
| Battery sense | `PowerService` | capacity not yet established |
| CO5300 AMOLED | `DisplayService` | QSPI; brightness is a panel command, not a rail |
| FT3168 touch | `InputService` | **has** a reset line, unlike the T-Watch |
| QMI8658 IMU | `MotionService` | 6-axis — **vendor BSP does not drive this** |
| PCF85063 RTC | `TimeService` | different part from the T-Watch, same interface |
| ES8311 codec | `AudioService` | output path |
| ES7210 + 2 mics | `AudioService` | dual-microphone input |
| Amplifier enable (GPIO46) | `AudioService` | must be low when audio is idle |
| SD card | `StorageService` | SDMMC 1-bit |
| Wi-Fi / BLE | `ConnectivityService` | the only radio on this board |
| LoRa | — | **absent** |
| GNSS | — | **absent** |
| Haptics | — | **none found** — see OPEN_QUESTIONS D4 |

### Parts with an owner but no application

These exist, are initialised, are put in a defined low-power state, and appear
in diagnostics. Nothing else consumes them yet, and that is fine — what is not
fine is leaving them unowned.

| Part | Board | Defined state when unused |
|---|---|---|
| IR transmitter | T-Watch | pin driven **low** — the inactive level, confirmed from the schematic, not assumed from LED convention. Never transmits without an explicit user action |
| Radio `DIO3` | T-Watch | configured per D10's answer; until then, left in the state the radio driver's own init demands and not repurposed |
| Charge LED | T-Watch | set to an explicit mode at boot, off by default |
| USB device | both | console only; no storage or HID class exposed without a decision |
| PDM microphone | T-Watch | clock stopped, not sampling |
| ES7210 dual mics | Waveshare | codec in standby |
| Gyroscope (QMI8658) | Waveshare | disabled at the sensor, not just ignored |
| SD card | Waveshare | unmounted, card detect observed |
| Wi-Fi | both | radio off, not merely disconnected |

The IR transmitter deserves its own sentence. It is an infrared diode that can
control other people's equipment in the room. Its owner exists to guarantee it
is *silent* by default, not to make it useful.

---

## 5. Absence is a first-class state

On the Waveshare board, two of the product's headline features — mesh messaging
and navigation — have no hardware. This is not a degraded mode to paper over.

The rules:

- An application whose capability is `Absent` is **not offered**. No greyed-out
  icon that promises something the board can never do.
- A service whose capability is `Absent` still exists and still answers. It
  returns `NOT_SUPPORTED`, not `false`, and never crashes a caller.
- The UI says which of the four states it is in, in human language.
  "This watch has no radio" is a complete, honest answer.
- The simulator can present a board with no radio and no GNSS, because that is
  a real configuration and it must be testable.

Error vocabulary — services return these, never a bare boolean:
`NOT_SUPPORTED · BUSY · TIMEOUT · NO_FIX · RADIO_UNAVAILABLE ·
POWER_RESTRICTED · PERMISSION_DENIED · INTERNAL_ERROR`.

---

## 6. Power rails are a service, not a driver detail

On the T-Watch, six peripherals are gated behind AXP2101 rails. Rail control
therefore cannot live in each driver — two drivers sharing a rail, or a driver
that powers down a rail its neighbour is using, is a class of bug that only
appears intermittently.

`PowerService` owns every rail. Drivers *request* power and release it:

| Rail | Feeds | Owner |
|---|---|---|
| ALDO2 | display backlight | `DisplayService` |
| ALDO3 | display **and touch** | shared — `DisplayService` + `InputService` |
| ALDO4 | LoRa | `RadioService` |
| BLDO1 | GNSS | `LocationService` |
| BLDO2 | DRV2605 enable | `HapticService` |
| DC4 | GNSS, LS550G variant only, 850 mV | `LocationService` |
| VBACKUP | RTC coin cell | `TimeService` |

Two consequences that are easy to miss:

**ALDO3 feeds display and touch together.** Neither service can power it down
unilaterally. This is a genuine shared resource and needs reference counting,
not a boolean.

**The haptic driver has a power-up latency.** Because the DRV2605 is enabled by
a rail, a buzz is not instantaneous — the rail must come up and the driver must
be ready. Whatever schedules haptics has to account for that, and the
application must not.

---

## 7. The two touch controllers do not share a sleep strategy

The T-Watch FT6336U has **no reset line**, and the vendor states plainly that
if the touch panel is put to sleep it will not work again. The Waveshare FT3168
does have a reset line.

This is not a driver difference — it changes the power state machine. On one
board, "sleep the touch controller to save power" is a valid transition; on the
other, it is a one-way trip to a watch that ignores fingers until it is reset.

The vendor's own figures make the trade-off concrete: deep sleep waking on the
touch panel costs about 1.08 mA against roughly 460–530 µA waking on a button —
a factor of two, for the convenience of tap-to-wake. That is a product decision
informed by measurement, and it must be made per board.

---

## 8. Coordination — build the measurement before the mitigation

A `HardwareCoordinator` arbitrates buses, rails, RF time and quiet windows, so
that applications never schedule around each other's hardware.

But the specification's motivating example — the vibration motor disturbing the
compass — **cannot occur on either board, because neither board has a
compass.** Building a mitigation for it now would be architecture in response to
a document rather than to hardware.

So the order is: build the diagnostic tooling that can *measure* baseline vs
A vs A+B, and let the measurements decide what needs arbitrating. The
coordinator's first real job on these boards is more mundane and more certain:
shared-bus ownership, rail reference counting, and keeping Wi-Fi, BLE and LoRa
from talking over each other on the T-Watch.

What is real and needs no measurement to justify: ALDO3 shared between display
and touch, the main I2C bus shared by five devices, and three unserviced
interrupt lines.

---

## 9. Concurrency

Applications and UI code do not create FreeRTOS tasks. Services own their tasks;
applications receive events. An application that needs work done off the UI
thread asks a service for it.

This is what keeps stack usage countable, and stack usage is part of the memory
budget on a device where internal RAM is the scarce resource.

---

## 10. What must be decided before implementation

| # | Decision | Where |
|---|---|---|
| 1 | Capability model — variant and degree | ADR-0001 (TASKS T-002) |
| 2 | Rail ownership and reference counting | ADR |
| 3 | Radio abstraction across five chips — and whether MeshCore permits it | ADR, blocked on OPEN_QUESTIONS M6, M9 |
| 4 | Partition layout and OTA scheme | ADR |
| 5 | ESP-IDF and LVGL versions | DEPENDENCIES (TASKS T-004) |
| 6 | Whether to depend on the vendor BSPs or take only their pin facts | REUSE_LEDGER (OPEN_QUESTIONS T6) |

Decision 3 is the one that can force a redesign. If MeshCore assumes exclusive,
uninterrupted ownership of the radio, it cannot coexist with a coordinator that
wants to schedule around it — and that is worth knowing before anything is
built on top.
