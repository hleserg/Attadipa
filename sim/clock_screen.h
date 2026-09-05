#pragma once

#include "attadipa/apps/clock.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/color.h"

namespace attadipa::sim {

// The clock, and the provisioning entry screen a long press on it opens.
//
// The pair moved out of `main.cpp` in #432 for the reason the navigation
// readout did: `main.cpp` is a `main()`, so no test in this repository can link
// a line of it, and the two theme owners living there were owners nothing could
// drive. They are one unit rather than two files because they are one state
// machine — the entry screen takes its geometry from the clock's config, and
// hands the panel back to the clock when it finishes.

// Put the clock on the panel in this theme, with this state, and say what `T`
// and `L` do to it. `live` starts the refresh timer that re-reads the host
// clock; a pinned `--clock-time` does not want one.
void build_clock_screen(const platform::BoardProfile &board, ui::Theme theme,
                        const apps::ClockState &state, bool live);

// Rebuild in the current locale, keeping the same screen object. Installed as
// the locale-changed handler, like every other screen here.
void rebuild_clock_screen();

// What a long press on the clock does: swap in the provisioning entry screen,
// move `T` and `L` to it, and arm the timer that gives the panel back when the
// entry is done. Exported because `--provision` wants the entry screen without
// a finger held on the clock first, and because a theme owner a test cannot
// reach is the defect this file exists to fix.
void enter_provisioning();

} // namespace attadipa::sim
