#pragma once

#include "lvgl.h"

// The UI fonts, generated from tools/font/charset.py.
//
// Six sizes of Nunito Sans Regular 400, selected from the canonical owner
// references after the measured Inter/Nunito comparison. All 177 catalogue
// codepoints are present, including Cyrillic; directional arrows are generated
// icons because Nunito Sans deliberately has no arrow glyphs.
//
// Licence: SIL Open Font License 1.1, assets/fonts/OFL.txt. A generated `.c` is
// a modified form of the font and stays under the same licence.

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(attadipa_nunito_sans_14)
LV_FONT_DECLARE(attadipa_nunito_sans_16)
LV_FONT_DECLARE(attadipa_nunito_sans_20)
LV_FONT_DECLARE(attadipa_nunito_sans_28)
LV_FONT_DECLARE(attadipa_nunito_sans_64)
LV_FONT_DECLARE(attadipa_nunito_sans_96)

#ifdef __cplusplus
}
#endif
