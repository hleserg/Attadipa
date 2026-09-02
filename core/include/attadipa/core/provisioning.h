#pragma once

#include <cstdint>

// What a holder can give this watch by hand, and what the board did with it.
//
// The seam ADR-0018 asks for: `apps/` builds the request from what was typed,
// a board answers it, and nothing above `firmware/` learns that there is a
// PCF85063 or an NVS namespace behind the answer. Two requests, because the
// product has two things a person provisions -- the wall clock and the
// MeshCore passkey -- and a third would be a line here, not a mechanism.
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
};

}  // namespace attadipa::core
