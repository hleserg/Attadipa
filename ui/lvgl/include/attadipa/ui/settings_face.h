#pragma once

#include "attadipa/apps/brightness.h"
#include "attadipa/ui/clock_face.h"

namespace attadipa::ui {

// The three real pages of Settings > Display > Brightness. No placeholder
// controls for features the composition root cannot apply.
class SettingsFace {
public:
  enum class Page { Settings, Display, Brightness };
  void build(lv_obj_t *screen, const ClockFaceConfig &config,
             apps::BrightnessSettings &brightness, void (*leave)(),
             Page page = Page::Settings);
  void clear();
  void update();
  Page page() const { return page_; }

private:
  enum class Action { OpenDisplay, OpenBrightness, Back, Minus, Plus, Cancel, Save };
  static void clicked(lv_event_t *event);
  static void slid(lv_event_t *event);
  void draw();
  void act(Action action);
  lv_obj_t *button(Action action, const char *text, int x, int y, int w, int h);
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *title_ = nullptr;
  lv_obj_t *value_ = nullptr;
  lv_obj_t *slider_ = nullptr;
  lv_obj_t *save_label_ = nullptr;
  apps::BrightnessSettings *brightness_ = nullptr;
  void (*leave_)() = nullptr;
  ClockFaceConfig config_{};
  Page page_ = Page::Settings;
};

} // namespace attadipa::ui
