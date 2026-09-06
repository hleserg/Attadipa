#include "nav_screen.h"

#include <cstdio>
#include <cstring>

#include "lvgl.h"

#include "attadipa/apps/navigation.h"
#include "attadipa/core/position.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/ui/metrics.h"
#include "attadipa/ui/nav_face.h"

#include "review_keys.h"

namespace attadipa::sim {
namespace {

using namespace attadipa;

ui::NavFace g_face;
ui::NavFaceConfig g_config;
apps::NavState g_state;

ui::Theme toggle_nav_theme() {
  g_config.theme =
      g_config.theme == ui::Theme::Day ? ui::Theme::Night : ui::Theme::Day;
  rebuild_nav_screen();
  return g_config.theme;
}

} // namespace

bool stage_nav_scenario(const char *name) {
  constexpr core::Position kHere{5000000, 10000000};
  constexpr core::Position kTarget{5100000, 10160000}; // 2.1 km, bearing 058

  core::LocationState own;
  own.availability = core::Availability::Ready;
  own.has_position = true;
  own.position.value = kHere;
  own.validity = core::PositionValidity::Valid;
  own.fix_type = core::FixType::ThreeD;
  own.source = core::PositionSource::LocalGnss;
  own.receiver = core::ReceiverPresence::Running;

  // What the MeshCore node link produces: a coordinate, an arrival age, and no
  // fix type at all — so `classify()` answers NoFix and goes on answering it.
  core::LocationState node;
  node.availability = core::Availability::Ready;
  node.has_position = true;
  node.position.value = kTarget;
  node.position.age_at_us_ms = 3000;
  node.validity = core::PositionValidity::NoFix;
  node.fix_type = core::FixType::Unknown;
  node.source = core::PositionSource::NodeGnss;

  g_state = apps::NavState{};
  if (std::strcmp(name, "ready") == 0) {
    g_state.own = own;
    g_state.target = node;
  } else if (std::strcmp(name, "waiting") == 0) {
    g_state.target = node;
  } else if (std::strcmp(name, "no-fix") == 0) {
    g_state.own.availability = core::Availability::Ready;
    g_state.own.receiver = core::ReceiverPresence::Running;
    g_state.own.fix_type = core::FixType::NoFix;
    g_state.target = node;
  } else if (std::strcmp(name, "own-stale") == 0) {
    own.validity = core::PositionValidity::Stale;
    g_state.own = own;
    g_state.target = node;
  } else if (std::strcmp(name, "own-degraded") == 0) {
    // Current, and solved badly. The numbers still render; the status is what
    // carries the caveat.
    own.validity = core::PositionValidity::Degraded;
    g_state.own = own;
    g_state.target = node;
  } else if (std::strcmp(name, "node-unavailable") == 0) {
    node.availability = core::Availability::Unreachable;
    node.position.age_at_us_ms = 45000;
    g_state.own = own;
    g_state.target = node;
  } else if (std::strcmp(name, "receiver-silent") == 0) {
    // The own half of `node-unavailable`: a receiver that is bound and has
    // stopped answering. The retained coordinate still measures, so this is
    // the layout case worth looking at — a status label over a full pair of
    // numbers, not over two em dashes.
    own.availability = core::Availability::Unreachable;
    g_state.own = own;
    g_state.target = node;
  } else if (std::strcmp(name, "node-unknown") == 0) {
    g_state.own = own;
    g_state.target.availability = core::Availability::Ready;
  } else if (std::strcmp(name, "node-stale") == 0) {
    node.position.age_at_us_ms = 300000;
    g_state.own = own;
    g_state.target = node;
  } else if (std::strcmp(name, "arrived") == 0) {
    node.position.value = kHere;
    g_state.own = own;
    g_state.target = node;
  } else if (std::strncmp(name, "head-up", 7) == 0) {
    // ADR-0009 §5 row 1, with the ring turned to four different places. The
    // needle is wrist-relative and the `N` marker travels to where north is.
    // Four angles rather than one because the marker orbits inside the ring and
    // each quarter tests a different part of that path -- the bottom of the
    // ring on `-south`, its two sides on `-east` and `-west` -- and the 240x240
    // panel is where the ring has the least room around it.
    g_state.own = own;
    g_state.target = node;
    g_state.heading.source = core::HeadingSource::Magnetometer;
    g_state.heading.frame = core::ReferenceFrame::WatchBody;
    g_state.heading.validity = core::HeadingValidity::Valid;
    g_state.heading.confidence = 90;
    const char *facing = name + 7;
    if (std::strcmp(facing, "-south") == 0) {
      g_state.heading.centideg = 18000;
    } else if (std::strcmp(facing, "-east") == 0) {
      g_state.heading.centideg = 9000;
    } else if (std::strcmp(facing, "-west") == 0) {
      g_state.heading.centideg = 27000;
    } else if (std::strcmp(facing, "") == 0) {
      g_state.heading.centideg = 4500;
    } else {
      std::fprintf(stderr, "unknown --nav-state \"%s\"\n", name);
      return false;
    }
  } else if (std::strcmp(name, "compass-unusable") == 0) {
    // A heading that exists and may not turn anything: uncalibrated, so the
    // readout goes back to north-up. The screen to check is that it is the
    // *whole* north-up screen and not a half-rotated one.
    g_state.own = own;
    g_state.target = node;
    g_state.heading.source = core::HeadingSource::Magnetometer;
    g_state.heading.frame = core::ReferenceFrame::WatchBody;
    g_state.heading.validity = core::HeadingValidity::Uncalibrated;
    g_state.heading.centideg = 18000;
    g_state.heading.confidence = 90;
  } else if (std::strcmp(name, "far") == 0) {
    node.position.value = core::Position{900000000, 10000000};
    g_state.own = own;
    g_state.target = node;
  } else {
    std::fprintf(stderr,
                 "unknown --nav-state \"%s\"; one of: ready waiting no-fix "
                 "own-stale own-degraded receiver-silent node-unavailable "
                 "node-unknown node-stale arrived far head-up head-up-east "
                 "head-up-south head-up-west compass-unusable\n",
                 name);
    return false;
  }
  return true;
}

void build_nav_screen(const platform::BoardProfile &board, ui::Theme theme) {
  g_config = {
      board.display.width_px,
      board.display.height_px,
      theme,
      board.display.technology == platform::PanelTechnology::Amoled
          ? ui::PixelCost::PerPixel
          : ui::PixelCost::Fixed,
      ui::Metrics::for_dpi(board.display.dpi()),
  };
  // Said here rather than by the caller: this file owns the config `T` has to
  // change, so this is the only place that can answer for it. A composition
  // root deciding on a screen's behalf is how the readout came to be missing
  // from the answer altogether.
  set_theme_toggle(toggle_nav_theme);
  rebuild_nav_screen();
}

void rebuild_nav_screen() {
  // The locale is read at the rebuild, the way the clock reads it, so `L` at
  // runtime switches this screen too.
  g_state.locale = l10n::locale();
  g_face.build(lv_screen_active(), g_config, apps::format_navigation(g_state));
}

} // namespace attadipa::sim
