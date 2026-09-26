#pragma once

// LVGL includes this as `LV_ASSERT_HANDLER_INCLUDE`, after its own config has
// defined the handler as `while(1);`, and this replaces it (#653). A failed
// assertion then ends the process instead of spinning with the LVGL port lock
// held; `ui/lvgl/status_frame.cpp` refuses to build if it does not.

#include <stdlib.h>

#undef LV_ASSERT_HANDLER
#define LV_ASSERT_HANDLER abort();
