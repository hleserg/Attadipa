#include "attadipa/ui/provision_face.h"

#include <algorithm>

#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"

namespace attadipa::ui {
namespace {

lv_color_t resolved(ColorRole role, Theme theme, PixelCost pixel_cost,
                    lv_color_t fallback) {
  const auto value = color(role, theme, pixel_cost);
  return value ? lv_color_hex(value->packed()) : fallback;
}

void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

// A line the model left empty is not an empty line: it is hidden, so the flex
// column above closes over it. That is the whole reason the lines are laid out
// rather than placed -- the entry shows three of them on one field and six on
// another, and arithmetic over `line_height` would have to know which.
void show(lv_obj_t *object, const char *text) {
  lv_label_set_text(object, text);
  if (text[0] == '\0') {
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
}

// Two rows of three. The value keys first, then the step keys, and the two
// keys that end something -- Forget and Leave -- on the right-hand column
// where neither is under a thumb reaching for Next.
constexpr apps::EntryKey kKeys[] = {
    apps::EntryKey::Minus,    apps::EntryKey::Plus, apps::EntryKey::Forget,
    apps::EntryKey::Previous, apps::EntryKey::Next, apps::EntryKey::Leave,
};
constexpr unsigned kColumns = 3;
constexpr unsigned kRows = 2;
static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == ProvisionFace::kKeyCount,
              "every EntryKey is drawn");

const char *label_of(apps::EntryKey key, const apps::EntryText &text) {
  switch (key) {
  case apps::EntryKey::Minus:
    return text.minus;
  case apps::EntryKey::Plus:
    return text.plus;
  case apps::EntryKey::Previous:
    return text.previous;
  case apps::EntryKey::Next:
    return text.next;
  case apps::EntryKey::Forget:
    return text.forget;
  case apps::EntryKey::Leave:
    return text.leave;
  }
  return "";
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
      resolved(ColorRole::BackgroundPrimary, config.theme, config.pixel_cost,
               lv_color_black());
  // Night has no `BackgroundRaised` -- DESIGN_SYSTEM records that as a gap
  // rather than inventing a value (`ui/src/color.cpp` — "    {ColorRole::BackgroundRaised, ColorKind::Background, kSoftClay, std::nullopt, std::nullopt},").
  // Falling through to the page would make every key that is not the accent
  // one look like plain text, which is what the pad this replaced did. Surface
  // is defined in both themes and is the nearest thing the system actually
  // states.
  const lv_color_t surface =
      resolved(ColorRole::BackgroundSurface, config.theme, config.pixel_cost,
               page);
  const lv_color_t raised = resolved(ColorRole::BackgroundRaised, config.theme,
                                     config.pixel_cost, surface);
  const lv_color_t ink = resolved(ColorRole::TextPrimary, config.theme,
                                  config.pixel_cost, lv_color_white());
  const lv_color_t muted =
      resolved(ColorRole::TextMuted, config.theme, config.pixel_cost, ink);
  const lv_color_t accent =
      resolved(ColorRole::AccentPrimary, config.theme, config.pixel_cost, ink);
  lv_obj_set_style_bg_color(screen, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  const bool large = config.width_px >= 400;
  const lv_font_t *small_font =
      large ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_16;
  const lv_font_t *value_font =
      large ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_20;
  // A key on the small panel is 72 px wide and the widest Russian label on
  // this screen is "Сохранить"; at the body size it runs past the key's edge.
  const lv_font_t *key_font = large ? small_font : &attadipa_nunito_sans_14;
  lv_obj_set_style_text_font(screen, small_font, LV_PART_MAIN);

  const int margin = m.px(dp_of(large ? Space::Md : Space::Sm));
  const int gap = m.px(dp_of(Space::Xs));
  const int width = static_cast<int>(config.width_px) - margin * 2;
  const int radius = m.px(dp_of(Radius::Sm));

  // Six keys, not fourteen, so a key can be a full touch target on both
  // panels -- which the fourteen-key pad this replaced could not be on a
  // 240 x 240 one.
  const int key_height = static_cast<int>(m.px(dp_of(TouchTarget::Adult)));
  // The small panel spends its side margin on key width instead: at the body
  // size a 72 px key cannot hold "Сохранить" and a 77 px one can.
  const int keypad_width =
      large ? width : static_cast<int>(config.width_px) - gap;
  const int key_width = (keypad_width - gap * (static_cast<int>(kColumns) - 1)) /
                        static_cast<int>(kColumns);
  const int keypad_height =
      key_height * static_cast<int>(kRows) + gap * (static_cast<int>(kRows) - 1);

  keypad_ = lv_obj_create(screen);
  bare(keypad_);
  lv_obj_set_size(keypad_, keypad_width, keypad_height);
  lv_obj_align(keypad_, LV_ALIGN_BOTTOM_MID, 0, -margin);

  // Whatever the keys leave. The lines stack from the top and the column
  // clips rather than scrolls: a screen this one has too much to say on is a
  // layout to fix, not a scrollbar to grow.
  lines_ = lv_obj_create(screen);
  bare(lines_);
  lv_obj_set_size(lines_, width,
                  static_cast<int>(config.height_px) - keypad_height -
                      margin * 2 - gap);
  lv_obj_align(lines_, LV_ALIGN_TOP_MID, 0, margin);
  lv_obj_set_flex_flow(lines_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(lines_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(lines_, gap / 2, LV_PART_MAIN);

  auto line = [&](const lv_font_t *font, lv_color_t colour) {
    lv_obj_t *label = lv_label_create(lines_);
    bare(label);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, colour, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
  };

  title_ = line(small_font, accent);
  value_ = line(value_font, ink);
  draft_ = line(small_font, muted);
  // The 240 x 240 panel does not get this one. Seven wrapping lines over two
  // rows of full-size keys overflow its 124 px of text room and the title goes
  // off the top -- and of the seven, the UTC echo is the one the panel can
  // spare: the draft line right above it already carries the same instant with
  // the offset that turns it into this one.
  utc_ = large ? line(small_font, muted) : nullptr;
  small_panel_ = !large;
  instruction_ = line(key_font, muted);
  verdict_ = line(small_font, ink);

  const apps::EntryText initial = entry.text(config.locale);
  for (unsigned i = 0; i < kKeyCount; ++i) {
    lv_obj_t *button = lv_button_create(keypad_);
    lv_obj_remove_style_all(button);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(button, key_width, key_height);
    lv_obj_set_pos(button,
                   static_cast<int>(i % kColumns) * (key_width + gap),
                   static_cast<int>(i / kColumns) * (key_height + gap));
    lv_obj_set_style_radius(button, radius, LV_PART_MAIN);
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
                                     static_cast<std::uintptr_t>(kKeys[i])));

    lv_obj_t *label = lv_label_create(button);
    bare(label);
    lv_obj_set_style_text_font(label, key_font, LV_PART_MAIN);
    lv_label_set_text(label, label_of(kKeys[i], initial));
    lv_obj_center(label);
    keys_[i] = button;
  }

  built_ = true;
  update();
}

void ProvisionFace::clear() {
  built_ = false;
  entry_ = nullptr;
  lines_ = nullptr;
  keypad_ = nullptr;
  for (unsigned i = 0; i < kKeyCount; ++i) {
    keys_[i] = nullptr;
  }
}

void ProvisionFace::update() {
  if (!built_) {
    return;
  }
  const apps::EntryText text = entry_->text(config_.locale);
  // "Day 1/6", not a line of its own: a step counter is a qualifier on the
  // title, and the small panel has no line to spend on it.
  char title[64] = {};
  if (text.steps > 0) {
    lv_snprintf(title, sizeof(title), "%s %u/%u", text.title, text.step,
                text.steps);
  } else {
    lv_snprintf(title, sizeof(title), "%s", text.title);
  }
  show(title_, title);
  // The node's first eight hex digits are what the holder compares against
  // the node's own screen, so they belong on the line the eye goes to. They
  // take it whenever the field has no value of its own -- which is every
  // field of the node task except the passkey being typed.
  show(value_, text.value[0] != '\0' ? text.value : text.node);
  // On the small panel the whole instant appears where it is the point --
  // the review step and the receipt, both of which have no step count -- and
  // not under every digit being stepped. 240 x 240 holds four lines over two
  // rows of keys, not six, and a title clipped off the top is worse than a
  // restatement deferred by one press.
  const bool stepping = text.steps > 0;
  const char *draft = small_panel_ && stepping ? "" : text.draft;
  show(draft_, draft);
  if (utc_ != nullptr) {
    show(utc_, text.utc);
  }
  // ... and only one of the two on the small panel, which holds a title, a
  // supporting line and two rows of keys and not a line more. The instant wins
  // where there is one -- on the review step it *is* the instruction, since
  // what "save" will write is exactly the line above the key -- and the
  // instruction takes the slot the rest of the time, including every field of
  // the node task, which has no instant to show and needs saying what Next
  // does.
  const bool draft_shown = draft[0] != '\0';
  show(instruction_, small_panel_ && draft_shown ? "" : text.instruction);
  show(verdict_, text.verdict);

  const lv_color_t page =
      resolved(ColorRole::BackgroundPrimary, config_.theme, config_.pixel_cost,
               lv_color_black());
  const lv_color_t ink = resolved(ColorRole::TextPrimary, config_.theme,
                                  config_.pixel_cost, lv_color_white());
  const lv_color_t surface = resolved(ColorRole::BackgroundSurface,
                                      config_.theme, config_.pixel_cost, page);
  const lv_color_t raised = resolved(ColorRole::BackgroundRaised, config_.theme,
                                     config_.pixel_cost, surface);
  const lv_color_t accent = resolved(ColorRole::AccentPrimary, config_.theme,
                                     config_.pixel_cost, ink);
  // The palette has no red in either theme and inventing one is the owner's
  // decision, not this file's (`ui/src/color.cpp` — "  //   - `Danger` has no
  // value in either theme."). Warning is the strongest thing that is actually
  // defined, and it marks the key by the field it sits on rather than by the
  // word printed on it: the same "Forget" key is ordinary on `Node`, where it
  // only opens the question, and answers it on `ForgetConfirm`.
  //
  // **Only night separates the two colours, and this is the honest extent of
  // the marking.** Warning is orange in day and undefined in night, where a
  // foreground role falls through to day (`ui/src/color.cpp:66` —
  // "    {ColorRole::Warning, ColorKind::Foreground, kAttadipaOrange, std::nullopt, kAttadipaOrange},"),
  // while AccentPrimary is amber in night and that same orange in both day
  // columns (`ui/src/color.cpp:63` —
  // "    {ColorRole::AccentPrimary, ColorKind::Foreground, kAttadipaOrange, kGlowAmber, kAttadipaOrange},").
  // So in day `danger == accent` exactly, and the fill there says "this key
  // acts", not "this key destroys" — the word on it is the whole of the
  // warning. Closing that gap means giving `Danger` a value, which is the
  // owner's call, so the code does not paper over it with an invented red.
  const lv_color_t danger = resolved(ColorRole::Warning, config_.theme,
                                     config_.pixel_cost, accent);
  const bool confirming = entry_->field() == apps::EntryField::ForgetConfirm;

  for (unsigned i = 0; i < kKeyCount; ++i) {
    const char *label = label_of(kKeys[i], text);
    if (label[0] == '\0') {
      lv_obj_add_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
    // On the confirmation the model gives Forget the same word as Next and
    // the same effect (`apps/src/provisioning.cpp:790` — "        out.forget   = out.next;"),
    // so both are the acting key there and both have to look like it. Marking
    // only one would put a second Forget on the screen dressed as the safe
    // choice.
    const bool through = kKeys[i] == apps::EntryKey::Next ||
                         (confirming && kKeys[i] == apps::EntryKey::Forget);
    const bool destructive = confirming && through;
    const lv_color_t fill = destructive ? danger : (through ? accent : raised);
    lv_obj_set_style_bg_color(keys_[i], fill, LV_PART_MAIN);
    lv_obj_t *text_of_key = lv_obj_get_child(keys_[i], 0);
    lv_obj_set_style_text_color(text_of_key, through ? page : ink,
                                LV_PART_MAIN);
    lv_label_set_text(text_of_key, label);
  }
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
