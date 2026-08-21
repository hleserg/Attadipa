#pragma once

#include "lvgl.h"

#include "firefly/core/capability_registry.h"
#include "firefly/l10n/locale.h"
#include "firefly/platform/hardware_inventory.h"

namespace firefly::sim {

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

// How many codepoints of a locale's catalogue the given font cannot draw.
// Prints each one it finds.
//
// This is the runtime sibling of tools/l10n/check_glyphs.py. The build-time
// check compares the catalogue against the subset the font will be *generated
// from*; this one compares it against the font actually linked in, which is the
// only thing that can answer the question on a device. Today they disagree, and
// loudly — see the note where main() calls it.
int report_undrawable_glyphs(const lv_font_t* font, l10n::Locale locale);

}  // namespace firefly::sim
