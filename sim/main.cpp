#include <cstdio>
#include <ctime>
#include <vector>

#include "lvgl.h"

#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/hardware_inventory.h"
#include "attadipa/version.h"

#include "attadipa/apps/clock.h"
#include "attadipa/core/input.h"
#include "attadipa/debug/bridge.h"

#include "boot_screen.h"
#include "clock_screen.h"
#include "debug_server.h"
#include "diagnostic_screen.h"
#include "nav_screen.h"
#include "options.h"
#include "png_writer.h"
#include "remote_input.h"
#include "review_keys.h"
#include "screen_source.h"

// The Attadipa desktop simulator.
//
// It is a first-class target (final §57), not a convenience: UI work does not
// wait on a board, and neither does design review at both geometries. This
// file is the composition root, which is why it is the one place allowed to
// see both attadipa::platform and attadipa::core — the same seat main() has on
// the device. Nothing under apps/ has it.

namespace {

using namespace attadipa;

// A simulated bring-up. On a board this is drivers coming up over I2C, SPI and
// a set of PMU rails; here it is a loop, and the loop is honest about what it
// is. What it is *not* is a claim about hardware: nothing in this process has
// touched a bus, and no result it produces may be recorded as
// HARDWARE-VERIFIED.
void bring_up(platform::ProfileInventory &inventory) {
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
bool save_screenshot(const char *path) {
  lv_draw_buf_t *snapshot =
      lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
  if (snapshot == nullptr) {
    std::fprintf(stderr, "could not take a snapshot\n");
    return false;
  }

  const std::uint32_t width = snapshot->header.w;
  const std::uint32_t height = snapshot->header.h;
  const std::uint32_t stride = snapshot->header.stride;

  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::uint8_t *row =
        snapshot->data + static_cast<std::size_t>(y) * stride;
    std::uint8_t *out = rgb.data() + static_cast<std::size_t>(y) * width * 3;
    for (std::uint32_t x = 0; x < width; ++x) {
      out[x * 3 + 0] = row[x * 3 + 2]; // red
      out[x * 3 + 1] = row[x * 3 + 1]; // green
      out[x * 3 + 2] = row[x * 3 + 0]; // blue
    }
  }

  const bool ok = attadipa::sim::write_png_rgb(path, rgb.data(), width, height);
  lv_draw_buf_destroy(snapshot);

  if (ok) {
    std::printf("screenshot: %s (%u x %u)\n", path, width, height);
  } else {
    std::fprintf(stderr, "could not write %s\n", path);
  }
  return ok;
}

core::NodeLink node_link_for(bool attached) {
  core::NodeLink link;
  link.bound = attached;
  link.reachable = attached;
  link.compatible = attached;
  link.provides = core::capability_bit(core::Capability::Position) |
                  core::capability_bit(core::Capability::Heading) |
                  core::capability_bit(core::Capability::MeshMessaging) |
                  core::capability_bit(core::Capability::Time);
  return link;
}

// ADR-0010 §3: a missing string is announced, never swallowed. On a device this
// goes to the log; here it goes to stderr, where a test run will show it.
void report_missing_string(l10n::Locale requested, const char *identifier) {
  std::fprintf(stderr, "l10n: '%s' has no %s text — showing English\n",
               identifier, l10n::to_string(requested));
}

} // namespace

int main(int argc, char **argv) {
  attadipa::sim::Options options;
  switch (attadipa::sim::parse_options(argc, argv, options)) {
  case attadipa::sim::ParseResult::Exit:
    return 0;
  case attadipa::sim::ParseResult::Error:
    return 2;
  case attadipa::sim::ParseResult::Ok:
    break;
  }

  platform::ProfileInventory inventory(options.board);
  if (options.bring_up) {
    bring_up(inventory);
  }

  core::CapabilityRegistry caps(inventory);
  caps.set_node_link(node_link_for(options.node_attached));

  std::printf("Attadipa %s simulator — %s, %u x %u, %u dpi%s\n",
              ATTADIPA_VERSION_STRING, options.board.name,
              options.board.display.width_px, options.board.display.height_px,
              options.board.display.dpi(),
              options.node_attached ? ", node attached" : "");

  lv_init();

  lv_display_t *display = lv_sdl_window_create(options.board.display.width_px,
                                               options.board.display.height_px);
  if (display == nullptr) {
    std::fprintf(
        stderr,
        "could not open a window. Headless? Try SDL_VIDEODRIVER=dummy\n");
    return 1;
  }

  // Resolution comes from the board and so does density. LV_DPX() and the
  // spacing tokens that will sit on top of it (T-009) need the second one:
  // 8 px is not the same physical distance on the T-Watch's 1.54-inch panel
  // and a 2.06-inch one, and a token system that ignores that produces a
  // design that only looks right on whichever board it was drawn on.
  lv_display_set_dpi(display, options.board.display.dpi());
  lv_sdl_window_set_title(display, options.board.name);
  if (options.zoom != 1.0F) {
    lv_sdl_window_set_zoom(display, options.zoom);
  }

  lv_sdl_mouse_create();                           // the finger
  lv_indev_t *keyboard = lv_sdl_keyboard_create(); // the buttons

  // The screen itself takes key events, so L works without anything focused.
  lv_group_t *group = lv_group_create();
  lv_indev_set_group(keyboard, group);
  lv_group_add_obj(group, lv_screen_active());
  lv_obj_add_event_cb(lv_screen_active(), attadipa::sim::on_screen_key,
                      LV_EVENT_KEY, nullptr);

  l10n::set_missing_string_handler(report_missing_string);
  l10n::set_locale_changed_handler(attadipa::sim::rebuild_boot_screen);
  l10n::set_locale(options.locale);

  attadipa::sim::set_theme(options.theme);
  if (options.diagnostic_screen) {
    // The test pattern replaces the capability screen rather than sitting
    // beside it: what a screenshot has to show is the whole panel, and a
    // pattern in half of it cannot reveal a crop.
    l10n::set_locale_changed_handler(attadipa::sim::rebuild_diagnostic_screen);
    attadipa::sim::build_diagnostic_screen(options.board);
  } else if (options.nav_screen) {
    if (!attadipa::sim::stage_nav_scenario(options.nav_state)) {
      return 2;
    }
    l10n::set_locale_changed_handler(attadipa::sim::rebuild_nav_screen);
    attadipa::sim::build_nav_screen(options.board, options.theme);
  } else if (options.clock_screen || options.provision_screen) {
    apps::ClockState state;
    state.time = {
        options.clock_time_set
            ? options.clock_time
            : core::WallTime{static_cast<std::int64_t>(std::time(nullptr))},
        0,
        0,
        options.clock_validity,
    };
    state.availability = options.clock_availability;
    state.touch_absent = options.touch_absent;
    state.locale = options.locale;
    state.mode =
        options.child_mode ? apps::ClockMode::Child : apps::ClockMode::Adult;
    attadipa::sim::build_clock_screen(options.board, options.theme, state,
                                      !options.clock_time_set);
    if (options.provision_screen) {
      // Straight to the entry screen, for a screenshot that does not need a
      // finger held on the clock first.
      attadipa::sim::enter_provisioning();
    }
  } else {
    attadipa::sim::build_boot_screen(inventory, caps);
  }

  // A box on a screen is a defect, and this is where it is refused.
  //
  // It used to be a warning, because it was unfixable: LVGL's built-in font
  // and unscii and both are Latin — Montserrat's own header says
  // `-r 0x20-0x7F,0xB0,0x2022` — so the Russian catalogue was not merely hard
  // to read in this build, it was undrawable, and the honest thing was to name
  // the codepoints rather than render boxes and leave a reviewer to guess.
  //
  // T-083 removed the reason. The simulator now links generated subsets that
  // cover all 181 codepoints in `tools/font/charset.py`, so an undrawable
  // codepoint is no longer a known limitation, it is a regression — and the
  // process exits non-zero, which makes it a test failure rather than a line
  // of output somebody scrolls past.
  //
  // **Both catalogues, not the current one.** A check that passes in English
  // and fails in Russian is a check that reports the locale the reviewer
  // happened to start in. ADR-0010 §1: a Latin-only font is not a font missing
  // some characters, it is a different artefact.
  {
    const lv_font_t *font =
        lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN);
    const l10n::Locale started_in = l10n::locale();
    int undrawable = 0;
    for (l10n::Locale locale : {l10n::Locale::En, l10n::Locale::Ru}) {
      undrawable += attadipa::sim::report_undrawable_glyphs(font, locale);
    }
    l10n::set_locale(started_in);
    if (undrawable > 0) {
      std::fprintf(
          stderr,
          "l10n: %d codepoint(s) cannot be drawn by the font in this build, "
          "so this screen would show boxes. See assets/fonts/README.md and "
          "docs/adr/0010-localization.md §1.\n",
          undrawable);
      return 1;
    }
  }

  // The input layer, and the debug channel above it.
  //
  // Created here because this is the composition root: it is the one place
  // allowed to see both the board and the services, exactly as main() is on a
  // device. The frame buffer is sized from the board's panel and lives here
  // rather than inside the bridge, so a build that does not ask for the debug
  // channel does not carry it -- which is the whole argument in section 9 of
  // the request, and the reason RESOURCE_BUDGET wants pools declared where
  // somebody can see them.
  core::InputQueue input_queue;
  core::InputState input_state;
  attadipa::sim::LvglScreenSource screen_source(options.board);
  std::vector<std::uint8_t> frame_buffer;
  attadipa::sim::DebugServer debug_server;

  attadipa::sim::remote_input_attach(input_queue);
  if (options.diagnostic_screen) {
    attadipa::sim::remote_input_set_button_listener(
        attadipa::sim::diagnostic_screen_on_button);
    attadipa::sim::remote_input_set_pointer_listener(
        attadipa::sim::diagnostic_screen_on_pointer);
  }

  if (options.debug_socket != nullptr) {
    frame_buffer.resize(screen_source.frame_bytes());
  }
  debug::Bridge bridge(input_queue, input_state, screen_source,
                       frame_buffer.empty() ? nullptr : frame_buffer.data(),
                       frame_buffer.size());

  if (options.debug_socket != nullptr &&
      !debug_server.listen(options.debug_socket)) {
    return 1;
  }

  if (options.screenshot != nullptr) {
    // One handler pass first, so layout and the theme have run. A snapshot
    // of an unlaid-out screen is a picture of nothing, and it is not
    // obviously a picture of nothing.
    lv_timer_handler();
    if (!save_screenshot(options.screenshot)) {
      return 1;
    }
  }

  // `remote_input_pump` before `lv_timer_handler`, always. LVGL's read timer
  // fires inside the handler, so an event delivered after it waits for the
  // next one -- and that is 33 ms (`LV_DEF_REFR_PERIOD`), not one iteration
  // of this loop. The pump therefore hands LVGL a *queue* of transitions
  // rather than a single state; see the long note in `sim/remote_input.cpp`
  // for why a single one silently merged two taps into one click.
  //
  // The debug server is polled after, so that a screenshot taken this frame
  // is of what was just drawn rather than of the frame before.
  if (options.frames == 0) {
    for (;;) {
      attadipa::sim::remote_input_pump();
      const std::uint32_t next = lv_timer_handler();
      debug_server.poll(lv_tick_get(), bridge);
      // A connected client is served promptly; an idle simulator still
      // idles. Polling at the interface's own pace while a transfer is
      // running would make a 600 kB screenshot take a minute.
      const std::uint32_t cap = debug_server.has_client() ? 5u : 50u;
      lv_delay_ms(next == LV_NO_TIMER_READY ? 5 : (next > cap ? cap : next));
    }
  }

  for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
    attadipa::sim::remote_input_pump();
    lv_timer_handler();
    debug_server.poll(lv_tick_get(), bridge);
    lv_delay_ms(5);
  }
  std::printf("rendered %u frames, exiting\n", options.frames);

  return 0;
}
