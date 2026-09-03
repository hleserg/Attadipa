#pragma once

namespace attadipa::firmware {

// Boot rollback, once a display exists.
//
// Both boards answer the same question -- what may a failed boot free while an
// LVGL display is registered? -- and they answer it differently. Not because of
// a hardware fact: what differs is `lv_indev_t` ownership in this repository's
// own C++, which is a software fact and is cited as one below. The question is
// one: the pinned
// `esp_lvgl_port` display API has add and remove and no completion barrier, so
// a queued SPI DMA callback cannot be proven finished, and freeing a display,
// its framebuffer, panel, panel IO or host is a use-after-free (#367 item 6).
// The DMA argument reaches exactly that stack and nothing else -- it authorises
// retaining neither board's I2C.
//
// What separates the two answers is who owns the touch handle:
//
//   * On the T-Watch the LVGL port owns it -- `twatch_board.cpp` registers it
//     with `lvgl_port_add_touch()` and rollback removes no indev, so an LVGL
//     that is still running still reads that `esp_lcd_touch_t`. Retaining the
//     display therefore has to retain touch and the bus under it as well.
//   * On the Waveshare the input service owns it: it creates its own
//     `lv_indev_t` and deletes it on its own failure, so nothing LVGL keeps
//     points at the touch controller. Only the display stack is retained.
//
// Both are template-only and board-agnostic so the decision is testable off a
// board: `tests/test_boot_rollback.cpp` pins every branch of both.

// The T-Watch answer: a registered display retains the whole stack, touch and
// buses included, because LVGL holds the touch handle.
template <typename Ops>
void rollback_boot_retaining_all(Ops &ops) {
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

// The Waveshare answer: a registered display retains the display stack, and
// everything the DMA argument does not reach is released either way. The power
// owner is detached first, so a retained panel cannot leave it paired with a
// PMU handle that is about to go.
template <typename Ops>
void rollback_boot_retaining_display(Ops &ops) {
  if (ops.display_registered()) {
    ops.retain_display_stack();
  } else {
    ops.stop_lvgl();
    ops.remove_panel();
    ops.remove_panel_io();
    ops.free_spi();
  }
  ops.detach_power_owner();
  ops.remove_touch();
  ops.remove_touch_io();
  ops.remove_rtc();
  ops.remove_pmu();
  ops.remove_main_bus();
}

} // namespace attadipa::firmware
