#include "clock_screen.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>

#include "lvgl.h"

#include "attadipa/apps/app_registry.h"
#include "attadipa/apps/clock.h"
#include "attadipa/apps/provisioning.h"
#include "attadipa/apps/brightness.h"
#include "attadipa/core/provisioning.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/ui/clock_face.h"
#include "attadipa/ui/metrics.h"
#include "attadipa/ui/provision_face.h"
#include "attadipa/ui/tokens.h"

#include "review_keys.h"
#include "attadipa/ui/status_frame.h"
#include "attadipa/ui/settings_face.h"
#include "mesh_screen.h"

namespace attadipa::sim {
namespace {

using namespace attadipa;

ui::StatusFrame g_frame;
ui::ClockFace g_clock_face;
ui::ClockFaceConfig g_clock_config;
apps::ClockState g_clock_state;
bool g_clock_active = false;
bool g_clock_live = false;

// Volatile desktop state: actual persistence and panel output are native
// acceptance. This port exercises the same editing and rendering callers.
struct SimBrightness final : apps::BrightnessPort {
  std::uint8_t stored = 5;
  apps::BrightnessRead load(std::uint8_t &value) override {
    value = stored;
    return apps::BrightnessRead::Present;
  }
  bool apply(std::uint8_t) override { return true; }
  apps::BrightnessWrite store(std::uint8_t value) override {
    stored = value;
    return apps::BrightnessWrite::Saved;
  }
  void restart() override {} // This port cannot have an uncertain flash write.
} g_brightness_port;
apps::BrightnessSettings g_brightness(g_brightness_port, 5, 5, 5);
ui::SettingsFace g_settings_face;

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

AcceptingProvisioner g_provisioner;
std::optional<apps::ProvisioningEntry> g_entry;
ui::ProvisionFace g_provision_face;
ui::ProvisionFaceConfig g_provision_config;

void rebuild_provision_screen() {
  g_provision_config.locale = l10n::locale();
  g_provision_face.clear();
  g_frame.build(lv_screen_active(), {g_provision_config.width_px,
                g_provision_config.height_px, g_provision_config.theme,
                g_provision_config.pixel_cost, g_provision_config.metrics});
  auto content = g_provision_config;
  content.height_px = g_frame.content_height();
  g_provision_face.build(g_frame.content(), content, *g_entry);
  g_frame.restore_content_geometry();
  g_frame.update(apps::format_mesh(staged_mesh_status(), l10n::locale()));
}

// What `T` does to each of the two faces this file owns the config of.
//
// Registered where the face is put on the panel, beside the line that says
// what `L` does to it — see `sim/review_keys.h` for why the key needs an owner
// at all rather than an `if` ladder over whichever screens a file remembers.
ui::Theme toggle_clock_theme() {
  g_clock_config.theme = g_clock_config.theme == ui::Theme::Day
                             ? ui::Theme::Night
                             : ui::Theme::Day;
  rebuild_clock_screen();
  return g_clock_config.theme;
}

void rebuild_settings_screen() {
  const auto page = g_settings_face.page();
  g_settings_face.clear();
  g_frame.build(lv_screen_active(), {g_clock_config.width_px,
                g_clock_config.height_px, g_clock_config.theme,
                g_clock_config.pixel_cost, g_clock_config.metrics});
  auto content = g_clock_config;
  content.height_px = g_frame.content_height();
  g_settings_face.build(g_frame.content(), content, g_brightness, [] {
    g_settings_face.clear();
    g_clock_active = true;
    l10n::set_locale_changed_handler(rebuild_clock_screen);
    set_theme_toggle(toggle_clock_theme);
    rebuild_clock_screen();
  }, page);
  g_frame.restore_content_geometry();
  g_frame.update(apps::format_mesh(staged_mesh_status(), l10n::locale()));
}

ui::Theme toggle_settings_theme() {
  g_clock_config.theme = g_clock_config.theme == ui::Theme::Day
      ? ui::Theme::Night : ui::Theme::Day;
  rebuild_settings_screen();
  return g_clock_config.theme;
}

void open_settings(lv_event_t *) {
  if (!g_clock_active) return;
  g_clock_face.clear();
  g_clock_active = false;
  g_settings_face.clear();
  l10n::set_locale_changed_handler(rebuild_settings_screen);
  set_theme_toggle(toggle_settings_theme);
  rebuild_settings_screen();
}

ui::Theme toggle_provision_theme() {
  g_provision_config.theme = g_provision_config.theme == ui::Theme::Day
                                 ? ui::Theme::Night
                                 : ui::Theme::Day;
  rebuild_provision_screen();
  return g_provision_config.theme;
}

ui::ProvisionFaceConfig provision_config_for(const ui::ClockFaceConfig &clock) {
  return {clock.width_px, clock.height_px, clock.theme,
          clock.pixel_cost, clock.metrics,  l10n::locale()};
}

// The same loop the board runs: poll for the answer the radio owes, and go
// back to the clock when the holder leaves. Here it is an LVGL timer that
// deletes itself; there it is the clock's own refresh timer.
void leave_provisioning(lv_timer_t *timer) {
  // The board polls the passkey on its clock tick; here it is this timer, and
  // it is the only thing that can end the wait. The tick that hears the answer
  // draws it and stops there, and nothing takes the receipt away afterwards:
  // `finished()` is the entry's `Exit` field, which only a press can reach.
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
  g_clock_active = true;
  l10n::set_locale_changed_handler(rebuild_clock_screen);
  set_theme_toggle(toggle_clock_theme);
  rebuild_clock_screen();
}

// A long press on the clock opens the entry screen, as on the board.
void on_long_press(lv_event_t *) {
  if (!g_clock_active) {
    return;
  }
  enter_provisioning();
}

// The slowest the simulator will let a screen sit between repaints.
//
// It has no work of its own on this timer -- unlike a board, which drains a
// receive ring on every tick whatever page is up -- so this is a ceiling and
// not an obligation. It is here so the simulator spends an application's
// declared cadence through the same door the firmware does
// (`apps::ui_period`), instead of naming one manifest by hand and drifting from
// the device on the one rule this seam exists to make visible.
constexpr core::Millis kSimBoardPeriod{1000};

void refresh_clock(lv_timer_t *timer) {
  if (g_clock_live) {
    g_clock_state.time.value.unix_seconds =
        static_cast<std::int64_t>(std::time(nullptr));
  }
  g_clock_state.locale = l10n::locale();
  g_clock_face.update(
      apps::format_clock(g_clock_state, g_clock_config.width_px < 300));
  lv_timer_set_period(
      timer, apps::ui_period(apps::clock_manifest(), kSimBoardPeriod).value);
}

} // namespace

void build_clock_screen(const platform::BoardProfile &board, ui::Theme theme,
                        const apps::ClockState &state, bool live) {
  g_clock_active = true;
  g_clock_live = live;
  g_settings_face.clear();
  g_brightness.load();
  g_clock_config = {
      board.display.width_px,
      board.display.height_px,
      theme,
      board.display.technology == platform::PanelTechnology::Amoled
          ? ui::PixelCost::PerPixel
          : ui::PixelCost::Fixed,
      ui::Metrics::for_dpi(board.display.dpi()),
  };
  g_clock_state = state;
  // Said here rather than by the caller, for `sim/nav_screen.cpp`'s reason:
  // this file owns the config `T` has to change, so it is the only place that
  // can answer for it.
  l10n::set_locale_changed_handler(rebuild_clock_screen);
  set_theme_toggle(toggle_clock_theme);
  rebuild_clock_screen();
  if (g_clock_live) {
    lv_timer_create(
        refresh_clock,
        apps::ui_period(apps::clock_manifest(), kSimBoardPeriod).value, nullptr);
  }
  lv_obj_add_event_cb(lv_screen_active(), on_long_press, LV_EVENT_LONG_PRESSED,
                      nullptr);
  lv_obj_add_event_cb(lv_screen_active(), open_settings, LV_EVENT_SHORT_CLICKED, nullptr);
}

void rebuild_clock_screen() {
  g_clock_state.locale = l10n::locale();
  g_clock_face.clear();
  g_frame.build(lv_screen_active(), {g_clock_config.width_px,
                g_clock_config.height_px, g_clock_config.theme,
                g_clock_config.pixel_cost, g_clock_config.metrics});
  auto content = g_clock_config;
  content.height_px = g_frame.content_height();
  g_clock_face.build(g_frame.content(), content,
      apps::format_clock(g_clock_state, g_clock_config.width_px < 300));
  g_frame.restore_content_geometry();
  g_frame.update(apps::format_mesh(staged_mesh_status(), l10n::locale()));
}

void enter_provisioning(apps::EntryTask task) {
  g_clock_face.clear();
  g_clock_active = false;
  g_provision_config = provision_config_for(g_clock_config);
  // Unseeded, and deliberately: `ClockState` carries a UTC instant and no
  // offset, so seeding from it would have to invent the offset half -- and an
  // invented offset reads exactly like a remembered one. The board seeds from
  // its time service, which keeps both.
  g_entry.emplace(g_provisioner, task);
  l10n::set_locale_changed_handler(rebuild_provision_screen);
  set_theme_toggle(toggle_provision_theme);
  rebuild_provision_screen();
  lv_timer_create(leave_provisioning,
                  ui::milliseconds_of(ui::Motion::Slow) * 8U, nullptr);
}

} // namespace attadipa::sim
