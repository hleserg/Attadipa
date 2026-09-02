#pragma once

#include <cstdint>

#include "attadipa/core/provisioning.h"
#include "attadipa/l10n/locale.h"

// The entry screen's half that has no pixels: which field is being typed, what
// has been typed into it, what OK does next. A keypad of fourteen keys drives
// it and a face renders `text()`; neither knows the other.
//
// Four fields, in the order a person has them to hand: the date, the time in
// UTC, the local offset, and the six-digit passkey the MeshCore node shows.
// The clock is committed when the offset is accepted -- the board wants all
// three at once -- and the passkey when its six digits are; OK on an empty
// passkey skips it, because a watch with no node yet still needs a clock.
//
// Cancel is the way out that is not the way through. Before the offset is
// accepted nothing has reached the board, so it leaves nothing behind; after,
// the clock the person just set stays and the passkey is skipped -- the same
// end as OK on an empty passkey. A long press is easy to make by accident on
// a face that invites a tap, and a screen that can only be left by retyping a
// correct clock is a trap (#406 round 1).

namespace attadipa::apps {

enum class EntryField : std::uint8_t { Date, Time, Offset, Passkey, Done };

enum class EntryKey : std::uint8_t {
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8,
    Digit9,
    Sign,       // Toggles the offset's sign; ignored elsewhere.
    Backspace,
    Ok,
    Cancel,     // Leave: nothing committed before the offset is; after it, skip.
};

// What was said about the last OK, shown until the next key.
enum class EntryVerdict : std::uint8_t {
    None, Accepted, Rejected, Failed, Skipped,
    Cancelled,  // Left before the board was asked anything.
};

struct EntryText {
    const char* title = "";   // which field
    char value[12] = {};      // the field with its blanks: "2026-09-__"
    const char* hint = "";    // what to type, or what the last OK did
    const char* ok = "";      // the OK key's label
    const char* backspace = "";
    const char* cancel = "";
    bool sign_key = false;    // whether the ± key means anything now
    bool done = false;
};

class ProvisioningEntry {
public:
    explicit ProvisioningEntry(core::Provisioner& sink);

    void press(EntryKey key);
    EntryText text(l10n::Locale locale) const;

    EntryField field() const { return field_; }
    EntryVerdict verdict() const { return verdict_; }
    bool finished() const { return field_ == EntryField::Done; }

private:
    unsigned capacity() const;
    void accept();

    core::Provisioner& sink_;
    EntryField field_ = EntryField::Date;
    EntryVerdict verdict_ = EntryVerdict::None;
    char digits_[9] = {};
    unsigned count_ = 0;
    bool offset_west_ = false;
    bool board_failed_ = false;  // a set_wall_clock the board could not finish
    // Kept across fields so the offset can be committed with them.
    std::int64_t year_ = 0;
    unsigned month_ = 0, day_ = 0, hour_ = 0, minute_ = 0;
};

}  // namespace attadipa::apps
