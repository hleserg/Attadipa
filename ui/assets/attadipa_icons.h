#pragma once

#include "attadipa/ui/metrics.h"
#include "attadipa/ui/tokens.h"
#include "generated/attadipa_images.h"

namespace attadipa::assets {

// The icons that exist. Adding one is a line in `tools/assets/manifest.py`, a
// drawing in `icon_drawings.py`, and a regeneration — not an edit here.
enum class Icon : std::uint8_t { Mesh, Position, Warning };

// The asset for this icon at this token size on this panel, or `nullptr`.
//
// `nullptr` is a real answer and not a failure to find one. The pipeline
// generates a fixed set of **pixel** sizes and refuses to resample between
// them (final §86), so a token-and-density pair that lands outside that set has
// no asset, and the honest thing to return is nothing. A caller that draws a
// substituted size would be showing a picture that was never drawn for that
// panel — which is the failure the rule exists to prevent, arriving one layer
// later.
//
// Note what is *not* a parameter: the board. Two boards asking for different
// tokens can land on the same pixel size and would then share one file. At the
// measured 220 and 315 dpi they happen not to; the rule is about the unit, not
// about that coincidence.
const lv_image_dsc_t* icon(Icon which, ui::IconSize size, const ui::Metrics& metrics);

// The same, by an explicit pixel size. For the simulator's contact sheet and
// for tests that want to assert the set rather than a lookup.
const lv_image_dsc_t* icon_px(Icon which, int pixels);

const char* name_of(Icon which);

}  // namespace attadipa::assets
