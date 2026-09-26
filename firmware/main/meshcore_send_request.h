#pragma once

// Which MeshCore send arguments the board adapter will pass to the worker, and
// which it refuses on the spot -- and nothing else.
//
// #609: `BoardMeshSink::send()` bounded the text at a literal 160 while
// `meshcore_ble.cpp:2568` -- "    if (text.empty() || text.size() > attadipa::core::kMeshTextBytes) {"
// -- refuses anything past 128. The 32 bytes in between were accepted by the
// adapter, refused synchronously one call deeper before the send slot was
// claimed or anything was queued, and reached the operator as
// `ErrorCode::OperationFailed` -- a hardware operation reported as failed
// without ever having been attempted. `send_room()` beside it was already
// written against `core::kMeshTextBytes`, so the two halves of one adapter
// disagreed about one field. They are one rule here instead, which is also
// what stops the next literal from being added to only one of them.
//
// Dependency-free so that a host test compiles this exact file. The adapter
// lives in `firmware/main/waveshare_board.cpp`, which is ESP-IDF-only and
// which no host test can reach, and a rule tested through a copy is not tested
// -- AGENTS.md: "A test of a fixture, copied implementation, generated patch,
// or isolated decision helper does not prove the production caller works."
// `tests/test_meshcore_send_request.cpp` compiles this file and
// `waveshare_board.cpp` calls it; `firmware/main/meshcore_passkey_outcome.h`
// is the same arrangement for the same reason.
//
// These answer one question -- is this argument list sendable -- and never
// whether the radio, the session or the queue can take it. Everything refused
// here is bad input, so the caller answers `MeshSinkResult::Rejected` and the
// bridge turns that into `ErrorCode::BadInput`. A false answer from
// `meshcore_ble_send()` *after* this has passed is a real subsystem refusing a
// well-formed request, and that one stays `Failed`.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "attadipa/core/mesh_service.h"

namespace attadipa::firmware {

// MeshCore text as this product sends it. 128 bytes is a decision and not a
// frame size -- `docs/research/OUTBOUND_MESHCORE_MESSAGES.md:552` -- "So: 128
// out, 128 in, one number, and the asymmetry with upstream's 160 is" -- so the
// bound is the product constant and never a second literal beside it.
//
// The NUL is not pedantry: the worker copies this into a fixed C string and
// terminates it, so a NUL inside the span would ship a silently truncated
// message rather than the one that was typed.
inline bool mesh_text_sendable(const char* text, std::size_t length)
{
    return text != nullptr && length != 0 &&
           length <= attadipa::core::kMeshTextBytes &&
           std::memchr(text, '\0', length) == nullptr;
}

// The wire carries a signed 64-bit second count and the worker takes a
// `WallTime` the MeshCore frame narrows to 32 bits, so the range is refused
// here rather than wrapped there.
inline bool mesh_timestamp_sendable(std::int64_t utc_seconds)
{
    return utc_seconds >= 0 &&
           utc_seconds <= std::numeric_limits<std::uint32_t>::max();
}

// A whole 32-byte recipient key, since #573; the pointer is all an array
// parameter decays to, so its length is the bridge's guarantee and not this
// one's -- `debug/include/attadipa/debug/bridge.h:187` -- "// MeshSend under 41 body bytes and MeshRoomSend under 42 before either call,".
// That sentence said "under 15" until this change, which was the six-byte
// prefix's arithmetic and would have left seven bytes for a thirty-two byte
// key; a decision not to re-check here should not rest on a stale number.
inline bool mesh_send_arguments_ok(const std::uint8_t* peer_key,
                                   const char* text, std::size_t text_length,
                                   std::int64_t utc_seconds)
{
    return peer_key != nullptr && mesh_text_sendable(text, text_length) &&
           mesh_timestamp_sendable(utc_seconds);
}

// 15 password bytes is the Room Server's own bound and stays a literal here
// because it is one: `meshcore_ble.cpp:2587` -- "    if (password.empty() ||
// password.size() > 15 || text.empty() ||" -- is the check this one guards, and
// giving it a name in this file alone would create the second source of truth
// the text bound above exists to avoid.
inline bool mesh_room_send_arguments_ok(const std::uint8_t* room,
                                        const char* password,
                                        std::size_t password_length,
                                        const char* text, std::size_t text_length,
                                        std::int64_t utc_seconds)
{
    return room != nullptr && password != nullptr && password_length != 0 &&
           password_length <= 15 &&
           std::memchr(password, '\0', password_length) == nullptr &&
           mesh_text_sendable(text, text_length) &&
           mesh_timestamp_sendable(utc_seconds);
}

}  // namespace attadipa::firmware
