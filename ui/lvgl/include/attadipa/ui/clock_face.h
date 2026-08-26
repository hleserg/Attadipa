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
  void clear();

private:
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
  lv_obj_t *steps_ = nullptr;
  lv_obj_t *leaf_fireflies_[4]{};
  lv_obj_t *touch_glow_halo_ = nullptr;
  lv_obj_t *touch_glow_dot_ = nullptr;
  lv_timer_t *motion_timer_ = nullptr;
  unsigned leaf_pulse_tick_ = 0;
  unsigned touch_glow_ticks_ = 0;
  unsigned touch_sequence_ = 0;
  int touch_glow_x_q4_ = 0;
  int touch_glow_y_q4_ = 0;
  int touch_glow_dx_q4_ = 0;
  int touch_glow_dy_q4_ = 0;
  bool built_ = false;
};

} // namespace attadipa::ui
