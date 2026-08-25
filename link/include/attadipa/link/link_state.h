#pragma once

#include <cstdint>

#include "attadipa/core/clock.h"
#include "attadipa/core/transport_state.h"

// One owner of "is the link up", and one point at which it resets.
//
// MeshCore's #2333 spends its length on three defects that are the same defect:
// `onDisconnect()` did not reset the connected flag unconditionally, so a
// disconnect arriving before `enable()` left stale state;
// `onAuthenticationComplete()` set connected even when the interface was
// disabled, letting a bonded phone attach before the device was ready; and
// three state variables were not reset in `begin()`, so RAM state survived a
// reset. All three are *connection state that no single place owns and that no
// single point resets* (docs/upstream/meshcore-1.17-review.md §6).
//
// #3005 and #3007 — bonded reconnect and receive-queue synchronisation — are
// merged and released upstream in v1.17.0, and Attadipa carries **no workaround
// for either**. The owner's instruction is explicit about not porting fixes for
// bugs the version in use has already fixed, and this comment exists so nobody
// adds one later out of caution.
//
// This machine is transport-agnostic on purpose: BLE, USB, UART and a Wi-Fi
// socket all have the same nine interesting events, and the one that catches
// people out — a callback arriving after the state has already moved — is not a
// BLE peculiarity. It runs on a host with no radio, which is why it can be
// tested at all today.

namespace attadipa::link {

using core::DisconnectReason;
using core::Millis;
using core::MonotonicTime;
using core::TransportKind;
using core::TransportPhase;

enum class LinkEvent : std::uint8_t {
    Attach,             // the peripheral or radio exists and is powered
    Detach,             // it is gone — unplugged, or its rail dropped
    PeerArriving,       // advertising accepted, enumeration begun, SYN seen
    PeerEstablished,    // the peer completed whatever this transport requires
    PeerData,           // a frame arrived. The only proof of liveness there is
    PeerGone,           // the peer closed, or the stack says it did
    LocalClose,         // we ended it
    Fault,              // the transport itself failed
    Suspend,            // quiesce deliberately, e.g. entering a sleep state
    Resume,             // come back from a suspend
    SubsystemRestart,   // the stack below was torn down and rebuilt
};

// Whether an event was acted on, ignored, or was not applicable.
//
// Ignored is the interesting one and it is *counted*, because "a callback
// arrived in a state where it makes no sense" is not a harmless no-op — it is
// the signature of the ghost-connection bug, and a device that has ignored four
// hundred of them is telling you something.
//
// Which makes the boundary between the two refusals load-bearing, so both
// halves are written down rather than one. `Redundant` is the narrow claim that
// **the link is already in the state the event asked for** — nothing to do,
// nothing worth counting, and a caller may read it as success. It is not a
// general "not applied": an event that cannot apply in this phase is `Ignored`,
// even where the hardware it names is physically present. An attach to a
// faulted transport is the example that cost a defect — the peripheral is
// there, and the link still carries nothing until the subsystem is restarted,
// so answering `Redundant` told an operator the attach had succeeded.
//
// **The rule yields where the machine deliberately counts an ordinary
// callback**, and that is a decision rather than an oversight, so it is written
// here rather than left to be tidied away. `PeerGone` arriving when the link is
// not live asks for a state it is often already in, which by the paragraph
// above would be `Redundant` — and it is counted anyway, because a duplicate
// disconnect callback is ordinary on every BLE stack and its *frequency* is the
// diagnostic. `PeerArriving` in `Connecting` is the same shape. Where counting
// the arrival is the point, counting wins; where the caller is simply asking
// for something already true, `Redundant` does. Anyone making these consistent
// should move the *code* to the rule only after deciding they are willing to
// lose those counters, which #2333 says they should not be.
enum class EventOutcome : std::uint8_t {
    Applied,
    Ignored,
    Redundant,
};

class LinkState {
public:
    struct Config {
        TransportKind kind = TransportKind::Unknown;

        // How long without a frame before `Ready` stops being an honest answer.
        //
        // This is the substitute for a connection bit the hardware does not
        // have. On the ESP32-S3's USB-Serial-JTAG there is no CDC line state,
        // so "a host is listening" is not knowable — but "a peer sent us
        // something within T" is, and it is the only claim that can be
        // supported. Naming it liveness rather than connection is the point:
        // a reader can tell it is inferred.
        Millis liveness{5000};

        // Whether reaching `Ready` requires an explicit establishment event, or
        // whether data alone is enough. BLE has a real connection callback;
        // a raw UART does not, and pretending it does is how a link ends up
        // permanently "connected" to nothing.
        bool requires_establishment = true;
    };

    LinkState() = default;
    explicit LinkState(const Config& config) : config_(config) {}

    // `reason` is the caller's answer to "why", and only two events ask it:
    // `Detach` and `PeerGone`. They are the two whose cause lives outside this
    // machine — a rail, a cable, a stack, a peer — and which no amount of local
    // state can reconstruct. Every other event carries its own cause by
    // definition (`LocalClose` is `LocalRequest`, `Fault` is `Fault`,
    // `SubsystemRestart` is `SubsystemRestart`, a `tick()` that expires is
    // `LivenessTimeout`) and ignores the argument rather than inviting a caller
    // to contradict it.
    //
    // The default is `Unknown` and it is a real answer, not a placeholder to be
    // improved on. A transport that cannot distinguish "the user unplugged it"
    // from "the regulator browned out" is *supposed* to say `Unknown`; an
    // adapter that guesses a specific reason to avoid saying so has turned a
    // gap in the evidence into a false field report, which is the more expensive
    // of the two. Pass the most precise reason the transport actually
    // established, and nothing else.
    //
    // `DisconnectReason::None` is not sayable here. It means "there has not been
    // a disconnect", so as an argument to one it is a contradiction, and it is
    // recorded as `Unknown`.
    EventOutcome apply(LinkEvent event, MonotonicTime now,
                       DisconnectReason reason = DisconnectReason::Unknown);

    // Expire liveness. Cheap, idempotent, and the only way `Ready` is ever
    // left without an event: a peer that stops talking never sends anything to
    // say so.
    void tick(MonotonicTime now);

    TransportPhase   phase() const { return phase_; }
    bool             ready() const { return phase_ == TransportPhase::Ready; }

    // Why the last *session* ended — and a session is a period spent in `Ready`,
    // which is the same thing `sessions()` counts. So this moves when `Ready` is
    // left and at no other time: a `Detach` from `Attached` or `Connecting`
    // ends no session, and must not overwrite the reason the previous one
    // ended with a report about a link that was already idle. `reset()` is the
    // exception, and it is not one really — a subsystem restart ends whatever
    // was going on, including nothing.
    //
    // It answers the first question anyone asks about an intermittent link,
    // which is who let go first. That only works if every route in is honest,
    // so the reason is either the caller's or one this machine can derive; it is
    // never assumed. Before issue #162 the `Detach` route assumed `PeerClosed`,
    // and a device that had switched its own peripheral rail off blamed the peer
    // for the disconnect it had itself caused.
    DisconnectReason last_disconnect() const { return last_disconnect_; }

    // Increments every time a session begins. Anything that outlives a session
    // — a pending request, a reassembly buffer, a negotiated version — must be
    // stamped with it and discarded when it changes. ADR-0005 §5: no state
    // survives a reconnect implicitly, which is precisely what MeshCore's
    // never-reset `app_target_ver` does.
    std::uint32_t epoch() const { return epoch_; }

    std::uint32_t sessions() const { return sessions_; }
    std::uint32_t ignored_events() const { return ignored_; }
    MonotonicTime last_activity() const { return last_activity_; }

    // Everything back to boot state. This is `begin()` done properly: it is the
    // single point of reset that #2333's three defects each lacked, and it is
    // called on SubsystemRestart rather than hoped for.
    void reset();

private:
    void enter(TransportPhase next, MonotonicTime now, DisconnectReason reason);

    Config           config_{};
    TransportPhase   phase_           = TransportPhase::Absent;
    TransportPhase   before_suspend_  = TransportPhase::Absent;
    DisconnectReason last_disconnect_ = DisconnectReason::None;
    MonotonicTime    last_activity_{};
    std::uint32_t    epoch_    = 0;
    std::uint32_t    sessions_ = 0;
    std::uint32_t    ignored_  = 0;
};

const char* to_string(LinkEvent event);
const char* to_string(EventOutcome outcome);

}  // namespace attadipa::link
