# 0001 — Capability model: presence, variant, degree, availability

Status: **superseded by [ADR-0007](0007-two-capability-layers.md)**
Date: 2026-08-21

> **Superseded, later the same day.** Everything under **Decision** below has
> been replaced. Read this ADR for its **Context** — the board survey that
> forced a capability model at all — and for its **Alternatives**, all four of
> which are still rejected for the reasons given. Do not implement anything
> under Decision.
>
> Two rounds of correction landed on it, which is one more than a document this
> size can carry legibly, and final §67 warns about exactly that: documentation
> that preserves mutually incompatible current truths. So rather than a third
> layer of inline notices:
>
> | What it decided | Now |
> |---|---|
> | `has(Capability::X)` for cheap UI gating | **gone.** `supports()` / `is_available()` / `availability()` — [ADR-0007](0007-two-capability-layers.md) §3 |
> | one capability set, listing parts | **two layers** — hardware inventory and product capability — ADR-0007 §1–§2 |
> | `LoraInfo{ LoraChip, RadioBand, max_tx_dbm }` | `RadioInfo`, and the part is a **`Radio`** — [ADR-0003](0003-radio-not-lora.md) |
> | four-state `Availability` | seven states — [ADR-0004](0004-capability-sources.md) |
> | descriptors produced by the BSP alone | the BSP is one contributor to a registry — ADR-0004 |
>
> What survived, and is why this is a supersession rather than a deletion: the
> three-questions framing (*is it there · which one · can I use it right now*),
> the per-sensing-axis enumeration, and the rule that no two availability states
> may render identically. All three are carried into ADR-0007 by name.
>
> The amendment notice from the first round follows, kept because it records
> what the Attadipa node broke and when.

> **Amendment notice (first round, 2026-08-21).** Two claims in this ADR were stated more broadly than the
> evidence supported, and a product decision the same day
> ([OWNER_DECISIONS OD-1](../research/OWNER_DECISIONS.md)) made both of them
> false:
>
> - *"The descriptors are produced by the BSP. Nothing above the platform layer
>   constructs them."* A capability may now be provided by a separate Attadipa
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
>
> **Precisely what is superseded**, so that nothing below is copied by mistake:
>
> | In this ADR | Superseded because |
> |---|---|
> | "two headline features must be *absent* rather than merely unavailable" (Context) | on a node-equipped Waveshare they are neither — they are present and remote |
> | "a typed, optional descriptor, defined by the BSP" (Decision) | a provider registered at runtime also defines descriptors |
> | `Absent, // not on this board — the feature does not exist here` | conflates *board* with *device*, and absence is no longer permanent |
> | "the day one is added externally, the answer changes … and nothing above the BSP needs rewriting" | the answer changes repeatedly, in both directions, and not through the BSP |
> | "probing costs power and time at boot" (Alternatives) | for a remote provider, runtime discovery is the only mechanism, and it does not happen at boot |
> | "the BSP carries more … concentrating the knowledge is the point" (Consequences) | the concentration point is now the capability registry, of which the BSP is one contributor |
> | "Every service must handle all four availability states" (Consequences) | there are seven |
>
> Everything else in this ADR stands. In particular the four rejected
> alternatives are still rejected, and for the same reasons.

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
  they can talk to another Attadipa at all. `has(Lora) == true` is the same
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
// SUPERSEDED — has() no longer exists. See ADR-0007 §3.
if (!caps.has(Capability::Gnss)) { /* the app is never offered */ }
```

**Variant and degree** come from a typed, optional descriptor, defined by the
BSP and consumed by drivers and services:

```cpp
// SUPERSEDED — the part is a Radio, not a LoRa. See ADR-0003 for RadioInfo.
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
// SUPERSEDED — do not implement this enum. See ADR-0004 for the one in force.
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
> "This watch has no compass", "Maps needs an Attadipa node" and "your node is out
> of range" are three sentences, and `Absent` was carrying all three.
> [ADR-0004](0004-capability-sources.md) splits it.

Capabilities are enumerated per sensing axis rather than per part:
`Accelerometer` and `Gyroscope` are separate entries, and `Magnetometer`
exists in the enum even though neither board has one — so that adding one later
changes an answer, not an interface.

The descriptors are produced by the BSP. Nothing above the platform layer
constructs them, and nothing above it may branch on board identity.

> **Amended.** The second clause holds; the first does not. A descriptor may
> also be produced by an attached provider — an Attadipa node — and registered at
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
mechanism *for parts on the board*: an I2C address that answers does not prove
which chip answered, and probing costs power and time at boot. **This rejection
does not extend to a remote provider** — for a node, runtime discovery is the
only mechanism there is, it does not happen at boot, and it happens again every
time the node comes and goes. What survives is the principle behind the
rejection: a declared descriptor beats an inferred one, so a node *declares*
what it provides in its capability exchange rather than being probed for it. Probing is still valuable as a
*verification* step in diagnostics — declared and detected should be compared,
and a mismatch is a finding worth surfacing.

## Consequences

**Easier.** One binary serves several board variants. Applications express
"I need rotation" rather than "I am on the Waveshare board". Adding a
magnetometer later changes an answer, not an interface. Absence becomes
something the UI can state plainly instead of a failure it must hide.

**Harder.** The BSP carries more: a descriptor per capability, per board, per
variant. That is deliberate — it is a layer that is *allowed* to know these
things, and concentrating the knowledge is the point. *(Amended: the
concentration point is the capability registry. The BSP is its largest
contributor, not its only one — a provider registered at runtime contributes
too. The principle is unchanged; the claim that the BSP is the sole source is
not.)*

**Committed to.** Every new peripheral needs a capability entry and a
descriptor before an application can use it. Every service must handle every
availability state without crashing or lying — *four when this was written,
seven under [ADR-0004](0004-capability-sources.md)*. Diagnostics
must be able to show declared-versus-detected, because a descriptor that
disagrees with the hardware is worse than no descriptor.

**Open.** ~~Whether `traits` should be extensible at runtime for external
sensors over the expansion connector — deferred until such a sensor exists.~~

**Closed, sooner than expected.** That question arrived within the day, and not
over the expansion connector: an Attadipa node provides LoRa and GNSS over a link.
Deferring it "until such a sensor exists" was the wrong instinct — the cost of
runtime-extensible capabilities is almost entirely in the *contracts* around
them (who may register, what happens to a running app when one vanishes), and
those are cheap to decide with no code and expensive to retrofit into a system
that assumed a fixed set. Resolved in [ADR-0004](0004-capability-sources.md).
