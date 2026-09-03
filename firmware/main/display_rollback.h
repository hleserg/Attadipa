#pragma once

#include <cstdint>

namespace attadipa::firmware {

enum class DisplayQuiescence : std::uint8_t { Proven, Unknown };
enum class DisplayRollback : std::uint8_t { Release, Retain };

// The production rollback decision, kept free of ESP-IDF types so the cases
// that decide ownership of DMA-backed objects run on the host too.
constexpr DisplayRollback display_rollback(bool display_registered,
                                           DisplayQuiescence quiescence,
                                           bool lvgl_locked) {
  if (!display_registered)
    return DisplayRollback::Release;
  return quiescence == DisplayQuiescence::Proven && lvgl_locked
             ? DisplayRollback::Release
             : DisplayRollback::Retain;
}

} // namespace attadipa::firmware
