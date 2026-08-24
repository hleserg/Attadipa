# Architecture

This document has one job: make sure **everything the device can do has an owner
in the core** — every part soldered to the board, whether or not any application
uses it yet, and every capability that reaches the device some other way.

It began as the first of those. The second arrived with the Attadipa node, and
the widening matters: a capability that arrives over a link still costs power,
still needs an owner, and still has to be somewhere when it goes away.

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

**Or driving it is the bug.** The rule above has at least one exception, and it
has to be named rather than discovered. The radio's `DIO3` reaches GPIO 6, and on
SX126x parts `DIO3` is frequently configured as the TCXO supply — a radio output.
If that is the case here, GPIO 6 must stay an input and a well-meaning
"initialise every pin to a defined level" pass would fight the radio for control
of its own clock. Resolve D10 before writing the radio driver; until then GPIO 6
is owned and left alone, which is not the same as unowned.

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

So: the core owns the board, **and everything else the device can currently do**.
Applications own a subset of what the core offers. Those are different scopes,
and conflating them is how peripherals get lost — and, now, how a node-provided
capability ends up owned by whichever application happened to need it first.

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
Board support package   pins, rails, parts, capability descriptors
Hardware
```

The BSP is not the only thing that declares a capability. A **provider
registry** sits beside it at the platform layer and accepts descriptors from
things that are not the board — today, an Attadipa node; tomorrow, whatever else
attaches. Registration is at runtime and is reversible, which the BSP's is not.

```
Board support package   ─┐
                         ├─►  Capability registry  ─►  everything above
Provider registry       ─┘    (the single answer to "what can this device do")
```

Without that second box an implementer has nowhere to put node code and will put
it in the BSP, which is the layer defined as knowing the board.

`#ifdef BOARD_X` is allowed inside `boards/` and `platform/`. It must not appear
in `core/` or `apps/`. This is not stylistic: the two target boards share only
the SoC and the PMU, so a conditional that leaks upward multiplies through
every layer above it.

---

## 3. The capability model — two layers

Applications ask what the device can **do**. They never ask what is on it.
Those are different questions with different answers, and the review that
arrived with the final specification found this document asking the second one
and calling it the first.

The application-facing set used to be this:

```
Display · Touch · Buttons · Pmu · BatterySense · Rtc · Accelerometer ·
Gyroscope · Magnetometer · Lora · Gnss · Haptics · AudioOut · AudioIn ·
IrTransmit · SdCard · Wifi · Ble
```

Every entry is a part. `Pmu`. `Rtc`. Nothing named `Position`. An application
asking `Gnss` was asking about a chip — and on a Waveshare board with an Attadipa
node attached, that chip is absent while a position is on screen. So there are
two layers, and only one of them is visible to an application.
[ADR-0007](../adr/0007-two-capability-layers.md) carries the decision and its
alternatives.

### 3.1 Hardware inventory — below the service boundary only

```cpp
enum class HardwareFeature : uint8_t {
    Display, Touch, Buttons,
    Pmu, BatterySense, Rtc,
    Accelerometer, Gyroscope, MagnetometerSensor,
    Radio, GnssReceiver,
    HapticActuator, AudioOutDevice, AudioInDevice,
    IrTransmitter, SdCard,
    Wifi, Ble, Usb,
};

bool             present(HardwareFeature) const;   // is the part here
const RadioInfo* radio() const;                    // which part, typed
HardwareState    state(HardwareFeature) const;     // is the driver up

enum class HardwareState : uint8_t {
    Absent,        // present() == false. There is no driver to have a state.
    Untouched,     // owned, deliberately not brought up. Not an error.
    RailOff,       // the supply that feeds it is down. Can be brought up.
    Initialising,
    Failed,        // bring-up was attempted and did not succeed
    Ready,
};
```

`Untouched` is the value that had to be invented. `Absent` and `Failed` already
had names; *we own this part and chose to leave it alone* did not, and §4 below
plus final §32 both insist that this is a legitimate owned state rather than a
gap. Without it, a rail deliberately left off reads as a failure on the
diagnostics screen, and the first person to see that screen will "fix" it.

It also has a mapping consequence. `Untouched`, `RailOff` and `Initialising` all
become `Availability::Off` — one sentence, *it is not running and it can be
started*. `Initialising` is the imprecise one: the honest sentence there is
"wait", and `Availability` has no state for it because waiting is not a remedy.
A service that is coming up reports progress through its own loading state.

This layer knows chips, pins, rails, buses, addresses, IRQ topology and
modulation families. It is contributed by the BSP for the local board and by a
provider for a node's own inventory. It is a fact about a **board**, it does not
change while running, and no node ever makes `present()` true.

The board survey is why it exists, and why a boolean is not enough on its own:

- The T-Watch radio is **one of five chips** — SX1262, SX1280, CC1101, LR1121,
  SI4432 — chosen at purchase. Two of the five have no LoRa modulator at all
  ([ADR-0003](../adr/0003-radio-not-lora.md)), so "a radio is present" and "this
  device can join the mesh" are different sentences.
- The T-Watch GNSS is **one of two modules**, with different power-up sequences
  and different assistance mechanisms.
- The T-Watch IMU is an accelerometer. The Waveshare IMU is six-axis. Both are
  "IMU present"; only one can report rotation.
- A part can be present and broken. The Waveshare vendor BSP does not initialise
  the QMI8658, AXP2101 or PCF85063 that are on the board.

`Lora` is not in the list. The part is a `Radio`; whether it can do LoRa is a
fact *about* it, not the name of the slot. `MagnetometerSensor` and
`AudioOutDevice` carry the suffix so no hardware name can be mistaken for the
product capability of similar name — the compiler catches the confusion this
section is about.

### 3.2 Product capabilities — what applications see

```cpp
enum class Capability : uint8_t {
    Time, Position, Heading, MotionSensing,
    MeshMessaging,
    Haptics, AudioPlayback, AudioCapture,
    NotificationRelay, InfraredBlast,
    PersistentStorage, RemovableStorage,
    CompanionLink,
};
```

Thirteen, argued in ADR-0007 §2 rather than copied. `Navigation` is deliberately
not among them — it is an application built on `Position` and `Heading`, and a
capability that gated the Navigator on its own existence would be circular.
`Position` and `Heading` are deliberately separate: a watch standing still with
a good fix has one valid and the other not.

### 3.3 The three questions, and why `has()` is gone

```cpp
bool         supports(Capability) const;      // could this device, ever?
bool         is_available(Capability) const;  // right now?
Availability availability(Capability) const;  // and what do we tell the user?
```

`has()` does not exist — not renamed, not deprecated, absent, so that no call
site survives the change by accident. It had become unanswerable: on a Waveshare
board `has(Gnss)` is false by hardware and true by product, and whichever answer
it gave was wrong for half its callers.

The replacement gives the behaviour that was actually wanted:
`supports(Position) == true` puts the Navigator in the launcher on every device,
and `availability(Position) == Unprovisioned` is what the Navigator says when it
opens on a Waveshare board with no node attached.

```cpp
enum class Availability : uint8_t {
    Unsupported,    // no configuration of this device can provide it
    Unprovisioned,  // a supported provider would give it; none is bound
    Unreachable,    // a provider is bound, but is not reachable now
    Incompatible,   // reachable, but no protocol version can be agreed
    Failed,         // bound and reachable; it did not come up
    Off,            // deliberately powered down; can be brought up
    Ready,          // usable now
};

// Where it comes from is an orthogonal axis, read only below the app layer.
enum class Origin : uint8_t { Local, Node };
```

No two of these may render the same way. "This watch has no compass", "Maps
needs an Attadipa node", "your node is out of range" and "the compass is broken"
are four different sentences, and each has a different thing the user can do
about it. That is the rule that sets the state count: **one state per remedy;
different wording for the same remedy is a reason code, not a state.**

`Ready` means the source can be asked. It does not mean it has an answer — a
GNSS that is powered and healthy and has no fix is `Ready`. Availability and
validity are different questions, and a datum that crossed a link has **two**
ages: how old it was at the source, and how long ago it reached us. The UI shows
the larger.

`Unsupported` is terminal. The full transition table is
[ADR-0004](../adr/0004-capability-sources.md) §2a, and it is centrally owned:
no component mutates availability on its own.

**The state and the origin are one answer, not two.** `source()` returns both —
`CapabilitySource { Availability, ProviderRef }` — and `availability()` and
`provider()` read it rather than deciding anything. That is a correctness
property and not tidiness: while the two were derived separately, `provider()`
defaulted to `Origin::Local` and only reached `Node` for a node that was already
`Ready`, so a bound node going out of range reported itself as the watch's own
hardware ([#174](https://github.com/hleserg/Attadipa/issues/174)) — breaking
ADR-0004 §2's invariant in exactly the degraded states the axis exists for. The
origin is meaningful only when the state is not `Unsupported`: nothing provides
an unsupported capability, and `Origin` has no value that says so.

### 3.4 The mapping is many-to-many and nobody above the service sees it

| Capability | Providers, in preference order |
|---|---|
| `Time` | GNSS · companion · RTC · user |
| `Position` | local `GnssReceiver` · node |
| `Heading` | `MagnetometerSensor` (neither board has one) · GNSS course-over-ground · node |
| `MeshMessaging` | local `Radio`, *only if* the fitted chip and the pinned MeshCore support it · node |
| `NotificationRelay` | companion only |

A capability is `Unsupported` only when no row can ever be satisfied.
Applications never see this table and never name a provider. Diagnostics and
Settings do, because inspecting and configuring providers is what they are for.

### 3.5 The boundary is a link boundary

`HardwareFeature`, `present()`, `state()` and the typed descriptors live in a
library that `apps/` does not link against. An application that tries to ask
about a chip fails to build. `#ifdef BOARD_X` was already forbidden in `core/`
and `apps/` and was already unenforced; this makes the stronger version of the
same rule mechanical, and lets a reviewer answer *does this application touch
hardware* by reading the link line instead of the diff.

As of the first commit of code, this is the target graph rather than a plan:

```
attadipa_platform   chips, pins, rails, buses, typed descriptors
        ▲ PRIVATE
attadipa_core       services, and the capability registry that owns the mapping
        ▲ PUBLIC
attadipa_apps       applications
```

The enforcement is one keyword. `core/CMakeLists.txt` links `attadipa_platform`
**PRIVATE**, so `core/` compiles against the inventory and nothing that links
`core/` inherits the ability to. `attadipa_core` is the only place in `core/`
that includes a platform header, and it does so in a `.cpp`; the registry's own
header forward-declares `HardwareInventory` so that an application can hold a
`CapabilityRegistry&` without gaining a way to ask it about chips.

Two tests compile the same fixture twice — linked against `attadipa_platform` it
must build, linked against `attadipa_apps` it must not. Two rather than one,
because a single failing compile cannot tell an enforced boundary from a
mistyped `#include` path. The failure is exactly the one intended:

```
tests/boundary/app_reaches_for_hardware.cpp:9:10: fatal error:
    attadipa/platform/hardware_feature.h: No such file or directory
```

The simulator's `main()` links both, and so will the device's. A composition
root is allowed to see everything it assembles; that is what makes it the
composition root and not a layer.

### 3.6 Capabilities are not data feeds

A node may supply weather, Home Assistant events, quest events and telemetry.
Those are **feeds**, not capabilities. A capability has an availability state and
gates whether an application is offered. A feed has a source and an age, and
nothing else. A capability called `Weather` would produce a screen that cannot
tell "no weather source" from "the weather is four hours old". The test: *can an
application be written that is useless without it?*

---

## 4. Ownership — every part on both boards

Every row has exactly one owning service.

**"Owns" does not mean "initialises".** This document used to define it that
way, and final §32 names that definition as too strong — correctly, and this
board gives the example:

> Radio `DIO3` (GPIO 6) — on SX126x parts `DIO3` is commonly configured as the
> TCXO supply, in which case GPIO 6 is driven by the *radio* and must never be
> driven by the SoC.

Under "ownership means initialisation", `RadioService` owns that pin and so
should configure it, and configuring it is how the oscillator gets shorted.
An ownership checklist that produces that outcome is worse than no checklist.

So: **owning a part means being the one component responsible for its
lifecycle, its safe default, its power state, access arbitration to it,
diagnostics for it, and policy about it.** Several perfectly valid owned states
involve touching nothing:

| Owned state | When it is right |
|---|---|
| intentionally untouched until first use | a part whose power-up costs more than idle leakage |
| left as an input / high-Z | another chip drives the line — GPIO 6 above; the strapping pins |
| rail off, driver not instantiated | the GNSS module until a navigation app opens |
| register read, never written | a strapped pin whose state is a fact to report, not a setting |

What is *not* allowed is a part with **no** owner. An unowned part still draws
power, still raises interrupts nobody services, still contends for the shared
bus, and still leaves its pin floating. The Waveshare vendor BSP, which ships
with three unowned parts on the board it supports, is the evidence — and is why
§1 of this document exists.

The tables below therefore say who is responsible, not who calls `init()`.

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
| Radio (1 of 5 chips) | `RadioService` → `MeshService` | rail ALDO4; chip is a purchase-time variant. **Not necessarily LoRa** — two of the five candidates are FSK-only, and only one is supported by the pinned MeshCore ([ADR-0003](../adr/0003-radio-not-lora.md)) |
| GNSS (1 of 2) | `LocationService` | rail BLDO1 (+DC4 for LS550G); **PPS not connected** |
| SPM1423 PDM mic | `AudioService` | input path; `SELECT` is strapped, channel fixed in hardware |
| MAX98357A amplifier | `AudioService` | I2S output. **`SD_MODE` is strapped — there is no shutdown pin.** Silence is a rail operation on `DLDO1` |
| IR transmitter (GPIO2) | `InfraredService` | **no application uses it** — see §5 |
| Radio `DIO3` (GPIO6) | `RadioService` | **may be a radio *output*.** On SX126x parts `DIO3` is commonly configured as the TCXO supply, in which case GPIO 6 must never be driven by the SoC. Unresolved — OPEN_QUESTIONS D10 |
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
| Buttons | `InputService` | at least two tactile keys exist; **the vendor BSP declares none** |
| Expansion header J3 | `BoardService` | ≥ 29 pins; pinout unresolved (D3). Owned so nothing else claims those pins by accident |
| 1.8 V rail (ALDO4) | `PowerService` | something on this board is 1.8 V; identify it before assuming any level |
| SD card | `StorageService` | SDMMC 1-bit |
| Wi-Fi / BLE | `ConnectivityService` | the only radio *on this board* — and, once a node is attached, the path by which a second one's traffic arrives |
| LoRa | `MeshService` via the provider registry | **not on this board.** With an Attadipa node attached the device has it; the service exists either way and reports which |
| GNSS | `LocationService` via the provider registry | **not on this board.** An Attadipa node supplies it; the service exists either way and reports which |
| Vibration motor | `HapticService` | **GPIO 18 + NPN, no driver IC.** Rail BLDO2. Same capability as the T-Watch, a fundamentally different degree — see below |

### Two haptics, one capability, two degrees

This is the clearest live example of why the hardware layer keeps a typed
descriptor rather than a boolean, and it was found by reading the schematic
after the capability model was already written. It is also a small argument for
the two-layer split: one product capability, two very different parts, and the
application should not have to know which:

| | T-Watch S3 Plus | Waveshare AMOLED 2.06 |
|---|---|---|
| Part | DRV2605L haptic driver | none — a motor and a transistor |
| Control | I2C, waveform library, closed-loop | one GPIO |
| Expressible | named effects, ramps, sequences | on, off, and whatever PWM produces |
| Latency | rail power-up before the driver responds | immediate |

`is_available(Capability::Haptics)` is **true on both**, and correctly so — the
product capability is real on both devices. A UI that stopped there and asked
for waveform 47 would compile, pass on the simulator, and do nothing on one of
the two shipping targets. The capability is not where the difference lives; the
hardware descriptor is, and it sits below the service boundary where
`HapticService` can read it and applications cannot:

```cpp
enum class HapticKind { None, DirectDrive, WaveformDriver };
struct HapticInfo { HapticKind kind; bool waveform_library; uint16_t warmup_ms; };
```

Note what this corrects. The matrix previously recorded Waveshare haptics as
"none found", on the strength of their absence from the vendor BSP — the same
weak argument-from-absence the magnetometer claim used to rest on. The schematic
says otherwise. Two rows in this document were wrong for the same reason, and
both were found the same way.

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

On the Waveshare board those two features have no *local* hardware — and since
2026-08-21 that is a different statement from having no hardware at all, because
an Attadipa node provides both. Absence is still first-class; it is no longer
permanent.

The rules:

- An application whose capability is `Unsupported` is **not offered**. No
  greyed-out icon that promises something the device can never do.
- An application whose capability is merely `Unprovisioned`, `Unreachable`,
  `Incompatible`, `Off` or `Failed` **is** offered, with the remedy stated. An
  application that vanishes from the launcher when the node walks out of range
  teaches the user that the device is unreliable, rather than that the node is
  away. The dividing line is not "on the board or not" — it is **whether the
  user has an action available**.
- A service whose capability is unavailable still exists and still answers. It
  returns an error, never a bare `false`, and never crashes a caller. **The
  error has to distinguish permanent from transient**: `NOT_SUPPORTED` is a
  final answer and a caller is entitled to stop asking, which is exactly wrong
  for a node that is briefly out of range. That is `PROVIDER_UNREACHABLE`, and
  it means *try again*.
- The UI says which of the seven states it is in, in human language.
  "This watch has no radio" is a complete, honest answer; so is "your node is out
  of range, last seen four minutes ago".
- Values are three-valued where they can be unknown. *Known* · *known to be
  none* · *not known* are three facts, and rendering the third as the second —
  "0 nodes nearby" when we have no idea — is a lie the interface tells
  confidently.
- The simulator can present a device with no radio and no GNSS, one with a node
  attached, and one that **loses the node while an application is open** —
  because all three are real configurations, the third is the one that exercises
  the hard contract, and none of them can be tested on hardware that does not
  exist.

Error vocabulary — services return these, never a bare boolean:
`NOT_SUPPORTED · PROVIDER_UNREACHABLE · PROVIDER_INCOMPATIBLE · BUSY · TIMEOUT ·
NO_FIX · STALE · RADIO_UNAVAILABLE · POWER_RESTRICTED · PERMISSION_DENIED ·
INTERNAL_ERROR`.

Three of those are new and each fills a hole the node opened. `NOT_SUPPORTED` is
permanent — a caller may stop asking. `PROVIDER_UNREACHABLE` is transient and
means *try again*; reporting an out-of-range node as `NOT_SUPPORTED` would show
the user the wrong sentence and stop the retry that would have fixed it.
`PROVIDER_INCOMPATIBLE` carries the version skew, which has opposite remedies
depending on its direction. `STALE` is for a value that arrived but is too old
to act on — distinct from `NO_FIX`, which means none ever arrived.

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

A fourth item joins that list and applies to **both** boards: a standing link to
a node is a continuous radio and power cost, not an occasional one. On the
Waveshare board — the one this document keeps describing as having a single
radio — the link is the mechanism by which a second radio's traffic enters the
power budget. It is the coordinator's business from the day it exists.

What is real and needs no measurement to justify: ALDO3 shared between display
and touch, the main I2C bus shared by five devices, and three unserviced
interrupt lines.

---

## 9. Concurrency, and applications that outlive their hardware

Applications and UI code do not create FreeRTOS tasks. Services own their tasks;
applications receive events. An application that needs work done off the UI
thread asks a service for it.

§33 gives applications create · open · pause · resume · close · event. None of
those is *the GNSS you were navigating with has just left the building* — and
with a detachable node that is an ordinary Tuesday. It arrives through `event`
rather than as a seventh verb, so that applications which do not care do not
implement a handler for it:

```cpp
struct CapabilityChanged {
    Capability   capability;
    Availability from;
    Availability to;
    ProviderRef  provider;
};
```

An application declares what it needs and the framework enforces it, because an
application cannot be trusted to enforce it on itself:

```cpp
struct AppManifest {
    std::span<const Capability> required;     // cannot run without these
    std::span<const Capability> enhanced_by;  // better with, fine without
};
```

A `required` capability leaving while the application is open drives it to a
framework-owned screen that states the remedy. It does not crash, and it does not
keep drawing a position that is no longer arriving. This contract is being
written while there are zero applications, which is the only time it is cheap.

This is what keeps stack usage countable, and stack usage is part of the memory
budget on a device where internal RAM is the scarce resource.

---

## 10. What must be decided before implementation

| # | Decision | Where | State |
|---|---|---|---|
| 1 | Capability model — presence, variant, degree | [ADR-0001](../adr/0001-capability-model.md) | superseded by 7 |
| 2 | Rail ownership and reference counting | ADR (TASKS T-035) | open |
| 3 | Radio abstraction, and what MeshCore actually supports | [ADR-0003](../adr/0003-radio-not-lora.md) (TASKS T-013) | **accepted** |
| 4 | Partition layout and OTA scheme | ADR (TASKS T-025) | open |
| 5 | ESP-IDF and LVGL versions | DEPENDENCIES (TASKS T-004, T-032) | **LVGL pinned** at v9.5.0 and building; ESP-IDF still open, and it blocks the device build rather than M1 |
| 6 | Whether to depend on the vendor BSPs or take only their pin facts | REUSE_LEDGER (OPEN_QUESTIONS T6) | open |
| 7 | Capability sources and their runtime lifecycle | [ADR-0004](../adr/0004-capability-sources.md) (TASKS T-015) | **accepted** |
| 8 | The watch↔node protocol | [ADR-0005](../adr/0005-node-protocol.md) (TASKS T-016) | **provisional** — encoding pending benchmark |
| 9 | Settings, and values bounded by a regulatory profile | [ADR-0006](../adr/0006-settings-and-bounded-values.md) (TASKS T-017) | **accepted** |
| 10 | The event bus and the concurrency model | ADR (TASKS T-024) | open |
| 11 | Two capability layers, and the end of `has()` | [ADR-0007](../adr/0007-two-capability-layers.md) | **accepted** |
| 12 | `MeshService` with a local and a node provider | [ADR-0008](../adr/0008-mesh-service-providers.md) (TASKS T-013) | **accepted** |
| 13 | Heading: quantities, sources and reference frames | [ADR-0009](../adr/0009-heading.md) (TASKS T-026) | **accepted** |
| 14 | English and Russian from the first screen | [ADR-0010](../adr/0010-localization.md) (TASKS T-033) | **accepted** |

Decision 3 was the one expected to force a redesign, and it did — just not in
the direction anyone was watching. The question was whether MeshCore assumes
exclusive ownership of the radio. It does, and that turns out to matter less
than what the same reading found: at the pinned revision, MeshCore supports
**none** of the five T-Watch radio chips except the SX1262, and two of those
chips cannot do LoRa at all. See [ADR-0003](../adr/0003-radio-not-lora.md).

Decision 5 was the one on the critical path, and half of it has moved. LVGL is
pinned at v9.5.0, the simulator builds against it and renders at both
geometries, so the font subset, the image pipeline and the design tokens are no
longer waiting on a version. The ESP-IDF half is still open and now blocks only
the device build — which is where it belongs, because M1 is the simulator.

What replaced it on the critical path is the font toolchain: `lv_font_conv` is
a separate npm tool, unpinned, licence unchecked, and it is the only supported
way to produce a Cyrillic subset. Nothing about that was made easier by pinning
the library (T-032).
