#pragma once

#include "attadipa/platform/board_profile.h"
#include "attadipa/core/mesh_service.h"
#include "attadipa/ui/color.h"

namespace attadipa::sim {

// The mesh screen, staged and put on the panel.
//
// It exists because the screen it replaces could not be put on a panel at all.
// `build_mesh_screen()` lived inside `firmware/main/waveshare_board.cpp` and
// said so about itself: there was no simulator entry for it, so every change to
// it merged without anybody seeing the result, on either board.

// Stage one of the link's states by name, or print the list and answer false.
//
// The keys and the node name are invented for this file. A real MeshCore public
// key identifies one physical node on somebody's bench and does not belong in a
// public repository, so nothing here is a key that has ever been on the air.
bool stage_mesh_scenario(const char *name);
const core::MeshStatus &staged_mesh_status();

// Draw the staged state for this board, in this theme, and say what `T` does.
void build_mesh_screen_sim(const platform::BoardProfile &board, ui::Theme theme);

// Rebuild in the current locale, keeping the same screen object.
void rebuild_mesh_screen();

} // namespace attadipa::sim
