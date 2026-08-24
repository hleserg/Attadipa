# 0013 — The node's IMU is a power decision, and motion belongs to a body

Status: **accepted**
Date: 2026-08-22

Extends [ADR-0004](0004-capability-sources.md) §4 and [ADR-0007](0007-two-capability-layers.md) §2 ·
carries [ADR-0009](0009-heading.md) §3's rule into a second subsystem ·
constrains [ADR-0011](0011-gnss-integrity.md) §6

## Context

[OD-17](../research/OWNER_DECISIONS.md)
gave the Attadipa node a **6-axis IMU — accelerometer and gyroscope — for GNSS
optimisation**, and explicitly no magnetometer. It also said what that part does
not buy: no absolute heading, no standalone position. What it buys is knowing
the node is still, so the node's own receiver can sleep, and knowing the node
moved.

The same decision said modelling it was a separate question and refused to
settle it inside [#56](https://github.com/hleserg/Attadipa/issues/56). This ADR
is that question, filed as [#93](https://github.com/hleserg/Attadipa/issues/93)
and **T-111**, and it has three parts: is the node IMU a capability at all; if
it is surfaced, what is its availability and its walk-away state; and how does
it interact with [OD-10](../research/OWNER_DECISIONS.md#od-10--a-standing-person-does-not-need-a-new-fix)'s
standing-still gate when the node is the one doing the standing.

### What made this urgent rather than tidy

The issue frames all three as design questions about hardware that does not
exist. Two of them are not. This repository already holds **two motion inputs,
and neither of them names a body**:

| Where | Shape | What it cannot say |
|---|---|---|
| [`MotionEvidence`](../../core/include/attadipa/core/trust.h) | `{known, moving}` | *whose* motion. It is correct that "nobody asked" and "at rest" are different facts, and silent on which object either is about |
| [`GnssContext::device_moving`](../../core/include/attadipa/core/gnss_power.h) | `bool` | both. "Nobody asked" and "at rest" share one value — and the value they share is `false`, which is the one that powers a receiver down |

Both were written when there was one body to be about. There is not: the
Waveshare board's `Capability::Position` comes from a node or from nowhere
([ADR-0004](0004-capability-sources.md)), and `PositionSource::NodeGnss` is
already a value the trust engine can be handed today.

**The failure is reachable in `main` as it stands.** `TrustEvaluator::observe()`
takes a `MotionEvidence` and a `GnssObservation`, and fires
`MotionDisagreement` — weight 45, which is past `degrade_at` on its own — when
the motion says still and the position moved more than
`jump_while_still_mm`. Nothing in that path checks that the accelerometer and
the receiver are attached to the same object. So:

> The wearer is at a desk. The watch's accelerometer says still, correctly. The
> node is in a friend's bag, sixty metres down the corridor and moving. Its
> receiver reports exactly that, correctly. The watch reads a correct position
> against a correct accelerometer and concludes something is lying.

And the mirror image, which is the one that matters more because it fails
quietly: the wearer is walking, the node is on a table being spoofed, and the
detector is switched off by the wrist's motion for the whole event.

Neither is a node-hardware question. Both are answered by saying which body a
motion sample is about, and there is no reason to wait for a part to say it.

## Decision

### 1. The node's IMU is not a capability, and specifically not `MotionSensing`

[ADR-0004](0004-capability-sources.md) §4's test is *can an application be
written that is useless without it?* The node IMU's consumer, as OD-16 decided
it, is the node's own GNSS duty cycle. That is a decision made on the node,
about the node, and in the common case it never crosses the link at all. No
application is useless without it because no application asks for it.

So there is **no new `Capability` entry**, and three candidate homes were
available. Two are refused by name, because both are the obvious thing to do:

- **`Capability::MotionSensing` must never be served by a node. Invariant.**
  That entry means *the wearer's* steps, wrist gestures and activity
  ([ADR-0007](0007-two-capability-layers.md) §2). A node satisfying it would
  hand a pedometer the movements of a bag, and it would do so through an API
  designed so the application cannot tell. This is exactly the shape of the
  `has()` bug ADR-0007 buried, and of the `NodeBody`-as-`WatchBody` bug
  [ADR-0009](0009-heading.md) §3 refused: an answer about one object presented
  as an answer about another. `kNodeProvidable` in
  [`capability_registry.cpp`](../../core/src/capability_registry.cpp) already
  excludes it; this ADR makes that a decision with a test behind it rather than
  an omission.
- **A new `Capability::NodeMotion` is refused.** It names a device in the one
  enum whose entire purpose is that applications never name devices. It would
  also be the first capability with no application that could require it, which
  is ADR-0004 §4's definition of the wrong side of the line.

What is accepted instead is the arrangement
[ADR-0011](0011-gnss-integrity.md) §3 already established for the GNSS
receiver's own defences, and the argument transfers without modification:

- it is a **`HardwareFeature` in the node's own inventory** —
  `Accelerometer` and `Gyroscope` already exist in that enum, and ADR-0007 §1
  already says the inventory is contributed by the BSP *and, for a node's own
  inventory, by the provider that speaks for it*. Nothing new is needed;
- it is an **input to the Position provider's power model**, which is where
  `GnssContext` already sits;
- it is therefore **below `LocationService`**, and `LocationService` is the
  last layer that may read it;
- **Diagnostics and Settings may show it**, because inspecting the machine is
  what they are for (ADR-0004 §2, final §9). "node: still / moving / not
  known" is a diagnostic line, not a capability. `GnssStatus` carries the seat
  — a body and a `MotionEvidence`, defaulting to *nobody asked* rather than to
  *still* — before anything fills it, because a field nobody can see is a field
  nobody notices is missing. `CLAUDE.md`'s rule that every part gets a seat in
  the core, applied to a part that is not on the board.

The case for surfacing it anyway — diagnostics wanting to show it — is
therefore satisfied without a capability, which is what makes the trade the
issue asked about decidable rather than a preference.

### 2. It has no `Availability` of its own — and stillness expires while motion does not

`Availability` is a property of a capability (ADR-0004 §1): seven states, seven
*remedies*, and "your node's accelerometer is not answering" is not a sentence
with a user action attached. Not a capability, therefore no `Availability`. What
the datum carries instead:

- **the node's `Capability::Position` availability, unchanged.** ADR-0004's L0
  and L1 — bound, session live — are already covered there, and a second
  state machine tracking the same link would be a second thing to keep in step
  with reality;
- **three values, not two.** `known` · `moving`, the three-valued discipline of
  ADR-0004 §3 and of `MEASURED` / `ESTIMATED` / `UNKNOWN` in
  [`CLAUDE.md`](../../CLAUDE.md). `MotionEvidence` already had this and it is
  carried forward unchanged;
- **the body it was measured on**, which is new and is §3;
- **two ages when it crosses the link** (ADR-0004 §3), with the larger shown.

**The walk-away rule, and the asymmetry that is the answer.** The issue asks
whether a stale "still" silently becomes untrustworthy. It must, and the reason
is that the two directions do not fail alike:

| The evidence expires saying | Consequence of believing it anyway |
|---|---|
| **moving** | a receiver stays awake for nothing. Costs charge, tells no lies |
| **still** | a receiver stays asleep on a fact that expired. The device stops asking where it is and nothing says so |

A stale *moving* fails safe. A stale *still* is the one state that is both
permissive and silent, so it is the one that must not survive its own age.

> **Not known is not still, and unknown moves nothing.**
>
> An expired stillness reverts to *not known*. Positive, same-body,
> unexpired evidence is required to enter a low-power state **and** to leave
> one; motion that is unknown causes no transition in either direction.

**Where the expiring happens, and where it does not.** `MotionEvidence` reads no
clock and therefore cannot expire anything: `known` means *somebody currently
asserts this*, and clearing it when a sample goes stale is the producer's
obligation. That obligation is real and **is not discharged anywhere today** —
no motion service exists, and T-080 is where one arrives. It is written into the
headers rather than left implicit, because a rule that lives only in an ADR is a
rule the next implementer of a motion source will not know they owe.

Refusing to act on unknown in *both* directions is deliberate, and it is not the
same as failing safe by waking. Treating unknown as *moving* would spin a
receiver up for a device that simply has not sampled its IMU yet, which is the
ordinary condition at boot and on every board that has no such sample to give.
Inert is the honest reading of "nobody knows", and the guarantee that a fix
eventually happens anyway is not this gate's job — it is OD-10 obligation 1's
ceiling, below.

**When the node walks away, the evidence is discarded, not held.** ADR-0004 §3:
no state survives implicitly. The node's motion evidence is reset with the rest
of the provider's state, and reverts to *not known* — which, by the rule above,
gates nothing.

**The ceiling is real and it is not in this gate.** OD-10 obligation 1 requires
*a longest interval after which the receiver is asked again regardless*, and a
setting rather than a constant ([ADR-0006](0006-settings-and-bounded-values.md)).
`next_state()` is pure and reads no clock — that is what makes it testable
without a receiver — so the ceiling cannot live inside it and is not being
smuggled in. It belongs to the location service that owns the tick and calls it,
and it is **T-080**, unimplemented. Stated here so that nobody reads the motion
gate as if the ceiling were already behind it.

### 3. Motion belongs to a body, and two bodies are not one

OD-17 states the general rule the A5 and A6 answers share, and
[ADR-0009](0009-heading.md) §3a records it:

> *A sensor may correct another reading taken on the same body, and may not be
> presented as a reading from a different one.*

ADR-0009 §3 is that rule's far side: a node's heading is never the watch's
heading, and no transform exists. §3a names the near side — the node's IMU
correcting the node's own GNSS — and hands the rest here. Motion is that near
side in full, and this ADR fixes exactly where the line falls, because "position,
unlike heading, composes correctly within one body" is true and is routinely
misread as "so it composes".

| | within one body | across two bodies |
|---|---|---|
| IMU correcting or gating that body's own GNSS | **yes** — no transform needed, both are bolted to the same object | — |
| IMU of body A gating body B's receiver | — | **no** |
| IMU of body A judging body B's position | — | **no** |
| heading | yes | **no**, and no transform exists ([ADR-0009](0009-heading.md) §3) |

There is no exception in that table. The node's IMU improving the node's own
GNSS is the first row, which is why OD-16 could say it "needs no transform"; it
is not a licence for the second or the third.

**Three wrong compositions, named so they are not re-proposed:**

1. **the watch's stillness gating the node's receiver.** It sleeps a receiver
   inside somebody else's bag because this wrist is resting;
2. **the watch's stillness judging a node's position** — the false
   `MotionDisagreement` above, live in `main` today;
3. **the node's motion suppressing the detector on a local fix** — the missed
   detection, the same defect facing the other way.

**"Node still" does not imply "wearer still", in either direction.** OD-10 is
about the *wearer*, inferred from the watch's own IMU (T-080). OD-16 is about
the *node*. All four combinations are ordinary, none is a contradiction, and
nothing may collapse them:

| watch | node | an ordinary way to be there |
|---|---|---|
| still | still | sitting at a desk with the node on it |
| still | moving | reading the watch on a train with the node in the luggage rack; handing the node to somebody |
| moving | still | walking around a camp with the node left at the tent as a relay |
| moving | moving | walking with the node in a pocket — the case everybody pictures, and one of four |

**The rule, as code has to state it.** A motion sample carries the body it was
measured on. A consumer names the body it is asking about. Evidence about a
different body — or about no body — **is not evidence**, and is treated exactly
as *not known*: refused rather than approximated. That is ADR-0009 §3's
discipline, one subsystem over.

```cpp
enum class SensorBody : std::uint8_t { Unknown, Watch, Node, Companion };

struct MotionEvidence {
    SensorBody body   = SensorBody::Unknown;   // first, and that is enforcement
    bool       known  = false;
    bool       moving = false;

    // Evidence about `body` is evidence about nothing else.
    bool speaks_for(SensorBody about) const;
};
```

**The body is the first member on purpose.** `MotionEvidence{true, false}` — the
two-field literal this type used to have, meaning *known, still, about nobody* —
now fails to compile, because `bool` does not convert to a scoped enum. Every
literal in the tree had to name a subject to keep building, and none can come
back by accident. That is ADR-0007's rule for `has()` applied to a struct rather
than a function: do not leave behind a shape that survives the change while
quietly meaning something else.

`SensorBody::Unknown` is a fourth state and not a default worth defaulting to:
a sample that claims to know something must know whose. The two consumers name
their subject rather than assume it —
`GnssContext::receiver_body` for the power gate, and, for the trust detector,
the body implied by `GnssObservation::source`, which the observation is already
required to carry honestly.

## What this does not decide

Named, because an ADR that quietly settles adjacent questions is worse than one
that leaves them open:

- **which IMU part the node carries.** OD-16 leaves it open and says the shape
  of the model does not depend on it. It does not.
- **whether node motion crosses the link at all, and in what encoding.** The
  common case is that it does not — the decision it feeds is made on the node.
  The transport is [ADR-0005](0005-node-protocol.md) and N2, still provisional,
  and nothing here depends on it.
- **whether the node computes its own trust verdict or ships evidence.**
  ADR-0011 §5 says trust is a property of the observation and belongs where the
  observation is made, which points at the node; the reason codes then have to
  cross the link, and that is an ADR-0005 question.
- **`ProviderDisagreement` across two bodies.** ADR-0011 §6 compares the
  on-board receiver against a node's position and calls a gap evidence. Two
  devices that are genuinely 300 m apart disagree by 300 m and are both right,
  and `provider_disagreement_mm` is 250 m. This ADR **exposes that and does not
  fix it** — the remedy is a different one (a co-location precondition, not a
  body label), it needs its own argument, and pretending otherwise would be
  scope creep in a document about motion. Filed as **T-132**.
- **dead reckoning.** ADR-0011 §8 stands in full. A motion flag that gates a
  receiver is not an inertial navigation system and nothing here may quietly
  become one.

## Alternatives considered

**Give the node IMU `Capability::MotionSensing`.** Rejected, and it is the
tempting one because the enum entry already exists and the node genuinely
senses motion. It would deliver a bag's movement to a pedometer through an
interface built so the application cannot tell — the same error in the same
shape as `NodeBody` heading on the navigator's arrow.

**Give it a new `Capability::NodeMotion`.** Rejected: it names a device in the
enum that exists so applications never name devices, and no application can
require it, which puts it on the feed side of ADR-0004 §4 by that section's own
test.

**Give it an `Availability` of its own, with the five node layers.** Rejected.
`Availability` exists to answer *may this application be offered, and what do I
tell the user*. There is no application and there is no remedy — "your node's
accelerometer is quiet" is a diagnostic line. The five layers are already
tracked once, for `Capability::Position`, and a second copy is a second thing to
keep true.

**Keep `device_moving` a `bool` and document that it means the local body.**
Rejected, and the precedent is exact: `has()` was documented — *"cheap, for
gating UI"* — and the documentation was already false the day it was written
(ADR-0007). A comment cannot hold a definition the type contradicts, and this
field is read by a code path that will be handed a node's context on the day a
node exists.

**Default an unlabelled motion sample to the watch.** Rejected. That is the
assumption, spelled as a default, and it is the assumption that produces every
one of the three wrong compositions in §3. It is also precisely how ADR-0009's
`ReferenceFrame` would have been got wrong if the frame had had a default.

**Treat unknown motion as "moving", so unknown fails safe by waking.**
Rejected — see §2. It makes a receiver spin up for a device that has simply not
sampled yet, which is the state every board is in at boot and the permanent
state of any provider with no IMU. Unknown is inert; the ceiling is what
guarantees a fix.

**Wait for node hardware before deciding any of this.** Rejected twice over.
OD-16 says the shape does not depend on the part, and one of the two defects
this ADR fixes is in `main` now, on a code path a Waveshare board reaches with
the only position source it has.

## Consequences

**Easier.** The node's IMU needs no new subsystem, no enum entry and no state
machine: it is a `HardwareFeature` in the node's own inventory and a field in a
context struct that already exists. A body-labelled motion sample makes the
right question ask itself at every consumer, so the next person to add a motion
consumer has to state a subject to compile.

**Harder.** Every producer of motion evidence must now say whose it is, and
every consumer must name the body it is asking about. The honest cost is that
"not known" is now common and gates nothing, so a receiver that would have
duty-cycled on an unlabelled `false` now stays up until somebody supplies real
evidence — thrift traded for not lying, in that direction on purpose.

**Committed to.** `Capability::MotionSensing` is never served by a node, and a
test says so rather than an omission implying it. `TrustEvaluator`'s
motion-disagreement detector is inert whenever the IMU and the receiver are not
demonstrably the same object. The OD-10 ceiling is owed by T-080 and is not
pretended to exist here.

**Testable, and tested.** In the replay rig: a node position walking away from a
still wrist raises no `MotionDisagreement`, and the same walk against the
*node's* own stillness does. In `tests/test_power.cpp`: unknown motion moves the
receiver in neither direction, and a watch's stillness does not sleep a node's
receiver. On hardware: `NOT EXECUTED — HARDWARE REQUIRED`, and there is no node
hardware to execute it on.

**Open.** T-132, provider disagreement between two bodies. T-080, the OD-10
ceiling and the wearer-side gate. Which IMU part the node carries (OD-16). Every
power figure attached to any of this is `UNKNOWN`: nothing here has been
measured, and the saving a motion gate buys is a claim about a receiver's
low-power modes that T-051 and T-052 have not yet made.
