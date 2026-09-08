#include "attadipa/apps/provisioning.h"

#include <cstdio>

#include "attadipa/l10n/string_id.h"
#include "attadipa/l10n/tr.h"

namespace attadipa::apps {
namespace {

using l10n::StringId;

// UTC-12 to UTC+14 is every zone there is, and every one of them is a whole
// number of quarter hours -- Nepal's +05:45 and the Chatham Islands' +12:45
// are the awkward ones, and both land on the grid.
constexpr std::int16_t kGrid        = 15;
constexpr std::int16_t kOffsetFloor = -12 * 60;
constexpr std::int16_t kOffsetCeil  = 14 * 60;
// The clock this product sets is a wristwatch's, not an archive's.
constexpr std::int64_t kYearFloor = 2000;
constexpr std::int64_t kYearCeil  = 2099;
constexpr unsigned     kPasskeyDigits = 6;
constexpr unsigned     kTimeSteps     = 6;  // day, month, year, hour, minute, offset

unsigned wrap(unsigned value, int direction, unsigned low, unsigned high)
{
    if (direction > 0) { return value >= high ? low : value + 1; }
    return value <= low ? high : value - 1;
}

std::int64_t wrap_year(std::int64_t value, int direction)
{
    if (direction > 0) { return value >= kYearCeil ? kYearFloor : value + 1; }
    return value <= kYearFloor ? kYearCeil : value - 1;
}

// The offset, by quarter hours, clamped rather than wrapped: a stepper that
// rolled from +14:00 to -12:00 in one key would set the wrong day by a key
// slip and say nothing about it.
//
// The first step off a value that is not on the grid goes to the nearest grid
// point in that direction, and only the first. `%` truncates toward zero in
// C++, so `rem` carries the sign of `value` and the two directions are not
// mirror images of each other.
std::int16_t step_offset(std::int16_t value, int direction, bool touched)
{
    int next = 0;
    const int rem = value % kGrid;
    if (!touched && rem != 0) {
        next = direction > 0 ? value - rem + (rem > 0 ? kGrid : 0)
                             : value - rem - (rem < 0 ? kGrid : 0);
    } else {
        next = value + direction * kGrid;
    }
    if (next < kOffsetFloor) { next = kOffsetFloor; }
    if (next > kOffsetCeil)  { next = kOffsetCeil; }
    return static_cast<std::int16_t>(next);
}

// snprintf that refuses to leave a truncated string behind. A half-written
// instant is a wrong instant and the face cannot tell one from a short one;
// an empty field it can at least decline to draw. Every caller here is sized
// to fit, so a false is a bug in this file rather than a runtime condition --
// which is exactly why it must not be silent.
bool fits(int written, unsigned capacity, char* out)
{
    if (written < 0 || static_cast<unsigned>(written) >= capacity) {
        out[0] = '\0';
        return false;
    }
    return true;
}

}  // namespace

ProvisioningEntry::ProvisioningEntry(core::Provisioner& sink, EntryTask task,
                                     const EntrySeed& seed)
    : sink_(sink),
      task_(task),
      field_(task == EntryTask::LocalTime ? EntryField::Day : EntryField::Node)
{
    if (task_ == EntryTask::LocalTime) {
        // `valid` is the board's claim; this is the check. An offset outside
        // the zones there are, or a local instant outside the years this watch
        // sets, is dropped whole rather than shown as a draft with a false
        // `seeded` beside it.
        if (seed.valid && seed.offset_minutes >= kOffsetFloor &&
            seed.offset_minutes <= kOffsetCeil) {
            const core::WallTime local{
                seed.utc.unix_seconds +
                static_cast<std::int64_t>(seed.offset_minutes) * 60};
            core::CivilTime civil;
            if (core::civil_from_wall_time(local, civil) &&
                civil.year >= kYearFloor && civil.year <= kYearCeil) {
                year_   = civil.year;
                month_  = civil.month;
                day_    = civil.day;
                hour_   = civil.hour;
                minute_ = civil.minute;
                offset_minutes_ = seed.offset_minutes;
                seeded_ = true;
            }
        }
        return;
    }
    // Nothing to keep or forget on a watch that is pinned to no node, and no
    // field to show it in: that journey is the passkey alone.
    has_node_ = sink_.mesh_node(node_);
    if (!has_node_) { field_ = EntryField::Passkey; }
}

// February, and the short months. Stepping the month or the year off a 31st
// has to land somewhere real, and the last day of the new month is the one a
// person meant.
void ProvisioningEntry::clamp_day()
{
    const unsigned last = core::days_in_month(year_, month_);
    if (day_ > last) { day_ = last; }
}

bool ProvisioningEntry::to_utc(core::WallTime& out) const
{
    core::CivilTime civil;
    civil.year   = year_;
    civil.month  = month_;
    civil.day    = day_;
    civil.hour   = hour_;
    civil.minute = minute_;
    if (!core::wall_time_from_civil(civil, out)) { return false; }
    out.unix_seconds -= static_cast<std::int64_t>(offset_minutes_) * 60;
    return true;
}

core::CivilTime ProvisioningEntry::draft() const
{
    core::CivilTime civil;
    civil.year   = year_;
    civil.month  = month_;
    civil.day    = day_;
    civil.hour   = hour_;
    civil.minute = minute_;
    return civil;
}

void ProvisioningEntry::step_value(int direction)
{
    switch (field_) {
    case EntryField::Day:
        day_ = wrap(day_, direction, 1, core::days_in_month(year_, month_));
        break;
    case EntryField::Month:
        month_ = wrap(month_, direction, 1, 12);
        clamp_day();
        break;
    case EntryField::Year:
        year_ = wrap_year(year_, direction);
        clamp_day();
        break;
    case EntryField::Hour:
        hour_ = wrap(hour_, direction, 0, 23);
        break;
    case EntryField::Minute:
        minute_ = wrap(minute_, direction, 0, 59);
        break;
    case EntryField::Offset:
        offset_minutes_ = step_offset(offset_minutes_, direction, offset_touched_);
        offset_touched_ = true;
        break;
    case EntryField::Passkey:
        digits_[cursor_] = wrap(digits_[cursor_], direction, 0, 9);
        break;
    case EntryField::TimeReview:
    case EntryField::Node:
    case EntryField::ForgetConfirm:
    case EntryField::Receipt:
    case EntryField::Exit:
        break;
    }
}

// One step along the sequence this task walks. The two sequences do not meet.
void ProvisioningEntry::advance(int direction)
{
    switch (field_) {
    case EntryField::Day:
        if (direction > 0) { field_ = EntryField::Month; }
        break;
    case EntryField::Month:
        field_ = direction > 0 ? EntryField::Year : EntryField::Day;
        break;
    case EntryField::Year:
        field_ = direction > 0 ? EntryField::Hour : EntryField::Month;
        break;
    case EntryField::Hour:
        field_ = direction > 0 ? EntryField::Minute : EntryField::Year;
        break;
    case EntryField::Minute:
        field_ = direction > 0 ? EntryField::Offset : EntryField::Hour;
        break;
    case EntryField::Offset:
        field_ = direction > 0 ? EntryField::TimeReview : EntryField::Minute;
        break;
    case EntryField::TimeReview:
        // Forward from the review is the save, which is not a field move.
        if (direction > 0) { save_time(); } else { field_ = EntryField::Offset; }
        break;
    case EntryField::Node:
        if (direction > 0) { field_ = EntryField::Passkey; }
        break;
    case EntryField::Passkey:
        if (direction > 0) {
            if (cursor_ + 1 < kPasskeyDigits) { ++cursor_; }
            else { send_passkey(); }
        } else if (cursor_ > 0) {
            --cursor_;
        } else if (has_node_) {
            field_ = EntryField::Node;
        }
        break;
    case EntryField::ForgetConfirm:
    case EntryField::Receipt:
    case EntryField::Exit:
        break;
    }
}

void ProvisioningEntry::save_time()
{
    receipt_of_ = EntryField::TimeReview;
    field_      = EntryField::Receipt;
    core::WallTime utc;
    if (!to_utc(utc)) {
        // The steppers cannot build a date that is not a date, so this is a
        // guard and not a path: reaching it means one of them let a value out
        // of range, and the honest answer is that nothing was written.
        verdict_ = EntryVerdict::TimeRefused;
        return;
    }
    switch (sink_.set_wall_clock({utc.unix_seconds, offset_minutes_})) {
    case core::ProvisionOutcome::Accepted:
        verdict_ = EntryVerdict::TimeSaved;
        return;
    case core::ProvisionOutcome::Rejected:
        verdict_ = EntryVerdict::TimeRefused;
        return;
    case core::ProvisionOutcome::Pending:
        // `set_wall_clock` is terminal by contract -- the clock is written by
        // the task that asks -- so there is no second half to wait for and no
        // `mesh_passkey_outcome()` equivalent to collect one from. A board
        // that answers this has left the write in a state nobody can read,
        // which is the uncertain case and not a wait.
    case core::ProvisionOutcome::Failed:
        verdict_ = EntryVerdict::TimeUncertain;
        return;
    }
}

void ProvisioningEntry::send_passkey()
{
    unsigned value = 0;
    for (unsigned i = 0; i < kPasskeyDigits; ++i) {
        value = value * 10 + digits_[i];
    }
    switch (sink_.set_mesh_passkey(value)) {
    case core::ProvisionOutcome::Pending:
        // The radio has it and has not armed it yet. The digits stay on the
        // screen and the field does not move: `poll()` is the only thing that
        // can carry this the rest of the way, which is the whole of #416.
        verdict_ = EntryVerdict::PasskeyPending;
        awaiting_passkey_ = true;
        return;
    case core::ProvisionOutcome::Accepted:
        verdict_ = EntryVerdict::PasskeyStored;
        break;
    case core::ProvisionOutcome::Rejected:
        verdict_ = EntryVerdict::PasskeyRefused;
        break;
    case core::ProvisionOutcome::Failed:
        // Refused before the radio saw it: no queue for it, no storage to keep
        // it in, or an earlier passkey still with the radio. Only the last can
        // still arm something, and the board says which -- its outcome is
        // `Pending` while a request is in flight and `Failed` with nothing
        // outstanding. Latched, because a later refusal cannot forgive an
        // earlier one (#416, round 4).
        passkey_uncertain_ =
            passkey_uncertain_ ||
            sink_.mesh_passkey_outcome() != core::ProvisionOutcome::Failed;
        verdict_ = passkey_uncertain_ ? EntryVerdict::PasskeyUncertain
                                      : EntryVerdict::PasskeyRefused;
        break;
    }
    receipt_of_ = EntryField::Passkey;
    field_      = EntryField::Receipt;
}

void ProvisioningEntry::begin_forget()
{
    switch (sink_.forget_mesh_node()) {
    case core::ProvisionOutcome::Pending:
        // The clears run on the radio's task. The confirmation stays on the
        // screen while they do, so there is somewhere for the answer to land.
        verdict_ = EntryVerdict::ForgetPending;
        awaiting_forget_ = true;
        return;
    case core::ProvisionOutcome::Rejected:
        // Nothing to forget: the node went between the field being drawn and
        // the key. Not a failure, and not a success either.
        forget_outcome_ = core::MeshForgetOutcome::Nothing;
        verdict_  = EntryVerdict::NodeNothingToForget;
        has_node_ = false;
        break;
    case core::ProvisionOutcome::Accepted:
        // Not a value the contract allows -- the clears run elsewhere -- but a
        // board that says it finished is not told that it failed.
        forget_outcome_ = core::MeshForgetOutcome::Forgotten;
        verdict_  = EntryVerdict::NodeForgotten;
        has_node_ = false;
        break;
    case core::ProvisionOutcome::Failed:
        // The request never reached the worker, so nothing of the node was
        // touched and the retry is honest. `BondKept` is the outcome that says
        // exactly that, and it is what the board itself answers with nothing
        // outstanding.
        forget_outcome_ = core::MeshForgetOutcome::BondKept;
        verdict_ = EntryVerdict::ForgetKept;
        break;
    }
    receipt_of_ = EntryField::Node;
    field_      = EntryField::Receipt;
}

// The receipt's forward key. What it is depends on what the receipt says, and
// so does where it goes.
namespace {

bool retryable(EntryVerdict verdict)
{
    switch (verdict) {
    case EntryVerdict::TimeRefused:
    case EntryVerdict::TimeUncertain:
    case EntryVerdict::PasskeyRefused:
    case EntryVerdict::PasskeyUncertain:
    case EntryVerdict::ForgetKept:
        return true;
    default:
        return false;
    }
}

// A receipt that leads somewhere rather than out: the node was forgotten in
// some measure, and the passkey it now needs is the next thing to set. The
// node shows new digits after a reset (#411), so stopping here would leave a
// watch that is silent with nothing on screen saying why.
bool leads_to_passkey(EntryVerdict verdict)
{
    return verdict == EntryVerdict::NodeForgotten ||
           verdict == EntryVerdict::NodePartlyForgotten ||
           verdict == EntryVerdict::NodeNothingToForget;
}

}  // namespace

void ProvisioningEntry::press(EntryKey key)
{
    if (field_ == EntryField::Exit) { return; }

    // While the radio has a request of ours the only key that means anything
    // is the way out. A second confirm would post a second request over an
    // answer nobody has read, and the steppers would edit a value that has
    // already gone.
    if (waiting()) {
        if (key == EntryKey::Leave) {
            verdict_ = EntryVerdict::Abandoned;
            awaiting_passkey_ = false;
            awaiting_forget_  = false;
            receipt_of_ = EntryField::Exit;  // nothing to go back to
            field_      = EntryField::Receipt;
        }
        return;
    }

    if (field_ == EntryField::Receipt) {
        switch (key) {
        case EntryKey::Next:
            if (retryable(verdict_)) {
                if (receipt_of_ == EntryField::TimeReview)   { save_time(); }
                else if (receipt_of_ == EntryField::Passkey) { send_passkey(); }
                else if (receipt_of_ == EntryField::Node)    { begin_forget(); }
                return;
            }
            if (leads_to_passkey(verdict_)) {
                verdict_ = EntryVerdict::None;
                cursor_  = 0;
                field_   = EntryField::Passkey;
                return;
            }
            field_ = EntryField::Exit;
            return;
        case EntryKey::Previous:
            // Back to the draft that produced this, untouched. Only from a
            // receipt there is something to go back to.
            if (retryable(verdict_) && receipt_of_ != EntryField::Exit) {
                verdict_ = EntryVerdict::None;
                field_   = receipt_of_;
                if (field_ == EntryField::Passkey) { cursor_ = 0; }
            }
            return;
        case EntryKey::Leave:
            field_ = EntryField::Exit;
            return;
        default:
            return;
        }
    }

    if (field_ == EntryField::ForgetConfirm) {
        switch (key) {
        case EntryKey::Next:
        case EntryKey::Forget:
            begin_forget();
            return;
        case EntryKey::Previous:  // Keep
        case EntryKey::Leave:     // Back
            // Neither has asked the board anything, and neither has touched
            // the node. The confirmation is the only screen `Leave` does not
            // leave from, because the key beside it is the destructive one.
            verdict_ = EntryVerdict::None;
            field_   = EntryField::Node;
            return;
        default:
            return;
        }
    }

    switch (key) {
    case EntryKey::Minus:
        verdict_ = EntryVerdict::None;
        step_value(-1);
        return;
    case EntryKey::Plus:
        verdict_ = EntryVerdict::None;
        step_value(1);
        return;
    case EntryKey::Previous:
        verdict_ = EntryVerdict::None;
        advance(-1);
        return;
    case EntryKey::Next:
        verdict_ = EntryVerdict::None;
        advance(1);
        return;
    case EntryKey::Forget:
        if (field_ == EntryField::Node) {
            verdict_ = EntryVerdict::None;
            field_   = EntryField::ForgetConfirm;
        }
        return;
    case EntryKey::Leave:
        // Out, with nothing in flight. Nothing has been written that was not
        // already reported on a receipt, so there is nothing left to say.
        field_ = EntryField::Exit;
        return;
    }
}

bool ProvisioningEntry::poll()
{
    if (awaiting_forget_) {
        const core::MeshForgetOutcome outcome = sink_.mesh_forget_outcome();
        if (outcome == core::MeshForgetOutcome::Pending) { return false; }
        awaiting_forget_ = false;
        forget_outcome_  = outcome;
        switch (outcome) {
        case core::MeshForgetOutcome::Forgotten:
        case core::MeshForgetOutcome::Unpinned:
            // Nothing of the node is left in either. `Unpinned` is the state
            // where no stale bond was ever recorded, so the pin was all there
            // was to drop.
            verdict_  = EntryVerdict::NodeForgotten;
            has_node_ = false;
            break;
        case core::MeshForgetOutcome::PinOnFlash:
            // Gone in RAM and still on flash. A restart before the next node
            // is adopted brings the old pin back, so this is not "forgotten"
            // and must not be drawn as one.
            verdict_  = EntryVerdict::NodePartlyForgotten;
            has_node_ = false;
            break;
        case core::MeshForgetOutcome::Nothing:
            verdict_  = EntryVerdict::NodeNothingToForget;
            has_node_ = false;
            break;
        case core::MeshForgetOutcome::BondKept:
        case core::MeshForgetOutcome::ReplayInhibited:
            // Trust stayed, because the store refused or its durable rollback
            // did. The node is still this watch's and the retry is real.
            verdict_ = EntryVerdict::ForgetKept;
            break;
        case core::MeshForgetOutcome::Pending:
            return false;
        }
        receipt_of_ = EntryField::Node;
        field_      = EntryField::Receipt;
        return true;
    }
    if (!awaiting_passkey_) { return false; }
    switch (sink_.mesh_passkey_outcome()) {
    case core::ProvisionOutcome::Pending:
        return false;
    case core::ProvisionOutcome::Accepted:
        awaiting_passkey_ = false;
        verdict_ = EntryVerdict::PasskeyStored;
        break;
    case core::ProvisionOutcome::Rejected:
        // A refusal this late is not a statement about the digits -- they were
        // taken. It is the request ending badly, and it ends the same way.
    case core::ProvisionOutcome::Failed:
        // The stack refused the passkey, or flash did. Either way it may be
        // armed for this boot and gone at the next, so leaving cannot claim
        // that no passkey was set.
        awaiting_passkey_  = false;
        passkey_uncertain_ = true;
        verdict_ = EntryVerdict::PasskeyUncertain;
        break;
    }
    receipt_of_ = EntryField::Passkey;
    field_      = EntryField::Receipt;
    return true;
}

namespace {

StringId title_of(EntryField field)
{
    switch (field) {
    case EntryField::Day:           return StringId::ProvisionTitleDay;
    case EntryField::Month:         return StringId::ProvisionTitleMonth;
    case EntryField::Year:          return StringId::ProvisionTitleYear;
    case EntryField::Hour:          return StringId::ProvisionTitleHour;
    case EntryField::Minute:        return StringId::ProvisionTitleMinute;
    case EntryField::Offset:        return StringId::ProvisionTitleOffset;
    case EntryField::TimeReview:    return StringId::ProvisionTitleReview;
    case EntryField::Node:          return StringId::ProvisionTitleNode;
    case EntryField::ForgetConfirm: return StringId::ProvisionTitleForget;
    case EntryField::Passkey:       return StringId::ProvisionTitlePasskey;
    case EntryField::Receipt:       return StringId::ProvisionTitleReceipt;
    case EntryField::Exit:          break;
    }
    return StringId::ProvisionTitleReceipt;
}

// The receipt has no instruction: the line above it is the board's answer and
// the keys below it are labelled. A sentence between the two would be a third
// account of the same thing.
const char* instruction_of(EntryField field, l10n::Locale locale)
{
    switch (field) {
    case EntryField::Day:
    case EntryField::Month:
    case EntryField::Year:
        return l10n::tr(StringId::ProvisionHintDate, locale);
    case EntryField::Hour:
    case EntryField::Minute:
        return l10n::tr(StringId::ProvisionHintTime, locale);
    case EntryField::Offset:
        return l10n::tr(StringId::ProvisionHintOffset, locale);
    case EntryField::TimeReview:
        return l10n::tr(StringId::ProvisionHintReview, locale);
    case EntryField::Node:
        return l10n::tr(StringId::ProvisionHintNode, locale);
    case EntryField::ForgetConfirm:
        return l10n::tr(StringId::ProvisionHintForget, locale);
    case EntryField::Passkey:
        return l10n::tr(StringId::ProvisionHintPasskey, locale);
    case EntryField::Receipt:
    case EntryField::Exit:
        break;
    }
    return "";
}

// The exact sentence a forget ended with. The verdict is the four-way shape
// the face styles from; this is the seven-way truth it prints, and the two are
// deliberately not the same enum.
StringId forget_line(core::MeshForgetOutcome outcome)
{
    switch (outcome) {
    case core::MeshForgetOutcome::Forgotten:
    case core::MeshForgetOutcome::Unpinned:  return StringId::ProvisionNodeForgotten;
    case core::MeshForgetOutcome::PinOnFlash: return StringId::ProvisionNodeForgottenRam;
    case core::MeshForgetOutcome::Nothing:    return StringId::ProvisionNodeNothing;
    case core::MeshForgetOutcome::ReplayInhibited:
        return StringId::ProvisionNodeReplayInhibited;
    case core::MeshForgetOutcome::BondKept:
    case core::MeshForgetOutcome::Pending:   break;
    }
    return StringId::ProvisionNodeKept;
}

}  // namespace

EntryText ProvisioningEntry::text(l10n::Locale locale) const
{
    EntryText out;
    out.finished = field_ == EntryField::Exit;
    out.waiting  = waiting();
    out.seeded   = seeded_;
    if (out.finished) { return out; }

    out.title       = l10n::tr(title_of(field_), locale);
    out.instruction = instruction_of(field_, locale);

    // --- the verdict line -------------------------------------------------
    switch (verdict_) {
    case EntryVerdict::None:
        break;
    case EntryVerdict::TimeSaved:
        out.verdict = l10n::tr(StringId::ProvisionTimeSaved, locale);
        break;
    case EntryVerdict::TimeRefused:
    case EntryVerdict::PasskeyRefused:
        out.verdict = l10n::tr(StringId::ProvisionRejected, locale);
        break;
    case EntryVerdict::TimeUncertain:
        out.verdict = l10n::tr(StringId::ProvisionTimeUncertain, locale);
        break;
    case EntryVerdict::PasskeyPending:
        out.verdict = l10n::tr(StringId::ProvisionPending, locale);
        break;
    case EntryVerdict::PasskeyStored:
        out.verdict = l10n::tr(StringId::ProvisionDone, locale);
        break;
    case EntryVerdict::PasskeyUncertain:
        out.verdict = l10n::tr(StringId::ProvisionPasskeyUncertain, locale);
        break;
    case EntryVerdict::ForgetPending:
        out.verdict = l10n::tr(StringId::ProvisionNodePending, locale);
        break;
    case EntryVerdict::NodeForgotten:
    case EntryVerdict::NodePartlyForgotten:
    case EntryVerdict::NodeNothingToForget:
    case EntryVerdict::ForgetKept:
        out.verdict = l10n::tr(forget_line(forget_outcome_), locale);
        break;
    case EntryVerdict::Abandoned:
        out.verdict = l10n::tr(StringId::ProvisionAbandoned, locale);
        break;
    }

    // --- the value under the cursor ---------------------------------------
    const int sign = offset_minutes_ < 0 ? -1 : 1;
    const unsigned offset_abs = static_cast<unsigned>(offset_minutes_ * sign);
    int written = 0;
    switch (field_) {
    case EntryField::Day:
        written = std::snprintf(out.value, sizeof out.value, "%u", day_);
        break;
    case EntryField::Month:
        written = std::snprintf(out.value, sizeof out.value, "%02u", month_);
        break;
    case EntryField::Year:
        written = std::snprintf(out.value, sizeof out.value, "%lld",
                                static_cast<long long>(year_));
        break;
    case EntryField::Hour:
        written = std::snprintf(out.value, sizeof out.value, "%02u", hour_);
        break;
    case EntryField::Minute:
        written = std::snprintf(out.value, sizeof out.value, "%02u", minute_);
        break;
    case EntryField::Offset:
        written = std::snprintf(out.value, sizeof out.value, "UTC%c%02u:%02u",
                                sign < 0 ? '-' : '+', offset_abs / 60,
                                offset_abs % 60);
        break;
    case EntryField::Passkey:
        written = std::snprintf(out.value, sizeof out.value, "%u%u%u%u%u%u",
                                digits_[0], digits_[1], digits_[2], digits_[3],
                                digits_[4], digits_[5]);
        break;
    case EntryField::TimeReview:
    case EntryField::Node:
    case EntryField::ForgetConfirm:
    case EntryField::Receipt:
    case EntryField::Exit:
        break;
    }
    (void)fits(written, sizeof out.value, out.value);

    // --- the whole draft, and the instant it means ------------------------
    //
    // On every field of the clock task, not only on the review: a person
    // stepping the month wants to see what the whole thing now says, and the
    // UTC line is the one that tells them whether the offset is the right way
    // round. ISO in English and dotted in Russian, because a mixed audience
    // reading `31.01` as a month is exactly the mistake this screen exists to
    // prevent.
    if (task_ == EntryTask::LocalTime) {
        const bool iso = locale == l10n::Locale::En;
        written = iso
            ? std::snprintf(out.draft, sizeof out.draft,
                            "%04lld-%02u-%02u \xC2\xB7 %02u:%02u \xC2\xB7 UTC%c%02u:%02u",
                            static_cast<long long>(year_), month_, day_, hour_,
                            minute_, sign < 0 ? '-' : '+', offset_abs / 60,
                            offset_abs % 60)
            : std::snprintf(out.draft, sizeof out.draft,
                            "%02u.%02u.%04lld \xC2\xB7 %02u:%02u \xC2\xB7 UTC%c%02u:%02u",
                            day_, month_, static_cast<long long>(year_), hour_,
                            minute_, sign < 0 ? '-' : '+', offset_abs / 60,
                            offset_abs % 60);
        (void)fits(written, sizeof out.draft, out.draft);

        core::WallTime utc;
        core::CivilTime civil;
        if (to_utc(utc) && core::civil_from_wall_time(utc, civil)) {
            written = std::snprintf(out.utc, sizeof out.utc,
                                    "%04lld-%02u-%02u %02u:%02uZ",
                                    static_cast<long long>(civil.year),
                                    civil.month, civil.day, civil.hour,
                                    civil.minute);
            (void)fits(written, sizeof out.utc, out.utc);
        }
    }

    // --- the node's first eight hex digits, the way its own screen shows ---
    if (has_node_) {
        static constexpr char kHex[] = "0123456789abcdef";
        for (unsigned i = 0; i < 4; ++i) {
            out.node[2 * i]     = kHex[node_.public_key[i] >> 4];
            out.node[2 * i + 1] = kHex[node_.public_key[i] & 0x0F];
        }
        out.node[8] = '\0';
    }

    // --- which step of how many -------------------------------------------
    switch (field_) {
    case EntryField::Day:    out.step = 1; out.steps = kTimeSteps; break;
    case EntryField::Month:  out.step = 2; out.steps = kTimeSteps; break;
    case EntryField::Year:   out.step = 3; out.steps = kTimeSteps; break;
    case EntryField::Hour:   out.step = 4; out.steps = kTimeSteps; break;
    case EntryField::Minute: out.step = 5; out.steps = kTimeSteps; break;
    case EntryField::Offset: out.step = 6; out.steps = kTimeSteps; break;
    case EntryField::Passkey:
        out.step  = cursor_ + 1;
        out.steps = kPasskeyDigits;
        break;
    default:
        break;
    }

    // --- the keys ---------------------------------------------------------
    //
    // A key with no label is a key the face does not draw. That is the whole
    // of the rule: nothing here is decided by which field it is except through
    // these six strings, so a face cannot draw `Next` on the step that saves.
    out.leave = l10n::tr(StringId::ProvisionKeyLeave, locale);
    if (out.waiting) {
        // Nothing else does anything while the radio has the request, and a
        // key drawn as live that is not is worse than no key.
        return out;
    }

    const char* const down = l10n::tr(StringId::ProvisionKeyMinus, locale);
    const char* const up   = l10n::tr(StringId::ProvisionKeyPlus, locale);
    const char* const back = l10n::tr(StringId::ProvisionKeyPrevious, locale);
    const char* const next = l10n::tr(StringId::ProvisionKeyNext, locale);

    switch (field_) {
    case EntryField::Day:
        out.minus = down; out.plus = up; out.next = next;
        break;
    case EntryField::Month:
    case EntryField::Year:
    case EntryField::Hour:
    case EntryField::Minute:
    case EntryField::Offset:
        out.minus = down; out.plus = up; out.previous = back; out.next = next;
        break;
    case EntryField::TimeReview:
        out.previous = back;
        out.next     = l10n::tr(StringId::ProvisionKeySave, locale);
        break;
    case EntryField::Node:
        out.next   = next;
        out.forget = l10n::tr(StringId::ProvisionKeyForget, locale);
        break;
    case EntryField::ForgetConfirm:
        out.previous = l10n::tr(StringId::ProvisionKeyKeep, locale);
        out.next     = l10n::tr(StringId::ProvisionKeyForget, locale);
        out.forget   = out.next;
        // `Leave` is Back here, not out: the key beside it is the destructive
        // one, and a screen where the two neighbours do opposite kinds of
        // thing is a screen that loses a node to a mis-tap.
        out.leave    = back;
        break;
    case EntryField::Passkey:
        out.minus = down;
        out.plus  = up;
        if (cursor_ > 0 || has_node_) { out.previous = back; }
        out.next = cursor_ + 1 < kPasskeyDigits
                       ? next
                       : l10n::tr(StringId::ProvisionKeySave, locale);
        break;
    case EntryField::Receipt:
        if (retryable(verdict_)) {
            out.next = l10n::tr(StringId::ProvisionKeyRetry, locale);
            if (receipt_of_ != EntryField::Exit) { out.previous = back; }
        } else if (leads_to_passkey(verdict_)) {
            out.next = next;
        } else {
            out.next = l10n::tr(StringId::ProvisionKeyDone, locale);
        }
        break;
    case EntryField::Exit:
        break;
    }
    return out;
}

}  // namespace attadipa::apps
