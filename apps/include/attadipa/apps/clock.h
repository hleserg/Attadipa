#pragma once

#include <cstdint>

#include "attadipa/apps/app_manifest.h"
#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"
#include "attadipa/l10n/locale.h"

namespace attadipa::apps {

enum class ClockMode : std::uint8_t { Adult, Child };

struct ClockState {
  core::Timed<core::WallTime> time{};
  core::Availability availability = core::Availability::Unprovisioned;
  l10n::Locale locale = l10n::Locale::En;
  ClockMode mode = ClockMode::Adult;
  // The face is also the only way in: a long press opens the entry screen.
  // A board that came up without its touch input says so here, and the face
  // shows it, or a correct clock that ignores every finger reads as frozen.
  bool touch_absent = false;
};

struct ClockText {
  char time[6] = "--:--";
  char seconds[3] = "--";
  char year[5] = {};
  char date[40] = {};
  char status[72] = {};  // the longest pair the catalogue has is 64 bytes
  unsigned day_progress_minutes = 0;
  unsigned weekday = 0;
  ClockMode mode = ClockMode::Adult;
  bool ready = false;
};

ClockText format_clock(const ClockState &state, bool compact_date);
// Cut a trailing incomplete UTF-8 sequence off a NUL-terminated string in
// place, so a status truncated by bytes never ends on half a character.
void trim_partial_utf8(char *s);
std::uint32_t milliseconds_to_next_minute(core::WallTime time);
const AppManifest &clock_manifest();

} // namespace attadipa::apps
