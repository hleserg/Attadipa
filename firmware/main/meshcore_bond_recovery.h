#pragma once

// Which bond an owner is allowed to forget, and nothing else.
//
// Same reasoning as `meshcore_write_outcome.h`, and the same shape: the numbers
// below are NimBLE's, from `host/ble_gap.h`, so the rule cannot live in `link/`
// -- `link/session_owner.h` says "Nothing in this header knows what BLE is" and
// ADR-0015 rests on that. It cannot live inside `meshcore_ble.cpp` either,
// because that translation unit is ESP-IDF-only and no host test can reach it.
// So it lives here, includes nothing but the standard library, and
// `tests/test_session_owner.cpp` compiles this exact file. `meshcore_ble.cpp`
// static_asserts both constants against the BLE_GAP_* definitions they mirror,
// so an upstream renumber is a build failure rather than a silent policy
// change.

#include <array>
#include <cstdint>

namespace attadipa::firmware {

// NimBLE's two answers to `BLE_GAP_EVENT_REPEAT_PAIRING` (`host/ble_gap.h`
// lines 211-212, contract at 1069-1077). That event is not how a stale bond
// reaches this device -- see `record()` -- but the handler still has to answer,
// and this is the answer it gives.
//
// RETRY is named here only to record that this firmware never returns it. The
// stack "will verify the bond has been deleted and continue the pairing
// procedure", so RETRY means deleting the bond *inside the callback* -- and
// Apache NimBLE issue #2206, open as of 2026-08-28, is that this deletion
// happens before Phase 2 authentication. ESP-IDF's `blecent` example does
// exactly that ("this app sacrifices security for convenience"), which is why
// it is read here and not copied: an unauthenticated radio peer in range can
// evict a legitimate bond with one Pairing Request.
inline constexpr int kRepeatPairingRetry = 1;   // BLE_GAP_REPEAT_PAIRING_RETRY
inline constexpr int kRepeatPairingIgnore = 2;  // BLE_GAP_REPEAT_PAIRING_IGNORE

// A peer's identity address, as `ble_gap_conn_find()` reports it. Copied rather
// than referenced because the NimBLE descriptor it comes from is a stack local
// in the callback, and the record outlives the event by design -- it has to,
// because the owner acts on it minutes later.
struct BondIdentity {
    std::array<std::uint8_t, 6> address{};
    std::uint8_t type = 0;
    bool valid = false;
};

// One conflicted bond at a time, and only an owner clears it.
//
// The defect this closes (#325) is not that the shipping handler answers
// IGNORE. IGNORE is the right answer: it is the only one of the two that does
// not delete a bond on a radio peer's say-so. The defect is that answering it
// left nothing behind -- no record of which peer conflicted, no state anyone
// could act on, and no way to recover short of erasing the watch's NVS.
class BondRecovery {
public:
    // Called from the NimBLE host callback with whatever `ble_gap_conn_find()`
    // could tell us about the peer. Records at most one conflict.
    //
    // Two events reach it, and only one of them can happen to this device.
    // `BLE_GAP_EVENT_REPEAT_PAIRING` is raised from
    // `ble_sm_chk_repeat_pairing()` (`host/src/ble_sm.c:990`), whose single
    // call site is `ble_sm_pair_req_rx()` (`:1956`, call at `:2079`) -- the
    // handler for an *inbound* Pairing Request. The watch is the central: it
    // sends that request and receives a Pairing Response, so it never receives
    // one, and a peripheral cannot make it: a peripheral sends a Security
    // Request, which makes the central originate the pairing. What the watch
    // sees instead when the node has lost its keys is the encryption attempt
    // being refused -- `PIN or Key Missing` -- which is the trigger this
    // firmware actually runs on.
    //
    // The first conflict is the one the owner will be told about, and a later
    // peer does not displace it. Any peer in radio range can provoke either
    // event; if the newest one won, a peer could aim the owner's next forget at
    // a bond of its choosing. A conflict from an unidentifiable peer is
    // recorded as nothing at all, which leaves the transport faulted and
    // recoverable only by configuring it again -- fail-closed, deliberately.
    //
    // Recording is not deleting, and that is the whole threat argument. A peer
    // spoofing the node's address and refusing the encryption can already fault
    // this transport today; all it additionally gains here is that the owner is
    // offered a forget it has to run itself, over USB, on a bond whose address
    // it can read in the log.
    void record(const BondIdentity& peer)
    {
        if (!conflicted_.valid && peer.valid) conflicted_ = peer;
    }

    // The repeat-pairing answer. Unreachable in the central role (see
    // `record()`); kept because it is the callback's contract, and because a
    // role that does receive a Pairing Request must not fall through to
    // NimBLE's default.
    int repeat_pairing(const BondIdentity& peer)
    {
        record(peer);
        return kRepeatPairingIgnore;
    }

    bool recovery_required() const { return conflicted_.valid; }

    // The owner action, and the only path that yields an address to delete.
    //
    // It takes no address from its caller: the only bond that can ever be
    // deleted is the one `record()` was given. That is what bounds
    // the operation -- a second forget with no new conflict is refused, so an
    // owner cannot walk the bond store, and neither can anything that reaches
    // this through the debug bridge.
    bool take_forget(BondIdentity& out)
    {
        if (!conflicted_.valid) return false;
        out = conflicted_;
        conflicted_ = BondIdentity{};
        return true;
    }

    // Encryption came up. Whatever was conflicted is no longer the reason the
    // link is down, so it stops being offered as one.
    void pairing_succeeded() { conflicted_ = BondIdentity{}; }

private:
    BondIdentity conflicted_{};
};

}  // namespace attadipa::firmware
