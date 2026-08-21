#include "boot_screen.h"

#include "lvgl.h"

#include "firefly/core/capability_registry.h"
#include "firefly/platform/hardware_inventory.h"

namespace firefly::sim {
namespace {

using core::Availability;
using core::Capability;
using platform::HardwareFeature;

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
    lv_label_set_text(heading, "Firefly OS");
    lv_obj_set_style_text_font(heading, title, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(kAttention), 0);

    lv_obj_t* subtitle = lv_label_create(screen);
    lv_label_set_text_fmt(subtitle, "%s\n%u x %u  %u dpi", inventory.board_name(),
                          inventory.display().width_px, inventory.display().height_px,
                          inventory.display().dpi());
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle, lv_pct(100));
    lv_obj_set_style_text_font(subtitle, font, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(kMuted), 0);

    // What an application may ask for. This is the list that exists to be read
    // by product code; the one below it is the list that exists to be read by
    // drivers, and the whole architecture is the claim that they are different.
    make_heading(screen, "Capabilities", font);
    for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
        const auto capability = static_cast<Capability>(i);
        const Availability availability = caps.availability(capability);
        make_row(screen, core::to_string(capability), core::to_string(availability),
                 colour_for(availability), font);
    }

    make_heading(screen, "Hardware", font);
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
        make_heading(screen, "Radio", font);
        make_row(screen, "chip", platform::to_string(radio->chip),
                 radio->chip == platform::RadioChip::Unknown ? kAttention : kInk, font);
        make_row(screen, "LoRa", radio->can_do_lora() ? "yes" : "no",
                 radio->can_do_lora() ? kReady : kMuted, font);
        make_row(screen, "MeshCore", platform::to_string(radio->meshcore),
                 radio->meshcore == platform::MeshCoreSupport::Supported ? kReady : kAttention,
                 font);
    }
}

}  // namespace firefly::sim
