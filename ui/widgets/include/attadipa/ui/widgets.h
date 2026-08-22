#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"

#include "attadipa/core/availability.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"
#include "attadipa/ui/tokens.h"
#include "attadipa_icons.h"

// Pieces a screen is assembled from, above the tokens and below the screens.
//
// `attadipa_ui` is arithmetic and knows nothing about LVGL, deliberately — a
// token must not be able to reach a panel. But a battery that fills to the
// charge is a *drawing*, and it will be wanted on the Clock, in Settings and on
// whatever comes after. Written once in an application it becomes written three
// times, and the third one always disagrees with the first about how a battery
// at 4 % looks.
//
// So: this target links LVGL, `attadipa_ui` and `attadipa_images`, and every
// screen composes from it. It still cannot see `platform` — the widgets get a
// `Metrics` and a `Theme`, never a board.

namespace attadipa::ui::widgets {

// A charge, and whether anybody has measured one.
//
// `known` is not a formality. A percentage nobody read is not 100 and not 0;
// [ADR-0011](docs/adr/0011-gnss-integrity.md) forbids presenting a value that
// was never observed, and a fuel gauge is where a person meets that rule
// second, right after the clock.
struct Battery {
    bool         known    = false;
    std::uint8_t percent  = 0;      // 0-100; clamped when drawn
    bool         charging = false;
    bool         large    = false;  // Child Mode: the same gauge, bigger
};

// A gauge that fills to the charge, with the number beside it.
//
// `number_font` is passed in rather than chosen here because the type scale does
// not exist yet — final §51 will not let a face be adopted before four things
// are checked and none has been, so today a screen picks one of four generated
// Montserrat subsets and this widget must use the one its caller is using or the
// row will not sit on a common baseline.
//
// Returns the row so a caller can align it; the caller owns nothing else.
lv_obj_t* build_battery(lv_obj_t* parent, const Battery& battery, const Metrics& metrics,
                        Theme theme, const lv_font_t* number_font);

// One capability on the status row.
//
// The icon says *which* and the availability says *how it is doing*, and this
// header is the only place that pairing is written down — so two screens cannot
// come to different conclusions about what a dim mesh icon means.
struct StatusIcon {
    assets::Icon       icon         = assets::Icon::Mesh;
    core::Availability availability = core::Availability::Unsupported;
};

// A row of capability icons, lit or struck through.
//
// Three rules, and each is a decision rather than a style:
//
//  * **`Unsupported` is not drawn at all.** A board with no LoRa does not get a
//    permanently dead mesh icon; a capability that can never exist here is not
//    a status, it is absent. So the Waveshare's row is shorter than the
//    T-Watch's, and that is correct rather than a bug.
//  * **Not-ready is a shape, not a colour.** DESIGN_SYSTEM §3.1 forbids
//    signalling state by colour alone, and on the day palette it could not be
//    done anyway — no accent clears 4.5:1 against Warm Ivory. So an icon that
//    is not `Ready` is struck through, and the stroke is what carries the
//    meaning if the colour does not arrive.
//  * **The row says whether, never why.** "Not set up" and "no signal" look the
//    same here on purpose: the face has room for a state, not a sentence, and
//    the reason lives one tap away. The previous design put the sentence on the
//    face and it read like a debug line, because it was one.
//
// Returns the row, or `nullptr` if nothing was drawable — every icon
// `Unsupported`, or no asset at this density (final §86 forbids resampling one,
// so `nullptr` from the asset table is a real answer and this honours it).
lv_obj_t* build_status_strip(lv_obj_t* parent, const StatusIcon* icons, std::size_t count,
                             const Metrics& metrics, Theme theme, bool large = false);

// Whether this state lights the icon. One definition, so the strip and anything
// that summarises it cannot disagree.
bool is_lit(core::Availability availability);

// How many pixels of a gauge `inner` pixels wide a charge of `percent` fills.
//
// Out here rather than inside the drawing because it is the only part of a
// battery that can be *wrong*, and a wrong one is wrong quietly: an off-by-one
// that renders 100 % one pixel short is invisible on a panel and obvious in an
// assertion. Three properties it has to hold, all of them tested:
//
//   * 0 % is empty and 100 % is full — the two a person checks;
//   * a charge that is not zero never draws as zero. A one-pixel sliver is the
//     truth; an empty box for 2 % is a lie a person acts on;
//   * nothing ever exceeds `inner`, including a percentage above 100, which a
//     fuel gauge that has just been calibrated will happily report.
std::int32_t battery_fill_px(std::uint8_t percent, std::int32_t inner);

}  // namespace attadipa::ui::widgets
