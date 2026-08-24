#pragma once

#include <cstdint>

// Availability, freshness and where a thing came from.
//
// docs/adr/0004-capability-sources.md. The enum has seven values because it has
// seven *user remedies*, not because seven felt thorough: each state is a
// different sentence somebody has to be told, and no two of them may render
// identically. "This watch has no compass", "Maps needs an Attadipa node" and
// "your node is out of range" are three sentences, and one enum value cannot
// carry all three.

namespace attadipa::core {

enum class Availability : std::uint8_t {
    Unsupported,    // no configuration of this device can provide it. Terminal.
    Unprovisioned,  // a supported provider would give it; none is bound
    Unreachable,    // a provider is bound, but is not reachable now
    Incompatible,   // reachable, but no protocol version can be agreed
    Failed,         // bound and reachable; it did not come up
    Off,            // deliberately powered down; can be brought up
    Ready,          // usable now
};

// Where a capability is being served from. Orthogonal to Availability: a
// capability can be Ready from a node and Unreachable from the same node a
// second later, and applications must never branch on this — only Settings and
// Diagnostics may, because inspecting providers is what they are for.
enum class Origin : std::uint8_t { Local, Node };

using ProviderId = std::uint16_t;

struct ProviderRef {
    Origin     origin = Origin::Local;
    ProviderId id     = 0;  // meaningless when origin == Local
};

// One decision, both of its halves.
//
// Availability and origin are not two questions. They are the same choice —
// *which source is answering for this capability* — seen from two sides, and
// returning them separately is exactly what let them disagree: `provider()`
// used to re-derive the choice with a condition of its own and default to
// `Local`, so a bound node going out of range reported itself as the local
// device (issue #174). Anything that computes one of these computes the other
// at the same time, and this struct is how it says so.
//
// **`provider` is an answer only when `availability != Unsupported`.**
// `Unsupported` means no configuration of this device can provide the
// capability, from either side, so there is nothing serving it and nothing to
// dispatch to, power, configure or diagnose. `Origin` has no value that says
// "nobody" — it has two, and whether it should have a third is TASKS.md T-111,
// which is an ADR decision and not a detail to settle in a struct. Until that
// lands, this field is what keeps `Local` from being read as a claim: the
// discriminator travels with the origin rather than having to be remembered,
// which is the same contract style as `ProviderRef::id` above.
struct CapabilitySource {
    Availability availability = Availability::Unsupported;
    ProviderRef  provider     = {};
};

enum class Validity : std::uint8_t { Unknown, Valid, Stale, Invalid };

// Two ages, because a datum that crossed a link has two of them, and collapsing
// them loses the one that matters. A position sampled ten seconds ago and
// delivered now is not the same as one sampled now and delivered ten seconds
// ago, and only the second is the link's fault.
template <typename T>
struct Timed {
    T             value            = {};
    std::uint32_t age_at_source_ms = 0;  // how old it was when the provider sampled it
    std::uint32_t age_at_us_ms     = 0;  // how long ago it reached this device
    Validity      validity         = Validity::Unknown;
};

struct CapabilityChanged;  // declared in capability.h, where Capability lives

// How actionable a state is, most actionable first: Ready 6, Off 5, Failed 4,
// Unreachable 3, Incompatible 2, Unprovisioned 1, Unsupported 0.
//
// It exists so that "which of these two unsatisfied answers do we show the
// user" has one definition instead of one per caller. It is a ranking of
// *remedies*, not of severity: Off outranks Failed because "turn it on" is
// something the user can act on and "it broke" is not.
int remedy_rank(Availability availability);

const char* to_string(Availability availability);
const char* to_string(Origin origin);
const char* to_string(Validity validity);

}  // namespace attadipa::core
