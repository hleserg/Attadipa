#include <cstdio>
#include <cstring>

#include "attadipa/apps/provisioning.h"

// The entry model with a board that records what it was asked. What is tested
// is the sequence -- which key does what to which field, and what reaches the
// board -- not any pixel.

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
using attadipa::l10n::Locale;

struct FakeBoard final : attadipa::core::Provisioner {
    ProvisionOutcome clock_answer = ProvisionOutcome::Accepted;
    ProvisionOutcome passkey_answer = ProvisionOutcome::Accepted;
    int clocks = 0, passkeys = 0;
    attadipa::core::WallClockEntry clock{};
    std::uint32_t passkey = 0;

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
        return passkey_answer;
    }
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
        CHECK(entry.finished());
        CHECK(entry.text(Locale::En).done);
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
        board.clock_answer = ProvisionOutcome::Accepted;
        entry.press(EntryKey::Ok);
        CHECK(entry.field() == EntryField::Passkey && board.clocks == 2);
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

    return failures == 0 ? 0 : 1;
}
