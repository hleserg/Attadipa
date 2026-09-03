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
// then it is the seam, tested, and costing one comparison per sleep request.

namespace attadipa::core {

// Is the node link in use in this phase?
//
// Read the phases from the enum that defines them, not from what the names
// suggest. *Powered* is in the definition of `Attached` itself —
// `core/include/attadipa/core/transport_state.h:27` — "Attached,    // it exists and is powered"
// — and the firmware is why: one `case` arm brings the stack up and starts
// scanning in the same breath,
// `firmware/main/meshcore_ble.cpp:1270` — "provider.begin(now());"
// followed immediately by `:1271` — "if (configured.load()) start_scan();",
// and that scan is neither passive nor bounded —
// `firmware/main/meshcore_ble.cpp:577` — "params.passive = 0;" and `:581`
// — "const int rc = ble_gap_disc(own_address_type.load(), BLE_HS_FOREVER, &params,".
// So the ordinary state of a configured watch with no node in range is
// `Attached` with the radio actively scanning forever. A declaration that
// called that phase idle would be false about the one state the watch spends
// most of its life in, which is also the only state in which a sleep is
// interesting.
//
// `Connecting` counts as much as `Ready`: advertising, enumerating and
// handshaking are a radio that is on, and a link that drops its declaration
// between sessions would be released and re-taken across every reconnect.
//
// What is left is the phases in which the link is not in use, and the limit of
// what this predicate may claim. `Absent` is "the peripheral or radio is not
// there at all" and `Suspended` is "deliberately quiesced, e.g. for a sleep
// state" (`core/include/attadipa/core/transport_state.h:26` — "Absent,      // the peripheral or radio is not there at all").
// `Faulted` is "it failed, and needs a reset rather than a retry" — a link that
// is not carrying traffic, which is what a lease declares. **None of those three
// is a claim about the controller.** `TransportPhase` is a statement about the
// link, and this repository has no source saying what the radio draws in them;
// the declaration is therefore known-incomplete on exactly that axis, and a
// future plan that gates a rail on `NodeLink` needs a controller-level fact
// this file does not have.
constexpr bool node_link_wants_power(TransportPhase phase)
{
    return phase == TransportPhase::Attached || phase == TransportPhase::Connecting ||
           phase == TransportPhase::Ready;
}

// Deliberately not `attadipa::link`'s `is_live()`
// (`link/src/link_state.cpp:11` — "return phase == TransportPhase::Ready || phase == TransportPhase::Connecting;").
// The two ask different questions and now give different answers: `is_live()`
// asks whether a peer is on the other end, this asks whether the radio is in
// use, and `Attached` is the phase that separates them. They looked like one
// predicate while this one was wrong.

// One lease, held for as long as the phase says the link is in use.
//
// Called by the sleeper immediately before `sleep()`, and nowhere else — which
// today means once per power-key release, the one place that asks for a sleep
// (`firmware/main/physical_input.cpp:451` — "sleep_requested_ = true;").
// So the table is sampled per sleep request rather than held as a running
// declaration, and between requests it can report the link held long after the
// link went `Absent`. Nothing observes that: `sleep()`'s own `held()` read is
// the only read of the lease table in the tree, and this call precedes it. The
// day a second reader appears — `outstanding()` on a diagnostics line is the
// obvious one — this call moves to every frame, and it is idempotent so that it
// can: it does nothing at all when the table already agrees with the phase, so
// the steady state costs one comparison and no owner call.
class NodeLinkLease {
public:
    // True when the table now matches the phase. False means the owner refused
    // and `why` says which refusal it was.
    //
    // The two branches drop the handle differently, and the difference is the
    // point. A refused *acquire* changes nothing at all, so the next call asks
    // again — right, because `Exhausted` is recoverable by definition and a link
    // that is still up still wants the lease. A refused *release* has already
    // cleared the handle before it calls, because a release that fails was
    // unbalanced to begin with: the table holds no record of that grant, and
    // keeping the handle would mean re-releasing it forever.
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
        // Dropped before the call, whatever the owner says: see the contract above.
        id_ = kNoLease;
        return owner.release(releasing, why);
    }

    bool held() const { return id_ != kNoLease; }

private:
    LeaseId id_ = kNoLease;
};

}  // namespace attadipa::core
