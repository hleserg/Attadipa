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
board that has no receiver of its own. Found in review.
Amended: 2026-08-24 — §3a states the axis and no longer states the
representation. The 2026-08-23 amendment replaced three wrong homes with one
mandated home, and ADR-0011 §2 then retracted that mandate and delegated the
choice to T-026 while still pointing at this section for it. One question, two
documents, opposite answers, and a P1 task told both. **T-026 chooses between a
stored field and an accessor over `PositionSource`**; this section says only
that the state is required, explicit, and not any of the three wrong homes.
Found in the eleventh review round of
[#94](https://github.com/hleserg/Attadipa/pull/94).

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

> **Co-location is a required state, carried explicitly and readable from every
> observation — never an assumption, and never folded into an axis that means
> something else. It defaults to `Unknown`, and `Unknown` is the ordinary case
> rather than a failure. What it withholds is the *claim*: a position whose
> co-location is `Unknown` may be shown, and must be shown with its actual
> source — never as the wearer's own instrument's fix.**

**Child Mode is open and this section does not answer it.** The rule above puts
a provenance sentence on every position whose co-location is `Unknown`, and on
the Waveshare board — which has no GNSS of its own — **every** position is
`Unknown`, so the hedge is permanent rather than occasional. The specification
says that screen is *for a six-year-old*, and a permanent qualifier on the one
number they came to read is a worse answer than the qualifier prevents,
which is a design question and not a correctness one. T-026 handles the
*localisation* of the sentence — it is a `StringId` and not a literal — and
that is a different question from whether Child Mode shows it at all. **Named
here rather than decided**: this ADR is about what the axis means, the Definition
of Done names Child Mode, and this section considered it nowhere until the
twelfth review round of [#94](https://github.com/hleserg/Attadipa/pull/94).
Whoever writes the Child Mode position screen owns the answer.

**This section states the axis and does not choose the representation.** A
stored field beside
[`PositionSource`](../../core/include/attadipa/core/position.h) and an accessor
over `PositionSource` both satisfy the rule above; **T-026 decides which**, and
[ADR-0011](0011-gnss-integrity.md) §2 leaves it open too — it says so in its own
words, and it argues the accessor case while leaving the choice with T-026,
which is not the same sentence as this one and is not meant to be. (This
paragraph claimed it said *"the same in the same words"* until the twelfth
round; it did not, and a reader checking a claim about wording is exactly the
reader who finds the two documents still disagreeing.) It had to leave it open,
because the two documents said opposite things until the eleventh review
round of [#94](https://github.com/hleserg/Attadipa/pull/94): this section
mandated a field four times over, ADR-0011 §2 retracted the mandate and argued
for the accessor — *"an accessor serves it at no risk, while a stored field buys
a field that can be forgotten"* — and ADR-0011 then **delegated the question
back here by name**. Follow that pointer and you land on the mandate ADR-0011
had just withdrawn. An agent picking up T-026 would have built to the first
line of its acceptance and been rejected for satisfying the ADR that defines the
state. That is under-determined rather than ambiguous, and it is this branch's
own *table-corrected-without-its-prose* failure applied to the delegating
document and not the deciding one.

**What does not move is which homes are wrong**, and that is the load-bearing
half. An earlier version of this section said the state was *"carried on
`PositionValidity` / `TrustState`"*. Both are wrong, and review caught all three
readings an implementer could take:

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
served it), and it belongs beside them rather than inside either. "Eleventh" is a
position in that table, not a claim that the register is complete: `PositionSource`
and `Origin` are not in it either, so a reader counting the axes a position
actually carries counts more than eleven. Note also that
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

**Three detectors already in the tree fire on this configuration, and this
section has now named too few of them twice.** The first version named one and
tried to legislate it away; the second named two; review found a third. That
pattern is the finding — a list of detectors that reads as complete is worth
less than one that says how long it is and how it was arrived at. All three are
below, none is changed here, and the third is the one that turns a `Degraded`
into an `Untrusted`.
`TrustEvaluator::compare_provider`
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
is measured against wherever the wearer was standing several minutes ago"*. The
space axis has the same shape, and an earlier version of this section wrote the
same answer for it: that `compare_provider` *must consult* co-location, and that
two fixes whose co-location is `Unknown` are neither disagreement nor agreement.

**That was wrong three ways, and review proved each of them against the tree
rather than against the argument.** It contradicted itself on its own fixture —
with the state consulted, 300 m still exceeds the 250 m threshold (`provider_disagreement_mm = 250000`), so
the two replays it demanded be identical are not. It contradicted two tests that
pass today: [`tests/test_trust.cpp`](../../tests/test_trust.cpp) asserts that a
`PositionSource::NodeGnss` fix ~550 m from the local one **raises**
`ProviderDisagreement`, and that a live bit is **left standing** when a later
comparison cannot be made — the regression test for the very
silence-not-an-all-clear rule the deleted paragraph cited as its precedent. And
because nothing produced any co-location value other than `Unknown`, the
exemption would have fired on every pair including local-versus-local, making a
weight-30 spoofing-relevant signal unreachable. **An ADR does not get to switch
off a trust signal in prose**, and one that reaches for a rule this large has
stopped describing a decision and started making an undocumented one.

**And the second detector is heavier, needs no second provider, and was missed
by the paragraph above until review named it.**
`TrustReason::MotionDisagreement` — weight **45**, the heaviest short of the
receiver reporting spoofing — is raised by
`moved_at_rest = motion.known && !motion.moving && moved > policy.jump_while_still_mm`
([`core/src/trust.cpp`](../../core/src/trust.cpp)), at **50 m** rather than 250,
against **one** position rather than a pair. `motion` is the wrist's
`MotionEvidence`; `observation` is whatever `LocationService` handed in,
`PositionSource::NodeGnss` included. The comment above that line states the
same-body premise out loud — *"a still wrist is genuinely still, so a position
that walks away from a stationary device is evidence"* — and this section is the
one saying that premise does not survive a change of body.

On a Waveshare board the reproduction is the product working normally: no
receiver of its own, a node attached, so `NodeGnss` is the only position there
is. The wearer sits at a desk and OD-6's always-on IMU reports
`known = true, moving = false`; somebody picks the bag up and walks 60 m. Two
node fixes 60 m apart clear the 50 m threshold, `MotionDisagreement` alone
reaches 45 against a `degrade_at` of 30, and the only fix the board has comes
out `Degraded` — with a reason saying the device moved while the accelerometer
says it did not. No second provider is involved and no 250 m threshold applies.
`Unknown` co-location withholds nothing here, because this section governs the
claim and this is arithmetic.

**And the third is heavier still, and needs two providers only in the sense that
something upstream chose between them.** `TrustReason::PositionJump` — weight
**40** — is raised in `TrustEvaluator::observe()`
([`core/src/trust.cpp`](../../core/src/trust.cpp)) from
`distance_mm(*observation.position, previous_position_)` divided by the elapsed
time, against `implausible_speed_mm_s` (55 000 mm/s). `previous_position_` is a
bare `Position`, exactly as `latest_position_` is in `compare_provider` — this
branch already says so in T-141's *Watch for* bullet and drew the conclusion for
only one of the two. It is **conditional**, which is why it is third rather than
first: it needs consecutive `observe()` calls carrying positions from different
bodies, and no `LocationService` exists yet to produce them.

The invitation is in `position.h`'s own header — *"Ordered worst to best … so a
fold over several providers can take the best without a table"* — and nothing in
the tree, or in this section, says the winner of that fold may not change
without a `reset()`. **So this ADR states the premise rather than leaving it
implied: a `TrustEvaluator` sees one body's positions, and a change of primary
provider is a `reset()`, not a new sample.** That is a constraint on
`LocationService` when it is written, and it is the cheaper half of the fix —
the alternative is `PositionJump` growing a source test of its own, which T-141
may still decide it wants.

The reproduction if the premise is not honoured is the OD-8 configuration T-142
names: wearer at a desk, node in a bag by the door 300 m away, both `Valid`, the
fold alternating at 1 Hz. 300 000 mm over 1 000 ms is 300 000 mm/s against a
55 000 limit, so `PositionJump` (40) raises; the wrist is still and the position
moved more than `jump_while_still_mm`, so `MotionDisagreement` (45) raises too;
85 clears `untrust_at` (60) and the device is **`Untrusted`**, not the
`Degraded` this section predicted. `remember()` requires `Trusted`, so there is
no last-trusted position either — the board has no navigation at all, which is
the outcome this section exists to prevent.

**All three belong to T-141, and it is the enumeration rather than the behaviour
that this section had wrong — twice.** An implementer reading *"one detector"*,
or *"two"*, builds to a list short by the likeliest case and reads the silence
as coverage.
[#112](https://github.com/hleserg/Attadipa/pull/112) — an open **pull request**,
not an issue, which an earlier version of this line got wrong — is already on the code
side of the second one — *a wrist's stillness stops judging a node's position* —
which sharpens the point rather than closing it: this ADR must not read as
settled-and-singular while another branch removes a case it does not mention.
Found in review.

**And #112 answers the same axis under another name, in another ADR, which is a
merge-order question rather than a disagreement.** Named here rather than left
for whoever merges second, because until the eleventh review round this section
knew #112's *behaviour* and neither its **type** nor its **ADR**. #112 adds
`SensorBody { Unknown, Watch, Node, Companion }` in a new `core/motion.h`, a
free function `SensorBody body_of(PositionSource source);` in
[`position.h`](../../core/include/attadipa/core/position.h), and
**ADR-0013 §3** to govern them; it makes motion disagreement inert unless the
two readings are demonstrably the same object. `body_of()` **is** an accessor
over `PositionSource` — which is precisely the representation ADR-0011 §2
recommends and which this section, as amended above, no longer forbids. Under
the earlier wording the two branches were in flat contradiction: `:187` required
a stored field for a value #112's tree computes. They are not any more, and
T-026 may well find that the accessor it is asked to choose between is
`body_of()` with a different question asked of it — co-location `SameBody` is
`body_of(source) == SensorBody::Watch`, and the two names are worth reconciling
rather than both existing.

**The merge order: #94 lands before #112**, because this section is the *same*
§3a heading #112 also writes, developed through eleven review rounds against
this branch's diff, so #112 rebasing onto it loses nothing while the reverse
loses all of that. That is the whole of what belongs here. **The mechanics —
which record #112 must then delete rather than renumber, and which task ID it
must reconcile — live in the two pull request bodies**, where
[OWNER_DECISIONS](../research/OWNER_DECISIONS.md) says agent-written merge
policy belongs: it binds nobody afterwards, and an accepted ADR does. An earlier
version of this paragraph carried all of it, which would have left an inventory
of an unmerged branch inside an accepted decision — archaeology the day #112
lands. Found in the twelfth review round. What #112 still carries is everything
this section does not say: `SensorBody`, `body_of()`, ADR-0013 and the motion
half. Its Testable item
**Three** below — *the two fixtures the trust suite already holds still pass
unchanged* — is asserted against the suite as it stands **when T-026 runs**, not
as of this ADR: #112 rewrites those fixtures on purpose, and an item that reads
otherwise would make a correct change look like a regression.

**So this section governs the claim, not the arithmetic.** Co-location decides
what the screen may say a position is *about*. It is not an input to
`TrustState`, and it changes nothing in the two fixtures above **as they stand
today**. It does not gate `compare_provider` or `moved_at_rest` — and that
sentence is about *co-location*, not about whether those detectors should be
gated by something: #112 gates `moved_at_rest` on `SensorBody`, which is T-141's
question and is not contradicted here. This paragraph read as though nothing
should ever gate them, which was never the claim.

**Who produces the value, because a state nothing can set is a constant.** A fix
from this board's own receiver is co-located *by construction* — the receiver is
strapped to the wrist the position is about — so a `LocalGnss` fix carries
`SameBody`. Everything else carries `Unknown`, because nothing on either board
measures the separation between the wearer and whatever produced the fix, which
is this section's own premise. There is deliberately no third producer and no
way to promote `Unknown` to `SameBody` by inference: the promotion would be the
confident number on an unobservable quantity that §3 exists to refuse.

**`PositionSource` has six enumerators and the *first* is the default.**
`PositionSource::Unknown` ([`position.h`](../../core/include/attadipa/core/position.h))
is the field's initialiser — what a driver that forgets to stamp provenance
produces — so *shown with its actual source* can print `Unknown` for the source
**and** `Unknown` for the co-location: two different questions answered with one
word. A screen that renders both as the same string is telling the reader
nothing twice. The two are separate **answers** and must read back separately —
whether the second is stored or computed is T-026's, per the paragraph above,
and this sentence said *"separate fields"* until the twelfth round. A source of
`Unknown` is a defect to surface rather than a provenance to display. Found in
review.

**`Unknown` is not a synonym for "the node's".** An earlier draft of the quoted
rule said such a position *"must be shown as the node's fix"*, and
[`PositionSource`](../../core/include/attadipa/core/position.h) has six
enumerators rather than two: `Companion` (a phone — ADR-0002's subject),
`Manual` (the user typed it) and `Simulated` all carry `Unknown` as well. On a
Waveshare with no node attached and a position typed in Settings, that sentence
made the screen credit a node that is not there — a false provenance claim on a
navigation screen, and worse in the simulator, where every scripted fix **is
meant to carry** `Simulated` and every fix would therefore be labelled the
node's. *Meant to*, because nothing produces the enumerator today: outside its
`to_string` arm in [`position.cpp`](../../core/src/position.cpp) it occurs
nowhere, the replay rig stamps `NodeGnss`
([`tests/replay/replay.cpp`](../../tests/replay/replay.cpp)) and every other
path keeps the `PositionSource::Unknown` initialiser. That makes stamping it a
**requirement T-026 carries**, not a property to assert against — an assertion
written today runs over an empty set and passes having exercised nothing. Found
in review. It also
contradicted OD-8 item 2, quoted verbatim in this same section, which names
**three** claims rather than two. The rule is therefore about *the actual
source*, whatever it is; what it forbids is passing an `Unknown`-co-location fix
off as this body's own instrument reading. Found in review.

**And the tension is real, so it is filed rather than legislated.** A node in a
bag by the door with a perfectly good fix 300 m from the wearer's perfectly good
fix raises `ProviderDisagreement` today, and that is the configuration OD-8 asks
for by name. Whether the comparison should be scoped by co-location, whether the
disagreement should be reported without costing trust, or whether it should be
left exactly as it is, is a **trust-engine decision with its own tests**: it
changes what `ProviderDisagreement` means under
[ADR-0011](0011-gnss-integrity.md) §5, the section that requires reason codes to
record *which* evidence moved the state, and it invalidates two shipped
regression tests. **§4 is not that section** — it is *"Differential corrections
belong to a provider, not to GNSS"*, about RTCM, and two earlier drafts cited it
here and in T-141's acceptance. The phrase that went with the wrong number,
*"disagreement is evidence about both of them and belongs to neither"*, is not
an ADR sentence at all: it is the comment on `compare_provider` in
[`trust.h`](../../core/include/attadipa/core/trust.h). Found in review. That is an ADR of
its own, filed as **T-141**. Until it is answered the tree's behaviour stands
unchanged, and this section says so instead of quietly overruling it. Found in
review, twice: the first version put the state on the wrong field, the second put
the wrong rule on the right field.

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
Waveshare that receiver sits on the *node*. Two bodies. Co-location neither
licenses that gate nor forbids it: the value on a node fix is `Unknown` by the
producer rule above, and `Unknown` says the separation is **unmeasured**, never
that it is zero.

**The node standing still is not the wearer standing still** — and that sentence
is this ADR's conclusion, not a quotation. An earlier version of this paragraph
attributed it to *"`TASKS.md`'s own wording for T-080"*; T-080 does not contain
it. [T-080](../../TASKS.md#t-080--a-standing-person-does-not-need-a-new-fix) is
written entirely about the **wearer** and mentions no node, no second body and
no cross-body gate. Found in review, and the attribution is dropped rather than
quietly repaired, because the reader who follows it finds nothing there.

**So T-080 carries the case now, rather than this ADR observing it in passing.**
A wearer sitting at a desk with the node in a bag by the door is still, and the
node is not. T-080 is P1 and *"the largest continuous draw on a watch that has
GNSS"*; on Waveshare, which has no receiver of its own, a duty-cycle gate driven
by the wrist's stillness slows the **node's** receiver, so the board's only
position ages exactly while the thing holding it moves. T-080's acceptance now
requires the gate to name which body's stillness it read. Found in review.

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
actually be in. **Co-location as a required, explicitly carried state** (§3a),
defaulting to `Unknown`, never folded into `PositionValidity`, `TrustState` or a
`TrustReason` bit — an eleventh axis under ADR-0011 §2 and subject to that
section's rule. **Whether it is a stored field or an accessor over
`PositionSource` is T-026's**, not this ADR's; this list said *a field of its
own* until the eleventh review round of #94, which is the sentence that put this
ADR and ADR-0011 §2 in contradiction. **`SameBody` is produced by exactly one thing**:
this board's own receiver, for its own fixes. Everything arriving over the node
link is `Unknown`, and nothing promotes it. **It is not an input to the trust
engine** — `compare_provider` keeps the behaviour its tests describe, and
whether that behaviour should change is T-141 rather than this ADR.

**Testable.** In the simulator: scripted heading and scripted GNSS, including
zero speed, a speed ramp across the gate, a node attaching with a `NodeBody`
heading, and a stale heading. The assertion that matters: **no configuration of
inputs causes a wrist-relative arrow to be drawn from a `NodeBody` or
`CourseOverGround` source.**

For §3a, four more, none of which need hardware. **One:** a node-supplied
position with co-location `Unknown` reaches the screen and is labelled as the
node's fix, not the wearer's — the Waveshare configuration, where refusing it
would leave the board with no navigation at all. The label is
`LocationService`'s: under
[ADR-0004](0004-capability-sources.md) — *"No application queries node state.
[ADR-0002] rule 2 extends here unchanged: an application asks `LocationService`
for a position and never learns where it came from"* — the distinction is drawn
where the position is handed out and **not** by an application reading
`PositionSource`. An earlier version of this sentence cited ADR-0002 **rule 4**
directly, and it was the wrong rule *for this question*: rule 4 is *"all input
from outside the device is untrusted — range-checked, expiry-checked,
refusable"*, which says nothing about labels. An implementer following it lands
on an untrusted-input rule, finds nothing about provenance, and either
re-derives the boundary or concludes an application may read `PositionSource`
after all. **Rule 4 itself reaches a node and is not weakened by any of this**:
[ADR-0002](0002-companion-is-optional.md) lists it among the two rules that
*"are **not** phone-specific and extend to the node unchanged"* — *"A node is
closer to us than a phone is; it is not more trusted for it"* — and
`position.h`'s `in_range()` guard, `ADR-0005`'s hostile-frame corpus and T-020
all rest on that sentence. A node-supplied coordinate is range-checked before
anything narrows, indexes or multiplies it. What rule 4 does not do is decide
whose name goes on the position. Found in review — the third
time a load-bearing citation in this ADR has pointed at the wrong text, which is
the failure this section itself names: *the next agent follows it.* **And the same
case again with a `Manual` fix on a board with no node attached**, because
`Unknown` co-location is not a synonym for *the node's*: the label must read
back the source the fix actually has, and the case that catches the wrong
reading is the one where crediting a node is impossible. `Simulated` is the
same assertion in the simulator — **once the rig stamps it**, which it does not
today, so T-026 makes the fixture change first and the assertion second.
**And that closes the co-located path to the simulator, deliberately.** The
producer rule below gives `SameBody` to `LocalGnss` alone with *no way to
promote `Unknown` by inference*, so an honestly-stamped simulator fix is
`Simulated`, therefore `Unknown`, therefore the simulator can **never** exercise
`SameBody` — and stamping `LocalGnss` to reach it costs the source label item
One asserts against. That is not a defect to route around: it is the producer
rule holding. **The vehicle for the co-located path is the host trust suite**,
which constructs observations directly and is where items Two, Three and Four
already live; the simulator's job on this axis is to prove the `Unknown` path
renders honestly, which is item One. Nothing is broken today because `sim/`
produces no `GnssObservation`s at all — but T-026's acceptance is what somebody
builds to, and an acceptance that implies a simulator assertion over `SameBody`
buys a test that runs over an empty set and passes having exercised nothing,
which is the failure this section identifies one level down for `Simulated`.
Found in the eleventh review round of
[#94](https://github.com/hleserg/Attadipa/pull/94).
**Two:**
co-location costs a fix nothing in `TrustState` — take **one** fix, replay it
with its co-location `SameBody` and with it `Unknown`, and assert the verdict
and the reason bits are identical. Scoped to the fix's own weight on purpose:
this is the assertion that fails if somebody re-implements the state as a
`TrustReason` bit, and it says nothing about what a *pair* comparison does,
because the pair rule is T-141's to decide and not this ADR's.
**How the two replays are built depends on the representation, and T-026 says
which in the same change that chooses it.** Under an **accessor** over
`PositionSource` the two cannot vary independently at all: the fixture is one
fix stamped `LocalGnss` and one stamped `NodeGnss`, and the assertion becomes
*the source changing costs the fix nothing in `TrustState` beyond what
`PositionSource` already costs it* — which is the same guard against a
`TrustReason` bit and is constructible. Under a **stored field** the two replays
vary the field directly, and one of them — `SameBody` with a non-`LocalGnss`
source, or `Unknown` with `LocalGnss` — is a pairing the biconditional forbids,
which is why T-026 requires that biconditional enforced **where the observation
is constructed** rather than in the type: a fixture must be able to build the
forbidden pairing on purpose to prove it is refused, and a type that makes it
unrepresentable also makes this item unwritable. Stated because the eleventh
review round found this item constructible under one representation and not the
other, in a section that had stopped naming which. **Three:** the
two fixtures the trust suite already holds still pass unchanged — a `NodeGnss`
fix ~550 m out raises `ProviderDisagreement`, and a live bit is left standing by
a comparison that could not be made. An implementation of §3a that reddens
either has changed something §3a did not ask for. **Against the suite as it
stands when T-026 runs, not as of this ADR**:
[#112](https://github.com/hleserg/Attadipa/pull/112) rewrites both fixtures on
purpose — that is its subject — so an item pinned to today's file would make a
correct change look like a regression. What is asserted is that *§3a* does not
move them, not that nothing does. Stated in the eleventh review round of
[#94](https://github.com/hleserg/Attadipa/pull/94). **Four:** co-location does
not live in `PositionValidity`, in `TrustState` or in a reason bit — whichever
representation T-026 picks — and the guard for
that is **the exhaustive `to_string` switch, not a count assertion**. An earlier
version of this item specified *"a compile-time assertion that
`kPositionValidityCount`, the `TrustState` enumerators and `kTrustReasonCount`
all still hold exactly their documented values, so appending `Unknown` to any of
the three is a build failure"*. **It is not, and review reproduced it.** Both
counts are defined from whichever enumerator is currently last —
`kPositionValidityCount` is `PositionValidity::Valid + 1` and
`kTrustReasonCount` is `TrustReason::FixLost + 1` — so appending after that
enumerator leaves the count unchanged and `static_assert(kTrustReasonCount ==
15)` passes. The assertion restates the count's own definition. *Insertion* is
caught, because it moves `Valid` and `FixLost`; appending is exactly the shape
§3a names, and it was the shape with no guard.

What does catch it is already in the tree and had to be **run** rather than
reasoned about: `to_string(TrustReason)`, `to_string(TrustState)` and
`to_string(PositionValidity)` are switches over every enumerator with **no
`default:`**, so a new one makes the switch non-exhaustive and `-Wswitch` fires
— and CI's strict job compiles with `-Werror`. Appending `NotColocated` after
`FixLost` gives *"error: enumeration value 'NotColocated' not handled in switch
[-Werror=switch]"* and the build stops. So the testable item is: **every one of
those three switches keeps its complete case list and never grows a
`default:`**, which is a property somebody can break in one line and which no
count assertion protects.

Why appending a `TrustReason` in particular must be a build failure rather than
a runtime surprise: `TrustEngine`'s `weight[kTrustReasonCount]` and
`evidence_at_[kTrustReasonCount]` stay sized to the old count while `index_of()`
is a raw cast, so the first `policy.weight[index_of(TrustReason::NotColocated)]`
writes one past the end of a member array. The reason-bit reading is the one
§3a calls *the worst of the three, because it looks right*, and this is why. On hardware: `NOT EXECUTED — HARDWARE REQUIRED`.

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
