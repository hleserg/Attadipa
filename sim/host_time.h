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
// `firmware/main/waveshare_board.cpp:461` — "clock.time = time.local;".
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
//
// "THE SAME VALUE IT SEES ON THE BOARD" IS TRUE PER INSTANT AND NOT ACROSS A
// SEASON, and the difference is the host's zone database rather than anything
// this file decides. A board adds one signed integer --
// `core/src/time_service.cpp:208` -- "add_offset(result.utc.value, timezone_.minutes_east_of_utc);"
// -- because ADR-0014 deliberately stores an effective offset and no zone rules
// -- `docs/adr/0014-time-source-and-synchronization.md:67` -- "This slice stores an effective offset, not an IANA zone database or a DST"
// -- so a device provisioned at `+120` goes on showing `+120` after the
// changeover and eventually says its offset is stale rather than correcting
// itself. `std::localtime` below reads the host's database and does correct
// itself. For `--clock` that is what #553 asked for and it is right; it is
// simply not a behaviour a screenshot may be used as evidence *of*.
core::WallTime host_local_wall_time(core::WallTime utc);

// The same conversion, applied to now.
//
// Live mode calls this on every refresh and not once at startup, so a window
// left open across a DST change follows the host's rule -- the host's, not a
// device's; see above -- instead of holding the offset that happened to be in
// force when it opened. It costs one `localtime` call a
// second, which is what the refresh already spends redrawing a minute that did
// not change.
core::WallTime host_local_wall_time();

} // namespace attadipa::sim
