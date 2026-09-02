#pragma once

#include "attadipa/apps/provisioning.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"
#include "lvgl.h"

namespace attadipa::ui {

struct ProvisionFaceConfig {
  unsigned width_px = 240;
  unsigned height_px = 240;
  Theme theme = Theme::Night;
  PixelCost pixel_cost = PixelCost::Fixed;
  Metrics metrics = Metrics::unscaled();
  l10n::Locale locale = l10n::Locale::En;
};

// The entry screen: a field above, twelve keys below. Every key goes straight
// to `ProvisioningEntry::press()` and the face redraws from `text()`; it has
// no state of its own beyond the objects it made.
class ProvisionFace {
public:
  void build(lv_obj_t *screen, const ProvisionFaceConfig &config,
             apps::ProvisioningEntry &entry);
  void update();
  void clear();

private:
  static void key_event(lv_event_t *event);

  ProvisionFaceConfig config_{};
  apps::ProvisioningEntry *entry_ = nullptr;
  lv_obj_t *title_ = nullptr;
  lv_obj_t *value_ = nullptr;
  lv_obj_t *hint_ = nullptr;
  lv_obj_t *keypad_ = nullptr;
  lv_obj_t *sign_key_ = nullptr;
  lv_obj_t *ok_key_ = nullptr;
  lv_obj_t *backspace_key_ = nullptr;
  bool built_ = false;
};

} // namespace attadipa::ui
