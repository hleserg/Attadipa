# 0011 — GNSS integrity: the receiver's own defences, and a trust state with reasons

Status: accepted
Date: 2026-08-21

## Context

[OD-5](../research/OWNER_DECISIONS.md#od-5--gnss-integrity-and-the-receivers-own-protection-comes-first)
arrived before any GNSS code exists in this repository. That timing is the whole
reason this ADR is cheap: there is no driver to refactor, no observation struct
to widen, and no application reading a field that is about to change meaning.
Everything below is a constraint on code that has not been written yet.

The amendment's central claim is that a modern GNSS receiver is not a source of
NMEA sentences with some extra registers. It is a device that already runs
interference detection, mitigation and — on some parts — spoofing detection,
and that will tell you what it found if you ask in its own protocol. A firmware
that parses `$GNRMC` and throws the rest away is not using a receiver, it is
using the least capable interface the receiver offers.

Three facts from this project's own research bound the design:

- **The T-Watch S3 Plus ships one of two receivers** — a u-blox **MIA-M10Q** or
  a Quectel **LS550G** — and the product name does not say which
  ([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md), VERIFIED). Their power
  rails differ: the LS550G variant needs DC4 at 850 mV *in addition to* BLDO1,
  so guessing wrong means GNSS silently never starts.
- **PPS is not connected.** The net exists on the daughterboard and appears
  nowhere in the main-board schematic. Anything that would have used a
  hardware pulse for timing has to be designed without one.
- **The Waveshare board has no GNSS at all.** Its `Capability::Position` comes
  from an attached Attadipa node or from nowhere
  ([ADR-0004](0004-capability-sources.md)), which means every rule here has to
  hold for a provider that is not on this board and can walk away mid-sentence.

And one fact that constrains the detectors rather than the driver: the T-Watch's
IMU is a **BMA423 — an accelerometer, with no gyroscope and no magnetometer**
([ADR-0009](0009-heading.md)). It is exactly the right part for asking *"is this
device physically moving?"* and it is not an inertial navigation system. Nothing
here may quietly become dead reckoning.

## Decision

Eight rules. They are architecture, not an implementation plan — OD-5 §15 is
explicit that the navigation stack is not being built now, and this ADR does not
build it.

### 1. The observation carries what the receiver said, not a summary of it

A GNSS observation is the widest point in the system, and the driver boundary is
where information is lost if it is going to be lost. So the observation carries
**both**:

- a **normalized Attadipa representation** — position, altitude, velocity,
  course, accuracy estimates, time, fix type, satellite counts, per-signal
  carrier-to-noise, and the integrity and interference indications, in units and
  enumerations that do not name a vendor; and
- the **receiver's native values** for anything that has one — the raw flag
  words, the raw status enumerations, the raw protection-level figures.

Not one at the cost of the other. The normalized form is what services consume;
the native form is what makes a field report diagnosable, and what makes it
possible to discover later that a normalization was wrong. A driver that
normalizes and discards has destroyed the evidence for its own bugs.

The list of fields is not to be copied mechanically from the amendment. The
binding part is the *rule*: **nothing the receiver reports may be dropped at the
GNSS driver boundary because the current consumer does not use it.**

### 2. The states do not collapse into one number

These are separate, and no two of them may be stored in the same field:

| Axis | Question it answers |
|---|---|
| availability | could this device ever produce a position, and is a provider bound and reachable — [`Availability`](../../core/include/attadipa/core/availability.h), seven values |
| receiver health | did the receiver come up, is it answering, is it configured |
| fix presence | is there a fix at all |
| fix type | 2D, 3D, dead-reckoning-assisted, time-only |
| freshness | how old is this position — [`Validity`](../../core/include/attadipa/core/availability.h) already carries `Stale` |
| accuracy | the receiver's own estimate of its error |
| integrity | a bound on that error the receiver is willing to stand behind |
| interference | is the band jammed, and is the receiver mitigating |
| spoofing suspicion | does anything believe these signals are fabricated |
| trust | the fused verdict, §5 below |

The case that forces this: **a provider can be `Ready`, with a numerically valid,
fresh, accurate-looking fix, and still be unusable for navigation** — because the
receiver is reporting a spoofing indication, or the protection level is invalid,
or the accelerometer says the device has not moved while the position has. One
`quality` scalar cannot express that, and a UI built on one will confidently
draw a wrong dot.

This is the same argument [ADR-0007](0007-two-capability-layers.md) made about
`has()` and [ADR-0009](0009-heading.md) made about heading, arriving a third
time. That is not repetition; it is the shape of every mistake this project has
had to correct so far.

### 3. What a receiver can defend itself with is a descriptor, and it lives below `LocationService`

A **GNSS receiver capability descriptor** records, per receiver, which of these
the part actually has:

```
JamDetection             InterferenceMonitor      ProtectionLevel
ActiveJamMitigation      PerSignalDiagnostics     SignalSecurityLog
SpoofDetection           ConstellationControl     AutonomousOrbitPrediction
AssistanceInjection      DifferentialCorrectionsInput
RawMeasurements          ConfigLock               MessageIntegrity
```

Three things about where it sits:

- It is **not** `platform::HardwareFeature`. That enum answers *"is a GNSS
  receiver on this board"*, and the answer for the T-Watch is yes for both
  variants. Which defences that particular part has is a property of the part,
  not of the board's parts list.
- It is **not** `core::Capability`. That enum is what a *product* can do, and
  "the receiver has a signal security log" is not something a user can want.
- It therefore sits **beside the GNSS driver, below `LocationService`**, and
  `LocationService` is the last layer that may read it. An application asks for
  a position and a trust state and **never learns the chip** — the same rule
  [ADR-0004](0004-capability-sources.md) §2 already applies to where a
  capability came from, and [ADR-0010](0010-localization.md) §4 applies to
  language. Diagnostics and Settings may show it, because inspecting the machine
  is what they are for.

Every entry starts as **`UNKNOWN`** and becomes `SUPPORTED` or `UNSUPPORTED`
only from a primary source — datasheet, integration manual, protocol
specification, vendor source — or from a real device. In particular, and stated
because the vendor's marketing says otherwise: **anti-spoofing on the LS550G is
`UNKNOWN`.** `UNKNOWN` behaves as *"do not rely on it"* at runtime and as
*"go and find out"* in the backlog (T-051, T-052).

### 4. Differential corrections belong to a provider, not to GNSS

**Differential corrections input is an optional capability of a specific
provider, never a method on an abstract GNSS driver and never an assumption
attached to the word "u-blox".** OD-5 states that the MIA-M10Q does not accept
RTCM; that claim is the owner's and is recorded as **to be confirmed from the
u-blox interface description in T-051**, because the specification in force says
its own technical claims are not automatically facts.

It does not matter to the architecture which way that confirmation goes. If a
generic `GnssDriver` grows an RTCM entry point, then every receiver that cannot
accept corrections has to implement a method that lies, and every caller has to
guess. Making it a declared capability of one provider means a receiver that
cannot do it simply does not offer it, and a caller that needs it asks and is
told no.

A grep of this repository at the time of writing shows **the assumption was
never written down here** — `RTCM` appears in no ADR, no architecture document,
no research file and no header. So this rule is not a correction; it is a fence
put up before the path was worn.

### 5. Trust is a state with reasons, hysteresis and a history

```
Trusted      the position may be used for navigation
Degraded     usable with a visible caveat; something is wrong
Untrusted    do not navigate by this
```

with all of:

- **weighted evidence from several sources**, not one flag;
- **hysteresis**, so a single bad epoch does not flip the state and a single
  good one does not clear it;
- **recovery earned from a retraction, never from the clock while the detector
  is still there** — §5.1 below, including the one case where the detector's
  subject leaves and the clock does then run. *"Still there" is itself measured
  in time*, and that is deliberate rather than a loophole reopening: a second
  source is judged gone only after `provider_departure_grace` of being unable to
  answer, where the first version of the rule judged it gone on a single
  uncomparable frame. The clock decides **whether there is still anyone who
  could retract**, never whether the allegation was retracted;
- **reason codes** — *which* evidence moved it, kept, not just the verdict;
- **timestamps** on both the state and each piece of evidence;
- **the last trusted position**, retained;
- **uncertainty that grows after trust is lost**, because a position that was
  good sixty seconds ago is a circle, not a point; and
- **a bounded transition log**, so a field report can be read after the fact.

What it is not: `gps_ok = true/false`, and not
`trusted = !(spoofFlag || jumpDetected || jamming)`. The second is worse than the
first, because it looks like it considered something.

**Keep the reasons.** A user-facing string, an app's decision to hide the
compass, and a diagnostic screen are three different consumers of the same
evidence, and a collapsed boolean serves none of them.

### 5.1 An allegation is retracted, not merely dropped

> **Added 2026-08-23**, after the implementation was found to be doing the
> opposite. OD-5 §4 and §8 already said this about a receiver's `Unknown`; what
> was missing was that the *same* rule governs the passage of time. (Not §2,
> which an earlier version of this note cited: §2 is about the LS550G's
> anti-spoofing **capability** being `UNKNOWN` rather than `SUPPORTED` — a
> datasheet claim about a part, not a per-epoch indication from a running
> receiver. Found in review.)

Evidence has to decay, or a device that walks out of an interference source
stays suspect for ever. So a piece of evidence stops counting after a time-to-
live — and **that is a statement about the score, not about the world.** Two
different facts can leave the score at zero:

| The score fell because | and that is |
|---|---|
| a detector said the condition is over | information |
| a detector stopped saying anything | silence |

**Only the first may move the state upwards.** The state machine therefore
remembers, per reason, which of the two doors an allegation left by, and while
any allegation stands unretracted the recovery hold does not run — the score is
low, and it is not low *for a reason anybody gave*. When a retraction does
arrive, the hold is measured from it, so time spent hearing nothing buys no part
of the recovery.

This is the same sentence as §6's *"a receiver that cannot detect spoofing is
not a receiver reporting that there is none"*, applied to a clock instead of to
an enumeration, and it is worth stating separately because the code got the
enumeration right and the clock wrong. `TrustEngine` expired a spoofing alarm
after fifteen seconds of silence, read the resulting zero as an all-clear,
started the clean hold on it and announced `Trusted` again twenty-five seconds
after the alarm — with no observation, no all-clear and no evidence of any kind
in between. The device asserted a position was fit to navigate by at exactly the
moment its receiver had stopped talking, which is the failure mode this whole
ADR exists to prevent, reached by the one door still open.

**What this costs, stated rather than discovered later.** A device that never
hears another positive word does not climb back on its own. That is deliberate:
the alternative is the behaviour above. The ways out are a detector saying the
condition is over, or `reset()` **when the provider goes away** — the scope is
part of the sentence, because `reset()` sets `Trusted` immediately, discards the
transition log and drops the remembered position, so it is the answer to *a
different provider is here now* and never to *this one is still stuck*. The pin
most likely to be met comes from the device's **own** receiver, which does not
detach, and for that one the only exit is a detector speaking.

**Not "never a timer", which is what an earlier version of this paragraph
said.** Bound 3 above describes `stop_awaiting()` and stops at *"stops
awaiting it"*, never at the consequence: once the allegation stops being
awaited the recovery hold runs and the state climbs one step per
`recover_hold`, with nothing having been retracted. That is a timer, and the
rejected alternatives at the end of this ADR call the shape *"a timer wearing
a state machine's clothes"*. What makes it legitimate here is narrower and has
to be said rather than implied: the allegation was about a **pair**, one of the
pair is gone, and there is no longer anything a retraction could come from.
Silence from a detector that is still present still buys nothing. Corrected in
the second review round of
[#153](https://github.com/hleserg/Attadipa/pull/153), which found the absolute
claim in five places, honoured in none.
([ADR-0005](0005-node-protocol.md) §5 is the rule about a link going away; an
earlier version of this paragraph cited ADR-0004 §3, which is *Availability is
not validity — and a remote datum has two ages* and says nothing about
`reset()`.)

**Three bounds on that, each of which was overstated somewhere before review.**

1. **It is per boot, not for ever.** Nothing in `core/` persists trust state, so
   a pin lasts exactly one session. That is probably right — an allegation is
   evidence about a moment, and carrying it across a reboot would need a story
   about how it is ever retired — but it is a property of there being no
   persistence rather than a decision, and it should be recorded as one.
2. **Any reason can reach the mask, not only three.** It was written that only
   `ReceiverSpoofing`, `ReceiverJamming` and `ProviderDisagreement` can, because
   every other reason is `set()` on each `observe()`. True only while `observe()`
   runs at least once per `evidence_ttl`. The call for when it does not is
   `refresh()`, which touches `FixLost` and `StalePosition` and nothing else, so
   in any observation gap every other live reason lapses unretracted too. The
   defaults size the window: `evidence_ttl` is 15 s and `stale_after` is 30 s,
   so in the prescribed `refresh(classify(retained, now), now)` pattern a
   reason live at t=10 s lapses at t=25 s while `classify()` still returns
   `Valid` and `StalePosition` does not go live until t=40 s — fifteen seconds
   of `Degraded` with `score() == 0` and **nothing in `reasons()` to show**. It
   self-heals on the next `observe()`, so no code changes here; the claim does
   not stand. *Half-stale as written, and corrected in the fourth review round
   of #153:* there is something to show now. The same change added
   `unconfirmed_reasons()` and `GnssStatus::trust_unconfirmed`, so those fifteen
   seconds are nameable on a screen — a lapsed allegation nobody withdrew, which
   is what they are. `reasons()` is still empty, and that is correct: the
   evidence really has expired. What was missing was a second field, not a
   different verdict.
3. **An allegation about a pair stops being awaited when the pair stops being
   comparable.** `ProviderDisagreement` is the one reason whose retraction is
   unreachable once its own freshness gate closes, and both halves of that gate
   close in ordinary operation — a duty-cycled receiver, or a relayed fix whose
   *measurement* time is older than the window, which this repository's own
   replay trace records at 40 s for a stalled link delivering a backlog. Left
   alone it is a permanent, invisible pin with no exit but `reset()`. So the
   evaluator now stops **awaiting** it when the comparison cannot be made,
   without touching a live one: refusing to be compared is not being exonerated,
   and what a second source gains by going uncomparable is only that it stops
   contradicting the local receiver — never that its own position is believed.
   Found in review of [#153](https://github.com/hleserg/Attadipa/pull/153).

   **And "stops being comparable" is a duration, not a frame** — the fourth
   review round, and the correction matters more than the bound. Keyed on one
   uncomparable frame, a node whose receiver went under canopy and kept relaying
   fix-less frames at 1 Hz was read as a node that had gone: the allegation was
   lifted about five seconds later, `Trusted` followed, and `remember()`
   committed the very coordinate the node was disputing as the fallback. No
   attacker, no hardware, the local receiver healthy throughout. A receiver
   losing its fix is the most **transient** of the three conditions this bound
   calls a departure — a doorway, a canopy, the node's own duty cycle — and on
   that path the retraction is not unreachable, only deferred: one frame with a
   fix and the comparison resumes. That is verbatim the argument §5.1 already
   uses to protect the *local* half, and the two halves had been given opposite
   treatments. The lift now requires the other side to have been unable to
   answer for longer than `provider_departure_grace` (`ESTIMATED`, 120 s), and
   the immediate exit for a node that really has left is `provider_detached()`,
   which is an edge somebody reports rather than a silence anybody interprets.

**And a pin freezes the fallback.** `remember()` runs only while `Trusted`, so a
pinned device stops updating its last trusted position while the uncertainty
around it grows at the configured rate — 1500 mm/s by default, 5.4 km per hour,
saturating rather than overflowing. That freeze used to be bounded by the 25 s
silent recovery, which is to say it was bounded by the defect. Nothing consumes
the value yet; when something does, it must read `has_last_trusted()` and the
uncertainty together rather than the position alone.

### 5.2 A side of a cross-provider comparison is a measurement, or it is nothing

> **Added 2026-08-24**, after the review of `6965191..8d757a7`
> ([#178](https://github.com/hleserg/Attadipa/issues/178)) found the local half
> of the comparison answering with a coordinate the local receiver had already
> disowned.

`ProviderDisagreement` is evidence about a **pair**, so it means something only
when both sides are answers to the same question. An answer is a *measurement*:
a coordinate the receiver solved for, and the instant it solved it. It is not
"the last thing that was in the position field".

**Those are different, and a receiver makes them different every time it goes
under a roof.** A
GNSS frame's position field is not emptied when the solution goes away — the
receiver keeps sending the coordinate it last solved for, with a fix type of
`NoFix` in the same frame saying there is no position at all. §2 above is the
rule that lets this be seen: the states do not collapse, so `PositionValidity`
is beside the coordinate rather than folded into it. The evaluator then held
**two models of the same observation's fitness**. The rate baselines read the
validity and refused retained state (issue #26); the local side of
`compare_provider()` took the last in-range position field unconditionally and
stamped it with the moment the frame was *processed*.

So at 1 Hz a receiver with no fix kept that side permanently fresh, and a node
that still had a fix — reporting the place the wearer had actually walked to —
was measured against a coordinate this device was no longer standing behind. The
difference was reported as `ProviderDisagreement`: 30 points, which reaches
`degrade_at` unaided, renewed for the whole dropout rather than for one epoch,
naming a conflict between two providers only one of which had a position. And it
outlives the dropout: `remember()` runs only while `Trusted`, so the fallback
position does not resume updating the moment the receiver recovers, and if the
node leaves in the meantime without anybody calling `provider_detached()`, the
fabricated allegation lapses into `unconfirmed_` where nothing can withdraw it —
the per-boot pin bound 3 above removes for real allegations, reached through one
that was never true.

**One direction of it was not fail-safe at all.** A node reporting the same
retained coordinate *agrees* with it — and agreement reaches `clear()`, which is
the only retraction this design has and the one thing that lets the state climb.
A coordinate nobody was asserting could therefore withdraw a live allegation
from a node that was still making it.

The rule, and it binds both sides even though only one of them can honour it
today: **only a `Valid` or `Degraded` measurement may seed a side, both sides
are judged by measurement age against the same window, and being unable to
compare withdraws nothing and asserts nothing.** A local
receiver that loses its fix stops answering; the next real fix reopens the
comparison and reports or clears in the ordinary way, which is exactly what §5.1
bound 3 already promised for the *other* side in the words *"one frame with a
fix and the comparison resumes"*. The code had read that as *any frame with a
coordinate*, which is the same failure as the one §5.1 records: a document
describing the intended behaviour beside an implementation that had only half of
it.

**What this does not close, so that it is a task rather than a silence.** The
second provider's frame arrives as a bare `GnssObservation`, which carries no
`PositionValidity` — that is a `classify()` verdict a caller reaches with a
policy — so a *node* relaying its own retained coordinate is still comparable,
and the fix above is one-sided until the call learns the other side's verdict.
`fix_type` is present in the frame and is not the same question. **T-154.**

### 6. The receiver's verdict is the strongest single input, and it is not the truth

Priority order, as OD-5 gives it: **receiver-native mechanisms → Attadipa's own
detectors → the fused state.** The receiver goes first because it can see things
we cannot — the RF front end, per-signal carrier-to-noise, the correlator. It
does not go last because it can be fooled, and because on one of our two
variants we do not yet know what it detects at all.

The independent detectors, fused with it:

- **motion disagreement** — the canonical case, and the reason the BMA423 is
  named in this ADR: *the position reports large movement while the
  accelerometer says the device is stationary.* A wrist device is unusually well
  placed to notice this, because a still wrist is genuinely still;
- **physical plausibility** — implied speed, acceleration and altitude rate
  against what a human body does;
- **clock disagreement** — GNSS time against the RTC's monotonic progression.
  A receiver that suddenly reports a different epoch is evidence, and this is
  where [T-047](../../TASKS.md)'s rule pays: the timeout that notices must be
  measured on the monotonic clock, or a spoofed time step breaks the detector
  that was supposed to catch it;
- **provider disagreement** — the on-board receiver against a node's position;
- **constellation anomalies** — satellites appearing with implausible
  uniformity of signal strength, or a constellation set that changes wholesale.

None of these is a spoofing detector. Together they are a reason to distrust,
which is all a wearable needs: the product decision is *"stop asserting"*, not
*"identify the attacker"*.

### 7. The diagnostic trace is bounded, controllable and replayable

Before any field testing there must be a trace that can be turned on, turned
off, bounded in size, and replayed into the trust engine offline. **Never an
unbounded log that can fill flash** — a diagnostic that bricks the device it was
diagnosing is not a diagnostic. Replayability is the part that makes the trust
engine testable at all: a captured field trace becomes a regression test, which
is the only honest way to develop a detector for an event we cannot stage.

### 8. What is not being built now

OD-5 §15, and it is emphatic. No Kalman filter, no RTS smoother, no pedestrian
dead reckoning, no second GNSS, no RTK, no DGNSS, no RTCM over LoRa, no map
matching, no HMM, no routing, no universal spoofing detector. The current
milestone is not to be broken.

This ADR is the fence. The receiver research (T-051, T-052), the simulator's
fault scenarios (T-053) and the implementation come later, in that order.

## Alternatives considered

**One `quality` or `confidence` scalar on the position.** It is what the final
specification's own conceptual output sketches (final §11 lists
`quality/confidence`), and it loses immediately on the case in §2: `Ready`, fixed,
fresh, accurate, and spoofed. Any scalar that can represent that has to be read
as a flag anyway, at which point it is a badly named flag. Note that final §11
*also* says «Separate: provider ready / position valid / position fresh. Do not
collapse them» — so the specification is arguing with itself, and this ADR takes
the half that survives contact with the failure mode.

**A boolean `gps_ok`.** Rejected in the amendment and rejected here for a reason
worth writing down: it forces the *policy* into the *detector*. Whether a
degraded fix is good enough depends on whether the user is looking at a map or
recording a track, and a boolean has already decided for both.

**RTCM on the `GnssDriver` interface.** §4. It makes every receiver that cannot
accept corrections implement a lie.

**Letting the time-to-live start the recovery hold** — i.e. treating a score
that has decayed to nothing as a clean bill of health. It is what the first
implementation did, and it is attractive because it needs no extra state and
guarantees the device always recovers eventually. Rejected on what "eventually"
means: the case where nothing is being reported is *exactly* the case where
nothing is known, and a watch that returns to `Trusted` because its receiver
went quiet has inverted the evidence. Guaranteeing recovery is not a property
worth having if the guarantee is unconditional — an unconditional recovery is a
timer wearing a state machine's clothes. §5.1.

**Trusting the receiver's spoofing flag alone.** It is the strongest single
input and it is absent on at least one of our two variants — and possibly on
both, since the LS550G's is `UNKNOWN`. A design that depends on it produces a
device whose safety property silently disappears depending on which
daughterboard was fitted at the factory. That is precisely the failure
[ADR-0003](0003-radio-not-lora.md) exists to prevent, in a different subsystem.

**Putting the receiver descriptor in `core::Capability`.** It would let an
application branch on the chip's defences, which is the one thing the whole
architecture is built to prevent. Rejected in §3.

**Computing trust in the application.** Then two applications disagree about
whether the same position is trustworthy, and the user sees a map that is sure
and a track that is not. Trust is a property of the observation, and it belongs
where the observation is made.

**Waiting for the receiver research before deciding any of this.** Tempting,
because six of the eight rules would benefit from knowing which part is fitted.
Rejected because none of them *depends* on it: "do not lose data at the
boundary", "do not collapse the states" and "corrections belong to a provider"
are true for any receiver, and writing them now is what keeps the research from
having to be re-done as a refactor.

## Consequences

**Easier.** A driver for the second receiver becomes an exercise in filling in a
descriptor and a normalization, rather than in discovering that the interface
was shaped around the first one. A field bug report becomes readable, because
the native values and the reason codes survived. And a node-supplied position
gets the same treatment as an on-board one for free, because trust is a property
of the observation rather than of the chip.

**Harder.** The observation type is wide, and wide types cost RAM on a device
that has to justify every kilobyte. The trust engine has state, hysteresis and a
log, none of which a boolean has. This is a real cost and it is accepted
knowingly: the alternative is a watch that points confidently in the wrong
direction, which for a navigation product is the one failure mode that matters.

**Committed to.** Every descriptor entry starts `UNKNOWN` and moves only on a
primary source, so this ADR creates an obligation to actually do T-051 and T-052
before the driver. The trace has to exist before field testing rather than after
the first unexplained report. And `LocationService` becomes the boundary that
nothing may see past — one more line the build will have to enforce rather than
the review, alongside the two that already exist.

**Not committed to.** Anything in §8. If this ADR is ever cited to justify a
Kalman filter, it is being misread.
