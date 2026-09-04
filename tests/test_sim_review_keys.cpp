#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "lvgl.h"

#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/platform/hardware_inventory.h"
#include "attadipa/ui/color.h"

#include "boot_screen.h"
#include "diagnostic_screen.h"
#include "nav_screen.h"
#include "review_keys.h"

// The simulator's two review keys, driven the way a person drives them.
//
// This is the regression for #432: `--nav` advertised "T toggles it while
// running" and the navigation readout did not follow, because `main.cpp`
// answered `T` with an `if` ladder over the screens it remembered and the
// readout was not in it. The console said `theme: night` while the panel stayed
// in day, which is worse than silence.
//
// **Nothing here is a copy of the decision.** It creates a real LVGL keypad
// input device, puts the real screen in a real group, and lets LVGL's own
// `indev_keypad_proc` dispatch `LV_EVENT_KEY` to the handler the simulator
// installs — the same path the SDL keyboard takes, with the SDL window replaced
// by a display that flushes into memory. Then it looks at the rendered pixels.
// A test that called a toggle function directly would have passed against the
// defect: the toggle was never the broken part, the routing to it was.
//
// It is not hardware evidence and cannot become any: nothing in this process
// has touched a panel. **NOT EXECUTED — HARDWARE REQUIRED** for anything about
// a physical display.

using namespace attadipa;

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct KeyTransition {
  std::uint32_t key = 0;
  lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

std::vector<KeyTransition> g_keys;
std::size_t g_key_at = 0;

// The read callback an LVGL keypad device has, which is all `lv_sdl_keyboard`
// is: it turns SDL's key events into exactly this. One transition per read,
// with `continue_reading` while more remain, the way `sim/remote_input.cpp`
// documents for the pointer.
void read_key(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (g_key_at >= g_keys.size()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  data->key = g_keys[g_key_at].key;
  data->state = g_keys[g_key_at].state;
  ++g_key_at;
  data->continue_reading = g_key_at < g_keys.size();
}

void run_frames(int frames) {
  for (int i = 0; i < frames; ++i) {
    // Longer than LV_DEF_REFR_PERIOD, so the keypad read timer fires. There is
    // no SDL here to advance LVGL's clock, so the test advances it.
    lv_tick_inc(50);
    lv_timer_handler();
  }
}

// Press a key and let go of it, then let LVGL settle. A press with no release
// would leave the device held and the next press would not be a new one.
void press(std::uint32_t key) {
  g_keys.clear();
  g_key_at = 0;
  g_keys.push_back({key, LV_INDEV_STATE_PRESSED});
  g_keys.push_back({key, LV_INDEV_STATE_RELEASED});
  run_frames(4);
}

void flush_cb(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  lv_display_flush_ready(display);
}

// The frame buffers, kept for the whole run rather than with the panel that
// uses one. See `Panel::close` for why a display outlives its section.
std::deque<std::vector<std::uint8_t>> g_buffers;

// The one the panel now on the screen renders into.
const std::vector<std::uint8_t> *g_frame = nullptr;

// A panel with no window behind it: the display, the group and the keypad the
// composition root creates, in the order `sim/main.cpp` creates them.
struct Panel {
  lv_display_t *display = nullptr;
  lv_group_t *group = nullptr;
  lv_indev_t *keyboard = nullptr;

  void open(const platform::BoardProfile &board) {
    const std::uint32_t width = board.display.width_px;
    const std::uint32_t height = board.display.height_px;
    std::vector<std::uint8_t> &buffer = g_buffers.emplace_back(
        static_cast<std::size_t>(width) * height * 2, 0);
    g_frame = &buffer;

    display = lv_display_create(static_cast<std::int32_t>(width),
                                static_cast<std::int32_t>(height));
    lv_display_set_default(display);
    lv_display_set_dpi(display, board.display.dpi());
    lv_display_set_buffers(display, buffer.data(), nullptr,
                           static_cast<std::uint32_t>(buffer.size()),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    keyboard = lv_indev_create();
    lv_indev_set_type(keyboard, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keyboard, read_key);
    lv_indev_set_display(keyboard, display);

    // The screen itself takes key events, so the keys work without anything
    // focused — `sim/main.cpp` says the same thing in the same three calls.
    group = lv_group_create();
    lv_indev_set_group(keyboard, group);
    lv_group_add_obj(group, lv_screen_active());
    lv_obj_add_event_cb(lv_screen_active(), attadipa::sim::on_screen_key,
                        LV_EVENT_KEY, nullptr);
  }

  // The keypad and the group go; the display stays.
  //
  // Every face in this repository is a file-static object holding pointers into
  // the screen it last built, and a rebuild starts by deleting those objects.
  // `sim/main.cpp` creates one display and never deletes it, which is also what
  // a watch does, so nothing there ever asks what those pointers mean
  // afterwards. This test opens a panel per screen, and deleting one would
  // delete the objects a face still points at — the next build would then free
  // them a second time. That is this test's freedom to open three panels, not a
  // defect in the faces, so it is paid for here.
  void close() {
    lv_indev_delete(keyboard);
    lv_group_delete(group);
    keyboard = nullptr;
    group = nullptr;
  }
};

// What was flushed to the panel, which is the strongest thing a host test can
// look at: the frame LVGL rendered and handed to the display driver, in the
// panel's own format. `LV_COLOR_DEPTH 16` in `sim/lv_conf_simulator.h`, so two
// bytes a pixel, little-endian RGB565 — not the RGB888 the `--screenshot` path
// asks a snapshot for, because a snapshot allocates its own buffer out of
// LVGL's 1 MiB pool and this test holds several panels open at once.
std::vector<std::uint8_t> pixels() {
  return g_frame != nullptr ? *g_frame : std::vector<std::uint8_t>{};
}

// The top-left pixel, which on every screen here is page background: the
// readout's ring is centred and its labels are inset, and the boot screen pads
// its rows.
std::uint32_t corner(const std::vector<std::uint8_t> &frame) {
  if (frame.size() < 2) {
    return 0;
  }
  return static_cast<std::uint32_t>(frame[0]) |
         (static_cast<std::uint32_t>(frame[1]) << 8);
}

// A design token in the panel's format, so the expectation is the *role* —
// "the background the night palette defines" — rather than a number copied out
// of a table that both sides could get wrong together.
std::uint32_t page_colour(ui::Theme theme, const platform::BoardProfile &board) {
  const ui::PixelCost cost =
      board.display.technology == platform::PanelTechnology::Amoled
          ? ui::PixelCost::PerPixel
          : ui::PixelCost::Fixed;
  const auto value = ui::color(ui::ColorRole::BackgroundPrimary, theme, cost);
  if (!value) {
    return 0;
  }
  const std::uint32_t packed = value->packed();
  return (((packed >> 16) & 0xF8) << 8) | (((packed >> 8) & 0xFC) << 3) |
         ((packed & 0xFF) >> 3);
}

// Where every object is and what every label says — everything a theme must
// **not** change. A palette switch that also moved a label or reformatted a
// distance is a different defect wearing this one's clothes.
void describe(lv_obj_t *object, std::string &out) {
  out += std::to_string(lv_obj_get_x(object));
  out += ',';
  out += std::to_string(lv_obj_get_y(object));
  out += ',';
  out += std::to_string(lv_obj_get_width(object));
  out += ',';
  out += std::to_string(lv_obj_get_height(object));
  if (lv_obj_check_type(object, &lv_label_class)) {
    out += '|';
    out += lv_label_get_text(object);
  }
  out += ';';
  const auto children = static_cast<std::int32_t>(lv_obj_get_child_count(object));
  for (std::int32_t i = 0; i < children; ++i) {
    describe(lv_obj_get_child(object, i), out);
  }
}

std::string layout() {
  lv_obj_update_layout(lv_screen_active());
  std::string out;
  describe(lv_screen_active(), out);
  return out;
}

// --------------------------------------------------------------------------
// The navigation readout, which is what #432 is about.

void nav_follows_the_theme_key(const platform::BoardProfile &board) {
  std::printf("navigation readout on %s (%u x %u)\n", board.id,
              board.display.width_px, board.display.height_px);

  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);
  l10n::set_locale_changed_handler(attadipa::sim::rebuild_nav_screen);

  CHECK(attadipa::sim::stage_nav_scenario("ready"));
  attadipa::sim::build_nav_screen(board, ui::Theme::Day);
  run_frames(2);

  const std::vector<std::uint8_t> day = pixels();
  const std::string day_layout = layout();
  CHECK(corner(day) == page_colour(ui::Theme::Day, board));

  // Whether the two themes are two pictures on **this** panel.
  //
  // On an emissive panel they are not, and that is a decision rather than an
  // accident: OD-16's `day_emissive` column reuses the night colours for every
  // background and every text role, so day and night differ only in
  // `AccentPrimary` — which this readout does not use. `ui/src/color.cpp` says
  // so where the column is declared. The consequence is worth stating out loud
  // because it is what a reviewer sees: on the Waveshare, `T` on `--nav`
  // repaints the same colours, and a screenshot pair proves nothing there.
  // This test therefore asserts the case it is in rather than accepting
  // whichever it gets, so that a palette that grows a difference, or loses
  // one, fails here instead of quietly changing what `T` means.
  const bool two_pictures = page_colour(ui::Theme::Day, board) !=
                            page_colour(ui::Theme::Night, board);

  // 1. `T` switches the readout to night.
  press('T');
  const std::vector<std::uint8_t> night = pixels();
  CHECK(corner(night) == page_colour(ui::Theme::Night, board));
  CHECK((night != day) == two_pictures);
  // and switches nothing else: same geometry, same words.
  CHECK(layout() == day_layout);

  // 2. A second `T` puts it back, pixel for pixel. This is what catches a
  //    one-way switch, which is the shape a half-fix takes.
  press('T');
  CHECK(pixels() == day);

  // 3. `L` and `T` are independent, in both orders. The readout re-reads the
  //    locale at every rebuild, so a theme rebuild that dropped the language
  //    (or a language rebuild that dropped the theme) is a real way to fail
  //    this while passing 1 and 2.
  press('L');
  CHECK(l10n::locale() == l10n::Locale::Ru);
  const std::string russian_day = layout();
  CHECK(russian_day != day_layout);
  CHECK(corner(pixels()) == page_colour(ui::Theme::Day, board));

  press('T');
  CHECK(l10n::locale() == l10n::Locale::Ru);
  CHECK(corner(pixels()) == page_colour(ui::Theme::Night, board));
  CHECK(layout() == russian_day);

  press('L');
  CHECK(l10n::locale() == l10n::Locale::En);
  // The language came back and the theme did not follow it home.
  CHECK(corner(pixels()) == page_colour(ui::Theme::Night, board));
  CHECK(layout() == day_layout);

  press('T');
  CHECK(pixels() == day);

  l10n::set_locale(l10n::Locale::En);
  panel.close();
}

// --------------------------------------------------------------------------
// The other three screens, which the fix must not have taken the key away
// from — or, for the test pattern, must still not give it to.

void boot_screen_still_follows_the_theme_key(
    const platform::BoardProfile &board) {
  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);

  // Kept for the whole run, for the reason `Panel::close` gives: the boot
  // screen holds pointers to what it was last built from, and it is not this
  // test's business to say when they stop being read.
  static std::deque<platform::ProfileInventory> inventories;
  static std::deque<core::CapabilityRegistry> registries;
  platform::ProfileInventory &inventory = inventories.emplace_back(board);
  core::CapabilityRegistry &caps = registries.emplace_back(inventory);

  l10n::set_locale_changed_handler(attadipa::sim::rebuild_boot_screen);
  attadipa::sim::set_theme(ui::Theme::Day);
  attadipa::sim::build_boot_screen(inventory, caps);
  run_frames(2);

  const std::vector<std::uint8_t> day = pixels();
  CHECK(corner(day) == page_colour(ui::Theme::Day, board));

  press('T');
  const std::vector<std::uint8_t> night = pixels();
  CHECK(corner(night) == page_colour(ui::Theme::Night, board));
  // Unconditional here, unlike the readout above: this screen paints its
  // headings in `AccentPrimary`, which is the one role OD-16 keeps different on
  // an emissive panel, so day and night are two pictures on both boards.
  CHECK(night != day);

  press('T');
  CHECK(pixels() == day);

  panel.close();
}

void the_test_pattern_ignores_the_theme_key(
    const platform::BoardProfile &board) {
  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);

  l10n::set_locale_changed_handler(attadipa::sim::rebuild_diagnostic_screen);
  attadipa::sim::build_diagnostic_screen(board);
  run_frames(2);

  const std::vector<std::uint8_t> before = pixels();
  press('T');
  // Deliberate, and stated in `sim/diagnostic_screen.h`: the pattern's colours
  // are test vectors, so a palette it followed would blind the one screen whose
  // job is to catch a swapped colour channel. What the fix changes is that `T`
  // no longer flips a theme belonging to a screen that is not on the panel —
  // it says nothing changed, and nothing does.
  CHECK(pixels() == before);
  press('T');
  CHECK(pixels() == before);

  panel.close();
}

} // namespace

int main() {
  lv_init();

  std::uint8_t count = 0;
  const platform::BoardProfile *profiles = platform::board_profiles(count);
  CHECK(count > 0);

  // Both geometries, which is the Definition of Done's "reviewed at both" in
  // the form a machine can hold: the readout lays out proportionally to the
  // panel and picks its palette from the panel technology, so the 240x240 IPS
  // and the 410x502 AMOLED are two different answers to the same keypress.
  for (std::uint8_t i = 0; i < count; ++i) {
    nav_follows_the_theme_key(profiles[i]);
    boot_screen_still_follows_the_theme_key(profiles[i]);
    the_test_pattern_ignores_the_theme_key(profiles[i]);
  }

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("the review keys reach the screen that is on the panel\n");
  return 0;
}
