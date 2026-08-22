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
//      behind a capability flag rather than assumed, and a receiver without it
//      simply cannot be in that state.
//   2. **Assistance is never a dependency.** Attadipa works offline, always. An
//      assistance path that becomes load-bearing is a bug even on the days it
//      works, so `assistance` only influences how fast a start is *expected* to
//      be — never whether one is attempted.
//
// And a third, from docs/adr/0013-node-motion.md, which is about *whose*
// receiver this is: a context describes one receiver on one body, and the only
// accelerometer that may gate it is one on the same body. The watch's wrist
// resting is not a reason to sleep a receiver inside somebody else's bag.

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

// What a *particular* receiver can do. Filled from primary sources, never from
// a product page: T-051 for the u-blox MIA-M10Q, T-052 for the Quectel LS550G.
// Every field defaults to false, which reads as "not established" — the same
// discipline as UNKNOWN in the research documents.
struct GnssCapabilities {
    bool backup_domain   = false;  // retains time and ephemeris while off
    bool power_save_mode = false;  // duty-cycles itself
    bool assistance      = false;  // accepts injected orbit or time data
    bool orbit_prediction = false; // computes its own predictions offline
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

    // Which chassis this receiver is bolted to, and what an accelerometer on
    // some chassis has to say. They are two fields because they are two facts:
    // the second is only evidence about the first when the bodies match.
    //
    // `Unknown` on either side means the gate below does nothing, which is the
    // safe reading and the deliberate one — `device_moving` used to be a plain
    // `bool` whose default `false` read as "at rest" and powered a receiver
    // down on a sample nobody had taken (ADR-0013 §2).
    SensorBody       receiver_body = SensorBody::Unknown;
    MotionEvidence   motion{};

    bool             fresh_fix_requested = false;  // an application is waiting
    PowerState       device_power = PowerState::Active;
    GnssCapabilities capabilities{};
    bool             assistance_available = false;  // optional, never required

    // Evidence that *this receiver's own body* is at rest — the only
    // conjunction that may make it sleep. Nothing here reads a clock, so
    // expiring a sample that has gone stale is the producer's job, not this
    // struct's (see motion.h, and T-080).
    bool own_body_at_rest() const { return motion.says_at_rest(receiver_body); }

    // ...and the only one that may wake it. Not the negation of the above:
    // everything neither of them covers is "not known", and not known moves
    // the receiver in neither direction.
    bool own_body_in_motion() const { return motion.says_in_motion(receiver_body); }
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
// `Backup` is refused outright when the hardware has no backup domain — which
// is the point: a state that pretends to retain what nothing retains would make
// every subsequent start look hot and every timeout too short.
bool transition_is_legal(GnssState from, GnssState to, const GnssCapabilities& capabilities);

// The state the receiver should be in, given what the device wants and what it
// can afford. Pure, so it is testable without a receiver — which is the only
// way it can be tested at all today.
GnssState next_state(GnssState current, const GnssContext& context);

const char* to_string(GnssState state);
const char* to_string(StartKind kind);

}  // namespace attadipa::core
