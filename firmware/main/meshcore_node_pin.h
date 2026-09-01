#pragma once

// What a watch does with the node that has just told it which node it is, and
// how long a refused node is left alone. Nothing else.
//
// Fourth file on the `meshcore_write_outcome.h` pattern, and here for the same
// reason: `meshcore_ble.cpp` is ESP-IDF-only, so a host test cannot reach the
// decision, and a rule tested through a copy is not tested (AGENTS.md, "a test
// of ... an isolated decision helper does not prove the production caller
// works"). What is here is therefore not a helper the production code mirrors
// -- it *is* the production decision. `settle_node_identity()` instantiates it
// with the real provider, NVS and NimBLE; `tests/test_node_pin.cpp`
// instantiates it with fakes, and there is one rule between them.
//
// It is deliberately not a policy engine. It knows three outcomes because this
// watch has three; a fourth would be a line here, not a mechanism.

#include <cstdint>

#include "attadipa/core/mesh_service.h"

namespace attadipa::firmware {

enum class PinOutcome : std::uint8_t {
    // This watch had no pin and now has one: the key was written to NVS and
    // handed to the provider.
    Adopted,
    // Same node, but the write failed. The watch stays unpinned rather than
    // pretending to a pin it could not store, so the next session adopts again.
    AdoptFailed,
    // The node this watch is pinned to. Nothing is written and nothing is torn
    // down.
    Pinned,
    // Some other node. Its address is cooled down and its connection is
    // terminated; nothing is written, no bond is deleted and the node is told
    // nothing.
    Refused,
    // The handshake carried no public key after all, so there is no identity to
    // settle and no decision to make.
    NoIdentity,
    // The connection that carried the frame has already been replaced. This is
    // the only outcome that reaches the refusal path and then does nothing: a
    // cooldown written here would be keyed on the *replacement's* address, and
    // the terminate would end the replacement's connection.
    SessionOver,
};

// Long enough that a scan does not walk straight back into the node it just
// refused; short enough that an operator watching the log sees it retry.
// Chosen, not derived, and nothing depends on the value.
//
// IT IS SCAN-THRASH MITIGATION AND NOTHING ELSE. It is not what enforces the
// pin and it is not a way to re-pin: the enforcement is the key comparison,
// which refuses the same node again on the next handshake whatever its address
// did in between -- which matters because whether this address is the kind that
// stays put or the kind that rotates is UNKNOWN
// (`docs/research/OWNER_DECISIONS.md:1177` -- "kind that rotates is therefore
// `UNKNOWN`").
// If it rotates, the cooldown lapses early and the key comparison still
// refuses; if it does not, the cooldown expires on its own. Neither answer
// changes what this watch talks to.
constexpr std::uint32_t kRefusedNodeCooldownMs = 60000;

// Whether a refusal recorded to expire at `until_ms` is still in force at
// `now_ms`, on a counter that wraps every 49 days.
//
// Unsigned difference, so the wrap reads as "expired" rather than as another 49
// days of refusal. Signed subtraction of the two `std::uint32_t` would be the
// same arithmetic with undefined behaviour on the overflow it exists to handle.
inline bool refusal_active(std::uint32_t until_ms, std::uint32_t now_ms)
{
    return static_cast<std::int32_t>(until_ms - now_ms) > 0;
}

// What the transport knows about the connection the identifying frame arrived
// on, read as one value because that is how the firmware reads it: under one
// critical section, in which the session may still be checked live. Offering
// the three separately would let a test drive a combination the firmware cannot
// produce -- a live session with the next session's address, which is the exact
// state the generation check exists to make unreachable.
struct PinnedSession {
    bool live = false;
    std::uint64_t peer_addr = 0;
    std::uint16_t connection = 0;
    bool has_connection = false;
};

// `Ops` supplies:
//
//   bool node_id(core::MeshPeerId&)     -- the key this session read, if any
//   bool pinned(core::MeshPeerId&)      -- the key this watch is pinned to, if any
//   bool wrong_node()                   -- the provider's verdict on the two
//   bool store(const core::MeshPeerId&) -- write the pin to NVS
//   void adopt(const core::MeshPeerId&) -- hand the provider its pin
//   PinnedSession session()             -- see above; read once, on the refuse path
//   void cool_down(std::uint64_t peer_addr)
//   void disconnect(std::uint16_t connection)
//
// Only the refuse path asks for `session()`. Adopting is valid whatever
// happened to the connection afterwards -- the key was genuinely read, and a
// watch that declined to remember it because the link dropped a millisecond
// later would be unpinned for no reason. Refusing is not: it writes a cooldown
// keyed on an address and terminates a handle, and both belong to a connection
// that may already be somebody else's.
template <typename Ops>
PinOutcome settle_node_pin(Ops& ops, core::MeshPeerId& seen,
                           core::MeshPeerId& expected)
{
    if (!ops.node_id(seen)) return PinOutcome::NoIdentity;

    if (!ops.pinned(expected)) {
        if (!ops.store(seen)) return PinOutcome::AdoptFailed;
        ops.adopt(seen);
        return PinOutcome::Adopted;
    }

    if (!ops.wrong_node()) return PinOutcome::Pinned;

    const PinnedSession live = ops.session();
    if (!live.live) return PinOutcome::SessionOver;
    if (live.peer_addr != 0) ops.cool_down(live.peer_addr);
    if (live.has_connection) ops.disconnect(live.connection);
    return PinOutcome::Refused;
}

}  // namespace attadipa::firmware
