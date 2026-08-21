#include "boot_screen.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdio>
#include <set>

#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/catalogue.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/hardware_inventory.h"

namespace attadipa::sim {
namespace {

using core::Availability;
using core::Capability;
using l10n::PluralId;
using l10n::StringId;
using l10n::tr;
using platform::HardwareFeature;

// What the screen was last built from, so the locale-changed handler can build
// it again. Pointers rather than copies: the inventory and the registry are
// owned by main(), which outlives every screen.
const platform::HardwareInventory* g_inventory = nullptr;
const core::CapabilityRegistry*    g_caps      = nullptr;

// NOT DESIGN TOKENS. These five values exist so this diagnostic can be read at
// all; the real palette is docs/ui/DESIGN_SYSTEM.md, it is still marked
// *proposed*, no value in it has been shown on a panel, and open question A7
// records that the published brand art disagrees with it. Writing hex in UI
// code is exactly what T-009 exists to stop — which is why nothing outside this
// file does it.
constexpr std::uint32_t kInk       = 0x2F3A2E;
constexpr std::uint32_t kPaper     = 0xFFF6E8;
constexpr std::uint32_t kMuted     = 0x7A5E3A;
constexpr std::uint32_t kReady     = 0x6FA07A;
constexpr std::uint32_t kAttention = 0xFF8A40;

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

std::uint32_t colour_for(Availability availability)
{
    switch (availability) {
        case Availability::Ready:       return kReady;
        case Availability::Unsupported: return kMuted;
        default:                        return kAttention;
    }
}

lv_obj_t* make_row(lv_obj_t* parent, const char* left, const char* right, std::uint32_t colour,
                   const lv_font_t* font)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* left_label = lv_label_create(row);
    lv_label_set_text(left_label, left);
    lv_obj_set_style_text_font(left_label, font, 0);
    lv_obj_set_style_text_color(left_label, lv_color_hex(kInk), 0);

    lv_obj_t* right_label = lv_label_create(row);
    lv_label_set_text(right_label, right);
    lv_obj_set_style_text_font(right_label, font, 0);
    lv_obj_set_style_text_color(right_label, lv_color_hex(colour), 0);

    return row;
}

void make_heading(lv_obj_t* parent, const char* text, const lv_font_t* font)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kMuted), 0);
    lv_obj_set_style_pad_top(label, 8, 0);
}

// Montserrat comes in fixed sizes and the two panels differ by 3.6x in area.
// Picking by width is crude and is meant to be: the real answer is a type scale
// resolved per board (T-009), and this is scaffolding that must not be mistaken
// for it.
const lv_font_t* pick_font(std::uint16_t width_px)
{
    return width_px >= 400 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
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

    const lv_font_t* font  = pick_font(inventory.display().width_px);
    const lv_font_t* title = inventory.display().width_px >= 400 ? &lv_font_montserrat_28
                                                                 : &lv_font_montserrat_16;

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(kPaper), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 10, 0);
    lv_obj_set_style_pad_row(screen, 2, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* heading = lv_label_create(screen);
    lv_label_set_text(heading, tr(StringId::ProductName));
    lv_obj_set_style_text_font(heading, title, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(kAttention), 0);

    lv_obj_t* subtitle = lv_label_create(screen);
    lv_label_set_text_fmt(subtitle, tr(StringId::DiagnosticGeometry), inventory.board_name(),
                          inventory.display().width_px, inventory.display().height_px,
                          inventory.display().dpi());
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle, lv_pct(100));
    lv_obj_set_style_text_font(subtitle, font, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(kMuted), 0);

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
        make_row(screen, core::to_string(capability), core::to_string(availability),
                 colour_for(availability), font);
    }

    make_heading(screen, tr(StringId::DiagnosticHardware), font);
    for (std::uint8_t i = 0; i < platform::kHardwareFeatureCount; ++i) {
        const auto feature = static_cast<HardwareFeature>(i);
        const platform::HardwareState state = inventory.state(feature);
        const std::uint32_t colour =
            state == platform::HardwareState::Ready
                ? kReady
                : (state == platform::HardwareState::Absent ? kMuted : kAttention);
        make_row(screen, platform::to_string(feature), platform::to_string(state), colour, font);
    }

    if (const platform::RadioInfo* radio = inventory.radio()) {
        make_heading(screen, tr(StringId::DiagnosticRadio), font);
        make_row(screen, tr(StringId::DiagnosticRadioChip), platform::to_string(radio->chip),
                 radio->chip == platform::RadioChip::Unknown ? kAttention : kInk, font);
        make_row(screen, tr(StringId::DiagnosticRadioLora),
                 tr(radio->can_do_lora() ? StringId::Yes : StringId::No),
                 radio->can_do_lora() ? kReady : kMuted, font);
        make_row(screen, tr(StringId::DiagnosticRadioMeshcore),
                 platform::to_string(radio->meshcore),
                 radio->meshcore == platform::MeshCoreSupport::Supported ? kReady : kAttention,
                 font);
    }
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
