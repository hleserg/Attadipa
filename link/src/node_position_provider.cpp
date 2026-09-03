#include "attadipa/link/node_position_provider.h"

namespace attadipa::link {

core::Availability NodePositionProvider::availability() const
{
    const core::Availability link = companion_.status().availability;
    // A fault is the transport failing to come up, and it outranks anything a
    // stale coordinate could claim.
    if (link == core::Availability::Failed) return link;

    core::Position position{};
    core::MonotonicTime arrived{};
    if (companion_.node_position(position, arrived)) return core::Availability::Ready;

    // No coordinate this session: whatever the link says about itself is the
    // truthful answer, and there is nothing here that could improve on it.
    return link;
}

bool NodePositionProvider::sample(core::PositionSample& out) const
{
    core::Position position{};
    core::MonotonicTime arrived{};
    if (!companion_.node_position(position, arrived)) return false;

    out = core::PositionSample{};
    out.observation.position = position;
    // WHEN IT ARRIVED, WHICH IS NOT WHEN IT WAS OBSERVED. `GnssObservation` has
    // one time field and the node states no observation time, so the arrival
    // stamp is what goes in it. That would be a dangerous substitution if
    // anything read it as an observation time -- and exactly one thing reads it,
    // `classify()`'s staleness test, which this fix type never reaches because
    // `FixType::Unknown` returns `NoFix` several lines above it. The age a
    // consumer is actually shown is `age_at_us_ms`, which is what this stamp
    // honestly measures.
    out.observation.observed_at = arrived;
    out.observation.fix_type = core::FixType::Unknown;
    out.observation.source = core::PositionSource::NodeGnss;
    // Every other field stays default: no altitude, no speed, no course, no
    // accuracy, no dilution, no satellite counts, no receiver time, and both
    // interference indications `Unknown`. The node sends none of them, and an
    // empty optional is this tree's way of saying nobody measured it.
    out.receiver = companion_.node_receiver();
    out.has_origin = companion_.node_id(out.origin);
    return true;
}

}  // namespace attadipa::link
