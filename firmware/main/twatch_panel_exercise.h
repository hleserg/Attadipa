#pragma once

namespace attadipa::firmware {

// §11's order is part of the experiment: draw first, then geometry, then the
// repeated display cycles, and finally compare the known-short and conforming
// sleep intervals. Ops owns the hardware calls; this keeps the sequence host
// testable without copying it into a fixture.
template <typename Ops>
bool run_twatch_panel_exercise(Ops &ops) {
  if (!ops.show_patterns() || !ops.rotation_and_gap()) {
    return false;
  }
  for (unsigned cycle = 1; cycle <= 10; ++cycle) {
    if (!ops.display_cycle(cycle)) {
      return false;
    }
  }
  return ops.sleep_cycle(false) && ops.sleep_cycle(true);
}

} // namespace attadipa::firmware
