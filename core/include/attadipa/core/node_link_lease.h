#pragma once

#include "attadipa/core/power_owner.h"
#include "attadipa/core/transport_state.h"

// #367 item 7: the node link's power declaration, and the serialisation
// decision that had to come with it.
//
// The transport had no power opinion at all. Giving it one is twenty lines of
// `acquire`/`release`; the reason it was not twenty lines is written in
// `power_owner.h` — "**Not thread-safe, and deliberately so.**" — and the race
// it names is not the lease table. `PowerOwner::sleep()` reads `held()` once
// and then holds hardware for the whole sleep, so a lease taken from the BLE
// task in that window is a lease the sleeper never saw. A lock around the
// table would make the table safe and leave that window exactly as wide.
//
// **The decision: the transport declares, the sleeper records.** The BLE task
// already publishes its phase behind its own critical section, and the UI task
// already reads that snapshot every frame to draw the mesh screen. That
// snapshot is the marshal the audit asked for — the issue's own second option,
// via a channel that exists rather than a new queue entry duplicating it. So
// `acquire()` and `release()` stay on the one task that calls `sleep()`, and
// the reconcile below runs immediately before it. The window closes by
// construction: there is no instant at which a lease exists that the next
// `held()` will not see, because the same task does both, in that order.
//
// What this deliberately does not do is gate anything. `sleep()` refuses on
// `held() & (plan.suspend | plan.rails_off)`, and the only plan the firmware
// runs suspends `Display` alone. #367 puts an actually-gated rail out of scope
// in as many words and ADR-0016 does not authorise one. The lease is a
// declaration that becomes load-bearing the day a plan names `NodeLink`; until
// then it is the seam, tested, and costing one comparison per frame.

namespace attadipa::core {

// Is the radio in use in this phase?
//
// `Connecting` counts as much as `Ready`: advertising, enumerating and
// handshaking are a radio that is on, and a link that drops its declaration
// between sessions would be released and re-taken across every reconnect.
// `Attached` is the one phase that genuinely wants nothing — the peripheral
// exists and nobody is talking on it — and `Absent`, `Suspended` and `Faulted`
// are each a radio that is not running.
constexpr bool node_link_wants_power(TransportPhase phase)
{
    return phase == TransportPhase::Connecting || phase == TransportPhase::Ready;
}

// One lease, held for as long as the phase says the link is in use.
//
// Idempotent on purpose: it is called every time round the caller's loop and
// does nothing at all when the table already agrees with the phase, so the
// steady state costs one comparison and no owner call.
class NodeLinkLease {
public:
    // True when the table now matches the phase. False means the owner refused,
    // `why` says which refusal it was, and nothing changed — including the
    // handle, so the next call tries again. Retrying is right: `Exhausted` is
    // recoverable by definition and a link that is still up still wants the
    // lease. It is the caller's job to not log it every 5 ms.
    bool reconcile(PowerOwner& owner, TransportPhase phase, LeaseError& why)
    {
        why = LeaseError::None;
        const bool wanted = node_link_wants_power(phase);
        if (wanted == held()) return true;

        if (wanted) {
            // No deadline. `PowerLeases::overdue()` skips `{}`, and it should:
            // a link that is up has no scheduled end, and a lease reported
            // overdue because the watch stayed awake would name a consumer bug
            // that is not one.
            id_ = owner.acquire(domain_bit(PowerDomain::NodeLink), {}, why);
            return id_ != kNoLease;
        }

        const LeaseId releasing = id_;
        // The handle is dropped whatever the owner says. A release that failed
        // was already unbalanced — the table has no record of this grant — and
        // keeping the handle would mean re-releasing it forever.
        id_ = kNoLease;
        return owner.release(releasing, why);
    }

    bool held() const { return id_ != kNoLease; }

private:
    LeaseId id_ = kNoLease;
};

}  // namespace attadipa::core
