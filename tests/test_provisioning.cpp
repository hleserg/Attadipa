#include <cstdio>
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
    void cancel_reprovision() { reprovision_pending = false; }
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

void type(ProvisioningEntry& entry, const char* digits)
{
    for (; *digits != '\0'; ++digits) {
        entry.press(static_cast<EntryKey>(
            static_cast<unsigned>(EntryKey::Digit0) +
            static_cast<unsigned>(*digits - '0')));
    }
}

bool value_is(const ProvisioningEntry& entry, const char* expected)
{
    return std::strcmp(entry.text(Locale::En).value, expected) == 0;
}

bool hint_is(const ProvisioningEntry& entry, const char* expected)
{
    return std::strcmp(entry.text(Locale::En).hint, expected) == 0;
}

// Everything before the passkey, typed the short way: a valid date, a time,
// and UTC. Leaves the entry on the passkey field with the clock committed.
void to_passkey(ProvisioningEntry& entry)
{
    type(entry, "20260902");
    entry.press(EntryKey::Ok);
    type(entry, "1230");
    entry.press(EntryKey::Ok);
    type(entry, "0000");
    entry.press(EntryKey::Ok);
}

}  // namespace

int main()
{
    // The whole path, west of Greenwich: 2026-09-02 12:30 local at UTC-3 is
    // 15:30Z -- no: the time field is typed in UTC, so the board gets 12:30Z
    // and the offset separately.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        CHECK(entry.field() == EntryField::Date);
        CHECK(value_is(entry, "____-__-__"));
        type(entry, "2026");
        CHECK(value_is(entry, "2026-__-__"));
        type(entry, "0902");
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Time);
        CHECK(entry.verdict() == EntryVerdict::Accepted);
        type(entry, "1230");
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Offset);
        CHECK(value_is(entry, "+__:__"));
        entry.press(EntryKey::Sign);
        type(entry, "0300");
        CHECK(value_is(entry, "-03:00"));
        CHECK(board.clocks == 0);
        entry.press(EntryKey::Ok);
        CHECK(board.clocks == 1);
        CHECK(board.clock.utc_seconds == 1788352200);  // 2026-09-02T12:30:00Z
        CHECK(board.clock.timezone_offset_minutes == -180);
        CHECK(entry.field() == EntryField::Passkey);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        CHECK(board.passkeys == 1 && board.passkey == 123456);
        // Queued is not armed. The radio has it, the screen waits, and the
        // sentence a person reads is neither of the two terminal ones.
        CHECK(!entry.finished() && entry.waiting());
        CHECK(entry.verdict() == EntryVerdict::Pending);
        CHECK(hint_is(entry, "still setting up the node"));
        CHECK(!entry.poll());
        CHECK(!entry.finished());
        // The worker armed it and flash holds it. Only now.
        board.worker(PasskeyOutcome::Armed);
        CHECK(entry.poll());
        CHECK(entry.finished() && !entry.waiting());
        CHECK(entry.text(Locale::En).done);
        CHECK(hint_is(entry, "the watch is set up"));
        // A second poll asks nothing: the answer was taken.
        const int polls = board.polls;
        CHECK(!entry.poll() && board.polls == polls);
        // Keys after Done do nothing.
        type(entry, "9");
        entry.press(EntryKey::Ok);
        CHECK(board.passkeys == 1 && entry.finished());
    }
    // A slip costs one key: a bad date is refused with its digits kept.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "20261332");
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Date);
        CHECK(entry.verdict() == EntryVerdict::Rejected);
        CHECK(value_is(entry, "2026-13-32"));
        for (int i = 0; i < 4; ++i) entry.press(EntryKey::Backspace);
        CHECK(value_is(entry, "2026-__-__"));
        CHECK(entry.verdict() == EntryVerdict::None);
        entry.press(EntryKey::Ok);  // short
        CHECK(entry.verdict() == EntryVerdict::Rejected);
        type(entry, "0229");  // 2026 is not a leap year
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Rejected);
        // Extra digits past the mask are dropped, not wrapped.
        type(entry, "99");
        CHECK(value_is(entry, "2026-02-29"));
    }
    // The year the chip can hold, and the hours of a day.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "19991231");
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Rejected);
        for (int i = 0; i < 8; ++i) entry.press(EntryKey::Backspace);
        type(entry, "20000101");
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Time);
        type(entry, "2400");
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Rejected);
        entry.press(EntryKey::Sign);  // means nothing here
        CHECK(value_is(entry, "24:00"));
    }
    // Every zone there is and none that is not: +14:00 yes, +14:01 no,
    // -12:00 yes, -12:01 no.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        type(entry, "1401");
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Rejected && board.clocks == 0);
        entry.press(EntryKey::Backspace);
        type(entry, "0");
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Passkey && board.clocks == 1);
        CHECK(board.clock.timezone_offset_minutes == 840);
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        entry.press(EntryKey::Sign);
        type(entry, "1201");
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Rejected && board.clocks == 0);
        entry.press(EntryKey::Backspace);
        type(entry, "0");
        entry.press(EntryKey::Ok);
        CHECK(board.clocks == 1 && board.clock.timezone_offset_minutes == -720);
    }
    // The board says Failed: the field stays, and the next OK asks again.
    {
        FakeBoard board;
        board.clock_answer = ProvisionOutcome::Failed;
        ProvisioningEntry entry(board);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Failed);
        CHECK(entry.field() == EntryField::Offset && board.clocks == 1);
        // Cancel here is not "nothing changed": the board was asked and may
        // have written the RTC before it failed, so the failure is what stays.
        {
            ProvisioningEntry left(board);
            type(left, "20260902");
            left.press(EntryKey::Ok);
            type(left, "0000");
            left.press(EntryKey::Ok);
            type(left, "0000");
            left.press(EntryKey::Ok);
            left.press(EntryKey::Cancel);
            CHECK(left.finished() && left.verdict() == EntryVerdict::Failed);
            CHECK(std::strcmp(left.text(Locale::En).hint, "could not be stored") == 0);
            CHECK(board.clocks == 2);
        }
        board.clock_answer = ProvisionOutcome::Accepted;
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Passkey && board.clocks == 3);
        // A refused passkey likewise.
        board.passkey_answer = ProvisionOutcome::Rejected;
        type(entry, "000000");
        entry.press(EntryKey::Ok);
        CHECK(entry.verdict() == EntryVerdict::Rejected && !entry.finished());
        CHECK(board.passkeys == 1);
    }
    // OK on an empty passkey skips it; the board is not asked.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        entry.press(EntryKey::Ok);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Skipped);
        CHECK(board.passkeys == 0);
        // Five digits is neither empty nor a passkey.
        FakeBoard board2;
        ProvisioningEntry entry2(board2);
        type(entry2, "20260902");
        entry2.press(EntryKey::Ok);
        type(entry2, "0000");
        entry2.press(EntryKey::Ok);
        type(entry2, "0000");
        entry2.press(EntryKey::Ok);
        type(entry2, "12345");
        entry2.press(EntryKey::Ok);
        CHECK(!entry2.finished() && board2.passkeys == 0);
    }
    // Both catalogues answer, and the sign key is offered only on the offset.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        CHECK(!entry.text(Locale::En).sign_key);
        CHECK(std::strcmp(entry.text(Locale::En).title,
                          entry.text(Locale::Ru).title) != 0);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        CHECK(entry.text(Locale::Ru).sign_key);
        // An accepted field shows the next field's hint, not a verdict.
        CHECK(std::strstr(entry.text(Locale::En).hint, "flips") != nullptr);
        entry.press(EntryKey::Ok);
        CHECK(std::strstr(entry.text(Locale::En).hint, "check") != nullptr);
    }
    // Cancel before the offset is accepted leaves nothing behind: the board
    // was never asked, and the screen says so.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "12");
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished());
        CHECK(entry.verdict() == EntryVerdict::Cancelled);
        CHECK(board.clocks == 0 && board.passkeys == 0);
        CHECK(entry.text(Locale::En).done);
        CHECK(std::strcmp(entry.text(Locale::En).hint, "nothing changed") == 0);
        CHECK(std::strcmp(entry.text(Locale::En).cancel, "Cancel") == 0);
        // Keys after a cancel do nothing either.
        type(entry, "3");
        entry.press(EntryKey::Ok);
        CHECK(board.clocks == 0 && entry.finished());
    }
    // Cancel on the passkey is the empty-passkey skip: the clock the person
    // just set stays set, and no passkey reaches the board.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        type(entry, "20260902");
        entry.press(EntryKey::Ok);
        type(entry, "1230");
        entry.press(EntryKey::Ok);
        type(entry, "0000");
        entry.press(EntryKey::Ok);
        CHECK(board.clocks == 1 && entry.field() == EntryField::Passkey);
        type(entry, "12");
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Skipped);
        CHECK(board.clocks == 1 && board.passkeys == 0);
    }
    // Cancel on a fresh screen: a long press made by accident costs one key.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Cancelled);
        CHECK(board.clocks == 0 && board.passkeys == 0);
    }

    // --- #416: the passkey is finished by the radio, not by the keypad ----

    // The stack refuses it. The screen says so, keeps the digits, and the next
    // OK asks again -- there is a way forward from a failure, which is what
    // makes it worth showing.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        board.worker(PasskeyOutcome::Refused);
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Failed);
        CHECK(!entry.finished() && !entry.waiting());
        CHECK(entry.field() == EntryField::Passkey);
        CHECK(hint_is(entry, "could not be stored"));
        CHECK(!entry.text(Locale::En).done);
        // The digits are still on the screen, so the retry is one key.
        CHECK(value_is(entry, "123456"));
        entry.press(EntryKey::Ok);
        CHECK(board.passkeys == 2 && entry.waiting());
        board.worker(PasskeyOutcome::Armed);
        CHECK(entry.poll());
        CHECK(entry.finished() && hint_is(entry, "the watch is set up"));
    }
    // Flash refuses the write. The passkey is armed for this boot and gone at
    // the next, which is a failure and not a set-up watch: Done is not
    // reachable from here at all.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        board.worker(PasskeyOutcome::NotStored);
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Failed && !entry.finished());
        CHECK(hint_is(entry, "could not be stored"));
        // Polling on does not turn it into a success later.
        CHECK(!entry.poll());
        CHECK(!entry.finished());
        // Leaving now does not claim the passkey was skipped: it may be armed
        // until the watch is next switched off.
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Failed);
        CHECK(hint_is(entry, "could not be stored"));
    }
    // The same exit through the other door: erase the digits and press OK,
    // which on a clean field is the skip. After NotStored it is not.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        board.worker(PasskeyOutcome::NotStored);
        CHECK(entry.poll());
        for (int i = 0; i < 6; ++i) entry.press(EntryKey::Backspace);
        entry.press(EntryKey::Ok);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Failed);
    }
    // A refusal after a failure does not forgive it. NotStored leaves the
    // digits armed in the radio and the board without a ticket; an OK the
    // worker queue then refuses is answered Failed over an idle slot, which
    // says nothing about what this screen was already told. Cancel and the
    // empty OK must still say Failed (#416, round 4).
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        board.worker(PasskeyOutcome::NotStored);
        CHECK(entry.poll());
        board.queue_full = true;
        entry.press(EntryKey::Ok);
        CHECK(!entry.finished() && entry.verdict() == EntryVerdict::Failed);
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Failed);
    }
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        board.worker(PasskeyOutcome::NotStored);
        CHECK(entry.poll());
        board.queue_full = true;
        entry.press(EntryKey::Ok);
        for (int i = 0; i < 6; ++i) entry.press(EntryKey::Backspace);
        entry.press(EntryKey::Ok);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Failed);
    }
    // Back to back. A second OK over an in-flight request would be a second
    // configure with one answer to share; the keypad is deaf until the radio
    // has answered, and the digits nobody can edit stay as they are.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        CHECK(board.passkeys == 1);
        entry.press(EntryKey::Ok);
        entry.press(EntryKey::Ok);
        type(entry, "9");
        entry.press(EntryKey::Backspace);
        CHECK(board.passkeys == 1);
        CHECK(entry.waiting() && entry.verdict() == EntryVerdict::Pending);
        CHECK(value_is(entry, "123456"));
        board.worker(PasskeyOutcome::Armed);
        CHECK(entry.poll() && entry.finished());
    }
    // The way out of the wait. A screen that can only be left by an answer the
    // radio may never send is the trap #406 round 1 closed for the clock; and
    // "no passkey; the clock is set" would be a lie, because the radio has one.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && !entry.waiting());
        CHECK(entry.verdict() == EntryVerdict::Pending);
        CHECK(entry.text(Locale::En).done);
        CHECK(hint_is(entry, "still setting up the node"));
        CHECK(std::strcmp(entry.text(Locale::Ru).hint,
                          "узел ещё настраивается") == 0);
        // Nothing the abandoned worker does can reach a screen that is over.
        board.worker(PasskeyOutcome::Armed);
        CHECK(!entry.poll());
        CHECK(hint_is(entry, "still setting up the node"));
    }
    // Cancel, reopen, type again: the answer to the session that was left
    // does not finish the one that replaced it (#416, DoD 3).
    {
        FakeBoard board;
        ProvisioningEntry left(board);
        to_passkey(left);
        type(left, "123456");
        left.press(EntryKey::Ok);
        const std::uint32_t abandoned = board.queued;
        left.press(EntryKey::Cancel);
        // The radio finishes what it was given. There is nobody on that screen
        // to hear it, and the answer sits there uncollected.
        board.worker(PasskeyOutcome::Armed);

        ProvisioningEntry fresh(board);
        to_passkey(fresh);
        type(fresh, "654321");
        fresh.press(EntryKey::Ok);
        CHECK(board.passkeys == 2 && board.passkey == 654321);
        CHECK(fresh.waiting() && board.queued != abandoned);
        // The success belonging to the passkey nobody waited for does not
        // finish this one.
        CHECK(!fresh.poll());
        CHECK(!fresh.finished() && fresh.waiting());

        // Nor does a completion that quotes the ticket it was made under.
        board.op.complete(abandoned, PasskeyOutcome::Armed);
        CHECK(!fresh.poll());
        CHECK(!fresh.finished());

        // The answer to this screen's own request is the one it hears.
        board.worker(PasskeyOutcome::NotStored);
        CHECK(fresh.poll());
        CHECK(fresh.verdict() == EntryVerdict::Failed && !fresh.finished());
    }
    // Two screens cannot have the radio at once. A request made over one it
    // has not answered is refused where the person can still be told, and is
    // taken as soon as the first is done -- there must not be two configures
    // in flight over one answer slot.
    {
        FakeBoard board;
        ProvisioningEntry left(board);
        to_passkey(left);
        type(left, "123456");
        left.press(EntryKey::Ok);
        const std::uint32_t in_flight = board.queued;
        left.press(EntryKey::Cancel);

        ProvisioningEntry fresh(board);
        to_passkey(fresh);
        type(fresh, "654321");
        fresh.press(EntryKey::Ok);
        CHECK(!fresh.waiting() && !fresh.finished());
        CHECK(fresh.verdict() == EntryVerdict::Failed);
        CHECK(board.queued == in_flight);  // nothing new was queued

        // The radio answers the first, and the retry is taken.
        board.worker(PasskeyOutcome::Armed);
        fresh.press(EntryKey::Ok);
        CHECK(fresh.waiting() && board.queued != in_flight);
        board.worker(PasskeyOutcome::Armed);
        CHECK(fresh.poll() && fresh.finished());
    }
    // Cancel after that refusal must not say "no passkey; the clock is set":
    // the first screen's request is still with the radio and may arm and
    // persist its digits after this screen is gone. The board's outcome is
    // Pending while that is so, and the entry reads it before it lets the
    // person leave under the skip line.
    {
        FakeBoard board;
        ProvisioningEntry left(board);
        to_passkey(left);
        type(left, "111111");
        left.press(EntryKey::Ok);
        left.press(EntryKey::Cancel);

        ProvisioningEntry fresh(board);
        to_passkey(fresh);
        type(fresh, "222222");
        fresh.press(EntryKey::Ok);
        CHECK(fresh.verdict() == EntryVerdict::Failed);
        fresh.press(EntryKey::Cancel);
        CHECK(fresh.finished() && fresh.verdict() == EntryVerdict::Failed);
    }
    // A queue that would not hold the request is refused where the person can
    // still be told, and nothing is left in flight: the entry stays on the
    // field and leaving it is still the honest skip, because no passkey ever
    // reached the radio. The board is asked once, at the refusal, whether an
    // earlier request is still out; it is not, and no tick asks again.
    {
        FakeBoard board;
        board.passkey_answer = ProvisionOutcome::Failed;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        CHECK(!entry.waiting() && !entry.finished());
        CHECK(entry.verdict() == EntryVerdict::Failed);
        CHECK(board.polls == 1);
        CHECK(!entry.poll() && board.polls == 1);
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Skipped);
    }
    // A board with no radio behind the seam still works: a terminal Accepted
    // from set_mesh_passkey() finishes the screen without a poll.
    {
        FakeBoard board;
        board.passkey_answer = ProvisionOutcome::Accepted;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        type(entry, "123456");
        entry.press(EntryKey::Ok);
        CHECK(entry.finished() && !entry.waiting());
        CHECK(hint_is(entry, "the watch is set up"));
    }

    // ----- The node field (#411) ------------------------------------------
    //
    // State (b) of the report's §6.1: the watch paired afresh with the reset
    // node and then refused its new key. Nothing stale is recorded, so the
    // bond is kept and the pin alone goes -- from memory, from flash, with
    // the refusal cooldown -- and nothing is re-armed: the radio stays down
    // until the passkey that follows arms it. The passkey itself is never
    // touched by the forget.
    {
        FakeBoard board;
        board.pinned = board.pin_on_flash = true;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        CHECK(entry.field() == EntryField::Node);
        CHECK(value_is(entry, "5c62d9bc"));
        CHECK(std::strcmp(entry.text(Locale::En).backspace, "Forget") == 0);
        CHECK(std::strcmp(entry.text(Locale::Ru).backspace, "Забыть") == 0);
        CHECK(std::strcmp(entry.text(Locale::En).title, "Node") == 0);
        CHECK(!entry.text(Locale::En).sign_key);
        // Digits mean nothing here.
        type(entry, "12");
        CHECK(value_is(entry, "5c62d9bc"));

        entry.press(EntryKey::Backspace);
        CHECK(board.forgets == 1 && entry.waiting());
        CHECK(entry.verdict() == EntryVerdict::Pending);
        CHECK(hint_is(entry, "still forgetting the node"));
        CHECK(entry.field() == EntryField::Node);
        // Still pinned: the screen said Pending and nothing has run.
        CHECK(board.pinned && board.armed);
        CHECK(!entry.poll());

        board.forget_worker();
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Forgotten);
        CHECK(entry.field() == EntryField::Passkey && !entry.waiting());
        CHECK(hint_is(entry, "forgotten; type its new digits"));
        CHECK(board.deletes == 0);           // the bond is kept
        CHECK(!board.pinned && !board.pin_on_flash);
        CHECK(!board.cooling_down);
        CHECK(!board.armed);                 // nothing re-armed
        CHECK(board.terminates == 1);
        CHECK(board.passkeys == 0);          // the passkey was not touched
        // The passkey hint stays the post-forget one while digits go in.
        type(entry, "1");
        CHECK(hint_is(entry, "forgotten; type its new digits"));
        entry.press(EntryKey::Backspace);
        // Leaving without a passkey is not "the clock is set".
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Skipped);
        CHECK(hint_is(entry, "no passkey; node is forgotten"));
        CHECK(!board.armed);
    }
    // State (a): a stale bond is recorded. It is deleted, once, and the pin
    // goes with it; the arm that follows is the passkey entry's Configure,
    // which is counted here as the passkey reaching the board and nothing
    // else -- the forget armed nothing.
    {
        FakeBoard board;
        board.pinned = board.pin_on_flash = true;
        board.stale_bond();
        ProvisioningEntry entry(board);
        to_passkey(entry);
        entry.press(EntryKey::Backspace);
        board.forget_worker();
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Forgotten);
        CHECK(board.deletes == 1);
        CHECK(board.delete_saw_marker && board.erase_saw_marker);
        CHECK(!board.recovery.recovery_required());
        CHECK(!board.pinned && !board.pin_on_flash && !board.armed);
        CHECK(board.reprovision_pending);
        CHECK(entry.field() == EntryField::Passkey);
        // A restart between the clear and the next adoption finds no pin:
        // what boot would read is `pin_on_flash`, and it is gone.
        {
            FakeBoard rebooted;
            rebooted.pinned = board.pin_on_flash;
            ProvisioningEntry again(rebooted);
            to_passkey(again);
            CHECK(again.field() == EntryField::Passkey);
        }
        type(entry, "654321");
        entry.press(EntryKey::Ok);
        CHECK(board.passkeys == 1 && board.passkey == 654321);
        board.worker(PasskeyOutcome::Armed);
        CHECK(!board.reprovision_pending);
        CHECK(entry.poll() && entry.finished());
        CHECK(hint_is(entry, "the watch is set up"));
    }
    // Refusing the transport termination or the crash-safe marker is a
    // truthful failure before either trust copy is changed.
    {
        FakeBoard board;
        board.pinned = board.pin_on_flash = true;
        board.stale_bond();
        board.terminate_refuses = true;
        CHECK(attadipa::firmware::forget_node(board) ==
              ForgetNodeOutcome::NotForgotten);
        CHECK(board.pinned && board.pin_on_flash);
        CHECK(board.recovery.recovery_required());
        CHECK(board.deletes == 0 && !board.reprovision_pending);

        board.terminate_refuses = false;
        board.marker_refuses = true;
        CHECK(attadipa::firmware::forget_node(board) ==
              ForgetNodeOutcome::NotForgotten);
        CHECK(board.pinned && board.pin_on_flash);
        CHECK(board.recovery.recovery_required());
        CHECK(board.deletes == 0 && !board.reprovision_pending);
    }
    // The store refuses. The record goes back, the pin is untouched in both
    // places, the node stays on the screen, and the key works again once
    // the store does.
    {
        FakeBoard board;
        board.pinned = board.pin_on_flash = true;
        board.stale_bond();
        board.store_refuses = true;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        entry.press(EntryKey::Backspace);
        board.forget_worker();
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Failed);
        CHECK(entry.field() == EntryField::Node && !entry.waiting());
        CHECK(hint_is(entry, "not forgotten; nothing changed"));
        CHECK(value_is(entry, "5c62d9bc"));
        CHECK(board.deletes == 1);
        CHECK(board.recovery.recovery_required());
        CHECK(board.pinned && board.pin_on_flash);
        board.store_refuses = false;
        entry.press(EntryKey::Backspace);
        board.forget_worker();
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Forgotten);
        CHECK(board.deletes == 2 && !board.pinned && !board.pin_on_flash);
    }
    // Flash refuses the erase. Memory is clear, so the next adoption goes
    // through, and the hint says what a restart before it would undo.
    {
        FakeBoard board;
        board.pinned = board.pin_on_flash = true;
        board.erase_refuses = true;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        entry.press(EntryKey::Backspace);
        board.forget_worker();
        CHECK(entry.poll());
        CHECK(entry.verdict() == EntryVerdict::Forgotten);
        CHECK(entry.field() == EntryField::Passkey);
        CHECK(hint_is(entry, "forgot till reboot; type digits"));
        CHECK(!board.pinned && board.pin_on_flash);
    }
    // Nothing to forget, twice over: with no pin the field is not shown at
    // all, and a request made anyway is refused where the caller can be told.
    {
        FakeBoard board;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        CHECK(entry.field() == EntryField::Passkey);
        CHECK(board.forget_mesh_node() == ProvisionOutcome::Rejected);
        CHECK(board.forgets == 1);
    }
    // The pin went between the field being shown and the key: the worker
    // finds nothing, says so, and the passkey field that follows is the
    // ordinary one -- nothing was forgotten here.
    {
        FakeBoard board;
        board.pinned = true;
        board.stale_bond();
        ProvisioningEntry entry(board);
        to_passkey(entry);
        CHECK(entry.field() == EntryField::Node);
        board.pinned = false;
        BondIdentity gone{};
        (void)board.recovery.take_forget(gone);
        entry.press(EntryKey::Backspace);
        CHECK(entry.verdict() == EntryVerdict::Forgotten);
        CHECK(hint_is(entry, "nothing to forget"));
        CHECK(entry.field() == EntryField::Passkey && !entry.waiting());
        CHECK(hint_is(entry, "nothing to forget"));
        entry.press(EntryKey::Cancel);
        CHECK(hint_is(entry, "no passkey; the clock is set"));
    }
    // OK keeps the node: on to the passkey with nothing asked of the board.
    {
        FakeBoard board;
        board.pinned = true;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Passkey);
        CHECK(board.forgets == 0 && board.pinned);
        CHECK(hint_is(entry, "six digits from the node; OK alone skips"));
    }
    // Leaving while the forget is with the radio: the screen says which
    // wait it left, and the worker's answer lands in a slot nobody reads.
    {
        FakeBoard board;
        board.pinned = true;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        entry.press(EntryKey::Backspace);
        // No other key does anything while the radio has it.
        entry.press(EntryKey::Ok);
        CHECK(board.forgets == 1 && entry.waiting());
        entry.press(EntryKey::Cancel);
        CHECK(entry.finished() && entry.verdict() == EntryVerdict::Pending);
        CHECK(hint_is(entry, "still forgetting the node"));
        board.forget_worker();
        CHECK(!entry.poll());
        CHECK(!board.pinned);
    }
    // The worker queue refusing the post: nothing changed and the key can
    // be pressed again.
    {
        FakeBoard board;
        board.pinned = true;
        board.queue_full = true;
        ProvisioningEntry entry(board);
        to_passkey(entry);
        entry.press(EntryKey::Backspace);
        CHECK(entry.verdict() == EntryVerdict::Failed && !entry.waiting());
        CHECK(hint_is(entry, "not forgotten; nothing changed"));
        CHECK(board.pinned && entry.field() == EntryField::Node);
        board.queue_full = false;
        entry.press(EntryKey::Backspace);
        CHECK(entry.waiting());
    }

    return failures == 0 ? 0 : 1;
}
