#pragma once

#include "attadipa/apps/mesh.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"
#include "lvgl.h"

namespace attadipa::ui {

struct StatusFrameConfig {
  unsigned width_px = 240;
  unsigned height_px = 240;
  Theme theme = Theme::Night;
  PixelCost pixel_cost = PixelCost::Fixed;
  Metrics metrics = Metrics::unscaled();
};

// The active screen owns both the status strip and the application's child.
// Clear the outgoing face before build(); restore the child's geometry after
// the incoming face removes its inherited styles. No input or timer owner.
class StatusFrame {
public:
  void build(lv_obj_t *screen, const StatusFrameConfig &config);
  void update(const apps::MeshText &text);
  void restore_content_geometry();
  lv_obj_t *content() const { return content_; }
  unsigned content_height() const { return config_.height_px - header_height_; }

private:
  StatusFrameConfig config_{};
  unsigned header_height_ = 0;
  lv_obj_t *content_ = nullptr;
  lv_obj_t *watch_ = nullptr;
  lv_obj_t *node_ = nullptr;
  lv_obj_t *link_[apps::kChannelRungs]{};
};

} // namespace attadipa::ui
