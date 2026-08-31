#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#include "attadipa/core/input.h"

// Everything a person can do to this watch with their hands: the FT3168 touch
// panel, GPIO0/BOOT, the AXP2101 power button, and the sleep and wake
// choreography those drive. It is built into every product image.
//
// This is the half of the old watch_control.cpp that is NOT optional. #346:
// one Kconfig symbol used to own both this and the USB control plane, so the
// only way to build a watch without a remote debug endpoint was to build one
// whose buttons did not work. The remote transport now feeds
// InputOrigin::Remote into the queue this owns; it is not what makes the queue
// exist.
//
// Called with LVGL locked. All mutable state stays on LVGL's task.
esp_err_t start_physical_input(esp_lcd_touch_handle_t touch,
                               i2c_master_dev_handle_t pmu,
                               esp_lcd_panel_handle_t panel,
                               std::uint8_t awake_brightness,
                               void (*refresh_ui)());

// The queue and the state the physical path owns, for a transport that wants to
// post InputOrigin::Remote into the same path a finger takes. Null until
// start_physical_input() has returned ESP_OK, which is the order
// start_waveshare_ui() calls them in.
attadipa::core::InputQueue *physical_input_queue();
attadipa::core::InputState *physical_input_state();

// Light-sleep cycles completed since boot. A transport holding session state
// across polls compares this against its own last-seen value: the device slept
// while the transport was not running, so whatever it believed about its peer
// -- decoder position, queued output, the connection itself -- is stale. The
// physical path does not know a transport exists; this is the whole of what it
// publishes for one.
std::uint32_t physical_input_sleep_cycles();
