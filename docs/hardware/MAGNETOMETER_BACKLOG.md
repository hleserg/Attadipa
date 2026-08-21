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

It is available wherever GNSS is — which, since a Firefly node supplies GNSS, is
no longer only the T-Watch. And it only exists while the user is moving, which
is the part that makes this a different product rather than a lesser one:
**standing still is the normal condition of someone reading their watch**, and
it is therefore a designed UI state rather than an absence. It never renders as
0°.

If A6 comes back *the node has a magnetometer*, that still does not give the
watch a compass. A node in a backpack or clipped to a belt measures its own
orientation, related to the watch's by a transform nobody has measured and which
changes every time the node is set down. ADR-0009 §3 refuses the conversion
unless a calibrated, still-valid transform exists, and none does.

This does not make the backlog pointless — it makes it **design-only, and
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
| G-02 | External sensor BSP | DESIGN | **Yes** — how an off-board sensor attaches at all; gated on A5 below |
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
| A5 | Is an external magnetometer intended at all — a variant board, a daughterboard, a different unit? | **owner decision** — [OPEN_QUESTIONS A5](../research/OPEN_QUESTIONS.md) |
| A6 | Does the Firefly node carry one? If it does, it is a *node* compass, not a watch compass — see [ADR-0009](../adr/0009-heading.md) §3 | **owner decision** — [OPEN_QUESTIONS A6](../research/OPEN_QUESTIONS.md) |
| G-14 | If yes: which part, on which bus, at what address, on which rail? | conditional on A5; local to this backlog |
| G-15 | If yes: is it on the same I2C bus as the PMU and RTC? | conditional on A5. Decides whether G-08–G-10 are even measurable |

Until A5 has an answer, the honest state of this backlog is: **five epics can be
designed usefully, three are research, five are blocked on hardware that does not
exist yet.** Recorded as such rather than left to look like a plan in progress.
