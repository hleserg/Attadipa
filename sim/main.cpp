#include <cstring>
#include <cstdio>
#include <ctime>
#include <optional>
#include <vector>

#include "lvgl.h"

#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/hardware_inventory.h"
#include "attadipa/version.h"

#include "attadipa/apps/clock.h"
#include "attadipa/core/input.h"
#include "attadipa/debug/bridge.h"
#include "attadipa/ui/clock_face.h"
#include "attadipa/ui/nav_face.h"
#include "attadipa/ui/provision_face.h"
#include "attadipa/ui/tokens.h"

#include "boot_screen.h"
#include "debug_server.h"
#include "diagnostic_screen.h"
#include "options.h"
#include "png_writer.h"
#include "remote_input.h"
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

ui::ClockFace g_clock_face;
ui::NavFace g_nav_face;
apps::NavState g_nav_state;
ui::NavFaceConfig g_nav_config;
ui::ClockFaceConfig g_clock_config;
apps::ClockState g_clock_state;
bool g_clock_active = false;
bool g_clock_live = false;

// A board that takes whatever it is given. The simulator has no RTC and no
// radio to hand a value to, so the seam ends here, out loud.
//
// The passkey is answered the way the board answers it and not one step
// earlier: taken now, terminal on a later tick. There is no worker behind it
// here, so the wait is one poll long -- but a screen that never saw `Pending`
// in the simulator is a screen nobody looked at in the state the watch spends
// real milliseconds in (#416).
struct AcceptingProvisioner final : core::Provisioner {
  core::ProvisionOutcome
  set_wall_clock(const core::WallClockEntry &entry) override {
    std::printf("provision: clock %lld s, offset %d min\n",
                static_cast<long long>(entry.utc_seconds),
                static_cast<int>(entry.timezone_offset_minutes));
    return core::ProvisionOutcome::Accepted;
  }
  core::ProvisionOutcome set_mesh_passkey(std::uint32_t passkey) override {
    std::printf("provision: passkey %06u queued\n",
                static_cast<unsigned>(passkey));
    in_flight_ = true;
    return core::ProvisionOutcome::Pending;
  }
  core::ProvisionOutcome mesh_passkey_outcome() override {
    if (!in_flight_) {
      // Nothing outstanding. `Failed` and not `Pending`, or a screen that asks
      // without having been told `Pending` waits for ever.
      return core::ProvisionOutcome::Failed;
    }
    in_flight_ = false;
    std::printf("provision: passkey armed\n");
    return core::ProvisionOutcome::Accepted;
  }

  // Pinned to a made-up node until it is forgotten, so the entry screen's
  // node field has something to show. The key is not a real node's.
  bool mesh_node(core::MeshPeerId &out) override {
    if (!pinned_) {
      return false;
    }
    out = core::MeshPeerId{};
    out.public_key[0] = 0xA1;
    out.public_key[1] = 0xB2;
    out.public_key[2] = 0xC3;
    out.public_key[3] = 0xD4;
    return true;
  }
  core::ProvisionOutcome forget_mesh_node() override {
    if (!pinned_) {
      return core::ProvisionOutcome::Rejected;
    }
    std::printf("provision: forget node queued\n");
    forget_in_flight_ = true;
    return core::ProvisionOutcome::Pending;
  }
  core::MeshForgetOutcome mesh_forget_outcome() override {
    if (!forget_in_flight_) {
      return core::MeshForgetOutcome::BondKept;
    }
    forget_in_flight_ = false;
    pinned_ = false;
    std::printf("provision: node forgotten\n");
    return core::MeshForgetOutcome::Forgotten;
  }

private:
  bool in_flight_ = false;
  bool forget_in_flight_ = false;
  bool pinned_ = true;
};

void rebuild_clock_screen();

AcceptingProvisioner g_provisioner;
std::optional<apps::ProvisioningEntry> g_entry;
ui::ProvisionFace g_provision_face;
ui::ProvisionFaceConfig g_provision_config;
bool g_provision_active = false;

void rebuild_provision_screen() {
  g_provision_config.locale = l10n::locale();
  g_provision_face.build(lv_screen_active(), g_provision_config, *g_entry);
}

ui::ProvisionFaceConfig provision_config_for(const ui::ClockFaceConfig &clock) {
  return {clock.width_px, clock.height_px, clock.theme,
          clock.pixel_cost, clock.metrics,  l10n::locale()};
}

// The same loop the board runs: Done shows for a moment, then the clock is
// back. Here it is an LVGL timer that deletes itself; there it is the
// clock's own refresh timer counting ticks.
void leave_provisioning(lv_timer_t *timer) {
  // The board polls the passkey on its clock tick; here it is this timer, and
  // it is the only thing that can end the wait. The tick that hears the answer
  // draws it and stops there: leaving on the same tick would put Done on the
  // screen for no frames at all. The board spends three ticks on it.
  if (g_entry->poll()) {
    g_provision_face.update();
    return;
  }
  if (!g_entry->finished()) {
    return;
  }
  lv_timer_delete(timer);
  g_provision_face.clear();
  g_entry.reset();
  g_provision_active = false;
  g_clock_active = true;
  l10n::set_locale_changed_handler(rebuild_clock_screen);
  rebuild_clock_screen();
}

// A long press on the clock opens the entry screen, as on the board.
void on_long_press(lv_event_t *) {
  if (!g_clock_active) {
    return;
  }
  g_clock_face.clear();
  g_clock_active = false;
  g_provision_active = true;
  g_provision_config = provision_config_for(g_clock_config);
  g_entry.emplace(g_provisioner);
  l10n::set_locale_changed_handler(rebuild_provision_screen);
  rebuild_provision_screen();
  lv_timer_create(leave_provisioning,
                  ui::milliseconds_of(ui::Motion::Slow) * 8U, nullptr);
}

// The navigation readout's staged scenarios.
//
// Coordinates are the same deliberately-nowhere place the position tests use —
// half a degree north, one degree east, in the Gulf of Guinea. No real location
// belonging to anybody appears in this repository.
//
// This exists because a screenshot of `Ready` proves nothing about the six
// other things the readout can say, and those six are where a number appears
// that should not have.
bool stage_nav_scenario(const char *name) {
  constexpr core::Position kHere{5000000, 10000000};
  constexpr core::Position kTarget{5100000, 10160000};  // 2.1 km, bearing 058

  core::LocationState own;
  own.availability = core::Availability::Ready;
  own.has_position = true;
  own.position.value = kHere;
  own.validity = core::PositionValidity::Valid;
  own.fix_type = core::FixType::ThreeD;
  own.source = core::PositionSource::LocalGnss;
  own.receiver = core::ReceiverPresence::Running;

  // What the MeshCore node link produces: a coordinate, an arrival age, and no
  // fix type at all — so `classify()` answers NoFix and goes on answering it.
  core::LocationState node;
  node.availability = core::Availability::Ready;
  node.has_position = true;
  node.position.value = kTarget;
  node.position.age_at_us_ms = 3000;
  node.validity = core::PositionValidity::NoFix;
  node.fix_type = core::FixType::Unknown;
  node.source = core::PositionSource::NodeGnss;

  g_nav_state = apps::NavState{};
  if (std::strcmp(name, "ready") == 0) {
    g_nav_state.own = own;
    g_nav_state.target = node;
  } else if (std::strcmp(name, "waiting") == 0) {
    g_nav_state.target = node;
  } else if (std::strcmp(name, "no-fix") == 0) {
    g_nav_state.own.availability = core::Availability::Ready;
    g_nav_state.own.receiver = core::ReceiverPresence::Running;
    g_nav_state.own.fix_type = core::FixType::NoFix;
    g_nav_state.target = node;
  } else if (std::strcmp(name, "own-stale") == 0) {
    own.validity = core::PositionValidity::Stale;
    g_nav_state.own = own;
    g_nav_state.target = node;
  } else if (std::strcmp(name, "node-unavailable") == 0) {
    node.availability = core::Availability::Unreachable;
    node.position.age_at_us_ms = 45000;
    g_nav_state.own = own;
    g_nav_state.target = node;
  } else if (std::strcmp(name, "node-unknown") == 0) {
    g_nav_state.own = own;
    g_nav_state.target.availability = core::Availability::Ready;
  } else if (std::strcmp(name, "node-stale") == 0) {
    node.position.age_at_us_ms = 300000;
    g_nav_state.own = own;
    g_nav_state.target = node;
  } else if (std::strcmp(name, "arrived") == 0) {
    node.position.value = kHere;
    g_nav_state.own = own;
    g_nav_state.target = node;
  } else if (std::strcmp(name, "far") == 0) {
    node.position.value = core::Position{900000000, 10000000};
    g_nav_state.own = own;
    g_nav_state.target = node;
  } else {
    std::fprintf(stderr,
                 "unknown --nav-state \"%s\"; one of: ready waiting no-fix "
                 "own-stale node-unavailable node-unknown node-stale arrived "
                 "far\n",
                 name);
    return false;
  }
  return true;
}

void rebuild_nav_screen() {
  g_nav_face.build(lv_screen_active(), g_nav_config,
                   apps::format_navigation(g_nav_state));
}

void rebuild_clock_screen() {
  g_clock_state.locale = l10n::locale();
  g_clock_face.build(
      lv_screen_active(), g_clock_config,
      apps::format_clock(g_clock_state, g_clock_config.width_px < 300));
}

void refresh_clock(lv_timer_t *timer) {
  if (g_clock_live) {
    g_clock_state.time.value.unix_seconds =
        static_cast<std::int64_t>(std::time(nullptr));
  }
  g_clock_state.locale = l10n::locale();
  g_clock_face.update(
      apps::format_clock(g_clock_state, g_clock_config.width_px < 300));
  lv_timer_set_period(timer, apps::clock_manifest().tick_period.value);
}

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

// L toggles the language, T the theme. Keypresses rather than a menu because
// the Settings screen does not exist yet (T-038) and the acceptance criterion —
// "switches at runtime without a reboot" — is about the mechanism, not about
// where the switch lives.
void on_screen_key(lv_event_t *event) {
  (void)event;
  const std::uint32_t key = lv_indev_get_key(lv_indev_active());
  if (key == 'l' || key == 'L') {
    const l10n::Locale next = l10n::locale() == l10n::Locale::En
                                  ? l10n::Locale::Ru
                                  : l10n::Locale::En;
    std::printf("locale: %s\n", l10n::to_string(next));
    l10n::set_locale(next);
  }
  if (key == 't' || key == 'T') {
    if (g_clock_active) {
      g_clock_config.theme = g_clock_config.theme == ui::Theme::Day
                                 ? ui::Theme::Night
                                 : ui::Theme::Day;
      rebuild_clock_screen();
    } else if (g_provision_active) {
      g_provision_config.theme = g_provision_config.theme == ui::Theme::Day
                                     ? ui::Theme::Night
                                     : ui::Theme::Day;
      rebuild_provision_screen();
    } else {
      attadipa::sim::toggle_theme();
    }
  }
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
  lv_obj_add_event_cb(lv_screen_active(), on_screen_key, LV_EVENT_KEY, nullptr);

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
    if (!stage_nav_scenario(options.nav_state)) {
      return 2;
    }
    g_nav_config = {
        options.board.display.width_px,
        options.board.display.height_px,
        options.theme,
        options.board.display.technology == platform::PanelTechnology::Amoled
            ? ui::PixelCost::PerPixel
            : ui::PixelCost::Fixed,
        ui::Metrics::for_dpi(options.board.display.dpi()),
    };
    l10n::set_locale_changed_handler(rebuild_nav_screen);
    rebuild_nav_screen();
  } else if (options.clock_screen || options.provision_screen) {
    g_clock_active = true;
    g_clock_live = !options.clock_time_set;
    g_clock_config = {
        options.board.display.width_px,
        options.board.display.height_px,
        options.theme,
        options.board.display.technology == platform::PanelTechnology::Amoled
            ? ui::PixelCost::PerPixel
            : ui::PixelCost::Fixed,
        ui::Metrics::for_dpi(options.board.display.dpi()),
    };
    g_clock_state.time = {
        options.clock_time_set
            ? options.clock_time
            : core::WallTime{static_cast<std::int64_t>(std::time(nullptr))},
        0,
        0,
        options.clock_validity,
    };
    g_clock_state.availability = options.clock_availability;
    g_clock_state.touch_absent = options.touch_absent;
    g_clock_state.locale = options.locale;
    g_clock_state.mode =
        options.child_mode ? apps::ClockMode::Child : apps::ClockMode::Adult;
    l10n::set_locale_changed_handler(rebuild_clock_screen);
    rebuild_clock_screen();
    if (g_clock_live) {
      lv_timer_create(refresh_clock, apps::clock_manifest().tick_period.value,
                      nullptr);
    }
    lv_obj_add_event_cb(lv_screen_active(), on_long_press,
                        LV_EVENT_LONG_PRESSED, nullptr);
    if (options.provision_screen) {
      // Straight to the entry screen, for a screenshot that does not need a
      // finger held on the clock first.
      lv_obj_send_event(lv_screen_active(), LV_EVENT_LONG_PRESSED, nullptr);
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
