#include "attadipa/core/position.h"

namespace attadipa::core {

PositionValidity classify(const GnssObservation& observation, MonotonicTime now,
                          const ValidityPolicy& policy)
{
    // No position, or a fix type that cannot carry one. TimeOnly is the
    // interesting case: the receiver is working, it has satellites, it can set
    // the clock — and it has no place to report. Calling that a bad position
    // would be wrong; it is not a position.
    if (!observation.position.has_value() || observation.fix_type == FixType::NoFix ||
        observation.fix_type == FixType::TimeOnly || observation.fix_type == FixType::Unknown) {
        return PositionValidity::NoFix;
    }

    // A coordinate outside the globe is not a stale or degraded position, it is
    // not a position. Hostile or corrupt input lands here rather than being
    // clamped into something plausible, because a clamped hostile coordinate is
    // indistinguishable from a real one two lines later.
    if (!in_range(*observation.position)) {
        return PositionValidity::NoFix;
    }

    // Freshness before quality. An excellent fix from ten minutes ago is not a
    // slightly worse fix, it is a different kind of answer: the device may have
    // moved arbitrarily far since, so the position is a circle whose radius
    // nobody measured. ADR-0011 §5 is where that circle grows.
    if (elapsed(observation.observed_at, now) >= policy.stale_after) {
        return PositionValidity::Stale;
    }

    // From here it is a real, current fix, and the question is only how much to
    // trust the numbers. Each test below is a *caveat the interface must show*,
    // never a reason to hide the position: a degraded fix is what a person under
    // a forest canopy has, and refusing to show it would be worse than showing
    // it with its uncertainty.

    if (observation.fix_type == FixType::TwoD) {
        return PositionValidity::Degraded;  // no altitude solution
    }
    if (observation.fix_type == FixType::DeadReckoning) {
        return PositionValidity::Degraded;  // propagated, not observed
    }

    // "Did not say" is not "is fine". A receiver that reports no satellite
    // count is a receiver we know less about, and less is a caveat.
    if (!observation.satellites_used.has_value() ||
        *observation.satellites_used < policy.min_satellites) {
        return PositionValidity::Degraded;
    }
    if (observation.horizontal_accuracy_mm.has_value() &&
        *observation.horizontal_accuracy_mm > policy.degraded_accuracy_mm) {
        return PositionValidity::Degraded;
    }
    if (observation.hdop_centi.has_value() && *observation.hdop_centi > policy.degraded_hdop_centi) {
        return PositionValidity::Degraded;
    }

    return PositionValidity::Valid;
}

const char* to_string(FixType type)
{
    switch (type) {
        case FixType::Unknown:       return "Unknown";
        case FixType::NoFix:         return "NoFix";
        case FixType::TimeOnly:      return "TimeOnly";
        case FixType::TwoD:          return "TwoD";
        case FixType::ThreeD:        return "ThreeD";
        case FixType::DeadReckoning: return "DeadReckoning";
    }
    return "?";
}

const char* to_string(PositionSource source)
{
    switch (source) {
        case PositionSource::Unknown:   return "Unknown";
        case PositionSource::LocalGnss: return "LocalGnss";
        case PositionSource::NodeGnss:  return "NodeGnss";
        case PositionSource::Companion: return "Companion";
        case PositionSource::Manual:    return "Manual";
        case PositionSource::Simulated: return "Simulated";
    }
    return "?";
}

const char* to_string(ReceiverIndication indication)
{
    switch (indication) {
        case ReceiverIndication::Unsupported: return "Unsupported";
        case ReceiverIndication::Unknown:     return "Unknown";
        case ReceiverIndication::None:        return "None";
        case ReceiverIndication::Warning:     return "Warning";
        case ReceiverIndication::Critical:    return "Critical";
    }
    return "?";
}

SensorBody body_of(PositionSource source)
{
    switch (source) {
        case PositionSource::LocalGnss: return SensorBody::Watch;
        case PositionSource::NodeGnss:  return SensorBody::Node;
        case PositionSource::Companion: return SensorBody::Companion;

        // Not a gap. A typed position was not measured on any object, so no
        // accelerometer anywhere is evidence about it; a simulated one is about
        // whichever body the fixture is simulating and has to say which through
        // a source that names one; and a source nobody set cannot be read as a
        // body nobody named — that reading is the whole defect ADR-0013 exists
        // to close.
        case PositionSource::Manual:
        case PositionSource::Simulated:
        case PositionSource::Unknown:   return SensorBody::Unknown;
    }
    return SensorBody::Unknown;
}

const char* to_string(PositionValidity validity)
{
    switch (validity) {
        case PositionValidity::NoFix:    return "NoFix";
        case PositionValidity::Stale:    return "Stale";
        case PositionValidity::Degraded: return "Degraded";
        case PositionValidity::Valid:    return "Valid";
    }
    return "?";
}

}  // namespace attadipa::core
