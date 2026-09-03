// The passkey rule in `firmware/main/meshcore_passkey.h`, driven by a fake
// store. This is the production sequence and not a copy of it:
// `meshcore_ble.cpp` instantiates the same two templates with NVS and its
// event queue. What it does not reach is the worker that writes the flash when
// `persist` is true, or erases it on `Deconfigure` -- those run on a board.

//
// The second half of the file is the slot the answer comes back in --
// `firmware/main/meshcore_passkey_outcome.h`, the shipping file again -- which
// is what stops a queued request from being read as an armed passkey (#416).

#include <cstdio>

#include "meshcore_passkey.h"
#include "meshcore_passkey_outcome.h"

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
using attadipa::firmware::PasskeyReplay;
using attadipa::firmware::StoredPasskey;

struct FakeStore {
    PasskeyReplay replay = PasskeyReplay::Allowed;
    StoredPasskey on_flash = StoredPasskey::Absent;
    std::uint32_t value = 0;
    bool queue_full = false;

    // What the worker was asked, last.
    unsigned configures = 0;
    std::uint32_t configured = 0;
    bool persisted = false;

    PasskeyReplay replay_permission() const { return replay; }

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

    // Forget is crash-safe: while the durable recovery marker is present,
    // boot must not replay the retained old digits into an unpinned watch.
    store.replay = PasskeyReplay::Inhibited;
    store.on_flash = StoredPasskey::Found;
    store.value = 123456;
    CHECK(restore_passkey(store) == PasskeyRestore::ReplayInhibited);
    CHECK(store.configures == 0);

    // An unreadable gate fails closed too. Treating it as absent would be the
    // same silent first-answer adoption as losing the marker entirely.
    store.replay = PasskeyReplay::Unreadable;
    CHECK(restore_passkey(store) == PasskeyRestore::ReplayUnreadable);
    CHECK(store.configures == 0);

    store.replay = PasskeyReplay::Allowed;

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

// --- The answer slot ------------------------------------------------------

using attadipa::firmware::PasskeyOperation;
using attadipa::firmware::PasskeyOutcome;

// A queued request is not an armed passkey, and nothing but the worker can say
// it is.
void test_slot_is_not_answered_until_the_worker_answers()
{
    PasskeyOperation op;
    std::uint32_t ticket = 0;
    CHECK(op.reserve(ticket));
    CHECK(ticket != 0);
    CHECK(op.take(ticket) == PasskeyOutcome::InFlight);
    CHECK(op.take(ticket) == PasskeyOutcome::InFlight);

    op.complete(ticket, PasskeyOutcome::Armed);
    CHECK(op.take(ticket) == PasskeyOutcome::Armed);
    // Once. A second reader would show a second screen the same success.
    CHECK(op.take(ticket) == PasskeyOutcome::Idle);
}

// Both worker failures survive the crossing, and neither is Armed.
void test_slot_carries_both_failures()
{
    const PasskeyOutcome bad[] = {PasskeyOutcome::Refused,
                                  PasskeyOutcome::NotStored};
    for (const PasskeyOutcome outcome : bad) {
        PasskeyOperation op;
        std::uint32_t ticket = 0;
        CHECK(op.reserve(ticket));
        op.complete(ticket, outcome);
        CHECK(op.take(ticket) == outcome);
        CHECK(op.take(ticket) == PasskeyOutcome::Idle);
    }
}

// One at a time. Two OKs in a row used to be two requests over one answer.
void test_one_operation_at_a_time()
{
    PasskeyOperation op;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    CHECK(op.reserve(first));
    CHECK(!op.reserve(second));
    CHECK(second == 0);  // untouched by a refusal

    // Answered, and the slot is free again.
    op.complete(first, PasskeyOutcome::Armed);
    CHECK(op.take(first) == PasskeyOutcome::Armed);
    CHECK(op.reserve(second));
    CHECK(second != first);
}

// The request never got queued: the slot goes back rather than staying in
// flight behind a request that does not exist.
void test_release_frees_the_slot()
{
    PasskeyOperation op;
    std::uint32_t first = 0;
    CHECK(op.reserve(first));
    op.release(first);
    CHECK(op.take(first) == PasskeyOutcome::Idle);
    std::uint32_t second = 0;
    CHECK(op.reserve(second));
    CHECK(second != first);
}

// #416's third Definition of Done: a screen that was cancelled leaves an
// operation nobody is waiting on, and the screen that replaces it must not be
// finished by that operation's answer.
void test_a_stale_completion_cannot_finish_the_next_session()
{
    PasskeyOperation op;
    std::uint32_t abandoned = 0;
    CHECK(op.reserve(abandoned));
    // The screen is cancelled; the worker finishes anyway, into a slot nobody
    // is going to read.
    op.complete(abandoned, PasskeyOutcome::Armed);

    // A new screen asks. An answer nobody collected does not refuse -- the
    // watch would be unprovisionable until it rebooted -- and the request that
    // takes the slot does not inherit that answer either.
    std::uint32_t fresh = 0;
    CHECK(op.reserve(fresh));
    CHECK(fresh != abandoned);
    CHECK(op.take(fresh) == PasskeyOutcome::InFlight);
    CHECK(op.take(abandoned) == PasskeyOutcome::Idle);

    // A completion quoting the old ticket lands nowhere, whenever it arrives.
    op.complete(abandoned, PasskeyOutcome::Armed);
    CHECK(op.take(fresh) == PasskeyOutcome::InFlight);

    // The answer to this request is the one this request gets.
    op.complete(fresh, PasskeyOutcome::NotStored);
    CHECK(op.take(fresh) == PasskeyOutcome::NotStored);
}

// The boot replay and the debug bridge post the same Configure event with
// nobody waiting. Their completion must not land in a screen's slot.
void test_a_ticketless_completion_touches_nothing()
{
    PasskeyOperation op;
    std::uint32_t ticket = 0;
    CHECK(op.reserve(ticket));
    op.complete(0, PasskeyOutcome::Armed);
    CHECK(op.take(ticket) == PasskeyOutcome::InFlight);
    CHECK(op.take(0) == PasskeyOutcome::Idle);

    // Nor may a worker park a non-terminal value there: Idle would strand the
    // screen on a slot that says nothing happened, InFlight never resolves.
    op.complete(ticket, PasskeyOutcome::Idle);
    op.complete(ticket, PasskeyOutcome::InFlight);
    CHECK(op.take(ticket) == PasskeyOutcome::InFlight);
}

}  // namespace

int main()
{
    test_request();
    test_restore();
    test_slot_is_not_answered_until_the_worker_answers();
    test_slot_carries_both_failures();
    test_one_operation_at_a_time();
    test_release_frees_the_slot();
    test_a_stale_completion_cannot_finish_the_next_session();
    test_a_ticketless_completion_touches_nothing();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("meshcore_passkey: OK");
    return 0;
}
