#include "attadipa/ui/clock_face.h"

#include <algorithm>

#include "attadipa_fonts.h"
#include "generated/attadipa_images.h"

namespace attadipa::ui {
namespace {

constexpr std::uint32_t kMotionPeriodMs = 50;
constexpr unsigned kTouchMotionTicks = 32;

lv_color_t resolved(ColorRole role, Theme theme, PixelCost pixel_cost) {
  const auto value = color(role, theme, pixel_cost);
  return value ? lv_color_hex(value->packed()) : lv_color_black();
}

void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

void circle(lv_obj_t *object, int size, lv_color_t colour, lv_opa_t opacity) {
  bare(object);
  lv_obj_set_size(object, size, size);
  lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, colour, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opacity, LV_PART_MAIN);
}

void track_transient(void *, int32_t) {}

} // namespace

void ClockFace::build(lv_obj_t *screen, const ClockFaceConfig &config,
                      const apps::ClockText &text) {
  built_ = false;
  if (motion_timer_ != nullptr) {
    lv_timer_delete(motion_timer_);
    motion_timer_ = nullptr;
  }
  lv_anim_delete(this, track_transient);
  if (screen_ == screen) {
    lv_obj_remove_event_cb_with_user_data(screen, touch_event, this);
  }
  screen_ = screen;
  config_ = config;
  touch_glow_ticks_ = 0;
  leaf_pulse_tick_ = 0;
  for (lv_obj_t *&dot : leaf_fireflies_) {
    dot = nullptr;
  }
  lv_obj_clean(screen);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, config.width_px, config.height_px);
  const lv_color_t page =
      resolved(ColorRole::BackgroundPrimary, config.theme, config.pixel_cost);
  const lv_color_t surface =
      resolved(ColorRole::BackgroundSurface, config.theme, config.pixel_cost);
  const lv_color_t ink =
      resolved(ColorRole::TextPrimary, config.theme, config.pixel_cost);
  const lv_color_t muted =
      resolved(ColorRole::TextMuted, config.theme, config.pixel_cost);
  const lv_color_t accent =
      resolved(ColorRole::AccentPrimary, config.theme, config.pixel_cost);
  const lv_color_t glow =
      resolved(ColorRole::AccentGlow, config.theme, config.pixel_cost);
  const lv_color_t border =
      resolved(ColorRole::BorderSubtle, config.theme, config.pixel_cost);

  lv_obj_set_style_bg_color(screen, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, touch_event, LV_EVENT_PRESSED, this);

  const bool large = config.width_px >= 400;

  if (config.theme == Theme::Night) {
    lv_obj_t *background = lv_image_create(screen);
    bare(background);
    lv_image_set_src(background,
                     &attadipa_background_clock_meadow_night_410x502);
    if (!large) {
      const unsigned scale = std::max(config.width_px * 256U / 410U,
                                      config.height_px * 256U / 502U);
      lv_image_set_scale(background, scale);
    }
    lv_obj_align(background, LV_ALIGN_CENTER, 0, 0);

    const int firefly_x[] = {13, 28, 77, 88};
    const int firefly_y[] = {77, 84, 78, 86};
    for (unsigned i = 0; i < 4; ++i) {
      lv_obj_t *&dot = leaf_fireflies_[i];
      dot = lv_obj_create(screen);
      circle(dot, large ? 5 : 4, glow, LV_OPA_20);
      lv_obj_set_pos(dot, config.width_px * firefly_x[i] / 100,
                     config.height_px * firefly_y[i] / 100);
      lv_obj_set_style_shadow_color(dot, glow, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(dot, large ? 10 : 7, LV_PART_MAIN);
    }
  }

  const lv_font_t *numeral =
      large ? &attadipa_nunito_sans_84 : &attadipa_nunito_sans_64;
  const lv_font_t *date_font =
      large ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_20;
  const lv_font_t *status_font =
      large ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_16;
  lv_obj_set_style_text_font(screen, date_font, LV_PART_MAIN);
  const int horizontal_margin = large ? 28 : 14;
  const int row_width =
      static_cast<int>(config.width_px) - horizontal_margin * 2;
  const int colon_width = large ? 30 : 18;
  const int digit_width = (row_width - colon_width) / 4;

  touch_glow_halo_ = lv_obj_create(screen);
  touch_glow_dot_ = lv_obj_create(screen);
  circle(touch_glow_halo_, large ? 18 : 14, glow, LV_OPA_TRANSP);
  circle(touch_glow_dot_, large ? 6 : 4, glow, LV_OPA_TRANSP);
  lv_obj_add_flag(touch_glow_halo_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(touch_glow_dot_, LV_OBJ_FLAG_HIDDEN);

  date_ = lv_label_create(screen);
  bare(date_);
  lv_obj_set_width(date_, row_width);
  lv_obj_set_style_text_font(date_, date_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(date_, accent, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(date_, large ? 2 : 1, LV_PART_MAIN);
  lv_label_set_long_mode(date_, LV_LABEL_LONG_CLIP);
  lv_obj_align(date_, LV_ALIGN_TOP_MID, 0, large ? 48 : 18);

  lv_obj_t *row = lv_obj_create(screen);
  bare(row);
  lv_obj_set_size(row, row_width, numeral->line_height + 8);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, large ? -38 : -26);

  int x = 0;
  for (unsigned i = 0; i < 5; ++i) {
    const int cell_width = i == 2 ? colon_width : digit_width;
    digits_[i] = lv_label_create(row);
    bare(digits_[i]);
    lv_obj_set_size(digits_[i], cell_width, numeral->line_height + 8);
    lv_obj_set_pos(digits_[i], x, 0);
    lv_obj_set_style_text_font(digits_[i], numeral, LV_PART_MAIN);
    lv_obj_set_style_text_align(digits_[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(digits_[i], LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(digits_[i], i == 2 ? accent : ink,
                                LV_PART_MAIN);
    x += cell_width;
  }

  seconds_ = lv_label_create(screen);
  bare(seconds_);
  lv_obj_set_size(seconds_, large ? 50 : 38, large ? 38 : 28);
  lv_obj_set_style_radius(seconds_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(seconds_, surface, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(seconds_, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_width(seconds_, config.metrics.px(Dp{1}),
                                LV_PART_MAIN);
  lv_obj_set_style_border_color(seconds_, border, LV_PART_MAIN);
  lv_obj_set_style_border_opa(seconds_, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_text_font(seconds_, status_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(seconds_, muted, LV_PART_MAIN);
  lv_obj_set_style_text_align(seconds_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_pad_top(seconds_, large ? 7 : 4, LV_PART_MAIN);
  lv_obj_align(seconds_, LV_ALIGN_CENTER, large ? 155 : 91, large ? 46 : 34);

  steps_ = lv_obj_create(screen);
  bare(steps_);
  lv_obj_set_size(steps_, large ? 84 : 62, large ? 30 : 20);
  lv_obj_align(steps_, LV_ALIGN_CENTER, 0, large ? 20 : 34);

  lv_obj_t *paw = lv_obj_create(steps_);
  bare(paw);
  lv_obj_set_size(paw, large ? 28 : 19, large ? 25 : 17);
  lv_obj_set_pos(paw, 0, large ? 2 : 1);
  const int toe_size = large ? 5 : 4;
  const int toe_x[] = {0, large ? 7 : 5, large ? 14 : 10, large ? 21 : 15};
  const int toe_y[] = {large ? 4 : 3, 0, 0, large ? 4 : 3};
  for (unsigned i = 0; i < 4; ++i) {
    lv_obj_t *toe = lv_obj_create(paw);
    circle(toe, toe_size, glow, LV_OPA_COVER);
    lv_obj_set_pos(toe, toe_x[i], toe_y[i]);
  }
  lv_obj_t *pad = lv_obj_create(paw);
  circle(pad, large ? 14 : 10, glow, LV_OPA_COVER);
  lv_obj_set_height(pad, large ? 10 : 7);
  lv_obj_set_pos(pad, large ? 7 : 5, large ? 13 : 9);

  lv_obj_t *steps_value = lv_label_create(steps_);
  bare(steps_value);
  lv_label_set_text(steps_value, "7777");
  lv_obj_set_style_text_font(steps_value, status_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(steps_value, muted, LV_PART_MAIN);
  lv_obj_align(steps_value, LV_ALIGN_RIGHT_MID, 0, 0);

  status_ = lv_label_create(screen);
  bare(status_);
  lv_obj_set_width(status_, row_width);
  lv_obj_set_style_text_font(status_, status_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(
      status_, resolved(ColorRole::Warning, config.theme, config.pixel_cost),
      LV_PART_MAIN);
  lv_label_set_long_mode(status_, LV_LABEL_LONG_CLIP);
  lv_obj_align(status_, LV_ALIGN_BOTTOM_MID, 0, large ? -82 : -28);

  built_ = true;
  update(text);
  motion_timer_ = lv_timer_create(motion_tick, kMotionPeriodMs, this);
  if (config.theme != Theme::Night) {
    lv_timer_pause(motion_timer_);
  }
}

void ClockFace::update(const apps::ClockText &text) {
  if (!built_) {
    return;
  }
  for (unsigned i = 0; i < 5; ++i) {
    lv_label_set_text_fmt(digits_[i], "%c", text.time[i]);
  }
  lv_label_set_text(seconds_, text.seconds);
  lv_label_set_text(date_, text.date);
  lv_label_set_text(status_, text.status);
  text.date[0] == '\0' ? lv_obj_add_flag(date_, LV_OBJ_FLAG_HIDDEN)
                       : lv_obj_remove_flag(date_, LV_OBJ_FLAG_HIDDEN);
  text.status[0] == '\0' ? lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN)
                         : lv_obj_remove_flag(status_, LV_OBJ_FLAG_HIDDEN);
  text.ready && text.status[0] == '\0'
      ? lv_obj_remove_flag(steps_, LV_OBJ_FLAG_HIDDEN)
      : lv_obj_add_flag(steps_, LV_OBJ_FLAG_HIDDEN);
  text.ready ? lv_obj_remove_flag(seconds_, LV_OBJ_FLAG_HIDDEN)
             : lv_obj_add_flag(seconds_, LV_OBJ_FLAG_HIDDEN);
}

void ClockFace::motion_tick(lv_timer_t *timer) {
  static_cast<ClockFace *>(lv_timer_get_user_data(timer))->animate();
}

void ClockFace::touch_event(lv_event_t *event) {
  static_cast<ClockFace *>(lv_event_get_user_data(event))->touch(event);
}

void ClockFace::touch(lv_event_t *event) {
  lv_indev_t *indev = lv_event_get_indev(event);
  if (indev == nullptr) {
    return;
  }
  lv_point_t point{};
  lv_indev_get_point(indev, &point);
  static constexpr int kDxQ4[] = {32, 26, 0, -26, -32, -26, 0, 26};
  static constexpr int kDyQ4[] = {0, 20, 32, 20, 0, -20, -32, -20};
  const unsigned direction =
      (static_cast<unsigned>(point.x) * 31U +
       static_cast<unsigned>(point.y) * 17U + ++touch_sequence_ * 47U) %
      8U;
  touch_glow_x_q4_ = point.x * 16;
  touch_glow_y_q4_ = point.y * 16;
  touch_glow_dx_q4_ = kDxQ4[direction];
  touch_glow_dy_q4_ = kDyQ4[direction];
  touch_glow_ticks_ = kTouchMotionTicks;
  lv_anim_t transient;
  lv_anim_init(&transient);
  lv_anim_set_var(&transient, this);
  lv_anim_set_exec_cb(&transient, track_transient);
  lv_anim_set_values(&transient, 0, 1);
  lv_anim_set_duration(&transient, kMotionPeriodMs * kTouchMotionTicks);
  lv_anim_start(&transient);
  lv_obj_move_foreground(touch_glow_halo_);
  lv_obj_move_foreground(touch_glow_dot_);
  lv_obj_remove_flag(touch_glow_halo_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(touch_glow_dot_, LV_OBJ_FLAG_HIDDEN);
  lv_timer_resume(motion_timer_);
}

void ClockFace::animate() {
  if (!built_) {
    return;
  }
  if (leaf_fireflies_[0] != nullptr) {
    static constexpr unsigned kHalfPeriod[] = {25, 36, 43, 31};
    static constexpr unsigned kPhase[] = {0, 9, 18, 26};
    ++leaf_pulse_tick_;
    for (unsigned i = 0; i < 4; ++i) {
      const unsigned cycle = kHalfPeriod[i] * 2U;
      const unsigned phase = (leaf_pulse_tick_ + kPhase[i]) % cycle;
      const unsigned level = phase <= kHalfPeriod[i] ? phase : cycle - phase;
      const lv_opa_t opacity = static_cast<lv_opa_t>(
          LV_OPA_20 + (LV_OPA_COVER - LV_OPA_20) * level / kHalfPeriod[i]);
      lv_obj_set_style_bg_opa(leaf_fireflies_[i], opacity, LV_PART_MAIN);
      lv_obj_set_style_shadow_opa(leaf_fireflies_[i], opacity / 2,
                                  LV_PART_MAIN);
    }
  }
  if (touch_glow_ticks_ == 0) {
    if (leaf_fireflies_[0] == nullptr) {
      lv_timer_pause(motion_timer_);
    }
    return;
  }

  const unsigned pulse_phase = touch_glow_ticks_ % 8U;
  const int pulse =
      static_cast<int>(pulse_phase <= 4U ? pulse_phase : 8U - pulse_phase);
  const bool large = config_.width_px >= 400;
  const int dot_size = (large ? 5 : 3) + pulse / 2;
  const int halo_size = (large ? 15 : 11) + pulse;
  const lv_opa_t opacity =
      static_cast<lv_opa_t>(std::min(touch_glow_ticks_ * 12U, 255U));
  const int x = touch_glow_x_q4_ / 16;
  const int y = touch_glow_y_q4_ / 16;
  lv_obj_set_size(touch_glow_halo_, halo_size, halo_size);
  lv_obj_set_pos(touch_glow_halo_, x - halo_size / 2, y - halo_size / 2);
  lv_obj_set_style_bg_opa(touch_glow_halo_, opacity / 4, LV_PART_MAIN);
  lv_obj_set_size(touch_glow_dot_, dot_size, dot_size);
  lv_obj_set_pos(touch_glow_dot_, x - dot_size / 2, y - dot_size / 2);
  lv_obj_set_style_bg_opa(touch_glow_dot_, opacity, LV_PART_MAIN);

  touch_glow_x_q4_ += touch_glow_dx_q4_;
  touch_glow_y_q4_ += touch_glow_dy_q4_;
  const int margin_q4 = (large ? 10 : 7) * 16;
  if (touch_glow_x_q4_ < margin_q4 ||
      touch_glow_x_q4_ >
          (static_cast<int>(config_.width_px) * 16 - margin_q4)) {
    touch_glow_dx_q4_ = -touch_glow_dx_q4_;
  }
  if (touch_glow_y_q4_ < margin_q4 ||
      touch_glow_y_q4_ >
          (static_cast<int>(config_.height_px) * 16 - margin_q4)) {
    touch_glow_dy_q4_ = -touch_glow_dy_q4_;
  }

  if (--touch_glow_ticks_ == 0) {
    lv_obj_add_flag(touch_glow_halo_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(touch_glow_dot_, LV_OBJ_FLAG_HIDDEN);
    if (leaf_fireflies_[0] == nullptr) {
      lv_timer_pause(motion_timer_);
    }
  }
}

} // namespace attadipa::ui
