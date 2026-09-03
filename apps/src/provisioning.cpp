#include "attadipa/apps/provisioning.h"

#include "attadipa/apps/clock.h"
#include "attadipa/l10n/string_id.h"
#include "attadipa/l10n/tr.h"

namespace attadipa::apps {
namespace {

unsigned number(const char* digits, unsigned from, unsigned count) {
  unsigned value = 0;
  for (unsigned i = 0; i < count; ++i) {
    value = value * 10 + static_cast<unsigned>(digits[from + i] - '0');
  }
  return value;
}

// The mask each field is typed into; `_` is a digit still to come.
const char* mask(EntryField field) {
  switch (field) {
  case EntryField::Date:    return "____-__-__";
  case EntryField::Time:    return "__:__";
  case EntryField::Offset:  return "__:__";
  case EntryField::Node:    return "";
  case EntryField::Passkey: return "______";
  case EntryField::Done:    return "";
  }
  return "";
}

}  // namespace

ProvisioningEntry::ProvisioningEntry(core::Provisioner& sink) : sink_(sink) {}

unsigned ProvisioningEntry::capacity() const {
  switch (field_) {
  case EntryField::Date:    return 8;
  case EntryField::Time:    return 4;
  case EntryField::Offset:  return 4;
  case EntryField::Node:    return 0;
  case EntryField::Passkey: return 6;
  case EntryField::Done:    return 0;
  }
  return 0;
}

void ProvisioningEntry::press(EntryKey key) {
  if (field_ == EntryField::Done) {
    return;
  }
  // While the radio has a passkey of ours, the only key that means anything is
  // the way out. A second OK here would post a second configure over an answer
  // nobody has read, and the digit keys would edit a value that has already
  // gone; both would also clear the line telling the person to wait.
  if (awaiting_ || awaiting_forget_) {
    if (key == EntryKey::Cancel) {
      // Neither "skipped" nor "set up" is true: the passkey, or the forget,
      // is with the radio and this screen is no longer here to hear how it
      // ended.
      verdict_ = EntryVerdict::Pending;
      pending_is_forget_ = awaiting_forget_;
      awaiting_ = false;
      awaiting_forget_ = false;
      field_ = EntryField::Done;
      count_ = 0;
    }
    return;
  }
  verdict_ = EntryVerdict::None;
  if (key >= EntryKey::Digit0 && key <= EntryKey::Digit9) {
    if (count_ < capacity()) {
      digits_[count_++] = static_cast<char>(
          '0' + (static_cast<unsigned>(key) -
                 static_cast<unsigned>(EntryKey::Digit0)));
    }
    return;
  }
  switch (key) {
  case EntryKey::Sign:
    if (field_ == EntryField::Offset) {
      offset_west_ = !offset_west_;
    }
    return;
  case EntryKey::Backspace:
    if (field_ == EntryField::Node) {
      forget();
      return;
    }
    if (count_ > 0) {
      --count_;
    }
    return;
  case EntryKey::Ok:
    accept();
    return;
  case EntryKey::Cancel:
    // Past the offset the clock is already the board's; leaving then is the
    // empty-passkey skip by another key, and says so -- unless a passkey did
    // reach the radio and came back badly, because "no passkey; the clock is
    // set" would then be describing a watch that may be armed for this boot
    // and unprovisioned at the next. Leaving after the board failed a write is
    // not "nothing changed" either: the RTC may hold the typed time with no
    // rollback behind it, so the last answer stands.
    if (field_ == EntryField::Passkey || field_ == EntryField::Node) {
      verdict_ = passkey_failed_ ? EntryVerdict::Failed : EntryVerdict::Skipped;
    } else {
      verdict_ = board_failed_ ? EntryVerdict::Failed : EntryVerdict::Cancelled;
    }
    field_ = EntryField::Done;
    count_ = 0;
    return;
  default:
    return;
  }
}

// What OK does with a full field. A field that is not full, or does not name
// a real moment, is refused and stays on the screen with its digits, so a slip
// costs one key and not the whole entry.
void ProvisioningEntry::accept() {
  switch (field_) {
  case EntryField::Date: {
    if (count_ != 8) {
      verdict_ = EntryVerdict::Rejected;
      return;
    }
    const unsigned year = number(digits_, 0, 4);
    const unsigned month = number(digits_, 4, 2);
    const unsigned day = number(digits_, 6, 2);
    core::WallTime probe;
    if (year < 2000 || year > 2099 ||
        !wall_time_from_civil({year, month, day, 0, 0, 0, 0}, probe)) {
      verdict_ = EntryVerdict::Rejected;
      return;
    }
    year_ = year;
    month_ = month;
    day_ = day;
    break;
  }
  case EntryField::Time: {
    const unsigned hour = number(digits_, 0, 2);
    const unsigned minute = number(digits_, 2, 2);
    if (count_ != 4 || hour > 23 || minute > 59) {
      verdict_ = EntryVerdict::Rejected;
      return;
    }
    hour_ = hour;
    minute_ = minute;
    break;
  }
  case EntryField::Offset: {
    const unsigned hours = number(digits_, 0, 2);
    const unsigned minutes = number(digits_, 2, 2);
    // UTC-12 to UTC+14 is every zone there is.
    if (count_ != 4 || minutes > 59 || hours * 60 + minutes > 14 * 60 ||
        (offset_west_ && hours * 60 + minutes > 12 * 60)) {
      verdict_ = EntryVerdict::Rejected;
      return;
    }
    core::WallTime utc;
    if (!wall_time_from_civil({static_cast<std::int64_t>(year_), month_, day_,
                               0, hour_, minute_, 0},
                              utc)) {
      verdict_ = EntryVerdict::Rejected;
      return;
    }
    const int signed_minutes =
        static_cast<int>(hours * 60 + minutes) * (offset_west_ ? -1 : 1);
    switch (sink_.set_wall_clock(
        {utc.unix_seconds, static_cast<std::int16_t>(signed_minutes)})) {
    case core::ProvisionOutcome::Accepted:
      break;
    case core::ProvisionOutcome::Rejected:
      verdict_ = EntryVerdict::Rejected;
      return;
    case core::ProvisionOutcome::Pending:
      // The clock is written by the task that asks it -- `set_wall_clock` is
      // terminal by contract, and there is no second half to wait for. A board
      // that answers this has an answer nobody will ever collect, so it is the
      // failure it already is rather than a screen that waits for ever.
    case core::ProvisionOutcome::Failed:
      verdict_ = EntryVerdict::Failed;
      board_failed_ = true;
      return;
    }
    break;
  }
  case EntryField::Node:
    // OK keeps the node. Forgetting it is the erase key's, above.
    break;
  case EntryField::Passkey: {
    if (count_ == 0) {
      // The empty OK is the other door out of this field, and it asks the
      // same question Cancel does: a passkey that reached the radio and came
      // back badly may be armed for this boot, and "no passkey; the clock is
      // set" would describe the opposite watch (#416, round 3).
      verdict_ = passkey_failed_ ? EntryVerdict::Failed : EntryVerdict::Skipped;
      field_ = EntryField::Done;
      return;
    }
    if (count_ != 6) {
      verdict_ = EntryVerdict::Rejected;
      return;
    }
    switch (sink_.set_mesh_passkey(number(digits_, 0, 6))) {
    case core::ProvisionOutcome::Pending:
      // The radio has it and has not armed it yet. The digits stay on the
      // screen, the field does not advance, and poll() is the only thing that
      // can move either -- which is the whole of #416.
      verdict_ = EntryVerdict::Pending;
      awaiting_ = true;
      return;
    case core::ProvisionOutcome::Accepted:
      break;
    case core::ProvisionOutcome::Rejected:
      verdict_ = EntryVerdict::Rejected;
      return;
    case core::ProvisionOutcome::Failed:
      // Refused before the radio saw it -- no queue for it, no storage to keep
      // it in, or an earlier passkey still with the radio. Only the last can
      // still arm something, and the board says which: its outcome is Pending
      // while a request is in flight and Failed with nothing outstanding, so
      // "no passkey; the clock is set" is honest only in the Failed case. A
      // screen that was cancelled with a passkey in flight is gone, and this
      // one is the only place left that can be told how that ended (#416,
      // round 1). Latched, not assigned: the board's answer is about the
      // radio now, and a refusal after an earlier failure cannot forgive it
      // (#416, round 4).
      passkey_failed_ =
          passkey_failed_ ||
          sink_.mesh_passkey_outcome() != core::ProvisionOutcome::Failed;
      verdict_ = EntryVerdict::Failed;
      return;
    }
    break;
  }
  case EntryField::Done:
    return;
  }
  verdict_ = EntryVerdict::Accepted;
  advance();
}

// The next field -- and past the node field on a watch that has no node,
// which is every watch until its first adoption and every watch after a
// forget.
void ProvisioningEntry::advance() {
  field_ = static_cast<EntryField>(static_cast<std::uint8_t>(field_) + 1);
  count_ = 0;
  if (field_ == EntryField::Node && !sink_.mesh_node(node_)) {
    field_ = EntryField::Passkey;
  }
}

// The erase key on the node field. What it asks for is the whole of #411's
// recovery -- the stale bond and the pin, together -- and it finishes on the
// radio's task, so like the passkey it ends in `Pending` and poll() carries
// it the rest of the way.
void ProvisioningEntry::forget() {
  switch (sink_.forget_mesh_node()) {
  case core::ProvisionOutcome::Pending:
    verdict_ = EntryVerdict::Pending;
    awaiting_forget_ = true;
    return;
  case core::ProvisionOutcome::Rejected:
    // Nothing to forget: the node went between this field being shown and
    // the key. The field has nothing left to show, so it is over.
    forget_outcome_ = core::MeshForgetOutcome::Nothing;
    verdict_ = EntryVerdict::Forgotten;
    advance();
    return;
  case core::ProvisionOutcome::Accepted:
    // Not a value the contract allows -- the clears run elsewhere -- but a
    // board that says it finished is not told it failed.
    forget_outcome_ = core::MeshForgetOutcome::Forgotten;
    node_forgotten_ = true;
    verdict_ = EntryVerdict::Forgotten;
    advance();
    return;
  case core::ProvisionOutcome::Failed:
    // The request never reached the worker. The node stays on screen and the
    // common failure line asks for a retry without claiming transport state.
    verdict_ = EntryVerdict::Failed;
    return;
  }
}

// The other half of the passkey, arriving on the tick rather than on a key.
// Terminal either way: the board consumes its answer, so this asks once and
// then stops asking.
bool ProvisioningEntry::poll() {
  if (awaiting_forget_) {
    const core::MeshForgetOutcome outcome = sink_.mesh_forget_outcome();
    if (outcome == core::MeshForgetOutcome::Pending) {
      return false;
    }
    awaiting_forget_ = false;
    forget_outcome_ = outcome;
    if (outcome == core::MeshForgetOutcome::BondKept ||
        outcome == core::MeshForgetOutcome::ReplayInhibited) {
      // Trust stayed, either because the store refused or because its durable
      // rollback did. The node stays on the screen so the key can retry it;
      // the hint below distinguishes the reboot-inhibited case.
      verdict_ = EntryVerdict::Failed;
      return true;
    }
    // Forgotten in some measure -- or there was nothing, which leaves the
    // watch exactly as forgotten as it already was. Either way the field has
    // nothing left to show, and the passkey is what comes next.
    node_forgotten_ = outcome != core::MeshForgetOutcome::Nothing;
    verdict_ = EntryVerdict::Forgotten;
    advance();
    return true;
  }
  if (!awaiting_) {
    return false;
  }
  switch (sink_.mesh_passkey_outcome()) {
  case core::ProvisionOutcome::Pending:
    return false;
  case core::ProvisionOutcome::Accepted:
    // Armed, and on flash where it had to be. The one route to Done.
    awaiting_ = false;
    verdict_ = EntryVerdict::Accepted;
    field_ = EntryField::Done;
    count_ = 0;
    return true;
  case core::ProvisionOutcome::Rejected:
    // A refusal this late is not a statement about the digits -- they were
    // taken. It is the request ending badly, and it ends the same way.
  case core::ProvisionOutcome::Failed:
    // The stack refused the passkey, or flash did. The digits stay where they
    // are, so OK asks again; leaving instead no longer claims the passkey was
    // skipped, because it may be armed for this boot and gone at the next.
    awaiting_ = false;
    passkey_failed_ = true;
    verdict_ = EntryVerdict::Failed;
    return true;
  }
  return false;
}

EntryText ProvisioningEntry::text(l10n::Locale locale) const {
  using l10n::StringId;
  EntryText out;
  out.ok = l10n::tr(StringId::ProvisionKeyOk, locale);
  out.backspace = l10n::tr(StringId::ProvisionKeyErase, locale);
  out.cancel = l10n::tr(StringId::ProvisionKeyCancel, locale);
  out.sign_key = field_ == EntryField::Offset;
  out.done = field_ == EntryField::Done;

  StringId title = StringId::ProvisionTitleDone;
  StringId hint = StringId::ProvisionDone;
  switch (field_) {
  case EntryField::Date:
    title = StringId::ProvisionTitleDate;
    hint = StringId::ProvisionHintDate;
    break;
  case EntryField::Time:
    title = StringId::ProvisionTitleTime;
    hint = StringId::ProvisionHintTime;
    break;
  case EntryField::Offset:
    title = StringId::ProvisionTitleOffset;
    hint = StringId::ProvisionHintOffset;
    break;
  case EntryField::Node:
    title = StringId::ProvisionTitleNode;
    hint = StringId::ProvisionHintNode;
    out.backspace = l10n::tr(StringId::ProvisionKeyForget, locale);
    break;
  case EntryField::Passkey:
    title = StringId::ProvisionTitlePasskey;
    // After a forget the digits wanted are the ones the node shows *now*:
    // a reset node rolls its passkey (report §5.3), and the old six would
    // fail with nothing on the screen to say why.
    hint = node_forgotten_ ? StringId::ProvisionNodeForgotten
                           : StringId::ProvisionHintPasskey;
    break;
  case EntryField::Done:
    break;
  }
  switch (verdict_) {
  case EntryVerdict::None:
    break;
  case EntryVerdict::Accepted:
    // Moving on is the answer; the new field's hint is what a person needs
    // next, not a word about the last one. Done says its own line.
    break;
  case EntryVerdict::Rejected:
    hint = StringId::ProvisionRejected;
    break;
  case EntryVerdict::Failed:
    hint = field_ != EntryField::Node
               ? StringId::ProvisionFailed
               : forget_outcome_ == core::MeshForgetOutcome::ReplayInhibited
                     ? StringId::ProvisionNodeReplayInhibited
                     : StringId::ProvisionNodeKept;
    break;
  case EntryVerdict::Skipped:
    // "the clock is set" is true either way; "no passkey" after a forget
    // is a watch that stays silent, and the line says so.
    hint = node_forgotten_ ? StringId::ProvisionForgottenSkipped
                           : StringId::ProvisionSkipped;
    break;
  case EntryVerdict::Forgotten:
    switch (forget_outcome_) {
    case core::MeshForgetOutcome::PinOnFlash:
      hint = StringId::ProvisionNodeForgottenRam;
      break;
    case core::MeshForgetOutcome::Nothing:
      hint = StringId::ProvisionNodeNothing;
      break;
    default:
      hint = StringId::ProvisionNodeForgotten;
      break;
    }
    break;
  case EntryVerdict::Cancelled:
    hint = StringId::ProvisionCancelled;
    break;
  case EntryVerdict::Pending:
    // On the passkey field this is "wait"; on a screen that was left during
    // the wait it is "this is where it got to". One sentence for both,
    // because it is one fact -- and a second sentence for the forget, which
    // is the other thing the radio can still be holding.
    hint = awaiting_forget_ || pending_is_forget_
               ? StringId::ProvisionNodePending
               : StringId::ProvisionPending;
    break;
  }
  out.title = l10n::tr(title, locale);
  out.hint = l10n::tr(hint, locale);

  if (field_ == EntryField::Node) {
    // The first eight hex digits of the node's key, the way the mesh screen
    // and the node's own screen show it.
    static constexpr char kHex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 4; ++i) {
      out.value[2 * i] = kHex[node_.public_key[i] >> 4];
      out.value[2 * i + 1] = kHex[node_.public_key[i] & 0x0F];
    }
    out.value[8] = '\0';
    return out;
  }

  // The mask with the typed digits laid over its blanks.
  const char* shape = mask(field_);
  unsigned typed = 0;
  unsigned n = 0;
  if (field_ == EntryField::Offset) {
    out.value[n++] = offset_west_ ? '-' : '+';
  }
  for (unsigned i = 0; shape[i] != '\0' && n + 1 < sizeof(out.value); ++i) {
    if (shape[i] == '_' && typed < count_) {
      out.value[n++] = digits_[typed++];
    } else {
      out.value[n++] = shape[i];
    }
  }
  out.value[n] = '\0';
  return out;
}

}  // namespace attadipa::apps
