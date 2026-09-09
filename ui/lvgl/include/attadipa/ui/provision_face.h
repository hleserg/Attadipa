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

// The entry screen: a column of lines above, six keys below. Every key goes
// straight to `ProvisioningEntry::press()` and the face redraws from `text()`;
// it has no state of its own beyond the objects it made.
//
// **This is a scaffold, not the designed face.** It exists so the model and its
// two callers compile and can be looked at; the visual identity of this screen
// — art, type, rhythm, the treatment of the receipt — is #469's face handoff
// and belongs to the design owner. What it does promise is the model's own
// contract: a line the model leaves empty is not drawn, a key the model does
// not offer is not shown, and the key that acts is marked by the field it is
// on rather than by the word on it. How far that marking goes is the palette's
// limit, not this file's: see the note over the fill in `provision_face.cpp`.
class ProvisionFace {
public:
  // `EntryKey` has six values and the face draws all six. Public because the
  // key table lives in the implementation and asserts against this, so a key
  // added to the model fails to compile here rather than going undrawn.
  static constexpr unsigned kKeyCount = 6;

  void build(lv_obj_t *screen, const ProvisionFaceConfig &config,
             apps::ProvisioningEntry &entry);
  void update();
  void clear();

private:
  static void key_event(lv_event_t *event);

  ProvisionFaceConfig config_{};
  apps::ProvisioningEntry *entry_ = nullptr;
  lv_obj_t *lines_ = nullptr;
  lv_obj_t *title_ = nullptr;
  lv_obj_t *value_ = nullptr;
  lv_obj_t *draft_ = nullptr;
  // Null on the small panel, which has no line to spare for it.
  lv_obj_t *utc_ = nullptr;
  lv_obj_t *instruction_ = nullptr;
  lv_obj_t *verdict_ = nullptr;
  lv_obj_t *keypad_ = nullptr;
  lv_obj_t *keys_[kKeyCount] = {};
  bool small_panel_ = false;
  bool built_ = false;
};

} // namespace attadipa::ui
