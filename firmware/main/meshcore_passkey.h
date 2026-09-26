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

// A retained passkey is normally replayed at boot. Forgetting a node keeps
// those digits for the holder to replace, so a separate durable gate prevents
// a reboot in between from arming them on an unpinned watch. A passkey write
// that did not finish leaves a second gate up; replay needs both down, and
// which one a step touches is `persist_passkey()`'s to say.
enum class PasskeyReplay : std::uint8_t {
    Allowed,
    Inhibited,
    Unreadable,
};

enum class PasskeyRestore : std::uint8_t {
    Restored,   // a pairing passkey was on flash and is queued for the worker
    Absent,
    Unreadable,
    Refused,    // on flash, and not a pairing passkey: the probe zero, or junk
    NotQueued,  // a pairing passkey was on flash and the worker's queue is full
    ReplayInhibited,  // a completed forget, or an unfinished passkey write,
                      // still awaits new owner-entered digits
    ReplayUnreadable, // the durable replay gate could not be read; fail closed
};

// `Ops` supplies:
//
//   PasskeyReplay replay_permission()
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

// The worker's write, once the stack has taken the digits. `Ops` supplies:
//
//   bool raise_write_gate()         -- the passkey write's durable gate
//   bool store(std::uint32_t passkey)
//   bool lower_forget_gate()        -- the forget-node one: stored digits are
//                                      entered digits
//   bool lower_write_gate()
//
// Which gate each step touches is decided here, where a host test can see it
// (#674); the firmware only maps each name to its NVS key.
//
// A refused NVS write is not a write that did nothing (#648): replacing a
// stored value writes the new entry before it erases the old one, and reports
// that erase failing as a failure. So the write gate goes up before the digits
// are touched and comes down last, after they are stored. What the next boot
// arms:
//
//   raise_write_gate() refuses  -- what it armed before: the previous digits,
//                                  or nothing while a forget is pending; or,
//                                  if the gate went up anyway, nothing
//   store() refuses             -- nothing: the write gate is up
//   lower_forget_gate() refuses -- nothing: the write gate is still up
//   lower_write_gate() refuses  -- nothing, or these digits: a refused erase
//                                  may have erased, and this boot cannot read
//                                  back which (VERIFIED_FACTS, "A failed erase
//                                  may already have erased")
//
// Power lost between the first and the last step also leaves the gate up, so
// the next boot arms nothing and the owner enters the digits again. False is
// `NotStored`.
template <typename Ops>
bool persist_passkey(Ops& ops, std::uint32_t passkey)
{
    return ops.raise_write_gate() && ops.store(passkey) &&
           ops.lower_forget_gate() && ops.lower_write_gate();
}

// `Deconfigure`'s off switch: the digits go, then both gates. With no digits
// on flash a gate guards nothing, and leaving one up would leave the one reset
// a watch has short of erasing NVS unable to clear it (#674) -- and a forget
// gate left up makes the next boot report a retained passkey that this erased.
// The order is the point: a gate lowered over digits that would not erase would
// arm the very digits `Deconfigure` was asked to stop. `Ops` supplies `erase()`,
// `lower_forget_gate()` and `lower_write_gate()`. False: the next boot may
// still scan.
//
// `Deconfigure` is reached only from the HIL. On a product image a store NVS
// refuses every time still leaves each boot arming nothing, and the owner
// enters the digits once per boot; the session pairs either way.
template <typename Ops>
bool erase_passkey(Ops& ops)
{
    return ops.erase() && ops.lower_forget_gate() && ops.lower_write_gate();
}

// The boot side. A value this image would not have stored is refused rather
// than replayed, and a replay is never re-persisted: flash already holds it,
// and a boot that rewrote its own input would turn one refused write into a
// log line at every start.
template <typename Ops>
PasskeyRestore restore_passkey(Ops& ops)
{
    switch (ops.replay_permission()) {
    case PasskeyReplay::Inhibited: return PasskeyRestore::ReplayInhibited;
    case PasskeyReplay::Unreadable: return PasskeyRestore::ReplayUnreadable;
    case PasskeyReplay::Allowed: break;
    }
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
