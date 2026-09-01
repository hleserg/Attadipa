#pragma once

// What to do about a status `ble_gattc_write_flat()` rejected a Companion frame
// with, and nothing else.
//
// This header is deliberately tiny, deliberately dependency-free, and
// deliberately *not* in `link/`. The numbers below are NimBLE's, from
// `host/ble_hs.h`, and they mean nothing in any other transport's status space
// -- `link/session_owner.h` says "Nothing in this header knows what BLE is" and
// ADR-0015 rests on that, so a rule spelled in BLE_HS numbers cannot live
// there. It cannot live inside `meshcore_ble.cpp` either: that translation unit
// is ESP-IDF-only and no host test can reach it, and a rule tested through a
// copy is not tested (AGENTS.md, "a test of ... an isolated decision helper
// does not prove the production caller works").
//
// So it lives here, includes nothing, and `tests/test_session_owner.cpp`
// compiles this exact file -- the pattern `test_time_service` already uses.
// `meshcore_ble.cpp` static_asserts every constant against the BLE_HS_*
// definition it mirrors, so an upstream renumber is a build failure.

#include <cstdint>

namespace attadipa::firmware {

// The two statuses that mean the radio subsystem itself failed, rather than
// this session failing. Everything else a `ble_gattc_write_flat()` can reject
// with is session-scoped: the peer went away (`BLE_HS_ENOTCONN`), the host ran
// out of mbufs (`BLE_HS_ENOMEM`), or the request was malformed
// (`BLE_HS_EINVAL`, `BLE_HS_EMSGSIZE`). None of those are a reason to stop
// scanning for the rest of the boot.
inline constexpr std::int32_t kWriteStatusStackFailure = 11;       // BLE_HS_EOS
inline constexpr std::int32_t kWriteStatusControllerFailure = 12;  // BLE_HS_ECONTROLLER

enum class WriteOutcome : std::uint8_t {
    // End this session and let the ordinary disconnect path recover it. The
    // same outcome an asynchronous write failure has always had.
    Recycle,
    // Fail closed: fault the transport and stop reconnecting until the device
    // is configured again. Reserved for the two statuses above.
    Fault,
};

// #335 was `rc != 0 => permanent`: an ordinary peer disappearance disabled
// reconnect until the device was configured again, while the *same* status
// arriving 40 ms later through the completion callback cost one session and
// recovered. The two arms now agree everywhere except where the stack itself
// says it is broken -- and this file already ruled twice, with a bench finding,
// that backpressure is not a broken subsystem (`meshcore_ble.cpp`
// "A full queue is backpressure, not a broken subsystem").
constexpr WriteOutcome classify_write_failure(std::int32_t result)
{
    return result == kWriteStatusStackFailure ||
                   result == kWriteStatusControllerFailure
               ? WriteOutcome::Fault
               : WriteOutcome::Recycle;
}

}  // namespace attadipa::firmware
