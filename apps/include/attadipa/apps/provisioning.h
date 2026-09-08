#pragma once

#include <cstdint>

#include "attadipa/core/clock.h"
#include "attadipa/core/provisioning.h"
#include "attadipa/l10n/locale.h"

// The entry screen's half that has no pixels: what is being set, what the six
// keys do to it, and what the board said. A face renders `text()`; neither
// knows the other.
//
// TWO TASKS, ONE AT A TIME (#469).
//
// A holder who wants to correct the clock should not have to walk past the
// node's passkey to do it, and a holder recovering a factory-reset node should
// not have to retype a date that was already right. `EntryTask` is fixed at
// construction and picks which sequence of fields this entry walks. It is a
// runtime discriminator and nothing more -- both tasks are this one class over
// one `core::Provisioner` -- so "a node task never writes the clock" is a
// property the tests prove about the journey, not one the type system can
// promise.
//
// NO TYPED DIGITS.
//
// Every value is stepped, not typed: `Minus` and `Plus` move the field under
// the cursor, `Previous` and `Next` move the cursor. A stepper cannot produce
// 2026-13-32, so there is no "that is not a date" to report and the fourteen-key
// pad is gone. What a stepper cannot rule out is the board refusing a value it
// does take the shape of, and `core::ProvisionOutcome::Rejected` is still a
// thing that happens; see the verdicts.
//
// THE RECEIPT IS NOT THE EXIT.
//
// `Receipt` shows what the board did. It stays until the holder leaves it:
// `finished()` is true on `Exit` and nowhere else. A watch that dismissed its
// own receipt would be the auto-dismiss #416 was about, one screen further on.
// A receipt that reports a failure keeps the draft and offers Retry and Back,
// so it is not an inert end either.

namespace attadipa::apps {

// Which of the two things this entry sets. Fixed for the life of the entry.
enum class EntryTask : std::uint8_t {
    LocalTime,   // Day..Offset, a review, one `set_wall_clock`.
    NodePasskey, // The node, optionally forgetting it, then its passkey.
    // Both halves in one walk: the clock, then its receipt, then the node and
    // the passkey. This is the shape the single flow had before this file was
    // split in two, and it is here because a board has no way to choose
    // between the two narrow tasks -- with only `LocalTime` wired, a product
    // image cannot reach `set_mesh_passkey` or `forget_mesh_node` at all, and
    // a watch off the shelf can never be given its node.
    //
    // Deliberately a placeholder. The chooser that makes the narrow tasks
    // reachable on their own is the entry screen's design work (#469); the
    // caller that gets one asks for `LocalTime` or `NodePasskey` and this
    // enumerator goes.
    All,
};

enum class EntryField : std::uint8_t {
    // LocalTime. `step`/`steps` count these six.
    Day, Month, Year, Hour, Minute, Offset,
    TimeReview,     // The draft and the UTC instant it means. Next saves.
    // NodePasskey.
    Node,           // The node this watch is pinned to. Forget asks to drop it.
    ForgetConfirm,  // Next forgets, Previous keeps, Leave goes back. Nothing
                    // has reached the board while this is on screen.
    Passkey,        // Six stepped digits; `step` is the one under the cursor.
    // Both.
    Receipt,        // What the board did. Visible until the holder leaves.
    Exit,           // Nothing is drawn here. `finished()` is this and only this.
};

enum class EntryKey : std::uint8_t {
    Minus,     // The value under the cursor, down.
    Plus,      // ... and up.
    Previous,  // Back one step; on the receipt, back to the draft.
    Next,      // On one; on the last step, the thing the step was for.
    Forget,    // Only on `Node`. Opens the confirmation, and confirms on it.
    Leave,     // Out. On `ForgetConfirm` it is Back, not out.
};

// What the last board answer was. `Rejected` and `Failed` stay apart because
// they describe different watches: `Rejected` changed nothing, `Failed` may
// have moved part of what it was given and has no rollback behind it. A screen
// that called both "did not work" would be telling the second holder that
// their RTC still holds the old time when it may not.
enum class EntryVerdict : std::uint8_t {
    None,
    // The clock.
    TimeSaved,        // Accepted. The only state that may say the clock is set.
    TimeRefused,      // Rejected: not a value this watch takes; nothing changed.
    TimeUncertain,    // Failed, or the Pending the contract forbids: part of
                      // the write may have landed.
    // The passkey.
    PasskeyPending,   // With the radio. Only `poll()` moves this.
    PasskeyStored,    // Armed, and on flash where it had to be.
    PasskeyRefused,   // Rejected before the radio took it. Nothing is armed.
    PasskeyUncertain, // Failed, or a bad answer after Pending: it may be armed
                      // for this boot and gone at the next.
    // The node. Four, not seven: the face needs to know which of complete,
    // partial, nothing and failed it is styling, and `forget_outcome()` carries
    // the exact one for the sentence.
    ForgetPending,
    NodeForgotten,        // Forgotten, or Unpinned: nothing of it is left.
    NodePartlyForgotten,  // PinOnFlash: a restart brings the old pin back.
    NodeNothingToForget,  // Nothing: there was neither a bond nor a pin.
    ForgetKept,           // BondKept or ReplayInhibited: trust stayed. Retry.
    // Leaving.
    Abandoned,        // Left while the radio still had a request of ours. The
                      // board keeps the answer from the entry that replaces
                      // this one, so nobody will ever be told how it ended.
};

// What the clock already believes, offered as the draft's starting point. A
// board that has no idea passes `valid = false` and the draft starts at a
// round default; the lack of a physical RTC is not by itself the test.
//
// `valid` is a claim, not a guarantee: the constructor applies the offset and
// checks the local instant it lands on. A seed that does not survive that is
// dropped whole, and `EntryText::seeded` then says false rather than showing a
// draft nothing stands behind.
struct EntrySeed {
    bool           valid          = false;
    core::WallTime utc{};
    std::int16_t   offset_minutes = 0;
};

// One frame of the screen, in one locale. Everything the face draws, and
// nothing about how.
struct EntryText {
    const char* title       = "";  // what is being set
    const char* instruction = "";  // what the keys do here
    const char* verdict     = "";  // what the board said, or ""

    char value[16] = {};  // the stepped field alone: "31", "UTC+05:15"
    char draft[40] = {};  // the whole local instant: "31.01.2026 · 21:00 · UTC+05:15"
    char utc[20]   = {};  // what that means in UTC: "2026-01-31 16:00Z"
    char node[9]   = {};  // the node's first eight hex digits

    // Per key, because a key with no label is a key nobody presses, and a face
    // that guessed the labels would guess "Next" on the step that saves. An
    // empty string is a key that does nothing here and should not be drawn.
    const char* minus    = "";
    const char* plus     = "";
    const char* previous = "";
    const char* next     = "";
    const char* forget   = "";
    const char* leave    = "";

    // Which step of how many. `steps == 0` is a field that is not stepped --
    // the review, the node, the confirmation, the receipt -- and the face
    // draws no progress there. On `Passkey`, `step` is the digit under the
    // cursor, 1 to 6.
    unsigned step  = 0;
    unsigned steps = 0;

    bool seeded   = false;  // the draft came from the clock, not from a default
    bool finished = false;  // `Exit`: there is nothing left to draw
    bool waiting  = false;  // the radio has a request and has not answered
};

class ProvisioningEntry {
public:
    ProvisioningEntry(core::Provisioner& sink, EntryTask task,
                      const EntrySeed& seed = {});

    void press(EntryKey key);

    // Asks the board whether the request it took has finished, and says
    // whether anything on the screen changed. Cheap and idempotent with
    // nothing in flight: whatever redraws this face may call it every tick.
    // Nothing else moves the screen off a Pending verdict.
    bool poll();

    EntryText text(l10n::Locale locale) const;

    EntryTask    task()    const { return task_; }
    EntryField   field()   const { return field_; }
    EntryVerdict verdict() const { return verdict_; }

    // True on `Exit` and nowhere else. A receipt is on the screen, not past it.
    bool finished() const { return field_ == EntryField::Exit; }

    // The board has a passkey or a forget of this screen's that it has not
    // answered.
    bool waiting() const { return awaiting_passkey_ || awaiting_forget_; }

    // The exact ending of the forget, for the sentence the receipt shows. The
    // verdict above is the four-way version the face styles from.
    core::MeshForgetOutcome forget_outcome() const { return forget_outcome_; }

    // The draft as it stands, for a test or a caller that wants the numbers
    // rather than the strings.
    core::CivilTime draft() const;
    std::int16_t    draft_offset_minutes() const { return offset_minutes_; }

private:
    void step_value(int direction);
    void advance(int direction);
    void save_time();
    void send_passkey();
    void begin_forget();
    void clamp_day();
    bool to_utc(core::WallTime& out) const;

    core::Provisioner& sink_;
    const EntryTask    task_;
    EntryField         field_;
    EntryVerdict       verdict_ = EntryVerdict::None;

    // The draft. Local civil time plus the offset that turns it into UTC.
    std::int64_t year_ = 2026;
    unsigned     month_ = 1, day_ = 1, hour_ = 0, minute_ = 0;
    std::int16_t offset_minutes_ = 0;
    bool         seeded_ = false;
    // A first step on the offset from a value that is not a multiple of the
    // grid moves to the nearest multiple in that direction rather than past it,
    // and only the first: after that it is grid by grid. Without the flag a
    // seeded +05:45 would step to +06:00 and then, on the way back, to +05:45
    // again, which is a stepper that cannot leave where it started.
    bool         offset_touched_ = false;

    unsigned digits_[6] = {};  // the passkey, one digit each
    unsigned cursor_    = 0;   // which of them the keys move

    bool awaiting_passkey_ = false;
    bool awaiting_forget_  = false;
    // A passkey the radio answered badly: it may have been armed for this boot
    // and not stored, so leaving is not "no passkey was set".
    bool passkey_uncertain_ = false;
    bool has_node_ = false;
    core::MeshPeerId        node_{};
    core::MeshForgetOutcome forget_outcome_ = core::MeshForgetOutcome::Nothing;
    // Where the receipt came from, so its keys know what Retry would retry and
    // what Back would go back to.
    EntryField  receipt_of_ = EntryField::Exit;
};

}  // namespace attadipa::apps
