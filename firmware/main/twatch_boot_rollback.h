#pragma once

namespace attadipa::firmware {

// Before a display exists, every acquired handle can be released in reverse.
// Once LVGL owns a display, its SPI DMA completion is asynchronous: retain the
// stack until a bounded quiescence barrier exists (#367), rather than freeing a
// framebuffer or display that a pending callback may still use.
template <typename Ops>
void rollback_twatch_boot(Ops &ops) {
  if (ops.display_registered()) {
    ops.retain_display_stack();
    return;
  }
  ops.remove_touch();
  ops.remove_touch_io();
  ops.remove_touch_bus();
  ops.stop_lvgl();
  ops.remove_panel();
  ops.remove_panel_io();
  ops.free_spi();
  ops.remove_pmu();
  ops.remove_main_bus();
}

} // namespace attadipa::firmware
