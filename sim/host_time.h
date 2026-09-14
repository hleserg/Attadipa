#pragma once

#include "attadipa/core/clock.h"

namespace attadipa::sim {

// The host's clock, in the contract the Clock application is handed.
//
// A board does not give `apps::ClockState.time.value` a UTC instant. It gives
// the local half of `core::TimeState`, which is the UTC one with the effective
// offset already added — `core/src/time_service.cpp:208` —
// "add_offset(result.utc.value, timezone_.minutes_east_of_utc)" — and the
// composition passes exactly that and not the other one:
// `firmware/main/waveshare_board.cpp:487` — "clock.time = time.local;".
// Downstream, the formatter renders whatever it is handed with calendar
// arithmetic that has no timezone in it at all
// (`core/include/attadipa/core/clock.h:134` — "no locale and no timezone: this
// is UTC"), so the value handed in IS the time on the face.
//
// The simulator used to hand it `std::time(nullptr)`, which is UTC whatever the
// host is set to, so `--clock` showed a developer in Berlin the hour in
// Reykjavík, and a screenshot taken there after 23:00 carried yesterday's date
// and weekday (#553). A review that approves such a screen has approved
// something the product does not do.
//
// This is the one place in the simulator allowed to know what timezone the host
// is in. Nothing above it learns that a timezone was involved, which is the
// point: the application layer sees the same presentation-ready value here that
// it sees on the board, and `apps::format_clock` stays free of process-global
// state it could not have on a device.
core::WallTime host_local_wall_time(core::WallTime utc);

// The same conversion, applied to now.
//
// Live mode calls this on every refresh and not once at startup, so a window
// left open across a DST change follows the host instead of holding the offset
// that happened to be in force when it opened. It costs one `localtime` call a
// second, which is what the refresh already spends redrawing a minute that did
// not change.
core::WallTime host_local_wall_time();

} // namespace attadipa::sim
