#include "attadipa/ui/provision_face.h"

#include <algorithm>

#include "attadipa/ui/tokens.h"
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
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

// Row-major: three digit rows, then sign / 0 / erase, then Cancel and a
// double-width OK. The order is the order a person expects from a phone, and
// the way out sits where a phone puts it, left of the way through.
constexpr apps::EntryKey kKeys[] = {
    apps::EntryKey::Digit1, apps::EntryKey::Digit2, apps::EntryKey::Digit3,
    apps::EntryKey::Digit4, apps::EntryKey::Digit5, apps::EntryKey::Digit6,
    apps::EntryKey::Digit7, apps::EntryKey::Digit8, apps::EntryKey::Digit9,
    apps::EntryKey::Sign,   apps::EntryKey::Digit0, apps::EntryKey::Backspace,
    apps::EntryKey::Cancel, apps::EntryKey::Ok,
};
constexpr unsigned kColumns = 3;
constexpr unsigned kRows = 5;

const char *label_of(apps::EntryKey key, const apps::EntryText &text) {
  static constexpr const char *kDigits[] = {"0", "1", "2", "3", "4",
                                            "5", "6", "7", "8", "9"};
  switch (key) {
  case apps::EntryKey::Sign:
    return "±";
  case apps::EntryKey::Backspace:
    return text.backspace;
  case apps::EntryKey::Cancel:
    return text.cancel;
  case apps::EntryKey::Ok:
    return text.ok;
  default:
    return kDigits[static_cast<unsigned>(key)];
  }
}

} // namespace

void ProvisionFace::build(lv_obj_t *screen, const ProvisionFaceConfig &config,
                          apps::ProvisioningEntry &entry) {
  clear();
  config_ = config;
  entry_ = &entry;
  lv_obj_clean(screen);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, config.width_px, config.height_px);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  const Metrics &m = config.metrics;
  const lv_color_t page =
      resolved(ColorRole::BackgroundPrimary, config.theme, config.pixel_cost);
  const lv_color_t raised =
      resolved(ColorRole::BackgroundRaised, config.theme, config.pixel_cost);
  const lv_color_t ink =
      resolved(ColorRole::TextPrimary, config.theme, config.pixel_cost);
  const lv_color_t muted =
      resolved(ColorRole::TextMuted, config.theme, config.pixel_cost);
  const lv_color_t accent =
      resolved(ColorRole::AccentPrimary, config.theme, config.pixel_cost);
  lv_obj_set_style_bg_color(screen, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  const bool large = config.width_px >= 400;
  const lv_font_t *small_font =
      large ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_16;
  const lv_font_t *value_font =
      large ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_20;
  const lv_font_t *key_font = value_font;
  lv_obj_set_style_text_font(screen, small_font, LV_PART_MAIN);

  // The small panel gives up header room for key height: 240 px minus three
  // lines of text leaves five rows of under 20 px, and a key nobody can hit
  // is worse than a hint that is one line instead of two.
  const int margin = m.px(dp_of(large ? Space::Md : Space::Sm));
  const int hint_lines = large ? 2 : 1;
  const int gap = m.px(dp_of(Space::Xs));
  const int width = static_cast<int>(config.width_px) - margin * 2;
  const int line = small_font->line_height;

  title_ = lv_label_create(screen);
  bare(title_);
  lv_obj_set_width(title_, width);
  lv_obj_set_style_text_font(title_, small_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(title_, accent, LV_PART_MAIN);
  lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(title_, LV_LABEL_LONG_CLIP);
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, margin);

  value_ = lv_label_create(screen);
  bare(value_);
  lv_obj_set_width(value_, width);
  lv_obj_set_style_text_font(value_, value_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(value_, ink, LV_PART_MAIN);
  lv_obj_set_style_text_align(value_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(value_, m.px(dp_of(Space::Xs)) / 2,
                                     LV_PART_MAIN);
  lv_label_set_long_mode(value_, LV_LABEL_LONG_CLIP);
  lv_obj_align(value_, LV_ALIGN_TOP_MID, 0, margin + line);

  hint_ = lv_label_create(screen);
  bare(hint_);
  lv_obj_set_width(hint_, width);
  lv_obj_set_style_text_font(hint_, small_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(hint_, muted, LV_PART_MAIN);
  lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(hint_, large ? LV_LABEL_LONG_WRAP
                                      : LV_LABEL_LONG_CLIP);
  const int hint_top = margin + line + value_font->line_height;
  lv_obj_align(hint_, LV_ALIGN_TOP_MID, 0, hint_top);

  // The keypad takes what the three lines above leave. A key is as tall as a
  // fifth of that allows and never taller than a touch target: on the
  // 410 x 502 panel that is under 44 dp already, and on a 240 x 240 one it is
  // a good deal under, which is the price of fourteen keys on 1.54 inches and
  // the reason this screen is entered on purpose and not in passing.
  const int keypad_top = hint_top + line * hint_lines + gap;
  const int keypad_height = static_cast<int>(config.height_px) - keypad_top -
                            margin;
  const int key_height = std::min(
      (keypad_height - gap * (static_cast<int>(kRows) - 1)) /
          static_cast<int>(kRows),
      static_cast<int>(m.px(dp_of(TouchTarget::Adult))));
  const int key_width =
      (width - gap * (static_cast<int>(kColumns) - 1)) /
      static_cast<int>(kColumns);
  const int radius = m.px(dp_of(Radius::Sm));

  keypad_ = lv_obj_create(screen);
  bare(keypad_);
  lv_obj_set_size(keypad_, width,
                  key_height * static_cast<int>(kRows) +
                      gap * (static_cast<int>(kRows) - 1));
  lv_obj_align(keypad_, LV_ALIGN_TOP_MID, 0, keypad_top);

  const apps::EntryText initial = entry.text(config.locale);
  for (unsigned i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
    const apps::EntryKey key = kKeys[i];
    const bool ok = key == apps::EntryKey::Ok;
    lv_obj_t *button = lv_button_create(keypad_);
    lv_obj_remove_style_all(button);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(button, ok ? key_width * 2 + gap : key_width, key_height);
    const int row = static_cast<int>(i / kColumns);
    const int column = static_cast<int>(i % kColumns);
    lv_obj_set_pos(button, column * (key_width + gap),
                   row * (key_height + gap));
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, ok ? accent : raised, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(
        button, LV_OPA_70,
        static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
    // No border: a rounded border is the one thing on this screen the
    // software renderer cannot draw into a snapshot twice -- the second
    // 617 kB capture of thirteen of them found no room in LVGL's pool, on
    // the simulator and on the device alike. The raised fill is the key.
    lv_obj_add_event_cb(button, key_event, LV_EVENT_CLICKED, this);
    // The key itself, not a pointer to it: fits in the user-data slot without
    // a table to look it up in.
    lv_obj_set_user_data(button, reinterpret_cast<void *>(
                                     static_cast<std::uintptr_t>(key)));

    lv_obj_t *label = lv_label_create(button);
    bare(label);
    // A word on a key, not a digit: the smaller face, or "Стереть" runs
    // past the key's edge on both panels.
    const bool word = key == apps::EntryKey::Backspace ||
                      key == apps::EntryKey::Cancel;
    lv_obj_set_style_text_font(label, word ? small_font : key_font,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(label, ok ? page : ink, LV_PART_MAIN);
    lv_label_set_text(label, label_of(key, initial));
    lv_obj_center(label);
    if (key == apps::EntryKey::Sign) {
      sign_key_ = button;
    } else if (key == apps::EntryKey::Backspace) {
      backspace_key_ = button;
    } else if (key == apps::EntryKey::Cancel) {
      cancel_key_ = button;
    } else if (ok) {
      ok_key_ = button;
    }
  }

  built_ = true;
  update();
}

void ProvisionFace::clear() {
  built_ = false;
  entry_ = nullptr;
  keypad_ = nullptr;
  sign_key_ = nullptr;
  ok_key_ = nullptr;
  backspace_key_ = nullptr;
  cancel_key_ = nullptr;
}

void ProvisionFace::update() {
  if (!built_) {
    return;
  }
  const apps::EntryText text = entry_->text(config_.locale);
  lv_label_set_text(title_, text.title);
  lv_label_set_text(value_, text.value);
  lv_label_set_text(hint_, text.hint);
  lv_label_set_text(lv_obj_get_child(ok_key_, 0), text.ok);
  lv_label_set_text(lv_obj_get_child(backspace_key_, 0), text.backspace);
  lv_label_set_text(lv_obj_get_child(cancel_key_, 0), text.cancel);
  text.sign_key ? lv_obj_remove_flag(sign_key_, LV_OBJ_FLAG_HIDDEN)
                : lv_obj_add_flag(sign_key_, LV_OBJ_FLAG_HIDDEN);
  text.done ? lv_obj_add_flag(keypad_, LV_OBJ_FLAG_HIDDEN)
            : lv_obj_remove_flag(keypad_, LV_OBJ_FLAG_HIDDEN);
}

void ProvisionFace::key_event(lv_event_t *event) {
  auto *face = static_cast<ProvisionFace *>(lv_event_get_user_data(event));
  auto *button = static_cast<lv_obj_t *>(lv_event_get_target(event));
  const auto key = static_cast<apps::EntryKey>(
      reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(button)));
  if (face->entry_ != nullptr) {
    face->entry_->press(key);
    face->update();
  }
}

} // namespace attadipa::ui
