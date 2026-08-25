#include "attadipa/ui/clock_face.h"

#include <algorithm>

#include "attadipa_fonts.h"

namespace attadipa::ui {
namespace {

lv_color_t resolved(ColorRole role, Theme theme, PixelCost pixel_cost) {
  const auto value = color(role, theme, pixel_cost);
  return value ? lv_color_hex(value->packed()) : lv_color_black();
}

void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void circle(lv_obj_t *object, int size, lv_color_t colour, lv_opa_t opacity) {
  bare(object);
  lv_obj_set_size(object, size, size);
  lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, colour, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opacity, LV_PART_MAIN);
}

int wave(unsigned value) {
  const unsigned phase = value % 200;
  return static_cast<int>(phase <= 100 ? phase : 200 - phase);
}

} // namespace

void ClockFace::build(lv_obj_t *screen, const ClockFaceConfig &config,
                      const apps::ClockText &text) {
  built_ = false;
  if (motion_timer_ != nullptr) {
    lv_timer_delete(motion_timer_);
    motion_timer_ = nullptr;
  }
  if (screen_ == screen) {
    lv_obj_remove_event_cb_with_user_data(screen, touch_event, this);
  }
  screen_ = screen;
  config_ = config;
  phase_ = 0;
  ripple_step_ = 0;
  touch_ticks_ = 0;
  brand_firefly_ = nullptr;
  brand_wings_[0] = nullptr;
  brand_wings_[1] = nullptr;
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
  const lv_color_t meadow =
      resolved(ColorRole::Success, config.theme, config.pixel_cost);
  const lv_color_t teal =
      resolved(ColorRole::Navigation, config.theme, config.pixel_cost);
  const lv_color_t border =
      resolved(ColorRole::BorderSubtle, config.theme, config.pixel_cost);

  lv_obj_set_style_bg_color(screen, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, touch_event, LV_EVENT_PRESSED, this);

  const bool large = config.width_px >= 400;
  const lv_font_t *numeral =
      large ? &attadipa_nunito_sans_96 : &attadipa_nunito_sans_64;
  const lv_font_t *date_font =
      large ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_20;
  const lv_font_t *status_font =
      large ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_16;
  const lv_font_t *caption_font =
      large ? &attadipa_nunito_sans_16 : &attadipa_nunito_sans_14;
  lv_obj_set_style_text_font(screen, date_font, LV_PART_MAIN);
  const int horizontal_margin = large ? 28 : 14;
  const int row_width =
      static_cast<int>(config.width_px) - horizontal_margin * 2;
  const int colon_width = large ? 30 : 18;
  const int digit_width = (row_width - colon_width) / 4;

  // Two soft organic fields and moving points carry the reference board's
  // meadow/firefly language without scaling a desktop illustration onto a
  // watch. They stay behind all information and use only semantic colours.
  lv_obj_t *meadow_blob = lv_obj_create(screen);
  circle(meadow_blob, large ? 260 : 150, meadow, LV_OPA_20);
  lv_obj_set_pos(meadow_blob, large ? -90 : -55, large ? 270 : 125);
  lv_obj_t *teal_blob = lv_obj_create(screen);
  circle(teal_blob, large ? 210 : 130, teal, LV_OPA_10);
  lv_obj_set_pos(teal_blob, large ? 275 : 165, large ? -65 : -45);

  for (unsigned i = 0; i < kFireflyCount; ++i) {
    firefly_halos_[i] = lv_obj_create(screen);
    fireflies_[i] = lv_obj_create(screen);
    const int dot_size =
        (large ? 5 : 3) + (text.mode == apps::ClockMode::Child ? 2 : 0);
    circle(firefly_halos_[i], dot_size * 4, glow, LV_OPA_20);
    circle(fireflies_[i], dot_size, i % 3 == 0 ? accent : glow, LV_OPA_COVER);
  }

  ripple_ = lv_obj_create(screen);
  bare(ripple_);
  lv_obj_set_style_radius(ripple_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ripple_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(ripple_, large ? 3 : 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(ripple_, glow, LV_PART_MAIN);
  lv_obj_set_style_border_opa(ripple_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(ripple_, LV_OBJ_FLAG_HIDDEN);

  // A tiny, deliberately geometric echo of the firefly mark. The 240 px face
  // has no honest room for it; on the tall panel its wings can breathe.
  if (large) {
    brand_firefly_ = lv_obj_create(screen);
    bare(brand_firefly_);
    lv_obj_remove_flag(brand_firefly_, LV_OBJ_FLAG_CLICKABLE);
    constexpr int mark_width = 76;
    constexpr int mark_height = 80;
    lv_obj_set_size(brand_firefly_, mark_width, mark_height);
    brand_x_ = (static_cast<int>(config.width_px) - mark_width) / 2;
    brand_y_ = 276;
    lv_obj_set_pos(brand_firefly_, brand_x_, brand_y_);

    lv_obj_t *tail_halo = lv_obj_create(brand_firefly_);
    circle(tail_halo, 36, glow, LV_OPA_20);
    lv_obj_set_pos(tail_halo, config.metrics.px(Dp{10}),
                   config.metrics.px(Dp{20}));
    for (unsigned i = 0; i < 2; ++i) {
      brand_wings_[i] = lv_obj_create(brand_firefly_);
      bare(brand_wings_[i]);
      lv_obj_set_size(brand_wings_[i], config.metrics.px(Dp{14}),
                      config.metrics.px(Dp{19}));
      lv_obj_set_style_radius(brand_wings_[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
      lv_obj_set_style_bg_color(brand_wings_[i], accent, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(brand_wings_[i], LV_OPA_70, LV_PART_MAIN);
      lv_obj_set_pos(brand_wings_[i],
                     config.metrics.px(i == 0 ? Dp{2} : Dp{23}),
                     config.metrics.px(Dp{11}));
    }
    lv_obj_t *body = lv_obj_create(brand_firefly_);
    bare(body);
    lv_obj_set_size(body, config.metrics.px(Dp{8}), config.metrics.px(Dp{15}));
    lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(body, muted, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_pos(body, config.metrics.px(Dp{16}), config.metrics.px(Dp{10}));
    lv_obj_t *head = lv_obj_create(brand_firefly_);
    circle(head, 16, muted, LV_OPA_COVER);
    lv_obj_set_pos(head, config.metrics.px(Dp{16}), config.metrics.px(Dp{4}));
    for (int x : {22, 51}) {
      lv_obj_t *antenna_tip = lv_obj_create(brand_firefly_);
      circle(antenna_tip, 6, glow, LV_OPA_COVER);
      lv_obj_set_pos(antenna_tip, x, 0);
    }
    lv_obj_t *tail = lv_obj_create(brand_firefly_);
    bare(tail);
    lv_obj_set_size(tail, config.metrics.px(Dp{9}), config.metrics.px(Dp{15}));
    lv_obj_set_style_radius(tail, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tail, glow, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tail, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_pos(tail, config.metrics.px(Dp{15}), config.metrics.px(Dp{22}));
  }

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
  lv_obj_align(seconds_, LV_ALIGN_CENTER, large ? 155 : 0, large ? 46 : 34);

  timeline_ = lv_obj_create(screen);
  bare(timeline_);
  const int timeline_height = large ? 104 : 58;
  lv_obj_set_size(timeline_, row_width, timeline_height);
  lv_obj_set_style_radius(timeline_, large ? 28 : 18, LV_PART_MAIN);
  lv_obj_set_style_bg_color(timeline_, surface, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(timeline_, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_border_width(timeline_, config.metrics.px(Dp{1}),
                                LV_PART_MAIN);
  lv_obj_set_style_border_color(timeline_, border, LV_PART_MAIN);
  lv_obj_set_style_border_opa(timeline_, LV_OPA_30, LV_PART_MAIN);
  lv_obj_align(timeline_, LV_ALIGN_BOTTOM_MID, 0, large ? -36 : -12);

  const int inset = large ? 26 : 16;
  timeline_width_ = static_cast<unsigned>(row_width - inset * 2);
  lv_obj_t *timeline_track = lv_obj_create(timeline_);
  bare(timeline_track);
  lv_obj_set_size(timeline_track, timeline_width_, large ? 4 : 3);
  lv_obj_set_style_radius(timeline_track, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(timeline_track, border, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(timeline_track, LV_OPA_30, LV_PART_MAIN);
  lv_obj_set_pos(timeline_track, inset, large ? 58 : 33);

  timeline_fill_ = lv_obj_create(timeline_);
  bare(timeline_fill_);
  lv_obj_set_height(timeline_fill_, large ? 4 : 3);
  lv_obj_set_style_radius(timeline_fill_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(timeline_fill_, accent, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(timeline_fill_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_pos(timeline_fill_, inset, large ? 58 : 33);

  timeline_dot_ = lv_obj_create(timeline_);
  circle(timeline_dot_, large ? 14 : 10, glow, LV_OPA_COVER);

  timeline_value_ = lv_label_create(timeline_);
  bare(timeline_value_);
  lv_obj_set_style_text_font(timeline_value_, caption_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(timeline_value_, muted, LV_PART_MAIN);
  lv_obj_align(timeline_value_, LV_ALIGN_TOP_RIGHT, -inset, large ? 16 : 7);

  year_ = lv_label_create(timeline_);
  bare(year_);
  lv_obj_set_style_text_font(year_, caption_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(year_, muted, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(year_, config.metrics.px(Dp{1}),
                                     LV_PART_MAIN);
  lv_obj_align(year_, LV_ALIGN_TOP_LEFT, inset + (large ? 134 : 86),
               large ? 16 : 7);

  for (unsigned i = 0; i < 7; ++i) {
    weekday_dots_[i] = lv_obj_create(timeline_);
    circle(weekday_dots_[i], large ? 7 : 5, border, LV_OPA_50);
    lv_obj_set_pos(weekday_dots_[i],
                   inset + static_cast<int>(i) * (large ? 17 : 11),
                   large ? 21 : 11);
  }

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
  animate();
  motion_timer_ = lv_timer_create(motion_tick, 50, this);
}

void ClockFace::update(const apps::ClockText &text) {
  if (!built_) {
    return;
  }
  for (unsigned i = 0; i < 5; ++i) {
    lv_label_set_text_fmt(digits_[i], "%c", text.time[i]);
  }
  lv_label_set_text(seconds_, text.seconds);
  lv_label_set_text(year_, text.year);
  lv_label_set_text(date_, text.date);
  lv_label_set_text(status_, text.status);
  const unsigned progress = std::min(text.day_progress_minutes, 1439U);
  const unsigned fill = timeline_width_ * progress / 1439U;
  lv_obj_set_width(timeline_fill_, std::max(fill, 1U));
  const int inset = config_.width_px >= 400 ? 26 : 16;
  const int y = config_.width_px >= 400 ? 53 : 29;
  lv_obj_set_pos(
      timeline_dot_,
      inset + static_cast<int>(fill) - lv_obj_get_width(timeline_dot_) / 2, y);
  lv_label_set_text_fmt(timeline_value_, "%u%%", progress * 100 / 1439U);
  for (unsigned i = 0; i < 7; ++i) {
    lv_obj_set_style_bg_color(weekday_dots_[i],
                              i == text.weekday
                                  ? resolved(ColorRole::AccentPrimary,
                                             config_.theme, config_.pixel_cost)
                                  : resolved(ColorRole::BorderSubtle,
                                             config_.theme, config_.pixel_cost),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(weekday_dots_[i],
                            i == text.weekday ? LV_OPA_COVER : LV_OPA_40,
                            LV_PART_MAIN);
  }
  text.date[0] == '\0' ? lv_obj_add_flag(date_, LV_OBJ_FLAG_HIDDEN)
                       : lv_obj_remove_flag(date_, LV_OBJ_FLAG_HIDDEN);
  text.status[0] == '\0' ? lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN)
                         : lv_obj_remove_flag(status_, LV_OBJ_FLAG_HIDDEN);
  text.ready ? lv_obj_remove_flag(timeline_, LV_OBJ_FLAG_HIDDEN)
             : lv_obj_add_flag(timeline_, LV_OBJ_FLAG_HIDDEN);
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
  touch_x_ = point.x;
  touch_y_ = point.y;
  touch_ticks_ = 32;
  ripple_step_ = 1;
  lv_obj_remove_flag(ripple_, LV_OBJ_FLAG_HIDDEN);
}

void ClockFace::animate() {
  if (!built_) {
    return;
  }
  ++phase_;
  const int width = static_cast<int>(config_.width_px);
  const int height = static_cast<int>(config_.height_px);
  for (unsigned i = 0; i < kFireflyCount; ++i) {
    const int margin = config_.width_px >= 400 ? 22 : 12;
    int x = margin +
            wave(phase_ * (i % 3 + 1) + i * 31) * (width - margin * 2) / 100;
    const int band = config_.width_px >= 400 ? 105 : 48;
    const int travel = wave(phase_ * ((i + 1) % 3 + 1) + i * 47) * band / 100;
    int y = i < 3 ? margin + travel : height - margin - travel;
    if (touch_ticks_ > 0) {
      x = (x * 2 + touch_x_) / 3;
      y = (y * 2 + touch_y_) / 3;
    }
    const int halo = lv_obj_get_width(firefly_halos_[i]);
    const int dot = lv_obj_get_width(fireflies_[i]);
    lv_obj_set_pos(firefly_halos_[i], x - halo / 2, y - halo / 2);
    lv_obj_set_pos(fireflies_[i], x - dot / 2, y - dot / 2);
  }
  if (brand_firefly_ != nullptr) {
    const int bob = wave(phase_ * 2) / 12;
    const int pull_x = touch_ticks_ > 0 ? (touch_x_ - width / 2) / 12 : 0;
    const int pull_y = touch_ticks_ > 0 ? (touch_y_ - height / 2) / 18 : 0;
    lv_obj_set_pos(brand_firefly_, brand_x_ + pull_x, brand_y_ + bob + pull_y);
    const int flutter = wave(phase_ * 7) * 2;
    lv_obj_set_style_transform_rotation(brand_wings_[0], -200 - flutter,
                                        LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(brand_wings_[1], 200 + flutter,
                                        LV_PART_MAIN);
  }
  if (touch_ticks_ > 0) {
    --touch_ticks_;
  }
  if (ripple_step_ > 0) {
    const int size =
        10 + static_cast<int>(ripple_step_ * (config_.width_px >= 400 ? 9 : 6));
    lv_obj_set_size(ripple_, size, size);
    lv_obj_set_pos(ripple_, touch_x_ - size / 2, touch_y_ - size / 2);
    lv_obj_set_style_border_opa(
        ripple_,
        static_cast<lv_opa_t>(
            std::max(0, 180 - static_cast<int>(ripple_step_ * 13))),
        LV_PART_MAIN);
    if (++ripple_step_ > 13) {
      ripple_step_ = 0;
      lv_obj_add_flag(ripple_, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

} // namespace attadipa::ui
