#include "attadipa/ui/widgets.h"

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

lv_color_t paint_graphic(ColorRole role, Theme theme)
{
    return paint(legible_as_graphic(role, theme) ? role : ColorRole::TextPrimary, theme);
}

lv_obj_t* plain(lv_obj_t* parent)
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// A dim icon is not a disabled icon at a glance, so the ones that are not
// working are dimmed *and* struck. This is the opacity half; `strike()` is the
// half that survives a person who cannot tell the two greys apart.
constexpr lv_opa_t kDimOpacity = LV_OPA_60;

// Frees the point array a struck line owns, when the line goes.
void release_points(lv_event_t* e)
{
    lv_free(lv_event_get_user_data(e));
}

// The slash, drawn corner to corner across the glyph's box — twice.
//
// Once was not enough, and the reason is specific rather than aesthetic: the
// `mesh` glyph's two links run at roughly 45°, so a single 45° slash laid over
// it lands *along* the drawing and vanishes. Every "no signal" icon in the world
// solves this the same way, and now this one does too — a wide stroke in the
// page colour first, then the mark itself inside it. The halo cuts a clean gap
// through whatever it crosses, so the mark reads as struck-out rather than as a
// third link.
//
// `lv_line` rather than a rotated rectangle: a line is antialiased by LVGL's own
// rasteriser at whatever angle it ends up, and the alternative — a thin `lv_obj`
// with a transform angle — pays a transform buffer for a diagonal.
void strike(lv_obj_t* over, std::int32_t box, std::int32_t width, lv_color_t colour,
            lv_color_t page)
{
    // A sixth of the box, not one stroke width. At 33 px a stroke-wide inset put
    // the ends of the slash on the icon's own corners, where it read as a broken
    // picture rather than a struck-out one — the eye needs to see the glyph
    // *and* the mark, not one shape.
    const std::int32_t inset = box / 6;

    const std::int32_t weights[] = {width * 3, width};
    const lv_color_t   colours[] = {page, colour};
    const lv_opa_t     opacities[] = {LV_OPA_COVER, kDimOpacity};

    for (int pass = 0; pass < 2; ++pass) {
        lv_obj_t* line = lv_line_create(over);
        lv_obj_remove_style_all(line);

        // One array per line, owned by that line. `lv_line_set_points` keeps the
        // pointer rather than copying, so the array has to outlive the widget,
        // and the tempting shortcut — one `static` array, since every strike on
        // a row is the same shape — holds only while exactly one strip is alive
        // in the process. It is not a row-wide invariant, it is a program-wide
        // one, and T-038's Settings screen breaks it the moment a second strip
        // at a different icon size exists while this one is still on screen.
        // Sixteen bytes and a delete handler cost less than that class of bug.
        lv_point_precise_t* points =
            static_cast<lv_point_precise_t*>(lv_malloc(2 * sizeof(lv_point_precise_t)));
        if (points == nullptr) {
            lv_obj_delete(line);
            continue;
        }
        points[0] = {inset, box - inset};
        points[1] = {box - inset, inset};
        lv_obj_add_event_cb(line, release_points, LV_EVENT_DELETE, points);
        lv_line_set_points(line, points, 2);
        lv_obj_set_style_line_width(line, weights[pass], 0);
        lv_obj_set_style_line_color(line, colours[pass], 0);
        lv_obj_set_style_line_opa(line, opacities[pass], 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 0);
    }
}

}  // namespace

bool is_lit(core::Availability availability)
{
    return availability == core::Availability::Ready;
}

lv_obj_t* build_status_strip(lv_obj_t* parent, const StatusIcon* icons, std::size_t count,
                             const Metrics& metrics, Theme theme, bool large)
{
    if (icons == nullptr || count == 0) {
        return nullptr;
    }

    // One token for the whole row. Child Mode goes up a step on the same scale;
    // it does not get a different drawing, and at 24 dp the pixel size is 39 on
    // the T-Watch and 47 on the Waveshare — both of which the pipeline has.
    const IconSize    size  = large ? IconSize::Lg : IconSize::Md;
    const std::int32_t box  = metrics.px(dp_of(size));
    const std::int32_t rule = metrics.px(dp_of(Stroke::Hairline));

    lv_obj_t* row = plain(parent);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, metrics.px(dp_of(Space::Md)), 0);

    std::size_t drawn = 0;
    for (std::size_t i = 0; i < count; ++i) {
        // A capability this board can never have is absent, not dead. See the
        // header: the Waveshare's row is legitimately shorter than the T-Watch's.
        if (icons[i].availability == core::Availability::Unsupported) {
            continue;
        }

        const lv_image_dsc_t* dsc = assets::icon(icons[i].icon, size, metrics);
        if (dsc == nullptr) {
            // No asset was drawn at this pixel size, and final §86 forbids
            // making one up by resampling a neighbour. Skipping is the only
            // honest option; a substituted size is a picture nobody drew.
            continue;
        }

        const bool lit = is_lit(icons[i].availability);

        lv_obj_t* slot = plain(row);
        lv_obj_set_size(slot, box, box);

        lv_obj_t* glyph = lv_image_create(slot);
        lv_image_set_src(glyph, dsc);
        lv_obj_align(glyph, LV_ALIGN_CENTER, 0, 0);
        // An A8 mask has no colour of its own; recolouring is how it gets one,
        // and it is how a theme change reaches an icon at all.
        lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(
            glyph, paint_graphic(lit ? ColorRole::TextPrimary : ColorRole::TextMuted, theme), 0);
        lv_obj_set_style_image_opa(glyph, lit ? LV_OPA_COVER : kDimOpacity, 0);

        if (!lit) {
            // Struck in the same ink and at the same opacity as the glyph it
            // crosses, so the pair reads as one dimmed thing. A full-strength
            // slash over a 60 %-opacity icon is a slash with an icon behind it.
            strike(slot, box, rule, paint(ColorRole::TextMuted, theme),
                   paint(ColorRole::BackgroundPrimary, theme));
        }
        ++drawn;
    }

    if (drawn == 0) {
        // Nothing to say. An empty row would still take its padding and push the
        // face off centre, so it is deleted rather than left standing — a watch
        // with no reportable capability shows a clock and nothing else, which is
        // the correct amount of chrome for it.
        lv_obj_delete(row);
        return nullptr;
    }
    return row;
}

}  // namespace attadipa::ui::widgets
