#pragma once

#include "attadipa/apps/navigation.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"
#include "lvgl.h"

namespace attadipa::ui {

struct NavFaceConfig {
  unsigned width_px = 240;
  unsigned height_px = 240;
  Theme theme = Theme::Night;
  PixelCost pixel_cost = PixelCost::Fixed;
  Metrics metrics = Metrics::unscaled();
};

// The navigation readout: how far the node is, and which way.
//
// North-up, always. There is no magnetometer on either board, so the ring does
// not turn with the wrist and the needle is a bearing rather than a heading.
// Saying so is the face's job — a needle that looked like it tracked the watch
// would be read as one, and the arrow would be wrong by however far the wearer
// happened to be turned.
//
// It draws a needle only when `NavText::has_bearing` is set, and a distance
// only when `has_distance` is. Neither is inferred from the strings.
class NavFace {
public:
  void build(lv_obj_t *screen, const NavFaceConfig &config,
             const apps::NavText &text);
  void update(const apps::NavText &text);
  void clear();

  // Whether `build()` has run and `update()` will do anything. One fact in one
  // place: a caller keeping its own parallel bool is a second copy of this,
  // and the two drift the first time a teardown path forgets one.
  bool built() const { return built_; }

private:
  void point_needle(const apps::NavText &text);

  NavFaceConfig config_{};
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *title_ = nullptr;
  lv_obj_t *ring_ = nullptr;
  lv_obj_t *north_ = nullptr;
  lv_obj_t *needle_ = nullptr;
  lv_obj_t *hub_ = nullptr;
  lv_obj_t *distance_ = nullptr;
  lv_obj_t *bearing_ = nullptr;
  lv_obj_t *status_ = nullptr;
  lv_obj_t *caveat_ = nullptr;
  lv_point_precise_t needle_points_[3]{};

  // What the needle was last drawn from. `lv_line_set_points()` invalidates the
  // whole line object, and the readout re-formats every tick whether or not the
  // bearing moved, so re-pointing it unconditionally costs a flush per tick for
  // nothing.
  bool          needle_drawn_ = false;
  std::uint16_t needle_centideg_ = 0;
  bool built_ = false;
};

} // namespace attadipa::ui
