#include <cstdio>
#include <cstring>

#include "attadipa/apps/clock.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);          \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

attadipa::core::WallTime wall(int year, unsigned month, unsigned day,
                              unsigned hour, unsigned minute,
                              unsigned second = 0) {
  attadipa::core::WallTime out;
  CHECK(attadipa::core::wall_time_from_civil(
      {year, month, day, 0, hour, minute, second}, out));
  return out;
}

attadipa::apps::ClockText render(attadipa::core::WallTime time,
                                 attadipa::l10n::Locale locale,
                                 bool compact = false) {
  attadipa::apps::ClockState state;
  state.time = {time, 0, 0, attadipa::core::Validity::Valid};
  state.availability = attadipa::core::Availability::Ready;
  state.locale = locale;
  return attadipa::apps::format_clock(state, compact);
}

} // namespace

int main() {
  using namespace attadipa;
  CHECK(std::strcmp(render(wall(2026, 8, 25, 0, 0), l10n::Locale::En).time,
                    "00:00") == 0);
  CHECK(std::strcmp(render(wall(2026, 8, 25, 9, 5), l10n::Locale::En).time,
                    "09:05") == 0);
  CHECK(std::strcmp(render(wall(2026, 8, 25, 23, 59), l10n::Locale::Ru).time,
                    "23:59") == 0);
  CHECK(std::strcmp(render(wall(2026, 8, 25, 12, 34), l10n::Locale::En).date,
                    "TUE, AUG 25") == 0);
  CHECK(std::strcmp(render(wall(2026, 8, 25, 12, 34), l10n::Locale::Ru).date,
                    "ВТ, 25 АВГ") == 0);
  CHECK(std::strcmp(
            render(wall(2026, 8, 25, 12, 34), l10n::Locale::Ru, true).date,
            "25 АВГ") == 0);
  const apps::ClockText detail =
      render(wall(2026, 8, 25, 12, 34, 56), l10n::Locale::En);
  CHECK(std::strcmp(detail.seconds, "56") == 0);
  CHECK(std::strcmp(detail.year, "2026") == 0);
  CHECK(detail.day_progress_minutes == 754 && detail.weekday == 2);

  core::CivilTime civil;
  CHECK(core::civil_from_wall_time(wall(2024, 2, 29, 23, 59, 59), civil));
  CHECK(civil.year == 2024 && civil.month == 2 && civil.day == 29 &&
        civil.weekday == 4);
  CHECK(core::civil_from_wall_time(wall(2025, 1, 1, 0, 0), civil));
  CHECK(civil.year == 2025 && civil.month == 1 && civil.day == 1 &&
        civil.weekday == 3);
  core::WallTime rejected;
  CHECK(!core::wall_time_from_civil({2023, 2, 29, 0, 0, 0, 0}, rejected));

  CHECK(apps::milliseconds_to_next_minute(wall(2026, 8, 25, 12, 34, 0)) ==
        60000);
  CHECK(apps::milliseconds_to_next_minute(wall(2026, 8, 25, 12, 34, 59)) ==
        1000);
  CHECK(apps::clock_manifest().tick_period == core::Millis{1000});

  apps::ClockState stale;
  stale.time = {wall(2026, 8, 25, 12, 34), 0, 0, core::Validity::Stale};
  stale.availability = core::Availability::Ready;
  stale.locale = l10n::Locale::Ru;
  CHECK(std::strcmp(apps::format_clock(stale, true).status,
                    "время могло устареть") == 0);

  apps::ClockState unavailable;
  unavailable.locale = l10n::Locale::Ru;
  unavailable.availability = core::Availability::Unprovisioned;
  const apps::ClockText missing = apps::format_clock(unavailable, false);
  CHECK(!missing.ready && std::strcmp(missing.time, "--:--") == 0);
  CHECK(std::strcmp(missing.status, "не настроено") == 0);

  // A board that came up without touch says so on the face, first: the
  // 240 px face clips the tail, and the dashes already show a clock that is
  // not ready.
  apps::ClockState no_touch;
  no_touch.time = {wall(2026, 8, 25, 12, 34), 0, 0, core::Validity::Valid};
  no_touch.availability = core::Availability::Ready;
  no_touch.touch_absent = true;
  CHECK(std::strcmp(apps::format_clock(no_touch, false).status, "no touch") ==
        0);
  no_touch.availability = core::Availability::Unreachable;
  CHECK(std::strcmp(apps::format_clock(no_touch, false).status,
                    "no touch · unreachable") == 0);
  // The Russian pairs are the long ones, and a cut by bytes would land inside
  // a two-byte character; every pair fits, and the longest is checked whole.
  no_touch.locale = l10n::Locale::Ru;
  no_touch.availability = core::Availability::Unprovisioned;
  CHECK(std::strcmp(apps::format_clock(no_touch, false).status,
                    "нет сенсора · не настроено") == 0);
  no_touch.availability = core::Availability::Unsupported;
  CHECK(std::strcmp(apps::format_clock(no_touch, false).status,
                    "нет сенсора · не поддерживается") == 0);
  char cut[] = "не\xD0";  // "не" and the lead byte of a third letter
  apps::trim_partial_utf8(cut);
  CHECK(std::strcmp(cut, "не") == 0);
  char whole[] = "нет";
  apps::trim_partial_utf8(whole);
  CHECK(std::strcmp(whole, "нет") == 0);
  char ascii[] = "no touch";
  apps::trim_partial_utf8(ascii);
  CHECK(std::strcmp(ascii, "no touch") == 0);

  return failures == 0 ? 0 : 1;
}
