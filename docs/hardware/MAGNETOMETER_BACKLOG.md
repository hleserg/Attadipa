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
([OWNER_DECISIONS.md](../research/OWNER_DECISIONS.md) OD-16). It gets an
accelerometer and probably a gyroscope instead, for GNSS power optimisation, not
for heading — filed as its own capability question,
[#93](https://github.com/hleserg/Attadipa/issues/93), not resolved here. So the
paragraph this backlog used to carry about a node compass — a node in a
backpack or clipped to a belt measuring its own orientation, related to the
watch's by an unmeasured transform — no longer describes a live possibility.
ADR-0009 §3 still states the rule that would have applied if it had (a remote
heading is never presented as `WatchBody` heading without a calibrated
transform), because the rule generalises beyond this one dead path.

**A5 is answered, and the compass path is a retrofit, not a board fact.** An
external module is ordered for the Waveshare unit — a CJMCU-9911 (AK09911C) and
a GY-271 (QMC5883L), [#83](https://github.com/hleserg/Attadipa/issues/83),
researched in `docs/research/MAGNETOMETER_RETROFIT.md` in
[PR #87](https://github.com/hleserg/Attadipa/pull/87), not yet merged — but **placement is undecided**, tracked as **T-109**, and nothing
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
| G-02 | External sensor BSP | DESIGN | **Yes** — how an off-board sensor attaches at all. A5 is answered (OD-16); this no longer waits on the owner, only on placement (T-109) before the mapping half of G-03 can follow |
| G-03 | Axis mapping | DESIGN | Partly — the representation can be designed; the actual mapping needs a physical sensor in a physical case |
| G-04 | Calibration storage | DESIGN | **Yes** — format, versioning, where it lives, what invalidates it |
| G-05 | Calibration wizard | DESIGN | UI flow can be designed; it cannot be validated |
| G-06 | Hard-iron calibration | BLOCKED | needs a sensor |
| G-07 | Soft-iron calibration | BLOCKED | needs a sensor |
| G-08 | Haptic interference test | BLOCKED | needs a sensor **and** a board that has both — see below |
| G-09 | Speaker interference test | BLOCKED | same |
| G-10 | Charging interference test | BLOCKED | same |
| G-11 | Quiet-window scheduling | DESIGN | **Yes** — and it is worth doing, because the mechanism is not magnetometer-specific |
| G-12 | Heading confidence | DESIGN | **done in principle** — [ADR-0009](../adr/0009-heading.md) carries source, frame, confidence and validity; the *rendering* of low confidence is still UI work |
| G-13 | Sensor fusion evaluation | RESEARCH | reading and evaluation only; no data to fuse |

## The consequence nobody should skip past

The master plan's motivating example for the whole coexistence architecture is
**a vibration motor disturbing a compass**. On the T-Watch there is a vibration
motor and no compass. On the Waveshare board there is *also* a vibration motor —
a bare one on GPIO 18 through an NPN, with no driver IC
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)) — and also no compass. Both
boards have the buzz; neither has the thing it would disturb. So G-08, G-09 and
G-10 — the three interference tests — cannot be run on any hardware this project
currently targets, in any configuration.

That is not a reason to drop the coexistence architecture. Bus contention, rail
sharing and interrupt storms are all real on these boards and are covered in
[COEXISTENCE_BACKLOG](COEXISTENCE_BACKLOG.md). It *is* a reason to stop citing
haptics-versus-compass as the example that justifies it, and to be explicit that
the arbiter is being built for the contention that actually exists here.

## What would unblock this

| # | Question | Status |
|---|---|---|
| ~~A5~~ | ~~Is an external magnetometer intended at all?~~ | **RESOLVED — yes, for the watch, hardware ordered** — [OWNER_DECISIONS OD-16](../research/OWNER_DECISIONS.md) |
| ~~A6~~ | ~~Does the Attadipa node carry one?~~ | **RESOLVED — no, deliberately** — [OWNER_DECISIONS OD-16](../research/OWNER_DECISIONS.md). The node compass path this row used to gate is closed, not merely unavailable |
| G-14 | Which part, on which bus, at what address, on which rail? | answered for the part: CJMCU-9911 (AK09911C, `0x0C`) and GY-271 (QMC5883L, `0x0D`), both on the Waveshare main I2C bus with `CAD` tied to ground — `docs/research/MAGNETOMETER_RETROFIT.md` in [PR #87](https://github.com/hleserg/Attadipa/pull/87), not yet merged. Rail is still open |
| G-15 | Is it on the same I2C bus as the PMU and RTC? | **yes** — the Waveshare main I2C bus carries all fitted devices; this decides G-08–G-10 are measurable in principle once T-109 places the sensor |

The honest state of this backlog, now that A5 and A6 are answered rather than
open: of the thirteen epics above, seven are `DESIGN` kind (one, G-03, only
partly — the mapping itself needs a placed sensor), one is `RESEARCH`, and five
are `BLOCKED`, all five on a sensor that is ordered but not yet placed (T-109)
rather than on an owner decision. Recorded as such rather than left to look
like a plan in progress.
