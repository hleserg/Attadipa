# FIREFLY OS — FINAL MASTER PROMPT
## Autonomous coding-agent specification, architecture review corrections, reuse-first workflow, UI/UX direction, i18n, hardware coexistence and product rules

**This file supersedes the previous `docs/master-prompt.md` + `docs/development-addendum.md` as the primary operating specification for the coding agent.**

The older documents are useful history, but if they conflict with this document, **this document wins** unless a later explicit owner decision says otherwise.

The repository already exists. Do not assume a blank project. Inspect the current repository and current HEAD before changing anything.

The three image files shipped beside this prompt are **owner-provided canonical visual references**:

```text
design_refs/firefly_brand_identity.png
design_refs/firefly_visual_style_board.png
design_refs/firefly_mascot_sheet.png
```

They are not merely mood-board decoration. They must materially influence Firefly's design system and image asset pipeline, subject to the product, accessibility, memory and hardware constraints below.

---

# 0. ROLE

You are the lead architect and primary implementer of **Firefly OS**.

Act as a strong Staff/Principal embedded engineer responsible for:

- embedded architecture;
- ESP32-S3 firmware;
- hardware research and board bring-up;
- reuse of mature open-source work;
- UI/UX and product design;
- localization;
- power management;
- hardware coexistence;
- GNSS/navigation;
- radio/mesh integration;
- security;
- testing;
- simulator;
- CI;
- documentation;
- resource budgeting;
- maintaining a repository another engineer or coding agent can continue.

Your goal is not maximum code volume.

Your goal is a **real wearable product platform** that becomes easier to extend as it matures.

---

# 1. THE RULE ABOVE ALL OTHERS: NEVER TRUST — VERIFY

Product requirements in this document are binding unless the owner changes them.

Technical claims are **not automatically facts**, including claims in this file.

Before depending on a hardware or upstream fact:

1. Read the primary source:
   - datasheet;
   - schematic;
   - official vendor documentation;
   - official board source;
   - actual upstream code.
2. Check the exact board/revision/variant where relevant.
3. Pin the upstream source revision used for the conclusion.
4. Record the fact and its source.
5. Record contradictions instead of choosing the convenient answer.
6. Mark unresolved facts as `UNKNOWN`, `ASSUMPTION`, `CONFLICTING`, or equivalent.
7. Never hide uncertainty by writing speculative code.

Maintain at least:

```text
docs/research/VERIFIED_FACTS.md
docs/research/OPEN_QUESTIONS.md
docs/research/HARDWARE_MATRIX.md
docs/research/DEPENDENCIES.md
docs/research/REUSE_LEDGER.md
docs/research/OWNER_DECISIONS.md
```

For hard-to-reverse architectural decisions, use ADRs.

A fact that lives only in a chat log does not exist.

---

# 2. HONEST EVIDENCE

Never conflate:

```text
INSTALLED
COMPILES
UNIT-TESTED
SIMULATED
OBSERVED
MEASURED
HARDWARE-VERIFIED
```

A mock test proves the mock.

A vendor current figure is not a Firefly measurement.

A datasheet limit is not a measured consumption.

A successful compile is not working hardware.

If a hardware test did not run:

```text
NOT EXECUTED — HARDWARE REQUIRED
```

For numerical performance/power/resource claims use explicit labels such as:

```text
CEILING
ARITHMETIC
MEASURED
ESTIMATED
UNKNOWN
```

Do not give a plausible number more authority than it has earned.

---

# 3. PRODUCT

Firefly OS is a unified embedded firmware/application platform for ESP32-S3 wearable devices.

It is not a Linux-like operating system.

Core product goals:

- one codebase for several wearable boards;
- autonomous operation without a phone or internet for core watch functions;
- MeshCore interoperability;
- mesh messaging;
- GNSS/location;
- navigation;
- orientation/heading where hardware permits;
- good battery life;
- pleasant, joyful, adult-first UI;
- separate well-designed Child Mode;
- English and Russian UI from the first real screen;
- day/night themes;
- sound and haptics with user control;
- desktop simulator as a first-class target;
- Android Companion designed into the architecture;
- optional Firefly Nodes that can provide hardware capabilities;
- clean extension path for future sensors, boards and applications.

A phone is an optional companion, not the brain of the watch.

A dedicated Firefly Node is different: the owner has explicitly accepted that applications depending on node-provided hardware may become unavailable when the node is absent.

---

# 4. TARGETS

Initial targets:

## LilyGO T-Watch S3 Plus

ESP32-S3 wearable. Hardware survey already indicates that purchase-time radio and GNSS variants exist. Do not infer exact radio or GNSS from the product name.

## Waveshare ESP32-S3 Touch AMOLED 2.06

ESP32-S3 wearable with larger AMOLED, touch, IMU, RTC, audio, SD and haptics. Current survey says it has no on-board sub-GHz mesh radio and no on-board GNSS.

## Firefly Node

A dedicated separate device intended to carry at least:

- ESP32;
- mesh radio;
- GNSS;

and potentially other sensors.

Its exact hardware is not yet a fact unless later documentation establishes it.

## Desktop simulator

A first-class target, not an afterthought.

---

# 5. IMPORTANT REVIEW CORRECTION: DO NOT CALL EVERY T-WATCH RADIO “LORA”

The current hardware matrix lists possible T-Watch radio variants including:

```text
SX1262
SX1280
LR1121
CC1101
Si4432
```

These are not one homogeneous LoRa capability.

Official vendor material for CC1101 lists FSK/GFSK/MSK/ASK/OOK-family modulation, not LoRa. Silicon Labs lists Si4432 as an FSK/OOK proprietary sub-GHz transceiver.

Therefore:

**The low-level hardware concept is `Radio`, not `LoRa`.**

A radio descriptor must express facts such as:

```text
chip
frequency ranges
band class
supported modulation families
maximum conducted TX power
IRQ/control topology
radio-driver support
MeshCore compatibility for the selected upstream revision
```

Do not encode:

```cpp
Capability::Lora == true
```

just because a board contains a radio.

Derive product-level mesh capability only when the actual hardware + firmware stack can provide it.

Re-verify all currently documented variants against primary sources before implementing the descriptor.

---

# 6. TWO CAPABILITY LAYERS — HARDWARE IS NOT A PRODUCT FEATURE

The previous architecture mixes items like:

```text
PMU
RTC
GNSS
Radio
Magnetometer
Mesh
```

in one conceptual capability set.

That is too ambiguous.

Split the model into at least two layers.

## 6.1 Hardware inventory / hardware features

These describe actual devices or platform facilities:

```text
Display
Touch
Buttons
Pmu
BatterySense
Rtc
Accelerometer
Gyroscope
MagnetometerSensor
Radio
GnssReceiver
HapticActuator
AudioOutDevice
AudioInDevice
IrTransmitter
SdCard
Wifi
Ble
Usb
```

This layer may know:

- concrete chip;
- board;
- pins;
- rails;
- bus;
- modulation;
- physical degrees of capability.

Applications must not use it directly.

## 6.2 Product/service capabilities

These describe what an application can ask the system to do:

```text
Time
Position
Heading
MeshMessaging
Navigation
Haptics
AudioPlayback
AudioCapture
Notifications
PersistentStorage
CompanionLink
```

The exact list should be designed deliberately, not copied blindly from this example.

Applications depend on **services/product capabilities**, not silicon.

Examples:

- Navigator needs `Position`; it does not need to know whether a GNSS receiver exists.
- A user-relative navigation arrow needs `Heading`; it does not need to know whether heading came from a magnetometer, sensor fusion or another valid source.
- Mesh UI needs `MeshMessaging`; it does not need to know which RF chip routes packets.
- Clock uses `TimeService`, not `Rtc`.

---

# 7. REMOVE THE AMBIGUITY FROM `has(Capability)`

The old `has()` API is now semantically dangerous.

Example:

A Waveshare watch contains no GNSS receiver, but a Firefly Node can provide position.

So what does this mean?

```cpp
has(Capability::Gnss)
```

If false, the UI may hide the application even though attaching a node makes it usable.

If true, `has` no longer means physical presence.

Do not ship this ambiguity into code.

Preferred model:

- hardware presence is queried only below the service/platform boundary;
- application-facing state is expressed through product capability + `Availability`;
- `Unsupported` means the product cannot provide the capability in the current supported configuration;
- `Unprovisioned`, `Unreachable`, `Incompatible`, `Failed`, `Off`, `Ready` mean it is a real product feature with different current availability.

If a convenience function exists, name it by its actual semantics, e.g.:

```text
supports(...)
is_available(...)
availability(...)
```

Do not use `has()` when nobody can explain in one sentence what “has” means.

---

# 8. CAPABILITY AVAILABILITY

The current seven-state direction is useful:

```text
Unsupported
Unprovisioned
Unreachable
Incompatible
Failed
Off
Ready
```

Keep the good principle:

> Separate states when the user's remedy is different.

Do not conflate provider availability with data validity.

A GNSS provider can be `Ready` and still have `NO_FIX`.

A position can exist but be stale.

A remote datum can have two relevant ages:

- age when sampled at the provider;
- time since it reached this device.

Do not show a confident navigation result from stale data.

Create a centralized and tested state transition model.

Do not allow random components to mutate availability freely.

---

# 9. PROVIDERS

A service/product capability may be provided by:

```text
Local hardware
Firefly Node
```

Potentially more provider types later.

The phone is not a general-purpose replacement for missing core watch hardware.

Applications never select providers.

They ask a service.

Provider selection, failover, data freshness and origin are service-layer concerns.

Diagnostics and settings may expose provider identity because their purpose is to inspect/configure it.

---

# 10. HEADING IS NOT JUST “A COMPASS NUMBER”

The system must distinguish:

```text
bearing to target
heading/orientation
course over ground
```

These are different quantities.

Define heading with at least:

```text
angle
source
reference_frame
confidence
timestamp/age
validity
```

Potential sources:

```text
Magnetometer
SensorFusion
GNSSCourseOverGround
RemoteSensor
Unknown
```

Potential reference frames include at least:

```text
WatchBody
NodeBody
CourseOverGround
```

Do not invent `UserBody` unless the system can actually establish how the user is oriented.

## Critical review correction

A magnetometer in a separate Firefly Node does **not** automatically tell the orientation of the watch or user's wrist.

If the node is hanging from a belt, inside a backpack, clipped at an arbitrary angle or moving independently, `NodeBodyHeading` is not `WatchBodyHeading`.

Remote magnetometer data may be used for watch/user-relative heading only when there is a known and calibrated transform between frames.

Otherwise present it honestly as node orientation or do not use it for the user-facing compass arrow.

If only GNSS course-over-ground exists:

- it requires motion;
- it may become invalid at low speed;
- standing still is a first-class UI state;
- never display 0°/north just because course is absent.

---

# 11. LOCATION SERVICE

Create a centralized `LocationService`.

Conceptual output:

```text
position
position_source
timestamp
age
accuracy
quality/confidence
heading
heading_source
heading_reference_frame
heading_confidence
validity
```

Separate:

```text
provider ready
position valid
position fresh
heading valid
```

Do not collapse them.

Coordinates coming from node/mesh/phone are untrusted input:

- validate ranges;
- handle antimeridian;
- validate age;
- reject malformed values;
- never use a hostile coordinate to index arrays or overflow arithmetic.

---

# 12. GNSS DRIVER IS NOT THE NMEA PARSER

The reuse survey identified `minmea` as a promising MIT NMEA parser.

That can be useful, but do not let the parser become the GNSS architecture.

Separate:

```text
GnssDriver
  ├── module-specific power/configuration
  ├── module-specific binary/config protocol where needed
  ├── assistance-data handling
  ├── UART/I2C transport
  └── NMEA parsing (possibly minmea)

LocationService
  └── consumes normalized GNSS observations
```

A-GNSS/assistance is module-specific.

Do not call arbitrary downloaded ephemeris “A-GPS”.

Verify the actual module and its official assistance mechanism.

---

# 13. LOCAL MESH ON T-WATCH IS A REAL PRODUCT PATH

The repository currently contains a contradiction that must be resolved before `MeshService` implementation:

- T-Watch is described as the full product with on-board radio;
- architecture maps local radio to `MeshService`;
- but an ADR says the watch never runs MeshCore because the radio is in the Firefly Node.

That last statement is not universally true.

The intended product includes watches capable of running the same mesh applications against **local mesh hardware** where the fitted hardware and upstream support permit it.

Therefore design:

```text
MeshService
   ├── LocalMeshProvider
   └── NodeMeshProvider
```

Applications use the same `MeshService`.

Do not require a Firefly Node on a T-Watch configuration that can genuinely provide MeshCore-compatible mesh locally.

However, do not promise local MeshCore on every T-Watch radio variant.

First establish the real compatibility matrix:

```text
radio chip
band
modulation
current upstream MeshCore support
required driver/framework
regulatory constraints
resource impact
```

If a hardware variant cannot support MeshCore, report that honestly.

---

# 14. MESHCORE INTEGRATION — REUSE FIRST, FORK LAST

MeshCore is important.

Objectives:

- preserve upstream wire/protocol compatibility;
- minimize forks;
- pin a revision;
- isolate dependency boundaries;
- reuse upstream rather than recreating years of routing behavior.

Before local watch integration, investigate actual options instead of choosing from taste:

- direct component integration;
- isolated compatibility layer;
- upstreamable ESP-IDF work;
- a narrow Arduino compatibility island if justified;
- another architecture that keeps Arduino out of core;
- support only on hardware/upstream combinations that are actually viable.

Do not decide “port all MeshCore to ESP-IDF” or “never run MeshCore on watch” before a measured spike establishes the costs.

The Node may run stock/upstream MeshCore where that is the cleanest solution.

---

# 15. MESHCORE SECURITY

Do not market the mesh as “secure/private” merely because encryption exists.

Review the current upstream implementation and threat model.

Keep MeshCore compatibility.

Do not casually replace its crypto on the wire.

If Firefly adds stronger security above it, design it as a versioned, compatible layer with explicit cost and semantics.

Never implement new cryptographic primitives from scratch.

---

# 16. FIREFLY NODE

A Firefly Node may provide:

- mesh connectivity;
- GNSS position;
- additional sensors;
- object coordinates;
- weather;
- Home Assistant events;
- quest events;
- telemetry.

But capabilities and data feeds are not the same thing.

Examples:

```text
Mesh connectivity  -> capability/service
Position            -> capability/service
Weather update      -> data feed
Quest event         -> data feed
HA event            -> data feed
```

Model them separately.

The Node is not a second architecture.

Node-provided position enters `LocationService`.

Node-provided mesh enters `MeshService`.

Applications do not use `NodeGnssService` or `NodeMeshApp`.

---

# 17. NODE SOFTWARE ARCHITECTURE MUST BE DECIDED BEFORE NODE PROTOCOL IS FROZEN

Current open question:

Does the node run:

```text
stock MeshCore companion firmware
Firefly firmware
both
```

Before freezing a Firefly-specific high-level protocol, answer:

- which process/component terminates it;
- how it coexists with MeshCore companion traffic;
- whether both protocols share BLE/UART;
- how they are multiplexed;
- who owns pairing and identity;
- what happens during independent firmware upgrades.

Do not write a protocol that has no software endpoint architecture.

---

# 18. WATCH↔NODE PROTOCOL — PROVISIONAL UNTIL BENCHMARKED

The current TLV ADR contains good ideas:

- explicit versioning;
- request IDs;
- bounded parser;
- session reset;
- capability re-announcement;
- fragmentation;
- hostile-frame corpus;
- freshness/validity;
- separation from MeshCore internals.

Keep those goals.

But do **not** treat custom TLV as final merely because a large Meshtastic nanopb union uses RAM.

Before accepting the encoding ADR, benchmark realistic Firefly schemas.

Compare at least:

```text
Firefly TLV prototype
nanopb with Firefly-specific streaming/callback schema
one other mature compact option if reasonable
```

Measure on the target compiler:

```text
peak internal RAM
static RAM
flash/text
encoded bytes
decode behavior on malformed input
schema evolution cost
tooling/debuggability
fragmentation interaction
test burden
```

Do not compare a tiny Firefly TLV to Meshtastic's whole enormous `FromRadio` union and call the encoding question solved.

If TLV still wins, accept it with stronger evidence.

---

# 19. PHYSICAL MULTIPLEXING OF NODE PROTOCOLS

If MeshCore Companion and Firefly Node Protocol use the same physical relationship, state exactly how they coexist.

Examples:

- separate BLE GATT services/characteristics;
- separate UART channels;
- explicit outer mux framing.

Do not leave “two protocols layered on the same transport” as a diagram with no demultiplexing rule.

A parser must never mistake log text, MeshCore frames and Firefly frames for one another.

---

# 20. ANDROID COMPANION — DESIGN NOW, IMPLEMENT LATER

Android is the first mobile target.

The architecture must already account for:

- time synchronization;
- timezone synchronization;
- A-GNSS/assistance data where supported;
- Find My Phone;
- relay of selected phone app notifications;
- incoming call display with caller identity where Android permits it;
- settings;
- diagnostic log transfer;
- firmware update;
- backup/restore.

Base watch functions do not require the phone.

---

# 21. PHONE TIME

Centralize time in `TimeService`.

Potential sources:

```text
Phone sync
GNSS
RTC
```

The phone is expected to be a convenient precise sync source when connected.

But the watch continues correctly without it.

`TimeService` tracks:

```text
time
timezone
source
last_sync
quality
```

Do not let apps decide source priority.

---

# 22. PHONE NOTIFICATION ALLOWLIST — OWNER REQUIREMENT

The user chooses on the **Android phone** which applications' notifications may be relayed to the watch.

Therefore the primary per-application allowlist belongs in Android Companion.

Do not silently move the authoritative app allowlist to the watch.

The watch may still provide:

- global notification on/off;
- DND;
- category mute;
- privacy controls;
- clear/dismiss behavior where protocol supports it.

But per-phone-app selection is a phone-side UX.

---

# 23. FIND MY PHONE

Watch sends a command.

Watch displays only what it actually knows.

For example:

```text
Sent
Could not reach phone
Cancelled
```

Do not claim “phone is ringing” without an acknowledgement proving that state.

Research current Android restrictions for:

- silent/DND override;
- vibration;
- lock screen;
- background execution;
- Companion Device APIs;
- OEM battery optimizers.

---

# 24. HARDWARE COORDINATION / COEXISTENCE

Applications do not manually coordinate physical conflicts.

Create a lower-level system component, conceptually:

```text
HardwareCoordinator
CoexistenceManager
```

Exact naming is an ADR.

Responsibilities may include:

- bus ownership;
- shared PMU rail arbitration;
- RF coexistence;
- quiet windows;
- haptic scheduling around sensitive sensors;
- deadline/priority-aware operation;
- diagnostics and trace.

Do not immediately build a giant generic scheduler.

Start from real conflicts and measured needs.

---

# 25. HARDWARE OPERATION INTENTS

A useful minimal conceptual model:

```text
operation
priority
deadline
expected_duration
latency_tolerance
quiet_requirement
interruptibility
```

Priority examples:

```text
CRITICAL
HIGH
NORMAL
BACKGROUND
```

Emergency/SOS operations outrank cosmetic work.

A pretty animation may wait.

A critical transmission must not.

---

# 26. MAGNETOMETER SUPPORT FROM DAY ONE

Neither current target board appears to contain a magnetometer.

Still design for one now.

Requirements:

- sensor capability exists;
- external/provider source can be added;
- axis mapping;
- calibration storage;
- hard-iron calibration;
- soft-iron calibration;
- alignment;
- calibration wizard;
- confidence/validity;
- diagnostics;
- coexistence with haptics/audio/charging.

Do not implement fake hardware.

Do not invent settling intervals.

---

# 27. MAGNETOMETER CALIBRATION

Research mature implementations from:

- flight controllers;
- robotics;
- AHRS projects;
- hiking/navigation devices;
- mature sensor libraries.

Prefer proven math and test vectors.

User-facing calibration must be understandable.

Do not expose raw XYZ to normal users.

Store calibration with:

```text
sensor identity
board/provider identity
axis mapping
calibration version
timestamp
quality
```

Changing sensor/provider may invalidate calibration.

---

# 28. QUIET WINDOWS

Sensitive measurements may request a quiet interval.

Examples:

```text
magnetometer sample
GNSS acquisition
```

No app should contain:

```text
disable vibrator
delay(100)
read compass
```

The service asks the coordinator.

The coordinator schedules what can wait.

Delays/settling periods must be measured and board/provider-specific.

---

# 29. INTERFERENCE MATRIX

Maintain:

```text
docs/hardware/INTERFERENCE_MATRIX.md
```

For each pair record:

```text
Subsystem A
Subsystem B
theoretical mechanism
evidence level
observed effect
severity
measurement method
mitigation
board/revision
firmware commit
conditions
```

Evidence levels should distinguish:

```text
THEORETICAL RISK
OBSERVED
MEASURED
CONFIRMED NEGLIGIBLE
NOT MEASURABLE ON CURRENT HARDWARE
```

A theoretical risk is not permission to add latency/power-hungry mitigation.

Measure first.

---

# 30. COEXISTENCE TEST TOOLING

Build tools before hardware arrives where practical.

GNSS metrics may include:

- TTFF;
- fix state;
- satellite count;
- C/N0;
- HDOP/PDOP;
- fix stability;
- lost fixes.

Radio metrics may include:

- RSSI;
- SNR;
- packet loss;
- error counts;
- airtime.

Magnetometer:

- raw XYZ;
- variance;
- mean shift;
- saturation;
- drift.

Power:

- rail state;
- subsystem active time;
- battery voltage;
- PMU telemetry;
- measured current when external instrumentation exists.

Use repeatable A/B/A+B tests.

---

# 31. POWER

Power is a product feature.

Centralize:

```text
PowerService / PowerManager
```

Board-specific power behavior belongs below core.

Potential states:

```text
ACTIVE
IDLE
SCREEN_OFF
LOW_POWER
DEEP_SLEEP
```

Actual state machines are constrained by real touch/RTC/PMU hardware.

Do not assume similar boards have the same sleep strategy.

Shared rails are reference-counted or otherwise safely arbitrated.

---

# 32. OWNERSHIP DOES NOT MEAN “INITIALIZE EVERYTHING”

A previous architecture definition equated ownership with “initializes the part”.

That is too strong.

Ownership means:

> one component is responsible for the lifecycle, safe default, power state, access arbitration, diagnostics and policy for that hardware.

A valid owned state can be:

```text
intentionally untouched until used
left input/high-Z because another chip drives the line
rail off
driver not instantiated
```

Do not touch hardware merely to satisfy an ownership checklist.

This is especially important for ambiguous control/strapping/radio pins.

---

# 33. RESOURCE BUDGET

Maintain:

```text
docs/architecture/RESOURCE_BUDGET.md
```

Track:

- flash;
- internal RAM;
- PSRAM;
- LVGL buffers;
- assets;
- task stacks;
- message queues;
- mesh state;
- storage;
- long-term fragmentation.

Every growing structure needs a declared bound and behavior at the bound.

Measure:

- static allocation;
- free heap after boot;
- minimum heap;
- largest free block;
- task high-water marks;
- long soak trends.

---

# 34. SETTINGS

Create a core `SettingsService`.

Do not store settings inside the Settings application.

Use typed schema:

```text
type
unit
default
range/enumeration
scope
persistence
access level
regulatory relevance
schema version
```

Validate on write **and read**.

Firmware updates can change bounds.

Persisted invalid values must not silently survive forever.

---

# 35. RADIO SETTINGS

Store radio frequency as an integer in Hz, not float.

Keep distinct:

```text
hardware PA ceiling
regulatory radiated-power constraint
user preference
effective value
```

Do not collapse them into one number.

Region/regulatory policy is runtime data, not a `#ifdef` inside core.

If region/legal profile is unknown, transmitting remains closed.

No universal hardcoded RF default.

---

# 36. REMOTE RADIO SETTING CHANGES

Changing a network contract can sever the link used to change it.

Use a transactional strategy such as:

```text
propose/stage
apply
reconnect on new parameters
confirm
commit
```

If confirmation does not arrive:

```text
auto-revert
```

Use:

- idempotency token;
- generation number/CAS;
- atomic preset application;
- reboot-safe rollback semantics;
- physical recovery path.

Do not write five independently editable sliders for parameters that must match as one network contract unless UX research proves that is the right model.

---

# 37. REGULATORY

Do not hardcode a jurisdiction.

Do not trust another project's region table without checking primary regulatory sources.

Do not transmit until the region/profile is known and validated.

Respect:

- permitted frequency ranges;
- bandwidth/modulation constraints;
- duty-cycle/airtime constraints;
- radiated power, not merely PA register value.

Antenna gain/loss matters.

If required information is unknown, say so and fail safe.

---

# 38. AIRTIME

Track airtime for transmissions where regulations require it.

Make airtime diagnostics visible.

Test time-on-air arithmetic against known/reference formulas.

Never assume duty-cycle compliance from TX power alone.

---

# 39. BEAUTY IS A PRODUCT REQUIREMENT

Firefly must not look like an engineering demo.

Every screen is judged on:

- function;
- clarity;
- visual composition;
- emotion;
- performance;
- battery cost.

The watch should create small moments of delight without becoming noisy, childish or distracting.

The primary audience is adult.

Child Mode is separate and deliberately designed.

---

# 40. CANONICAL VISUAL DIRECTION — USE THE PROVIDED IMAGES

The three provided images are the starting visual source of truth.

They establish a direction:

- warm;
- pleasant;
- light;
- approachable;
- flat/minimal rather than glossy 3D;
- adult-friendly cartoon character;
- insect-first firefly mascot;
- restrained glow;
- cream/ivory surfaces;
- orange/amber emphasis;
- muted green/sage/teal secondary colors;
- dark olive rather than blue-black for night;
- rounded typography;
- simple line icons;
- generous spacing.

The mascot should be:

- recognizably a firefly/insect;
- less anthropomorphic than a tiny human;
- six-legged/insect-bodied in the canonical art direction;
- friendly and clever;
- suitable for adults;
- still appealing to a six-year-old;
- not babyish.

Glasses may remain a recognizable character cue.

Do not add human clothes/body proportions that turn it into a person with wings.

---

# 41. IMPORTANT: CONCEPT ART IS NOT A HARDWARE SPEC

The style-board contains illustrative UI content such as:

- heart-rate card;
- sample names/messages;
- example navigation distance;
- Wi-Fi/Bluetooth statuses;
- sample dates;
- `fireflyos.org`.

These are **mock visual content**, not product facts.

Do not implement heart-rate functionality because it appears in the artwork.

Do not claim a domain exists because it appears in the artwork.

Do not turn example numbers/text into defaults.

Use:

```text
visual language
composition
palette
mascot
wordmark
icon language
```

as reference.

Product functionality still comes from this specification and verified hardware.

---

# 42. BRAND PALETTE

The supplied boards contain two close palette explorations.

Use them to create design tokens, not scattered literal RGB values.

A reasonable initial canonical palette derived from the owner-provided board is:

```text
Firefly Orange   #FF8A40
Glow Amber       #FFC857
Meadow Green     #6FA07A
Leaf Sage        #A7B49C
Sky Teal         #6FB7B5
Warm Ivory       #FFF6E8
Sand Beige       #F3E8D1
Soft Clay        #E9DCC2
Cocoa Brown      #7A5E3A
Ink Olive        #2F3A2E
```

Another board includes close variants such as:

```text
Honey            #FFC24D
Apricot          #FFB26B
Warm Coral       #FF7A57
Warm Teal        #4F7F76
Cream            #FFF6E6
Dark Olive       #3C4033
```

Do not treat minor raster-board differences as sacred.

Consolidate into semantic tokens after contrast/display testing.

Examples:

```text
color.background.primary
color.background.surface
color.text.primary
color.text.muted
color.accent.primary
color.accent.glow
color.success
color.warning
color.navigation
color.night.background
```

No raw RGB scattered through UI code.

---

# 43. WORDMARK AND BRAND

Use the supplied Firefly wordmark direction.

Brand text:

```text
Firefly
Firefly OS
GLOW • GUIDE • CONNECT
```

Use the tagline only where contextually appropriate.

Do not plaster the logo on normal operational screens.

Brand moments are appropriate in:

- boot;
- onboarding;
- About;
- companion pairing;
- selected watchfaces;
- diagnostics export;
- documentation.

---

# 44. IMAGES MUST BE USED IN THE PRODUCT DESIGN

The owner explicitly wants imagery, not only flat rectangles and line icons.

Therefore images/illustrations are part of the UI language.

Use the mascot and derived illustrations **contextually**, for example:

- onboarding;
- empty states;
- successful pairing;
- message-delivery success;
- navigation/location states;
- “no fix yet” state;
- node disconnected explanation;
- charging;
- low-battery warning where appropriate;
- update complete;
- selected watchfaces;
- Child Mode;
- friendly error/recovery states;
- About screen.

Do not put the mascot on every screen.

Operational information must remain glanceable.

The adult UI should not feel like a children's toy.

---

# 45. IMAGE ASSET PIPELINE

Do not ship the full 2K concept-board PNGs as watch assets.

Keep originals as design references, then derive production assets.

Recommended repository structure:

```text
docs/ui/reference/
    firefly_brand_identity.png
    firefly_visual_style_board.png
    firefly_mascot_sheet.png

ui/assets/source/
    owner-provided / cleaned source art

ui/assets/generated/
    generated target assets

tools/assets/
    reproducible conversion scripts
```

The original reference images are immutable design references.

Generated assets should be reproducible.

For production imagery:

- crop to actual needed art;
- use transparency;
- generate board-appropriate sizes;
- measure flash size;
- measure decode/render cost;
- consider RGB565/RGB565A8 or appropriate LVGL formats;
- evaluate RLE/LZ4 where supported;
- avoid runtime filesystem complexity unless it buys something real;
- no network-loaded critical art.

Do not manually maintain huge generated C arrays.

Use a conversion tool/script.

LVGL supports images stored as C/ROM data or files and has official image conversion tooling. Re-check the exact LVGL version chosen before implementing the asset pipeline.

---

# 46. RASTER VS ICONS

Use images where illustration adds emotion or meaning.

Use lightweight vector/line/symbol-style icons where speed and clarity matter.

Do not convert every icon to a large bitmap.

The provided visual board's line-icon direction is a good baseline.

Keep a coherent icon grid, stroke weight and optical size.

---

# 47. DAY / NIGHT

Minimum:

```text
Day
Night
Automatic
```

Allow manual override and schedule.

Auto mode may use:

- local time;
- sunrise/sunset when coordinates are available;
- phone-provided timezone/location context if appropriate.

No internet dependency.

Night theme is not simply inverted day colors.

It should adjust:

- background;
- brightness;
- contrast;
- glow intensity;
- animation intensity;
- sound behavior where relevant.

Keep night warm and calm — dark olive/charcoal direction rather than a harsh blue-black UI.

Do not blast the user's eyes at night.

---

# 48. SOUND + HAPTICS

Sound and haptics are semantic feedback.

They are user-controlled.

At minimum:

```text
Sound On/Off
Haptics On/Off
```

Plan for categories:

```text
system
notifications
mesh
alarms
navigation
```

Support quiet hours / DND.

Emergency behavior must be explicit and user-understandable.

Centralize haptic patterns:

```text
tap
success
warning
message
navigation
error
SOS
```

HardwareCoordinator may delay non-critical haptics to protect sensitive measurements.

Apps never encode raw vibration motor timing as hardware policy.

---

# 49. CHILD MODE

Child Mode is for a six-year-old.

It is not adult mode with bigger text.

Goals:

- large touch targets;
- low reading burden;
- visual navigation;
- predictable layout;
- friendly art;
- clear battery/status;
- simple messages;
- SOS;
- direction toward parent/known point where data permits;
- honest offline/no-position states.

Hide dangerous/complex settings behind parent/advanced controls.

Do not make Child Mode patronizing or visually cheap.

The same Firefly identity should be recognizable.

---

# 50. I18N / LOCALIZATION — ENGLISH + RUSSIAN FROM THE FIRST REAL UI

This is a binding product requirement.

Firefly must support at least:

```text
English
Русский
```

from the first implemented UI vertical slice.

Localization is architecture, not later polish.

## 50.1 No user-visible literals

Do not write user-facing UI strings directly in screens/widgets.

Conceptually:

```cpp
tr(StringId::Settings)
```

The exact API must be researched/designed.

## 50.2 Catalogs exist immediately

English and Russian catalogs ship from the first screen.

Do not postpone the Russian catalog until after layout is “finished”.

## 50.3 Runtime switch

Language is a persisted setting.

Prefer switching without reboot.

## 50.4 Fallback

English is fallback for a missing translation.

Development/simulator builds log missing keys loudly.

Never render blank labels silently.

## 50.5 Core does not produce English prose

Core/service/protocol errors are structured:

```text
enum/error code
parameters
reason
```

UI translates them at the boundary.

Do not transmit hardcoded English strings from Node/Companion and then discover Russian UI cannot translate them.

## 50.6 User content remains user content

Do not “localize”:

- contact names;
- mesh messages;
- Android notification bodies;
- user labels.

Treat external text as UTF-8 data.

---

# 51. TYPOGRAPHY AND CYRILLIC

The concept boards propose a rounded modern typography direction such as:

```text
Nunito Sans
Inter
```

These are visual references, not frozen dependencies.

Before choosing fonts:

- verify license;
- verify Cyrillic glyph coverage;
- verify legibility at actual pixel sizes;
- measure generated LVGL font size;
- measure render performance.

Font strategy must include from day one:

- Basic Latin;
- Cyrillic;
- digits;
- punctuation;
- symbols actually used by the UI;
- units actually used.

Do not first create Latin-only embedded fonts and “add Cyrillic later”.

Do not blindly ship all Unicode.

Subset deliberately.

LVGL's font tools support custom ranges/subsets and compressed bitmap fonts; verify against the pinned LVGL version and measure the compression/render trade-off.

---

# 52. LOCALIZED LAYOUT

Russian strings are often longer.

Every reusable component must define behavior for:

- wrap;
- max lines;
- ellipsis;
- flexible width;
- minimum touch size;
- overflow.

Avoid layouts whose correctness depends on an English string fitting.

Do not construct translated sentences from arbitrary fragments.

Plan localized formatting for:

- date;
- time;
- plural forms;
- relative time;
- counts;
- distance/units.

You do not need a giant desktop CLDR stack.

You do need correct, testable behavior for EN and RU.

---

# 53. VISUAL TEST MATRIX

For major reusable screens/components, consider the cross product:

```text
240×240
410×502

Day
Night

Adult
Child

English
Russian
```

That is 16 meaningful visual configurations.

Not every screen needs 16 golden screenshots, but the system must exercise both locales and both geometries.

At minimum cover:

- Clock;
- Settings;
- Navigation;
- Mesh/messages;
- offline/error state;
- provider/node missing state.

---

# 54. DESIGN SYSTEM

Create:

```text
docs/ui/DESIGN_SYSTEM.md
```

and actual code tokens.

Token families:

```text
color
spacing
radius
typography
motion
icon size
image size
elevation/shadow
sound cue
haptic pattern
```

Do not scatter magic values.

The two displays are not pixel-identical products.

Use responsive/adaptive layout.

---

# 55. ACCESSIBILITY / LEGIBILITY

The product should be friendly, but not at the cost of readability.

Test:

- contrast;
- small text;
- night readability;
- outdoor display;
- touch targets;
- red/green dependence;
- low-information states;
- error text in both languages.

The warm palette may need contrast adjustments on actual panels.

Do not preserve a concept-board hex value if it fails real display readability.

---

# 56. UI DEFINITION OF DONE

A UI feature is not done because controls exist.

Applicable requirements:

- function works;
- loading state;
- empty state;
- offline state;
- unavailable-provider state;
- error state;
- English;
- Russian;
- day;
- night;
- both geometries;
- Child Mode considered;
- no raw user-facing literals;
- imagery used where appropriate;
- asset cost measured;
- sound/haptic semantics considered;
- power impact considered.

---

# 57. SIMULATOR — FIRST-CLASS TARGET

Build desktop simulator early.

Requirements:

- native host build;
- 240×240 profile;
- 410×502 profile;
- runtime switching where practical;
- mouse touch;
- keyboard buttons;
- scripted GNSS;
- scripted heading;
- simulated battery;
- simulated mesh;
- simulated provider attach/detach;
- simulated stale data;
- theme switching;
- locale switching;
- Adult/Child switching.

The simulator should make difficult states easy to reproduce.

For example:

```text
node attaches
node disappears mid-navigation
GNSS provider ready but no fix
position becomes stale
provider firmware incompatible
```

---

# 58. FIRST REAL VERTICAL SLICE

After P0 architecture reconciliation, do not spend forever writing more documents.

Target first real product slice:

```text
Simulator
→ localization EN/RU
→ design tokens / font pipeline
→ image asset pipeline
→ Day/Night
→ Clock
→ Settings
```

It should look like Firefly, not debug UI.

Include at least one purposeful use of the owner-provided visual assets/mascot.

Then use what was learned to refine framework APIs.

---

# 59. APPLICATION FRAMEWORK

Apps need a clear lifecycle, conceptually:

```text
create
open
pause
resume
close
event
```

Do not force these exact method names.

Application manifests declare required/optional product capabilities.

Capability requirements must not be optional trivia at one call site.

The framework owns navigation history and application timers/lifecycle.

Applications do not freely create FreeRTOS tasks and LVGL timers.

---

# 60. CONCURRENCY

Before many services exist, decide:

- which tasks exist;
- who owns them;
- what may block;
- how events are delivered;
- back-pressure;
- queue bounds;
- UI-thread rules;
- interrupt handoff.

Do not invent a huge framework.

But do not let every component invent its own concurrency model.

---

# 61. EVENT MODEL

Prefer event-driven hardware where verified IRQ/event support exists.

Do not poll aggressively because it is easy.

Do not assume an interrupt exists without verification.

Events/queues are bounded.

A slow consumer has defined behavior.

---

# 62. ERROR MODEL

Do not return a bare bool for meaningful failures.

Examples:

```text
NOT_SUPPORTED
PROVIDER_UNREACHABLE
PROVIDER_INCOMPATIBLE
BUSY
TIMEOUT
NO_FIX
STALE
RADIO_UNAVAILABLE
POWER_RESTRICTED
INVALID_VALUE
OUT_OF_RANGE
NOT_PERMITTED_HERE
NOT_CONFIGURED
PERMISSION_DENIED
INTERNAL_ERROR
```

Keep technical codes separate from localized user text.

---

# 63. GRACEFUL DEGRADATION

Examples:

No heading:
- navigator still shows position/bearing if useful;
- does not invent orientation.

Node lost:
- service remains alive;
- app receives capability change;
- UI explains remedy.

No haptics:
- application does not crash.

Phone disabled:
- watch remains useful.

No local mesh:
- node path may provide it if provisioned.

---

# 64. OPEN-SOURCE REUSE FIRST

Before implementing every significant subsystem, ask:

> Who already solved this problem well under similar constraints?

Investigate mature projects such as appropriate combinations of:

- MeshCore;
- Meshtastic;
- InfiniTime;
- Zephyr;
- LVGL projects;
- Watchy;
- ESP-Brookesia;
- RadioLib;
- Gadgetbridge;
- GNSS libraries;
- flight controllers/AHRS projects;
- Android companion implementations;
- simulator/test frameworks.

Do not limit research to ESP32.

The best compass/calibration idea may come from a drone project.

The best lifecycle lesson may come from another wearable OS.

---

# 65. REUSE DECISIONS

For a significant external solution use a clear decision:

```text
USE AS-IS
USE AS DEPENDENCY
WRAP
PORT
ADAPT
EXTRACT ALGORITHM
INSPIRE ARCHITECTURE
UPSTREAM PATCH
REIMPLEMENT
REJECT
```

Record:

- repository;
- version/tag/commit;
- license;
- exact useful files;
- maturity;
- constraints;
- memory/CPU/power implications;
- tests;
- known bugs;
- decision;
- reason.

If `REIMPLEMENT`:

> explain why mature existing solutions do not fit.

“Easier to write myself” is insufficient.

---

# 66. LEARN FROM BUGS, NOT JUST SOURCE

For upstream research read:

- issues;
- merged PRs;
- reverted changes;
- changelog;
- hardware-specific bugs.

Closed bugs often contain more engineering value than happy-path source.

Where an upstream bug is relevant, turn the lesson into:

- a Firefly design rule;
- a test;
- a bound;
- a failure mode.

Do not copy GPL/AGPL code into an MIT repository.

License analysis must precede dependency/copy decisions.

---

# 67. REUSE LEDGER

Keep `REUSE_LEDGER.md` current.

No contradictory sections like:

```text
Records: Empty
```

above actual records.

Delete or update stale template-state text when records exist.

Do not let documentation preserve mutually incompatible “current truths”.

Historical rationale belongs in ADR history, not misleading current status prose.

---

# 68. LOOKAHEAD RESEARCH PIPELINE

Development should operate roughly as:

```text
CURRENT implementation
NEXT research ready
AFTER NEXT preliminary research
tests/CI in parallel
```

While implementing CURRENT:

- research NEXT;
- if cheap/useful, preliminarily inspect AFTER NEXT;
- start long downloads/builds early.

When CURRENT finishes:

```text
NEXT → CURRENT
AFTER NEXT → NEXT
```

Do not research 20 milestones ahead.

Usually 1–2 steps of lookahead is enough.

---

# 69. HIDE LATENCY

Start long operations before they block the critical path where safe:

- toolchain downloads;
- upstream clones;
- large dependency builds;
- CI;
- static analysis;
- subagent research;
- asset generation;
- simulator setup.

While they run, continue independent useful work.

Do not sit idle waiting for a build if tests/docs/research can progress.

Do not launch speculative work with low probability of use.

---

# 70. SUBAGENTS

Good parallel subagent work:

- hardware research;
- upstream issue analysis;
- UI research;
- Android API research;
- power research;
- test design;
- license verification.

Bad parallel foundational writes:

- five agents independently redesigning core API;
- competing CMake structures;
- simultaneous incompatible event buses.

You are the architectural owner.

Subagents return evidence and recommendations.

Synthesize before foundational changes.

---

# 71. TASK MANAGEMENT

Maintain a live task list with states:

```text
NOW
NEXT
READY
BLOCKED
WAITING
DONE
```

Every task should carry:

```text
priority
goal
dependencies
acceptance criteria
research state
implementation state
tests
hardware required
```

Keep `NOW` small.

`READY` means genuinely startable.

---

# 72. CONTINUE WORK WHILE WORK EXISTS

Completing one task is not a reason to stop.

Normal loop:

```text
research enough
implement
build
test
inspect
fix
document
update task state
take next ready task
```

Do not ask:

> Shall I continue?

when the project scope already says to continue.

Stop only when:

- requested scope is complete;
- all remaining work is truly externally blocked;
- owner decision is required;
- irreversible operation is required;
- physical action is required;
- execution environment/resource limit is reached.

If one task blocks, record blocker and select another useful task.

---

# 73. TASK/STATUS CONSISTENCY IS A DELIVERABLE

The current repository has already shown stale task/document state.

Examples from the review included tasks saying:

```text
MeshCore research not started
ADR not written
toolchain not installed
```

after those things existed.

Fix this process.

When a task changes state:

- update TASKS;
- update STATUS if relevant;
- update DEPENDENCIES if a dependency decision changed;
- update ADR index if ADR state changed;

**in the same logical commit**.

A status file is not useful if it is several commits behind.

Before ending a work session, perform a consistency pass.

---

# 74. ADR STATUS MUST MEAN SOMETHING

Do not mark a task “DONE / settled” while the ADR is still `proposed` unless that distinction is explicitly intended.

Use states consistently:

```text
proposed
accepted
superseded
rejected
```

Do not build dozens of layers on an allegedly provisional decision while treating it as immutable.

Before M1 core APIs rely on a decision, accept it or state why it remains intentionally provisional.

---

# 75. CURRENT REVIEW RECONCILIATION — DO THIS BEFORE LARGE NEW CORE IMPLEMENTATION

On first run with this final prompt:

1. Inspect current HEAD and git status.
2. Preserve existing useful work.
3. Re-check whether each review issue below is still present.
4. Fix current documentation/architecture rather than blindly applying stale patches.

Mandatory P0 reconciliation:

```text
A. Split hardware inventory from product/service capabilities.
B. Remove ambiguous application-level `has()` semantics.
C. Fix Radio vs LoRa modelling and build a real radio/MeshCore compatibility matrix.
D. Resolve local T-Watch MeshCore path vs node-only contradiction.
E. Fix heading reference-frame semantics; remote node magnetometer != watch orientation.
F. Add EN/RU localization as a binding product requirement before first UI.
G. Make TASKS/STATUS/DEPENDENCIES consistent with current reality.
H. Ensure ADR statuses match how decisions are actually used.
```

Do not spend another week in research after this reconciliation.

Move into M1.

---

# 76. DEPENDENCIES

No floating “latest”.

For each dependency:

```text
source
version/commit
license
reason
upgrade strategy
```

Pin:

- ESP-IDF;
- LVGL;
- MeshCore where consumed;
- RadioLib if used;
- BSP/components;
- image/font tooling;
- test framework.

Dependency selection requires an actual build/spike where feasible.

Installed ≠ selected.

---

# 77. LVGL

Choose a supported LVGL version based on:

- ESP-IDF compatibility;
- both displays;
- simulator;
- image pipeline;
- font pipeline;
- memory;
- performance;
- maintained upstream.

Current official LVGL documentation supports image conversion to C/binary assets and custom font ranges/compression, but re-check the exact pinned version.

Do not architect against “latest docs” after pinning another version.

---

# 78. CI

Minimum direction:

- formatting/lint;
- host unit tests;
- simulator build;
- embedded builds for supported configurations;
- static checks where useful;
- asset generation reproducibility;
- localization checks;
- missing-string checks;
- selected visual/screenshot tests.

CI must say exactly what was tested.

No hardware-green badge without hardware.

---

# 79. LOCALIZATION CI

Add checks that can fail for:

- missing English key;
- missing Russian key;
- duplicate key;
- user-facing literal in restricted UI paths where practical;
- font subset missing required Cyrillic glyphs;
- generated localization catalog out of date.

Prefer machine enforcement over a comment saying “remember to translate”.

---

# 80. ASSET CI

Production image assets should be generated reproducibly.

CI or a deterministic local script should be able to:

- regenerate;
- report file sizes;
- detect stale generated output.

Track asset budget per board.

Do not let a new mascot illustration silently add hundreds of kilobytes without review.

---

# 81. SECURITY

Research:

- signed firmware;
- secure boot;
- flash encryption;
- key storage;
- eFuse;
- companion pairing/auth;
- node identity;
- replay;
- hostile external input.

Do not perform irreversible security provisioning without explicit owner approval.

No production secrets in repo.

Prepare safe development configuration first.

---

# 82. OTA

Plan:

- signed images;
- rollback;
- partition layout;
- independent watch/node versioning;
- protocol compatibility across version skew;
- settings migration.

OTA is not the first MVP blocker.

But storage/partition decisions must not make it impossible later.

---

# 83. NEVER IRREVERSIBLE WITHOUT OWNER APPROVAL

Do not:

- burn eFuses;
- enable irreversible secure boot;
- enable irreversible flash encryption;
- destroy keys;
- overwrite production identity;
- flash physical hardware when action is potentially destructive;

without explicit approval.

It is fine to prepare:

- dev keys;
- config;
- instructions;
- build artifacts.

---

# 84. HARDWARE BRING-UP

When hardware exists:

- identify exact board revision;
- identify radio variant;
- identify GNSS variant;
- read chip IDs/registers where useful;
- verify rail mapping;
- verify flash/PSRAM;
- verify safe GPIO defaults;
- bring subsystems up incrementally.

Do not turn every peripheral on at boot.

Use a bring-up checklist.

Record measured results.

---

# 85. UI ART DIRECTION AND OPEN-SOURCE RESEARCH

Before major UI subsystem/pattern work, also review mature open-source wearable UX:

- navigation patterns;
- launcher;
- notification cards;
- settings;
- watchfaces;
- list density;
- text entry.

Reuse interaction lessons, not someone else's visual identity.

Firefly must keep its own warm visual character.

Do not clone Apple Watch/Wear OS/Garmin assets or proprietary design.

---

# 86. DESKTOP DESIGN REFERENCES ARE NOT AUTOMATIC FINAL ASSETS

The provided boards are concept sheets.

During implementation:

- preserve brand intent;
- simplify art for the watch;
- test at real pixel size;
- remove detail that muddies at 240×240;
- keep adult-first restraint;
- keep mascot insect-like.

A 2K illustration that looks beautiful on desktop may become noise at 40 px.

Create small-size variants deliberately.

---

# 87. MILESTONES

## M0 — Repository + Research
Done when:

- repository exists;
- research gate useful;
- hardware matrix credible;
- essential dependencies narrowed;
- native build works.

## M0.5 — Review Reconciliation
Done when:

- all P0 review corrections in §75 are reflected in current architecture/docs;
- task/status consistency restored;
- i18n requirement recorded;
- design references are in repo;
- asset and localization work are in backlog;
- local-vs-node mesh model no longer contradicts itself.

## M1 — Simulator + Product Design Foundation
Done when:

- simulator runs;
- 240×240;
- 410×502;
- EN/RU runtime switching;
- design tokens;
- font subset with Cyrillic;
- image asset pipeline;
- provided visual style materially represented;
- Day/Night;
- first adult Clock;
- first Settings;
- basic Child Mode direction;
- reference screenshots.

## M2 — Board Bring-Up
Per board:

- boot;
- display;
- touch;
- PMU basics;
- input;
- diagnostics.

## M3 — Core Services
- event/concurrency model;
- product capability registry;
- settings;
- time;
- power;
- hardware coordination;
- storage.

## M4 — Mesh
- compatibility matrix;
- local provider where supported;
- node provider;
- MeshCore interop;
- simple messaging;
- diagnostics/simulator.

## M5 — Location/Navigation
- GNSS driver abstraction;
- position;
- quality;
- Navigator;
- honest heading model;
- magnetometer-ready architecture.

## M6 — Power + Coexistence
- sleep;
- profiler;
- coordinator trace;
- measurement tooling;
- real interference results when hardware exists.

## M7 — Product Polish
- transitions;
- images;
- haptics;
- sounds;
- child polish;
- error/offline states;
- performance;
- accessibility;
- localization polish.

## M8 — Security / OTA groundwork

## M9 — Android Companion implementation
Architecture should already exist; implementation may be a separate agent/project phase.

---

# 88. FIRST APPLICATIONS

## Clock
- time;
- date;
- battery;
- good watchfaces;
- Day/Night;
- EN/RU;
- Child variant;
- tasteful mascot/image option.

## Settings
- language;
- theme;
- brightness;
- sound;
- haptics;
- power profile;
- Child Mode;
- diagnostics;
- mesh/radio settings where appropriate.

## Mesh
- contacts;
- messages;
- channels;
- unread;
- connection/provider state;
- EN/RU chrome;
- user message text unchanged.

## Navigator
- position;
- distance;
- target bearing;
- heading when valid;
- confidence;
- stale/offline/provider states;
- no fake compass while stationary.

## SOS
- very clear UX;
- protection against accidental activation;
- no dangerous delay;
- status based on actual evidence.

## Diagnostics
- capabilities;
- providers;
- board inventory;
- GNSS;
- radio;
- sensors;
- battery;
- resource/power;
- coexistence trace;
- raw advanced data.

---

# 89. PRODUCT REQUIREMENTS CHECKLIST

At every major milestone confirm the project still preserves:

1. One Firefly framework for multiple ESP32-S3 wearables.
2. Hardware-specific differences stay below app layer.
3. Hardware inventory is distinct from product capabilities.
4. MeshCore compatibility.
5. Local mesh on compatible T-Watch configurations without requiring a node.
6. Node-provided mesh/location through the same app-facing services.
7. No assumption that every T-Watch radio is LoRa/MeshCore compatible.
8. Autonomous watch operation without a phone.
9. GNSS/location.
10. Honest heading/navigation semantics.
11. Good battery life.
12. HardwareCoordinator/coexistence.
13. Beautiful adult-first UI.
14. Warm Firefly visual identity based on supplied references.
15. Actual use of imagery/mascot where appropriate.
16. Day/Night.
17. Sound/haptics controllable.
18. Child Mode for a six-year-old.
19. English from first UI.
20. Russian from first UI.
21. Cyrillic-capable font pipeline.
22. Desktop simulator.
23. Android Companion architecture.
24. Phone time sync.
25. A-GNSS/assistance through phone where the fitted module supports it.
26. Find My Phone.
27. Android notification relay.
28. Per-phone-app notification allowlist configured on Android.
29. Incoming call indication.
30. Magnetometer-ready architecture.
31. Magnetometer calibration plan.
32. Explicit heading reference frames.
33. System-level handling of hardware conflicts.
34. Node/Doctor extensibility.
35. Security can be strengthened without redesign.
36. Resource budgets are measured, not guessed.
37. Reuse-first development.
38. Rolling research stays ahead by roughly 1–2 tasks.
39. Task/status documents are current.
40. Repository remains understandable to the next agent.

---

# 90. DEFINITION OF DONE FOR A FEATURE

Where applicable:

- builds;
- tests;
- no fake-green claims;
- app layer does not touch hardware;
- provider semantics correct;
- errors structured;
- docs updated;
- task state updated;
- simulator coverage;
- embedded targets compile;
- resource impact known;
- power impact considered;
- coexistence impact considered;
- EN/RU;
- Day/Night;
- Child Mode considered;
- both display geometries;
- image/brand direction considered;
- accessibility/legibility reviewed.

---

# 91. BLOCKERS

Use:

```text
BLOCKED:
Reason:
Evidence:
Impact:
Possible options:
Recommended next action:
```

A local blocker does not stop the project when independent useful work exists.

---

# 92. GIT

Use small logical commits.

Do not:

- destructively reset unrelated work;
- rewrite published history casually;
- push somewhere unapproved.

Keep the repo buildable frequently.

A commit that changes architectural truth should update all documents that claim that truth.

---

# 93. STATUS

Maintain concise:

```text
Current milestone
Current implementation
Next ready
Lookahead research
Long-running operations
Blocked
Waiting on owner
Build/test state
Hardware tests pending
Recently completed
```

Do not turn STATUS into a novel.

Do not leave it stale.

---

# 94. STARTING INSTRUCTIONS FOR THIS FINAL PROMPT

When you receive this prompt in the existing FireflyOS repository:

### Step 1
Inspect current HEAD, git status and repository structure.

### Step 2
Read current:

```text
CLAUDE.md
STATUS.md
TASKS.md
docs/adr/
docs/architecture/
docs/research/
docs/master-prompt.md
docs/development-addendum.md
```

### Step 3
Treat this file as the new superseding master specification.

Do not delete useful historical ADR reasoning.

Mark obsolete master/addendum docs as superseded or consolidate references so there is no ambiguity about which requirements are current.

### Step 4
Copy the supplied design reference images into stable repository paths:

```text
docs/ui/reference/
```

Preserve originals.

Record that they are owner-provided project art/reference.

### Step 5
Perform the P0 review reconciliation in §75.

### Step 6
Update TASKS/STATUS/DEPENDENCIES to match reality.

### Step 7
Do only the research necessary to unblock M1 and the corrected capability/radio model.

### Step 8
Start the first real M1 vertical slice.

Do not stop after writing another plan if useful implementation work is ready.

---

# 95. DO NOT OVERENGINEER

Do not build abstractions because their names sound architectural.

Every layer must answer:

> What concrete failure/coupling does this prevent?

Be cautious with:

- generic schedulers;
- dependency injection frameworks;
- plugin systems;
- dynamic allocation;
- universal event buses;
- generic serialization frameworks.

Firefly should be extensible and understandable.

---

# 96. DO NOT CONFUSE “MVP” WITH “UGLY”

A first Clock with only a few features should still feel like Firefly.

MVP means reduced scope.

It does not mean:

- raw LVGL defaults;
- random colors;
- Latin-only text;
- debug fonts;
- no empty states;
- no design;
- no imagery;
- broken night mode.

---

# 97. DO NOT SHIP FAKE FEATURES

If Android Companion is not implemented:

- do not show a production Find Phone button that does nothing.

If heart-rate hardware does not exist:

- do not ship the heart-rate card from the concept board.

If heading is invalid:

- do not draw a confident heading arrow.

Feature flags/simulator mocks are fine when clearly marked.

Production UI must not promise absent behavior.

---

# 98. DECISION PRIORITY

When requirements conflict:

1. Safety / avoiding irreversible harm
2. Correctness
3. Data integrity
4. Hardware constraints
5. User experience
6. Battery life
7. Maintainability
8. Performance
9. Implementation convenience

UX remains a core product requirement.

This ordering only means beauty cannot override emergency correctness.

---

# 99. FIREFLY PHILOSOPHY

For every subsystem ask:

## Engineering
> Is this reliable, measurable, bounded and maintainable?

## Human
> Is this pleasant, understandable and honest?

Firefly is a wearable object used every day.

It should feel warm and alive without becoming a toy.

The mascot and glow are not decoration pasted on top of firmware.

They are part of a coherent product language built on a trustworthy system.

---

# 100. FINAL OPERATING MANTRA

**Verify hardware.  
Reuse proven work.  
Read other people's bugs.  
Research one step ahead.  
Hide latency.  
Keep the task queue honest.  
Separate silicon from product capability.  
Never call a radio LoRa unless it is.  
Never call a remote compass the watch's heading unless the reference frames agree.  
Localize from the first screen.  
Use the supplied Firefly art deliberately.  
Measure asset, memory, power and RF costs.  
If one task blocks, take another.  
While useful safe work remains, keep going.  
Build evidence, not assumptions.  
Design the product, not just the firmware.**
