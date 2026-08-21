# 0004 — Where a capability comes from, and what happens when it leaves

Status: **accepted** (2026-08-21)
Date: 2026-08-21

<!-- Accepted per final §74. Final §8 endorses the seven-state model by name and
     adds one requirement it already met: that the transition model be
     centralized and tested (§2a). Extended, not replaced, by ADR-0007. -->
Amends: [ADR-0001](0001-capability-model.md) · scopes [ADR-0002](0002-companion-is-optional.md)

## Context

A separate **Attadipa node** carrying LoRa, GNSS and an ESP32 exists in the
product plan. A watch attached to one runs the same applications a watch with
its own LoRa runs; with no node attached it is a watch, an audio device, and
whatever the installed applications make it
([OWNER_DECISIONS OD-1](../research/OWNER_DECISIONS.md)).

This is not a new direction. §32 requires the architecture to account for a
Attadipa/Doctor node and lists "additional GNSS" among what it may provide. §74
item 23 — in the list of product requirements §73 forbids changing without
explicit grounds — says the node must be integrable **without rewriting the
system**. And §1 says *nodes*, plural: the design must not assume there is one.

What is new is that the requirement now has a shape. Three things ADR-0001 said
are false as written:

1. *"The descriptors are produced by the BSP."* A node is not known to the BSP
   at build time.
2. *`Absent` — "not on this board; the feature does not exist here."* Absence is
   no longer permanent.
3. The model was implicitly boot-time-static. A capability can now appear and
   disappear while an application is open.

The third is the expensive one. It touches every application that consumes a
node capability, there are currently zero applications, and it is exactly the
kind of coupling that accumulates silently — which is the failure mode
[ADR-0002](0002-companion-is-optional.md) was written to prevent, on a different
axis.

### Why the existing enum cannot be stretched

An on-board capability has two layers between silicon and screen:

```
L2  provider ready    rail up, driver initialised, part answering
L3  datum valid       fix acquired, with an age and an accuracy
```

`Availability` was designed as a name for **L2**. A node-provided capability has
five:

```
L0  bound             is a node bound to this watch at all
L1  session live      transport up, identity confirmed, version agreed,
                      capability set exchanged
L2  provider ready    the node's own GNSS: absent | failed | off | ready
L3  datum valid       at the node
L3ʹ datum fresh       at us — the age of the last thing that crossed the link
```

L0, L1 and L3ʹ have no slot in the existing enum. Every state a naive model
collapses is an adjacent-layer conflation, and they are derivable rather than
remembered:

- **L1 vs L2** — "the node is connected" is not "the node's GNSS is switched on".
- **L2 vs L3** — "its GNSS is on" is not "its GNSS has a fix". This pair already
  exists on-board, and the mistake is assuming the remote case adds nothing else.
- **L1 vs L3** — "connected" is not "we have a position". A two-layer skip, which
  is why it is the one that actually gets shipped.
- **L3 vs L3ʹ** — the sharpest, and it has **no on-board analogue at all.** The
  node holds a fix that is two seconds old. We last heard from the node ninety
  seconds ago. One number now has two ages and they disagree. Report the fix's
  own timestamp and a navigation screen draws a confident arrow to a position
  the user has since walked away from.

The reference implementation already fails the weaker version of this: the
MeshCore data model in
[OD-2](../research/OWNER_DECISIONS.md) carries latitude and longitude with **no
timestamp at all**.

## Decision

### 1. One state per remedy, not one state per sentence

The rule that keeps the state count finite:

> **If the user's remedy differs, it is a distinct state. If only the wording
> differs, it is one state with a different reason.**

Gating is driven by states. Text and diagnostics are driven by reasons. The
state set stays small enough to render exhaustively; the reason vocabulary stays
free to grow without touching a switch.

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
```

Seven states, seven remedies: *nothing* · *get a node* · *go to your node* ·
*update something* · *service it* · *switch it on* · *none needed*.

`Absent` is gone, and deliberately. It was carrying three different sentences —
"this watch has no compass", "Maps needs an Attadipa node" and "your node is out
of range" — and only the first of them is permanent. `Unsupported` says what
`Absent` was meant to say and cannot be misread as the other two.

**`Incompatible` is the one place this ADR bends its own rule**, and it is worth
naming rather than hiding. *Their firmware is too old* and *ours is too old* have
opposite remedies, so by the discriminator they should be two states. They are
one state with a **mandatory** direction field:

```cpp
enum class VersionSkew : uint8_t { ProviderTooOld, ConsumerTooOld };
```

Mandatory, not optional — `Incompatible` must not be renderable without it.
Splitting the top-level enum on a protocol detail would leak the protocol into
every UI switch; making the direction optional would produce "version mismatch",
which is a sentence that tells the user nothing they can act on.

### 2. The provider is an axis, not more enum values

```cpp
enum class Origin : uint8_t { Local, Node };

struct ProviderRef {
    Origin      origin;
    ProviderId  id;      // meaningless when origin == Local
};
```

Argued from call sites rather than from taste. The places in this firmware that
consult capability state, and what each actually needs:

| Call site | Needs `Availability` | Needs `Origin` |
|---|---|---|
| Launcher — is this application offered | yes | no |
| Application asking a service for data | no — it asks the service | no |
| Service dispatching to a driver | yes | **yes** — this *is* the dispatch |
| Rail / power service | yes | **yes** — a remote part has no rail here |
| Coexistence coordinator | yes | **yes** — a remote radio is not on our antenna |
| Settings screen | yes | **yes** — it configures a specific device |
| Diagnostics | yes | **yes** | 
| Offering rule for an installed application | yes | no |
| Simulator harness | yes | **yes** |

Five of nine need the origin, and **every one of them lives in `core/` or
`platform/`. None is in `apps/`.** That is the whole argument: folding origin
into `Availability` would multiply five values into every switch in the system
to serve five call sites that already sit below the abstraction. As an axis, the
callers who do not care stay ignorant — which is what the capability layer is
for.

**Invariant:** `Unprovisioned`, `Unreachable` and `Incompatible` imply a remote
provider. A local capability can never be in them. This is checkable in a test.

### 2a. The enum is worthless without a transition table

Two findings from Meshtastic's history, read from its commits rather than
inferred, and together they are the strongest evidence for everything above.

**They shipped GPS as a two-state boolean and had to retrofit a third.** Adding
`NOT_PRESENT` was PR #3157, merged as `7f7c5cbd629e5188939926fd7c0a64280405df6f`
on 2024-02-01, and it touched the firmware broadly — including screen text.
Widening an availability enum later is not a header change. It is the argument
for putting all seven states in on the first day, when there are no
applications and the cost is a paragraph.

**Adding the state did not stop code from leaving it.** Two years and one month
later, commit `4a534f02a48626f2addf742dced2f9e8321d5e16` (2026-03-19) is
*"fix(gps): prevent GPS re-enablement in NOT_PRESENT mode"* — a hardware switch
could still drag a device out of the state that means *this device does not have
one*.

So the enum is only half the decision. The other half:

```
Unsupported     terminal. Nothing may leave it. Ever.
Unprovisioned   -> Unreachable | Incompatible | Failed | Off | Ready   (a bind)
Unreachable     -> Ready | Off | Failed | Incompatible | Unprovisioned (unbind)
Incompatible    -> Unreachable | Unprovisioned                          (never Ready)
Failed          -> Off | Ready | Unreachable | Unprovisioned
Off             -> Ready | Failed | Unreachable | Unprovisioned
Ready           -> anything except Unsupported
```

The table lives in one place, every transition goes through it, and an illegal
one is a test failure rather than a screen that says the wrong thing. In
particular `Incompatible` never reaches `Ready` without a renegotiation, and
nothing ever reaches `Unsupported` — a device that never had a magnetometer does
not acquire one because a node said so, it acquires a *provider*, and that is a
different edge.

### 3. Availability is not validity — and a remote datum has two ages

`Ready` means the source can be asked. It says nothing about whether it has an
answer. That distinction already exists for an on-board GNSS with no fix; the
node does not create it, it makes ignoring it fatal.

Every datum that crosses the link carries **both** ages:

```cpp
struct Timed<T> {
    T        value;
    uint32_t age_at_source_ms;   // how old it was when the provider sampled it
    uint32_t age_at_us_ms;       // how long ago it reached this device
    Validity validity;           // Valid | Stale | Invalid | Unknown
};
```

**The UI shows the larger of the two ages.** On a navigation screen this is a
safety property, not a diagnostics nicety.

Absent values are three-valued, never two. `Node count: Unknown` in the
reference model is not an artefact — *known* · *known to be none* · *not known*
are three different facts, and rendering the third as the second is a lie the
interface tells confidently. This is the same discipline as `Unsupported` versus
`Failed` here, and `MEASURED` versus `ESTIMATED` versus `UNKNOWN` in
[`CLAUDE.md`](../../CLAUDE.md).

### 4. Capabilities and data feeds are different things

§32's list mixes two kinds of thing: **capabilities** — mesh connectivity,
additional GNSS — and **data feeds** — weather, Home Assistant events, quest
events, object coordinates, telemetry. They must not share a model.

| | Capability | Data feed |
|---|---|---|
| Has an `Availability` state | yes | no |
| Gates whether an application is offered | yes | no |
| Has a driver-shaped contract | yes | no |
| Has staleness and a source label | yes, on its data | **this is all it has** |
| Absence means | the feature cannot run | there is nothing to show yet |

A `Capability::Weather` would be a category error. It produces a screen that
cannot tell "no weather source configured" from "the weather is four hours old",
which are not the same problem and do not have the same fix.

The test for which side something falls on: **can an application be written that
is useless without it?** Navigation is useless without a position source — that
is a capability. A weather widget with no weather is an empty widget — that is a
feed.

### 5. A capability change is an event, not a seventh lifecycle verb

§33 defines create · open · pause · resume · close · event, and says the names
are not binding but the concepts are. A capability appearing or disappearing
arrives through `event`:

```cpp
struct CapabilityChanged {
    Capability   capability;
    Availability from;
    Availability to;
    ProviderRef  provider;
};
```

Adding a seventh verb would mean every application implements a handler for
something most of them do not care about. An event reaches the ones that
subscribed.

An application declares what it needs, and the framework — not the application —
enforces it:

```cpp
struct AppManifest {
    std::span<const Capability> required;    // cannot run without these
    std::span<const Capability> enhanced_by; // better with, fine without
};
```

Three consequences, all of which must hold before the first application is
written:

- **`required` going non-`Ready` while the application is open** drives it to a
  framework-owned degraded screen that states the remedy. It does not crash, and
  it does not silently show stale data.
- **An application installed when a capability existed and opened when it does
  not** is offered with its remedy, or not offered at all, according to §6.
- **No application queries node state.** [ADR-0002](0002-companion-is-optional.md)
  rule 2 extends here unchanged: an application asks `LocationService` for a
  position and never learns where it came from. This is the rule that makes
  everything else checkable by review.
- **The manifest is compulsory, and omitting a requirement is a compile error.**
  InfiniTime's navigation app carried its availability as a plain `bool` copied
  at one call site; an unrelated refactor silently disabled it and it stayed
  broken for **nineteen months** (fixed by `9afc23cba9bcf938d8c49d6e15e7662ee8e6385d`,
  2025-05-24). A requirement that can be forgotten will be forgotten, and it will
  not announce itself when it is.
- **A provider is a trust boundary, not a peripheral.** InfiniTime's
  `MusicService` sized a variable-length array from the length of a peer's GATT
  write (issue #825). Every length, count and index arriving from a node is
  bounded at the link edge, before it reaches the capability layer.

### 6. When to offer an application you cannot currently run

`Unsupported` → **not offered.** No icon that promises what the device can never
do. This is unchanged from ADR-0001.

`Unprovisioned` · `Unreachable` · `Incompatible` · `Off` · `Failed` → **offered,
with the remedy stated.** An application that vanishes from the launcher when
the node walks out of range is worse than one that says "your node is out of
range" — the user learns the device is unreliable rather than that the node is
away.

The distinction is not "on the board or not". It is **whether the user has an
action available.** That is why `Unsupported` had to be separated out.

## Alternatives considered

**Keep four states and add `Remote` as a fifth.** Rejected: it conflates the
"where" axis with the "can I use it" axis, so `Remote` would have to mean both
"provided by a node" and "provided by a node *and currently fine*", and the
states that matter — bound-but-unreachable, version-skewed — still have nowhere
to live.

**Model the node as a `NodeService` that applications ask directly.** Rejected,
and it is the tempting one because it is the least work today. It puts device
identity back into application code by another name, violates §32's separation,
and means a node-provided GNSS and an on-board GNSS reach applications by two
different routes — so every navigation application is written twice. It also
breaks §74 item 23 outright: integrating a node would then require rewriting
every consumer.

**Treat the node exactly like the phone companion, under ADR-0002.** Rejected:
ADR-0002 forbids an external device from *providing* a capability, which would
forbid the product. That rule is correct about a general-purpose phone the
project does not control and whose failures it cannot observe. A dedicated node
is a different relationship, and the owner has explicitly accepted losing whole
applications when it is away. ADR-0002 has been scope-corrected rather than
overruled.

**Boot-time-only capability discovery, with a reboot on node attach.** Rejected.
It is genuinely simpler and it is how a lot of embedded systems handle this. It
also means walking out of range reboots the watch, or that attaching a node does
nothing until the user restarts — and on a device whose headline feature is an
emergency beacon, a reboot triggered by radio range is not acceptable.

**One flat "usable / not usable" boolean plus a free-text reason.** Rejected: it
type-checks nothing, and the reason string becomes the API. Every consumer ends
up matching on prose, which is how "not offered" and "offered with a remedy"
stop being distinguishable.

## Consequences

**Easier.** Two boards that share almost no hardware run the same applications,
because the difference is a capability answer rather than a build. A node
appearing is an ordinary state change, not a special case. The Waveshare board
stops being a lesser device with a purpose to be found for it. Absence stays
something the interface can state plainly.

**Harder.** Seven states instead of four, each needing a designed screen state in
both themes and both geometries, and `Incompatible` needing two. Every datum
carries two ages, which is real overhead on a constrained link and in memory.
The framework owns a degraded screen it did not previously need.

**Committed to.** Every service handles all seven states without crashing or
lying. Every application declares a manifest. Every state is reachable in the
simulator without a rebuild — including the ones a real node makes hard to
produce on demand, which is most of them
([TASKS](../../TASKS.md) T-022). Application code that references node state is a
review failure, mechanically checkable.

**Open.** The transport is undecided ([NODE_PROFILE](../node/NODE_PROFILE.md) N2)
and nothing here depends on it. Whether two LoRa radios on one mesh — a T-Watch's
own plus a node's — can both be live without duplicating packets is a MeshCore
question (OPEN_QUESTIONS M-series, T-006). How many watches one node serves is
N5, and it decides whether "queued behind another device" is a sentence this
system ever has to say.
