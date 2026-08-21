#pragma once

#include "attadipa/core/capability_registry.h"
#include "attadipa/platform/hardware_inventory.h"

namespace attadipa::sim {

// Which screen the simulator is showing.
//
// There is no navigation model yet — T-018 owns that — so this is a key press
// and a flag, and it is honest about being one. What it does buy immediately is
// the thing final §53 asks for: a reviewer can see every screen at both
// geometries, in both locales, in both themes and in both Child states without
// a rebuild, which is the difference between a visual matrix and a wish.
enum class Screen { Clock, Diagnostic };

void set_screen(Screen screen);
void toggle_screen();

// Rebuild whichever screen is current, in the current locale, theme and mode.
// Installed as the locale-changed handler.
void rebuild_current_screen();

// The composition root's half of the Clock.
//
// `apps/clock` draws a `ClockModel` and knows nothing else. Filling that model
// out of the capability registry — and, on a device, out of a real clock and a
// real fuel gauge — is this side's job, because it is the only side allowed to
// see both layers.
void build_clock_screen(const platform::HardwareInventory& inventory,
                        const core::CapabilityRegistry&    caps);

// Whether Child Mode is on. `K` toggles it, so a reviewer can see both variants
// without a rebuild — the same argument as the theme and the locale.
void toggle_child_mode();
void set_child_mode(bool on);

}  // namespace attadipa::sim
