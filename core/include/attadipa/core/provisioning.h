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
    Accepted,  // Applied and, where it persists, persisted -- or, for a
               // radio, handed to it: a stack that then refuses is not
               // reported back here.
    Rejected,  // Not a value this watch takes. Nothing changed.
    Failed,    // Storage or hardware refused; part of it may have moved.
};

struct WallClockEntry {
    std::int64_t utc_seconds = 0;
    std::int16_t timezone_offset_minutes = 0;
};

class Provisioner {
public:
    virtual ~Provisioner() = default;
    virtual ProvisionOutcome set_wall_clock(const WallClockEntry& entry) = 0;
    virtual ProvisionOutcome set_mesh_passkey(std::uint32_t passkey) = 0;
};

}  // namespace attadipa::core
