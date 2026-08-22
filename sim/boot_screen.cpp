#include "boot_screen.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <set>

#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/catalogue.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/hardware_inventory.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"
#include "labels.h"

namespace attadipa::sim {
namespace {

using core::Availability;
using core::Capability;
using l10n::PluralId;
using l10n::StringId;
using l10n::tr;
using platform::HardwareFeature;
using ui::ColorRole;
using ui::Dp;
using ui::Metrics;
using ui::PixelCost;
using ui::Space;
using ui::Theme;
using ui::TypeRole;

// What the screen was last built from, so the locale-changed handler can build
// it again. Pointers rather than copies: the inventory and the registry are
// owned by main(), which outlives every screen.
const platform::HardwareInventory* g_inventory = nullptr;
const core::CapabilityRegistry*    g_caps      = nullptr;

// Which theme this screen is currently drawn in. `T` toggles it and the screen
// rebuilds, which is how "day and night both checked" becomes something a person
// can do in four seconds rather than a line in a review checklist.
Theme g_theme = Theme::Day;

// Dp to pixels for the panel this run was given. Rebuilt on every build because
// --board can change between runs and the value is two bytes.
Metrics metrics()
{
    return g_inventory != nullptr ? Metrics::for_dpi(g_inventory->display().dpi())
                                  : Metrics::unscaled();
}

// The one place `platform::PanelTechnology` and `ui::PixelCost` meet — OD-16 /
// issue #52's first constraint. Nothing past this function, including `paint()`
// three lines below, ever learns which enumerator produced the answer: it asks
// only "does a lit pixel cost power here".
PixelCost pixel_cost()
{
    if (g_inventory == nullptr) {
        return PixelCost::Fixed;
    }
    switch (g_inventory->display().technology) {
        case platform::PanelTechnology::Amoled: return PixelCost::PerPixel;
        case platform::PanelTechnology::Ips:
        case platform::PanelTechnology::Unknown:
        default: return PixelCost::Fixed;
    }
}

std::int32_t px(Space s)
{
    return metrics().px(ui::dp_of(s));
}

// A role, resolved for the current theme, in the form LVGL wants.
//
// The substitution is the interesting part. `color()` returns nothing for a role
// the theme genuinely does not define — `color.danger` in both themes, any
// background the night table omits — and this screen must not paint a guess and
// must not paint nothing either. So it substitutes the one colour that is always
// defined, and says out loud that it did. A diagnostic that quietly invents a
// colour is a diagnostic that lies about the palette it is diagnosing.
lv_color_t paint(ColorRole role)
{
    const PixelCost cost = pixel_cost();
    if (const std::optional<ui::Rgb> value = ui::color(role, g_theme, cost)) {
        return lv_color_hex(value->packed());
    }

    const ColorRole substitute = ui::kind_of(role) == ui::ColorKind::Background
                                     ? ColorRole::BackgroundPrimary
                                     : ColorRole::TextPrimary;
    std::fprintf(stderr, "palette: %s is UNKNOWN in the %s theme (%s), drawing %s instead\n",
                 ui::name_of(role), ui::name_of(g_theme), ui::name_of(cost), ui::name_of(substitute));
    return lv_color_hex(ui::color(substitute, g_theme, cost)->packed());
}

// A UTF-8 reader, because the catalogue is UTF-8 and LVGL's own decoder lives
// behind a private header. Fifteen lines is a smaller dependency than a header
// upstream marks as not-for-us.
std::uint32_t next_codepoint(const char* text, std::size_t& i)
{
    const auto byte = static_cast<unsigned char>(text[i]);
    std::uint32_t codepoint = 0;
    std::size_t   extra     = 0;

    if (byte < 0x80) {
        codepoint = byte;
    } else if ((byte & 0xE0) == 0xC0) {
        codepoint = byte & 0x1FU;
        extra     = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        codepoint = byte & 0x0FU;
        extra     = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        codepoint = byte & 0x07U;
        extra     = 3;
    } else {
        ++i;  // a stray continuation byte: skip it rather than loop forever
        return 0;
    }

    ++i;
    for (std::size_t k = 0; k < extra; ++k) {
        const auto cont = static_cast<unsigned char>(text[i]);
        if ((cont & 0xC0) != 0x80) {
            return 0;
        }
        codepoint = (codepoint << 6) | (cont & 0x3FU);
        ++i;
    }
    return codepoint;
}

// Availability is a seven-state enum and this maps it onto three roles, which
// is a loss of information on purpose: the row already spells the state out in
// words beside the colour. DESIGN_SYSTEM §3.1 requires exactly that, and on the
// day palette it is not a nicety — Meadow Green is 2.81:1 on Warm Ivory and
// Attadipa Orange is 2.19:1, so the colour here is emphasis and the word is the
// message. See docs/ui/DESIGN_SYSTEM.md §3.4.
ColorRole role_for(Availability availability)
{
    switch (availability) {
        case Availability::Ready:       return ColorRole::Success;
        case Availability::Unsupported: return ColorRole::TextMuted;
        default:                        return ColorRole::Warning;
    }
}

// A two-column row: a name on the left, a state on the right.
//
// DESIGN_SYSTEM §8 says a reusable component defines its wrap, max lines,
// ellipsis, flexible width and overflow **before** it is used, and this row is
// why that rule is in the document. Two earlier versions of it broke on the
// 240 px panel in two different ways, and both were visible in a screenshot
// before anybody read the code:
//
//   1. `SPACE_BETWEEN` with two content-sized labels pushes them to opposite
//      edges and then draws them through each other. Nothing clips, nothing
//      warns, and the result is two unreadable words on top of one another.
//   2. Giving the left label `flex_grow` fixes the overlap and produces the
//      next bug: `LV_LABEL_LONG_DOT` cannot ellipsize a label whose height is
//      content-sized, so it wraps to a second line instead, and the row grows
//      into the one below it.
//
// So the rule is stated rather than hoped for. The **right** column is sized to
// its content and never shrinks — a state that says "не настроено" instead of
// "не наст…" is the whole point of the row. The **left** column takes what is
// left, is exactly one line tall, and ellipsizes. Which is also why the labels
// in labels.cpp are chosen short: the fallback should be rare, not routine.
lv_obj_t* make_row(lv_obj_t* parent, const char* left, const char* right, ColorRole role,
                   const lv_font_t* font)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* left_label = lv_label_create(row);
    lv_label_set_text(left_label, left);
    lv_obj_set_style_text_font(left_label, font, 0);
    lv_obj_set_style_text_color(left_label, paint(ColorRole::TextPrimary), 0);
    lv_obj_set_flex_grow(left_label, 1);
    // One line, and the height comes from the font rather than from a constant:
    // the four generated sizes have four different line heights, and a number
    // here would be right for one of them.
    lv_obj_set_height(left_label, lv_font_get_line_height(font));
    lv_label_set_long_mode(left_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_pad_right(left_label, px(Space::Sm), 0);

    lv_obj_t* right_label = lv_label_create(row);
    lv_label_set_text(right_label, right);
    lv_obj_set_style_text_font(right_label, font, 0);
    lv_obj_set_style_text_color(right_label, paint(role), 0);
    lv_obj_set_style_text_align(right_label, LV_TEXT_ALIGN_RIGHT, 0);
    // Content-sized and it stays that way. `flex_grow` on the left already
    // takes the remainder, so this one is only ever asked for what it needs.
    lv_obj_set_flex_grow(right_label, 0);

    return row;
}

void make_heading(lv_obj_t* parent, const char* text, const lv_font_t* font)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, paint(ColorRole::TextMuted), 0);
    lv_obj_set_style_pad_top(label, px(Space::Sm), 0);
}

// The one place in this file that still holds bare numbers, and the reason is
// in tokens.h: `TypeRole` deliberately carries no sizes, because final §51 wants
// licence, Cyrillic coverage, legibility at real pixel size and generated flash
// size checked before a face is adopted, and none of the four has been.
//
// So this is a scaffold and is written as one: a role goes in, one of the four
// generated sizes comes out. When the type scale exists it replaces the body of
// this function and no call site changes.
//
// The fonts are ours rather than LVGL's built-ins, and that is not a preference.
// `lv_font_montserrat_14` is generated from `-r 0x20-0x7F,0xB0,0x2022` — Latin
// only — so `×` draws as a box and every Cyrillic letter draws as a box. See
// assets/fonts/README.md.
const lv_font_t* pick_font(TypeRole role, std::uint16_t width_px)
{
    const bool large = width_px >= 400;
    switch (role) {
        case TypeRole::Display:
        case TypeRole::Title:
            return large ? &attadipa_montserrat_28 : &attadipa_montserrat_16;
        default:
            return large ? &attadipa_montserrat_20 : &attadipa_montserrat_14;
    }
}

}  // namespace

void build_boot_screen(const platform::HardwareInventory& inventory,
                       const core::CapabilityRegistry&    caps)
{
    g_inventory = &inventory;
    g_caps      = &caps;

    // Clean rather than create: the screen object outlives the language, and a
    // new screen on every switch would leak one per keypress.
    lv_obj_clean(lv_screen_active());

    const std::uint16_t width = inventory.display().width_px;
    const lv_font_t*    font  = pick_font(TypeRole::Body, width);
    const lv_font_t*    title = pick_font(TypeRole::Title, width);

    lv_obj_t* screen = lv_screen_active();
    // The screen's own default, so that a label created without an explicit font
    // still draws every codepoint, and so that the undrawable-glyph check has
    // something real to ask about. LVGL's default is its Latin-only Montserrat.
    lv_obj_set_style_text_font(screen, font, 0);
    lv_obj_set_style_bg_color(screen, paint(ColorRole::BackgroundPrimary), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, px(Space::Sm), 0);
    lv_obj_set_style_pad_row(screen, px(Space::Xs), 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* heading = lv_label_create(screen);
    lv_label_set_text(heading, tr(StringId::ProductName));
    lv_obj_set_style_text_font(heading, title, 0);
    lv_obj_set_style_text_color(heading, paint(ColorRole::AccentPrimary), 0);

    lv_obj_t* subtitle = lv_label_create(screen);
    lv_label_set_text_fmt(subtitle, tr(StringId::DiagnosticGeometry), inventory.board_name(),
                          inventory.display().width_px, inventory.display().height_px,
                          inventory.display().dpi());
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle, lv_pct(100));
    lv_obj_set_style_text_font(subtitle, font, 0);
    lv_obj_set_style_text_color(subtitle, paint(ColorRole::TextMuted), 0);

    // What an application may ask for. This is the list that exists to be read
    // by product code; the one below it is the list that exists to be read by
    // drivers, and the whole architecture is the claim that they are different.
    char counted[64];
    l10n::format_plural(counted, sizeof counted, PluralId::DiagnosticCapabilityCount,
                        core::kCapabilityCount);
    make_heading(screen, counted, font);
    for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
        const auto capability = static_cast<Capability>(i);
        const Availability availability = caps.availability(capability);
        make_row(screen, tr(label_of(capability)), tr(label_of(availability)),
                 role_for(availability), font);
    }

    make_heading(screen, tr(StringId::DiagnosticHardware), font);
    for (std::uint8_t i = 0; i < platform::kHardwareFeatureCount; ++i) {
        const auto feature = static_cast<HardwareFeature>(i);
        const platform::HardwareState state = inventory.state(feature);
        const ColorRole role =
            state == platform::HardwareState::Ready
                ? ColorRole::Success
                : (state == platform::HardwareState::Absent ? ColorRole::TextMuted
                                                            : ColorRole::Warning);
        make_row(screen, tr(label_of(feature)), tr(label_of(state)), role, font);
    }

    if (const platform::RadioInfo* radio = inventory.radio()) {
        make_heading(screen, tr(StringId::DiagnosticRadio), font);
        // A part number is not translated; only the absence of one is a word.
        const char* chip = chip_name(radio->chip);
        make_row(screen, tr(StringId::DiagnosticRadioChip),
                 chip != nullptr ? chip : tr(StringId::RadioChipUnknown),
                 chip != nullptr ? ColorRole::TextPrimary : ColorRole::Warning, font);
        make_row(screen, tr(StringId::DiagnosticRadioLora),
                 tr(radio->can_do_lora() ? StringId::Yes : StringId::No),
                 radio->can_do_lora() ? ColorRole::Success : ColorRole::TextMuted, font);
        make_row(screen, tr(StringId::DiagnosticRadioMeshcore),
                 tr(label_of(radio->meshcore)),
                 radio->meshcore == platform::MeshCoreSupport::Supported ? ColorRole::Success
                                                                        : ColorRole::Warning,
                 font);
    }
}

void set_theme(Theme theme)
{
    g_theme = theme;
}

void toggle_theme()
{
    g_theme = g_theme == Theme::Day ? Theme::Night : Theme::Day;
    std::printf("theme: %s\n", ui::name_of(g_theme));
    rebuild_boot_screen();
}

void rebuild_boot_screen()
{
    if (g_inventory != nullptr && g_caps != nullptr) {
        build_boot_screen(*g_inventory, *g_caps);
    }
}

int report_undrawable_glyphs(const lv_font_t* font, l10n::Locale locale)
{
    const l10n::Catalogue& catalogue = l10n::catalogue(locale);

    // Distinct codepoints, not occurrences: "Возможности" is one problem, not
    // eleven, and a screenful of duplicates is a report nobody finishes reading.
    std::set<std::uint32_t> reported;

    int missing = 0;
    const auto audit = [&](const char* text, const char* where) {
        if (text == nullptr) {
            return;
        }
        for (std::size_t i = 0; text[i] != '\0';) {
            const std::uint32_t codepoint = next_codepoint(text, i);
            if (codepoint == 0) {
                break;
            }
            // U+000A is layout, not a glyph: LVGL breaks a line on it rather
            // than drawing it, so asking the font for it produces a false
            // report about the one character in the string that is behaving.
            // Exactly one exemption — a stray tab in a label is a bug, and
            // tools/l10n/check_glyphs.py draws the same line in the same place.
            if (codepoint == 0x0A) {
                continue;
            }
            if (reported.count(codepoint) != 0) {
                continue;
            }
            lv_font_glyph_dsc_t dsc;
            if (!lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)) {
                reported.insert(codepoint);
                std::fprintf(stderr, "  U+%04X cannot be drawn — first seen in '%s'\n",
                             codepoint, where);
                ++missing;
            } else {
                lv_font_glyph_release_draw_data(&dsc);
            }
        }
    };

    for (std::uint16_t i = 0; i < l10n::kStringIdCount; ++i) {
        const auto id = static_cast<StringId>(i);
        audit(l10n::find(catalogue, id), l10n::string_id_name(id));
    }
    for (std::uint16_t i = 0; i < l10n::kPluralIdCount; ++i) {
        const auto id = static_cast<PluralId>(i);
        for (std::uint8_t c = 0; c < l10n::kPluralCategoryCount; ++c) {
            audit(l10n::find(catalogue, id, static_cast<l10n::PluralCategory>(c)),
                  l10n::plural_id_name(id));
        }
    }
    return missing;
}

}  // namespace attadipa::sim
