#pragma once

#include <cstdint>

#include "attadipa/apps/app_manifest.h"
#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"
#include "attadipa/l10n/locale.h"

namespace attadipa::apps {

enum class ClockMode : std::uint8_t { Adult, Child };

struct CivilTime {
  std::int64_t year = 1970;
  unsigned month = 1;
  unsigned day = 1;
  unsigned weekday = 4; // 0 = Sunday
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
};

struct ClockState {
  core::Timed<core::WallTime> time{};
  core::Availability availability = core::Availability::Unprovisioned;
  l10n::Locale locale = l10n::Locale::En;
  ClockMode mode = ClockMode::Adult;
};

struct ClockText {
  char time[6] = "--:--";
  char date[40] = {};
  char status[32] = {};
  ClockMode mode = ClockMode::Adult;
  bool ready = false;
};

bool civil_from_wall_time(core::WallTime time, CivilTime &out);
bool wall_time_from_civil(const CivilTime &civil, core::WallTime &out);
ClockText format_clock(const ClockState &state, bool compact_date);
std::uint32_t milliseconds_to_next_minute(core::WallTime time);
const AppManifest &clock_manifest();

} // namespace attadipa::apps
