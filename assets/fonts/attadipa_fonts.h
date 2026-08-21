#pragma once

#include "lvgl.h"

// The UI fonts, generated from tools/font/charset.py.
//
// Four sizes, one typeface, and every one of the 181 codepoints both catalogues
// use — which LVGL's own built-in Montserrat does not have. Its generated subset
// is `-r 0x20-0x7F,0xB0,0x2022`, so `×` renders as a box and so does every
// Cyrillic letter. That is what T-083 is about, and it is a defect rather than a
// missing feature: a shipped screen with a box on it is a bug the user sees
// before anybody else does.
//
// **Not a typeface decision.** Montserrat is here because LVGL already ships it
// under OFL-1.1 at the revision this project pins, and because it covers the
// whole charset — the two properties that make it usable scaffolding today.
// Which face the product uses is open question D16, and final §51 lists four
// things that must be checked before either candidate is adopted. None has been.
//
// Licence: SIL Open Font License 1.1, assets/fonts/OFL.txt. A generated `.c` is
// a modified form of the font and stays under the same licence.

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(attadipa_montserrat_14)
LV_FONT_DECLARE(attadipa_montserrat_16)
LV_FONT_DECLARE(attadipa_montserrat_20)
LV_FONT_DECLARE(attadipa_montserrat_28)

#ifdef __cplusplus
}
#endif
