# 0001 — Capability model: presence, variant, degree, availability

Status: **proposed — amended 2026-08-21 by [ADR-0004](0004-capability-sources.md)**
Date: 2026-08-21

> **Amendment notice.** Two claims in this ADR were stated more broadly than the
> evidence supported, and a product decision the same day
> ([OWNER_DECISIONS OD-1](../research/OWNER_DECISIONS.md)) made both of them
> false:
>
> - *"The descriptors are produced by the BSP. Nothing above the platform layer
>   constructs them."* A capability may now be provided by a separate Firefly
>   node, which the BSP cannot know about at build time.
> - `Availability::Absent` — *"not on this board; the feature does not exist
>   here."* Absence is no longer permanent. A board with no GNSS acquires GNSS
>   when a node is attached.
>
> The three-questions structure below — *is it there*, *which one*, *can I use
> it right now* — survives intact and is the reason the change costs an
> amendment rather than a rewrite. ADR-0004 extends the availability enum and
> adds a provider axis. Read this ADR for the reasoning; read ADR-0004 for the
> enum that is actually in force.

## Context

The specification proposes that applications query hardware like this:

```cpp
device.capabilities().has(Capability::GNSS)
```

The board survey of 2026-08-21 (see
[`../research/HARDWARE_MATRIX.md`](../research/HARDWARE_MATRIX.md)) shows a
boolean cannot carry what the code actually needs to know:

- The T-Watch S3 Plus ships with **one of five** LoRa chips — SX1262, SX1280,
  CC1101, LR1121, SI4432 — selected at purchase. Sub-GHz and 2.4 GHz are not
  interchangeable: they differ in regulatory region, in range, and in whether
  they can talk to another Firefly at all. `has(Lora) == true` is the same
  answer for a device that can reach a village and one that can reach a room.
- Its GNSS is **one of two** modules with different power-up sequences and
  different assistance mechanisms.
- The T-Watch IMU is an accelerometer with no gyroscope; the Waveshare IMU is
  six-axis. Both are "IMU present", but only one can report rotation.
- The Waveshare board has neither LoRa nor GNSS, so two headline features must
  be *absent* rather than merely unavailable — a distinction a boolean can
  express but the rest of the system currently cannot act on.
- A part can also be present and broken. The vendor BSP for the Waveshare board
  does not initialise the QMI8658, AXP2101 or PCF85063 that are on the board;
  a driver that fails to come up is a state the UI must be able to explain.

So there are three orthogonal questions — *is it there*, *which one*, and *can I
use it right now* — collapsed into one boolean.

## Decision

A capability answers all three, through two access paths and one state enum.

**Presence** stays cheap, because most callers only gate UI with it:

```cpp
if (!caps.has(Capability::Gnss)) { /* the app is never offered */ }
```

**Variant and degree** come from a typed, optional descriptor, defined by the
BSP and consumed by drivers and services:

```cpp
struct LoraInfo {
    LoraChip chip;          // Sx1262 | Sx1280 | Cc1101 | Lr1121 | Si4432
    RadioBand band;         // SubGhz | Ghz24
    int8_t   max_tx_dbm;
};

struct ImuInfo {
    ImuPart  part;
    bool     accelerometer;
    bool     gyroscope;
    bool     magnetometer;
    uint16_t max_odr_hz;
};

if (auto lora = caps.lora()) { /* lora->chip, lora->band */ }
```

**Availability** is a separate axis from presence:

```cpp
enum class Availability {
    Absent,      // not on this board — the feature does not exist here
    Failed,      // on the board, initialisation failed
    Off,         // deliberately powered down; can be brought up
    Ready,       // usable now
};
```

`Absent` and `Failed` must never render identically. "This watch has no
compass" and "the compass is broken" are different sentences to a user.

> **Amended.** That principle — one state per sentence a user would be told —
> is right, and applying it to node-provided capabilities forces the enum wider.
> "This watch has no compass", "Maps needs a Firefly node" and "your node is out
> of range" are three sentences, and `Absent` was carrying all three.
> [ADR-0004](0004-capability-sources.md) splits it.

Capabilities are enumerated per sensing axis rather than per part:
`Accelerometer` and `Gyroscope` are separate entries, and `Magnetometer`
exists in the enum even though neither board has one — so that adding one later
changes an answer, not an interface.

The descriptors are produced by the BSP. Nothing above the platform layer
constructs them, and nothing above it may branch on board identity.

> **Amended.** The second clause holds; the first does not. A descriptor may
> also be produced by an attached provider — a Firefly node — and registered at
> runtime. What does not change is that *applications* never construct or branch
> on descriptors, and never learn where one came from. See
> [ADR-0004](0004-capability-sources.md).

## Alternatives considered

**Boolean `has()` only, as the specification suggests.** Rejected: it cannot
distinguish a 2.4 GHz radio from a sub-GHz one, which is a regulatory and
interoperability difference, not an implementation detail. Every caller needing
more would reach around the abstraction, and within a few weeks board identity
would be back in application code.

**A capability per concrete part** — `Capability::Sx1262`, `Capability::Bma423`.
Rejected: applications would then enumerate parts, which is board identity
wearing a different hat. It also grows without bound as boards are added.

**Feature flags at compile time.** Rejected: it produces a separate binary per
board *variant*, and with five radio chips and two GNSS modules the T-Watch
alone would need ten. It also makes the simulator unable to present
configurations it was not compiled for, which defeats a first-class simulator.

**An untyped bag** — `variant` as a `uint16_t` plus a `traits` bitfield.
Rejected: it type-checks nothing and moves the errors to runtime. The typed
descriptors cost a little more code in the BSP and catch mistakes at compile
time, in a codebase where a wrong radio assumption is a regulatory problem.

**Runtime probing instead of a declared descriptor.** Rejected as the primary
mechanism: an I2C address that answers does not prove which chip answered, and
probing costs power and time at boot. Probing is still valuable as a
*verification* step in diagnostics — declared and detected should be compared,
and a mismatch is a finding worth surfacing.

## Consequences

**Easier.** One binary serves several board variants. Applications express
"I need rotation" rather than "I am on the Waveshare board". Adding a
magnetometer later changes an answer, not an interface. Absence becomes
something the UI can state plainly instead of a failure it must hide.

**Harder.** The BSP carries more: a descriptor per capability, per board, per
variant. That is deliberate — it is the layer that is *allowed* to know these
things, and concentrating the knowledge is the point.

**Committed to.** Every new peripheral needs a capability entry and a
descriptor before an application can use it. Every service must handle all four
availability states, including `Absent`, without crashing or lying. Diagnostics
must be able to show declared-versus-detected, because a descriptor that
disagrees with the hardware is worse than no descriptor.

**Open.** ~~Whether `traits` should be extensible at runtime for external
sensors over the expansion connector — deferred until such a sensor exists.~~

**Closed, sooner than expected.** That question arrived within the day, and not
over the expansion connector: a Firefly node provides LoRa and GNSS over a link.
Deferring it "until such a sensor exists" was the wrong instinct — the cost of
runtime-extensible capabilities is almost entirely in the *contracts* around
them (who may register, what happens to a running app when one vanishes), and
those are cheap to decide with no code and expensive to retrofit into a system
that assumed a fixed set. Resolved in [ADR-0004](0004-capability-sources.md).
