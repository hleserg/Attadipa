#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include "lvgl.h"

#include "attadipa/apps/clock.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/board_profile.h"

#include "clock_screen.h"
#include "host_time.h"

// The simulator's clock is the host's clock, in the host's timezone.
//
// This is the regression for #553. The live `--clock` screen put
// `std::time(nullptr)` — a UTC instant — where a board puts the *local* half of
// `core::TimeState`, and the formatter below it does calendar arithmetic with no
// timezone in it, so every developer outside UTC was shown the wrong hour and,
// after 23:00 or before 01:00, the wrong date and weekday. Screenshots taken
// that way are review evidence about something the product does not do.
//
// Two things are checked and they are different things. The conversion is
// checked against instants whose local rendering was worked out by hand, in
// three zones including one behind UTC and both sides of a European DST change.
// Then the **live screen** is built through `sim/clock_screen.cpp` and LVGL's
// own timer is allowed to fire, because the defect existed twice — once at
// startup and once in the refresh callback — and a test of the adapter alone
// would have passed against the second copy.
//
// It is not hardware evidence and cannot become any: nothing here has touched a
// panel or an RTC. **NOT EXECUTED — HARDWARE REQUIRED** for anything about a
// physical display or a physical clock.

using namespace attadipa;

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// Instants worked out by hand, and the rendering each one has in each zone.
//
// `date -u -d @1768480496` is 2026-01-15 12:34:56, a Thursday. Europe/Berlin is
// UTC+1 in January and UTC+2 in July, so the summer instant is a different
// offset in the same zone rather than a second winter in disguise, and
// America/New_York is five hours the other way, which is the direction that
// moves the date *backwards*.
constexpr std::int64_t kWinterNoonish = 1768480496; // 2026-01-15 12:34:56 UTC
constexpr std::int64_t kSummerNoonish = 1784464496; // 2026-07-19 12:34:56 UTC
constexpr std::int64_t kLateEvening = 1768519800;   // 2026-01-15 23:30:00 UTC
constexpr std::int64_t kSmallHours = 1768444200;    // 2026-01-15 02:30:00 UTC

// Set the process timezone the way a person sets it before running the
// simulator, and make the C library notice. `tzset()` is the half that is easy
// to forget: without it the library keeps the zone it cached on its first
// conversion, and a test would then check the same zone three times and pass.
void use_timezone(const char *zone) {
  ::setenv("TZ", zone, 1);
  ::tzset();
}

// What the shell had, put back. Test order must not depend on which test ran
// first, and an exported `TZ` this process invented would outlive it inside
// `ctest`'s environment for anything sharing it.
struct TimezoneRestored {
  std::string had;
  bool was_set = false;

  TimezoneRestored() {
    const char *current = std::getenv("TZ");
    was_set = current != nullptr;
    if (was_set) {
      had = current;
    }
  }

  ~TimezoneRestored() {
    if (was_set) {
      ::setenv("TZ", had.c_str(), 1);
    } else {
      ::unsetenv("TZ");
    }
    ::tzset();
  }
};

// The production formatter, over the adapter's answer, at the panel width the
// long date form needs.
apps::ClockText rendered(std::int64_t utc_seconds) {
  apps::ClockState state;
  state.time = {sim::host_local_wall_time(core::WallTime{utc_seconds}), 0, 0,
                core::Validity::Valid};
  state.availability = core::Availability::Ready;
  state.locale = l10n::Locale::En;
  return apps::format_clock(state, false);
}

// `%H:%M` for an instant, through the C library's own formatter rather than
// through the conversion under test. Two different mechanisms have to agree, or
// the expectation is just the implementation written twice.
std::string hhmm(const std::tm *broken) {
  if (broken == nullptr) {
    return {};
  }
  char out[6] = {};
  return std::strftime(out, sizeof(out), "%H:%M", broken) == 5 ? out
                                                               : std::string{};
}

std::string hhmm_local(std::time_t at) { return hhmm(std::localtime(&at)); }
std::string hhmm_utc(std::time_t at) { return hhmm(std::gmtime(&at)); }

// ---------------------------------------------------------------------------
// A panel with no window behind it, which is what lets the real screen and the
// real refresh timer run. One display per screen and none ever deleted, for the
// reason `tests/test_sim_review_keys.cpp:158` — "The keypad and the group go; the display stays." —
// gives: every face here is a file-static object holding pointers into the
// screen it last built, and deleting a display deletes the screen under them.

void flush_cb(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  lv_display_flush_ready(display);
}

std::deque<std::vector<std::uint8_t>> g_buffers;

void open_panel(const platform::BoardProfile &board) {
  const std::uint32_t width = board.display.width_px;
  const std::uint32_t height = board.display.height_px;
  std::vector<std::uint8_t> &buffer =
      g_buffers.emplace_back(static_cast<std::size_t>(width) * height * 2, 0);
  lv_display_t *display = lv_display_create(static_cast<std::int32_t>(width),
                                            static_cast<std::int32_t>(height));
  lv_display_set_default(display);
  lv_display_set_dpi(display, board.display.dpi());
  lv_display_set_buffers(display, buffer.data(), nullptr,
                         static_cast<std::uint32_t>(buffer.size()),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush_cb);
}

// There is no SDL here to advance LVGL's clock, so the test advances it. The
// clock's refresh asks for its own period from `apps::ui_period`, so this runs
// well past a second of LVGL time rather than counting on a number.
void run_frames(int frames) {
  for (int i = 0; i < frames; ++i) {
    lv_tick_inc(50);
    lv_timer_handler();
  }
}

void collect(lv_obj_t *object, std::string &out) {
  if (lv_obj_check_type(object, &lv_label_class)) {
    out += lv_label_get_text(object);
  }
  const auto children =
      static_cast<std::int32_t>(lv_obj_get_child_count(object));
  for (std::int32_t i = 0; i < children; ++i) {
    collect(lv_obj_get_child(object, i), out);
  }
}

// Every label on the screen, in tree order, run together.
//
// The face draws the time as five one-character labels —
// `ui/lvgl/clock_face.cpp:201` — "lv_label_set_text_fmt(digits_[i]" —
// so "13:34" is five siblings in a row and only exists as a string once they
// are concatenated. That is also why this searches rather than compares: the
// question here is what the panel says the time is, not where it says it.
std::string screen_text() {
  lv_obj_update_layout(lv_screen_active());
  std::string out;
  collect(lv_screen_active(), out);
  return out;
}

bool shows(const std::string &text, const std::string &wanted) {
  return !wanted.empty() && text.find(wanted) != std::string::npos;
}

// ---------------------------------------------------------------------------

// The conversion, against instants whose local rendering is written out here.
void the_host_timezone_reaches_the_formatter() {
  use_timezone("UTC");
  CHECK(std::strcmp(rendered(kWinterNoonish).time, "12:34") == 0);
  CHECK(std::strcmp(rendered(kWinterNoonish).date, "THU, JAN 15") == 0);
  CHECK(sim::host_local_wall_time(core::WallTime{kWinterNoonish}) ==
        core::WallTime{kWinterNoonish});

  use_timezone("Europe/Berlin");
  CHECK(std::strcmp(rendered(kWinterNoonish).time, "13:34") == 0);
  CHECK(std::strcmp(rendered(kWinterNoonish).date, "THU, JAN 15") == 0);
  // The seconds are the instant's, not the zone's: an offset is whole minutes
  // everywhere this century, and a conversion that lost the second would show a
  // frozen one.
  CHECK(std::strcmp(rendered(kWinterNoonish).seconds, "56") == 0);

  // Summer in the same zone is a different offset. A fix that read the offset
  // once and kept it would pass the winter case and fail here.
  CHECK(std::strcmp(rendered(kSummerNoonish).time, "14:34") == 0);
  CHECK(std::strcmp(rendered(kSummerNoonish).date, "SUN, JUL 19") == 0);

  // Past midnight: the date, the weekday and `year` all move, which is the half
  // of this defect a reviewer would have read as a stale screenshot.
  CHECK(std::strcmp(rendered(kLateEvening).time, "00:30") == 0);
  CHECK(std::strcmp(rendered(kLateEvening).date, "FRI, JAN 16") == 0);
  CHECK(rendered(kLateEvening).weekday == 5);
  CHECK(std::strcmp(rendered(kLateEvening).year, "2026") == 0);

  // Behind UTC, where the local date is the *previous* day. Adding an offset
  // and adding its magnitude are the same thing until somebody is west of
  // Greenwich.
  use_timezone("America/New_York");
  CHECK(std::strcmp(rendered(kSmallHours).time, "21:30") == 0);
  CHECK(std::strcmp(rendered(kSmallHours).date, "WED, JAN 14") == 0);
  CHECK(rendered(kSmallHours).weekday == 3);
}

// One adapter, and `now` goes through it. The no-argument form is what both
// callers in the simulator use, and it must be the argument form applied to the
// host's current second rather than a second implementation of the conversion.
void now_goes_through_the_same_conversion() {
  use_timezone("Europe/Berlin");
  const auto before = static_cast<std::int64_t>(std::time(nullptr));
  const core::WallTime live = sim::host_local_wall_time();
  const auto after = static_cast<std::int64_t>(std::time(nullptr));
  CHECK(live == sim::host_local_wall_time(core::WallTime{before}) ||
        live == sim::host_local_wall_time(core::WallTime{after}));
  // And it is genuinely the shifted value, not the epoch: Berlin is never UTC.
  CHECK(live.unix_seconds - before == 3600 || live.unix_seconds - after == 3600 ||
        live.unix_seconds - before == 7200 || live.unix_seconds - after == 7200);
}

// The pinned screen, which is what every deterministic screenshot in this
// repository is. `--clock-time` names a UNIX instant and that instant is what
// gets drawn, in whatever zone the reviewer's machine happens to be in.
void a_pinned_instant_ignores_the_host_timezone(
    const platform::BoardProfile &board) {
  apps::ClockState state;
  state.time = {core::WallTime{kWinterNoonish}, 0, 0, core::Validity::Valid};
  state.availability = core::Availability::Ready;
  state.locale = l10n::Locale::En;

  // Both sides run the same number of frames, and both run long past the
  // refresh period: a pinned screen that had kept a live timer would have
  // overwritten itself by now, and a comparison between a short run and a long
  // one would have been about the frame count as much as about the zone.
  use_timezone("UTC");
  open_panel(board);
  sim::build_clock_screen(board, ui::Theme::Day, state, false);
  run_frames(60);
  const std::string in_utc = screen_text();

  use_timezone("Europe/Berlin");
  open_panel(board);
  sim::build_clock_screen(board, ui::Theme::Day, state, false);
  run_frames(60);
  const std::string in_berlin = screen_text();

  CHECK(in_utc == in_berlin);
  CHECK(shows(in_berlin, "12:34"));
  CHECK(!shows(in_berlin, "13:34"));
}

// The live screen, through the refresh callback LVGL calls.
//
// Seeded with an instant that is not now and not local, so a screen that never
// refreshed, or refreshed from the wrong clock, cannot pass by accident.
void the_live_screen_follows_the_host(const platform::BoardProfile &board) {
  use_timezone("Europe/Berlin");

  apps::ClockState state;
  state.time = {core::WallTime{kWinterNoonish}, 0, 0, core::Validity::Valid};
  state.availability = core::Availability::Ready;
  state.locale = l10n::Locale::En;

  open_panel(board);
  const auto before = std::time(nullptr);
  sim::build_clock_screen(board, ui::Theme::Day, state, true);
  run_frames(60);
  const auto after = std::time(nullptr);
  const std::string text = screen_text();

  // A minute may turn while the frames run, so either side of it is right and
  // nothing else is.
  CHECK(shows(text, hhmm_local(before)) || shows(text, hhmm_local(after)));

  // The seed is gone — unless the host really is at that minute, which is a
  // legitimate 60 seconds a day and not a defect.
  if (hhmm_local(before) != "12:34" && hhmm_local(after) != "12:34") {
    CHECK(!shows(text, "12:34"));
  }

  // The counter-check, and the assertion that fails without the fix: Berlin is
  // never UTC, so the UTC rendering of this same second must not be on the
  // panel. Before #553 it was the only thing on it.
  if (hhmm_utc(before) != hhmm_local(before) &&
      hhmm_utc(after) != hhmm_local(after)) {
    CHECK(!shows(text, hhmm_utc(before)));
    CHECK(!shows(text, hhmm_utc(after)));
  }
}

// Every zone named here has to exist, or the checks above compare UTC with
// itself and pass without having tested anything. A host with no zone database
// fails here, by name, rather than quietly agreeing.
//
// Asked through the C library and not through the adapter, deliberately. The
// adapter is the thing on trial: if it were to answer UTC for every instant —
// which is exactly the defect #553 removed — a guard built on it would report a
// missing zone database and send the next reader to install a package that is
// already there.
bool zone_database_present() {
  use_timezone("Europe/Berlin");
  const auto instant = static_cast<std::time_t>(kWinterNoonish);
  const std::string local = hhmm_local(instant);
  if (local == "13:34") {
    return true;
  }
  std::fprintf(stderr,
               "FAIL: with TZ=Europe/Berlin the C library renders "
               "2026-01-15 12:34:56 UTC as %s, not 13:34. This host has no "
               "IANA zone database, so a timezone test here would compare UTC "
               "with itself. Install tzdata.\n",
               local.empty() ? "nothing at all" : local.c_str());
  return false;
}

} // namespace

int main() {
  const TimezoneRestored restore_the_shells_timezone;
  lv_init();
  l10n::set_locale(l10n::Locale::En);

  if (!zone_database_present()) {
    return 1;
  }

  the_host_timezone_reaches_the_formatter();
  now_goes_through_the_same_conversion();

  std::uint8_t count = 0;
  const platform::BoardProfile *profiles = platform::board_profiles(count);
  CHECK(count > 0);
  // Both geometries: the date is the compact form under 300 px and the long one
  // above it, and this defect showed up in the date as well as the hour.
  for (std::uint8_t i = 0; i < count; ++i) {
    a_pinned_instant_ignores_the_host_timezone(profiles[i]);
    the_live_screen_follows_the_host(profiles[i]);
  }

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("sim host time: every check passed\n");
  return 0;
}
