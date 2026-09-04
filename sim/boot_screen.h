#pragma once

#include "lvgl.h"

#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/locale.h"
#include "attadipa/platform/hardware_inventory.h"
#include "attadipa/ui/color.h"

namespace attadipa::sim {

// The simulator's first screen.
//
// It is a **diagnostic**, not a product screen, and it says so in its own
// source: it shows the two capability layers side by side because that is the
// thing worth looking at before there is a product screen to look at. The real
// first screen is the Clock (T-037), and it will not look like this.
//
// Its chrome goes through tr() all the same, because "a screen can be written
// with no user-facing literal" is a claim about a mechanism and a mechanism has
// to be exercised by something.
void build_boot_screen(const platform::HardwareInventory& inventory,
                       const core::CapabilityRegistry&    caps);

// Rebuild in the current locale, keeping the same screen object. Installed as
// the locale-changed handler, which is how ADR-0010's "switched without a
// reboot" gets demonstrated rather than asserted.
void rebuild_boot_screen();

// Switch between the day and night palettes and redraw, and say which palette
// the screen is now drawn in.
//
// Bound to a key rather than to a light sensor because the point of it here is
// review: "day and night themes checked" is in the Definition of Done, and a
// reviewer who has to rebuild with a different constant to see the other one
// will check it once. `build_boot_screen` registers it as this screen's answer
// to `T` (`sim/review_keys.h`); nothing else may call it, because a theme
// toggled for a screen that is not on the panel is a console line and nothing
// else.
ui::Theme toggle_theme();

// The palette this run starts in. Called by the composition root before the
// first build; after that, T.
void set_theme(ui::Theme theme);

// How many codepoints of a locale's catalogue the given font cannot draw.
// Prints each one it finds.
//
// This is the runtime sibling of tools/l10n/check_glyphs.py. The build-time
// check compares the catalogue against the subset the font will be *generated
// from*; this one compares it against the font actually linked in, which is the
// only thing that can answer the question on a device. Today they disagree, and
// loudly — see the note where main() calls it.
int report_undrawable_glyphs(const lv_font_t* font, l10n::Locale locale);

}  // namespace attadipa::sim
