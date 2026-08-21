#include "attadipa/link/link_state.h"

namespace attadipa::link {
namespace {

constexpr bool is_live(TransportPhase phase)
{
    return phase == TransportPhase::Ready || phase == TransportPhase::Connecting;
}

}  // namespace

void LinkState::enter(TransportPhase next, MonotonicTime now, DisconnectReason reason)
{
    if (next == phase_) {
        return;
    }

    // A session ends whenever we leave Ready, whatever the route out. The epoch
    // moves here and nowhere else, so there is exactly one place that can
    // forget to move it.
    if (phase_ == TransportPhase::Ready) {
        ++epoch_;
        last_disconnect_ = reason;
    }
    if (next == TransportPhase::Ready) {
        ++sessions_;
        last_activity_ = now;
    }

    phase_ = next;
}

EventOutcome LinkState::apply(LinkEvent event, MonotonicTime now, DisconnectReason reason)
{
    switch (event) {
        case LinkEvent::Attach:
            if (phase_ != TransportPhase::Absent) {
                return EventOutcome::Redundant;
            }
            enter(TransportPhase::Attached, now, DisconnectReason::None);
            return EventOutcome::Applied;

        case LinkEvent::Detach:
            // Unconditional, from any state including Absent and Faulted. This
            // is #2333's first defect inverted: a disconnect that arrives in an
            // unexpected state must still clear the state, because the
            // alternative is a device that believes in a peer that is gone.
            if (phase_ == TransportPhase::Absent) {
                return EventOutcome::Redundant;
            }
            enter(TransportPhase::Absent, now, DisconnectReason::PeerClosed);
            return EventOutcome::Applied;

        case LinkEvent::PeerArriving:
            // A peer cannot arrive at a peripheral that is not there. Upstream's
            // `onAuthenticationComplete()` accepted exactly this and let a
            // bonded phone ghost-connect before the interface was enabled.
            if (phase_ != TransportPhase::Attached) {
                ++ignored_;
                return EventOutcome::Ignored;
            }
            enter(TransportPhase::Connecting, now, DisconnectReason::None);
            return EventOutcome::Applied;

        case LinkEvent::PeerEstablished:
            if (phase_ == TransportPhase::Ready) {
                return EventOutcome::Redundant;
            }
            if (phase_ != TransportPhase::Connecting && phase_ != TransportPhase::Attached) {
                ++ignored_;
                return EventOutcome::Ignored;
            }
            enter(TransportPhase::Ready, now, DisconnectReason::None);
            last_activity_ = now;
            return EventOutcome::Applied;

        case LinkEvent::PeerData:
            // Data is proof of life, and on a transport with no connection
            // signal it is the *only* proof. Where establishment is required,
            // data arriving in a lesser state does not promote the link —
            // otherwise a stray byte would be enough to declare a session, and
            // "enumerated" would once again be confused with "ready".
            if (phase_ == TransportPhase::Ready) {
                last_activity_ = now;
                return EventOutcome::Applied;
            }
            if (!config_.requires_establishment &&
                (phase_ == TransportPhase::Attached || phase_ == TransportPhase::Connecting)) {
                enter(TransportPhase::Ready, now, DisconnectReason::None);
                last_activity_ = now;
                return EventOutcome::Applied;
            }
            ++ignored_;
            return EventOutcome::Ignored;

        case LinkEvent::PeerGone:
            if (!is_live(phase_)) {
                // Not an error and not applied: a disconnect callback for a
                // session that already ended is ordinary on every BLE stack.
                // Counting it is how a storm of them becomes visible.
                ++ignored_;
                return EventOutcome::Ignored;
            }
            enter(TransportPhase::Attached, now, reason);
            return EventOutcome::Applied;

        case LinkEvent::LocalClose:
            if (!is_live(phase_)) {
                return EventOutcome::Redundant;
            }
            enter(TransportPhase::Attached, now, DisconnectReason::LocalRequest);
            return EventOutcome::Applied;

        case LinkEvent::Fault:
            // Faulted is not Attached. The difference is the remedy: one needs a
            // reset of the subsystem, the other needs a peer. Collapsing them
            // produces a link that retries forever against broken hardware.
            enter(TransportPhase::Faulted, now, DisconnectReason::Fault);
            return EventOutcome::Applied;

        case LinkEvent::Suspend:
            if (phase_ == TransportPhase::Suspended) {
                return EventOutcome::Redundant;
            }
            if (phase_ == TransportPhase::Absent || phase_ == TransportPhase::Faulted) {
                ++ignored_;
                return EventOutcome::Ignored;
            }
            before_suspend_ = phase_;
            enter(TransportPhase::Suspended, now, DisconnectReason::LocalRequest);
            return EventOutcome::Applied;

        case LinkEvent::Resume:
            if (phase_ != TransportPhase::Suspended) {
                ++ignored_;
                return EventOutcome::Ignored;
            }
            // Never back to Ready. A suspend outlives the peer's patience and
            // the session's epoch has already moved, so the link comes back
            // attached and the peer has to arrive again. Resuming into Ready
            // would be resuming into a session the other end has forgotten.
            enter(before_suspend_ == TransportPhase::Absent ? TransportPhase::Absent
                                                            : TransportPhase::Attached,
                  now, DisconnectReason::LocalRequest);
            return EventOutcome::Applied;

        case LinkEvent::SubsystemRestart:
            reset();
            return EventOutcome::Applied;
    }

    ++ignored_;
    return EventOutcome::Ignored;
}

void LinkState::tick(MonotonicTime now)
{
    if (phase_ != TransportPhase::Ready) {
        return;
    }
    if (config_.liveness.value == 0) {
        return;  // liveness disabled: the transport has a real connection signal
    }
    if (core::elapsed(last_activity_, now) >= config_.liveness) {
        enter(TransportPhase::Attached, now, DisconnectReason::LivenessTimeout);
    }
}

void LinkState::reset()
{
    // Every field, unconditionally. Counters that describe the *device's*
    // history rather than the link's session survive — sessions and ignored
    // events are exactly what a restart is evidence about, and zeroing them
    // would erase the reason anyone is looking.
    phase_           = TransportPhase::Absent;
    before_suspend_  = TransportPhase::Absent;
    last_disconnect_ = DisconnectReason::SubsystemRestart;
    last_activity_   = MonotonicTime{};
    ++epoch_;
}

const char* to_string(LinkEvent event)
{
    switch (event) {
        case LinkEvent::Attach:           return "Attach";
        case LinkEvent::Detach:           return "Detach";
        case LinkEvent::PeerArriving:     return "PeerArriving";
        case LinkEvent::PeerEstablished:  return "PeerEstablished";
        case LinkEvent::PeerData:         return "PeerData";
        case LinkEvent::PeerGone:         return "PeerGone";
        case LinkEvent::LocalClose:       return "LocalClose";
        case LinkEvent::Fault:            return "Fault";
        case LinkEvent::Suspend:          return "Suspend";
        case LinkEvent::Resume:           return "Resume";
        case LinkEvent::SubsystemRestart: return "SubsystemRestart";
    }
    return "?";
}

const char* to_string(EventOutcome outcome)
{
    switch (outcome) {
        case EventOutcome::Applied:   return "Applied";
        case EventOutcome::Ignored:   return "Ignored";
        case EventOutcome::Redundant: return "Redundant";
    }
    return "?";
}

}  // namespace attadipa::link
