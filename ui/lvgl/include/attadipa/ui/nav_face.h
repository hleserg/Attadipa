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
// North-up unless the watch knows which way it is turned, which on both boards
// today it does not: no magnetometer is fitted to either. In that state the
// ring does not turn with the wrist and the trail is a bearing rather than a
// heading. Saying so is the face's job — a trail that looked like it tracked
// the watch would be read as one, and the direction would be wrong by however
// far the wearer happened to be turned.
//
// With `NavText::has_arrow` the readout is head-up instead: the trail points
// where to walk relative to the case and the `N` marker travels to where north
// is. The two always move together, because the marker is the sentence that
// says which of the two the ring is.
//
// It draws a trail only when `NavText::has_bearing` is set, and a distance
// only when `has_distance` is. Neither is inferred from the strings.
//
// The direction is drawn as a run of glowing dots rather than as an arrow,
// which is the mascot's own motif and the same primitive the clock face
// already lights its meadow with — `ui/lvgl/clock_face.cpp:90` —
// "      circle(dot, large ? 5 : 4, glow, LV_OPA_20);". It carries one more
// thing an arrow cannot: the dots grow and brighten outward, so which end is
// the head is legible without colour, at a glance, and in one frame.
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
  void point_trail(const apps::NavText &text);

  // How many of the seven are drawn on this panel. The run occupies a fixed
  // fraction of the ring's radius, so the *span* shrinks with the panel while
  // the dots do not shrink as fast; four is what still reads as a run on the
  // 240x240 ring instead of as a solid arc.
  unsigned trail_dots() const { return config_.width_px >= 320 ? kTrailDots : 4; }

  // Seven, because the run has to read as a direction and not as a dotted
  // circle: fewer and the growth from tail to head is too coarse to see the
  // way round, more and on the 240x240 ring the dots touch and become an arc.
  static constexpr unsigned kTrailDots = 7;

  NavFaceConfig config_{};
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *background_ = nullptr;
  lv_obj_t *scrim_ = nullptr;

  // The scrim's gradient, held here because LVGL keeps the pointer it is given
  // and reads the stops at every draw. A local would be read after it died.
  lv_grad_dsc_t scrim_grad_{};
  lv_obj_t *title_ = nullptr;
  lv_obj_t *ring_ = nullptr;
  lv_obj_t *north_ = nullptr;
  lv_obj_t *trail_[kTrailDots]{};
  lv_obj_t *hub_ = nullptr;
  lv_obj_t *distance_ = nullptr;
  lv_obj_t *bearing_ = nullptr;
  lv_obj_t *status_ = nullptr;
  lv_obj_t *caveat_ = nullptr;

  // What the trail was last drawn from. Moving seven objects invalidates seven
  // areas, and the readout re-formats every tick whether or not the bearing
  // moved, so repositioning them unconditionally costs seven flushes a tick
  // for nothing.
  bool          trail_drawn_ = false;
  std::uint16_t trail_centideg_ = 0;
  bool built_ = false;
};

} // namespace attadipa::ui
