#include "attadipa_icons.h"

namespace attadipa::assets {
namespace {

struct Entry {
    Icon                  which;
    int                   pixels;
    const lv_image_dsc_t* dsc;
};

// One per icon name, so that the generator's bare token — `mesh` — can become
// an enumerator. The preprocessor cannot do that mapping on its own, and this
// is the cheapest way to make it a **compile** error when the manifest gains a
// name nobody has taught this file about: `icon_named_battery` will not exist.
constexpr Icon icon_named_mesh() { return Icon::Mesh; }
constexpr Icon icon_named_position() { return Icon::Position; }
constexpr Icon icon_named_companion() { return Icon::Companion; }
constexpr Icon icon_named_warning() { return Icon::Warning; }

// Built from the generator's own list, so the table cannot outlive an asset
// that was removed or miss one that was added.
#define ATTADIPA_ICON_ENTRY(name_token, px, symbol) \
    Entry{icon_named_##name_token(), (px), &(symbol)},

const Entry kTable[] = {ATTADIPA_ICON_LIST(ATTADIPA_ICON_ENTRY)};

#undef ATTADIPA_ICON_ENTRY

}  // namespace

const lv_image_dsc_t* icon_px(Icon which, int pixels)
{
    for (const Entry& e : kTable) {
        if (e.which == which && e.pixels == pixels) {
            return e.dsc;
        }
    }
    return nullptr;
}

const lv_image_dsc_t* icon(Icon which, ui::IconSize size, const ui::Metrics& metrics)
{
    return icon_px(which, static_cast<int>(metrics.px(ui::dp_of(size))));
}

const char* name_of(Icon which)
{
    switch (which) {
        case Icon::Mesh:     return "icon.mesh";
        case Icon::Position: return "icon.position";
        case Icon::Companion: return "icon.companion";
        case Icon::Warning:  return "icon.warning";
    }
    return "icon.unknown";
}

}  // namespace attadipa::assets
