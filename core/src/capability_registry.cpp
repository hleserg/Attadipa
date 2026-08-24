#include "attadipa/core/capability_registry.h"

#include "attadipa/platform/hardware_inventory.h"

// The single place availability is computed.
//
// This file is the only one in core/ that includes a platform header, and
// attadipa_core links attadipa_platform PRIVATE so that the include does not
// travel any further. docs/adr/0007-two-capability-layers.md §4 and §5.

namespace attadipa::core {
namespace {

using platform::HardwareFeature;
using platform::HardwareState;

// What *a* Attadipa node can provide, as a matter of the node's design rather
// than of any particular node being attached.
//
// This constant is what makes Unprovisioned distinguishable from Unsupported
// with nothing paired, and therefore what keeps supports() stable at runtime —
// the property ADR-0007 §3 asks for by name. Without it, a Waveshare board
// would report Position as Unsupported until a node appeared, the Navigator
// would vanish from and reappear in the launcher, and "could this device ever?"
// would have become "is something plugged in?".
//
// Heading is in the list, and the frame is the reason it is allowed to be: a
// node reports course-over-ground, which is a property of a track and not of
// anybody's body. A node's *magnetometer* heading is NodeBody and stays there —
// docs/adr/0009-heading.md refuses to present it as the watch's orientation.
constexpr std::uint16_t kNodeProvidable =
    capability_bit(Capability::Position) | capability_bit(Capability::Heading) |
    capability_bit(Capability::MeshMessaging) | capability_bit(Capability::Time);

// How a driver state reads as an availability.
//
// Untouched and RailOff both become Off, which is right: the user-facing
// sentence for each is "it is not running, and it can be started". Initialising
// also lands on Off, and that is the one imprecise mapping here — the honest
// sentence would be "wait". Availability has no state for "wait" because
// waiting is not a remedy, and a service that is coming up reports progress
// through its own loading state rather than through this enum.
Availability from_state(HardwareState state)
{
    switch (state) {
        case HardwareState::Absent:       return Availability::Unsupported;
        case HardwareState::Untouched:    return Availability::Off;
        case HardwareState::RailOff:      return Availability::Off;
        case HardwareState::Initialising: return Availability::Off;
        case HardwareState::Failed:       return Availability::Failed;
        case HardwareState::Ready:        return Availability::Ready;
    }
    return Availability::Unsupported;
}

}  // namespace

CapabilityRegistry::CapabilityRegistry(const platform::HardwareInventory& inventory)
    : inventory_(&inventory)
{
}

void CapabilityRegistry::set_node_link(const NodeLink& link)
{
    node_ = link;
}

void CapabilityRegistry::set_companion(const CompanionLinkState& companion)
{
    companion_ = companion;
}

Availability CapabilityRegistry::local_availability(Capability capability) const
{
    const platform::HardwareInventory& hw = *inventory_;

    const auto by_feature = [&hw](HardwareFeature feature) {
        return hw.present(feature) ? from_state(hw.state(feature)) : Availability::Unsupported;
    };

    switch (capability) {
        case Capability::Time:
            // Never Unsupported, and never Off either. A device with a dead RTC
            // still has an uptime and a time the user typed in; what varies is
            // how much that time is worth, and that is TimeService's business
            // (source and quality), not availability's. ADR-0007 §2 argues the
            // seat; this is what the seat costs.
            return Availability::Ready;

        case Capability::Position:
            return by_feature(HardwareFeature::GnssReceiver);

        case Capability::Heading: {
            // Two local sources, in the order ADR-0007 §4 gives — and only
            // two. Accelerometer+gyroscope fusion is not a third: without a
            // magnetometer, yaw is unobservable, and integrating gyro alone
            // drifts without bound (docs/adr/0009-heading.md §"Alternatives
            // considered"; docs/upstream/research-integration.md §9,
            // verdict REJECT). Availability says only that *something* could
            // produce a heading. Whether the answer means anything right now
            // is validity, and a standing user with a GNSS fix is Ready and
            // Invalid at the same time — docs/adr/0009-heading.md.
            if (inventory_->present(HardwareFeature::MagnetometerSensor)) {
                return by_feature(HardwareFeature::MagnetometerSensor);
            }
            return by_feature(HardwareFeature::GnssReceiver);
        }

        case Capability::MotionSensing:
            return by_feature(HardwareFeature::Accelerometer);

        case Capability::MeshMessaging: {
            // The whole point of ADR-0003. A radio being present says nothing:
            // two of the five chips a T-Watch may carry have no LoRa modulator,
            // a third does LoRa only at 2.4 GHz, and the pinned MeshCore drives
            // exactly one of them. All three conditions have to hold.
            const platform::RadioInfo* radio = inventory_->radio();
            if (radio == nullptr || !radio->can_do_lora() ||
                radio->meshcore != platform::MeshCoreSupport::Supported) {
                return Availability::Unsupported;
            }
            return by_feature(HardwareFeature::Radio);
        }

        case Capability::Haptics:
            return by_feature(HardwareFeature::HapticActuator);

        case Capability::AudioPlayback:
            return by_feature(HardwareFeature::AudioOutDevice);

        case Capability::AudioCapture:
            return by_feature(HardwareFeature::AudioInDevice);

        case Capability::InfraredBlast:
            return by_feature(HardwareFeature::IrTransmitter);

        case Capability::PersistentStorage:
            // Internal flash. Not an inventory entry, because a board that
            // cannot store anything cannot hold the firmware that would ask.
            return Availability::Ready;

        case Capability::RemovableStorage:
            return by_feature(HardwareFeature::SdCard);

        case Capability::NotificationRelay:
        case Capability::CompanionLink: {
            // A phone, over BLE. There is no companion implementation yet, so
            // this reports Unprovisioned on both boards — which is the truthful
            // answer, and the one docs/adr/0002-companion-is-optional.md needs:
            // a watch that works with no phone must be able to say "no phone"
            // without calling it a failure.
            if (!inventory_->present(HardwareFeature::Ble)) {
                return Availability::Unsupported;
            }
            if (!companion_.bound) {
                return Availability::Unprovisioned;
            }
            return companion_.reachable ? Availability::Ready : Availability::Unreachable;
        }
    }

    return Availability::Unsupported;
}

Availability CapabilityRegistry::node_availability(Capability capability) const
{
    if ((kNodeProvidable & capability_bit(capability)) == 0) {
        return Availability::Unsupported;
    }
    if (!node_.bound) {
        return Availability::Unprovisioned;
    }
    if (!node_.offers(capability)) {
        // A node is paired and it does not do this one. Still Unprovisioned
        // rather than Unsupported: a different node would, and the remedy the
        // user needs is about equipment, not about this device's limits.
        return Availability::Unprovisioned;
    }
    if (!node_.compatible) {
        return Availability::Incompatible;
    }
    if (!node_.reachable) {
        return Availability::Unreachable;
    }
    return Availability::Ready;
}

// Choosing between the two sources — once, for both halves of the answer.
//
// This used to be two functions. `availability()` picked the effective source
// by remedy rank and `provider()` re-derived the choice with a condition of its
// own, defaulting to `Local` and reaching `Node` only when the node was already
// `Ready`. So on a board with no local GNSS, a bound node going out of range
// took `Position` from Ready/node to Unreachable/**local** — the same provider,
// unchanged, reported as the opposite side of the device. Settings and
// Diagnostics would have offered "service the receiver" for a receiver this
// board does not have, and the remedy the user needed — walk back to the node —
// belonged to a provider the API had just disowned. Issue #174; the invariant
// is ADR-0004 §2.
//
// Two answers derived from one choice cannot disagree, which is the whole
// reason this returns a pair rather than being called twice.
CapabilitySource CapabilityRegistry::source(Capability capability) const
{
    const CapabilitySource from_local{local_availability(capability), {Origin::Local, 0}};
    if (from_local.availability == Availability::Ready) {
        return from_local;  // local is preferred when it works — ADR-0008 §4
    }

    const CapabilitySource from_node{node_availability(capability), {Origin::Node, 0}};
    if (from_node.availability == Availability::Ready) {
        return from_node;
    }

    // Neither works. Nothing provides it at all, and that is a third thing —
    // not "the local device has it and it is broken". Named here rather than
    // left to fall out of the tie-break below, because `Origin` has two values
    // and neither of them means "nobody": `CapabilitySource` documents
    // `Unsupported` as the discriminator that says so, and TASKS.md T-111 owns
    // the question of whether the axis should grow a value instead.
    //
    // The tie-break *is* this case, as it happens — every other pair of states
    // the two sources can hold at once has distinct remedy ranks — so writing
    // it out costs a branch and buys the case a name.
    if (from_local.availability == Availability::Unsupported &&
        from_node.availability == Availability::Unsupported) {
        return from_local;
    }

    // Both are unsatisfied and one of them is the better thing to tell the
    // user about. remedy_rank decides which, and the origin follows it — that
    // following is the fix.
    return remedy_rank(from_local.availability) >= remedy_rank(from_node.availability)
               ? from_local
               : from_node;
}

Availability CapabilityRegistry::availability(Capability capability) const
{
    return source(capability).availability;
}

ProviderRef CapabilityRegistry::provider(Capability capability) const
{
    return source(capability).provider;
}

bool CapabilityRegistry::supports(Capability capability) const
{
    // Stable at runtime, and that stability is a requirement rather than a
    // side effect: an application must not appear in and vanish from the
    // launcher as a node comes and goes. It holds because kNodeProvidable is a
    // constant — nothing here consults node_.bound.
    //
    // ADR-0007 §3 has one sentence that reads the other way, describing
    // MeshMessaging on a CC1101 T-Watch as Unsupported "and no node attached".
    // The table two paragraphs above it is explicit that supports() never
    // changes at runtime, and the Waveshare/Position example depends on it, so
    // the table governs and the sentence is loose.
    return availability(capability) != Availability::Unsupported;
}

bool CapabilityRegistry::is_available(Capability capability) const
{
    return availability(capability) == Availability::Ready;
}

}  // namespace attadipa::core
