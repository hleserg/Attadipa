#pragma once

#include <cstdint>

// Which way something is pointing, and which body that is a statement about.
//
// The whole model is [ADR-0009](../../../../docs/adr/0009-heading.md), accepted
// on 2026-08-21 and until now implemented by nothing. This header is that
// decision as code and adds no decision of its own; where it departs from the
// ADR's sketch the comment says so and why.
//
// There is no `HeadingProvider` here, and that is deliberate. `PositionProvider`
// arrived in the same commit as `link::NodePositionProvider`, the first thing
// that implemented it; no magnetometer is fitted to either board, the
// simulator builds its `NavState` by hand rather than through a provider, so an
// interface added now would have no implementation and no caller. It arrives
// with the driver that reads a real part.

namespace attadipa::core {

// Where the angle came from. It is carried rather than inferred because two of
// these are not interchangeable with the other three: a course over ground is
// only a heading while the wearer is moving, and a remote sensor is a heading
// about somebody else's body.
enum class HeadingSource : std::uint8_t {
    Unknown,
    Magnetometer,
    SensorFusion,
    GnssCourseOverGround,
    RemoteSensor,
};

// What the angle is a statement about. ADR-0009 §2 — "Heading carries its
// frame, and the frame is not decorative".
//
// This is not `SensorBody` in `motion.h` and does not replace it. That enum
// answers "which physical body was this measured on" and has a `Companion`
// value; this one answers "is this a body orientation at all", and
// `CourseOverGround` — the direction of travel, which no body's orientation
// has to agree with — is why the two cannot be one type.
enum class ReferenceFrame : std::uint8_t {
    WatchBody,        // the way the watch case is pointing: the only frame an arrow may use
    NodeBody,         // the way some other node is pointing. ADR-0009 §3
    CourseOverGround, // the direction of travel, which needs motion to exist
};

// How actionable the angle is, worst first, so that "at least Uncalibrated" is
// a comparison rather than a switch. The same shape and the same reason as
// `PositionValidity` in `position.h`, and a separate enum for the same reason:
// `core::Validity` has four values and none of them can say "there is a number
// and no reason to believe it".
//
// ADR-0009 spells the field `Validity` and lists these five values inside a
// heading struct. The name here is `HeadingValidity` because `core::Validity`
// is already taken by a different four-value enum, which is exactly the
// collision `PositionValidity` avoided first.
enum class HeadingValidity : std::uint8_t {
    Invalid,       // an answer arrived and it cannot be one: saturated, or out of range
    NoMotion,      // a course over ground with nothing moving. ADR-0009 §4, a designed state
    Stale,         // there was a believable angle and it is too old to act on
    Uncalibrated,  // there is a number and no reason to believe it. Drawn, marked
    Valid,
};

// The angle itself.
//
// Not a `Timed<std::uint16_t>`, though `LocationState` pairs `Timed<Position>`
// with a `PositionValidity` and this would mirror it. `Timed<T>` carries a
// second age and a second validity, and in `LocationState` both are dead: the
// inner `validity` is written `Validity::Unknown` and read by one test that
// asserts it stays that way. Copying that here would ship two fields that no
// producer can fill. `age_at_source_ms` arrives with the first producer that
// states when it sampled — the same rule 3 `location_service.h` states for
// positions, and equally true of an angle that crossed a radio link. The
// arrival age is absent for the same reason and not a different one: no
// producer exists to stamp it, nothing reads it, and freshness is already
// expressed where a consumer acts on it — `HeadingValidity::Stale`, which the
// producer sets and `can_orient()` refuses. It arrives with that producer, next
// to the readout that shows it.
struct Heading {
    // 0 .. 35999, clockwise from **true** north. Integer, not float: a tenth of
    // a degree is finer than any magnetometer this device will carry, and a
    // float invites a comparison for equality that never holds.
    std::uint16_t   centideg = 0;

    HeadingSource   source   = HeadingSource::Unknown;
    ReferenceFrame  frame    = ReferenceFrame::WatchBody;

    // 0..100, and 0 is legal and means the value exists and is worthless.
    // ADR-0009 §6: the number is always carried, because Diagnostics needs it,
    // and it is the renderer that decides what to do with a low one.
    std::uint8_t    confidence = 0;

    HeadingValidity validity = HeadingValidity::Invalid;
};

// Whether this heading may turn a north-up bearing into a wrist-relative arrow.
//
// Four conditions, and dropping any one of them draws an arrow that points
// somewhere the wearer is not being sent:
//
//   * the frame is `WatchBody`. ADR-0009 §3 — a node's compass is the node's
//     compass, and a perfectly valid `NodeBody` angle at confidence 100 still
//     says nothing about which way this wrist is turned;
//   * the validity is `Valid`. `Uncalibrated` has a number and no reason to
//     believe it, and `Stale` had one;
//   * the confidence clears the caller's floor. ADR-0009 §6 leaves that
//     threshold to the renderer rather than fixing it here;
//   * the source is one that can measure *this* case's orientation, which is
//     `Magnetometer` or `SensorFusion` and nothing else.
//
// That fourth one is not in the ADR's list of three, and it is the one
// that carries the ADR's own assertion. `WatchBody` is the *default* frame —
// ADR-0009 §2 lists it first and this enum keeps that order — and `frame` is
// the single field a driver has no local evidence for: it states which body the
// driver is bolted to. A course-over-ground producer, or a node's compass
// arriving over the link, sets a real source, sets a real confidence, sets
// `Valid`, and leaves the one field it cannot know — and gets the one frame an
// arrow may use. ADR-0009's Testable clause is written against exactly that:
// "no configuration of inputs causes a wrist-relative arrow to be drawn from a
// `NodeBody` or `CourseOverGround` source." Naming the two sources that can
// produce a `WatchBody` heading is that assertion, in the one place every
// caller passes through.
//
// Testing the *source* rather than the frame is deliberate. Refusing an unset
// frame needs a `ReferenceFrame::Unknown` the ADR does not have, and that is an
// amendment to argue there. A whitelist of sources needs nothing: it only ever
// narrows, and the ADR enumerates when an arrow *may* be drawn, so a further
// condition can refuse a heading the ADR allowed and can never allow one it
// refused.
// AND `Magnetometer` HERE MEANS AN ANGLE ALREADY CORRECTED TO TRUE NORTH.
// `centideg` above says true north and a magnetometer measures magnetic north;
// the difference is declination, it needs a position and a field model, and no
// field of this struct can say which of the two an angle is. That gap is this
// repository's own open question — MAGNETOMETER_RETROFIT.md Q10, "How does a
// heading say 'magnetic north, declination unknown'?", whose answer is "either
// the model gains the distinction or the Navigator must not label the arrow" —
// and it is Q10's amending ADR that closes it, not this header.
//
// What this header can do is refuse to let the whitelist above quietly answer
// it. A driver that cannot correct to true north **must not report `Valid`**,
// and the state for that is `Invalid` — "an answer arrived and it cannot be
// one" — because an angle measured from magnetic north cannot be the true-north
// angle `centideg` promises. Deliberately **not** `Uncalibrated`: that word has
// a remedy attached to it in an accepted ADR, `docs/adr/0009-heading.md` §5
// row 7 offering the wearer the calibration entry point, and a compass with a
// perfectly good calibration record on a watch that simply has no position fix
// would then be sent to a wizard that cannot give the arrow back. Somebody
// doing the wrong thing is worse than somebody doing nothing. Either way the
// refusal costs the arrow rather than the wearer. The
// consequence of getting this wrong is not subtle. The needle would be drawn
// from magnetic north beside a printed bearing measured from true north, both
// unlabelled, and local anomalies run past ten degrees.
constexpr bool can_orient(const Heading& heading, std::uint8_t min_confidence)
{
    return heading.frame == ReferenceFrame::WatchBody &&
           (heading.source == HeadingSource::Magnetometer ||
            heading.source == HeadingSource::SensorFusion) &&
           heading.validity == HeadingValidity::Valid &&
           heading.confidence >= min_confidence;
}

// A north-up bearing seen from a body turned `heading` away from north.
//
// Both are centidegrees clockwise from true north, and the result is the angle
// to turn through: 0 is straight ahead. The addition of a full turn before the
// modulus is what keeps an unsigned subtraction from wrapping to ~65500 when
// the bearing is west of the heading.
constexpr std::uint16_t relative_bearing(std::uint16_t bearing_centideg,
                                         std::uint16_t heading_centideg)
{
    return static_cast<std::uint16_t>(
        (bearing_centideg % 36000U + 36000U - heading_centideg % 36000U) % 36000U);
}

const char* to_string(HeadingSource source);
const char* to_string(ReferenceFrame frame);
const char* to_string(HeadingValidity validity);

}  // namespace attadipa::core
