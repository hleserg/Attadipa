#pragma once

#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/color.h"

namespace attadipa::sim {

// The node navigation readout, staged and put on the panel.
//
// It lived in `main.cpp` until #432. That is why the theme key could not reach
// it and why nothing could test that it had not: `main.cpp` is a `main()`, so
// no test in this repository can link a line of it. It is here now for the same
// reason `boot_screen` and `diagnostic_screen` are — a screen the simulator
// shows is a thing that can be built, rebuilt and driven, and the composition
// root's job is to choose one, not to be one.

// Stage one of the readout's scenarios by name, or print the list and answer
// false. Coordinates are the deliberately-nowhere place the position tests use;
// no real location belonging to anybody appears in this repository.
//
// This exists because a screenshot of `Ready` proves nothing about the seven
// other things the readout can say, and those seven are where a number appears
// that should not have.
bool stage_nav_scenario(const char *name);

// Draw the staged scenario for this board, in this theme, and say what `T` does
// to it (`sim/review_keys.h`). Geometry and density come from the board; the
// palette follows the panel technology, because a lit pixel costs power on one
// of the two and not on the other.
void build_nav_screen(const platform::BoardProfile &board, ui::Theme theme);

// Rebuild in the current locale, keeping the same screen object. Installed as
// the locale-changed handler by the composition root, the same way the boot and
// diagnostic screens are.
void rebuild_nav_screen();

} // namespace attadipa::sim
