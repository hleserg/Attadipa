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

// Starts the USB watch-control transport: screenshots, remote input, time and
// mesh opcodes over USB-Serial/JTAG. Development only -- a production image is
// built without it, and physical touch, buttons and sleep are unaffected
// because start_physical_input() owns those (#346).
//
// Called with LVGL locked, and after start_physical_input(), whose queue and
// state this feeds. All mutable state remains on LVGL's task.
esp_err_t start_watch_control(attadipa::debug::TimeSink *time_sink,
                              attadipa::debug::MeshSink *mesh_sink);
