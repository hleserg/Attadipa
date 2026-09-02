#pragma once

// Which passkey outlives a boot, and what a stored one is allowed to do at the
// next. Nothing else.
//
// Fourth file on the `meshcore_boot.h` pattern, for the reason that file
// gives: `meshcore_ble.cpp` is ESP-IDF-only, and the round-1 review of #356
// found the probe zero being persisted -- a rule that no host test could have
// failed, because it lived inside the worker. What is here is not a helper the
// worker mirrors; `configure_meshcore_ble()` and `restore_passkey()` in
// `meshcore_ble.cpp` instantiate it with NVS and the event queue, and
// `tests/test_meshcore_passkey.cpp` instantiates it with a fake store.

#include <cstdint>

namespace attadipa::firmware {

// A six-digit BLE static passkey. Zero is `mesh-configure --unpaired-probe`,
// which turns pairing and with it link encryption off for one session -- a
// diagnostic an operator asks for while watching the console, never something
// a boot repeats on its own.
constexpr std::uint32_t kPasskeyMax = 999999;

constexpr bool is_pairing_passkey(std::uint32_t passkey)
{
    return passkey != 0 && passkey <= kPasskeyMax;
}

enum class StoredPasskey : std::uint8_t {
    Absent,      // no namespace, or no key
    Unreadable,  // NVS refused
    Found,       // `out` holds whatever flash held, unchecked
};

enum class PasskeyRestore : std::uint8_t {
    Restored,   // a pairing passkey was on flash and is queued for the worker
    Absent,
    Unreadable,
    Refused,    // on flash, and not a pairing passkey: the probe zero, or junk
    NotQueued,  // a pairing passkey was on flash and the worker's queue is full
};

// `Ops` supplies:
//
//   StoredPasskey load(std::uint32_t& out)
//   bool configure(std::uint32_t passkey, bool persist)
//       -- queue a Configure for the worker; `persist` is whether the worker
//          writes it to flash once the stack has taken it. false = queue full.

// An operator's request, from the bridge or the watch's own entry screen.
// Only a pairing passkey is marked for flash: the probe stays with the session
// that asked for it.
template <typename Ops>
bool request_passkey(Ops& ops, std::uint32_t passkey)
{
    if (passkey > kPasskeyMax) return false;
    return ops.configure(passkey, is_pairing_passkey(passkey));
}

// The boot side. A value this image would not have stored is refused rather
// than replayed, and a replay is never re-persisted: flash already holds it,
// and a boot that rewrote its own input would turn one refused write into a
// log line at every start.
template <typename Ops>
PasskeyRestore restore_passkey(Ops& ops)
{
    std::uint32_t passkey = 0;
    switch (ops.load(passkey)) {
    case StoredPasskey::Absent: return PasskeyRestore::Absent;
    case StoredPasskey::Unreadable: return PasskeyRestore::Unreadable;
    case StoredPasskey::Found: break;
    }
    if (!is_pairing_passkey(passkey)) return PasskeyRestore::Refused;
    return ops.configure(passkey, false) ? PasskeyRestore::Restored
                                         : PasskeyRestore::NotQueued;
}

}  // namespace attadipa::firmware
