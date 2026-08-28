#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

namespace attadipa::debug {
class TimeSink;
class MeshSink;
}

// Starts the physical/remote input path and the existing watch-control
// protocol. Called with LVGL locked; all mutable state remains on LVGL's task.
esp_err_t start_watch_control(esp_lcd_touch_handle_t touch,
                              i2c_master_dev_handle_t pmu,
                              esp_lcd_panel_handle_t panel,
                              std::uint8_t awake_brightness,
                              void (*refresh_ui)(),
                              attadipa::debug::TimeSink *time_sink,
                              attadipa::debug::MeshSink *mesh_sink);
