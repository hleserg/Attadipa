#include "host_time.h"

#include <cstdint>
#include <cstdio>
#include <ctime>

namespace attadipa::sim {
namespace {

// Said once, however long the window stays open.
//
// The refresh timer asks for the local time every second, so a host that cannot
// answer would otherwise write the same line a thousand times a session. It is
// still said, because the fallback below is the UTC instant — the exact value
// #553 removed — and a screen that quietly went back to it looks precisely like
// a screen that is right.
bool g_host_time_unavailable_reported = false;

} // namespace

core::WallTime host_local_wall_time(core::WallTime utc) {
  const auto instant = static_cast<std::time_t>(utc.unix_seconds);
  // `std::localtime` rather than `localtime_r`, for two reasons and not one.
  // The simulator has no second thread — nothing under `sim/` or `debug/`
  // creates one and the debug channel is polled from the same loop as LVGL — so
  // the static buffer this returns has a single reader. And `localtime_r` is
  // POSIX, not C++: under `-std=c++17` glibc hides it behind a feature macro,
  // and this file compiles clean under the `-Werror` set with no such macro
  // defined. If a thread ever appears here, this is the call that has to
  // change.
  const std::tm *broken = std::localtime(&instant);
  if (broken != nullptr) {
    // A leap second is the one field value a correct host can produce that
    // `wall_time_from_civil` refuses: a `right/` timezone counts 23:59:60. Left
    // alone it would send the whole face back to UTC for that second, an hour
    // wrong to report a second that POSIX does not count either. Every other
    // clock in this build shows :59 twice.
    const unsigned second =
        broken->tm_sec > 59 ? 59U : static_cast<unsigned>(broken->tm_sec);
    const core::CivilTime civil{
        static_cast<std::int64_t>(broken->tm_year) + 1900,
        static_cast<unsigned>(broken->tm_mon) + 1U,
        static_cast<unsigned>(broken->tm_mday),
        // Unused by the conversion — `civil_from_wall_time` recomputes the
        // weekday downstream from the seconds — and filled anyway, so this
        // value is not a `CivilTime` that disagrees with itself.
        static_cast<unsigned>(broken->tm_wday),
        static_cast<unsigned>(broken->tm_hour),
        static_cast<unsigned>(broken->tm_min),
        second,
    };
    core::WallTime local{};
    if (core::wall_time_from_civil(civil, local)) {
      return local;
    }
  }
  if (!g_host_time_unavailable_reported) {
    g_host_time_unavailable_reported = true;
    std::fprintf(stderr,
                 "sim: the host cannot say what its local time is at %lld, so "
                 "the clock is showing UTC. Check TZ and the system clock.\n",
                 static_cast<long long>(utc.unix_seconds));
  }
  return utc;
}

core::WallTime host_local_wall_time() {
  return host_local_wall_time(
      core::WallTime{static_cast<std::int64_t>(std::time(nullptr))});
}

} // namespace attadipa::sim
