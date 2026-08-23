# 0009 — Heading is three quantities, and one of them belongs to a different body

Status: **accepted**
Date: 2026-08-21
Amended: 2026-08-22 — **§3a added** by
[OD-17](../research/OWNER_DECISIONS.md), which answered A5 and A6. §3a states the
general rule the node-compass refusal in §3 is a special case of, so it holds
whichever way A6 had come back. §3's own text is unchanged in force; what changed
is that the condition it was written against is now settled.
Amended: 2026-08-23 — §3a's operative sentence rewritten. It named
`PositionValidity` / `TrustState` as the state's home; neither can hold it, and
one of the three readings would have penalised every node-supplied fix on a
board that has no receiver of its own. Co-location now has a field of its own.
Found in review.

## Context

Final §75 item **E**, and final §10. Re-checking found this one narrower than
the review described, and in a way that is more dangerous rather than less.

The review warns that a magnetometer in an Attadipa node does not tell the
orientation of the watch. That error had not been made — because there was no
heading model at all to make it with. "Heading" appeared as prose in seven
documents and as a structure in none. No source, no reference frame, no
confidence, no validity. `MAGNETOMETER_BACKLOG.md` had the right instinct —

> Heading from GNSS course-over-ground is a different thing … It also only works
> while the user is moving, which is the part that makes it a different product
> rather than a lesser one.

— and then never named the frame it was in.

That is the dangerous state, not a safe one. Nothing had been decided, several
documents used one word for three quantities, and the node has an open question
(A6) about whether it carries a magnetometer. If A6 comes back "yes" with no
model in place, the obvious implementation — pipe the node's compass into the
navigator's arrow — is wrong, ships, and is wrong in a way that looks perfectly
plausible on a desk and fails outdoors.

There is a second reason to settle this now. Neither board has a magnetometer
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)), so today the *only* possible
source is GNSS course-over-ground, which does not exist while the user is
standing still. A model built around "the compass angle" has no way to express
that, and the default behaviour of a compass widget with no data is to point
north — which final §97 forbids by name and which is, on a navigation device, a
safety problem rather than a cosmetic one.

## Decision

### 1. Three quantities, three names, never interchangeable

| Quantity | What it is | Needs | Frame |
|---|---|---|---|
| **Bearing to target** | the direction from here to there | two positions | geographic — true north |
| **Heading** | which way a *body* is pointing | a magnetometer, or fusion | that body's frame |
| **Course over ground** | which way something is *moving* | successive positions, and motion | its own frame |

A navigation arrow that rotates with the wrist needs **bearing minus heading**.
If heading is unavailable, that arrow cannot be drawn — and the honest fallback
is to draw the bearing against a fixed north-up reference and say so, not to
substitute course-over-ground and hope the user is walking forwards.

### 2. Heading carries its frame, and the frame is not decorative

```cpp
enum class HeadingSource : uint8_t {
    Unknown, Magnetometer, SensorFusion, GnssCourseOverGround, RemoteSensor,
};

enum class ReferenceFrame : uint8_t {
    WatchBody,          // the watch's own chassis
    NodeBody,           // an Attadipa node's chassis
    CourseOverGround,   // the direction of travel of whatever is moving
};

struct Heading {
    uint16_t       centideg;      // 0 .. 35999, true north; integer, not float
    HeadingSource  source;
    ReferenceFrame frame;
    uint8_t        confidence;    // 0..100; 0 is legal and means "no idea"
    Timed<>        age;           // two ages if it crossed a link — ADR-0004 §3
    Validity       validity;      // Valid | Stale | Uncalibrated | NoMotion | Invalid
};
```

**There is no `UserBody`.** Final §10 forbids inventing one unless the system
can establish how the user is oriented, and it cannot: the watch is on a wrist
that swings, rotates and hangs at an arbitrary angle to the torso. Every wearable
that draws a confident user-relative arrow is making an assumption about arm
position that this project has not measured and cannot currently measure.

`Validity::NoMotion` is a state, not an error. It is the ordinary condition of a
person reading their watch.

### 3. A node's compass is the node's compass

**`NodeBody` heading is never presented as `WatchBody` heading.** Not scaled, not
offset, not "close enough".

The node is a separate object. It may be in a backpack, clipped to a belt at an
arbitrary yaw, face-down on a table, or hanging from a strap and rotating freely.
Its magnetometer measures *its* orientation, which is related to the watch's by a
transform nobody has measured and which changes every time the user puts the
node down.

A remote heading may be converted to `WatchBody` only when **all** of these hold,
and the conversion is refused otherwise:

1. a transform between the two frames is known;
2. it is calibrated, with a recorded calibration identity and timestamp;
3. it is still valid — the calibration is invalidated by the node being detached,
   by either device rebooting, and by an inactivity timeout;
4. the resulting confidence is the product of both sources' confidence and the
   transform's, not the better of them.

None of those holds today, and there is no mechanism that would establish them.
So the answer is: **a node's heading is displayed as node orientation in
diagnostics, and is not used for the user-facing arrow.**

**A6 is answered, 2026-08-22: no — for the Attadipa node.** It will not carry a
magnetometer: owner decision, *"в нодах магнитометр реально лишний"*
([OWNER_DECISIONS](../research/OWNER_DECISIONS.md) OD-17). That is the whole of
what A6 asked, and it is narrower than *"`NodeBody` heading has no source and
never will"* — which an earlier draft of this line claimed.
[OD-7](../research/OWNER_DECISIONS.md) makes the companion **any** node the
watch has a client for, not only ours, and what a third-party companion carries
is `UNKNOWN`. So `HeadingSource::RemoteSensor` and the four-condition gate stay exactly where
they are. The gate is in **§3** (*"A node's compass is the node's compass"*), not
§2, and the reason to keep it is **OD-7** in the sentence above: a third-party
companion may carry a magnetometer, and deleting the gate would leave that day's
device with nowhere safe to report to but the arrow, which is the thing this ADR
exists to prevent.

An earlier version of this paragraph cited *"§2"* for the gate and *"§7 below"*
for the reason. There is no §7; the phrase it quoted is in **Consequences →
Open**, and that entry says surfacing `RemoteSensor` heading in Diagnostics is
*"probably yes"* — a decision this ADR records as **unmade**. Resting §3's
survival on it would have made the strongest argument here the weakest, one
`git blame` away from the deletion this paragraph exists to prevent. This paragraph is retained anyway,
because the rule it states is not really about a magnetometer. It is a special
case of a rule general enough to survive the answer coming back either way —
see §3a.

### 3a. The rule that made both answers correct at once

The same owner decision that closed A6 also *planned* an accelerometer, and
probably a gyroscope, for the node, for GNSS power optimisation — planned, not
ordered, and the gyroscope only probably; see OD-17 for the quote. Read
carelessly, that looks like a contradiction of §3: sensors on the node, used to
improve a reading, again. It is not, and the reason is worth stating as its own
rule rather than left to be re-derived the next time a node sensor is proposed:

> **An *orientation* may not be carried across bodies. A sensor may correct
> another reading taken on the same body; when it corrects a reading taken on a
> different one, what it may correct is bounded by how far apart the two bodies
> can be.**

The qualifier is the whole rule and an earlier draft of this paragraph dropped
it, which made the rule forbid the product. Orientation does not survive a
change of body **at all**: a node lying in a bag and a wrist held out in front
share no transform, and the error is unbounded — the node can be pointing any
way. Position differs, and the difference is in kind rather than degree. A node's
heading is uncorrelated with the wearer's — a device loose in a bag can point
any way, so the error is the whole circle. A node's position differs from the
wearer's by exactly the separation of the two bodies, whatever that separation
happens to be.

**That last sentence is this ADR's own reasoning and is sourced nowhere else.**
An earlier version attributed it to `ADR-0004`, which says no such thing: the
lines it cited are the *"No application queries node state"* layering rule, and
`ADR-0004`'s only use of "bounded" is about hostile input at the link edge.
A wrong citation under a load-bearing premise is the worse of the two failures,
because the next agent follows it, finds a layering rule, and either re-derives
the physics or assumes it settled.

**So the separation is not an assumption here. It is a state, and it defaults to
unknown.** A metre in the same bag is one case, not a bound: OD-7 makes the
companion *any* node, OD-8's own example is *"a fix relayed from a node on a
roof"*, and this section's own closing example is a node in a bag by the door
while the wearer sits at a desk. Nothing on either board measures node-to-wearer
separation, so a rule phrased as *"bounded by how far apart the two bodies can
be"* is satisfied by construction and evaluates nothing — a confident number
resting on an unobservable quantity, which is the failure §3 above exists to
refuse, moved from heading to position. §3 gates orientation with four
conditions and refuses otherwise. Cross-body position gets the same shape:

> **Co-location is a required state, carried in a field of its own beside
> [`PositionSource`](../../core/include/attadipa/core/position.h) — never an
> assumption. It defaults to `Unknown`, and `Unknown` is the ordinary case
> rather than a failure. What it withholds is the *claim*: a position whose
> co-location is `Unknown` may be shown, and must be shown as the node's fix
> rather than as the wearer's.**

**A field of its own, and the emphasis is load-bearing.** An earlier version of
this section said the state was *"carried on `PositionValidity` / `TrustState`"*.
Both are wrong, and review caught all three readings an implementer could take:

- [`PositionValidity`](../../core/include/attadipa/core/position.h) is
  `NoFix < Stale < Degraded < Valid`, and the file makes the order a contract —
  *"ordered worst to best so that 'at least Degraded' is a comparison rather than
  a switch"*, with `kPositionValidityCount` assuming `Valid` is last. `Unknown`
  has nowhere to sit: this section calls it *ordinary, not a failure*, so it
  cannot sort below `NoFix`, and it is not a degree of position quality, so it
  sorts nowhere at all. Appending it makes every fold-the-best comparison prefer
  it to `Valid`.
- [`TrustState`](../../core/include/attadipa/core/trust.h) is
  `Untrusted | Degraded | Trusted`, the output of a weighted score. A fourth
  value that is neither better nor worse is not a verdict.
- **A `TrustReason::NotColocated` bit is the worst of the three**, because it
  looks right. Reason bits carry weight *toward* `Untrusted`, and this section
  says `Unknown` withholds the claim and not the position. As a reason bit,
  every node-supplied fix on a Waveshare board — a board with no receiver of its
  own, whose entire navigation story is the node's fix — takes a permanent trust
  penalty. That is the product this section was rewritten to protect, broken by
  the sentence protecting it.

[ADR-0011](0011-gnss-integrity.md) §2 forbids all three by name: *"These are
separate, and no two of them may be stored in the same field."* Co-location is
an **eleventh axis** and not one of that table's ten — it is a *provenance
geometry* question, "is this fix's body the wearer's body", answered by neither
`PositionSource` (which body produced it) nor `Origin` (which side of the link
served it), and it belongs beside them rather than inside either. Note also that
`PositionValidity` is **not**
[`Validity`](../../core/include/attadipa/core/availability.h), which *does* carry
`Unknown` and is the freshness axis — an implementer who reaches for the name
lands on the wrong one.

That is what lets the Waveshare board — which has no GNSS of its own — have a
navigation story at all ([OD-1](../research/OWNER_DECISIONS.md),
[OD-8](../research/OWNER_DECISIONS.md)) without the story being a lie. The
decision that governs is **OD-8 item 2**, which this section did not previously
cite: *"Provenance travels with the position, always. A fix from the wearer's own
receiver, a fix relayed from a node on a roof, and a coordinate lifted out of
somebody else's message are three different claims about where the wearer is, and
exactly one of them is about the wearer. The user-facing consequence is that the
screen says which, in words."* So a node-supplied *position* reaches the wearer's
screen, labelled as the node's; a node-supplied *heading* does not reach the
arrow at all.

**One detector already in the tree will fire on this, and §3a is what makes it
wrong.** `TrustEvaluator::compare_provider`
([`core/src/trust.cpp`](../../core/src/trust.cpp)) raises
`TrustReason::ProviderDisagreement` when two positions inside the 5 s comparison
window are more than `provider_disagreement_mm` apart — 250 m by default
([`trust.h`](../../core/include/attadipa/core/trust.h)) — and it has **no notion
of which body either fix came from**. Before §3a, two positions that far apart
were evidence that something was wrong. After it, they are the ordinary case:
OD-8 asks by name for a watch with its own receiver *and* an attached node, and
a node in a bag by the door while the wearer sits at a desk is exactly the
configuration this section legitimises. A node 300 m away with a perfectly good
fix would degrade trust in the wearer's own perfectly good fix.

The same failure was already closed on the **time** axis — the comparison window
exists because *"a node's position arriving after the watch's own fix went stale
is measured against wherever the wearer was standing several minutes ago"*. §3a
introduces the state that closes it on the **space** axis, and the comparison has
to consult it: two fixes whose co-location is `Unknown` are not evidence of
disagreement, and must not be recorded as such — nor as agreement, by the same
silence-not-an-all-clear rule the window already follows. Carried into T-026's
acceptance. Found in review.

[OD-7](../research/OWNER_DECISIONS.md) item 3's *"never presented as the wearer's
own fix"* says the same thing, and an earlier version of this section rested on it
alone. That was a mis-scoping worth naming: the clause attaches to *"a coordinate
taken out of somebody else's message"*, one of the three arrival paths item 3
lists, and the companion's own receiver is a different one — the Waveshare
product. OD-8 item 2 covers all three, and `CLAUDE.md` ranks `OWNER_DECISIONS.md`
above this ADR.

Two worked examples, because they land on opposite sides of the same line:

- **The node's magnetometer, had A6 come back "yes", correcting the *watch's*
  heading.** Refused by §3 above. The node and the wrist are different bodies —
  the magnetometer measures the node's orientation, not the wearer's, and no
  transform between the two is ever established for a device loose in a bag.
- **The node's IMU correcting the *node's own* GNSS position.** Allowed, and
  needs no transform at all. The IMU and the GNSS receiver sit on the same
  body, so "the node is still" or "the node moved" composes directly with the
  node's own position estimate — that was always what the node's position
  meant. It is why the node's IMU is filed as its own capability question
  ([#93](https://github.com/hleserg/Attadipa/issues/93)) rather than treated as
  a small addition to this ADR: it is a same-body correction, not a heading
  source, and does not belong in the `HeadingSource` enum at all.

**And [OD-10](../research/OWNER_DECISIONS.md#od-10--a-standing-person-does-not-need-a-new-fix)
is the cross-body case, not the same-body one.** An earlier draft cited it here
in support, which was wrong and worth correcting rather than quietly dropping:
OD-10 gates a receiver on **the wearer's** stillness, read from **the watch's**
accelerometer (OD-10 sources it to OD-6's always-on watch IMU) — and on
Waveshare that receiver sits on the *node*. Two bodies. It is allowed under the
bound above, not under the same-body clause, and `TASKS.md`'s own wording for
T-080 says why the distinction has to survive: **the node standing still is not
the wearer standing still.** A wearer sitting at a desk with the node in a bag
by the door is still, and the node is stiller.

### 4. Course over ground needs motion, and standing still is a designed state

GNSS course is derived from movement. Below some speed it is noise; at zero
speed it does not exist. Two consequences:

- **A speed gate exists**, below which course is reported `NoMotion` rather than
  reported badly. **Its value is unknown** and depends on the fitted GNSS module,
  its update rate and whether it reports Doppler-derived velocity or differenced
  positions. It is a **measurement**, recorded as open (**H10**), not a number
  invented here. Final §26 is explicit: *do not invent settling intervals.*
- **Standing still is a first-class UI state** with its own design, not an
  absence. It shows the bearing to the target, north-up, and says that direction
  needs a few steps. It never shows a rotating arrow, and it never shows 0°.

The empty NMEA course field is a known trap with a known victim: TinyGPS++
committed empty fields as zero, and course-over-ground is exactly the field that
is empty when stationary, so a stopped device reported due north
([REUSE_LEDGER](../research/REUSE_LEDGER.md)). The parser must distinguish
*absent* from *zero*, and this is one of the reasons minmea's explicit
`_available` flags were preferred.

### 5. What the Navigator actually draws

The interesting design work is here rather than in the struct.

| Available | What is drawn |
|---|---|
| Position + bearing + valid `WatchBody` heading | the rotating arrow — the intended experience |
| Position + bearing + `CourseOverGround`, moving | a north-up map with a course indicator; **not** a wrist-relative arrow |
| Position + bearing, standing still | north-up, bearing marked, "walk a few steps to orient" |
| Position, no target | position and its quality |
| No position, provider `Ready` | acquiring, with elapsed time and satellite count |
| No position, provider `Unprovisioned` | the mascot's `GUIDING` pose and what an Attadipa node would add |
| Heading `Uncalibrated` | the value, marked, plus the calibration entry point |

Every row is a real state with a real remedy, which is the same rule the
availability enum follows ([ADR-0004](0004-capability-sources.md)). Three of the
seven are states this hardware is in *most of the time*, so they get designed
first rather than last.

### 6. Confidence is carried, and it is allowed to be zero

An angle without a confidence invites a UI that renders all angles alike. A
magnetometer near a vibrating motor, a course-over-ground at 0.4 m/s and a
freshly calibrated compass are three different numbers with the same units.

Confidence is `0..100`, and `0` is legal and means the value exists but is
worthless. The renderer decides what to do with low confidence; the service
never silently withholds the number, because Diagnostics needs it.

## Alternatives considered

**One `heading()` returning a float, with validity as a null.** Rejected. It
cannot express the difference between "no compass on this device", "the compass
needs calibrating", "you are standing still" and "the node knows its own
orientation but not yours" — four different sentences with four different
remedies. It is the same collapse `has()` made, in a different subsystem.

**Fuse everything into one best-estimate heading and hide the source.** Rejected.
The sources are not commensurable: they measure different bodies. Fusing
`NodeBody` with `CourseOverGround` produces a number with no frame, and a number
with no frame cannot be checked, explained, or refused.

**Use the accelerometer for tilt-compensated heading.** Not possible — it needs a
magnetometer to compensate, and neither board has one. Recorded so the idea is
not re-proposed: on the T-Watch there is not even a gyroscope, so the IMU cannot
integrate a relative heading either.

**Assume the node is worn in a known orientation and calibrate once.** Rejected.
It is an assumption about user behaviour dressed as a calibration, and it fails
silently the first time somebody puts the node in the other pocket. If a
transform is ever established it must be *measured*, invalidated aggressively,
and refused when stale.

**Wait for A6 before deciding any of this.** Rejected — that is the failure this
ADR exists to prevent. The wrong implementation is the obvious one, and it
becomes obvious at exactly the moment the answer arrives.

## Consequences

**Easier.** Adding a magnetometer later is a new `HeadingSource` and a
calibration record; nothing above `LocationService` changes. The Navigator's
states are enumerable and testable before any GNSS hardware exists.

**Harder.** Every heading consumer must handle a frame it cannot use, which
means the "no usable heading" path is written first and exercised most. On
current hardware that path is not an edge case — it is the normal case, every
time the user stops walking.

**Committed to.** A speed gate that is measured on the fitted module rather than
chosen. A calibration record that carries sensor identity, provider identity,
axis mapping, version, timestamp and quality (final §27), and that is invalidated
when the provider changes. A Navigator that is designed for the states it will
actually be in. **Co-location as a field of its own beside `PositionSource`**
(§3a), defaulting to `Unknown`, never folded into `PositionValidity`,
`TrustState` or a `TrustReason` bit — an eleventh axis under ADR-0011 §2 and
subject to that section's rule.

**Testable.** In the simulator: scripted heading and scripted GNSS, including
zero speed, a speed ramp across the gate, a node attaching with a `NodeBody`
heading, and a stale heading. The assertion that matters: **no configuration of
inputs causes a wrist-relative arrow to be drawn from a `NodeBody` or
`CourseOverGround` source.**

For §3a, three more, none of which need hardware. **One:** a node-supplied
position with co-location `Unknown` reaches the screen and is labelled as the
node's fix, not the wearer's — the Waveshare configuration, where refusing it
would leave the board with no navigation at all. **Two:** co-location `Unknown`
costs the fix nothing in `TrustState` — replay the same fix with the state set
and unset and assert the verdict and the reason bits are identical, which is the
assertion that fails if somebody re-implements it as a `TrustReason` bit.
**Three:** the field is not `PositionValidity` and not `TrustState` — a
compile-time assertion that both enumerations still hold exactly their documented
values, so appending `Unknown` to either is a build failure rather than a silent
reordering. On hardware: `NOT EXECUTED — HARDWARE REQUIRED`.

**Open.** **H10** — the speed gate, per GNSS module. **A5 and A6 are answered**
(2026-08-22, [OWNER_DECISIONS](../research/OWNER_DECISIONS.md) OD-17): a
magnetometer is intended, external, on the watch, placement not yet chosen
(T-109); **the *Attadipa* node will never carry one**, which is the scope §3
insists in §3a above — OD-7 makes the companion any node, and whether a
third party's carries a magnetometer is `UNKNOWN` rather than no. An earlier
draft of this line said "the node" unqualified, so this ADR contradicted itself
end to end. What remains open is whether `RemoteSensor` heading is worth
surfacing in Diagnostics even with no live source today — probably yes; it is
nearly free and it makes the frame distinction visible to whoever implements a
transform later, on the day some other remote device is capable of one.
