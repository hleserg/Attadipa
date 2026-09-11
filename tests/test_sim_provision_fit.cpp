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
#include "attadipa/ui/provision_face.h"

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
// (`ui/lvgl/provision_face.cpp:217` -- "  lv_obj_set_flex_align(lines_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,")
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

// One per walk, and popped by the walk that finishes. It is still a deque and
// not a member of `Panel` because a walk that could NOT leave the entry screen
// keeps its display (see `Panel::close`), and a display that is still being
// rendered needs the memory it renders into to outlive the walk with it.
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

  // `left` is whether the walk got the entry all the way to `EntryField::Exit`.
  //
  // THE POLL TIMER IS WHY THIS TAKES AN ARGUMENT. `enter_provisioning` creates
  // one timer per entry and only `leave_provisioning` deletes it, on the tick
  // that finds the entry finished -- and its period is
  // `ui::Motion::Slow * 8`, 2560 ms, where a walk is some four hundred. So a
  // walk that just stopped left a timer running: the next `emplace()` replaces
  // the entry under it and two timers then poll one optional, which is what
  // `sim/clock_screen.cpp:222` -- "  // A TIMER MAY OUTLIVE THE ENTRY IT
  // POLLS, AND ONLY THIS SAYS SO." -- is about. 64 frames is 3200 ms, one
  // period and a half, so the timer collects itself here rather than being
  // deleted from under the function that owns it.
  //
  // And that is also why the display is deleted only when the walk left. If it
  // did not, the entry is alive, the timer is alive, and its next `update()`
  // would run on keys this deleted: `g_provision_face` is a file static still
  // holding those pointers. A leaked display is the safe half of that trade,
  // and the walk has already failed by the time it is taken.
  void close(bool left) {
    run_frames(64);
    lv_indev_delete(touch);
    touch = nullptr;
    if (!left) {
      return;
    }
    lv_display_delete(display);
    display = nullptr;
    g_buffers.pop_back();
  }
};

// --------------------------------------------------------------------------
// Reaching the keys.
//
// NOT BY POSITION, AND NOT ON A CLAIM ABOUT THE REPOSITORY. This used to say
// `lv_button_create` has one caller, so the nth button in the tree is the nth
// key. It has two -- `ui/lvgl/provision_face.cpp:260` -- "    lv_obj_add_event_cb(button, key_event, LV_EVENT_CLICKED, this);"
// -- and `ui/lvgl/settings_face.cpp:47` -- "  lv_obj_t *button = lv_button_create(screen_);"
// -- and a claim like that is false the first time either face is put on a
// screen this one also uses, in a way that shifts every tap by a slot and
// still passes.
//
// So the keys are read back by the identity the face stamped on each of them
// (`ui/lvgl/provision_face.cpp:263` -- "    lv_obj_set_user_data(button, reinterpret_cast<void *>("),
// and the screen has to hold exactly one button per `EntryKey` and nothing
// else. A seventh button, a missing one, or two claiming a slot empties the
// keypad, and an empty keypad fails the walk at its first check instead of
// moving it.

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

// Empty unless the active screen holds one button for each `EntryKey`.
std::vector<lv_obj_t *> keypad() {
  std::vector<lv_obj_t *> found;
  collect_buttons(lv_screen_active(), found);
  std::vector<lv_obj_t *> keys(ui::ProvisionFace::kKeyCount, nullptr);
  if (found.size() != keys.size()) {
    return {};
  }
  for (lv_obj_t *button : found) {
    const auto slot = reinterpret_cast<std::uintptr_t>(
        lv_obj_get_user_data(button));
    if (slot >= keys.size() || keys[slot] != nullptr) {
      return {};
    }
    keys[slot] = button;
  }
  return keys;
}

// A hidden key is a key the model does not offer here, and LVGL will not
// hit-test one: tapping it would silently do nothing and the walk would run
// off the rails several presses later, somewhere else.
bool tap(apps::EntryKey key) {
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

// What is behind a key. The keypad the keys sit in is `bare()`, so the thing an
// opacity blends into is whatever paints next: the page the frame fills the
// screen with. Walked rather than assumed, because a container acquiring a fill
// would change the answer and nothing else here would notice.
lv_color_t backdrop_of(lv_obj_t *object) {
  for (lv_obj_t *at = lv_obj_get_parent(object); at != nullptr;
       at = lv_obj_get_parent(at)) {
    if (lv_obj_get_style_bg_opa(at, LV_PART_MAIN) == LV_OPA_COVER) {
      return lv_obj_get_style_bg_color(at, LV_PART_MAIN);
    }
  }
  return lv_color_black();
}

// The colour this key is drawn in while a finger is on it. Read by putting the
// widget in that state and asking the style system, so a pressed COLOUR and a
// pressed OPACITY are both answered -- the face has used each in turn, and a
// helper that knew which would go stale on the next change. The blend is
// `lv_color_mix`, which is the function the renderer itself would use.
lv_color_t pressed_colour(lv_obj_t *button) {
  lv_obj_add_state(button, LV_STATE_PRESSED);
  const lv_opa_t opa = lv_obj_get_style_bg_opa(button, LV_PART_MAIN);
  const lv_color_t fill = lv_obj_get_style_bg_color(button, LV_PART_MAIN);
  lv_obj_remove_state(button, LV_STATE_PRESSED);
  return lv_color_mix(fill, backdrop_of(button), opa);
}

// WHAT THE PANEL CAN ACTUALLY SHOW, which is not what the style holds.
// `sim/lv_conf_simulator.h:69` — "#define LV_COLOR_DEPTH 16" — so a difference
// under one bucket of `r >> 3`, `g >> 2`, `b >> 3` is drawn as no difference at
// all. A pressed fill two parts from the resting one is a press nobody sees.
std::uint16_t as_pixel(lv_color_t colour) { return lv_color_to_u16(colour); }

// Every key that is drawn says its word in an ink that reads on its own fill --
// in BOTH the states that fill has. Read back off the widget, so it is the
// colour the face set and not the colour a second copy of `update()` would
// have chosen.
//
// The pressed state is not a detail that can be left out of a claim about
// whether a key's word is readable: the press moves the fill, and the ink that
// was chosen against the resting fill does not move with it. Under the
// LV_OPA_70 this screen used to press at, the day-emissive accent key fell
// from 5.08:1 to 3.23:1 and this test said the screen was readable.
//
// AND READABLE IN BOTH STATES IS NOT THE SAME AS HAVING TWO STATES. The fix
// for that first defect was an opacity so high the pressed key quantised to
// the resting key's RGB565 pixel on the four keys filled `raised` -- a screen
// that was perfectly readable and did not answer a finger. This test was green
// through it, and would have been green with the pressed style deleted
// outright, because it measured two fills and never compared them. That is the
// counter-check `ui/AGENTS.md:72` — "- **A pixel test needs a counter-check.**
// Comparing two frames proves nothing" — asks for, so it is here.
void every_key_is_readable() {
  const std::vector<lv_obj_t *> buttons = keypad();
  for (lv_obj_t *button : buttons) {
    if (!visible(button) || lv_obj_get_child_count(button) == 0) {
      continue;
    }
    lv_obj_t *word = lv_obj_get_child(button, 0);
    const ui::Rgb ink = rgb_of(lv_obj_get_style_text_color(word, LV_PART_MAIN));
    const lv_color_t resting = lv_obj_get_style_bg_color(button, LV_PART_MAIN);
    const lv_color_t pressed = pressed_colour(button);
    if (as_pixel(resting) == as_pixel(pressed)) {
      char said[240];
      const ui::Rgb at_rest = rgb_of(resting);
      std::snprintf(said, sizeof said,
                    "\"%s\" is #%02X%02X%02X pressed and #%02X%02X%02X at "
                    "rest, which is one RGB565 pixel -- pressing this key "
                    "changes nothing a panel can draw",
                    lv_label_get_text(word), pressed.red, pressed.green,
                    pressed.blue, at_rest.r, at_rest.g, at_rest.b);
      fail(said);
    }
    const struct {
      const char *state;
      ui::Rgb fill;
    } states[] = {
        {"at rest", rgb_of(resting)},
        {"pressed", rgb_of(pressed)},
    };
    for (const auto &one : states) {
      const std::uint16_t measured = ui::contrast_ratio_centi(ink, one.fill);
      if (measured < ui::kContrastBodyText) {
        char said[240];
        std::snprintf(said, sizeof said,
                      "\"%s\" %s is #%02X%02X%02X on #%02X%02X%02X -- "
                      "%u.%02u:1, under the %u.%02u:1 a word needs",
                      lv_label_get_text(word), one.state, ink.r, ink.g, ink.b,
                      one.fill.r, one.fill.g, one.fill.b, measured / 100u,
                      measured % 100u, ui::kContrastBodyText / 100u,
                      ui::kContrastBodyText % 100u);
        fail(said);
      }
    }
  }
}

// How many screens this walk actually looked at. A walk that cannot reach the
// keys performs no checks and, counting nothing, used to print success.
int screens_checked = 0;

void check_this_screen(const char *step) {
  lv_obj_update_layout(lv_screen_active());
  const std::string outer = where;
  where = outer + " " + step;
  every_line_fits(lv_screen_active());
  every_key_is_readable();
  where = outer;
  ++screens_checked;
}

// --------------------------------------------------------------------------

// Next until the walk stops offering it. Every task ends on a screen whose
// Next is hidden -- `EntryField::Exit` draws nothing at all -- so the budget is
// a guard against a model that loops, not the normal way out, and reaching it
// is a failure rather than the end of the walk. Nineteen presses is the longest
// real route; the budget is loose on purpose, because what it is guarding
// against is a loop and not a fifth digit.
constexpr int kSteps = 24;

// Leave, from anywhere, takes at most three presses. The confirmation answers
// its question and stays on the node, the waiting frame becomes the abandoned
// receipt, and every other field goes straight out.
constexpr int kWaysOut = 3;

// Nothing is drawn on `EntryField::Exit`: `text()` returns before it labels a
// single key, and a key with no label is hidden. So "no key is on the screen"
// is how this walk reads "the holder has left", off the tree rather than off
// the model it is testing.
bool left_the_entry() {
  for (lv_obj_t *button : keypad()) {
    if (visible(button)) {
      return false;
    }
  }
  return true;
}

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

  // BEFORE ANY OF IT: is the entry screen even on the panel? `tap` cannot tell
  // "this key is not offered here" from "there are no keys", and a walk that
  // finds nothing to press performs no checks and reports success -- which is
  // what this test would have done if `enter_provisioning` had quietly not
  // built anything.
  if (keypad().empty()) {
    fail("the six keys the face lays out are not on this screen, so nothing "
         "below this line would have checked anything");
    where.clear();
    panel.close(false);
    return;
  }

  screens_checked = 0;
  check_this_screen("start");

  // The Forget branch on the way past the node. It is the one screen a Next
  // walk cannot reach -- `EntryField::ForgetConfirm` draws no Next at all --
  // and the one whose keys carry the `Warning` fill, so it is where the
  // contrast check has the most to say.
  if (task != apps::EntryTask::LocalTime && tap(apps::EntryKey::Forget)) {
    check_this_screen("forget-confirm");
    // Back, not out: on this screen Leave is Back.
    if (!tap(apps::EntryKey::Leave)) {
      fail("could not leave the forget confirmation");
    }
    check_this_screen("back from forget-confirm");
  }

  int step = 0;
  for (; step < kSteps && tap(apps::EntryKey::Next); ++step) {
    // The board answers on a later tick, the way a radio does; a receipt this
    // walk read before the answer is a receipt with nothing on it.
    run_frames(8);
    char said[32];
    std::snprintf(said, sizeof said, "after %d x Next", step + 1);
    check_this_screen(said);
  }
  if (step == kSteps) {
    fail("the Next walk ran out of budget instead of running out of screens, "
         "so the model is looping and everything after the loop is unchecked");
  }

  // WHERE THE REFUSED RECEIPTS LIVE. A Next walk cannot reach one: the
  // simulator's provisioner accepts everything it is given, and the passkey it
  // accepts goes to the radio, so the walk ends on the waiting frame -- the one
  // frame that draws Leave and nothing else. Pressing it is what makes an
  // `EntryVerdict::Abandoned`, whose line is the longest of the whole set (49
  // characters in English against the 44 of `ProvisionTimeUncertain`), and the
  // fit half of this test had never once drawn it.
  //
  // It is also how every walk ends on `EntryField::Exit`, which is what lets
  // `Panel::close` delete the display: see the note there.
  for (int out = 0; out < kWaysOut && tap(apps::EntryKey::Leave); ++out) {
    run_frames(8);
    char said[32];
    std::snprintf(said, sizeof said, "after %d x Leave", out + 1);
    check_this_screen(said);
  }

  const bool left = left_the_entry();
  if (!left) {
    fail("the walk could not leave the entry screen, so its poll timer is "
         "still running and the next walk would share it");
  }
  // Every task offers more than one screen, so a walk that checked one checked
  // the screen it started on and nothing the presses were supposed to reach.
  if (screens_checked < 2) {
    fail("the walk checked only the screen it started on: every press it made "
         "was refused, and a walk that checks nothing passes");
  }

  where.clear();
  panel.close(left);
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
