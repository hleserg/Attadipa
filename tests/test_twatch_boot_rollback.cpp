#include "twatch_boot_rollback.h"
#include "twatch_panel_exercise.h"

#include <cassert>
#include <string_view>
#include <vector>

namespace {

struct FakeOps {
  bool display = false;
  std::vector<std::string_view> calls;

  bool display_registered() const { return display; }
  void retain_display_stack() { calls.push_back("retain display stack"); }
  void remove_touch() { calls.push_back("touch"); }
  void remove_touch_io() { calls.push_back("touch io"); }
  void remove_touch_bus() { calls.push_back("touch bus"); }
  void stop_lvgl() { calls.push_back("lvgl"); }
  void remove_panel() { calls.push_back("panel"); }
  void remove_panel_io() { calls.push_back("panel io"); }
  void free_spi() { calls.push_back("spi"); }
  void remove_pmu() { calls.push_back("pmu"); }
  void remove_main_bus() { calls.push_back("main bus"); }
};

struct FakePanelExercise {
  std::vector<std::string_view> calls;
  unsigned display_cycles = 0;

  bool show_patterns() {
    calls.push_back("patterns");
    return true;
  }
  bool rotation_and_gap() {
    calls.push_back("rotation and gap");
    return true;
  }
  bool display_cycle(unsigned cycle) {
    ++display_cycles;
    calls.push_back(cycle == display_cycles ? "display cycle" : "bad cycle");
    return true;
  }
  bool sleep_cycle(bool respect_interval) {
    calls.push_back(respect_interval ? "sleep respected" : "sleep violated");
    return true;
  }
};

} // namespace

int main() {
  FakeOps before_display;
  attadipa::firmware::rollback_twatch_boot(before_display);
  assert((before_display.calls == std::vector<std::string_view>{
                                      "touch", "touch io", "touch bus", "lvgl",
                                      "panel", "panel io", "spi", "pmu", "main bus"}));

  FakeOps live_display;
  live_display.display = true;
  attadipa::firmware::rollback_twatch_boot(live_display);
  assert((live_display.calls ==
          std::vector<std::string_view>{"retain display stack"}));

  FakePanelExercise panel;
  assert(attadipa::firmware::run_twatch_panel_exercise(panel));
  assert(panel.calls.size() == 14);
  assert(panel.calls.front() == "patterns");
  assert(panel.calls[1] == "rotation and gap");
  for (std::size_t i = 2; i < 12; ++i) {
    assert(panel.calls[i] == "display cycle");
  }
  assert(panel.calls[12] == "sleep violated");
  assert(panel.calls[13] == "sleep respected");
}
