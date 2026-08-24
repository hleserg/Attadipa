#pragma once

#include "attadipa/core/input.h"
#include "attadipa/platform/board_profile.h"

namespace attadipa::sim {

// The screen that makes a broken screenshot visible instead of plausible.
//
// Every element on it answers one specific way the path from framebuffer to PNG
// can be wrong while still producing a picture:
//
//   * **Four differently coloured, differently sized corner markers, lettered
//     TL, TR, BL, BR.** A rotation moves them; a mirror swaps the letters while
//     leaving the layout looking reasonable. Letters are what catches a mirror,
//     because a mirrored abstract pattern still looks like a pattern.
//   * **An asymmetric F in the centre.** The classic rotation target: F is the
//     one glyph with no symmetry in either axis, so all eight orientations of it
//     are distinguishable at a glance.
//   * **A strip of pure colours** -- full red, full green, full blue, white,
//     mid grey, black -- at known 8-bit values. A swapped R and B turns the
//     strip's ends around; a 565 conversion that lost the low bits shows in the
//     grey.
//   * **A coordinate grid** with its origin labelled, so a scale or crop error
//     has a ruler to be measured against.
//   * **The last button event and how long it was held**, so a press provably
//     arrived and a long press is distinguishable from a short one by looking.
//   * **The last touch point, its id, and a fading trail of the last points.**
//     A swipe replayed as one artificial jump draws two dots; a real
//     down/move/up draws a line. That difference is the whole reason the
//     request insists swipes be sent as sequences.
//
// It is a diagnostic and never a product screen, which is why it is in sim/ and
// why its colours are literal -- see tools/ui/check_raw_values.py.
void build_diagnostic_screen(const platform::BoardProfile& board);

// Rebuilt on a **locale** change, and deliberately not on a theme change --
// which is where this differs from the boot screen and is why the difference is
// stated rather than left to be inferred. The pattern's colours are test
// vectors: pure primaries exist so a swapped channel is visible, and a palette
// that followed the theme would make the screen unable to detect the thing it
// is for. `main.cpp` wires this to the locale handler only.
void rebuild_diagnostic_screen();

// Fed from the input layer so that what is drawn is what the interface saw,
// not what a caller intended.
void diagnostic_screen_on_button(const core::InputEvent& event);
void diagnostic_screen_on_pointer(const core::InputEvent& event);

}  // namespace attadipa::sim
