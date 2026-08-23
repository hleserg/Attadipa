# Magnetometer backlog

The mandatory epics from master plan §67.

## The situation this backlog is in

**Neither target board has a magnetometer.**

- T-Watch S3 Plus: the only motion part on the schematic is a BMA423 — a
  three-axis accelerometer with no gyroscope and no magnetometer. Established by
  an exhaustive part search across all six schematic sheets, not by absence from
  a feature table ([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)).
- Waveshare AMOLED 2.06: QMI8658, a six-axis accelerometer plus gyroscope. No
  magnetometer.

So there is no *magnetic* heading on either board today, and there is nothing to
calibrate.

Course-over-ground from GNSS is not a substitute for it — it is a **different
quantity in a different reference frame**, and saying so precisely is the whole
of [ADR-0009](../adr/0009-heading.md). A magnetometer answers *which way is this
body pointing*; course-over-ground answers *which way is this body moving*. They
coincide only when the user walks forwards with the watch face aligned to their
path, which is an assumption about arm position that this project has not
measured. So course-over-ground is carried in frame `CourseOverGround`, never in
`WatchBody`, and it may not drive a wrist-relative arrow.

It is available wherever GNSS is — which, since an Attadipa node supplies GNSS, is
no longer only the T-Watch. And it only exists while the user is moving, which
is the part that makes this a different product rather than a lesser one:
**standing still is the normal condition of someone reading their watch**, and
it is therefore a designed UI state rather than an absence. It never renders as
0°.

**A6 is answered, and the node path is closed rather than merely unavailable:**
the Attadipa node will never carry a magnetometer — owner decision, 2026-08-22,
*"в нодах магнитометр реально лишний"*
([OWNER_DECISIONS.md](../research/OWNER_DECISIONS.md) OD-17). It gets an
accelerometer and probably a gyroscope instead, for GNSS power optimisation, not
for heading — filed as its own capability question,
[#93](https://github.com/hleserg/Attadipa/issues/93), not resolved here. So the
paragraph this backlog used to carry about a node compass — a node in a
backpack or clipped to a belt measuring its own orientation, related to the
watch's by an unmeasured transform — no longer describes a live possibility.
ADR-0009 §3 still states the rule that would have applied if it had (a remote
heading is never presented as `WatchBody` heading without a calibrated
transform) — **and it is not a dead path, which matters most to whoever
implements G-02.** The owner's decision is about *this project's* node; OD-7
makes the companion **any** node, and whether a third-party MeshCore device
carries a magnetometer is `UNKNOWN`. So `HeadingSource::RemoteSensor` and its
four-condition gate are the code that runs when one turns up, not scaffolding
around a possibility that has been closed. An earlier version of this paragraph
called it "this one dead path", which is exactly the sentence that would make an
implementer delete the gate.

**A5 is answered, and the compass path is a retrofit, not a board fact.** An
external module is ordered for the Waveshare unit — a CJMCU-9911 (AK09911C) and
a GY-271 (QMC5883L), [#83](https://github.com/hleserg/Attadipa/issues/83),
researched in [MAGNETOMETER_RETROFIT](../research/MAGNETOMETER_RETROFIT.md)
([#87](https://github.com/hleserg/Attadipa/pull/87), merged) — but **placement is undecided**, tracked as **T-109**, and nothing
below gets built from a part that has not been placed. This is a fact about one
physical unit, not about the `ESP32-S3-Touch-AMOLED-2.06` board type: a stock
board still has no magnetometer, and the firmware must run correctly on a stock
board — so this backlog does not become pointless, it stays **design-only, and
honest about why**. The capability model already treats the magnetometer as a
first-class absence ([ADR-0007](../adr/0007-two-capability-layers.md)), which is
what lets the rest of the system be written now and a sensor be added later
without reshaping anything.

What it does mean: **no epic below that requires a physical magnetic reading can
start.** Marking any of them "done" from simulated data would be exactly the
fake-green result the project forbids.

## Backlog

| # | Epic | Kind | Can start now? |
|---|---|---|---|
| G-01 | Magnetometer capability API | DESIGN | **Yes** — ADR-0001 covers presence and degree; this is the sensor-facing side |
| G-02 | External sensor BSP | DESIGN | **Yes** — how an off-board sensor attaches at all. A5 is answered (OD-17); this no longer waits on the owner, only on placement (T-109) before the mapping half of G-03 can follow — and T-109's own part-choosing measurement waits on a driven magnetic source this unit does not have (T-097, or T-105) |
| G-03 | Axis mapping | DESIGN | Partly — the representation can be designed; the actual mapping needs a physical sensor in a physical case |
| G-04 | Calibration storage | DESIGN | **Yes** — format, versioning, where it lives, what invalidates it |
| G-05 | Calibration wizard | DESIGN | UI flow can be designed; it cannot be validated |
| G-06 | Hard-iron calibration | BLOCKED | needs a sensor |
| G-07 | Soft-iron calibration | BLOCKED | needs a sensor |
| G-08 | Haptic interference test | BLOCKED | placement (T-109) **and a vibration motor that is not fitted** — the pads on this unit are bare, T-097. Placing the sensor does not unblock it. **And T-105 can invert this reason**: `HARDWARE_MATRIX.md` records the Speaker row as `CONFLICTING` because a parallel reading of the same unit calls `AAC210602A1` a haptic actuator, and AAC makes both. If T-105 traces the pads and lands on *actuator*, this unit has a haptic source after all and *no motor is fitted* becomes false |
| G-09 | Speaker interference test | BLOCKED | placement (T-109), **a rail** (G-14), the module pull-ups, the bus hazard **T-130** (*not* T-096, which asks how a **node** attaches, not what a soldered seventh device does to this bus), **and T-105**. What is `VERIFIED` about `AAC210602A1` is its marking, mounting and wiring — **not its function**: `HARDWARE_MATRIX.md`'s Speaker row reads `CONFLICTING`, *"a parallel reading of the same unit calls this part a haptic actuator… AAC makes both, so the marking does not decide it… Resolved by tracing the pads"*. If it is an actuator there is no speaker to disturb anything, and this row's premise is gone |
| G-10 | Charging interference test | BLOCKED | placement (T-109), **a rail** (G-14), the module pull-ups, and **T-130**. The charge path exists but is **not characterised** — this test establishes its own disturbing current |
| G-11 | Quiet-window scheduling | DESIGN | **Yes** — and it is worth doing, because the mechanism is not magnetometer-specific |
| G-12 | Heading confidence | DESIGN | **done in principle** — [ADR-0009](../adr/0009-heading.md) carries source, frame, confidence and validity; the *rendering* of low confidence is still UI work |
| G-13 | Sensor fusion evaluation | RESEARCH | reading and evaluation only; no data to fuse |

## The consequence nobody should skip past

The master plan's motivating example for the whole coexistence architecture is
**a vibration motor disturbing a compass**. On the T-Watch there is a vibration
motor and no compass. On the Waveshare board there is a vibration motor **as a
circuit and not as a part**: a bare footprint on GPIO 18 through an NPN, with no
driver IC ([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)) — and the received
unit has **no motor fitted at all**, `OBSERVED` at
[WAVESHARE_BOARD_RECEIVED](../research/WAVESHARE_BOARD_RECEIVED.md) §1.7. That
is the schematic-against-unit distinction this document draws everywhere else,
and an earlier version of this paragraph dropped it.

**The sentence that followed is superseded in part, and is kept rather than
deleted.** It read *"G-08, G-09 and G-10 — the three interference tests — cannot
be run on any hardware this project currently targets, in any configuration"*,
which was true while neither board had a magnetometer and none was on order. A5
is now answered and a sensor is going into the Waveshare unit (OD-17). What
survives is **G-08 alone**: the haptic pair still has no motor to disturb
anything, on the one unit the sensor is going into. G-09 and G-10 have their
disturbing source on the unit and are blocked on things this project can do.
[*What would unblock this*](#what-would-unblock-this) below is the authority
wherever it and this paragraph disagree.

That is not a reason to drop the coexistence architecture. Bus contention, rail
sharing and interrupt storms are all real on these boards and are covered in
[COEXISTENCE_BACKLOG](COEXISTENCE_BACKLOG.md). It *is* a reason to stop citing
haptics-versus-compass as the example that justifies it, and to be explicit that
the arbiter is being built for the contention that actually exists here.

## What would unblock this

| # | Question | Status |
|---|---|---|
| ~~A5~~ | ~~Is an external magnetometer intended at all?~~ | **RESOLVED — yes, for the watch, hardware ordered** — [OWNER_DECISIONS OD-17](../research/OWNER_DECISIONS.md) |
| ~~A6~~ | ~~Does the Attadipa node carry one?~~ | **RESOLVED — no, deliberately** — [OWNER_DECISIONS OD-17](../research/OWNER_DECISIONS.md). The node compass path this row used to gate is closed, not merely unavailable |
| G-14 | Which part, on which bus, at what address, on which rail? | answered for the part: CJMCU-9911 (AK09911C, `0x0C`) and GY-271 (QMC5883L, `0x0D`), both on the Waveshare main I2C bus. **The `CAD` strap belongs to the AK09911C alone** — it is what puts that part at `0x0C` rather than `0x0D`; the QMC5883L has no address-select pin at all, so its address is not a choice ([MAGNETOMETER_RETROFIT](../research/MAGNETOMETER_RETROFIT.md) §3.4 for the QMC5883L's address — which that section declines to call *fixed*, the datasheet stating `0x0D` without a strap rather than guaranteeing no variant differs — and §2.4 for the AK09911C's `CAD` strap table). Rail is still open |
| G-15 | Is it on the same I2C bus as the PMU and RTC? | **yes — and that is a hazard as much as an answer.** The Waveshare main I2C bus carries every fitted device, so the retrofit is the seventh on it, and a seventh that holds `SDA` low takes the AXP2101 with it — [WAVESHARE_BOARD_RECEIVED](../research/WAVESHARE_BOARD_RECEIVED.md) §1.5, filed as **T-130**. T-096 was cited here and answers a different question — it scopes a *detachable node*, and a soldered sensor shares the stuck-slave failure but not the detachability, so T-096 can close in full without this ever being asked. [MAGNETOMETER_RETROFIT](../research/MAGNETOMETER_RETROFIT.md) §4.3 clears the *address* conflict and not this one. What this settles is that no second bus has to be found; what it leaves open is the rail, the module pull-ups and **T-130**. The *“seventh”* count assumes T-096 does **not** land on I2C; if it does, it is eight |

The honest state of this backlog, now that A5 and A6 are answered rather than
open: of the thirteen epics above, seven are `DESIGN` kind (one, G-03, only
partly — the mapping itself needs a placed sensor), one is `RESEARCH`, and five
are `BLOCKED` — but **not all five on the same thing**, and an earlier version of
this paragraph said they were.

- **Two are blocked on placement alone (T-109)** — a sensor that is ordered and
  not yet in the unit: G-06 and G-07, the two calibration epics. They are
  sensor-only: a magnetometer held still and turned, with nothing else on the
  board needing to be decided first. **Placement itself is not the floor**,
  though: T-109 chooses *which part* from the field at the mounting position
  with the motor driven, and this unit has no motor fitted (T-097). Until T-097
  or T-105 lands, a motor-idle survey can be recorded but cannot choose the
  part — the QMC's ±800 µT ceiling is exactly what a driven motor magnet
  threatens, so a survey without one finds nothing and reads as if it had
  looked. Found in review.
- **Two more — G-09 and G-10 — need placement AND a rail AND the bus.** An
  earlier version of this bullet folded them in with the calibration epics as
  blocked on placement alone, three lines under a G-14 row that says **"rail is
  still open"**. G-09 is the *speaker* test and G-10 the *charging* test
  ([the table above](#backlog)), mapping to *Audio amplifier × Magnetometer* and
  *Battery charging × Magnetometer* in
  [INTERFERENCE_MATRIX](INTERFERENCE_MATRIX.md). Neither disturbing source is
  the vibration motor and both are on the unit: the speaker is `VERIFIED` —
  `AAC210602A1`, a metal-can micro-speaker in the back cover
  ([WAVESHARE_BOARD_RECEIVED](../research/WAVESHARE_BOARD_RECEIVED.md) §1.8) —
  and the AXP2101 charge path exists, §1.2 recording where the battery connects.
  **It is not characterised**, and an earlier version of this bullet cited
  *"§1.3"* for a characterisation; §1.3 is the flash section, and §1.2 explicitly
  declines to give a current figure. G-10 therefore establishes its own
  disturbing current as part of its method rather than citing one.

  What placement does **not** settle for these two:

  1. **The rail** — G-14 below, still open. An unpowered sensor measures nothing,
     and a sensor sharing a rail with the subsystem it is measuring returns a
     confounded number that comes out labelled `MEASURED` and cannot be
     retracted.
  2. **Pull-ups — and the gate is not the one this list used to name.**
     [MAGNETOMETER_RETROFIT](../research/MAGNETOMETER_RETROFIT.md) records
     *"open-drain, external pull-ups required"* from the AK09911C's I2C
     electrical specification. That is a **datasheet fact about the die**, true
     of nearly every I2C slave, and it is already satisfied: the Waveshare main
     bus runs six working devices, so pull-ups exist on it. It settles nothing
     and it is not an open question.

     The real risk points the other way and was stated nowhere until review said
     so. **CJMCU-9911 and GY-271 are breakout modules, and breakout modules
     commonly populate their own pull-ups.** Fitting one — or both, which
     `MAGNETOMETER_RETROFIT` §4.3 contemplates — puts those in **parallel** with
     the bus resistors already there, lowering the effective pull-up and raising
     the sink current on a bus the AXP2101 shares. Whether either module is
     populated is **`UNKNOWN`**: the retrofit's own source table calls its
     module evidence *"about **labels**, not about **nets**"*. Measure it or read
     the module schematic; the die's requirement does not answer it. Part of
     **T-130**.
  3. **The bus** — the sensor is the **seventh** device on the Waveshare main
     I2C bus. `WAVESHARE_BOARD_RECEIVED` §1.5 lists six and warns that a seventh
     holding `SDA` low takes the AXP2101 with it: **T-130**. Fitting both
     candidate modules at once, which `MAGNETOMETER_RETROFIT` plans, makes
     eight.
- **One — G-08, the haptic test — is blocked on placement *and* on a vibration
  motor that is not there.** The sensor is going into the Waveshare unit
  (OD-17), and `WAVESHARE_BOARD_RECEIVED` §1.7 records `OBSERVED`: *"There is no
  vibration motor on this unit."* The pads are bare. So placing the magnetometer
  does not unblock this one — it leaves it with a compass and still nothing to
  disturb it, which is exactly the state
  [*The consequence nobody should skip past*](#the-consequence-nobody-should-skip-past)
  above describes for G-08. (That reference read `§"What this backlog is not"`,
  a heading which does not exist anywhere in `docs/`.)

An earlier version of this paragraph said two and three, folding G-09 and G-10
in with G-08 because all three are "the interference tests". Found in review,
and it is the mirror image of the failure named below: it parks two runnable
measurements behind a soldering job they never needed, on the pairing most
likely to actually bite — a speaker magnet sitting beside a magnetometer inside
a closed case.

The distinction is the point of writing it down, in both directions. An agent
who believes placement was the last gate runs **G-08** on a unit with no motor,
sees no interference, and writes `PASS` — a fake green reached through the file
that exists to prevent it. Fitting a motor to `P1`/`P2` is T-097 and is a
separate physical change.

Recorded as such rather than left to look like a plan in progress.
