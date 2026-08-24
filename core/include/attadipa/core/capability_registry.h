#pragma once

#include <cstdint>

#include "attadipa/core/availability.h"
#include "attadipa/core/capability.h"

// A forward declaration, on purpose. The registry needs a hardware inventory to
// do its job, and it is the one place in core/ that is allowed to know one
// exists — docs/adr/0007-two-capability-layers.md commits to "a capability
// registry that owns the mapping and is the single place availability is
// computed". Declaring the type rather than including its header means an
// application can hold a CapabilityRegistry& without gaining the ability to ask
// it about chips, and cannot construct one without linking a library it does
// not link.
namespace attadipa::platform {
class HardwareInventory;
}

namespace attadipa::core {

// What an Attadipa node is offering, as far as the registry is concerned.
//
// This is deliberately smaller than ADR-0004 §3's provider registry, which is
// not built yet. It is the smallest thing that makes Unprovisioned, Unreachable
// and Ready distinct on screen instead of hypothetical — and those three being
// distinguishable is the entire reason the seven-state enum exists. When the
// real provider registry lands it replaces this struct; the three questions
// below do not change.
struct NodeLink {
    bool          bound     = false;  // a node has been paired
    bool          reachable = false;  // and we can talk to it right now
    bool          compatible = true;  // and we agreed a protocol version
    std::uint16_t provides  = 0;      // Capability bitmask

    bool offers(Capability capability) const
    {
        return (provides & capability_bit(capability)) != 0;
    }
};

// Likewise: no phone companion exists yet. The flag is here so that
// NotificationRelay and CompanionLink report Unprovisioned rather than
// Unsupported on a board with BLE, which is the truthful answer and the one
// docs/adr/0002-companion-is-optional.md depends on.
struct CompanionLinkState {
    bool bound     = false;
    bool reachable = false;
};

class CapabilityRegistry {
public:
    explicit CapabilityRegistry(const platform::HardwareInventory& inventory);

    // Could this device ever do this, in some supported configuration?
    // Stable enough to decide whether an application appears in the launcher.
    bool supports(Capability capability) const;

    // Can it do it right now?
    bool is_available(Capability capability) const;

    // Which source is answering for this capability, and what state it is in.
    // The one place the choice is made; everything below reads this.
    CapabilitySource source(Capability capability) const;

    // Why not, and what can the user do about it?
    Availability availability(Capability capability) const;

    // Which side is serving it. Diagnostics and Settings only, and an answer
    // only when availability() is not Unsupported — see CapabilitySource.
    //
    // Prefer source() where both halves are wanted. These two agree because
    // they are the same call, and that is a property to keep: deriving one of
    // them separately is the defect issue #174 was opened about.
    ProviderRef provider(Capability capability) const;

    void set_node_link(const NodeLink& link);
    void set_companion(const CompanionLinkState& companion);

    const NodeLink&           node_link() const { return node_; }
    const CompanionLinkState& companion() const { return companion_; }

private:
    Availability local_availability(Capability capability) const;
    Availability node_availability(Capability capability) const;

    const platform::HardwareInventory* inventory_;
    NodeLink                           node_;
    CompanionLinkState                 companion_;
};

}  // namespace attadipa::core
