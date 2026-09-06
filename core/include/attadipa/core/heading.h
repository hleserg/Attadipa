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
// Three conditions, and dropping any one of them draws an arrow that points
// somewhere the wearer is not being sent:
//
//   * the frame is `WatchBody`. ADR-0009 §3 — a node's compass is the node's
//     compass, and a perfectly valid `NodeBody` angle at confidence 100 still
//     says nothing about which way this wrist is turned;
//   * the validity is `Valid`. `Uncalibrated` has a number and no reason to
//     believe it, and `Stale` had one;
//   * the confidence clears the caller's floor. ADR-0009 §6 leaves that
//     threshold to the renderer rather than fixing it here;
//   * the source is stated. This one is not in the ADR's list, and it is here
//     because `WatchBody` is the *default* frame — ADR-0009 §2 lists it first
//     and this enum keeps that order — so a producer that fills the validity
//     and the confidence and forgets the frame gets the one frame an arrow may
//     use, silently. `HeadingSource::Unknown` is the default that catches that,
//     and refusing on it is safe to add to an accepted ADR because it only ever
//     narrows: the ADR enumerates when an arrow *may* be drawn, and a fourth
//     condition can refuse a heading it allowed but can never allow one it
//     refused. A frame that says "nobody stated one" would be the direct fix
//     and it is an amendment to argue in the ADR, not here.
constexpr bool can_orient(const Heading& heading, std::uint8_t min_confidence)
{
    return heading.frame == ReferenceFrame::WatchBody &&
           heading.source != HeadingSource::Unknown &&
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
