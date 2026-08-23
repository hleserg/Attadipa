#include "diagnostic_screen.h"

#include <cstdio>

#include "lvgl.h"

#include "attadipa_fonts.h"

#include "attadipa/ui/color.h"

// The diagnostic test pattern.
//
// Raw colours and raw pixel geometry on purpose, and this file is named in
// tools/ui/check_raw_values.py's exemption list for that reason. The values
// here are test vectors, not design values: a pattern drawn from theme tokens
// could not detect a swapped colour channel, because the pattern and the
// expectation would move together.

namespace attadipa::sim {
namespace {

constexpr std::size_t kTrailPoints = 24;

struct Diagnostic {
    lv_obj_t* screen      = nullptr;
    lv_obj_t* touch_label = nullptr;
    lv_obj_t* button_label= nullptr;
    lv_obj_t* trail[kTrailPoints] = {};
    std::size_t trail_next = 0;

    platform::BoardProfile board{};
    bool built = false;

    // Last button event, so a rebuild does not lose it.
    int           last_button       = -1;
    bool          last_button_down  = false;
    std::uint32_t last_button_at    = 0;
    std::uint32_t last_button_held  = 0;
    std::uint32_t button_down_at[core::kMaxButtons] = {};
};

Diagnostic g;

lv_color_t rgb(std::uint32_t value)
{
    return lv_color_hex(value);
}

// Six known values. Chosen so that every common failure separates them: full
// primaries catch a channel swap, white and black catch an inversion, and the
// two greys catch a 565 round trip that dropped low bits.
struct Swatch {
    std::uint32_t value;
    const char*   name;
};
constexpr Swatch kSwatches[] = {
    {0xFF0000, "R"}, {0x00FF00, "G"}, {0x0000FF, "B"},
    {0xFFFFFF, "W"}, {0x808080, "5"}, {0x000000, "K"},
};

// Corner markers: different colours *and* different sizes, so that a 180-degree
// rotation is visible even to somebody who cannot read the letters.
struct Corner {
    const char*   text;
    std::uint32_t colour;
    bool          large;      // the asymmetry: two big, two small, diagonally
    lv_align_t    align;
    lv_align_t    label_side; // where the caption sits relative to the marker
};
constexpr Corner kCorners[] = {
    {"TL", 0xFF4000, true, LV_ALIGN_TOP_LEFT, LV_ALIGN_OUT_RIGHT_MID},
    {"TR", 0x00C0FF, false, LV_ALIGN_TOP_RIGHT, LV_ALIGN_OUT_LEFT_MID},
    {"BL", 0xFFD000, false, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_OUT_RIGHT_MID},
    {"BR", 0x40FF80, true, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_OUT_LEFT_MID},
};

lv_obj_t* block(lv_obj_t* parent, int w, int h, std::uint32_t colour)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, rgb(colour), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    return o;
}

void draw_grid(lv_obj_t* parent, int width, int height)
{
    // Eight divisions each way. A ruler for a scale or crop error to be
    // measured against, drawn faintly so it never hides the pattern.
    const int step_x = width / 8;
    const int step_y = height / 8;
    for (int i = 1; i < 8; ++i) {
        lv_obj_t* v = block(parent, 1, height, 0x303030);
        lv_obj_set_pos(v, i * step_x, 0);
        lv_obj_t* h = block(parent, width, 1, 0x303030);
        lv_obj_set_pos(h, 0, i * step_y);
    }
    // The origin, named, and placed below the top-left marker rather than at
    // (3, 3) where the marker covers it -- which is what the first render did.
    lv_obj_t* origin = lv_label_create(parent);
    lv_label_set_text(origin, "0,0");
    lv_obj_set_style_text_color(origin, rgb(0x909090), LV_PART_MAIN);
    lv_obj_set_pos(origin, 3, width / 9 + 3);

    // One labelled grid step, so the ruler has a unit. Without it the grid can
    // only show that something is wrong, not by how much.
    lv_obj_t* step = lv_label_create(parent);
    char text[24];
    std::snprintf(text, sizeof(text), "grid %dpx", step_x);
    lv_label_set_text(step, text);
    lv_obj_set_style_text_color(step, rgb(0x909090), LV_PART_MAIN);
    lv_obj_align(step, LV_ALIGN_BOTTOM_MID, 0, -3);
}

// An F built from three rectangles: the one glyph with no symmetry in either
// axis, so every rotation and every mirror of it is distinguishable.
void draw_f(lv_obj_t* parent, int width, int height)
{
    const int unit = (width < height ? width : height) / 10;
    const int x    = width / 2 - unit;
    const int y    = height / 2 - (unit * 3) / 2;

    lv_obj_t* stem = block(parent, unit, unit * 3, 0xFF00FF);
    lv_obj_set_pos(stem, x, y);

    lv_obj_t* top = block(parent, unit * 2, unit, 0xFF00FF);
    lv_obj_set_pos(top, x + unit, y);

    lv_obj_t* middle = block(parent, (unit * 3) / 2, unit, 0xFF00FF);
    lv_obj_set_pos(middle, x + unit, y + unit + unit / 2);
}

void draw_swatches(lv_obj_t* parent, int width, int height)
{
    const int count = static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0]));
    const int w     = width / count;
    const int h     = height / 12;
    const int y     = height - h - height / 8;

    for (int i = 0; i < count; ++i) {
        lv_obj_t* s = block(parent, w, h, kSwatches[i].value);
        lv_obj_set_pos(s, i * w, y);

        lv_obj_t* label = lv_label_create(parent);
        lv_label_set_text(label, kSwatches[i].name);
        // Grey rather than black-on-black or white-on-white: the label has to
        // stay readable over every swatch including the two extremes.
        lv_obj_set_style_text_color(label, rgb(0x808080), LV_PART_MAIN);
        lv_obj_set_pos(label, i * w + 3, y + 2);
    }
}

void refresh_labels()
{
    if (!g.built) {
        return;
    }
    char text[96];

    if (g.last_button < 0) {
        std::snprintf(text, sizeof(text), "button: none yet");
    } else {
        const char* id = g.last_button < g.board.button_count
                             ? g.board.buttons[g.last_button].id
                             : "?";
        if (g.last_button_down) {
            std::snprintf(text, sizeof(text), "button %s DOWN @%ums", id, g.last_button_at);
        } else {
            // The held duration is what separates a click from a long press by
            // looking, which is the only way an agent can check it.
            std::snprintf(text, sizeof(text), "button %s UP after %ums", id, g.last_button_held);
        }
    }
    lv_label_set_text(g.button_label, text);
}

}  // namespace

void build_diagnostic_screen(const platform::BoardProfile& board)
{
    g.board = board;

    const int width  = board.display.width_px;
    const int height = board.display.height_px;

    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    // Deliberately NOT lv_obj_remove_style_all(): that resets the screen's font
    // to LVGL's built-in Montserrat, which is Latin-only, and the undrawable-
    // glyph check in main() reads the font off this screen. Wiping the style
    // would make that check fail on 54 Cyrillic codepoints and report a font
    // regression that is really this screen throwing the font away. Only what
    // the pattern needs is overridden.
    lv_obj_set_style_bg_color(screen, rgb(0x101010), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // The project's font, not LVGL's. `lv_font_montserrat_14` is generated from
    // `-r 0x20-0x7F,0xB0,0x2022` -- Latin only -- so leaving the screen on
    // LVGL's default makes the undrawable-glyph check in main() report 54
    // Cyrillic codepoints it cannot draw. That would be a true statement about
    // the wrong font: this screen has no localised text at all, and the check
    // is asking what the *build* can draw. Same reasoning as boot_screen.cpp,
    // and the same two sizes, chosen by panel width.
    lv_obj_set_style_text_font(
        screen, board.display.width_px >= 400 ? &attadipa_montserrat_20 : &attadipa_montserrat_14,
        LV_PART_MAIN);
    g.screen = screen;

    draw_grid(screen, width, height);
    draw_f(screen, width, height);
    draw_swatches(screen, width, height);

    // The caption sits *beside* the marker, not on it.
    //
    // It used to sit on it, and the first render of this screen at 410 px
    // showed why that is wrong: the font is chosen by panel width, so "TL" in
    // the 20 px face overflowed a marker sized in absolute pixels and the
    // letters ran off the edge of the screen. A diagnostic that produces its
    // own clipping artefact teaches a reader to ignore clipping. Beside it, the
    // marker can be any size and the caption is always whole.
    const int large_marker = width / 9;
    const int small_marker = width / 13;
    for (const Corner& corner : kCorners) {
        const int size   = corner.large ? large_marker : small_marker;
        lv_obj_t* marker = block(screen, size, size, corner.colour);
        lv_obj_align(marker, corner.align, 0, 0);

        lv_obj_t* label = lv_label_create(screen);
        lv_label_set_text(label, corner.text);
        lv_obj_set_style_text_color(label, rgb(corner.colour), LV_PART_MAIN);
        lv_obj_align_to(label, marker, corner.label_side, 0, 0);
    }

    // Identity, so a screenshot filed as evidence says which board and which
    // build produced it without anyone having to remember.
    char header[64];
    std::snprintf(header, sizeof(header), "%s  %dx%d", board.id, width, height);
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, header);
    lv_obj_set_style_text_color(title, rgb(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, height / 12);

    g.button_label = lv_label_create(screen);
    lv_obj_set_style_text_color(g.button_label, rgb(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(g.button_label, LV_ALIGN_CENTER, 0, -height / 5);

    g.touch_label = lv_label_create(screen);
    lv_label_set_text(g.touch_label, "touch: none yet");
    lv_obj_set_style_text_color(g.touch_label, rgb(0xE0E0E0), LV_PART_MAIN);
    lv_obj_align(g.touch_label, LV_ALIGN_CENTER, 0, height / 5);

    // The trail, pre-created and recycled. Creating an object per touch point
    // during a swipe would make the act of measuring the thing change it.
    for (std::size_t i = 0; i < kTrailPoints; ++i) {
        g.trail[i] = block(screen, 6, 6, 0x00FFC0);
        lv_obj_add_flag(g.trail[i], LV_OBJ_FLAG_HIDDEN);
    }
    g.trail_next = 0;
    g.built      = true;

    refresh_labels();
}

void rebuild_diagnostic_screen()
{
    if (g.built) {
        build_diagnostic_screen(g.board);
    }
}

void diagnostic_screen_on_button(const core::InputEvent& event)
{
    if (event.button >= core::kMaxButtons) {
        return;
    }
    g.last_button      = static_cast<int>(event.button);
    g.last_button_down = event.type == core::InputEventType::ButtonDown;
    g.last_button_at   = event.at_ms;

    if (g.last_button_down) {
        g.button_down_at[event.button] = event.at_ms;
    } else {
        g.last_button_held = event.at_ms - g.button_down_at[event.button];
    }
    refresh_labels();
}

void diagnostic_screen_on_pointer(const core::InputEvent& event)
{
    if (!g.built) {
        return;
    }

    char text[80];
    const char* phase = event.type == core::InputEventType::PointerDown  ? "DOWN"
                        : event.type == core::InputEventType::PointerUp  ? "UP"
                                                                        : "MOVE";
    std::snprintf(text, sizeof(text), "touch %s  x=%d y=%d  id=%u", phase,
                  static_cast<int>(event.x), static_cast<int>(event.y),
                  static_cast<unsigned>(event.touch_id));
    lv_label_set_text(g.touch_label, text);

    if (event.type == core::InputEventType::PointerDown) {
        // A new gesture starts a new trail, so a screenshot shows one swipe
        // rather than every swipe of the session overlaid.
        for (std::size_t i = 0; i < kTrailPoints; ++i) {
            lv_obj_add_flag(g.trail[i], LV_OBJ_FLAG_HIDDEN);
        }
        g.trail_next = 0;
    }

    lv_obj_t* dot = g.trail[g.trail_next];
    lv_obj_set_pos(dot, event.x - 3, event.y - 3);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    // Down and up are marked differently from the moves between them, so a
    // trail shows its own direction.
    lv_obj_set_style_bg_color(
        dot, rgb(event.type == core::InputEventType::PointerDown  ? 0xFFFFFF
                 : event.type == core::InputEventType::PointerUp  ? 0xFF0060
                                                                  : 0x00FFC0),
        LV_PART_MAIN);
    g.trail_next = (g.trail_next + 1) % kTrailPoints;
}

}  // namespace attadipa::sim
