#pragma once

#include <cstdint>

#include "lvgl.h"

#include "attadipa/core/availability.h"
#include "attadipa/core/capability.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"

// The Clock — the first product screen (T-037, final §58 and §88).
//
// It is an application, so it is written against `core` and `ui` and never
// against `platform`. It does not know which board it is on, which RTC ticks,
// which PMU reports the battery, or whether the time came from a GNSS receiver
// or from a phone. Everything it draws arrives in `ClockModel`, filled by the
// composition root out of the capability registry.
//
// That is not ceremony. The two panels share almost nothing, the time may come
// from four different places, and a screen that reached for any of it would
// have to be written twice.

namespace attadipa::apps {

// Everything the Clock draws. No pointers, no ownership, no hardware.
//
// The optionality is the interesting part and it is deliberate: a watch that
// cannot tell the time yet must say so rather than draw 00:00. ADR-0011 already
// forbids presenting a value nobody observed, and a clock is the first place
// that rule becomes visible to a person.
struct ClockModel {
    bool          time_known = false;
    std::uint8_t  hour       = 0;    // 0-23, local
    std::uint8_t  minute     = 0;

    bool          date_known = false;
    std::uint8_t  weekday    = 0;    // 0 = Monday
    std::uint8_t  day        = 1;    // 1-31
    std::uint8_t  month      = 1;    // 1-12

    // A battery percentage nobody has measured is not 100. `false` here draws
    // the gauge as unknown rather than as full.
    bool          battery_known   = false;
    std::uint8_t  battery_percent = 0;
    bool          charging        = false;

    // The one status the face carries. Which capability, and how it is doing —
    // the screen turns that into a word and a colour, and never into a claim
    // about a chip.
    core::Capability   status_of     = core::Capability::MeshMessaging;
    core::Availability status         = core::Availability::Unprovisioned;
    bool               status_shown   = false;

    // Child Mode (final §58): larger type, larger targets, fewer things.
    bool child_mode = false;
};

// Build the Clock into `parent`, replacing whatever was there.
//
// `metrics` carries the panel's density and nothing else about it — the ui
// library does not link platform, and this one does not either. `theme` is day
// or night; the screen resolves every colour through `ColorRole`, so a theme it
// has never seen works without a recompile.
void build_clock(lv_obj_t* parent, const ClockModel& model, const ui::Metrics& metrics,
                 ui::Theme theme);

// The Clock's manifest. It requires `Time` and nothing else — a watch with no
// mesh, no position and no node is still a watch — and is enhanced by the rest.
const struct AppManifest& clock_manifest();

}  // namespace attadipa::apps
