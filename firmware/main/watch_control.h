#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"

// Starts the physical/remote input path and the existing watch-control
// protocol. Called with LVGL locked; all mutable state remains on LVGL's task.
esp_err_t start_watch_control(esp_lcd_touch_handle_t touch,
                              i2c_master_dev_handle_t pmu);
