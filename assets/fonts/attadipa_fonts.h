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

// The clock's own faces: **numerals only** — digits, colon, the dashes the
// unknown-time placeholder uses, and the degree sign a bearing will want.
//
// A watch face needs a time that dominates the screen, which is 64 px on a
// 240 px panel and more on a 410 px one. At the full charset that would be
// about 160 kB and 360 kB of .rodata for a string that is only ever "09:41" or
// "--:--"; six characters cost a fraction of it. Asking for a letter from one
// of these draws nothing, which is why they are named for what they contain.
LV_FONT_DECLARE(attadipa_montserrat_num_64)
LV_FONT_DECLARE(attadipa_montserrat_num_96)

#ifdef __cplusplus
}
#endif
