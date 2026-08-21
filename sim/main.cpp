#include <cstdio>
#include <vector>

#include "lvgl.h"

#include "firefly/core/capability_registry.h"
#include "firefly/platform/hardware_inventory.h"
#include "firefly/version.h"

#include "boot_screen.h"
#include "options.h"
#include "png_writer.h"

// The Firefly OS desktop simulator.
//
// It is a first-class target (final §57), not a convenience: UI work does not
// wait on a board, and neither does design review at both geometries. This
// file is the composition root, which is why it is the one place allowed to
// see both firefly::platform and firefly::core — the same seat main() has on
// the device. Nothing under apps/ has it.

namespace {

using namespace firefly;

// A simulated bring-up. On a board this is drivers coming up over I2C, SPI and
// a set of PMU rails; here it is a loop, and the loop is honest about what it
// is. What it is *not* is a claim about hardware: nothing in this process has
// touched a bus, and no result it produces may be recorded as HARDWARE-VERIFIED.
void bring_up(platform::ProfileInventory& inventory)
{
    for (std::uint8_t i = 0; i < platform::kHardwareFeatureCount; ++i) {
        const auto feature = static_cast<platform::HardwareFeature>(i);
        if (inventory.present(feature)) {
            inventory.set_state(feature, platform::HardwareState::Ready);
        }
    }
}

// LVGL stores RGB888 as blue, green, red (lv_color_t in src/misc/lv_color.h).
// PNG wants red, green, blue. One swap, in one place, with the reason written
// down — the alternative is a screenshot that looks subtly wrong and a day
// spent blaming the palette.
bool save_screenshot(const char* path)
{
    lv_draw_buf_t* snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (snapshot == nullptr) {
        std::fprintf(stderr, "could not take a snapshot\n");
        return false;
    }

    const std::uint32_t width  = snapshot->header.w;
    const std::uint32_t height = snapshot->header.h;
    const std::uint32_t stride = snapshot->header.stride;

    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = snapshot->data + static_cast<std::size_t>(y) * stride;
        std::uint8_t* out = rgb.data() + static_cast<std::size_t>(y) * width * 3;
        for (std::uint32_t x = 0; x < width; ++x) {
            out[x * 3 + 0] = row[x * 3 + 2];  // red
            out[x * 3 + 1] = row[x * 3 + 1];  // green
            out[x * 3 + 2] = row[x * 3 + 0];  // blue
        }
    }

    const bool ok = firefly::sim::write_png_rgb(path, rgb.data(), width, height);
    lv_draw_buf_destroy(snapshot);

    if (ok) {
        std::printf("screenshot: %s (%u x %u)\n", path, width, height);
    } else {
        std::fprintf(stderr, "could not write %s\n", path);
    }
    return ok;
}

core::NodeLink node_link_for(bool attached)
{
    core::NodeLink link;
    link.bound      = attached;
    link.reachable  = attached;
    link.compatible = attached;
    link.provides   = core::capability_bit(core::Capability::Position) |
                    core::capability_bit(core::Capability::Heading) |
                    core::capability_bit(core::Capability::MeshMessaging) |
                    core::capability_bit(core::Capability::Time);
    return link;
}

}  // namespace

int main(int argc, char** argv)
{
    firefly::sim::Options options;
    switch (firefly::sim::parse_options(argc, argv, options)) {
        case firefly::sim::ParseResult::Exit:  return 0;
        case firefly::sim::ParseResult::Error: return 2;
        case firefly::sim::ParseResult::Ok:    break;
    }

    platform::ProfileInventory inventory(options.board);
    if (options.bring_up) {
        bring_up(inventory);
    }

    core::CapabilityRegistry caps(inventory);
    caps.set_node_link(node_link_for(options.node_attached));

    std::printf("Firefly OS %s simulator — %s, %u x %u, %u dpi%s\n", FIREFLY_VERSION_STRING,
                options.board.name, options.board.display.width_px,
                options.board.display.height_px, options.board.display.dpi(),
                options.node_attached ? ", node attached" : "");

    lv_init();

    lv_display_t* display =
        lv_sdl_window_create(options.board.display.width_px, options.board.display.height_px);
    if (display == nullptr) {
        std::fprintf(stderr, "could not open a window. Headless? Try SDL_VIDEODRIVER=dummy\n");
        return 1;
    }

    // Resolution comes from the board and so does density. LV_DPX() and the
    // spacing tokens that will sit on top of it (T-009) need the second one:
    // 8 px is not the same physical distance on a 1.3-inch panel and a
    // 2.06-inch one, and a token system that ignores that produces a design
    // that only looks right on whichever board it was drawn on.
    lv_display_set_dpi(display, options.board.display.dpi());
    lv_sdl_window_set_title(display, options.board.name);
    if (options.zoom != 1.0F) {
        lv_sdl_window_set_zoom(display, options.zoom);
    }

    lv_sdl_mouse_create();     // the finger
    lv_sdl_keyboard_create();  // the buttons

    firefly::sim::build_boot_screen(inventory, caps);

    if (options.screenshot != nullptr) {
        // One handler pass first, so layout and the theme have run. A snapshot
        // of an unlaid-out screen is a picture of nothing, and it is not
        // obviously a picture of nothing.
        lv_timer_handler();
        if (!save_screenshot(options.screenshot)) {
            return 1;
        }
    }

    if (options.frames == 0) {
        for (;;) {
            const std::uint32_t next = lv_timer_handler();
            lv_delay_ms(next == LV_NO_TIMER_READY ? 5 : (next > 50 ? 50 : next));
        }
    }

    for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
        lv_timer_handler();
        lv_delay_ms(5);
    }
    std::printf("rendered %u frames, exiting\n", options.frames);

    return 0;
}
