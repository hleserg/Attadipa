#include "attadipa/ui/widgets.h"

#include "attadipa/l10n/tr.h"

namespace attadipa::ui::widgets {
namespace {

lv_color_t paint(ColorRole role, Theme theme)
{
    if (const std::optional<Rgb> value = color(role, theme)) {
        return lv_color_hex(value->packed());
    }
    const ColorRole substitute = kind_of(role) == ColorKind::Background
                                     ? ColorRole::BackgroundPrimary
                                     : ColorRole::TextPrimary;
    return lv_color_hex(color(substitute, theme)->packed());
}

// A colour a *shape* can be drawn in, which is a lower bar than a word.
//
// DESIGN_SYSTEM §3.2 separates the two thresholds and this is the graphic one:
// a filled bar two millimetres tall survives a contrast a sentence would not.
// Where even that fails the ink colour is used, so the gauge is always visible
// and only ever loses its accent.
lv_color_t paint_graphic(ColorRole role, Theme theme)
{
    return paint(legible_as_graphic(role, theme) ? role : ColorRole::TextPrimary, theme);
}

// A bare `lv_obj` with none of LVGL's default chrome. Every widget here starts
// from one, because the default theme's background, border and scrollbar are
// three things to switch off at every call site otherwise.
lv_obj_t* plain(lv_obj_t* parent)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// Under a fifth is the one number on a watch face that changes what a person
// does next, so it is the one that changes colour — and it changes shape too,
// because the fill is shorter. Colour alone would not be allowed (§3.1); here
// it is the second channel rather than the only one.
constexpr std::uint8_t kLowWater = 20;

}  // namespace

std::int32_t battery_fill_px(std::uint8_t percent, std::int32_t inner)
{
    if (inner <= 0) {
        return 0;
    }
    const std::int32_t clamped = percent > 100 ? 100 : static_cast<std::int32_t>(percent);
    if (clamped == 0) {
        return 0;
    }
    const std::int32_t fill = inner * clamped / 100;
    // Integer division rounds down, so every charge under one pixel's worth
    // would draw as empty. A sliver is the truth and an empty box is not.
    return fill == 0 ? 1 : fill;
}

lv_obj_t* build_battery(lv_obj_t* parent, const Battery& battery, const Metrics& metrics,
                        Theme theme, const lv_font_t* number_font)
{
    // The gauge is exactly as tall as an icon on the same row, which is what
    // makes a row of them look drawn rather than assembled. Child Mode moves up
    // one step on the same scale instead of inventing a second size.
    const std::int32_t body_h =
        metrics.px(dp_of(battery.large ? IconSize::Lg : IconSize::Sm));
    const std::int32_t body_w   = body_h * 2;   // a proportion, not a length
    const std::int32_t wall     = metrics.px(dp_of(Stroke::Regular));
    const std::int32_t cap_w    = metrics.px(dp_of(Space::Xs));
    const std::int32_t cap_h    = body_h * 5 / 9;
    // Not `Radius::Sm`. The first version used it and the gauge came back from
    // the render looking like a toggle switch — at 26 px tall an 8 dp corner is
    // half the height, so the shell reads as a lozenge with a knob in it. A
    // battery's corners are *softened*, not rounded, and the only length on this
    // widget small enough to do that is its own wall.
    const std::int32_t radius   = wall;

    lv_obj_t* row = plain(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, metrics.px(dp_of(Space::Sm)), 0);

    // --- the shell: body, then the terminal nub on its right.
    lv_obj_t* shell = plain(row);
    lv_obj_set_size(shell, body_w + cap_w, body_h);

    lv_obj_t* body = plain(shell);
    lv_obj_set_size(body, body_w, body_h);
    lv_obj_align(body, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(body, wall, 0);
    lv_obj_set_style_border_color(body, paint(ColorRole::TextMuted, theme), 0);
    lv_obj_set_style_radius(body, radius, 0);

    lv_obj_t* cap = plain(shell);
    lv_obj_set_size(cap, cap_w, cap_h);
    lv_obj_align(cap, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(cap, paint(ColorRole::TextMuted, theme), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cap, radius, 0);

    // --- the fill, which is the whole reason this is a drawing and not a word.
    if (battery.known) {
        const std::uint8_t percent = battery.percent > 100 ? 100 : battery.percent;
        // A hairline of page between the wall and the fill. It is one pixel and
        // it is the difference between a gauge and a box with a thick left
        // edge: at 12 % the fill is five pixels of the same ink the wall is
        // drawn in, and flush against it the two merge into one stroke. The
        // render showed exactly that, on the day theme, where `Warning` is
        // refused for contrast and the fill has no colour of its own to
        // separate it. Every battery glyph ever drawn has this gap.
        const std::int32_t gap   = metrics.px(dp_of(Stroke::Hairline));
        const std::int32_t inner = body_w - 2 * wall - 2 * gap;
        const std::int32_t fill  = battery_fill_px(battery.percent, inner);

        const ColorRole role = battery.charging ? ColorRole::Success
                               : percent <= kLowWater ? ColorRole::Warning
                                                      : ColorRole::TextPrimary;

        if (fill > 0) {
            lv_obj_t* level = plain(body);
            lv_obj_set_size(level, fill, body_h - 2 * wall - 2 * gap);
            lv_obj_align(level, LV_ALIGN_LEFT_MID, gap, 0);
            lv_obj_set_style_bg_color(level, paint_graphic(role, theme), 0);
            lv_obj_set_style_bg_opa(level, LV_OPA_COVER, 0);
            // The fill is square. Rounding both ends of it is what made the
            // first version a switch: a rounded left edge floats away from the
            // wall it is supposed to start at.
            lv_obj_set_style_radius(level, 0, 0);
        }
    } else {
        // Unknown is not empty. An empty gauge is a claim — "flat" — and this
        // is the absence of one, so the box is hatched with a single bar rather
        // than left blank or filled.
        lv_obj_t* hatch = plain(body);
        lv_obj_set_size(hatch, body_w - 2 * wall - 2 * metrics.px(dp_of(Space::Xs)), wall);
        lv_obj_align(hatch, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(hatch, paint(ColorRole::TextMuted, theme), 0);
        lv_obj_set_style_bg_opa(hatch, LV_OPA_COVER, 0);
    }

    // --- the number, because a gauge alone makes a person estimate.
    //
    // Both, not either. The bar is what the eye reads at a glance and the digits
    // are what a person checks before deciding whether to charge, and a design
    // that offers one of the two makes somebody squint at a fifth of a
    // rectangle.
    lv_obj_t* number = lv_label_create(row);
    if (battery.known) {
        lv_label_set_text_fmt(number, l10n::tr(l10n::StringId::ClockBattery),
                              static_cast<unsigned>(battery.percent > 100 ? 100
                                                                          : battery.percent));
        lv_obj_set_style_text_color(
            number,
            paint(battery.percent <= kLowWater && !battery.charging ? ColorRole::TextPrimary
                                                                    : ColorRole::TextMuted,
                  theme),
            0);
    } else {
        lv_label_set_text(number, l10n::tr(l10n::StringId::ValueUnknown));
        lv_obj_set_style_text_color(number, paint(ColorRole::TextMuted, theme), 0);
    }
    lv_obj_set_style_text_font(number, number_font, 0);

    return row;
}

}  // namespace attadipa::ui::widgets
