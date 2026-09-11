#include "attadipa/apps/brightness.h"

#include <algorithm>

namespace attadipa::apps {

const char *describe(BrightnessOrigin origin) {
  switch (origin) {
  case BrightnessOrigin::Restored: return "restored";
  case BrightnessOrigin::Default: return "default";
  case BrightnessOrigin::Unreadable: return "unreadable";
  }
  return "unreadable";
}

void BrightnessSettings::load() {
  std::uint8_t value = fallback_;
  const auto result = port_.load(value);
  const bool corrupt = result == BrightnessRead::Present && !valid(value);
  const bool restored = result == BrightnessRead::Present && !corrupt;
  saved_ = restored ? value : fallback_;
  draft_ = saved_;
  error_ = result == BrightnessRead::Failed || corrupt
               ? BrightnessError::Load : BrightnessError::None;
  // Three outcomes, not two: a healthy store with no key yet is not a failure
  // and is not a restore either, and the boot line has to be able to say so.
  origin_ = restored ? BrightnessOrigin::Restored
            : result == BrightnessRead::Missing ? BrightnessOrigin::Default
                                                : BrightnessOrigin::Unreadable;
}

bool BrightnessSettings::preview(int percent) {
  // Validate before narrowing: e.g. 261 must never become a valid 5% request.
  if (!valid(percent) || !port_.apply(static_cast<std::uint8_t>(percent))) {
    if (error_ != BrightnessError::Uncertain) error_ = BrightnessError::Apply;
    return false;
  }
  draft_ = static_cast<std::uint8_t>(percent);
  if (error_ != BrightnessError::Uncertain) error_ = BrightnessError::None;
  return true;
}

bool BrightnessSettings::adjust(int direction) {
  const int next = static_cast<int>(draft_) + (direction < 0 ? -step_ : step_);
  return preview(std::clamp(next, static_cast<int>(minimum_), 100));
}

bool BrightnessSettings::save() {
  // Flash may have changed before its write returned an error. Do not retry
  // into that state or promise that Cancel can undo its durable outcome.
  if (error_ == BrightnessError::Uncertain) return false;
  const auto result = port_.store(draft_);
  if (result != BrightnessWrite::Saved) {
    error_ = result == BrightnessWrite::Uncertain ? BrightnessError::Uncertain
                                                 : BrightnessError::Save;
    return false;
  }
  saved_ = draft_;
  error_ = BrightnessError::None;
  return true;
}

bool BrightnessSettings::cancel() {
  // Restores the last committed request and reports whether the panel took it.
  // An `Uncertain` write is NOT a reason to answer no: what cannot be promised
  // is that the durable value was undone, and `error_` stays `Uncertain` to say
  // so on every later look at this screen. Folding that into the return value
  // made the one caller keep its page, and the brightness page has no Back --
  // so an uncertain write left rebooting as the only way off it.
  return preview(saved_);
}

void BrightnessSettings::restart() {
  if (error_ == BrightnessError::Uncertain) port_.restart();
}

} // namespace attadipa::apps
