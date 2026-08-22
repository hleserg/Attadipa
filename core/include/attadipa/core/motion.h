#pragma once

#include <cstdint>

// Whether something is moving, and — the part this header exists for — *what*.
//
// docs/adr/0013-node-motion.md. Motion is a fact about one physical object, and
// this system routinely holds two at once: a watch on a wrist and an Attadipa
// node that may be in a bag, on a table, or in somebody else's hands. A motion
// sample with no subject is not a smaller fact than one with a subject, it is a
// different and unusable one, because every consumer of it is really asking
// about a particular object.
//
// The rule, from OD-16 by way of ADR-0009 §3: a sensor may correct another
// reading taken on the same body, and may not be presented as a reading from a
// different one. Heading is that rule's far side — a node's compass is never
// the watch's orientation. Motion is its near side: an IMU may gate or correct
// the GNSS bolted to the same chassis, and may do nothing at all for a receiver
// on a different one.

namespace attadipa::core {

// Whose motion, whose receiver, whose chassis.
//
// `Unknown` is a fourth state rather than a default worth defaulting to: a
// sample that claims to know something must know whose. `Companion` is here
// because a phone is a third body and there is no phone IMU — so evidence for
// it can be named and can never be supplied, which is the truthful shape.
enum class SensorBody : std::uint8_t {
    Unknown,    // nobody said. Never read as "the watch"
    Watch,      // this device's own chassis — the wrist
    Node,       // an Attadipa node's chassis
    Companion,  // a phone
};

inline constexpr std::uint8_t kSensorBodyCount =
    static_cast<std::uint8_t>(SensorBody::Companion) + 1;

// What an accelerometer has to say, and about which object.
//
// Three values, not two: "the device is not moving" and "nobody asked the
// accelerometer" are different facts and only the first is evidence. The
// distinction is not decorative — the two are told apart at exactly the places
// where confusing them would either power a receiver down on nothing or accuse
// a correct fix of lying.
//
// The T-Watch's BMA423 is an accelerometer: no gyroscope, no magnetometer. It
// is exactly the right part for this one question and it is not an inertial
// navigation system; nothing here may quietly become dead reckoning
// (ADR-0009, ADR-0011 §6 and §8). The node's part is a 6-axis IMU (OD-16) and
// answers the same one question for its own chassis.
// The body comes first, and that ordering is the enforcement rather than a
// preference. `MotionEvidence{true, false}` — the two-field literal this type
// used to have, meaning "known, still, about nobody" — no longer compiles, and
// cannot be revived by accident: `bool` does not convert to a scoped enum. So
// every literal in the tree had to name a subject to keep building, which is
// ADR-0007's rule for `has()` applied to a struct instead of a function: do not
// leave a shape that survives the change by meaning something subtly different.
struct MotionEvidence {
    SensorBody body   = SensorBody::Unknown;
    bool       known  = false;
    bool       moving = false;

    // Is this evidence about `about`, at all?
    //
    // False for a sample nobody took, for a sample about a different object,
    // and for a question about no object in particular. Every consumer goes
    // through here, so "the wrong body" and "no sample" reach the same code
    // path and produce the same behaviour — which is the decision, not an
    // implementation convenience: evidence about another body is refused
    // rather than approximated (ADR-0013 §3).
    constexpr bool speaks_for(SensorBody about) const
    {
        return known && about != SensorBody::Unknown && body == about;
    }

    // Same-body evidence that the thing is at rest. This is the only
    // conjunction that may make a receiver sleep.
    //
    // Nothing here reads a clock, so nothing here can expire: `known` means
    // *somebody currently asserts this*, and clearing it when a sample gets too
    // old is the producer's job. That obligation is real and is not yet
    // discharged anywhere — no motion service exists (T-080) — so it is stated
    // rather than assumed. It matters in one direction more than the other: a
    // stale "moving" costs charge, and a stale "still" silently stops a device
    // asking where it is (ADR-0013 §2).
    constexpr bool says_at_rest(SensorBody about) const
    {
        return speaks_for(about) && !moving;
    }

    // ...and the only one that may wake it. Deliberately not the negation of
    // the above: everything neither says is *not known*, and not known moves
    // nothing in either direction (ADR-0013 §2).
    constexpr bool says_in_motion(SensorBody about) const
    {
        return speaks_for(about) && moving;
    }

    // A sample that knows something must know whose. This combination is the
    // shape of the bug this type exists to prevent; it is refused wherever it
    // could do harm, and named here so a test can assert nothing produces one.
    constexpr bool is_coherent() const { return !known || body != SensorBody::Unknown; }
};

const char* to_string(SensorBody body);

}  // namespace attadipa::core
