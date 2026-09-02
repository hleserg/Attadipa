// The passkey rule in `firmware/main/meshcore_passkey.h`, driven by a fake
// store. This is the production sequence and not a copy of it:
// `meshcore_ble.cpp` instantiates the same two templates with NVS and its
// event queue. What it does not reach is the worker that writes the flash when
// `persist` is true, or erases it on `Deconfigure` -- those run on a board.

#include <cstdio>

#include "meshcore_passkey.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

using attadipa::firmware::PasskeyRestore;
using attadipa::firmware::StoredPasskey;

struct FakeStore {
    StoredPasskey on_flash = StoredPasskey::Absent;
    std::uint32_t value = 0;
    bool queue_full = false;

    // What the worker was asked, last.
    unsigned configures = 0;
    std::uint32_t configured = 0;
    bool persisted = false;

    StoredPasskey load(std::uint32_t& out)
    {
        if (on_flash == StoredPasskey::Found) out = value;
        return on_flash;
    }
    bool configure(std::uint32_t passkey, bool persist)
    {
        if (queue_full) return false;
        ++configures;
        configured = passkey;
        persisted = persist;
        return true;
    }
};

void test_request()
{
    using attadipa::firmware::request_passkey;

    // A pairing passkey is armed and marked for flash.
    FakeStore store;
    CHECK(request_passkey(store, 123456));
    CHECK(store.configures == 1 && store.configured == 123456);
    CHECK(store.persisted);

    // The probe zero is armed and stays with this boot.
    CHECK(request_passkey(store, 0));
    CHECK(store.configures == 2 && store.configured == 0);
    CHECK(!store.persisted);

    // Six digits is the whole range; the edge is inside.
    CHECK(request_passkey(store, 999999));
    CHECK(store.persisted);
    CHECK(!request_passkey(store, 1000000));
    CHECK(store.configures == 3);

    // A full queue is the caller's answer, not a stored passkey.
    store.queue_full = true;
    CHECK(!request_passkey(store, 123456));
}

void test_restore()
{
    using attadipa::firmware::restore_passkey;

    FakeStore store;
    CHECK(restore_passkey(store) == PasskeyRestore::Absent);
    CHECK(store.configures == 0);

    store.on_flash = StoredPasskey::Unreadable;
    CHECK(restore_passkey(store) == PasskeyRestore::Unreadable);
    CHECK(store.configures == 0);

    // What was stored is replayed, and not stored again.
    store.on_flash = StoredPasskey::Found;
    store.value = 123456;
    CHECK(restore_passkey(store) == PasskeyRestore::Restored);
    CHECK(store.configures == 1 && store.configured == 123456);
    CHECK(!store.persisted);

    // A zero on flash is the probe, or a write this image never made: a boot
    // does not turn link encryption off on its own.
    store.value = 0;
    CHECK(restore_passkey(store) == PasskeyRestore::Refused);
    CHECK(store.configures == 1);
    store.value = 1000000;
    CHECK(restore_passkey(store) == PasskeyRestore::Refused);
    CHECK(store.configures == 1);

    store.value = 654321;
    store.queue_full = true;
    CHECK(restore_passkey(store) == PasskeyRestore::NotQueued);
    CHECK(store.configures == 1);
}

}  // namespace

int main()
{
    test_request();
    test_restore();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("meshcore_passkey: OK");
    return 0;
}
