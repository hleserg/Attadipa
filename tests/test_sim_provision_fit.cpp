#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "lvgl.h"

#include "attadipa/apps/clock.h"
#include "attadipa/apps/provisioning.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/color.h"

#include "clock_screen.h"

// Two properties of the provisioning entry screen that a screenshot can only
// show one board, one locale and one theme of at a time: nothing the screen
// draws falls outside the box it was put in, and every key's word is readable
// on that key's own fill.
//
// **Both are about the shipping screen.** It is built through
// `sim::enter_provisioning()`, walked with a real LVGL pointer device on the
// coordinates the keys are laid out at, and read back off the LVGL tree --
// `lv_obj_get_coords` and the style the face actually set, not a second copy
// of the arithmetic that set them. A test that recomputed the column height
// from the board profile would agree with a wrong `build()` forever.
//
// **The fit half.** `lines_` is a flex column aligned `LV_FLEX_ALIGN_CENTER`
// (`ui/lvgl/provision_face.cpp:185` -- "  lv_obj_set_flex_align(lines_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,")
// and the comment above it says the column clips rather than scrolls. Centred,
// it clips at BOTH ends, and the title is the line that goes first -- so the
// failure mode is a screen that has quietly lost the word naming what the
// value under the cursor is. #502 predicted a live clip at 240 x 240 from
// `240 - 128 - 22 - 6 = 84 px`; the panel height never reaches the face, which
// is given `g_frame.content_height()` at
// `sim/clock_screen.cpp:150` -- "  content.height_px = g_frame.content_height();"
// -- so the column is 77 px and the worst real line stack MEASURED across this
// walk is 75. Two pixels. Nothing clips today and one more wrapped line ends
// that, which is why this is a test and not an edit to the layout.
//
// **The contrast half.** `ui/AGENTS.md:40` -- "- **Contrast decides whether a state may be a word.** The night table measures"
// -- is the rule, and `kContrastBodyText` is the number. Day used to paint
// `BackgroundPrimary` on the acting key's `AccentPrimary`: #FFF6E8 on #FF8A40,
// 2.19:1. The screenshot in the PR shows one board in one theme; this covers
// every key that is drawn, on both, in both themes and both locales.
//
// It is not hardware evidence and cannot become any: nothing in this process
// has touched a watch or a panel. **NOT EXECUTED — HARDWARE REQUIRED** for
// anything about a physical screen.

using namespace attadipa;

namespace {

int failures = 0;
std::string where;

void fail(const std::string &what) {
  std::fprintf(stderr, "FAIL %s: %s\n", where.c_str(), what.c_str());
  ++failures;
}

// --------------------------------------------------------------------------
// The panel, and the finger. The same shape as
// `tests/test_sim_provision_secrecy.cpp`, for the same reason: LVGL reads an
// input device on its own timer, so a press and a release held in two flags
// coalesce into one click.

void flush_cb(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  lv_display_flush_ready(display);
}

struct PointerTransition {
  std::int32_t x = 0;
  std::int32_t y = 0;
  bool pressed = false;
};

std::deque<PointerTransition> g_transitions;
PointerTransition g_pointer;

void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  if (!g_transitions.empty()) {
    g_pointer = g_transitions.front();
    g_transitions.pop_front();
  }
  data->point.x = g_pointer.x;
  data->point.y = g_pointer.y;
  data->state =
      g_pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->continue_reading = !g_transitions.empty();
}

void run_frames(int frames) {
  for (int i = 0; i < frames; ++i) {
    lv_tick_inc(50);
    lv_timer_handler();
  }
}

// Kept for the whole run rather than with the panel that uses one: every face
// here is a file-static holding pointers into the screen it last built.
std::deque<std::vector<std::uint8_t>> g_buffers;

struct Panel {
  lv_display_t *display = nullptr;
  lv_indev_t *touch = nullptr;

  void open(const platform::BoardProfile &board) {
    const std::uint32_t width = board.display.width_px;
    const std::uint32_t height = board.display.height_px;
    std::vector<std::uint8_t> &buffer = g_buffers.emplace_back(
        static_cast<std::size_t>(width) * height * 2, 0);

    display = lv_display_create(static_cast<std::int32_t>(width),
                                static_cast<std::int32_t>(height));
    lv_display_set_default(display);
    lv_display_set_dpi(display, board.display.dpi());
    lv_display_set_buffers(display, buffer.data(), nullptr,
                           static_cast<std::uint32_t>(buffer.size()),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    g_transitions.clear();
    g_pointer = PointerTransition{};
    touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_pointer);
    lv_indev_set_display(touch, display);
  }

  void close() {
    lv_indev_delete(touch);
    touch = nullptr;
  }
};

// --------------------------------------------------------------------------
// Reaching the keys. `lv_button_create` is called in exactly one place in this
// repository, so a button in this tree is one of the six the face lays out, in
// the order `kKeys` names them.

void collect_buttons(lv_obj_t *object, std::vector<lv_obj_t *> &out) {
  if (lv_obj_check_type(object, &lv_button_class)) {
    out.push_back(object);
  }
  const auto children =
      static_cast<std::int32_t>(lv_obj_get_child_count(object));
  for (std::int32_t i = 0; i < children; ++i) {
    collect_buttons(lv_obj_get_child(object, i), out);
  }
}

enum class Key : std::size_t {
  Minus = 0,
  Plus = 1,
  Forget = 2,
  Previous = 3,
  Next = 4,
  Leave = 5,
};

std::vector<lv_obj_t *> keypad() {
  std::vector<lv_obj_t *> buttons;
  collect_buttons(lv_screen_active(), buttons);
  return buttons;
}

// A hidden key is a key the model does not offer here, and LVGL will not
// hit-test one: tapping it would silently do nothing and the walk would run
// off the rails several presses later, somewhere else.
bool tap(Key key) {
  const std::vector<lv_obj_t *> buttons = keypad();
  const auto index = static_cast<std::size_t>(key);
  if (index >= buttons.size()) {
    return false;
  }
  lv_obj_t *button = buttons[index];
  if (lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN)) {
    return false;
  }
  lv_obj_update_layout(lv_screen_active());
  lv_area_t area;
  lv_obj_get_coords(button, &area);
  g_transitions.push_back({(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2,
                           true});
  g_transitions.push_back({(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2,
                           false});
  run_frames(4);
  return true;
}

// --------------------------------------------------------------------------
// The two checks.

bool visible(lv_obj_t *object) {
  for (lv_obj_t *at = object; at != nullptr; at = lv_obj_get_parent(at)) {
    if (lv_obj_has_flag(at, LV_OBJ_FLAG_HIDDEN)) {
      return false;
    }
  }
  return true;
}

ui::Rgb rgb_of(lv_color_t colour) {
  return ui::Rgb{colour.red, colour.green, colour.blue};
}

// Every label the screen is currently drawing sits inside the box that holds
// it. An empty label is zero-sized and passes trivially, which is correct: a
// line the model has nothing to say on is not drawn.
//
// The parent, not the screen: the column is what clips, and a label that has
// escaped it is already the defect whether or not it also left the panel.
void every_line_fits(lv_obj_t *object) {
  if (lv_obj_check_type(object, &lv_label_class) && visible(object) &&
      lv_obj_get_parent(object) != nullptr) {
    lv_area_t self;
    lv_area_t box;
    lv_obj_get_coords(object, &self);
    lv_obj_get_coords(lv_obj_get_parent(object), &box);
    if (self.y1 < box.y1 || self.y2 > box.y2 || self.x1 < box.x1 ||
        self.x2 > box.x2) {
      char said[160];
      std::snprintf(said, sizeof said,
                    "\"%s\" is drawn at (%d,%d)-(%d,%d), outside its box "
                    "(%d,%d)-(%d,%d)",
                    lv_label_get_text(object), self.x1, self.y1, self.x2,
                    self.y2, box.x1, box.y1, box.x2, box.y2);
      fail(said);
    }
  }
  const auto children =
      static_cast<std::int32_t>(lv_obj_get_child_count(object));
  for (std::int32_t i = 0; i < children; ++i) {
    every_line_fits(lv_obj_get_child(object, i));
  }
}

// Every key that is drawn says its word in an ink that reads on its own fill.
// Read back off the widget, so it is the colour the face set and not the
// colour a second copy of `update()` would have chosen.
void every_key_is_readable() {
  const std::vector<lv_obj_t *> buttons = keypad();
  for (lv_obj_t *button : buttons) {
    if (!visible(button) || lv_obj_get_child_count(button) == 0) {
      continue;
    }
    lv_obj_t *word = lv_obj_get_child(button, 0);
    const ui::Rgb fill =
        rgb_of(lv_obj_get_style_bg_color(button, LV_PART_MAIN));
    const ui::Rgb ink = rgb_of(lv_obj_get_style_text_color(word, LV_PART_MAIN));
    const std::uint16_t measured = ui::contrast_ratio_centi(ink, fill);
    if (measured < ui::kContrastBodyText) {
      char said[200];
      std::snprintf(said, sizeof said,
                    "\"%s\" is #%02X%02X%02X on #%02X%02X%02X -- %u.%02u:1, "
                    "under the %u.%02u:1 a word needs",
                    lv_label_get_text(word), ink.r, ink.g, ink.b, fill.r,
                    fill.g, fill.b, measured / 100u, measured % 100u,
                    ui::kContrastBodyText / 100u, ui::kContrastBodyText % 100u);
      fail(said);
    }
  }
}

void check_this_screen(const char *step) {
  lv_obj_update_layout(lv_screen_active());
  const std::string outer = where;
  where = outer + " " + step;
  every_line_fits(lv_screen_active());
  every_key_is_readable();
  where = outer;
}

// --------------------------------------------------------------------------

// Next until the walk stops offering it. Every task ends on a screen whose
// Next is hidden -- `EntryField::Exit` draws nothing at all -- so the budget is
// a guard against a model that loops, not the normal way out.
constexpr int kSteps = 24;

void the_entry_screen_fits_and_reads(const platform::BoardProfile &board,
                                     ui::Theme theme, l10n::Locale locale,
                                     apps::EntryTask task, const char *name) {
  Panel panel;
  panel.open(board);
  l10n::set_locale(locale);

  const apps::ClockState state{};
  attadipa::sim::build_clock_screen(board, theme, state, false);
  run_frames(2);

  attadipa::sim::enter_provisioning(task);
  run_frames(2);

  char label[128];
  std::snprintf(label, sizeof label, "%s %s/%s/%s", board.id, name,
                theme == ui::Theme::Day ? "day" : "night",
                locale == l10n::Locale::Ru ? "ru" : "en");
  where = label;

  check_this_screen("start");

  // The Forget branch on the way past the node. It is the one screen a Next
  // walk cannot reach -- `EntryField::ForgetConfirm` draws no Next at all --
  // and the one whose keys carry the `Warning` fill, so it is where the
  // contrast check has the most to say.
  if (task != apps::EntryTask::LocalTime && tap(Key::Forget)) {
    check_this_screen("forget-confirm");
    // Back, not out: on this screen Leave is Back.
    if (!tap(Key::Leave)) {
      fail("could not leave the forget confirmation");
    }
    check_this_screen("back from forget-confirm");
  }

  for (int step = 0; step < kSteps; ++step) {
    if (!tap(Key::Next)) {
      break;
    }
    // The board answers on a later tick, the way a radio does; a receipt this
    // walk read before the answer is a receipt with nothing on it.
    run_frames(8);
    char said[32];
    std::snprintf(said, sizeof said, "after %d x Next", step + 1);
    check_this_screen(said);
  }

  where.clear();
  panel.close();
}

} // namespace

int main() {
  lv_init();

  std::uint8_t count = 0;
  const platform::BoardProfile *profiles = platform::board_profiles(count);
  if (count == 0) {
    std::fprintf(stderr, "FAIL: no board profiles\n");
    return 1;
  }

  const struct {
    apps::EntryTask task;
    const char *name;
  } tasks[] = {
      {apps::EntryTask::LocalTime, "time"},
      {apps::EntryTask::NodePasskey, "node"},
      {apps::EntryTask::All, "all"},
  };

  for (std::uint8_t i = 0; i < count; ++i) {
    for (const ui::Theme theme : {ui::Theme::Day, ui::Theme::Night}) {
      for (const l10n::Locale locale :
           {l10n::Locale::En, l10n::Locale::Ru}) {
        for (const auto &task : tasks) {
          the_entry_screen_fits_and_reads(profiles[i], theme, locale,
                                          task.task, task.name);
        }
      }
    }
  }

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("the provisioning entry screen fits its column and reads on "
              "every key\n");
  return 0;
}
