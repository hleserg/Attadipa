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
    error_ = BrightnessError::Apply;
    return false;
  }
  draft_ = static_cast<std::uint8_t>(percent);
  error_ = BrightnessError::None;
  return true;
}

bool BrightnessSettings::adjust(int direction) {
  const int next = static_cast<int>(draft_) + (direction < 0 ? -step_ : step_);
  return preview(std::clamp(next, static_cast<int>(minimum_), 100));
}

bool BrightnessSettings::save() {
  if (!port_.store(draft_)) {
    error_ = BrightnessError::Save;
    return false;
  }
  saved_ = draft_;
  error_ = BrightnessError::None;
  return true;
}

bool BrightnessSettings::cancel() { return preview(saved_); }

} // namespace attadipa::apps
