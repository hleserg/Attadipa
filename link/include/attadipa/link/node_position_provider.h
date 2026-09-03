#pragma once

#include "attadipa/core/location_service.h"
#include "attadipa/link/meshcore_companion.h"

// The first `core::PositionProvider` this repository has.
//
// It lives in `link/` and not in `core/` for one reason: it knows a wire
// format. Bytes 36-43 of RESP_CODE_SELF_INFO are a MeshCore fact, and
// `core::LocationService` is not allowed to learn one. Everything above this
// class sees an availability, a validity and two ages.
//
// The whole class is a translation, and the interesting part of it is what it
// refuses to translate. The node sends two coordinates and nothing else -- no
// fix flag, no satellite count, no dilution, no observation time -- so every
// optional in the `GnssObservation` it builds stays empty and `fix_type` stays
// `FixType::Unknown`. Filling any of them with a plausible default would make
// the classifier answer a question nobody asked it.

namespace attadipa::link {

class NodePositionProvider final : public core::PositionProvider {
public:
    explicit NodePositionProvider(const MeshCoreCompanion& companion)
        : companion_(companion)
    {
    }

    // The node's availability *is* the position's availability: one link, one
    // answer, and no second state machine to disagree with it.
    //
    // Note what is absent. `Off` is never produced, and that is not an
    // oversight -- `core/include/attadipa/core/availability.h:22`
    // "Off,            // deliberately powered down; can be brought up"
    // describes a remedy this device can perform, and there is no remedy here
    // that reaches a receiver on the far side of a radio link. A node that
    // hands over a coordinate with its own GPS switched off is `Ready` and says
    // so through `ReceiverPresence::PoweredOff` instead, where it is a fact
    // about the coordinate rather than an instruction to the user.
    //
    // It is `Ready` as soon as the node has stated a coordinate, which happens
    // at RESP_CODE_SELF_INFO -- earlier than the mesh's own `Ready`, which also
    // waits for the contact list. That is not a looser rule, it is a different
    // question: a position does not need a contact list, and reporting it
    // `Unreachable` while holding a coordinate the node has just sent would be
    // a sentence about the wrong capability.
    core::Availability availability() const override;

    bool sample(core::PositionSample& out) const override;

private:
    const MeshCoreCompanion& companion_;
};

}  // namespace attadipa::link
