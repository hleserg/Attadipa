#include "attadipa/apps/clock.h"

#include <cstdio>
#include <cstring>

#include "attadipa/l10n/string_id.h"
#include "attadipa/l10n/tr.h"

namespace attadipa::apps {
namespace {

l10n::StringId availability_id(core::Availability availability) {
  using core::Availability;
  using l10n::StringId;
  switch (availability) {
  case Availability::Unsupported:
    return StringId::AvailabilityUnsupported;
  case Availability::Unprovisioned:
    return StringId::AvailabilityUnprovisioned;
  case Availability::Unreachable:
    return StringId::AvailabilityUnreachable;
  case Availability::Incompatible:
    return StringId::AvailabilityIncompatible;
  case Availability::Failed:
    return StringId::AvailabilityFailed;
  case Availability::Off:
    return StringId::AvailabilityOff;
  case Availability::Ready:
    return StringId::AvailabilityReady;
  }
  return StringId::AvailabilityFailed;
}

const l10n::StringId kMonths[] = {
    l10n::StringId::ClockMonthJan, l10n::StringId::ClockMonthFeb,
    l10n::StringId::ClockMonthMar, l10n::StringId::ClockMonthApr,
    l10n::StringId::ClockMonthMay, l10n::StringId::ClockMonthJun,
    l10n::StringId::ClockMonthJul, l10n::StringId::ClockMonthAug,
    l10n::StringId::ClockMonthSep, l10n::StringId::ClockMonthOct,
    l10n::StringId::ClockMonthNov, l10n::StringId::ClockMonthDec,
};

const l10n::StringId kWeekdays[] = {
    l10n::StringId::ClockWeekdaySun, l10n::StringId::ClockWeekdayMon,
    l10n::StringId::ClockWeekdayTue, l10n::StringId::ClockWeekdayWed,
    l10n::StringId::ClockWeekdayThu, l10n::StringId::ClockWeekdayFri,
    l10n::StringId::ClockWeekdaySat,
};

} // namespace

ClockText format_clock_time(const ClockState &state, bool compact_date);

// A cut inside a multi-byte character leaves a lead byte with no tail; LVGL
// drops the glyph rather than crashing, but a dropped glyph is still wrong.
void trim_partial_utf8(char *s) {
  const std::size_t end = std::strlen(s);
  std::size_t i = end;  // one past the last byte that is not a continuation
  while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0U) == 0x80U) {
    --i;
  }
  if (i == 0) {
    return;
  }
  const std::size_t lead = i - 1;
  const unsigned char first = static_cast<unsigned char>(s[lead]);
  const std::size_t need = (first & 0xE0U) == 0xC0U   ? 2
                           : (first & 0xF0U) == 0xE0U ? 3
                           : (first & 0xF8U) == 0xF0U ? 4
                                                      : 1;
  if (end - lead < need) {
    s[lead] = '\0';
  }
}

// The missing input first, then the clock's own reason: the face clips at
// 240 px (`LV_LABEL_LONG_CLIP`), the dashes already say the clock is not
// ready, and "no touch" is the one fact nothing else on the glass shows.
// `status` holds every pair the catalogue has today; the trim is for the day
// it does not. `%.*s` is what keeps GCC's format-truncation check quiet about
// a cut that is the intent.
void prepend_touch_absent(const ClockState &state, ClockText &text) {
  if (!state.touch_absent) {
    return;
  }
  char reason[sizeof(text.status)];
  std::memcpy(reason, text.status, sizeof(reason));
  const char *no_touch = l10n::tr(l10n::StringId::ClockNoTouch, state.locale);
  const int room = static_cast<int>(sizeof(text.status)) - 1;
  const int written =
      reason[0] == '\0'
          ? std::snprintf(text.status, sizeof(text.status), "%.*s", room,
                          no_touch)
          : std::snprintf(text.status, sizeof(text.status), "%.*s · %.*s", room,
                          no_touch, room, reason);
  if (written >= room) {
    trim_partial_utf8(text.status);
  }
}

ClockText format_clock(const ClockState &state, bool compact_date) {
  ClockText text = format_clock_time(state, compact_date);
  prepend_touch_absent(state, text);
  return text;
}

ClockText format_clock_time(const ClockState &state, bool compact_date) {
  ClockText text;
  text.mode = state.mode;
  if (state.availability != core::Availability::Ready) {
    std::snprintf(text.status, sizeof(text.status), "%s",
                  l10n::tr(availability_id(state.availability), state.locale));
    return text;
  }
  if (state.time.validity != core::Validity::Valid &&
      state.time.validity != core::Validity::Stale) {
    std::snprintf(text.status, sizeof(text.status), "%s",
                  l10n::tr(l10n::StringId::ClockTimeInvalid, state.locale));
    return text;
  }

  core::CivilTime civil;
  if (!core::civil_from_wall_time(state.time.value, civil)) {
    std::snprintf(text.status, sizeof(text.status), "%s",
                  l10n::tr(l10n::StringId::ClockTimeInvalid, state.locale));
    return text;
  }
  std::snprintf(text.time, sizeof(text.time), "%02u:%02u", civil.hour,
                civil.minute);
  std::snprintf(text.seconds, sizeof(text.seconds), "%02u", civil.second);
  std::snprintf(text.year, sizeof(text.year), "%04lld",
                static_cast<long long>(civil.year));
  text.day_progress_minutes = civil.hour * 60 + civil.minute;
  text.weekday = civil.weekday;
  const char *month = l10n::tr(kMonths[civil.month - 1], state.locale);
  if (compact_date) {
    if (state.locale == l10n::Locale::Ru) {
      std::snprintf(text.date, sizeof(text.date), "%u %s", civil.day, month);
    } else {
      std::snprintf(text.date, sizeof(text.date), "%s %u", month, civil.day);
    }
  } else {
    const char *weekday = l10n::tr(kWeekdays[civil.weekday], state.locale);
    if (state.locale == l10n::Locale::Ru) {
      std::snprintf(text.date, sizeof(text.date), "%s, %u %s", weekday,
                    civil.day, month);
    } else {
      std::snprintf(text.date, sizeof(text.date), "%s, %s %u", weekday, month,
                    civil.day);
    }
  }
  if (state.time.validity == core::Validity::Stale) {
    std::snprintf(text.status, sizeof(text.status), "%s",
                  l10n::tr(l10n::StringId::ClockTimeStale, state.locale));
  }
  text.ready = true;
  return text;
}

std::uint32_t milliseconds_to_next_minute(core::WallTime time) {
  std::int64_t second = time.unix_seconds % 60;
  if (second < 0) {
    second += 60;
  }
  return static_cast<std::uint32_t>((60 - second) * 1000);
}

const AppManifest &clock_manifest() {
  static constexpr core::Capability required[] = {core::Capability::Time};
  static const AppManifest manifest{"clock", required, 1,
                                    nullptr, 0,        core::Millis{1000}};
  return manifest;
}

} // namespace attadipa::apps
