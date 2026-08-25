#pragma once

#include <cstdint>

#include "attadipa/core/clock.h"
#include "attadipa/core/motion.h"
#include "attadipa/core/power_state.h"

// What the GNSS receiver is doing, and what it costs to ask it for a position.
//
// docs/adr/0011-gnss-integrity.md and the owner's §8. A receiver is not a
// device you switch on and read; it is a device whose *start time* depends on
// what it still remembers, and every decision here is really a decision about
// how long the user waits and how much charge it costs.
//
// Two rules bound the model, and both are the owner's:
//
//   1. `Backup` is only real if the hardware retains it. On the T-Watch the
//      GNSS daughterboard carries an MS412FE cell, and **whether it actually
//      backs the receiver's RAM is UNKNOWN until T-051**. So the state is gated
//      behind a capability that must be *established as supported* rather than
//      assumed — a receiver known not to have it and a receiver nobody has
//      checked are both refused, and for reasons the type keeps apart.
//   2. **Assistance is never a dependency.** Attadipa works offline, always. An
//      assistance path that becomes load-bearing is a bug even on the days it
//      works, so `assistance` only influences how fast a start is *expected* to
//      be — never whether one is attempted.

namespace attadipa::core {

enum class GnssState : std::uint8_t {
    Off,        // powered down. Nothing retained beyond what the hardware holds
    Backup,     // powered down, RTC and ephemeris retained. Hardware-gated
    Acquiring,  // powered, searching, no usable fix yet
    Tracking,   // powered, fixed, reporting
    PowerSave,  // duty-cycled by the receiver itself. Capability-gated
    Degraded,   // powered and trying, but the solution is not usable
};

inline constexpr std::uint8_t kGnssStateCount = static_cast<std::uint8_t>(GnssState::Degraded) + 1;

// How much the receiver still knows, which is what decides the wait.
enum class StartKind : std::uint8_t {
    Cold,  // no almanac, no ephemeris, no position, no time
    Warm,  // almanac and time, stale or absent ephemeris
    Hot,   // valid ephemeris. The fastest a receiver can be
};

// Whether a *particular part* has a feature. Three-valued, because two of the
// three answers are different facts and a `bool` can only hold one of them.
//
// docs/adr/0011-gnss-integrity.md §3 already decided this shape and this
// default for the receiver capability descriptor: every entry "starts as
// UNKNOWN and becomes SUPPORTED or UNSUPPORTED only from a primary source",
// and UNKNOWN behaves as *do not rely on it* at runtime. The same discipline as
// `ReceiverIndication`'s Unknown/Unsupported pair in position.h, `Provenance`
// in power_state.h, and MEASURED · ESTIMATED · UNKNOWN in CLAUDE.md.
//
// It is **not** `Availability` (ADR-0004). That is a runtime service state —
// Off, Failed, Unreachable — about a provider that exists and might be having a
// bad day. This is a static fact about silicon, and switching something on does
// not change it.
enum class SupportState : std::uint8_t {
    Unknown,      // nobody has read the datasheet yet. The default, and it stays
    Unsupported,  // a primary source says this part does not have it
    Supported,    // a primary source says it does
};

// Whether the planner may spend this feature. `Unknown` is never yes, which is
// the fail-safe direction: an unread datasheet buys a cold start, not a state
// the receiver may not have.
constexpr bool is_supported(SupportState state)
{
    return state == SupportState::Supported;
}

// Whether the question has been answered at all, either way. This is the fact
// `is_supported()` cannot express and a `bool` could not hold: it separates
// "we looked and it is not there" from "nobody has looked".
constexpr bool is_established(SupportState state)
{
    return state != SupportState::Unknown;
}

// What a *particular* receiver can do. Filled from primary sources, never from
// a product page: T-051 for the u-blox MIA-M10Q, T-052 for the Quectel LS550G.
// Until one of those lands, every field here is honestly `Unknown` — which is
// the state the project is actually in, and which the previous `bool` spelled
// the same way as a receiver proven not to have the feature.
struct GnssCapabilities {
    SupportState backup_domain    = SupportState::Unknown;  // retains time and ephemeris while off
    SupportState power_save_mode  = SupportState::Unknown;  // duty-cycles itself
    SupportState assistance       = SupportState::Unknown;  // accepts injected orbit or time data
    SupportState orbit_prediction = SupportState::Unknown;  // computes its own predictions offline

    // Whether every field carries a verdict. False today, for both candidate
    // receivers, and it is meant to be: this is how "T-051 is finished" becomes
    // something a machine can check, rather than somebody remembering which of
    // four fields they filled in.
    constexpr bool fully_established() const
    {
        return is_established(backup_domain) && is_established(power_save_mode) &&
               is_established(assistance) && is_established(orbit_prediction);
    }
};

// What the receiver's owner knows when it decides what to do next.
struct GnssContext {
    Millis           since_last_fix{0};
    bool             ephemeris_retained = false;

    // Whether the backup domain was actually kept powered — which is a
    // different fact from `capabilities.backup_domain`, and the difference is a
    // cold start. A receiver that has a backup domain and was switched off at
    // the rail comes up knowing nothing, and telling the caller to expect a
    // warm start there is promising a fix in thirty seconds that arrives in
    // several minutes.
    bool             backup_retained = false;
    // A receiver may be power-gated only from motion evidence measured on its
    // own body. Unknown or other-body evidence is deliberately neutral.
    SensorBody       receiver_body = SensorBody::Unknown;
    MotionEvidence   motion{};
    bool             fresh_fix_requested = false;  // an application is waiting
    PowerState       device_power = PowerState::Active;
    GnssCapabilities capabilities{};
    bool             assistance_available = false;  // optional, never required

    constexpr bool own_body_at_rest() const
    {
        return motion.says_at_rest(receiver_body);
    }

    constexpr bool own_body_in_motion() const
    {
        return motion.says_in_motion(receiver_body);
    }
};

// Which start a transition from this state, with this context, would be.
//
// `Hot` requires retained ephemeris *and* a recent fix; `Warm` requires the
// backup domain to be real. Everything else is `Cold`, because a receiver that
// might have kept something and might not has, for planning purposes, kept
// nothing.
StartKind start_kind(const GnssContext& context);

// Whether the receiver may move directly between two states.
//
// `Backup` is refused outright unless the backup domain is *established as
// supported* — which is the point: a state that pretends to retain what nothing
// retains would make every subsequent start look hot and every timeout too
// short. `Unknown` is refused on the same line as `Unsupported` and for a
// different reason, and the reason survives in the capability rather than being
// flattened into the verdict.
bool transition_is_legal(GnssState from, GnssState to, const GnssCapabilities& capabilities);

// The state the receiver should be in, given what the device wants and what it
// can afford. Pure, so it is testable without a receiver — which is the only
// way it can be tested at all today.
GnssState next_state(GnssState current, const GnssContext& context);

const char* to_string(GnssState state);
const char* to_string(StartKind kind);

// `Unknown` has to stay sayable all the way out to a diagnostics screen. A
// renderer that printed it as "Unsupported" would put the collision back one
// layer up, where it is harder to see and nobody is testing for it.
const char* to_string(SupportState state);

}  // namespace attadipa::core
