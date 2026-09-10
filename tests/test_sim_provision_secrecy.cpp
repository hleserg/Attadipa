#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <unistd.h>

#include "lvgl.h"

#include "attadipa/apps/clock.h"
#include "attadipa/apps/provisioning.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/color.h"

#include "clock_screen.h"

// The simulator's provisioning console says what happened and not what was
// typed (#316).
//
// The entry screen takes a six-digit MeshCore pairing passkey, and the
// simulator's board answers it on stdout. It used to answer with the number:
// `std::printf("provision: passkey %06u queued\n", ...)`. A made-up node does
// not make a typed number made-up — the digits a person puts into this screen
// are as likely to be the bench node's live PIN as an invention, and nothing
// on this side of the seam can tell those two apart — while stdout is a
// terminal scrollback, a shell redirect and a CI artefact at once.
//
// **Nothing here is a copy of the decision.** It opens the shipping entry
// screen through `sim::enter_provisioning()`, presses the shipping keypad with
// a real LVGL pointer device at the coordinates the keys are laid out at, and
// reads the bytes that reached file descriptor 1. A test that called the
// provisioner directly would prove that one function no longer formats a
// number and nothing about the screen that calls it; a test that asserted on a
// format string would pass against a second `printf` added next to it.
//
// The passkey it types is a synthetic canary and is not any node's. It has six
// distinct non-zero digits so that every formatting of it — `%06u`, `%u`, one
// digit at a time — leaves a six-digit run in the output for the second
// assertion to find.
//
// It is not hardware evidence and cannot become any: nothing in this process
// has touched a watch or a radio. **NOT EXECUTED — HARDWARE REQUIRED** for
// anything about a physical console.

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

// The passkey this test types. Not a credential: six digits picked so that a
// leak is unmistakable in a diff and in a failure message, the way
// `tests/test_meshcore_companion.cpp` picks `CANARY-NOTREAL` for the Room
// Server password.
constexpr unsigned kCanaryDigits[] = {4, 8, 3, 9, 1, 7};
constexpr const char *kCanary = "483917";

// --------------------------------------------------------------------------
// The panel, and the finger.

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

// One transition per read, with `continue_reading` while more remain — the
// idiom `sim/remote_input.cpp` documents at length, for its reason: LVGL reads
// an input device on its own timer, so a pair of press/release states held in
// two flags would coalesce into a single click.
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
    // Longer than LV_DEF_REFR_PERIOD, so the pointer read timer fires. There is
    // no SDL here to advance LVGL's clock, so the test advances it.
    lv_tick_inc(50);
    lv_timer_handler();
  }
}

// The frame buffers, kept for the whole run rather than with the panel that
// uses one: every face here is a file-static holding pointers into the screen
// it last built. `tests/test_sim_review_keys.cpp` pays for the same freedom in
// the same place, and says why at length.
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

  // The pointer goes; the display stays, for the reason above `g_buffers`.
  void close() {
    lv_indev_delete(touch);
    touch = nullptr;
  }
};

// --------------------------------------------------------------------------
// Reaching the keys.

// Every key on the entry screen, in the order the face creates them, which is
// `kKeys` in `ui/lvgl/provision_face.cpp` — Minus, Plus, Forget, Previous,
// Next, Leave. `lv_button_create` is called in exactly one place in this
// repository, so a button in this tree is one of these six and nothing else.
void collect_buttons(lv_obj_t *object, std::vector<lv_obj_t *> &out) {
  if (lv_obj_check_type(object, &lv_button_class)) {
    out.push_back(object);
  }
  const auto children = static_cast<std::int32_t>(lv_obj_get_child_count(object));
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

// Put a finger on a key and take it off again. `LV_EVENT_CLICKED` is what the
// face listens for, and LVGL only sends it when the release lands on the object
// the press did.
bool tap(Key key) {
  const std::vector<lv_obj_t *> buttons = keypad();
  const auto index = static_cast<std::size_t>(key);
  if (index >= buttons.size()) {
    return false;
  }
  lv_obj_t *button = buttons[index];
  // A hidden key is a key the model does not offer here, and LVGL will not
  // hit-test one — tapping it would silently do nothing and the walk would run
  // off the rails several presses later, somewhere else.
  if (lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN)) {
    return false;
  }
  lv_obj_update_layout(lv_screen_active());
  lv_area_t area;
  lv_obj_get_coords(button, &area);
  const std::int32_t x = (area.x1 + area.x2) / 2;
  const std::int32_t y = (area.y1 + area.y2) / 2;
  g_transitions.push_back({x, y, true});
  g_transitions.push_back({x, y, false});
  run_frames(4);
  return true;
}

// What the screen currently says, everywhere it says anything. The passkey
// under the cursor is drawn here on purpose — the person typing it has to see
// it — so this is how the test proves the digits really went in before it
// claims they did not come out.
void collect_text(lv_obj_t *object, std::string &out) {
  if (lv_obj_check_type(object, &lv_label_class)) {
    out += lv_label_get_text(object);
    out += '\n';
  }
  const auto children = static_cast<std::int32_t>(lv_obj_get_child_count(object));
  for (std::int32_t i = 0; i < children; ++i) {
    collect_text(lv_obj_get_child(object, i), out);
  }
}

std::string on_screen() {
  lv_obj_update_layout(lv_screen_active());
  std::string out;
  collect_text(lv_screen_active(), out);
  return out;
}

// --------------------------------------------------------------------------
// The console.

// stdout and stderr for the length of one walk.
//
// `dup2` on the file descriptors and not `freopen` on the `FILE*`: the subject
// is the bytes that leave this process, and a check that read a string this
// process handed itself would not be looking at the console at all.
class Console {
public:
  bool begin() {
    std::fflush(stdout);
    std::fflush(stderr);
    std::snprintf(path_, sizeof path_, "/tmp/attadipa-provision-XXXXXX");
    file_ = mkstemp(path_);
    if (file_ < 0) {
      return false;
    }
    saved_out_ = dup(STDOUT_FILENO);
    saved_err_ = dup(STDERR_FILENO);
    if (saved_out_ < 0 || saved_err_ < 0 || dup2(file_, STDOUT_FILENO) < 0 ||
        dup2(file_, STDERR_FILENO) < 0) {
      // Half a redirect is worse than none: the caller's own failure message
      // would go into the file it is about to delete, and the run would end
      // green with nothing on the console at all.
      (void)end();
      return false;
    }
    return true;
  }

  std::string end() {
    // Redirected, stdout is a file and therefore fully buffered: without this
    // the last line of the walk is still in this process when the test reads
    // the file, and "no passkey in the output" would be true of an output that
    // has not been written yet.
    std::fflush(stdout);
    std::fflush(stderr);
    if (saved_out_ >= 0) {
      (void)dup2(saved_out_, STDOUT_FILENO);
      close(saved_out_);
      saved_out_ = -1;
    }
    if (saved_err_ >= 0) {
      (void)dup2(saved_err_, STDERR_FILENO);
      close(saved_err_);
      saved_err_ = -1;
    }
    std::string captured;
    if (file_ >= 0) {
      (void)lseek(file_, 0, SEEK_SET);
      char block[4096];
      ssize_t got = 0;
      while ((got = read(file_, block, sizeof block)) > 0) {
        captured.append(block, static_cast<std::size_t>(got));
      }
      close(file_);
      file_ = -1;
      unlink(path_);
    }
    return captured;
  }

private:
  char path_[64] = {};
  int file_ = -1;
  int saved_out_ = -1;
  int saved_err_ = -1;
};

// Only the lines this simulator's board wrote. Everything else on the captured
// console belongs to LVGL, which logs at `LV_LOG_LEVEL_WARN` through the same
// two descriptors (`sim/lv_conf_simulator.h`), and a rule about digits applied
// to somebody else's warning is a rule that fails on a day nothing changed.
std::string provisioning_lines(const std::string &captured) {
  std::string out;
  std::size_t at = 0;
  while (at < captured.size()) {
    const std::size_t end = captured.find('\n', at);
    const std::size_t stop = end == std::string::npos ? captured.size() : end;
    const std::string line = captured.substr(at, stop - at);
    if (line.rfind("provision:", 0) == 0) {
      out += line;
      out += '\n';
    }
    at = stop + 1;
  }
  return out;
}

// A run of six or more decimal digits. The canary check is the one that names
// this test's secret; this one is the rule — a passkey is six digits, and no
// formatting of one, padded or not, gets past a check that refuses six digits
// in a row on a line the board wrote. It is stronger than a substring search
// in the case that matters: a leak that reformatted or reordered the value
// would still be a leak, and would still be caught.
bool has_six_digit_run(const std::string &text) {
  std::size_t run = 0;
  for (const char c : text) {
    run = std::isdigit(static_cast<unsigned char>(c)) ? run + 1 : 0;
    if (run >= 6) {
      return true;
    }
  }
  return false;
}

bool contains(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

// --------------------------------------------------------------------------

void the_console_reports_the_passkey_without_saying_it(
    const platform::BoardProfile &board) {
  std::fprintf(stderr, "provisioning console on %s (%u x %u)\n", board.id,
               board.display.width_px, board.display.height_px);

  Panel panel;
  panel.open(board);
  l10n::set_locale(l10n::Locale::En);

  const apps::ClockState state{};
  attadipa::sim::build_clock_screen(board, ui::Theme::Day, state, false);
  run_frames(2);

  Console console;
  if (!console.begin()) {
    std::fprintf(stderr, "FAIL: could not capture the console\n");
    ++failures;
    panel.close();
    return;
  }

  // The node half alone, which is the walk `--provision-node` opens and the
  // shortest one that reaches the passkey. The board this simulator gives the
  // screen is pinned to a node, so the walk starts on the node field.
  attadipa::sim::enter_provisioning(apps::EntryTask::NodePasskey);
  run_frames(2);

  bool walked = tap(Key::Next); // past the node, onto the first digit

  // Six digits, each stepped up from zero and then confirmed. The last `Next`
  // is the save: it is what calls `set_mesh_passkey`.
  std::string typed;
  for (std::size_t i = 0; i < 6; ++i) {
    for (unsigned step = 0; step < kCanaryDigits[i]; ++step) {
      walked = tap(Key::Plus) && walked;
    }
    if (i + 1 == 6) {
      // Read the screen before the save, not after: the receipt that follows
      // draws no value, so this is the last frame that can show whether the
      // digits this test believes it typed are the digits the model holds.
      typed = on_screen();
    }
    walked = tap(Key::Next) && walked;
  }

  // The board answers on a later tick, the way a radio does. Long enough for
  // `leave_provisioning` to poll it: that timer runs at 320 ms x 8.
  run_frames(80);

  // Out through the receipt, so the entry screen ends the way a person ends it
  // and the walk leaves no timer behind holding a screen this test deleted.
  const bool left = tap(Key::Next);
  run_frames(80);

  const std::string captured = console.end();

  CHECK(walked);
  CHECK(left);

  // The digits went in. Without this the two assertions below are also true of
  // a test whose finger missed every key.
  CHECK(contains(typed, kCanary));

  // And they did not come out.
  CHECK(!contains(captured, kCanary));
  CHECK(!has_six_digit_run(provisioning_lines(captured)));

  // The console still says what happened, which is the other half of the fix:
  // a line deleted rather than redacted would pass the two checks above and
  // leave nobody able to tell a queued passkey from a dropped one.
  CHECK(contains(captured, "provision: passkey queued"));
  CHECK(contains(captured, "provision: passkey armed"));

  if (failures > 0) {
    // The canary is synthetic, so printing it here costs nothing and is the
    // difference between a failure a reader can act on and a line number.
    std::fprintf(stderr, "captured console was:\n%s", captured.c_str());
  }

  panel.close();
}

} // namespace

int main() {
  lv_init();

  std::uint8_t count = 0;
  const platform::BoardProfile *profiles = platform::board_profiles(count);
  CHECK(count > 0);

  // Both geometries. The entry screen lays its keypad out from the panel, so
  // the small board is a different set of coordinates for the same six taps,
  // and a keypad this test could not reach on one of them is a keypad a finger
  // cannot reach either.
  for (std::uint8_t i = 0; i < count; ++i) {
    the_console_reports_the_passkey_without_saying_it(profiles[i]);
  }

  if (failures > 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("the provisioning console reports the passkey without saying it\n");
  return 0;
}
