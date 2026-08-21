# 0009 — Heading is three quantities, and one of them belongs to a different body

Status: **accepted**
Date: 2026-08-21

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
So the answer for the foreseeable future is: **a node's heading is displayed as
node orientation in diagnostics, and is not used for the user-facing arrow.**

If A6 comes back "the node has a magnetometer", this is the paragraph that
prevents the obvious wrong implementation. That is the whole reason this ADR is
being written before the answer arrives.

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
actually be in.

**Testable.** In the simulator: scripted heading and scripted GNSS, including
zero speed, a speed ramp across the gate, a node attaching with a `NodeBody`
heading, and a stale heading. The assertion that matters: **no configuration of
inputs causes a wrist-relative arrow to be drawn from a `NodeBody` or
`CourseOverGround` source.** On hardware: `NOT EXECUTED — HARDWARE REQUIRED`.

**Open.** **H10** — the speed gate, per GNSS module. **A5/A6** — whether a
magnetometer is intended at all, and whether the node carries one. Whether
`RemoteSensor` heading is worth surfacing in Diagnostics before any of that is
answered (probably yes; it is nearly free and it makes the frame distinction
visible to whoever implements the transform later).
