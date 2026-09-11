#include "attadipa/ui/settings_face.h"

#include <algorithm>
#include <cstdint>

#include "attadipa/l10n/tr.h"
#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"
#include "generated/attadipa_images.h"

namespace attadipa::ui {
namespace {
using l10n::StringId;
const char *word(StringId id) { return l10n::tr(id); }
lv_color_t ink(ColorRole role, const ClockFaceConfig &config) {
  const auto value = color(role, config.theme, config.pixel_cost);
  return value ? lv_color_hex(value->packed()) : lv_color_black();
}
void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
}
} // namespace

void SettingsFace::build(lv_obj_t *screen, const ClockFaceConfig &config,
                         apps::BrightnessSettings &brightness, void (*leave)(),
                         Page page) {
  clear();
  screen_ = screen;
  config_ = config;
  brightness_ = &brightness;
  leave_ = leave;
  page_ = page;
  draw();
}

void SettingsFace::clear() {
  screen_ = title_ = value_ = slider_ = save_label_ = nullptr;
  brightness_ = nullptr;
  leave_ = nullptr;
  page_ = Page::Settings;
}

lv_obj_t *SettingsFace::button(Action action, const char *text,
                              int x, int y, int width, int height) {
  lv_obj_t *button = lv_button_create(screen_);
  bare(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_radius(button, config_.metrics.px(dp_of(Radius::Sm)), LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, ink(ColorRole::BackgroundSurface, config_), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_70,
      static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
  lv_obj_set_user_data(button, reinterpret_cast<void *>(static_cast<std::uintptr_t>(action)));
  lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, this);
  lv_obj_t *label = lv_label_create(button);
  bare(label);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return label;
}

void SettingsFace::draw() {
  lv_obj_clean(screen_);
  // The shared frame owns this object's position. Internal page changes
  // redraw children without erasing its offset below the status strip.
  lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(screen_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(screen_, config_.width_px, config_.height_px);
  lv_obj_set_style_bg_color(screen_, ink(ColorRole::BackgroundPrimary, config_), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen_, ink(ColorRole::TextPrimary, config_), LV_PART_MAIN);
  const bool large = config_.width_px >= 400;
  lv_obj_set_style_text_font(screen_, large ? &attadipa_nunito_sans_20
                                           : &attadipa_nunito_sans_16, LV_PART_MAIN);
  if (config_.theme == Theme::Night) {
    lv_obj_t *background = lv_image_create(screen_);
    bare(background);
    lv_image_set_src(background, &attadipa_background_clock_meadow_night_410x502);
    const unsigned scale = std::max(config_.width_px * 256U / 410U,
                                    config_.height_px * 256U / 502U);
    lv_image_set_scale(background, scale);
    lv_obj_center(background);
  }
  const int target = config_.metrics.px(dp_of(TouchTarget::Adult));
  const int gap = config_.metrics.px(dp_of(Space::Xs));
  const int margin = config_.metrics.px(dp_of(Space::Sm));
  const int width = static_cast<int>(config_.width_px) - margin * 2;
  const int bottom = gap / 2;
  const int footer = static_cast<int>(config_.height_px) - target - bottom;
  title_ = lv_label_create(screen_);
  bare(title_);
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, bottom);
  value_ = slider_ = save_label_ = nullptr;
  if (page_ != Page::Brightness) {
    lv_label_set_text(title_, word(page_ == Page::Settings ? StringId::SettingsTitle : StringId::SettingsDisplay));
    char text[96]{};
    if (page_ == Page::Settings) {
      lv_snprintf(text, sizeof(text), "%s", word(StringId::SettingsDisplay));
    } else {
      lv_snprintf(text, sizeof(text), "%s  %u%%", word(StringId::SettingsBrightness), brightness_->saved());
    }
    button(page_ == Page::Settings ? Action::OpenDisplay : Action::OpenBrightness,
           text, margin, footer - target - gap, width, target);
    button(Action::Back, word(StringId::SettingsBack), margin, footer, width, target);
    return;
  }
  // Three full adult rows. On 240 px the title gets the remaining 32 px;
  // vertical gaps use half the existing Xs token, not smaller touch targets.
  const int row_gap = gap / 2;
  const int slider_y = footer - target - row_gap;
  const int values_y = slider_y - target - row_gap;
  button(Action::Minus, "-", margin, values_y, target, target);
  button(Action::Plus, "+", margin + width - target, values_y, target, target);
  value_ = lv_label_create(screen_);
  bare(value_);
  lv_obj_set_style_text_font(value_, large ? &attadipa_nunito_sans_64
                                          : &attadipa_nunito_sans_28, LV_PART_MAIN);
  lv_obj_set_pos(value_, margin + target + gap, values_y);
  lv_obj_set_size(value_, width - 2 * (target + gap), target);
  lv_obj_set_style_text_align(value_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_pad_top(value_, (target - (large ? 64 : 28)) / 2, LV_PART_MAIN);
  lv_obj_t *slider_row = lv_obj_create(screen_);
  bare(slider_row);
  lv_obj_set_pos(slider_row, margin, slider_y);
  lv_obj_set_size(slider_row, width, target);
  slider_ = lv_slider_create(slider_row);
  lv_obj_remove_style_all(slider_);
  lv_obj_remove_flag(slider_, LV_OBJ_FLAG_SCROLLABLE);
  // A thin native slider with a full-height hit area. Its parent clips the
  // extended horizontal hit box to this row, so it cannot steal nearby keys.
  lv_obj_set_size(slider_, width - gap * 4, gap);
  lv_obj_center(slider_);
  lv_obj_set_ext_click_area(slider_, (target - gap + 1) / 2);
  lv_obj_set_style_bg_color(slider_, ink(ColorRole::BackgroundSurface, config_), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider_, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_radius(slider_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  for (lv_part_t part : {LV_PART_INDICATOR, LV_PART_KNOB}) {
    lv_obj_set_style_bg_color(slider_, ink(ColorRole::AccentPrimary, config_), part);
    lv_obj_set_style_bg_opa(slider_, LV_OPA_COVER, part);
    lv_obj_set_style_radius(slider_, LV_RADIUS_CIRCLE, part);
  }
  lv_obj_set_style_pad_all(slider_, gap, LV_PART_KNOB);
  lv_slider_set_range(slider_, brightness_->minimum(), 100);
  lv_obj_add_event_cb(slider_, slid, LV_EVENT_VALUE_CHANGED, this);
  const int half = (width - gap) / 2;
  button(Action::Cancel, word(StringId::SettingsCancel), margin, footer, half, target);
  save_label_ = button(Action::Save, word(StringId::ProvisionKeySave), margin + half + gap,
                        footer, half, target);
  update();
}

void SettingsFace::update() {
  if (screen_ == nullptr || page_ != Page::Brightness) return;
  StringId title = StringId::SettingsBrightness;
  switch (brightness_->error()) {
  case apps::BrightnessError::Load: title = StringId::BrightnessNotLoaded; break;
  case apps::BrightnessError::Apply: title = StringId::BrightnessNotApplied; break;
  case apps::BrightnessError::Save: title = StringId::BrightnessNotSaved; break;
  case apps::BrightnessError::Uncertain: title = StringId::BrightnessRestartCheck; break;
  case apps::BrightnessError::None: break;
  }
  lv_label_set_text(title_, word(title));
  lv_label_set_text_fmt(value_, "%u%%", brightness_->value());
  lv_slider_set_value(slider_, brightness_->value(), LV_ANIM_OFF);
  lv_label_set_text(save_label_, word(brightness_->error() == apps::BrightnessError::Uncertain
      ? StringId::BrightnessRestart : brightness_->error() == apps::BrightnessError::Save
      ? StringId::ProvisionKeyRetry : StringId::ProvisionKeySave));
}

void SettingsFace::act(Action action) {
  switch (action) {
  case Action::OpenDisplay: page_ = Page::Display; break;
  case Action::OpenBrightness: page_ = Page::Brightness; break;
  case Action::Back:
    if (page_ == Page::Settings) { if (leave_ != nullptr) leave_(); return; }
    page_ = Page::Settings;
    break;
  case Action::Minus: brightness_->adjust(-1); update(); return;
  case Action::Plus: brightness_->adjust(1); update(); return;
  case Action::Cancel:
    if (!brightness_->cancel()) { update(); return; }
    page_ = Page::Display;
    break;
  case Action::Save:
    if (brightness_->error() == apps::BrightnessError::Uncertain) {
      brightness_->restart();
      return;
    }
    if (!brightness_->save()) { update(); return; }
    page_ = Page::Display;
    break;
  }
  draw();
}

void SettingsFace::clicked(lv_event_t *event) {
  auto *self = static_cast<SettingsFace *>(lv_event_get_user_data(event));
  const auto action = static_cast<Action>(reinterpret_cast<std::uintptr_t>(
      lv_obj_get_user_data(lv_event_get_target_obj(event))));
  self->act(action);
}

void SettingsFace::slid(lv_event_t *event) {
  auto *self = static_cast<SettingsFace *>(lv_event_get_user_data(event));
  self->brightness_->preview(lv_slider_get_value(self->slider_));
  self->update();
}

} // namespace attadipa::ui
