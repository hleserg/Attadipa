#pragma once

// Forgetting the node this watch is pinned to, in the order the clears have to
// run and with one word for what landed -- and nothing else.
//
// #411, from the research in docs/research/MESHCORE_NODE_RESET_RECOVERY.md
// §6: a factory-reset node leaves the watch with a bond the node no longer
// honours and a pin the node's new key no longer matches, and a product image
// had no way to clear either. The clears are several -- the live session, the
// recorded bond, the pin on flash, the pin in memory, the refusal cooldown --
// and the report's §6 is why they are one operation: a surface that reported
// the first of them as success is #378 with more state.
//
// Dependency-free for the reason `meshcore_passkey_outcome.h` gives:
// `meshcore_ble.cpp` is ESP-IDF-only and no host test can reach it, so the
// sequence lives here, `meshcore_ble.cpp` instantiates it with NimBLE and NVS
// behind `Ops`, and `tests/test_provisioning.cpp` instantiates the same
// function with a fake store (AGENTS.md -- "a test of ... an isolated decision
// helper does not prove the production caller works", so the helper is the
// production sequence, not a copy of it).
//
// What it deliberately does NOT do is re-arm the radio. `reconnect_allowed`
// is dropped first and left down: after a forget the watch is unpinned, and a
// reconnect it armed by itself would adopt the first node to answer before
// the person holding it had reached the passkey field. The Configure that the
// passkey entry posts is the one arm, exactly as it is for a first
// provisioning; §7 of the report is where that rule is written down.
//
// `quiesce_gap()` and `start_discovery()` below are here for the same reason
// and are not about forgetting a node: they are the two halves of who owns
// GAP discovery, they have to agree, and a forget is one of their callers.

#include <cstdint>

#include "meshcore_bond_recovery.h"

namespace attadipa::firmware {

enum class ForgetNodeOutcome : std::uint8_t {
    // The slot's two states, spelled as `TicketedOperation` needs them.
    Idle,
    InFlight,
    // The recorded bond is deleted and the pin is gone from flash and memory.
    Forgotten,
    // No stale bond was recorded, so the bond -- a good one, made after the
    // node's reset -- is kept, and the pin alone is gone. §6.1 state (b).
    Unpinned,
    // A prerequisite or the bond store refused. Any taken record is put back
    // and neither pin copy is changed. Transport may already be stopped, so
    // the request must report a retry rather than claiming nothing changed.
    BondKept,
    // The pin is gone from memory and the NVS erase refused. The next
    // adoption overwrites the key anyway, so this holds until the watch is
    // next restarted before a node is adopted -- which re-pins the old key
    // out of flash.
    PinOnFlash,
    // Neither a recorded bond nor a pin: nothing to forget, and nothing
    // was.
    Nothing,
    // Trust was kept, but rolling back the durable recovery marker failed.
    // Boot therefore stays disarmed until this is retried successfully.
    ReplayInhibited,
};

// `Pending` includes NimBLE's already-terminating answer; `Gone` is its
// already-disconnected answer, while `Absent` means SessionOwner had no handle.
enum class ForgetTransportTermination : std::uint8_t {
    Absent,
    Pending,
    Gone,
    Refused,
};

// The exact production seam between SessionOwner and the asynchronous radio.
// `start` runs outside the session lock. Only an accepted disconnect makes the
// captured generation stale, and that happens before forget_node() can clear
// trust, so frames already queued by the host task cannot put the pin back.
template <typename Snapshot, typename Start, typename End>
ForgetTransportTermination terminate_forget_session(
    Snapshot snapshot, Start start, End end, std::uint16_t no_connection)
{
    const auto session = snapshot();
    if (session.connection == no_connection) {
        end(session.generation);
        return ForgetTransportTermination::Absent;
    }
    const ForgetTransportTermination result = start(session.connection);
    if (result != ForgetTransportTermination::Refused) end(session.generation);
    return result;
}

// Stopping everything the GAP layer may be doing for this transport: reconnect
// disarmed first, so a discovery report already on the host task cannot start
// a connection after the cancels below; then the scan, a pending connection,
// and the live session. Forgetting a node and faulting on a refused passkey
// (#628) both need exactly this and nothing more.
//
// `Ops`:
//   void disarm();                          reconnect_allowed <- false
//   bool discovering();                     ble_gap_disc_active()
//   bool cancel_discovery();                accepted or already over
//   bool connecting();                      ble_gap_conn_active()
//   bool cancel_connect();                  accepted or already over
//   ForgetTransportTermination end_session();  terminate_forget_session()
template <typename Ops>
ForgetTransportTermination quiesce_gap(Ops& ops)
{
    ops.disarm();
    if (ops.discovering() && !ops.cancel_discovery())
        return ForgetTransportTermination::Refused;
    if (ops.connecting() && !ops.cancel_connect())
        return ForgetTransportTermination::Refused;
    return ops.end_session();
}

// How a start of discovery ended. The last two are the race below: the scan
// was started and then taken back, because by the time it existed nothing
// wanted it any more.
enum class ScanStart : std::uint8_t {
    // The gate was down, or a scan was already running. The stack was not
    // asked for anything.
    Skipped,
    // The stack would not start one. Nothing is scanning.
    Refused,
    // Started, and still wanted once it had started.
    Scanning,
    // Started after a disarm, and cancelled again.
    Stopped,
    // Started after a disarm, and the cancel was refused. Somebody else owes
    // the retry; `cancel_discovery()` is where that was recorded.
    StopOwed,
};

// Starting discovery, and owning what was started. The other half of
// `quiesce_gap()`, and one decision with it (#628).
//
// `quiesce_gap()` disarms and then asks whether a scan is running, which is
// the truth at that instant and about nothing else. A caller on the radio's
// own callback task that has read the gate armed and has not yet reached the
// start is invisible to it: the teardown finds nothing to cancel, the fault is
// published, and the unbounded scan begins afterwards, behind a phase that
// says the transport failed and needs a reset. Neither half was wrong on its
// own. The check and the start were two operations with a disarm allowed in
// between, and disarming only stops the entries that have not happened yet.
//
// So the start reads the gate again once the stack has taken it, and undoes
// itself when the disarm won. Whichever of the two runs second sees the
// other's work -- the disarm before this second read, or the scan before
// `quiesce_gap()`'s `discovering()` -- so they cannot both find nothing, and
// no step here needs a lock held across a call into the stack.
//
// `Ops`:
//   bool armed();             the stack is up, configured, reconnect allowed
//   bool discovering();       ble_gap_disc_active()
//   bool begin();             ble_gap_disc(..., BLE_HS_FOREVER, ...) == 0
//   bool cancel_discovery();  accepted or already over; owes the retry if not
template <typename Ops>
ScanStart start_discovery(Ops& ops)
{
    if (!ops.armed() || ops.discovering()) return ScanStart::Skipped;
    if (!ops.begin()) return ScanStart::Refused;
    if (ops.armed()) return ScanStart::Scanning;
    return ops.cancel_discovery() ? ScanStart::Stopped : ScanStart::StopOwed;
}

// `Ops` is the board:
//   void disarm();                          reconnect_allowed <- false
//   bool terminate();                       end the live session, if any
//   bool mark_reprovision();                durable boot-replay inhibitor
//   bool cancel_reprovision();              undo it when trust was unchanged
//   bool take_forget(BondIdentity& out);    BondRecovery::take_forget
//   bool delete_bond(const BondIdentity&);  ble_store_util_delete_peer == 0
//   void record(const BondIdentity&);       BondRecovery::record, on refusal
//   bool erase_pin();                       the NVS key; true when absent
//   bool unpin();                           MeshCoreCompanion::unpin
//   void clear_refusal();                   the cooldown and its floor
template <typename Ops>
ForgetNodeOutcome forget_node(Ops& ops)
{
    // Down before anything else, as Deconfigure does: the disconnect that
    // the terminate below causes must not be the reconnect that adopts a
    // node while the clears are half done.
    ops.disarm();
    if (!ops.terminate()) return ForgetNodeOutcome::BondKept;

    // The passkey itself is deliberately retained. This marker is therefore
    // the crash-safe half of disarm(): a reboot after any clear below still
    // waits for new owner-entered digits instead of replaying the old ones.
    if (!ops.mark_reprovision()) return ForgetNodeOutcome::BondKept;

    BondIdentity peer{};
    const bool taken = ops.take_forget(peer);
    if (taken && !ops.delete_bond(peer)) {
        // The store said no and the bond is still there. The record goes back
        // so the next request finds it, and the pin is left alone: clearing
        // it beside a bond that still blocks every connection would be
        // today's dead end with the one clue to it removed.
        ops.record(peer);
        return ops.cancel_reprovision() ? ForgetNodeOutcome::BondKept
                                        : ForgetNodeOutcome::ReplayInhibited;
    }

    // Flash before memory. A restart between the two then finds no pin,
    // which is the state this operation is for; the other order would find
    // the old key and put it back.
    const bool erased = ops.erase_pin();
    const bool was_pinned = ops.unpin();
    ops.clear_refusal();

    if (!taken && !was_pinned && erased) {
        return ops.cancel_reprovision() ? ForgetNodeOutcome::Nothing
                                        : ForgetNodeOutcome::ReplayInhibited;
    }
    if (!erased) return ForgetNodeOutcome::PinOnFlash;
    return taken ? ForgetNodeOutcome::Forgotten : ForgetNodeOutcome::Unpinned;
}

}  // namespace attadipa::firmware
