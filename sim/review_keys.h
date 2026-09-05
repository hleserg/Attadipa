#pragma once

#include "lvgl.h"

#include "attadipa/ui/color.h"

namespace attadipa::sim {

// The two keys a reviewer has: `L` for the language, `T` for the theme.
//
// Keys rather than a menu because the Settings screen does not exist yet
// (T-038) and the acceptance criterion — "switches at runtime without a
// reboot" — is about the mechanism, not about where the switch lives.
//
// `L` has one address and has had it since ADR-0010:
// `l10n::set_locale_changed_handler()`, set by whoever put a screen on the
// panel. `T` had none. It was answered by an `if` ladder in `main.cpp` over the
// screens that file happened to know were active, with the boot screen's own
// toggle as the fallback for everything else — so the navigation readout, added
// later to the same file and never added to the ladder, took a theme once at
// startup and never heard about a change again. The console did not lie about
// it: with no boot screen built the old line qualified itself as `theme: night
// (nothing on screen follows it in this mode)`. It is `--help` that promised
// "T toggles it while running", and under `--nav` nothing did (#432, and
// #430's unresolved review comment before it).
//
// So `T` gets the shape `L` has: one address, set where the screen is built. A
// screen that is not on the panel cannot answer for a palette it does not own,
// and a screen added tomorrow either registers here or is visibly ignored — it
// cannot quietly land on another face's config.
//
// The toggle returns the theme it switched to, so the one line of console
// output is printed by the one place that knows something actually followed.
using ThemeToggle = ui::Theme (*)();

// Say what `T` does now.
//
// `nullptr` is a screen that deliberately does not follow the theme — the
// diagnostic pattern, whose colours are test vectors rather than a palette
// (`sim/diagnostic_screen.h`) — and `T` then says nothing changed instead of
// changing something off screen.
void set_theme_toggle(ThemeToggle toggle);

// The `LV_EVENT_KEY` handler the composition root installs on the active
// screen. The key comes from the input device that dispatched the event, which
// is what makes this reachable by anything that drives an LVGL keypad — the SDL
// keyboard in the simulator, and the regression in `tests/test_sim_review_keys.cpp`.
void on_screen_key(lv_event_t *event);

} // namespace attadipa::sim
