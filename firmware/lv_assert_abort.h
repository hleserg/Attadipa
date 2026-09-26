#pragma once

// LVGL includes this as `LV_ASSERT_HANDLER_INCLUDE`, after its own config has
// defined the handler as `while(1);`, and this replaces it (#653). A failed
// assertion then ends the process instead of spinning with the LVGL port lock
// held; `ui/lvgl/status_frame.cpp` refuses to build if it does not.
//
// On the device abort() is a panic, and firmware/sdkconfig.defaults pins the
// panic action: CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT with a 0 s delay, which a
// Waveshare build's generated sdkconfig.h carries (MEASURED, ESP-IDF v5.5.5).
// So an assertion prints a backtrace on serial and reboots. One that repeats
// on every boot, such as create_ui() running the pool dry, becomes a reboot
// loop; that is kept on purpose, because each pass says why on serial, where
// the spin said nothing.

#include <stdlib.h>

#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER abort();
