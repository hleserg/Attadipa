# 0011 — GNSS integrity: the receiver's own defences, and a trust state with reasons

Status: accepted
Date: 2026-08-21
Amended: 2026-08-23 — **an eleventh axis, co-location**, added to §2's table by
[ADR-0009](0009-heading.md) §3a. The rule §2 states is unchanged; the register
of axes was incomplete, and a reader consulting this table alone would reach for
exactly the three homes §3a rules out. Found in review.

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
| co-location | is this position about the body wearing this device, or about a different one — [ADR-0009](0009-heading.md) §3a, a field of its own beside [`PositionSource`](../../core/include/attadipa/core/position.h), defaulting to `Unknown` |

**Eleven, and the last one arrived from the other side.** A position can be
fresh, accurate, high-integrity and unspoofed and still be about a node in a bag
by the door rather than about the wearer — a quantity none of the ten above can
express, and one that no receiver reports because it is not a property of the
receiver. [ADR-0009](0009-heading.md) §3a decides where it lives and what it
withholds; it is recorded here because this table is the register an
implementer consults, and the three wrong homes §3a rules out —
`PositionValidity`, **`TrustState`**, a `TrustReason` bit — are precisely what a
reader of this section alone would reach for. An earlier version of this
sentence listed `Availability` in place of `TrustState`, which dropped the one
of the three that had actually been in this ADR once and had to come out, and
substituted an axis §3a does not discuss. Found in review, twice.

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
