#include <cstdio>
#include <limits>
#include <cstring>

#include "attadipa/apps/provisioning.h"
#include "meshcore_bond_recovery.h"
#include "meshcore_node_forget.h"
#include "meshcore_passkey_outcome.h"

// The entry model with a board that records what it was asked. What is tested
// is the sequence -- which key does what to which field, and what reaches the
// board -- not any pixel.
//
// The passkey half of that board is not a fake. `FakeBoard` reserves, reads
// back and completes through `firmware/main/meshcore_passkey_outcome.h`, the
// file `meshcore_ble.cpp` and `waveshare_board.cpp` use, so what these tests
// exercise is the production handover and not a second implementation of it
// (AGENTS.md: an isolated decision helper does not prove the production caller
// works). What stays out of reach on a host is NimBLE and NVS themselves --
// `worker()` stands in for those two calls, and for nothing else.
//
// The node half is the same arrangement one file over: `forget_worker()` runs
// `forget_node()` from `firmware/main/meshcore_node_forget.h` -- the sequence
// `meshcore_ble.cpp` runs -- with this board as its `Ops`, over the real
// `BondRecovery` and the same ticketed slot. The fake is the store and the
// radio; the order of the clears, and what each ending is called, is shipped.

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

using attadipa::apps::EntryField;
using attadipa::apps::EntryKey;
using attadipa::apps::EntrySeed;
using attadipa::apps::EntryTask;
using attadipa::apps::EntryText;
using attadipa::apps::EntryVerdict;
using attadipa::apps::ProvisioningEntry;
using attadipa::core::ProvisionOutcome;
using attadipa::core::MeshForgetOutcome;
using attadipa::firmware::BondIdentity;
using attadipa::firmware::BondRecovery;
using attadipa::firmware::ForgetNodeOutcome;
using attadipa::firmware::PasskeyOperation;
using attadipa::firmware::PasskeyOutcome;
using attadipa::firmware::TicketedOperation;
using attadipa::l10n::Locale;

struct FakeBoard final : attadipa::core::Provisioner {
    ProvisionOutcome clock_answer = ProvisionOutcome::Accepted;
    // What the passkey is answered with before the radio is involved at all.
    // `Pending` is what the board does: reserve a slot, queue the request, and
    // let the worker finish it. `Rejected` and `Failed` refuse it outright.
    ProvisionOutcome passkey_answer = ProvisionOutcome::Pending;
    // The worker queue refusing the post: the slot is reserved and given back,
    // exactly as meshcore_ble.cpp does before it answers ESP_ERR_NO_MEM.
    bool queue_full = false;
    int clocks = 0, passkeys = 0, polls = 0;
    attadipa::core::WallClockEntry clock{};
    std::uint32_t passkey = 0;

    // The shipping slot, and the two tickets either end of it: `ticket_` is the
    // board's, exactly as `BoardProvisioner` holds one, and `queued` is what
    // the Configure event carried to the worker.
    PasskeyOperation op;
    std::uint32_t queued = 0;

    ProvisionOutcome set_wall_clock(
        const attadipa::core::WallClockEntry& entry) override
    {
        ++clocks;
        clock = entry;
        return clock_answer;
    }
    ProvisionOutcome set_mesh_passkey(std::uint32_t value) override
    {
        ++passkeys;
        passkey = value;
        if (passkey_answer != ProvisionOutcome::Pending) return passkey_answer;
        // As the board does: the ticket is written on success only, so a
        // busy refusal leaves the in-flight one for mesh_passkey_outcome().
        std::uint32_t reserved = 0;
        if (!op.reserve(reserved)) return ProvisionOutcome::Failed;
        if (queue_full) {
            op.release(reserved);
            return ProvisionOutcome::Failed;
        }
        ticket_ = reserved;
        queued = reserved;
        return ProvisionOutcome::Pending;
    }
    ProvisionOutcome mesh_passkey_outcome() override
    {
        ++polls;
        switch (op.take(ticket_)) {
        case PasskeyOutcome::InFlight:
            return ProvisionOutcome::Pending;
        case PasskeyOutcome::Armed:
            ticket_ = 0;
            return ProvisionOutcome::Accepted;
        default:
            ticket_ = 0;
            return ProvisionOutcome::Failed;
        }
    }

    // The mesh worker's three lines, run when a test says so: the stack took
    // the passkey and flash holds it, or one of them refused.
    void worker(PasskeyOutcome outcome)
    {
        if (outcome == PasskeyOutcome::Armed) reprovision_pending = false;
        op.complete(queued, outcome);
    }

    // --- The node, and what forgetting it touches --------------------------
    //
    // No pin by default, so every test above sees the four-field screen it
    // was written for. A pinned board shows the node field between the
    // offset and the passkey.
    bool pinned = false;        // the RAM copy, what `settle_node_pin` reads
    bool pin_on_flash = false;  // the NVS key
    bool store_refuses = false; // ble_store_util_delete_peer says no
    bool erase_refuses = false; // nvs_erase_key says no
    bool terminate_refuses = false;
    bool marker_refuses = false;
    bool marker_clear_refuses = false;
    bool reprovision_pending = false;
    bool delete_saw_marker = false;
    bool erase_saw_marker = false;
    bool armed = true;          // reconnect_allowed
    bool cooling_down = true;   // a refusal cooldown still running
    int forgets = 0, deletes = 0, terminates = 0, forget_polls = 0;
    BondRecovery recovery;      // the shipping record, not a stand-in
    TicketedOperation<ForgetNodeOutcome> forget_op;
    std::uint32_t forget_queued = 0;

    void stale_bond()
    {
        BondIdentity peer{};
        peer.address = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
        peer.type = 1;
        peer.valid = true;
        recovery.record(peer);
    }

    bool mesh_node(attadipa::core::MeshPeerId& out) override
    {
        if (!pinned) return false;
        out = attadipa::core::MeshPeerId{};
        out.public_key[0] = 0x5c;
        out.public_key[1] = 0x62;
        out.public_key[2] = 0xd9;
        out.public_key[3] = 0xbc;
        return true;
    }
    ProvisionOutcome forget_mesh_node() override
    {
        ++forgets;
        // The request gate, as meshcore_ble_forget_node() has it: a recorded
        // bond or a pin, or ESP_ERR_INVALID_STATE -> Rejected.
        if (!recovery.recovery_required() && !pinned) {
            return ProvisionOutcome::Rejected;
        }
        std::uint32_t reserved = 0;
        if (!forget_op.reserve(reserved)) return ProvisionOutcome::Failed;
        if (queue_full) {
            forget_op.release(reserved);
            return ProvisionOutcome::Failed;
        }
        forget_ticket_ = reserved;
        forget_queued = reserved;
        return ProvisionOutcome::Pending;
    }
    MeshForgetOutcome mesh_forget_outcome() override
    {
        ++forget_polls;
        const ForgetNodeOutcome outcome = forget_op.take(forget_ticket_);
        if (outcome != ForgetNodeOutcome::InFlight) forget_ticket_ = 0;
        switch (outcome) {
        case ForgetNodeOutcome::InFlight:   return MeshForgetOutcome::Pending;
        case ForgetNodeOutcome::Forgotten:  return MeshForgetOutcome::Forgotten;
        case ForgetNodeOutcome::Unpinned:   return MeshForgetOutcome::Unpinned;
        case ForgetNodeOutcome::PinOnFlash: return MeshForgetOutcome::PinOnFlash;
        case ForgetNodeOutcome::Nothing:    return MeshForgetOutcome::Nothing;
        case ForgetNodeOutcome::ReplayInhibited:
            return MeshForgetOutcome::ReplayInhibited;
        default:                            return MeshForgetOutcome::BondKept;
        }
    }

    // `Ops` for forget_node(): the worker's ordered changes to the board.
    void disarm() { armed = false; }
    bool terminate()
    {
        ++terminates;
        return !terminate_refuses;
    }
    bool mark_reprovision()
    {
        if (marker_refuses) return false;
        reprovision_pending = true;
        return true;
    }
    bool cancel_reprovision()
    {
        if (marker_clear_refuses) return false;
        reprovision_pending = false;
        return true;
    }
    bool take_forget(BondIdentity& out) { return recovery.take_forget(out); }
    bool delete_bond(const BondIdentity&)
    {
        delete_saw_marker = reprovision_pending;
        ++deletes;
        return !store_refuses;
    }
    void record(const BondIdentity& peer) { recovery.record(peer); }
    bool erase_pin()
    {
        erase_saw_marker = reprovision_pending;
        if (erase_refuses) return false;
        pin_on_flash = false;
        return true;
    }
    bool unpin()
    {
        const bool was = pinned;
        pinned = false;
        return was;
    }
    void clear_refusal() { cooling_down = false; }

    // The worker's turn: the shipping sequence over this board.
    void forget_worker()
    {
        forget_op.complete(forget_queued, attadipa::firmware::forget_node(*this));
    }

private:
    std::uint32_t ticket_ = 0;
    std::uint32_t forget_ticket_ = 0;
};

void press_n(ProvisioningEntry& entry, EntryKey key, unsigned times)
{
    for (unsigned i = 0; i < times; ++i) { entry.press(key); }
}

bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

bool value_is(const ProvisioningEntry& entry, const char* expected)
{
    return eq(entry.text(Locale::En).value, expected);
}

bool verdict_is(const ProvisioningEntry& entry, const char* expected)
{
    return eq(entry.text(Locale::En).verdict, expected);
}

// The clock task's six steppers, walked to a given local instant from the
// default the constructor leaves. Every move is a real key press, so what this
// reaches is what a finger reaches.
// Five Nexts and nothing else: from Day to Offset with the draft untouched.
// The one to use on a seeded entry, where `step_to` below would name values
// it does not set.
void walk_to_offset(ProvisioningEntry& entry)
{
    for (unsigned i = 0; i < 5; ++i) {
        entry.press(EntryKey::Next);
    }
}

// Steps *from wherever the draft already is*, so the values name the result
// only on a fresh unseeded entry -- which starts on 2026-01-01 00:00 UTC+00:00
// and is what every caller of this one has.
void step_to(ProvisioningEntry& entry, unsigned day, unsigned month,
             unsigned year_from_2026, unsigned hour, unsigned minute,
             unsigned offset_quarters)
{
    press_n(entry, EntryKey::Plus, day - 1);
    entry.press(EntryKey::Next);
    press_n(entry, EntryKey::Plus, month - 1);
    entry.press(EntryKey::Next);
    press_n(entry, EntryKey::Plus, year_from_2026);
    entry.press(EntryKey::Next);
    press_n(entry, EntryKey::Plus, hour);
    entry.press(EntryKey::Next);
    press_n(entry, EntryKey::Plus, minute);
    entry.press(EntryKey::Next);
    press_n(entry, EntryKey::Plus, offset_quarters);
}

// The passkey's six digits, each stepped up from zero and left on the last.
void step_passkey(ProvisioningEntry& entry, const char* digits)
{
    for (unsigned i = 0; i < 6; ++i) {
        press_n(entry, EntryKey::Plus,
                static_cast<unsigned>(digits[i] - '0'));
        if (i + 1 < 6) { entry.press(EntryKey::Next); }
    }
}

// A seed the constructor should accept, built from a local instant so the
// test says what it means rather than a Unix number.
EntrySeed seed_at(std::int64_t year, unsigned month, unsigned day,
                  unsigned hour, unsigned minute, std::int16_t offset)
{
    attadipa::core::CivilTime civil;
    civil.year = year; civil.month = month; civil.day = day;
    civil.hour = hour; civil.minute = minute;
    attadipa::core::WallTime local{};
    EntrySeed seed;
    if (!attadipa::core::wall_time_from_civil(civil, local)) { return seed; }
    seed.valid = true;
    seed.utc.unix_seconds = local.unix_seconds -
                            static_cast<std::int64_t>(offset) * 60;
    seed.offset_minutes = offset;
    return seed;
}

// --- the clock ------------------------------------------------------------

// The whole clock task, and the one thing it is for: what the review showed is
// what the board was given. The draft is local and the board takes UTC, so the
// two lines on the review are not the same instant written twice -- getting
// the sign of that subtraction backwards is a watch that is five hours wrong
// and says nothing about it.
void test_the_clock_task_saves_the_instant_the_review_showed()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::LocalTime);
    CHECK(entry.task() == EntryTask::LocalTime);
    CHECK(entry.field() == EntryField::Day);
    CHECK(!entry.text(Locale::En).seeded);
    CHECK(entry.text(Locale::En).step == 1 && entry.text(Locale::En).steps == 6);

    // 31 January 2026, 21:00 local, UTC+05:15.
    step_to(entry, 31, 1, 0, 21, 0, 21);
    CHECK(entry.field() == EntryField::Offset);
    CHECK(value_is(entry, "UTC+05:15"));
    CHECK(entry.text(Locale::En).step == 6);

    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::TimeReview);
    CHECK(entry.text(Locale::En).steps == 0);
    CHECK(eq(entry.text(Locale::En).draft, "2026-01-31 \xC2\xB7 21:00 \xC2\xB7 UTC+05:15"));
    CHECK(eq(entry.text(Locale::En).utc, "2026-01-31 15:45Z"));
    CHECK(eq(entry.text(Locale::En).next, "Save"));

    CHECK(board.clocks == 0);
    entry.press(EntryKey::Next);
    CHECK(board.clocks == 1);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::TimeSaved);
    CHECK(board.clock.timezone_offset_minutes == 315);
    // 2026-01-31T15:45:00Z.
    CHECK(board.clock.utc_seconds == 1769874300);
}

// The receipt is a screen, not a door closing behind you. Until #469 the
// terminal field *was* `finished()`, which is the auto-dismiss #416 removed
// from the passkey arriving one screen later.
void test_the_receipt_is_not_the_exit()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::LocalTime);
    walk_to_offset(entry);
    entry.press(EntryKey::Next);
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::TimeSaved);
    CHECK(!entry.finished());
    CHECK(!entry.text(Locale::En).finished);
    CHECK(verdict_is(entry, "the clock is set"));
    CHECK(eq(entry.text(Locale::En).next, "Done"));
    // No Back from a receipt that succeeded: there is no draft to go back to
    // that the board does not already hold.
    CHECK(eq(entry.text(Locale::En).previous, ""));

    entry.press(EntryKey::Next);
    CHECK(entry.finished() && entry.field() == EntryField::Exit);
    CHECK(entry.text(Locale::En).finished);
    // Keys past the exit do nothing.
    entry.press(EntryKey::Next);
    entry.press(EntryKey::Plus);
    CHECK(entry.finished() && board.clocks == 1);
}

// A board that refuses the value has not changed anything, and the draft it
// refused is still worth keeping: retyping six fields to fix one is the trap
// #406 called a trap.
void test_a_refused_clock_keeps_the_draft_and_offers_both_ways_on()
{
    FakeBoard board;
    board.clock_answer = ProvisionOutcome::Rejected;
    ProvisioningEntry entry(board, EntryTask::LocalTime);
    step_to(entry, 9, 2, 1, 7, 30, 4);
    entry.press(EntryKey::Next);
    const attadipa::core::CivilTime before = entry.draft();
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::TimeRefused);
    CHECK(verdict_is(entry, "not accepted \xE2\x80\x94 nothing changed"));
    CHECK(eq(entry.text(Locale::En).next, "Retry"));
    CHECK(eq(entry.text(Locale::En).previous, "Back"));

    // Retry asks again with the same draft, and is answered the same way.
    entry.press(EntryKey::Next);
    CHECK(board.clocks == 2);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::TimeRefused);

    // Back returns to the review with the draft untouched.
    entry.press(EntryKey::Previous);
    CHECK(entry.field() == EntryField::TimeReview);
    const attadipa::core::CivilTime after = entry.draft();
    CHECK(before.year == after.year && before.month == after.month &&
          before.day == after.day && before.hour == after.hour &&
          before.minute == after.minute);
    CHECK(entry.draft_offset_minutes() == 60);

    board.clock_answer = ProvisionOutcome::Accepted;
    entry.press(EntryKey::Next);
    CHECK(entry.verdict() == EntryVerdict::TimeSaved && board.clocks == 3);
}

// `Rejected` and `Failed` are two different watches and get two different
// sentences. Collapsing them into "did not work" tells the second holder
// their clock still holds the old time when it may not.
void test_a_failed_clock_does_not_claim_nothing_changed()
{
    for (const ProvisionOutcome answer :
         {ProvisionOutcome::Failed, ProvisionOutcome::Pending}) {
        FakeBoard board;
        board.clock_answer = answer;
        ProvisioningEntry entry(board, EntryTask::LocalTime);
        walk_to_offset(entry);
        entry.press(EntryKey::Next);
        entry.press(EntryKey::Next);
        CHECK(entry.verdict() == EntryVerdict::TimeUncertain);
        CHECK(verdict_is(entry, "may be part-written; check the clock"));
        CHECK(!verdict_is(entry, "not accepted \xE2\x80\x94 nothing changed"));
        // Uncertain is not a wait: `set_wall_clock` is terminal by contract,
        // so there is no second answer coming and nothing to poll for.
        CHECK(!entry.waiting());
        CHECK(!entry.poll());
        CHECK(eq(entry.text(Locale::En).next, "Retry"));
    }
}

// A stepper cannot produce 2026-13-32, and the reason it cannot is that the
// day follows the month and the year rather than standing still while they
// move underneath it.
void test_the_stepper_cannot_build_a_date_that_is_not_one()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::LocalTime);
    press_n(entry, EntryKey::Plus, 30);  // 31 January
    CHECK(value_is(entry, "31"));
    entry.press(EntryKey::Plus);         // and it wraps rather than growing
    CHECK(value_is(entry, "1"));
    press_n(entry, EntryKey::Plus, 30);
    entry.press(EntryKey::Next);
    entry.press(EntryKey::Plus);         // February
    CHECK(value_is(entry, "02"));
    entry.press(EntryKey::Previous);
    CHECK(value_is(entry, "28"));        // 2026 is not a leap year

    entry.press(EntryKey::Next);
    entry.press(EntryKey::Next);
    press_n(entry, EntryKey::Plus, 2);   // 2028
    CHECK(value_is(entry, "2028"));
    entry.press(EntryKey::Previous);
    entry.press(EntryKey::Previous);
    CHECK(value_is(entry, "28"));        // the clamp does not step back up

    // 29 February 2028 is real, and stepping the year off it is not.
    entry.press(EntryKey::Plus);
    CHECK(value_is(entry, "29"));
    entry.press(EntryKey::Next);
    entry.press(EntryKey::Next);
    entry.press(EntryKey::Plus);         // 2029
    entry.press(EntryKey::Previous);
    entry.press(EntryKey::Previous);
    CHECK(value_is(entry, "28"));
}

// Every zone there is, and no zone there is not. Clamped rather than wrapped,
// because a stepper that rolls from +14:00 to -12:00 in one key sets the wrong
// day on a slip and says nothing about it.
void test_the_offset_steps_by_quarter_hours_and_stops_at_the_ends()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::LocalTime);
    walk_to_offset(entry);
    CHECK(value_is(entry, "UTC+00:00"));
    entry.press(EntryKey::Plus);
    CHECK(value_is(entry, "UTC+00:15"));
    entry.press(EntryKey::Minus);
    entry.press(EntryKey::Minus);
    CHECK(value_is(entry, "UTC-00:15"));

    press_n(entry, EntryKey::Plus, 200);
    CHECK(value_is(entry, "UTC+14:00"));
    entry.press(EntryKey::Plus);
    CHECK(value_is(entry, "UTC+14:00"));
    press_n(entry, EntryKey::Minus, 200);
    CHECK(value_is(entry, "UTC-12:00"));
    entry.press(EntryKey::Minus);
    CHECK(value_is(entry, "UTC-12:00"));
}

// A seeded offset that is not on the grid is the case the grid alone gets
// wrong: without the first-step rule, +05:53 steps to +06:00 and then back to
// +05:53, which is a stepper that cannot leave where it started. `%` truncates
// toward zero in C++, so the two directions are not mirror images and both are
// checked.
void test_the_first_step_off_the_grid_lands_on_it()
{
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 6, 1, 12, 0, 353));
        CHECK(entry.text(Locale::En).seeded);
        walk_to_offset(entry);
        CHECK(value_is(entry, "UTC+05:53"));
        entry.press(EntryKey::Plus);
        CHECK(value_is(entry, "UTC+06:00"));
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 6, 1, 12, 0, 353));
        walk_to_offset(entry);
        entry.press(EntryKey::Minus);
        CHECK(value_is(entry, "UTC+05:45"));
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 6, 1, 12, 0, -353));
        walk_to_offset(entry);
        CHECK(value_is(entry, "UTC-05:53"));
        entry.press(EntryKey::Plus);
        CHECK(value_is(entry, "UTC-05:45"));
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 6, 1, 12, 0, -353));
        walk_to_offset(entry);
        entry.press(EntryKey::Minus);
        CHECK(value_is(entry, "UTC-06:00"));
    }
}

// `valid` is the board's claim and the constructor is the check. An offset
// outside the zones there are, or a local instant outside the years this watch
// sets, is dropped whole -- not shown as a draft with a false `seeded` beside
// it, which is a screen asserting that the clock told it something.
void test_a_seed_that_does_not_survive_its_offset_is_dropped()
{
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 9, 8, 14, 30, 315));
        CHECK(entry.text(Locale::En).seeded);
        CHECK(eq(entry.text(Locale::En).draft,
                 "2026-09-08 \xC2\xB7 14:30 \xC2\xB7 UTC+05:15"));
        CHECK(entry.draft_offset_minutes() == 315);
    }
    {
        // An offset no zone has. The instant behind it may be perfectly good
        // and it is still not a draft this screen can show.
        EntrySeed seed = seed_at(2026, 9, 8, 14, 30, 315);
        seed.offset_minutes = 900;
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime, seed);
        CHECK(!entry.text(Locale::En).seeded);
        CHECK(entry.draft().year == 2026 && entry.draft().month == 1 &&
              entry.draft().day == 1);
        CHECK(entry.draft_offset_minutes() == 0);
    }
    {
        // A local year this watch does not set. The offset is what carries it
        // over the edge, which is why the check has to come after applying it.
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2100, 1, 1, 0, 0, 0));
        CHECK(!entry.text(Locale::En).seeded);
        CHECK(entry.draft().year == 2026);
    }
    {
        // 2099-12-31 23:30 UTC is inside the range; at +01:00 the local
        // instant it seeds is 2100-01-01 00:30, and it is not.
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2100, 1, 1, 0, 30, 60));
        CHECK(!entry.text(Locale::En).seeded);
        CHECK(entry.draft().year == 2026);
    }
    {
        // The two ends of the type. `valid` is the board's claim, so a seed
        // this far out is a thing the constructor must survive, not a thing it
        // may assume away: applying the offset first would be signed overflow
        // -- undefined, and undefined before the civil-range check that would
        // have rejected the value could run. Both directions, because the
        // guard is one-sided in each.
        for (const std::int16_t offset : {std::int16_t{840}, std::int16_t{-840}}) {
            for (const std::int64_t instant :
                 {std::numeric_limits<std::int64_t>::max(),
                  std::numeric_limits<std::int64_t>::min()}) {
                EntrySeed seed;
                seed.valid = true;
                seed.utc.unix_seconds = instant;
                seed.offset_minutes = offset;
                FakeBoard board;
                ProvisioningEntry entry(board, EntryTask::LocalTime, seed);
                CHECK(!entry.text(Locale::En).seeded);
                CHECK(entry.draft().year == 2026 && entry.draft().month == 1 &&
                      entry.draft().day == 1);
                CHECK(entry.draft_offset_minutes() == 0);
            }
        }
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime, EntrySeed{});
        CHECK(!entry.text(Locale::En).seeded);
    }
}

// --- the walk a board opens -----------------------------------------------

// `EntryTask::All` is what `waveshare_board.cpp` emplaces, and it is the only
// thing on a product image that can reach `set_mesh_passkey` or
// `forget_mesh_node`: nothing there chooses between the two narrow tasks. Wire
// the board to `LocalTime` alone and a watch off the shelf can never be told
// its node, so this test is the one that says the firmware is still reachable.
void test_the_board_walk_reaches_the_node_and_the_passkey()
{
    FakeBoard board;
    board.pinned = true;
    ProvisioningEntry entry(board, EntryTask::All);
    CHECK(entry.field() == EntryField::Day);

    // The clock half, unchanged: six steps, a review, one write.
    for (int i = 0; i < 6; ++i) { entry.press(EntryKey::Next); }
    CHECK(entry.field() == EntryField::TimeReview);
    entry.press(EntryKey::Next);
    CHECK(board.clocks == 1);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::TimeSaved);

    // ... and the key on that receipt goes on rather than out, because there
    // is more of this walk left. `Done` would be a lie about the screen.
    CHECK(eq(entry.text(Locale::En).next, "Next"));
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Node);
    CHECK(!entry.finished());
    // The node is read on the way in, not at construction: the clock half has
    // no business reading the mesh. A screen that offered Forget over a blank
    // node is what this check is here for.
    CHECK(eq(entry.text(Locale::En).node, "5c62d9bc"));
    CHECK(eq(entry.text(Locale::En).forget, "Forget"));

    // The node half, reached with no chooser and no simulator flag.
    entry.press(EntryKey::Forget);
    CHECK(entry.field() == EntryField::ForgetConfirm);
    entry.press(EntryKey::Minus);
    board.forget_worker();
    CHECK(entry.poll());
    CHECK(entry.verdict() == EntryVerdict::NodeForgotten);
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Passkey);
    step_passkey(entry, "135790");
    entry.press(EntryKey::Next);
    CHECK(board.passkeys == 1 && board.passkey == 135790);

    // Both halves ran once each, and the clock was not written a second time.
    CHECK(board.clocks == 1 && board.forgets == 1);
}

// A watch pinned to no node has nothing to show it in, so the walk goes where
// the node task itself goes from there: the passkey, and not a blank field
// with a Forget key over it.
void test_the_board_walk_skips_a_node_that_is_not_there()
{
    FakeBoard board;
    board.pinned = false;
    ProvisioningEntry entry(board, EntryTask::All);
    for (int i = 0; i < 7; ++i) { entry.press(EntryKey::Next); }
    CHECK(entry.field() == EntryField::Receipt);
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Passkey);
    CHECK(eq(entry.text(Locale::En).node, ""));
}

// The clock's own draft is the clock's. Carried past the receipt it would sit
// under the node and the passkey asserting a date nobody is editing there.
void test_the_clock_draft_stops_where_the_clock_does()
{
    FakeBoard board;
    board.pinned = true;
    ProvisioningEntry entry(board, EntryTask::All);
    for (int i = 0; i < 7; ++i) { entry.press(EntryKey::Next); }
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(!eq(entry.text(Locale::En).draft, ""));
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Node);
    CHECK(eq(entry.text(Locale::En).draft, ""));
    CHECK(eq(entry.text(Locale::En).utc, ""));
}

// A watch whose board will not take the time still has a radio, and `All` is
// the only walk on a product image that reaches it. The clock's receipt says
// Retry on `Next` -- and a board that refuses one write refuses the retry too,
// so if Retry were the only key there, one bad RTC would take
// `set_mesh_passkey` and `forget_mesh_node` out of reach for good and the node
// would never start scanning.
void test_a_refused_clock_does_not_end_the_board_walk()
{
    for (const ProvisionOutcome answer :
         {ProvisionOutcome::Rejected, ProvisionOutcome::Failed}) {
        FakeBoard board;
        board.pinned       = true;
        board.clock_answer = answer;
        ProvisioningEntry entry(board, EntryTask::All);
        for (int i = 0; i < 7; ++i) { entry.press(EntryKey::Next); }
        CHECK(entry.field() == EntryField::Receipt);
        CHECK(board.clocks == 1);
        CHECK(entry.verdict() != EntryVerdict::TimeSaved);

        // Retry is what `Next` is, and it does retry: the way on cannot be
        // that key.
        CHECK(eq(entry.text(Locale::En).next, "Retry"));
        entry.press(EntryKey::Next);
        CHECK(board.clocks == 2);
        CHECK(entry.field() == EntryField::Receipt);

        // The way on is `Plus`, and it reaches the node with the clock's
        // verdict cleared -- the receipt has been read by then.
        CHECK(eq(entry.text(Locale::En).plus, "Next"));
        entry.press(EntryKey::Plus);
        CHECK(entry.field() == EntryField::Node);
        CHECK(entry.verdict() == EntryVerdict::None);
        CHECK(eq(entry.text(Locale::En).node, "5c62d9bc"));

        // And the passkey past it, which is the capability at stake.
        entry.press(EntryKey::Next);
        CHECK(entry.field() == EntryField::Passkey);
        step_passkey(entry, "424242");
        entry.press(EntryKey::Next);
        CHECK(board.passkeys == 1 && board.passkey == 424242);

        // The clock was written only by the two presses that asked for it.
        CHECK(board.clocks == 2);
    }
}

// `Plus` is that key on one receipt and no other. A receipt the passkey
// produced is the end of the walk, and a clock receipt in the narrow clock
// task has no node behind it at all -- drawing a key there would promise a
// screen that does not exist.
void test_the_receipt_way_on_is_drawn_on_no_other_receipt()
{
    {
        FakeBoard board;
        board.clock_answer = ProvisionOutcome::Rejected;
        ProvisioningEntry entry(board, EntryTask::LocalTime);
        for (int i = 0; i < 7; ++i) { entry.press(EntryKey::Next); }
        CHECK(entry.field() == EntryField::Receipt);
        CHECK(eq(entry.text(Locale::En).plus, ""));
        entry.press(EntryKey::Plus);
        CHECK(entry.field() == EntryField::Receipt);
    }
    {
        FakeBoard board;
        board.pinned         = true;
        board.passkey_answer = ProvisionOutcome::Rejected;
        ProvisioningEntry entry(board, EntryTask::All);
        for (int i = 0; i < 8; ++i) { entry.press(EntryKey::Next); }
        CHECK(entry.field() == EntryField::Node);
        entry.press(EntryKey::Next);
        step_passkey(entry, "135790");
        entry.press(EntryKey::Next);
        CHECK(entry.field() == EntryField::Receipt);
        CHECK(entry.verdict() == EntryVerdict::PasskeyRefused);
        CHECK(eq(entry.text(Locale::En).next, "Retry"));
        CHECK(eq(entry.text(Locale::En).plus, ""));
        entry.press(EntryKey::Plus);
        CHECK(entry.field() == EntryField::Receipt);
    }
}

// --- the node -------------------------------------------------------------

// THE INVARIANT THE TASK EXISTS FOR, and the only way it can be shown: both
// tasks are this one class over one `core::Provisioner`, so nothing but the
// journey proves that the node one never writes the clock. Every branch the
// node task has is walked here -- keep, back, forget, a refused passkey, a
// retry, the exit -- and `set_wall_clock` is never called once.
void test_the_node_task_never_writes_the_clock()
{
    FakeBoard board;
    board.pinned = true;
    board.pin_on_flash = true;
    board.stale_bond();
    ProvisioningEntry entry(board, EntryTask::NodePasskey);
    CHECK(entry.field() == EntryField::Node);
    CHECK(eq(entry.text(Locale::En).node, "5c62d9bc"));

    // Keep, and Back: neither asks the board anything.
    entry.press(EntryKey::Forget);
    CHECK(entry.field() == EntryField::ForgetConfirm);
    entry.press(EntryKey::Previous);
    CHECK(entry.field() == EntryField::Node && board.forgets == 0);
    entry.press(EntryKey::Forget);
    entry.press(EntryKey::Leave);
    CHECK(entry.field() == EntryField::Node && board.forgets == 0);
    CHECK(!entry.finished());

    // Two keys do not confirm and neither is drawn. `Next` is the one between
    // the two that undo. `Forget` is the one that asked -- and it is the one
    // that matters, because it is where the finger already is: a second tap of
    // it is a mis-tap, not an answer, and it must not cost the node.
    entry.press(EntryKey::Forget);
    CHECK(entry.field() == EntryField::ForgetConfirm);
    CHECK(eq(entry.text(Locale::En).next, ""));
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::ForgetConfirm && board.forgets == 0);
    CHECK(eq(entry.text(Locale::En).forget, ""));
    entry.press(EntryKey::Forget);
    CHECK(entry.field() == EntryField::ForgetConfirm && board.forgets == 0);

    // And then the forget, which finishes on the radio's task. The key that
    // does it is `Minus`, which the node screen does not draw, so no press
    // that could have opened this question lands on it.
    CHECK(eq(entry.text(Locale::En).minus, "Forget"));
    entry.press(EntryKey::Minus);
    CHECK(board.forgets == 1);
    CHECK(entry.verdict() == EntryVerdict::ForgetPending);
    CHECK(entry.waiting() && entry.field() == EntryField::ForgetConfirm);
    CHECK(!entry.poll());
    board.forget_worker();
    CHECK(entry.poll());
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::NodeForgotten);
    CHECK(entry.forget_outcome() == MeshForgetOutcome::Forgotten);

    // The passkey a forgotten node now needs is what the receipt leads to.
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Passkey);
    CHECK(entry.text(Locale::En).step == 1 && entry.text(Locale::En).steps == 6);

    // Refused outright, and retried.
    board.passkey_answer = ProvisionOutcome::Rejected;
    step_passkey(entry, "246813");
    CHECK(value_is(entry, "246813"));
    CHECK(entry.text(Locale::En).step == 6);
    CHECK(eq(entry.text(Locale::En).next, "Save"));
    entry.press(EntryKey::Next);
    CHECK(board.passkeys == 1 && board.passkey == 246813);
    CHECK(entry.verdict() == EntryVerdict::PasskeyRefused);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(eq(entry.text(Locale::En).next, "Retry"));

    board.passkey_answer = ProvisionOutcome::Pending;
    entry.press(EntryKey::Next);
    CHECK(board.passkeys == 2);
    CHECK(entry.verdict() == EntryVerdict::PasskeyPending && entry.waiting());
    board.worker(PasskeyOutcome::Armed);
    CHECK(entry.poll());
    CHECK(entry.verdict() == EntryVerdict::PasskeyStored);
    CHECK(verdict_is(entry, "the watch is set up"));
    CHECK(!entry.finished());
    entry.press(EntryKey::Next);
    CHECK(entry.finished());

    CHECK(board.clocks == 0);
}

// The key the acting fill lands on, for a face that must not name it itself.
static const char *drawn(const EntryText &text, EntryKey key)
{
    switch (key) {
    case EntryKey::Minus:    return text.minus;
    case EntryKey::Plus:     return text.plus;
    case EntryKey::Forget:   return text.forget;
    case EntryKey::Previous: return text.previous;
    case EntryKey::Next:     return text.next;
    case EntryKey::Leave:    return text.leave;
    }
    return "";
}

// `EntryText::acting` is the only thing telling a face which key does the
// thing, and a face cannot check it: it draws what it is given. So the model
// owes it two properties, and both are silent when broken -- the screen keeps
// working and one key is filled wrong, which is exactly the class of defect a
// screenshot is needed to see. Delete the assignment on the confirmation and
// this is the check that goes red.
//
//   1. The acting key is drawn. A fill on a hidden key marks nothing.
//   2. On the confirmation it is a key the frame that asked does not draw --
//      the property that makes a second tap of the asking slot harmless, and
//      the one the fix for round 2 turns on.
void test_the_acting_key_is_drawn_and_never_the_one_that_asked()
{
    FakeBoard board;
    board.pinned = true;
    ProvisioningEntry entry(board, EntryTask::NodePasskey);

    CHECK(entry.field() == EntryField::Node);
    const EntryText node = entry.text(Locale::En);
    CHECK(node.acting == EntryKey::Next);
    CHECK(!eq(drawn(node, node.acting), ""));

    entry.press(EntryKey::Forget);
    CHECK(entry.field() == EntryField::ForgetConfirm);
    const EntryText ask = entry.text(Locale::En);
    CHECK(!eq(drawn(ask, ask.acting), ""));
    // Not stated as "is Minus": what matters is that the node screen has no
    // key there, so any future move that keeps that true keeps this green.
    CHECK(eq(drawn(node, ask.acting), ""));

    // The receipt of the forget, and the passkey behind it: acting is drawn on
    // every frame, not only the two the confirmation is between.
    entry.press(EntryKey::Minus);
    board.forget_worker();
    CHECK(entry.poll() && entry.field() == EntryField::Receipt);
    const EntryText receipt = entry.text(Locale::En);
    CHECK(!eq(drawn(receipt, receipt.acting), ""));
    entry.press(EntryKey::Next);
    CHECK(entry.field() == EntryField::Passkey);
    const EntryText passkey = entry.text(Locale::En);
    CHECK(!eq(drawn(passkey, passkey.acting), ""));
}

// A watch pinned to no node has nothing to keep or forget, and no field to
// show it in.
void test_an_unpinned_watch_starts_at_the_passkey()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::NodePasskey);
    CHECK(entry.field() == EntryField::Passkey);
    CHECK(eq(entry.text(Locale::En).node, ""));
    // Nothing behind the first digit to go back to.
    CHECK(eq(entry.text(Locale::En).previous, ""));
    entry.press(EntryKey::Previous);
    CHECK(entry.field() == EntryField::Passkey);
}

// One value names what actually landed, and each of the six needs its own
// sentence. A partial forget drawn as a complete one is #378 with more state:
// the pin comes back at the next restart and the screen said it was gone.
void test_every_forget_ending_gets_its_own_sentence()
{
    struct Case {
        attadipa::firmware::ForgetNodeOutcome worker;
        MeshForgetOutcome outcome;
        EntryVerdict verdict;
        const char* line;
    };
    const Case cases[] = {
        {attadipa::firmware::ForgetNodeOutcome::Forgotten,
         MeshForgetOutcome::Forgotten, EntryVerdict::NodeForgotten,
         "forgotten; set its new passkey"},
        // The bond was kept: only the pin went. Same verdict as a complete
        // forget -- a passkey is still the next thing to set -- and a
        // different sentence, because the pairing is still there.
        {attadipa::firmware::ForgetNodeOutcome::Unpinned,
         MeshForgetOutcome::Unpinned, EntryVerdict::NodeForgotten,
         "node dropped; the pairing stayed, set its new passkey"},
        {attadipa::firmware::ForgetNodeOutcome::PinOnFlash,
         MeshForgetOutcome::PinOnFlash, EntryVerdict::NodePartlyForgotten,
         "forgot till reboot; a restart brings it back"},
        {attadipa::firmware::ForgetNodeOutcome::Nothing,
         MeshForgetOutcome::Nothing, EntryVerdict::NodeNothingToForget,
         "nothing to forget"},
        {attadipa::firmware::ForgetNodeOutcome::BondKept,
         MeshForgetOutcome::BondKept, EntryVerdict::ForgetKept,
         "not forgotten; retry"},
        {attadipa::firmware::ForgetNodeOutcome::ReplayInhibited,
         MeshForgetOutcome::ReplayInhibited, EntryVerdict::ForgetKept,
         "not forgotten; reboot scan is blocked"},
    };
    for (const Case& one : cases) {
        FakeBoard board;
        board.pinned = true;
        ProvisioningEntry entry(board, EntryTask::NodePasskey);
        entry.press(EntryKey::Forget);
        entry.press(EntryKey::Minus);
        CHECK(entry.waiting());
        board.forget_op.complete(board.forget_queued, one.worker);
        CHECK(entry.poll());
        CHECK(entry.forget_outcome() == one.outcome);
        CHECK(entry.verdict() == one.verdict);
        CHECK(verdict_is(entry, one.line));
        // A kept node is still this watch's, and the retry is real; anything
        // else has nothing left to keep, and leads on to the passkey.
        if (one.verdict == EntryVerdict::ForgetKept) {
            CHECK(eq(entry.text(Locale::En).next, "Retry"));
            entry.press(EntryKey::Next);
            CHECK(board.forgets == 2);
        } else {
            CHECK(eq(entry.text(Locale::En).next, "Next"));
            entry.press(EntryKey::Next);
            CHECK(entry.field() == EntryField::Passkey);
        }
    }
}

// A partial forget must not be styled as a complete one, and the face styles
// from the verdict. Three of the six endings would read as "forgotten" if the
// verdict were the outcome by another name.
void test_a_partial_forget_is_not_the_same_verdict_as_a_complete_one()
{
    FakeBoard board;
    board.pinned = true;
    ProvisioningEntry entry(board, EntryTask::NodePasskey);
    entry.press(EntryKey::Forget);
    entry.press(EntryKey::Minus);
    board.forget_op.complete(board.forget_queued,
                             attadipa::firmware::ForgetNodeOutcome::PinOnFlash);
    CHECK(entry.poll());
    CHECK(entry.verdict() != EntryVerdict::NodeForgotten);
    CHECK(!verdict_is(entry, "forgotten; set its new passkey"));
}

// The sentence a person reads BEFORE the destructive key, held against what
// the key does. It used to promise "its bond and its passkey, both" and the
// operation does neither unconditionally: the passkey is kept on purpose --
// `firmware/main/meshcore_node_forget.h:108` — "    // The passkey itself is deliberately retained. This marker is therefore" --
// and a bond goes only where `take_forget()` had one recorded stale (#504).
// Consent given to the old sentence was consent to a different operation.
//
// Two boards reach the identical question, differing only in whether a stale
// bond is on record -- which is the whole of the difference between the two
// endings, and is not knowable when the confirmation is drawn. So the same
// sentence has to be true of both, and the `Unpinned` half is the
// counterexample that keeps a universal erase claim from coming back.
void test_the_forget_confirmation_promises_only_what_forget_does()
{
    // The Russian is written out rather than escaped, the one place in this
    // file that is: it is the text under test, and a reader has to be able to
    // read it to see whether it is true.
    static const char* const kEn =
        "the node goes, only a stale pairing; the passkey stays";
    static const char* const kRu =
        "сбросятся узел и устаревшее сопряжение; код останется";

    const bool recorded[] = {false, true};
    const char* lines[2] = {};
    for (unsigned i = 0; i < 2; ++i) {
        const bool stale = recorded[i];
        FakeBoard board;
        board.pinned = true;
        board.pin_on_flash = true;
        if (stale) { board.stale_bond(); }
        ProvisioningEntry entry(board, EntryTask::NodePasskey);
        entry.press(EntryKey::Forget);
        CHECK(entry.field() == EntryField::ForgetConfirm);

        // One question, both locales, and nothing asked of the board yet.
        CHECK(eq(entry.text(Locale::En).instruction, kEn));
        CHECK(eq(entry.text(Locale::Ru).instruction, kRu));
        CHECK(board.forgets == 0 && board.deletes == 0);

        entry.press(EntryKey::Minus);
        board.forget_worker();
        CHECK(entry.poll() && entry.field() == EntryField::Receipt);
        lines[i] = entry.text(Locale::En).verdict;

        // What the sentence said would always happen, did.
        CHECK(!board.pinned && !board.pin_on_flash);
        // What it said was conditional, was: the bond store is touched only
        // for the board that had a stale record, and the other keeps the
        // pairing the confirmation never promised to take.
        CHECK(board.deletes == (stale ? 1 : 0));
        CHECK(entry.forget_outcome() ==
              (stale ? MeshForgetOutcome::Forgotten
                     : MeshForgetOutcome::Unpinned));
        // And the passkey is still on flash. `Ops` has no eraser for it at
        // all -- the marker is what stands in place of one, and it is left
        // standing so the old digits are not replayed at the next boot.
        CHECK(board.reprovision_pending);
        CHECK(board.passkeys == 0);
    }

    // The two endings stay two sentences. A confirmation that had named one
    // of them in advance would have been wrong on the other board.
    CHECK(eq(lines[0], "node dropped; the pairing stayed, set its new passkey"));
    CHECK(eq(lines[1], "forgotten; set its new passkey"));
    CHECK(!eq(lines[0], lines[1]));
}

// --- leaving --------------------------------------------------------------

// Leaving with the radio still holding the request is neither "set up" nor
// "nothing changed", and it is not a wait either: the board keeps that answer
// from whatever screen replaces this one, so nobody is ever going to hear it.
void test_leaving_a_wait_says_the_answer_is_lost()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::NodePasskey);
    step_passkey(entry, "123456");
    entry.press(EntryKey::Next);
    CHECK(entry.waiting() && entry.verdict() == EntryVerdict::PasskeyPending);
    CHECK(verdict_is(entry, "still setting up the node"));
    // Nothing but the way out means anything while the radio has it.
    entry.press(EntryKey::Plus);
    entry.press(EntryKey::Next);
    CHECK(board.passkeys == 1 && entry.waiting());
    CHECK(eq(entry.text(Locale::En).next, ""));

    entry.press(EntryKey::Leave);
    CHECK(entry.field() == EntryField::Receipt);
    CHECK(entry.verdict() == EntryVerdict::Abandoned);
    CHECK(verdict_is(entry,
                     "the node still had it; how that ended is unknown"));
    CHECK(!entry.waiting() && !entry.finished());
    // Nothing to go back to: the draft it would return to has already gone.
    CHECK(eq(entry.text(Locale::En).previous, ""));
    entry.press(EntryKey::Next);
    CHECK(entry.finished());

    // AND THE ANSWER IS NOT HANDED TO THE ENTRY THAT REPLACES IT. The worker
    // arms the abandoned request after the screen that asked for it has gone.
    // A second screen reserves the slot afresh, which discards that answer
    // where it stands: `take()` only ever answers the ticket that owns the
    // slot, and this one no longer does.
    board.worker(PasskeyOutcome::Armed);
    ProvisioningEntry second(board, EntryTask::NodePasskey);
    step_passkey(second, "654321");
    second.press(EntryKey::Next);
    CHECK(board.passkeys == 2 && board.passkey == 654321);
    CHECK(second.verdict() == EntryVerdict::PasskeyPending);
    CHECK(second.waiting());
    // Nothing to collect: the armed answer belonged to the request this
    // screen replaced, and no amount of polling will produce it.
    CHECK(!second.poll());
    CHECK(second.verdict() == EntryVerdict::PasskeyPending);

    // The answer this screen does get is its own. Had the abandoned `Armed`
    // reached it, this would say the watch is set up.
    board.worker(PasskeyOutcome::Refused);
    CHECK(second.poll());
    CHECK(second.verdict() == EntryVerdict::PasskeyUncertain);
    CHECK(second.verdict() != EntryVerdict::PasskeyStored);
    CHECK(verdict_is(second, "it may work until the next restart"));
    CHECK(!second.finished());
}

// The frame after the last press, which no test used to look at.
//
// `Exit` is not a screen; it is the absence of one, and the thing that takes
// the screen away is the caller, on its own tick -- a second on the board, two
// and a half in the simulator. Something repaints in that gap, and what it
// repaints is whatever `text()` says. It used to say nothing at all, so the
// gap was a bare panel on a product image. It says the frame the holder left,
// with the whole keypad gone: the words are still true and every key is dead.
void test_the_finished_frame_keeps_the_words_and_drops_the_keys()
{
    auto no_keys = [](const EntryText &t) {
        return eq(t.minus, "") && eq(t.plus, "") && eq(t.forget, "") &&
               eq(t.previous, "") && eq(t.next, "") && eq(t.leave, "");
    };

    // Left from the first step of the clock: its title, and no pad.
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime);
        const EntryText before = entry.text(Locale::En);
        CHECK(!eq(before.title, "") && !eq(before.leave, ""));
        entry.press(EntryKey::Leave);
        CHECK(entry.finished());
        const EntryText after = entry.text(Locale::En);
        CHECK(after.finished);
        CHECK(eq(after.title, before.title));
        CHECK(no_keys(after));
    }

    // Left from the receipt the walk ends on, which is the frame a holder
    // actually leaves from: the answer stays up until the caller tears it down.
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime);
        for (unsigned i = 0; i < 6; ++i) { entry.press(EntryKey::Next); }
        entry.press(EntryKey::Next);
        CHECK(entry.field() == EntryField::Receipt && board.clocks == 1);
        const EntryText receipt = entry.text(Locale::En);
        CHECK(!eq(receipt.verdict, ""));
        entry.press(EntryKey::Next);
        CHECK(entry.finished());
        const EntryText after = entry.text(Locale::En);
        CHECK(eq(after.title, receipt.title));
        CHECK(eq(after.verdict, receipt.verdict));
        CHECK(no_keys(after));
    }
}

// A screen that can only be left by getting something right is a trap, and a
// long press onto it is easy to make by accident (#406 round 1). Every field
// of all three tasks has a way out that asks nothing of the holder -- except
// the confirmation, where `Leave` is Back: it answers the destructive question
// the safe way rather than taking the screen away with it still open.
void test_leave_is_never_a_trap()
{
    const EntryKey walk_time[] = {EntryKey::Next, EntryKey::Next, EntryKey::Next,
                                  EntryKey::Next, EntryKey::Next, EntryKey::Next};
    for (unsigned stop = 0; stop <= 6; ++stop) {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime);
        for (unsigned i = 0; i < stop; ++i) { entry.press(walk_time[i]); }
        entry.press(EntryKey::Leave);
        CHECK(entry.finished());
        CHECK(board.clocks == 0);
    }
    {
        FakeBoard board;
        board.pinned = true;
        ProvisioningEntry entry(board, EntryTask::NodePasskey);
        entry.press(EntryKey::Leave);
        CHECK(entry.finished() && board.forgets == 0);
    }
    {
        // The confirmation is the one screen Leave does not leave -- and the
        // node it protects is still there afterwards.
        FakeBoard board;
        board.pinned = true;
        ProvisioningEntry entry(board, EntryTask::NodePasskey);
        entry.press(EntryKey::Forget);
        entry.press(EntryKey::Leave);
        CHECK(!entry.finished() && entry.field() == EntryField::Node);
        CHECK(board.forgets == 0);
        entry.press(EntryKey::Leave);
        CHECK(entry.finished());
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::NodePasskey);
        step_passkey(entry, "111111");
        entry.press(EntryKey::Leave);
        CHECK(entry.finished() && board.passkeys == 0);
    }
    {
        // `EntryTask::All` is the walk a product image builds, and until now
        // this test never took it -- round 5's review walked it by hand and
        // said so. What `Leave` may never do is write, so the counters are
        // read before the press and compared after rather than checked
        // against zero: by the node half this walk has legitimately saved a
        // clock, and a test that demanded zero could only cover the first
        // half of it. The one screen it does not leave is the confirmation,
        // which is the whole of the exception stated once.
        const EntryKey walk_all[] = {
            EntryKey::Next, EntryKey::Next, EntryKey::Next, EntryKey::Next,
            EntryKey::Next, EntryKey::Next, EntryKey::Next, EntryKey::Next,
            EntryKey::Forget};
        const unsigned stops = sizeof(walk_all) / sizeof(walk_all[0]);
        for (unsigned stop = 0; stop <= stops; ++stop) {
            FakeBoard board;
            board.pinned = true;
            ProvisioningEntry entry(board, EntryTask::All);
            for (unsigned i = 0; i < stop; ++i) { entry.press(walk_all[i]); }
            const bool confirm = entry.field() == EntryField::ForgetConfirm;
            const int clocks = board.clocks;
            const int forgets = board.forgets;
            const int passkeys = board.passkeys;
            entry.press(EntryKey::Leave);
            CHECK(entry.finished() != confirm);
            CHECK(board.clocks == clocks && board.forgets == forgets);
            CHECK(board.passkeys == passkeys);
        }
        // ... and the last stop above really is the confirmation, so the
        // `!= confirm` above was exercised in both directions.
        FakeBoard board;
        board.pinned = true;
        ProvisioningEntry entry(board, EntryTask::All);
        for (const EntryKey key : walk_all) { entry.press(key); }
        CHECK(entry.field() == EntryField::ForgetConfirm);
    }
}

// --- what the face is given -----------------------------------------------

// The exact bytes, in both locales, with an offset that is neither zero nor a
// whole hour and on both sides of Greenwich. `draft` is emptied rather than
// truncated when it does not fit, so a non-empty string here is also the
// check that it did.
void test_both_locales_print_the_whole_instant()
{
    {
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 1, 31, 21, 0, 315));
        const EntryText en = entry.text(Locale::En);
        const EntryText ru = entry.text(Locale::Ru);
        CHECK(eq(en.draft, "2026-01-31 \xC2\xB7 21:00 \xC2\xB7 UTC+05:15"));
        CHECK(eq(ru.draft, "31.01.2026 \xC2\xB7 21:00 \xC2\xB7 UTC+05:15"));
        CHECK(eq(en.utc, "2026-01-31 15:45Z"));
        CHECK(eq(ru.utc, "2026-01-31 15:45Z"));
        CHECK(std::strlen(en.draft) == 32 && std::strlen(ru.draft) == 32);
        CHECK(std::strlen(en.utc) == 17);
        CHECK(eq(en.title, "Day") && eq(ru.title, "\xD0\x94\xD0\xB5\xD0\xBD\xD1\x8C"));
    }
    {
        // West of Greenwich, and over the date line into the next day.
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2026, 1, 31, 21, 0, -315));
        const EntryText en = entry.text(Locale::En);
        CHECK(eq(en.draft, "2026-01-31 \xC2\xB7 21:00 \xC2\xB7 UTC-05:15"));
        CHECK(eq(en.utc, "2026-02-01 02:15Z"));
    }
    {
        // The widest the two lines get: four-digit year, +14:00, and the
        // longest month and day. Still inside `draft[40]`.
        FakeBoard board;
        ProvisioningEntry entry(board, EntryTask::LocalTime,
                                seed_at(2099, 12, 31, 23, 59, 840));
        const EntryText en = entry.text(Locale::En);
        CHECK(eq(en.draft, "2099-12-31 \xC2\xB7 23:59 \xC2\xB7 UTC+14:00"));
        CHECK(eq(en.utc, "2099-12-31 09:59Z"));
    }
}

// `step` is which of `steps` is being set, and on the passkey that is the
// digit under the cursor rather than the field. A face drawing six boxes has
// no other way to know which one to light.
void test_the_passkey_step_names_the_digit_under_the_cursor()
{
    FakeBoard board;
    ProvisioningEntry entry(board, EntryTask::NodePasskey);
    for (unsigned digit = 1; digit <= 6; ++digit) {
        CHECK(entry.text(Locale::En).step == digit);
        CHECK(entry.text(Locale::En).steps == 6);
        CHECK(eq(entry.text(Locale::En).next, digit < 6 ? "Next" : "Save"));
        if (digit < 6) { entry.press(EntryKey::Next); }
    }
    entry.press(EntryKey::Previous);
    CHECK(entry.text(Locale::En).step == 5);
    press_n(entry, EntryKey::Minus, 1);
    CHECK(value_is(entry, "000090"));
}

}  // namespace

int main()
{
    test_the_clock_task_saves_the_instant_the_review_showed();
    test_the_receipt_is_not_the_exit();
    test_a_refused_clock_keeps_the_draft_and_offers_both_ways_on();
    test_a_failed_clock_does_not_claim_nothing_changed();
    test_the_stepper_cannot_build_a_date_that_is_not_one();
    test_the_offset_steps_by_quarter_hours_and_stops_at_the_ends();
    test_the_first_step_off_the_grid_lands_on_it();
    test_a_seed_that_does_not_survive_its_offset_is_dropped();
    test_the_board_walk_reaches_the_node_and_the_passkey();
    test_the_board_walk_skips_a_node_that_is_not_there();
    test_the_clock_draft_stops_where_the_clock_does();
    test_a_refused_clock_does_not_end_the_board_walk();
    test_the_receipt_way_on_is_drawn_on_no_other_receipt();
    test_the_node_task_never_writes_the_clock();
    test_the_acting_key_is_drawn_and_never_the_one_that_asked();
    test_an_unpinned_watch_starts_at_the_passkey();
    test_every_forget_ending_gets_its_own_sentence();
    test_a_partial_forget_is_not_the_same_verdict_as_a_complete_one();
    test_the_forget_confirmation_promises_only_what_forget_does();
    test_leaving_a_wait_says_the_answer_is_lost();
    test_the_finished_frame_keeps_the_words_and_drops_the_keys();
    test_leave_is_never_a_trap();
    test_both_locales_print_the_whole_instant();
    test_the_passkey_step_names_the_digit_under_the_cursor();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("provisioning entry: all host checks passed\n");
    return 0;
}
