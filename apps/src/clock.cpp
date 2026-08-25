#include "attadipa/apps/clock.h"

#include <cstdio>

#include "attadipa/l10n/string_id.h"
#include "attadipa/l10n/tr.h"

namespace attadipa::apps {
namespace {

constexpr bool leap(std::int64_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr unsigned days_in_month(std::int64_t year, unsigned month) {
  constexpr unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 0 || month > 12
             ? 0
             : (month == 2 && leap(year) ? 29 : days[month - 1]);
}

constexpr std::int64_t days_from_civil(std::int64_t year, unsigned month,
                                       unsigned day) {
  year -= month <= 2;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

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

bool civil_from_wall_time(core::WallTime time, CivilTime &out) {
  std::int64_t days = time.unix_seconds / 86400;
  std::int64_t seconds = time.unix_seconds % 86400;
  if (seconds < 0) {
    seconds += 86400;
    --days;
  }

  const std::int64_t shifted = days + 719468;
  const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(shifted - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  std::int64_t year = static_cast<std::int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned day = doy - (153 * mp + 2) / 5 + 1;
  const unsigned month = mp < 10 ? mp + 3 : mp - 9;
  year += month <= 2;

  std::int64_t weekday = (days + 4) % 7;
  if (weekday < 0) {
    weekday += 7;
  }
  out = {year,
         month,
         day,
         static_cast<unsigned>(weekday),
         static_cast<unsigned>(seconds / 3600),
         static_cast<unsigned>((seconds / 60) % 60),
         static_cast<unsigned>(seconds % 60)};
  return year >= 1 && year <= 9999;
}

bool wall_time_from_civil(const CivilTime &civil, core::WallTime &out) {
  if (civil.year < 1 || civil.year > 9999 || civil.day == 0 ||
      civil.day > days_in_month(civil.year, civil.month) || civil.hour > 23 ||
      civil.minute > 59 || civil.second > 59) {
    return false;
  }
  out.unix_seconds =
      days_from_civil(civil.year, civil.month, civil.day) * 86400 +
      static_cast<std::int64_t>(civil.hour * 3600 + civil.minute * 60 +
                                civil.second);
  return true;
}

ClockText format_clock(const ClockState &state, bool compact_date) {
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

  CivilTime civil;
  if (!civil_from_wall_time(state.time.value, civil)) {
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
