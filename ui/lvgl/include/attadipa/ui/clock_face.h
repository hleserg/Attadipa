#pragma once

#include "attadipa/apps/clock.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"
#include "lvgl.h"

namespace attadipa::ui {

struct ClockFaceConfig {
  unsigned width_px = 240;
  unsigned height_px = 240;
  Theme theme = Theme::Night;
  PixelCost pixel_cost = PixelCost::Fixed;
  Metrics metrics = Metrics::unscaled();
};

class ClockFace {
public:
  void build(lv_obj_t *screen, const ClockFaceConfig &config,
             const apps::ClockText &text);
  void update(const apps::ClockText &text);

private:
  static constexpr unsigned kFireflyCount = 6;

  static void motion_tick(lv_timer_t *timer);
  static void touch_event(lv_event_t *event);
  void animate();
  void touch(lv_event_t *event);

  ClockFaceConfig config_{};
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *digits_[5]{};
  lv_obj_t *date_ = nullptr;
  lv_obj_t *seconds_ = nullptr;
  lv_obj_t *status_ = nullptr;
  lv_obj_t *timeline_ = nullptr;
  lv_obj_t *timeline_fill_ = nullptr;
  lv_obj_t *timeline_dot_ = nullptr;
  lv_obj_t *timeline_value_ = nullptr;
  lv_obj_t *year_ = nullptr;
  lv_obj_t *weekday_dots_[7]{};
  lv_obj_t *firefly_halos_[kFireflyCount]{};
  lv_obj_t *fireflies_[kFireflyCount]{};
  lv_obj_t *brand_firefly_ = nullptr;
  lv_obj_t *brand_wings_[2]{};
  lv_obj_t *ripple_ = nullptr;
  lv_timer_t *motion_timer_ = nullptr;
  unsigned timeline_width_ = 0;
  unsigned phase_ = 0;
  unsigned ripple_step_ = 0;
  unsigned touch_ticks_ = 0;
  int touch_x_ = 0;
  int touch_y_ = 0;
  int brand_x_ = 0;
  int brand_y_ = 0;
  bool built_ = false;
};

} // namespace attadipa::ui
