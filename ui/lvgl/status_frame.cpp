#include "attadipa/ui/status_frame.h"

#include <cstring>

#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"

namespace attadipa::ui {
namespace {

void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void update_label(lv_obj_t *label, const char *text) {
  if (std::strcmp(lv_label_get_text(label), text) != 0) {
    lv_label_set_text(label, text);
  }
}

} // namespace

void StatusFrame::build(lv_obj_t *screen, const StatusFrameConfig &config) {
  config_ = config;
  lv_obj_clean(screen);
  bare(screen);
  lv_obj_set_size(screen, config.width_px, config.height_px);
  const auto page =
      color(ColorRole::BackgroundPrimary, config.theme, config.pixel_cost);
  const auto ink =
      color(ColorRole::TextPrimary, config.theme, config.pixel_cost);
  lv_obj_set_style_bg_color(
      screen, page ? lv_color_hex(page->packed()) : lv_color_black(),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  const lv_font_t *font = config.width_px >= 320 ? &attadipa_nunito_sans_20
                                               : &attadipa_nunito_sans_14;
  lv_obj_set_style_text_font(screen, font, LV_PART_MAIN);
  const auto &m = config.metrics;
  const auto gap = m.px(dp_of(Space::Xs));
  const auto margin = m.px(dp_of(Space::Sm));
  const auto link_width = m.px(dp_of(IconSize::Md));
  header_height_ = font->line_height + gap;
  const auto labels_width = config.width_px - 2 * margin - link_width - 2 * gap;
  const auto watch_width = labels_width * 2 / 5;
  const auto node_width = labels_width - watch_width;
  auto label = [&](int x, unsigned width) {
    auto *object = lv_label_create(screen);
    bare(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(object, x, gap / 2);
    lv_obj_set_size(object, width, font->line_height);
    lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        object, ink ? lv_color_hex(ink->packed()) : lv_color_white(),
        LV_PART_MAIN);
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    lv_label_set_text(object, "");
    return object;
  };
  watch_ = label(margin, watch_width);
  node_ = label(margin + watch_width + link_width + 2 * gap, node_width);
  lv_obj_set_style_text_align(node_, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  const auto step = link_width / apps::kChannelRungs;
  const auto stroke = m.px(Dp{1});
  for (unsigned i = 0; i < apps::kChannelRungs; ++i) {
    auto *rung = link_[i] = lv_obj_create(screen);
    bare(rung);
    lv_obj_remove_flag(rung, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(rung, margin + watch_width + gap + i * step,
                   (header_height_ - stroke) / 2);
    lv_obj_set_size(rung, step * 3 / 4, stroke);
    lv_obj_set_style_bg_color(
        rung, ink ? lv_color_hex(ink->packed()) : lv_color_white(),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rung, LV_OPA_COVER, LV_PART_MAIN);
  }
  content_ = lv_obj_create(screen);
  bare(content_);
  lv_obj_add_flag(content_, LV_OBJ_FLAG_EVENT_BUBBLE);
  restore_content_geometry();
}

void StatusFrame::restore_content_geometry() {
  lv_obj_set_pos(content_, 0, header_height_);
  lv_obj_set_size(content_, config_.width_px, content_height());
}

void StatusFrame::update(const apps::MeshText &text) {
  if (content_ == nullptr)
    return;
  update_label(watch_, text.watch_power);
  update_label(node_, text.node_power);
  for (unsigned i = 0; i < apps::kChannelRungs; ++i) {
    // A whole link, a growing link, or two severed ends: the shape carries
    // the state even without colour. An integrated supply has no second end.
    const bool visible = text.node_power[0] != '\0' &&
                         (text.link == apps::MeshLink::Linked ||
                          (text.link == apps::MeshLink::Reaching
                               ? i < 2
                               : (i == 0 || i + 1 == apps::kChannelRungs)));
    visible ? lv_obj_remove_flag(link_[i], LV_OBJ_FLAG_HIDDEN)
            : lv_obj_add_flag(link_[i], LV_OBJ_FLAG_HIDDEN);
  }
}

} // namespace attadipa::ui
