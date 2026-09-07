#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "lvgl.h"

#include "attadipa/apps/clock.h"
#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/platform/hardware_inventory.h"
#include "attadipa/apps/navigation.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/nav_face.h"

#include "boot_screen.h"
#include "clock_screen.h"
#include "diagnostic_screen.h"
#include "nav_screen.h"
#include "review_keys.h"

// The simulator's two review keys, driven the way a person drives them.
//
// This is the regression for #432: `--nav` advertised "T toggles it while
// running" and the navigation readout did not follow, because `main.cpp`
// answered `T` with an `if` ladder over the screens it remembered and the
// readout was not in it. The console said as much — `theme: night (nothing on
// screen follows it in this mode)` — so the defect was never a lying console.
// It was a key the help text promised and the screen on the panel ignored.
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

// Everything `describe()` wrote about the screen's children, with the screen's
// own box dropped. A theme that adds a backdrop adds it as the *first* children
// — LVGL draws siblings in creation order and a background has to be under
// everything — so a screen that gained one still ends with every row the other
// theme had, byte for byte, and a theme that moved a label or reformatted a
// distance does not.
//
// THE LEADING ';' IS KEPT ON PURPOSE and is the whole guarantee. `ends_with()`
// is a raw byte suffix, so without a row boundary in front of it the match can
// land inside a number: day's title at `x = 5` and night's at `x = 15` makes
// "5,10,60,19|NAVIGATION;…" a suffix of "15,10,60,19|NAVIGATION;…", and a theme
// that moved the first row passes. With the separator the comparison can only
// begin where a row does.
std::string rows(const std::string &described) {
  const std::size_t screen_box = described.find(';');
  return screen_box == std::string::npos ? described
                                         : described.substr(screen_box);
}

bool ends_with(const std::string &whole, const std::string &tail) {
  return whole.size() >= tail.size() &&
         whole.compare(whole.size() - tail.size(), tail.size(), tail) == 0;
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

  // NIGHT ON THIS SCREEN IS MORE THAN A PALETTE, AND THAT IS NEW.
  //
  // It used to be exactly a palette, and on an emissive panel not even that:
  // OD-16's `day_emissive` column reuses the night colours for every background
  // and every text role, so the two differed only in `AccentPrimary`, which the
  // readout did not use. This test carried a `two_pictures` flag for it, and
  // asserted `corner()` and an identical layout on both sides.
  //
  // `ui/lvgl/nav_face.cpp` now draws the meadow and its scrim under `Night` and
  // nothing at all under `Day`, the way the clock already did — the assertions
  // below are the ones `the_clock_follows_the_theme_key` makes for the same
  // reason, and its comment is the longer version of this one.
  //
  // WHETHER THE CORNER IS ART DEPENDS ON THE PANEL, and an earlier version of
  // this comment claimed it was art on both. The meadow is 410x502 and covers
  // rather than fits, so on a panel at least that size the corner is painted.
  // On a smaller one the cover scale is an integer 1/256th: 240x240 takes
  // `max(240*256/410, 240*256/502) = 149`, which draws `410*149 >> 8 = 238` px
  // into 240 and leaves column 0 and column 239 at bare page colour. So the
  // corner is the night page role on the T-Watch and is not on the Waveshare,
  // and the assertion below says exactly that — which keeps the *palette* the
  // subject of this test on the small panel, where `night != day` alone would
  // be satisfied by the two backdrop children whatever the roles resolved to.
  //
  // What survives unchanged is the guarantee this test was written for, in a
  // stronger form than `==` could state it: night's rows *end with* every row
  // day had, so the backdrop is added ahead of them and not one label moved,
  // resized or reworded underneath.

  // 1. `T` switches the readout to night.
  press('T');
  const std::vector<std::uint8_t> night = pixels();
  CHECK(night != day);
  const bool art_reaches_the_corner =
      board.display.width_px >= 410 && board.display.height_px >= 502;
  CHECK((corner(night) == page_colour(ui::Theme::Night, board)) !=
        art_reaches_the_corner);
  const std::string night_layout = layout();
  CHECK(night_layout != day_layout);
  CHECK(ends_with(rows(night_layout), rows(day_layout)));

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
  CHECK(ends_with(rows(layout()), rows(russian_day)));

  press('L');
  CHECK(l10n::locale() == l10n::Locale::En);
  // The language came back and the theme did not follow it home: still night,
  // so still the backdrop, and the English rows underneath it.
  CHECK(layout() != day_layout);
  CHECK(ends_with(rows(layout()), rows(day_layout)));

  press('T');
  CHECK(pixels() == day);

  l10n::set_locale(l10n::Locale::En);
  panel.close();
}

// --------------------------------------------------------------------------
// The trail itself, which is what #459 is and what nothing above asserts.
//
// `nav_follows_the_theme_key` compares night against day, so anything the two
// themes draw identically is invisible to it — and the trail is drawn
// identically in both. Every one of these passes that test: no dots at all,
// `trail_dots()` returning zero, the unhide loop mis-bounded, the sine and the
// cosine swapped so the run is plausible everywhere and correct only at the
// four cardinals, and `head` and `tail` transposed so the run brightens toward
// the wearer and the watch quietly points backwards.

struct Blob {
  std::int32_t cx = 0;
  std::int32_t cy = 0;
  std::int32_t size = 0;
};

// A square, visible child that the flex layout does not place. On this screen
// that is the trail plus the hub, and nothing else: the backdrop is 410x502
// scaled and the scrim is half the panel, so neither is square; every text row
// is a label; and the ring is square but *is* laid out, which is what lets it
// be found without asking the face for a pointer it does not hand out.
std::vector<Blob> circles_on_the_dial(bool laid_out) {
  std::vector<Blob> found;
  lv_obj_t *screen = lv_screen_active();
  const auto children = static_cast<std::int32_t>(lv_obj_get_child_count(screen));
  for (std::int32_t i = 0; i < children; ++i) {
    lv_obj_t *child = lv_obj_get_child(screen, i);
    if (lv_obj_check_type(child, &lv_label_class) ||
        lv_obj_check_type(child, &lv_image_class)) {
      continue;
    }
    if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) {
      continue;
    }
    if (lv_obj_has_flag(child, LV_OBJ_FLAG_IGNORE_LAYOUT) == laid_out) {
      continue;
    }
    const std::int32_t w = lv_obj_get_width(child);
    const std::int32_t h = lv_obj_get_height(child);
    if (w != h) {
      continue;
    }
    found.push_back(Blob{lv_obj_get_x(child) + w / 2,
                         lv_obj_get_y(child) + h / 2, w});
  }
  return found;
}

// The bearing off the screen rather than out of the fixture, so that changing
// the scenario cannot leave this test asserting an angle nothing draws.
//
// THREE DIGITS ARE NOT ENOUGH TO NAME IT, and the degree sign is what makes
// this safe to reuse. `apps::format_navigation` prints anything under a
// kilometre as `"%u m"` -- `apps/src/navigation.cpp:53` --
// "  if (metres < 1000) {" -- so a target a few hundred metres away puts
// "249 m" in the distance row, `distance_` is created before `bearing_`, and a
// scan for three leading digits returns the distance while reading exactly like
// a bearing. The bearing is the only row that follows its digits with U+00B0.
bool bearing_on_screen(double &degrees) {
  lv_obj_t *screen = lv_screen_active();
  const auto children = static_cast<std::int32_t>(lv_obj_get_child_count(screen));
  for (std::int32_t i = 0; i < children; ++i) {
    lv_obj_t *child = lv_obj_get_child(screen, i);
    if (!lv_obj_check_type(child, &lv_label_class)) {
      continue;
    }
    const char *text = lv_label_get_text(child);
    if (text == nullptr || std::strlen(text) < 3) {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(text[0])) &&
        std::isdigit(static_cast<unsigned char>(text[1])) &&
        std::isdigit(static_cast<unsigned char>(text[2])) &&
        std::strncmp(text + 3, "°", 2) == 0) {
      degrees = (text[0] - '0') * 100.0 + (text[1] - '0') * 10.0 + (text[2] - '0');
      return true;
    }
  }
  return false;
}

void the_trail_points_where_the_readout_says(const platform::BoardProfile &board) {
  std::printf("navigation trail on %s (%u x %u)\n", board.id,
              board.display.width_px, board.display.height_px);

  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);
  l10n::set_locale_changed_handler(attadipa::sim::rebuild_nav_screen);

  // `ready` is north-up — nothing on either board orients a bearing, so
  // `has_arrow` is false and the drawn angle *is* the bearing on the screen.
  // That is what lets the label be the expected value rather than a constant
  // copied out of the fixture.
  CHECK(attadipa::sim::stage_nav_scenario("ready"));
  attadipa::sim::build_nav_screen(board, ui::Theme::Night);
  run_frames(2);

  const std::vector<Blob> ring = circles_on_the_dial(true);
  CHECK(ring.size() == 1);
  if (ring.size() != 1) {
    panel.close();
    return;
  }
  const Blob centre = ring.front();

  std::vector<Blob> dots = circles_on_the_dial(false);
  // The hub is the one at the ring's centre; everything else is the trail.
  std::vector<Blob> trail;
  int hubs = 0;
  for (const Blob &blob : dots) {
    if (blob.cx == centre.cx && blob.cy == centre.cy) {
      ++hubs;
    } else {
      trail.push_back(blob);
    }
  }
  CHECK(hubs == 1);

  // The same split `NavFace::trail_dots()` makes, restated here because a test
  // that asked the face how many it drew would agree with it by construction.
  // 320 is the header's threshold — `ui/lvgl/include/attadipa/ui/nav_face.h` —
  // "unsigned trail_dots() const { return config_.width_px >= 320 ? kTrailDots : 4; }".
  const std::size_t expected = board.display.width_px >= 320 ? 7u : 4u;
  CHECK(trail.size() == expected);

  double bearing = 0.0;
  CHECK(bearing_on_screen(bearing));

  // Screen y grows downward and a bearing grows clockwise from north, so the
  // unit vector along the trail is (sin, -cos). Swapping the pair is the defect
  // `point_trail()`'s own comment names, and it survives at 000 and 090 and 180
  // and 270 — which is why `ready`'s 057 is the scenario to assert on.
  const double radians = bearing * 3.14159265358979323846 / 180.0;
  const double ux = std::sin(radians);
  const double uy = -std::cos(radians);

  double nearest = 1e9;
  double furthest = 0.0;
  std::int32_t size_at_nearest = 0;
  std::int32_t size_at_furthest = 0;
  for (const Blob &dot : trail) {
    const double dx = dot.cx - centre.cx;
    const double dy = dot.cy - centre.cy;
    const double along = dx * ux + dy * uy;
    // Perpendicular distance to the ray, which says "on the line" without
    // depending on how far out the dot is: an angular tolerance that a 47-px
    // head can pass is one a 17-px tail cannot.
    const double across = std::fabs(dx * uy - dy * ux);
    CHECK(across <= 2.0);
    // And on the right side of the wearer, which is what a 180-degree flip
    // would break while leaving every dot exactly on the line.
    CHECK(along > 0.0);
    if (along < nearest) {
      nearest = along;
      size_at_nearest = dot.size;
    }
    if (along > furthest) {
      furthest = along;
      size_at_furthest = dot.size;
    }
  }
  // The run reaches outward and grows as it goes. Transposing `head` and `tail`
  // leaves every dot on the line and every one of them on the correct side; the
  // only thing that changes is which end is bright, and the watch then points
  // at the wearer.
  CHECK(furthest > nearest);
  CHECK(size_at_furthest > size_at_nearest);

  // Nothing to point at, nothing pointed: `no-fix` has no bearing, and the
  // trail and the hub both go. A readout that kept drawing the last direction
  // under a dash is the exact failure this project exists to refuse.
  CHECK(attadipa::sim::stage_nav_scenario("no-fix"));
  attadipa::sim::build_nav_screen(board, ui::Theme::Night);
  run_frames(2);
  CHECK(circles_on_the_dial(false).empty());
  double no_bearing = 0.0;
  CHECK(!bearing_on_screen(no_bearing));
  panel.close();
}

// The device does not rebuild this face every tick. It builds once and calls
// `update()` — `firmware/main/waveshare_board.cpp:975` —
// "    state.nav_face.update(text);" — and the simulator only ever builds, so
// the trail's hide-and-show path has never had a caller any test could reach.
// It is the path that decides whether a watch that loses its bearing and gets
// it back draws the trail again, which is not a decoration: a run that came
// back one dot short, or not at all, would look like a working watch.
//
// This drives a face of the test's own, on its own panel, for the reason
// `Panel::close` gives about the boot screen: `NavFace::build()` cleans the
// screen, so two faces must never share one.
// The north marker is the one label excluded from the column layout, so it is
// found by that rather than by its text -- which is localised, and a test that
// matched on "N" would pass for the wrong reason in English and fail in
// Russian.
lv_obj_t *north_marker() {
  lv_obj_t *screen = lv_screen_active();
  const auto children = static_cast<std::int32_t>(lv_obj_get_child_count(screen));
  for (std::int32_t i = 0; i < children; ++i) {
    lv_obj_t *child = lv_obj_get_child(screen, i);
    if (lv_obj_check_type(child, &lv_label_class) &&
        lv_obj_has_flag(child, LV_OBJ_FLAG_IGNORE_LAYOUT)) {
      return child;
    }
  }
  return nullptr;
}

// HEAD-UP IS HALF OF THIS SCREEN AND IT WAS THE UNTESTED HALF. When the watch
// knows which way the wrist is turned, the ring turns with it: the trail draws
// the wrist-relative angle and the marker travels to where north actually is.
// Neither is what the readout prints -- the printed bearing stays true north --
// so every assertion the north-up test makes passes unchanged if the code
// forgets the distinction and draws the true bearing in a turned frame. That
// failure is silent, it looks like a working watch, and it walks the wearer off
// by however far they are turned. `apps::NavText` splits the two angles into
// two fields to make it impossible; nothing checked that the split was used.
void the_ring_turns_with_the_wrist(const platform::BoardProfile &board) {
  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);
  l10n::set_locale_changed_handler(nullptr);

  const ui::NavFaceConfig config{
      board.display.width_px,
      board.display.height_px,
      ui::Theme::Night,
      board.display.technology == platform::PanelTechnology::Amoled
          ? ui::PixelCost::PerPixel
          : ui::PixelCost::Fixed,
      ui::Metrics::for_dpi(board.display.dpi()),
  };

  apps::NavState turned;
  turned.own.availability = core::Availability::Ready;
  turned.own.has_position = true;
  turned.own.position.value = {5100000, 10000000};
  turned.own.validity = core::PositionValidity::Valid;
  turned.own.fix_type = core::FixType::ThreeD;
  turned.own.source = core::PositionSource::LocalGnss;
  turned.own.receiver = core::ReceiverPresence::Running;
  turned.target.availability = core::Availability::Ready;
  turned.target.has_position = true;
  turned.target.position.value = {5110000, 10020000};
  turned.target.validity = core::PositionValidity::NoFix;
  turned.target.source = core::PositionSource::NodeGnss;
  // The `head-up` fixture's own heading -- `sim/nav_screen.cpp:112` --
  // "    g_state.heading.source = core::HeadingSource::Magnetometer;"
  turned.heading.source = core::HeadingSource::Magnetometer;
  turned.heading.frame = core::ReferenceFrame::WatchBody;
  turned.heading.validity = core::HeadingValidity::Valid;
  turned.heading.confidence = 90;
  // 90 degrees, and NOT the fixture's 180. At 180 the marker's angle and its
  // negation are the same direction, so a placement that turned the wrong way
  // would land on the same pixels and this test would pass against a compass
  // that ran backwards. A quarter turn is the cheapest heading that is
  // degenerate under neither the sign nor the axis.
  turned.heading.centideg = 9000;

  const apps::NavText text = apps::format_navigation(turned);
  CHECK(text.has_arrow);
  CHECK(text.has_bearing);
  // If these two agreed there would be nothing to tell apart, and the whole
  // test would pass against a face that ignored the wrist.
  CHECK(text.arrow_centideg != text.bearing_centideg);

  ui::NavFace face;
  face.build(lv_screen_active(), config, text);
  run_frames(2);

  const std::vector<Blob> ring = circles_on_the_dial(true);
  CHECK(ring.size() == 1);
  lv_obj_t *marker = north_marker();
  CHECK(marker != nullptr);
  if (ring.size() != 1 || marker == nullptr) {
    panel.close();
    return;
  }
  const Blob centre = ring.front();

  // The trail follows the WRIST-RELATIVE angle, and the printed bearing is read
  // off the screen so the test can require the dots NOT to lie along it. Both
  // halves are needed and neither implies the other: the first says the trail
  // follows something, the second says that something is not the true north the
  // readout prints. A face that ignored the wrist satisfies the first against
  // `bearing_centideg` and fails only the second.
  double printed = 0.0;
  CHECK(bearing_on_screen(printed));
  // AND IT IS THE BEARING, not whatever else on this screen begins with three
  // digits. Without this the checks below are satisfied by any value that is
  // merely not the trail's angle -- the distance included -- so the one thing
  // that would notice `bearing_on_screen` reading the wrong row is a direct
  // comparison against the angle the readout was given. The readout prints TRUE
  // north while the trail draws the wrist-relative angle: that is head-up, and
  // this is the line that says so.
  const double true_bearing = text.bearing_centideg / 100.0;
  const double bearing_error = std::fabs(printed - true_bearing);
  CHECK(std::fmin(bearing_error, 360.0 - bearing_error) <= 1.0);

  const double printed_rad = printed * 3.14159265358979323846 / 180.0;
  const double px = std::sin(printed_rad);
  const double py = -std::cos(printed_rad);
  const double arrow_rad = text.arrow_centideg * 3.14159265358979323846 / 18000.0;
  const double ux = std::sin(arrow_rad);
  const double uy = -std::cos(arrow_rad);

  std::vector<Blob> dots;
  for (const Blob &b : circles_on_the_dial(false)) {
    if (std::abs(b.cx - centre.cx) > 2 || std::abs(b.cy - centre.cy) > 2) {
      dots.push_back(b);
    }
  }
  CHECK(!dots.empty());
  for (const Blob &d : dots) {
    const double dx = d.cx - centre.cx;
    const double dy = d.cy - centre.cy;
    CHECK(std::fabs(dx * uy - dy * ux) <= 2.0);
    CHECK(dx * ux + dy * uy > 0.0);
  }
  // And not along the printed one, measured on the dot furthest out, where a
  // quarter turn of wrist is tens of pixels and no tolerance argument is needed.
  const Blob &far_dot = *std::max_element(
      dots.begin(), dots.end(), [&](const Blob &a, const Blob &b) {
        return std::hypot(a.cx - centre.cx, a.cy - centre.cy) <
               std::hypot(b.cx - centre.cx, b.cy - centre.cy);
      });
  CHECK(std::fabs((far_dot.cx - centre.cx) * py -
                  (far_dot.cy - centre.cy) * px) > 2.0);

  // The marker travels to where north is: half the ring's height out from the
  // ring's centre, less half its own, at minus the heading. Within a pixel,
  // because the placement truncates a `double` and this recomputes it.
  const double orbit = centre.size / 2.0 - lv_obj_get_height(marker) / 2.0;
  const double marker_rad = -static_cast<double>(text.heading_centideg % 36000U) *
                            3.14159265358979323846 / 18000.0;
  const double want_cx = centre.cx + std::sin(marker_rad) * orbit;
  const double want_cy = centre.cy - std::cos(marker_rad) * orbit;
  const double got_cx = lv_obj_get_x(marker) + lv_obj_get_width(marker) / 2.0;
  const double got_cy = lv_obj_get_y(marker) + lv_obj_get_height(marker) / 2.0;
  CHECK(std::fabs(got_cx - want_cx) <= 1.0);
  CHECK(std::fabs(got_cy - want_cy) <= 1.0);

  // AND IT STAYS ON THE RING. A marker that orbited at the full radius would
  // hang half outside the hairline at east and west and be clipped at the
  // panel edge on the square board; both checks above would still pass.
  CHECK(lv_obj_get_x(marker) >= centre.cx - centre.size / 2);
  CHECK(lv_obj_get_x(marker) + lv_obj_get_width(marker) <= centre.cx + centre.size / 2);
  CHECK(lv_obj_get_y(marker) >= centre.cy - centre.size / 2);
  CHECK(lv_obj_get_y(marker) + lv_obj_get_height(marker) <= centre.cy + centre.size / 2);

  // The two branches meet at heading 0, which is the claim the north-up branch
  // is written out character for character to guarantee. In real arithmetic the
  // orbit form lands exactly on the north-up position; the code divides
  // integers on one side and truncates a `double` on the other, so a pixel of
  // disagreement is the documented cost and two is a bug.
  apps::NavState straight = turned;
  straight.heading.centideg = 0;
  const apps::NavText ahead = apps::format_navigation(straight);
  CHECK(ahead.has_arrow);
  face.update(ahead);
  run_frames(2);
  lv_obj_t *at_zero = north_marker();
  CHECK(at_zero != nullptr);
  if (at_zero != nullptr) {
    const std::int32_t north_up_x = centre.cx - lv_obj_get_width(at_zero) / 2;
    const std::int32_t north_up_y = centre.cy - centre.size / 2;
    CHECK(std::abs(lv_obj_get_x(at_zero) - north_up_x) <= 1);
    CHECK(std::abs(lv_obj_get_y(at_zero) - north_up_y) <= 1);
  }

  // BEFORE THE FACE GOES. `ui/lvgl/include/attadipa/ui/nav_face.h:73` --
  // "  // and reads the stops at every draw. A local would be read after it died."
  // -- is the reason the gradient is a member and not a local, and a `NavFace`
  // on the stack recreates that one level up: the screen, the scrim on it and
  // the style holding `&face.scrim_grad_` all outlive this frame, while every
  // later test pumps `lv_timer_handler()`. `clear()` deletes the objects that
  // hold the pointer, so it has to run while the face is still alive.
  face.clear();
  panel.close();
}

void the_trail_comes_back_after_it_goes(const platform::BoardProfile &board) {
  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);
  l10n::set_locale_changed_handler(nullptr);

  const ui::NavFaceConfig config{
      board.display.width_px,
      board.display.height_px,
      ui::Theme::Night,
      board.display.technology == platform::PanelTechnology::Amoled
          ? ui::PixelCost::PerPixel
          : ui::PixelCost::Fixed,
      ui::Metrics::for_dpi(board.display.dpi()),
  };

  // Deliberately nowhere, the way every position test in this repository
  // states its coordinates.
  apps::NavState pointing;
  pointing.own.availability = core::Availability::Ready;
  pointing.own.has_position = true;
  pointing.own.position.value = {5100000, 10000000};
  pointing.own.validity = core::PositionValidity::Valid;
  pointing.own.fix_type = core::FixType::ThreeD;
  pointing.own.source = core::PositionSource::LocalGnss;
  pointing.own.receiver = core::ReceiverPresence::Running;
  pointing.target.availability = core::Availability::Ready;
  pointing.target.has_position = true;
  pointing.target.position.value = {5110000, 10020000};
  pointing.target.validity = core::PositionValidity::NoFix;
  pointing.target.source = core::PositionSource::NodeGnss;

  apps::NavState blind = pointing;
  blind.own.fix_type = core::FixType::NoFix;
  blind.own.validity = core::PositionValidity::NoFix;

  const std::size_t expected = board.display.width_px >= 320 ? 7u : 4u;

  ui::NavFace face;
  face.build(lv_screen_active(), config, apps::format_navigation(pointing));
  run_frames(2);
  CHECK(circles_on_the_dial(false).size() == expected + 1);  // the trail and the hub

  // It goes.
  face.update(apps::format_navigation(blind));
  run_frames(2);
  CHECK(circles_on_the_dial(false).empty());

  // And it all comes back. One dot short here is the off-by-one in the unhide
  // loop, which the build path cannot see because `build()` creates the first
  // `trail_dots()` visible in the first place.
  face.update(apps::format_navigation(pointing));
  run_frames(2);
  CHECK(circles_on_the_dial(false).size() == expected + 1);
  // The gradient outlives the frame otherwise; the reason is written out in
  // `the_ring_turns_with_the_wrist`.
  face.clear();
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

// The clock, and the entry screen a long press opens. Both took their theme
// once at startup from a config that lived in `main.cpp`, so both were owners
// no test could reach — the #432 fix moved them, and this is what says they
// arrived. The clock is the screen the watch actually shows, which makes it the
// one whose theme key mattering is not a convenience for reviewers.
void the_clock_follows_the_theme_key(const platform::BoardProfile &board) {
  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);

  // Pinned, not the host clock: `live` would start the refresh timer and
  // `run_frames` would then redraw a different minute under the comparison.
  apps::ClockState state;
  state.time = {core::WallTime{1'700'000'000}, 0, 0, core::Validity::Valid};
  state.availability = core::Availability::Ready;
  attadipa::sim::build_clock_screen(board, ui::Theme::Day, state, false);
  run_frames(2);

  const std::vector<std::uint8_t> day = pixels();
  const std::string day_layout = layout();
  CHECK(corner(day) == page_colour(ui::Theme::Day, board));

  // Night is a different picture here, and deliberately more than a palette:
  // `ui/lvgl/clock_face.cpp` draws a meadow image and four fireflies under the
  // numerals for `Theme::Night` and nothing at all for `Theme::Day`. So `==` on
  // the layout is false for this screen by design, and asserting it here would
  // be asserting the wrong thing loudly. The navigation readout no longer makes
  // that assertion either — it draws the same meadow now — and states the
  // weaker true one, that night's rows end with day's; this screen could say
  // the same and does not, because the fireflies are interleaved among the
  // numerals rather than added ahead of them. What `T` has to prove here is
  // that the screen on the panel followed it, and that a second press puts back
  // exactly what was there.
  press('T');
  const std::vector<std::uint8_t> night = pixels();
  CHECK(night != day);
  CHECK(layout() != day_layout);

  press('T');
  CHECK(pixels() == day);
  CHECK(layout() == day_layout);

  // `L` still reaches it, and neither key takes the other with it. The clock
  // formats a weekday, so the two locales are two pictures on both boards.
  press('L');
  CHECK(l10n::locale() == l10n::Locale::Ru);
  const std::string russian = layout();
  CHECK(russian != day_layout);
  const std::vector<std::uint8_t> russian_day = pixels();

  press('T');
  CHECK(l10n::locale() == l10n::Locale::Ru);
  CHECK(pixels() != russian_day);

  press('T');
  CHECK(pixels() == russian_day);

  press('L');
  CHECK(l10n::locale() == l10n::Locale::En);
  CHECK(pixels() == day);

  // And the entry screen takes the key over when it takes the panel. Entered
  // directly rather than through a long press: the transition is not what is
  // under test, and `leave_provisioning` is a timer that would hand the panel
  // back mid-comparison if the entry ever finished.
  attadipa::sim::enter_provisioning();
  run_frames(2);
  const std::vector<std::uint8_t> entry_day = pixels();
  CHECK(corner(entry_day) == page_colour(ui::Theme::Day, board));

  press('T');
  CHECK(corner(pixels()) == page_colour(ui::Theme::Night, board));

  press('T');
  CHECK(pixels() == entry_day);

  l10n::set_locale(l10n::Locale::En);
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
    the_trail_points_where_the_readout_says(profiles[i]);
    the_trail_comes_back_after_it_goes(profiles[i]);
    the_ring_turns_with_the_wrist(profiles[i]);
    boot_screen_still_follows_the_theme_key(profiles[i]);
    the_clock_follows_the_theme_key(profiles[i]);
    the_test_pattern_ignores_the_theme_key(profiles[i]);
  }

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("the review keys reach the screen that is on the panel\n");
  return 0;
}
