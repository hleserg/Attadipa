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
// A fifth, only on a watch that is pinned to a node, sits between the offset
// and the passkey: the node itself, shown as the first eight hex digits of
// its key so it can be held against the node's own screen. OK keeps it;
// the erase key -- labelled Forget there -- asks the board to drop it, bond
// and pin together, which is what a factory-reset node needs (#411). It is
// where it is because a forgotten node's passkey is the next thing typed,
// and the passkey hint then says so: the node shows new digits after a
// reset, and the old ones would fail silently.
//
// Cancel is the way out that is not the way through. Before the offset is
// accepted nothing has reached the board, so it leaves nothing behind; after,
// the clock the person just set stays and the passkey is skipped -- the same
// end as OK on an empty passkey. A long press is easy to make by accident on
// a face that invites a tap, and a screen that can only be left by retyping a
// correct clock is a trap (#406 round 1).
//
// The passkey does not finish where it is typed. The radio arms and stores it
// on its own task, so OK on six digits ends in `Pending` and this screen waits
// -- `poll()`, driven by whatever redraws the face -- until the board says
// which way it went. Done is reachable from a terminal success and from
// nowhere else (#416): before that the watch had said "the watch is set up"
// while the stack was still being asked, and a passkey the flash write then
// refused was gone at the next boot with nobody told.

namespace attadipa::apps {

enum class EntryField : std::uint8_t { Date, Time, Offset, Node, Passkey, Done };

enum class EntryKey : std::uint8_t {
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8,
    Digit9,
    Sign,       // Toggles the offset's sign; ignored elsewhere.
    Backspace,  // On the node field: forget it.
    Ok,
    Cancel,     // Leave: nothing committed before the offset is; after it, skip.
};

// What was said about the last OK, shown until the next key.
enum class EntryVerdict : std::uint8_t {
    None, Accepted, Rejected, Failed, Skipped,
    Cancelled,  // Left before the board was asked anything.
    Pending,    // The radio has the passkey or the forget and has not
                // answered yet -- or, on a finished screen, still had not
                // when the person left.
    Forgotten,  // The forget finished, one way or another: the hint names
                // which, from the board's `MeshForgetOutcome`.
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

    // Asks the board whether the passkey it took has finished, and says whether
    // anything on the screen changed. Cheap and idempotent when nothing is in
    // flight: whatever redraws this face may call it every tick. Nothing else
    // moves the screen off `Pending`, so a face that never calls it never
    // reaches Done.
    bool poll();

    EntryText text(l10n::Locale locale) const;

    EntryField field() const { return field_; }
    EntryVerdict verdict() const { return verdict_; }
    bool finished() const { return field_ == EntryField::Done; }
    // The board has a passkey or a forget of this screen's that it has not
    // answered.
    bool waiting() const { return awaiting_ || awaiting_forget_; }

private:
    unsigned capacity() const;
    void accept();
    void forget();
    void advance();

    core::Provisioner& sink_;
    EntryField field_ = EntryField::Date;
    EntryVerdict verdict_ = EntryVerdict::None;
    char digits_[9] = {};
    unsigned count_ = 0;
    bool offset_west_ = false;
    bool board_failed_ = false;  // a set_wall_clock the board could not finish
    bool awaiting_ = false;      // a passkey the radio has not answered
    // A passkey the radio answered badly: it may have been armed for this boot
    // and not stored, so leaving now is not the empty-passkey skip.
    bool passkey_failed_ = false;
    bool awaiting_forget_ = false;  // a forget the radio has not answered
    bool pending_is_forget_ = false;  // which wait a Pending exit left
    // The node was forgotten on this screen, so the passkey hint asks for the
    // digits the node shows *now*, and leaving without one is not "the clock
    // is set": the watch is silent until its node's current passkey is typed.
    bool node_forgotten_ = false;
    core::MeshForgetOutcome forget_outcome_ = core::MeshForgetOutcome::Nothing;
    core::MeshPeerId node_{};  // what the node field shows
    // Kept across fields so the offset can be committed with them.
    std::int64_t year_ = 0;
    unsigned month_ = 0, day_ = 0, hour_ = 0, minute_ = 0;
};

}  // namespace attadipa::apps
