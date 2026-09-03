#include <cstdio>

#include "display_rollback.h"

using attadipa::firmware::display_rollback;
using attadipa::firmware::DisplayQuiescence;
using attadipa::firmware::DisplayRollback;

int main() {
  // Nothing registered has no DMA-owned display object to retain.
  if (display_rollback(false, DisplayQuiescence::Unknown, false) !=
      DisplayRollback::Release)
    return 1;
  // A proven-idle display may be released only while rollback owns LVGL.
  if (display_rollback(true, DisplayQuiescence::Proven, true) !=
      DisplayRollback::Release)
    return 1;
  // Pending/unknown completion retains even while the LVGL mutex is held:
  // that mutex is not the DMA completion barrier.
  if (display_rollback(true, DisplayQuiescence::Unknown, true) !=
      DisplayRollback::Retain)
    return 1;
  // A lock timeout retains regardless of the caller's prior knowledge.
  if (display_rollback(true, DisplayQuiescence::Proven, false) !=
      DisplayRollback::Retain)
    return 1;
  std::puts("all display rollback checks passed");
  return 0;
}
