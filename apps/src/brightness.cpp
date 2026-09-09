#include "attadipa/apps/brightness.h"

#include <algorithm>

namespace attadipa::apps {

void BrightnessSettings::load() {
  std::uint8_t value = fallback_;
  const auto result = port_.load(value);
  const bool corrupt = result == BrightnessRead::Present && !valid(value);
  saved_ = result == BrightnessRead::Present && !corrupt ? value : fallback_;
  draft_ = saved_;
  error_ = result == BrightnessRead::Failed || corrupt
               ? BrightnessError::Load : BrightnessError::None;
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
  const bool applied = preview(saved_);
  // Restore visible brightness, but keep the restart notice on screen until
  // boot has recovered storage and read which request actually survived.
  return applied && error_ != BrightnessError::Uncertain;
}

void BrightnessSettings::restart() {
  if (error_ == BrightnessError::Uncertain) port_.restart();
}

} // namespace attadipa::apps
