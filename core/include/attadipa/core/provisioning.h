#pragma once

#include <cstdint>

#include "attadipa/core/mesh_service.h"

// What a holder can give this watch by hand, and what the board did with it.
//
// The seam ADR-0018 asks for: `apps/` builds the request from what was typed,
// a board answers it, and nothing above `firmware/` learns that there is a
// PCF85063 or an NVS namespace behind the answer. Two requests, because the
// product has two things a person provisions -- the wall clock and the
// MeshCore passkey -- and the third, forgetting the node the watch is pinned
// to, is what a person does when that node was factory-reset (#411): the
// same screen, one field earlier than the passkey it then needs.
//
// It is deliberately not `debug::TimeSink`/`MeshSink` brought out from behind
// `CONFIG_ATTADIPA_WATCH_CONTROL`: that layer is added to the build only under
// that symbol, so a product image cannot name those types in a signature.

namespace attadipa::core {

enum class ProvisionOutcome : std::uint8_t {
    Accepted,  // Applied and, where it persists, persisted. Terminal, and it
               // is what a screen is allowed to call "set up".
    Rejected,  // Not a value this watch takes. Nothing changed.
    Failed,    // Storage or hardware refused; part of it may have moved.
    Pending,   // Taken by hardware that answers on another task, and not
               // finished. `mesh_passkey_outcome()` is where the answer
               // arrives and the only place it does. Until #416 this value
               // did not exist and a radio's `Accepted` meant "handed to it":
               // the watch said it was set up, and a stack or a flash write
               // that then refused reached the serial log and stopped there.
};

// How forgetting the node ended. One value names what actually landed,
// because the operation is several clears and a screen that reported the
// first of them as success would be #378 with more state.
enum class MeshForgetOutcome : std::uint8_t {
    Pending,     // The radio has the request and has not finished.
    Forgotten,   // The stale bond and the pin are both gone. The watch is
                 // silent until a passkey is entered; that entry arms it.
    Unpinned,    // No stale bond was recorded, so the bond was kept and the
                 // pin alone is gone -- the state where the watch had paired
                 // afresh and then refused the node's new key.
    BondKept,    // The bond store refused to delete the bond. Nothing
                 // changed; the same request can be made again.
    PinOnFlash,  // Forgotten in memory and not on flash: a restart before
                 // the next node is adopted brings the old pin back.
    Nothing,     // There was neither a bond nor a pin to forget.
};

struct WallClockEntry {
    std::int64_t utc_seconds = 0;
    std::int16_t timezone_offset_minutes = 0;
};

class Provisioner {
public:
    virtual ~Provisioner() = default;

    // The clock is written by the task that asks, so its answer is terminal:
    // this one never returns `Pending`.
    virtual ProvisionOutcome set_wall_clock(const WallClockEntry& entry) = 0;

    // The radio is not. `Rejected` and `Failed` still mean the request never
    // got as far as the hardware -- a passkey this watch does not take, a
    // queue that would not hold it -- and `Pending` means it did and the
    // answer is not known yet. An implementation that answers `Pending` owes
    // exactly one `mesh_passkey_outcome()` other than `Pending` afterwards.
    virtual ProvisionOutcome set_mesh_passkey(std::uint32_t passkey) = 0;

    // The answer to a `Pending` passkey, or `Pending` while the radio still
    // has it. Consumed once: the caller that shows the terminal screen is the
    // only one that sees it, and an answer belonging to a request that was
    // abandoned is not handed to the one that replaced it.
    //
    // With nothing outstanding this answers `Failed`, not `Pending`: a screen
    // waiting on an answer that is never coming would wait for ever, and there
    // is no deadline behind it.
    virtual ProvisionOutcome mesh_passkey_outcome() = 0;

    // The node this watch is pinned to, if it is pinned to one. False is
    // "no node", and the entry screen then has nothing to offer forgetting.
    virtual bool mesh_node(MeshPeerId& out) = 0;

    // Asks the radio to forget that node: its stale bond where one was
    // recorded, and the pin in both the places it is kept. Never `Accepted`:
    // the clears run on the radio's task, so a `Pending` here owes exactly one
    // `mesh_forget_outcome()` other than `Pending`. `Rejected` means there is
    // nothing to forget, `Failed` that the request could not be queued.
    //
    // Forgetting arms nothing. The watch stays silent until a passkey is
    // entered, and that entry -- the same one a first provisioning makes --
    // is what arms exactly one attempt; a reconnect that a forget re-armed by
    // itself would adopt the first node to answer before the person had
    // reached the passkey field.
    virtual ProvisionOutcome forget_mesh_node() = 0;

    // The answer to a `Pending` forget, consumed once like the passkey's.
    // With nothing outstanding this answers `BondKept`: nothing of the
    // caller's is in flight, and "nothing changed" is the one claim that
    // cannot be a stale success.
    virtual MeshForgetOutcome mesh_forget_outcome() = 0;
};

}  // namespace attadipa::core
