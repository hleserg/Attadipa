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

    EventOutcome apply(LinkEvent event, MonotonicTime now,
                       DisconnectReason reason = DisconnectReason::Unknown);

    // Expire liveness. Cheap, idempotent, and the only way `Ready` is ever
    // left without an event: a peer that stops talking never sends anything to
    // say so.
    void tick(MonotonicTime now);

    TransportPhase   phase() const { return phase_; }
    bool             ready() const { return phase_ == TransportPhase::Ready; }
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
