#pragma once

#include "esp_err.h"

// The T-Watch S3 Plus board backend, first slice: panel, touch and the two
// rails they hang from, plus a bring-up screen whose photograph is the pass
// criterion (docs/research/TWATCH_S3_PLUS_BSP_REUSE.md §11). Returns the
// display's error; a dead touch is logged and reported, not fatal (§10.6).
esp_err_t start_twatch_ui();
