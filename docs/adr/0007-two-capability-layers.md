# 0007 — Two capability layers, and the end of `has()`

Status: **accepted**
Date: 2026-08-21
Supersedes the Decision of [ADR-0001](0001-capability-model.md). Extends
[ADR-0004](0004-capability-sources.md), which is unaffected.

Accepted rather than proposed. Final §74 forbids building layers on a nominally
provisional decision while treating it as fixed, and this decision is the one
every service signature depends on. It is also not really ours: final §6 and §7
state it as a review correction.

## Context

Final §75 items **A** and **B**. The review said the architecture *mixes*
hardware and product concepts in one capability set. Re-checking found something
slightly worse: there was no mixture, because there was only ever one layer —
the hardware one — and applications were pointed straight at it.

`ARCHITECTURE.md` listed the application-facing capability set as:

```
Display · Touch · Buttons · Pmu · BatterySense · Rtc · Accelerometer ·
Gyroscope · Magnetometer · Lora · Gnss · Haptics · AudioOut · AudioIn ·
IrTransmit · SdCard · Wifi · Ble
```

Every entry is a part. `Pmu`. `Rtc`. `BatterySense`. Nothing named `Position`,
nothing named `MeshMessaging`, nothing an application actually wants. An
application asking `Gnss` was asking about a chip, and the architecture's own
headline rule — *applications ask what the device can do, never which device it
is* — was being violated by the very enum meant to enforce it.

`has()` is where the damage shows. Final §7 poses it exactly:

> A Waveshare watch contains no GNSS receiver, but an Attadipa Node can provide
> position. So what does `has(Capability::Gnss)` mean? If false, the UI may hide
> the application even though attaching a node makes it usable. If true, `has`
> no longer means physical presence.

Both answers are wrong, and the repository had committed to the second one:
ADR-0004 had already widened `Absent` into `Unsupported` and
`Unprovisioned` precisely because absence stopped being permanent, but `has()`
was left in place, documented as *"cheap, for gating UI"*. So `has()` meant "on
this device, possibly over a link, possibly not right now" — a sentence nobody
would write, which is final §7's actual test: *"Do not use `has()` when nobody
can explain in one sentence what 'has' means."*

Three concrete failures were already reachable before any code existed:

- A Waveshare board would never offer the Navigator, because no GNSS chip is on
  it — even with a node attached and a fix on screen.
- A T-Watch fitted with a CC1101 would offer mesh messaging, because a radio is
  present, and no message would ever be delivered
  ([ADR-0003](0003-radio-not-lora.md)).
- Heading would be gated on `Magnetometer`, which is `false` on both boards —
  hiding a feature that GNSS course-over-ground can honestly provide
  ([ADR-0009](0009-heading.md)).

Each is the same bug: a question about silicon standing in for a question about
the product.

## Decision

**Two layers, two vocabularies, two audiences, and a boundary that is enforced
by the build rather than by review.**

### 1. The hardware inventory — what is physically here

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
```

This layer knows chips, pins, rails, buses, addresses, IRQ topology and
modulation families. It is per **board**, contributed by the BSP and — for a
node's own inventory — by the provider that speaks for it.

Note the renames. `Lora` is gone: the part is a `Radio`, and whether it can do
LoRa is a fact *about* the radio, not the name of the slot
([ADR-0003](0003-radio-not-lora.md)). `Magnetometer` becomes
`MagnetometerSensor` and `AudioOut` becomes `AudioOutDevice`, so that no
hardware name can be mistaken for the product capability of similar name — the
compiler catches the confusion this ADR is about.

It answers three things and no more:

```cpp
bool               present(HardwareFeature) const;   // is the part here
const RadioInfo*   radio() const;                    // which part, typed
HardwareState      state(HardwareFeature) const;     // is the driver up
```

`present()` is honest and narrow: it is a fact about a board, it does not
change while running, and no node ever makes it true. A Waveshare board has
`present(GnssReceiver) == false` forever, and that is *correct* — the board has
no GNSS receiver. What changes is whether the device can report a position, and
that is a different question asked of a different layer.

Capabilities stay enumerated **per sensing axis** rather than per part —
`Accelerometer` and `Gyroscope` are separate entries. That part of ADR-0001 was
right and is carried forward unchanged.

### 2. Product capabilities — what an application can ask for

```cpp
enum class Capability : uint8_t {
    Time,               // a wall clock worth displaying
    Position,           // a geographic fix
    Heading,            // an orientation or a course, with a frame
    MotionSensing,      // steps, wrist gestures, activity
    MeshMessaging,      // messages to and from other devices
    Haptics,            // semantic tactile feedback
    AudioPlayback,
    AudioCapture,
    NotificationRelay,  // a phone's notifications, on the wrist
    InfraredBlast,      // controlling other devices in the room
    PersistentStorage,  // settings and app state that survive a reboot
    RemovableStorage,   // media the user can take out
    CompanionLink,      // the phone link itself, for its own settings screen
};
```

Thirteen, and the list is argued rather than copied — final §6.2 asks for
exactly that. Four decisions inside it are worth stating, because each one is a
place the obvious answer is wrong:

**`Navigation` is not here**, though final §6.2's example list contains it.
Navigation is an application built on `Position` and `Heading`; there is no
provider that supplies "navigation" and no hardware that stops supplying it. The
test in [ARCHITECTURE](../architecture/ARCHITECTURE.md) is *can an application
be written that is useless without it* — and for `Navigation` that application
is the Navigator itself, which is circular. Adding it would mean the Navigator
gates itself on its own existence.

**`Position` and `Heading` are separate**, and this is not tidiness. On a
T-Watch standing still with a GNSS fix, `Position` is `Ready` and valid while
`Heading` is `Ready` and *invalid* — course-over-ground needs motion. One
capability could not express that, and a navigator that treated them as one
would draw a confident arrow at a standing user, which final §97 forbids by
name.

**`Time` is here even though it is never `Unsupported`.** Both boards have an
RTC, and a device with no RTC at all would still have an uptime and a user-set
time. It earns its place because *availability is not the only thing a
capability carries*: `TimeService` also reports source and quality, and a clock
synced from GNSS a minute ago and a clock the user typed in last March are
different things the UI must be able to distinguish.

**`PersistentStorage` and `RemovableStorage` are separate.** Both boards have
flash; only one has an SD slot. An application that stores its state is useless
without the first and merely poorer without the second, and conflating them
would make settings appear to depend on a card the user can eject.

Deliberately **not** capabilities, recorded so the question is not reopened
every quarter: `Display`, `Touch` and `Buttons` (an application that cannot draw
is not running); `Pmu` and `BatterySense` (`PowerService` is always present —
what varies is telemetry detail, which is a field, not a state); `Wifi` and
`Ble` (transports, not user-facing capability — what the user cares about is
`NotificationRelay` or `MeshMessaging`, and which radio carried it is the
service's business); anything a node merely *feeds* — weather, Home Assistant
events, quest events, telemetry — which are data feeds and are already
distinguished in [ADR-0004](0004-capability-sources.md) §4.

### 3. The application-facing API, and the funeral for `has()`

```cpp
// Could this device ever do this, in some supported configuration?
// Stable enough to decide whether an application appears in the launcher.
bool supports(Capability) const;        // availability() != Unsupported

// Can it do it right now?
bool is_available(Capability) const;    // availability() == Ready

// Why not, and what can the user do about it?
Availability availability(Capability) const;
```

`has()` does not exist. Not renamed, not deprecated — absent, so that no call
site can survive the change by accident.

The three names are not synonyms and the difference is the whole point:

| Call | Question | Changes when |
|---|---|---|
| `supports()` | *could this device, ever?* | never, at runtime — it is a property of the device's supported configurations |
| `is_available()` | *right now?* | a node attaches, a rail is cut, a driver fails |
| `availability()` | *and what should we tell the user?* | same, but carries the remedy |

So a Waveshare board with no node attached reports
`supports(Position) == true` and `availability(Position) == Unprovisioned`.
The Navigator is in the launcher, and opening it explains that it needs a
Attadipa node. That is the behaviour final §7 asks for, and neither answer
`has()` could have given produces it.

`Unsupported` is reserved for *no configuration of this device can provide it*
— the Waveshare board's `InfraredBlast`, or `MeshMessaging` on a T-Watch fitted
with a radio that has no LoRa modulator and no node attached. It is terminal
([ADR-0004](0004-capability-sources.md) §2a).

The seven-state enum, the provider `Origin` axis and the two-age freshness model
are unchanged from ADR-0004 and endorsed by final §8.

### 4. The mapping is many-to-many, runtime, and nobody above the service sees it

| Capability | Providers, in preference order |
|---|---|
| `Time` | GNSS · companion · RTC · user |
| `Position` | local `GnssReceiver` · node |
| `Heading` | `MagnetometerSensor` (neither board has one) · GNSS course-over-ground · node |
| `MeshMessaging` | local `Radio`, *only if* the fitted chip and the pinned MeshCore support it · node |
| `NotificationRelay` | companion only — and it is a phone-only capability, which is exactly why [ADR-0002](0002-companion-is-optional.md) allows it to be `Unprovisioned` rather than requiring a fallback |

A capability is `Unsupported` only when **no** row can ever be satisfied.
Applications never see this table, never name a provider, and never learn which
row was used. Diagnostics and Settings do, because inspecting and configuring
providers is their entire purpose (final §9).

### 5. The boundary is a link boundary, not a comment

`HardwareFeature`, `present()`, `state()` and the typed descriptors live in a
library that `apps/` **does not link against**. An application that tries to ask
about a chip fails to build.

This is the concrete failure the layer prevents, in the sense final §95 demands:
without it, the rule is a review convention, and review conventions lose to
deadlines. `#ifdef BOARD_X` was already forbidden in `core/` and `apps/` and was
already unenforced; this makes the stronger version of the same rule mechanical.

Two consequences that are features rather than costs: the simulator can present
any configuration without compiling for a board, and a reviewer can answer *does
this application touch hardware* by reading the link line instead of the
diff.

## Alternatives considered

**One layer, with the enum renamed to product terms.** Rejected. The BSP still
has to describe a chip — a driver needs `RadioInfo`, an axis map, a bus address.
Renaming the enum would push those facts into ad-hoc side channels, which is
strictly worse than a second named layer: the knowledge does not disappear, it
just stops being findable.

**One layer, with `has()` kept and documented carefully.** Rejected, and the
documentation attempt is the evidence. `has()` was documented — *"cheap, for
gating UI"* — and the documentation was already false when it was written,
because ADR-0004 had made presence non-permanent the same day. A comment cannot
hold a definition that the type system contradicts.

**Three layers — hardware, service, product.** Rejected as premature. The
proposed middle layer ("a GNSS receiver that is powered and configured") is not
a third vocabulary, it is the hardware layer's `HardwareState`, which already
exists. Final §95: *every layer must answer what concrete failure it prevents*,
and this one answered "it feels more complete".

**Let applications query providers directly, for better UI.** Rejected, and this
is the rejection with the longest reach — it is the same one
[ADR-0002](0002-companion-is-optional.md) made about the phone, and it holds for
the same reason. Once one application knows a node exists, "applications do not
depend on which device this is" stops being checkable, and every subsequent
violation is defensible by precedent. Where an app genuinely needs to explain a
gap, `availability()` carries the remedy and the service carries the data age —
both meaningful without naming a provider.

**Derive the product set mechanically from the hardware set.** Rejected: the
interesting cases are exactly the ones that are not mechanical. `MeshMessaging`
depends on the radio's *modulation* and on upstream support, not on the radio's
presence. `Heading` has three unrelated possible sources and a validity that
depends on the user's speed. A generated mapping would encode the easy half and
silently omit the half that matters.

## Consequences

**Easier.** An application says what it needs. `AppManifest{required, enhanced_by}`
([ADR-0004](0004-capability-sources.md) §5) now lists things that mean something
to a product person — `Position`, `MeshMessaging` — rather than chip names. The
launcher can be built from manifests. A reviewer can check the hardware rule by
reading a CMake target.

**Harder.** Two enums to keep in step, and a mapping table that is real code
somebody must maintain. Every new part needs an inventory entry *and* an answer
to "does this change what an application can ask for" — often "no", which is the
correct and slightly unsatisfying answer for a PMU.

**Committed to.** A capability registry that owns the mapping and is the single
place availability is computed. A link-level separation between `platform/` and
`apps/` from the first CMake file, not retrofitted. Diagnostics that can show
both layers side by side, including declared-versus-detected, because a
descriptor that disagrees with the hardware is worse than no descriptor.

**Testable.** For every capability, a simulator scenario per availability state
(final §57). The one that catches real bugs: `supports() == true` while
`is_available() == false`, sustained, with the application open — which is a
Waveshare board waiting for a node, and is the ordinary case rather than the
exotic one.

**What this does not change.** ADR-0004 in full — the seven states, the
transition table, `Origin`, the two ages, capabilities-are-not-feeds. ADR-0001's
four rejected alternatives, all still rejected for the reasons given; this ADR
replaces what ADR-0001 decided, not what it ruled out.
