#pragma once

#include "attadipa/apps/mesh.h"
#include "attadipa/ui/color.h"
#include "attadipa/ui/metrics.h"
#include "lvgl.h"

namespace attadipa::ui {

struct MeshFaceConfig {
  unsigned width_px = 240;
  unsigned height_px = 240;
  Theme theme = Theme::Night;
  PixelCost pixel_cost = PixelCost::Fixed;
  Metrics metrics = Metrics::unscaled();
};

// Whether this watch is talking to a node, and which one.
//
// The state is carried by the shape of the link and not only by the word under
// it: a channel that is whole, part-lit, dimmed, gapped or absent reads before
// the word does, and reads the same in either language. That matters because
// this is the screen an operator passes through on the way to navigation, and
// they are usually reading it to find out whether to keep walking.
//
// NOTHING IS DRAWN FOR WHAT IS NOT THERE. `apps::MeshText` leaves a field empty
// rather than filling it with a placeholder, and an empty field here means the
// widget is hidden, not that it draws an em dash. A screen that prints
// `peers 0` for a radio that is absent tells the same lie as `0 m` on the
// navigation face, told about a different unknown, so the measurement row is
// hidden whole rather than zero-filled.
//
// This used to be `build_mesh_screen()` inside `firmware/main/waveshare_board.cpp`,
// with literal hex and offsets measured for one panel. It could not be drawn on
// the 240x240 board, and the simulator could not render it, so no change to it
// was ever looked at before it merged.
class MeshFace {
public:
  void build(lv_obj_t *screen, const MeshFaceConfig &config,
             const apps::MeshText &text);
  void update(const apps::MeshText &text);
  void clear();

  bool built() const { return built_; }

private:
  void lay_out(const apps::MeshText &text);
  void paint_channel(const apps::MeshText &text);

  // Whether this panel gets the roomy composition. 320 px is where the second
  // column of the measurement row stops colliding with the first, and it is
  // the same threshold `NavFace::trail_dots()` splits on, so a board does not
  // get one face's large layout and the other's small one.
  bool large() const { return config_.width_px >= 320; }

  MeshFaceConfig config_{};
  lv_obj_t *screen_ = nullptr;
  lv_obj_t *title_ = nullptr;

  // The link glyph. `watch_` is drawn in every state including the empty ones:
  // it is the one thing on this screen that is never in question.
  lv_obj_t *watch_ = nullptr;
  lv_obj_t *watch_dot_ = nullptr;
  lv_obj_t *rung_[apps::kChannelRungs]{};
  lv_obj_t *socket_ = nullptr;
  lv_obj_t *halo_ = nullptr;
  // The node that answered when it was not the pinned one. It hangs below the
  // socket rather than beside it: on the 240 px panel the right edge is closer
  // to the socket than the glyph's own radius, and a dot placed there is drawn
  // off the display.
  lv_obj_t *intruder_ = nullptr;

  lv_obj_t *state_ = nullptr;
  lv_obj_t *note_ = nullptr;
  lv_obj_t *way_out_ = nullptr;
  lv_obj_t *node_key_ = nullptr;
  lv_obj_t *node_name_ = nullptr;
  lv_obj_t *pinned_ = nullptr;
  lv_obj_t *answered_ = nullptr;
  lv_obj_t *rule_ = nullptr;
  lv_obj_t *msg_heading_ = nullptr;
  lv_obj_t *msg_ = nullptr;
  lv_obj_t *msg_meta_ = nullptr;
  lv_obj_t *value_[3]{};
  lv_obj_t *label_[3]{};

  bool built_ = false;
};

} // namespace attadipa::ui
