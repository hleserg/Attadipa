#include "mesh_screen.h"

#include <cstdio>
#include <cstring>

#include "lvgl.h"

#include "attadipa/apps/mesh.h"
#include "attadipa/core/mesh_service.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/ui/mesh_face.h"
#include "attadipa/ui/metrics.h"

#include "review_keys.h"
#include "attadipa/ui/status_frame.h"

namespace attadipa::sim {
namespace {

using namespace attadipa;

ui::StatusFrame g_frame;
ui::MeshFace g_face;
ui::MeshFaceConfig g_config;
core::MeshStatus g_status;

ui::Theme toggle_mesh_theme() {
  g_config.theme =
      g_config.theme == ui::Theme::Day ? ui::Theme::Night : ui::Theme::Day;
  rebuild_mesh_screen();
  return g_config.theme;
}

// Two invented keys, differing in every one of the four bytes the screen
// prints. Two prefixes that share seven of their eight characters would defeat
// the reason the screen shows a prefix at all -- and the first render of this
// fixture did exactly that, because only the leading byte varied.
core::MeshPeerId key(std::uint32_t bytes) {
  core::MeshPeerId id{};
  id.public_key[0] = static_cast<std::uint8_t>(bytes >> 24U);
  id.public_key[1] = static_cast<std::uint8_t>(bytes >> 16U);
  id.public_key[2] = static_cast<std::uint8_t>(bytes >> 8U);
  id.public_key[3] = static_cast<std::uint8_t>(bytes);
  return id;
}

void copy(std::array<char, core::kMeshPeerNameBytes + 1> &out, const char *text) {
  std::snprintf(out.data(), out.size(), "%s", text);
}

void with_session(core::MeshStatus &status) {
  status.node_id = key(0x4C9A2F7BU);
  status.has_node_id = true;
  copy(status.node_name, "Ridge companion");
  copy(status.last_sender, "Ridge companion");
  std::snprintf(status.last_message.data(), status.last_message.size(), "%s",
                "at the ridge, heading down");
  status.delivery = core::MeshDelivery::Confirmed;
  status.snr_quarter_db = 29; // 7.25 dB
  status.has_snr = true;
  status.peers_reported = 3;
  status.peers_retained = 3; // the whole list kept: one number, not a pair
  status.peers_complete = true;
  status.mtu = 244;
  status.node_battery.millivolts = 3700;
  status.node_battery.validity = core::Validity::Valid;
  status.node_battery.received_at = core::MonotonicTime{1000};
}

} // namespace

const core::MeshStatus &staged_mesh_status() { return g_status; }

bool stage_mesh_scenario(const char *name) {
  g_status = core::MeshStatus{};
  g_status.node_battery.separate_supply = true;

  if (std::strcmp(name, "unprovisioned") == 0) {
    g_status.availability = core::Availability::Unprovisioned;
    g_status.transport = core::TransportPhase::Absent;
  } else if (std::strcmp(name, "absent") == 0) {
    g_status.availability = core::Availability::Unreachable;
    g_status.transport = core::TransportPhase::Absent;
  } else if (std::strcmp(name, "attached") == 0) {
    g_status.availability = core::Availability::Unreachable;
    g_status.transport = core::TransportPhase::Attached;
  } else if (std::strcmp(name, "connecting") == 0) {
    g_status.availability = core::Availability::Unreachable;
    g_status.transport = core::TransportPhase::Connecting;
  } else if (std::strcmp(name, "battery-unknown") == 0 ||
             std::strcmp(name, "battery-stale") == 0 ||
             std::strcmp(name, "battery-low") == 0 ||
             std::strcmp(name, "integrated") == 0 ||
             std::strcmp(name, "ready") == 0) {
    g_status.availability = core::Availability::Ready;
    g_status.transport = core::TransportPhase::Ready;
    with_session(g_status);
  } else if (std::strcmp(name, "suspended") == 0) {
    // The fields a `Ready` link filled, on a link that is now deliberately off.
    // This is the state the screen has to draw differently from `ready` while
    // showing the same six values, and it is why the heading changes rather
    // than the values disappearing.
    g_status.availability = core::Availability::Ready;
    g_status.transport = core::TransportPhase::Suspended;
    with_session(g_status);
  } else if (std::strcmp(name, "faulted") == 0) {
    g_status.availability = core::Availability::Unreachable;
    g_status.transport = core::TransportPhase::Faulted;
  } else if (std::strcmp(name, "refused") == 0) {
    g_status.availability = core::Availability::Unreachable;
    g_status.transport = core::TransportPhase::Connecting;
    g_status.pinned_id = key(0x4C9A2F7BU);
    g_status.has_pinned = true;
    g_status.refused_id = key(0x9E14C003U);
    g_status.has_refused = true;
  } else if (std::strcmp(name, "refused-faulted") == 0) {
    // A REFUSAL THAT IS STILL LATCHED WHEN THE TRANSPORT GIVES UP.
    //
    // Reachable and not a corner: `MeshCoreCompanion::reset_session()` keeps
    // the pin and the refusal across a disconnect on purpose, so any fault
    // after a wrong node answered arrives here. It is staged because it used
    // to draw `refused` above -- the wearer told to select a different node
    // by a screen whose link had faulted (#465).
    g_status.availability = core::Availability::Failed;
    g_status.transport = core::TransportPhase::Faulted;
    g_status.pinned_id = key(0x4C9A2F7BU);
    g_status.has_pinned = true;
    g_status.refused_id = key(0x9E14C003U);
    g_status.has_refused = true;
  } else if (std::strcmp(name, "refused-unnamed") == 0) {
    // THE OTHER SCREEN A LATCHED REFUSAL REACHES, AND THE ONLY ONE THAT DRAWS
    // KEYS AND A WAY OUT AT ONCE.
    //
    // `Unprovisioned` is terminal once the transport has faulted, so the
    // refusal does not outrank it and the screen is `NoNode`: no node named,
    // both keys still latched, and "hold the clock to name one" -- which is
    // the instruction, so it stays. Staged because the arrangement that put
    // the keys under the note drew the second of them on the way out's row.
    g_status.availability = core::Availability::Unprovisioned;
    g_status.transport = core::TransportPhase::Faulted;
    g_status.pinned_id = key(0x4C9A2F7BU);
    g_status.has_pinned = true;
    g_status.refused_id = key(0x9E14C003U);
    g_status.has_refused = true;
  } else if (std::strcmp(name, "truncated") == 0) {
    // Both completeness flags at once, on a live link: a message longer than
    // `last_message` and more contacts than the retained set holds. Neither is
    // a layout problem, so neither may look like one.
    g_status.availability = core::Availability::Ready;
    g_status.transport = core::TransportPhase::Ready;
    with_session(g_status);
    std::snprintf(g_status.last_message.data(), g_status.last_message.size(),
                  "%s",
                  "at the ridge, heading down the north side before the light "
                  "goes and the wind gets up, will call from the sadd");
    g_status.message_truncated = true;
    g_status.peers_reported = 40;
    g_status.peers_retained = 16;
    g_status.peers_complete = true;
  } else {
    std::fprintf(stderr,
                 "unknown --mesh-state '%s'\n"
                 "known: unprovisioned absent attached connecting ready "
                 "suspended faulted refused refused-faulted refused-unnamed "
                 "truncated "
                 "battery-unknown battery-stale battery-low integrated\n",
                 name);
    return false;
  }
  if (std::strcmp(name, "battery-unknown") == 0) {
    g_status.node_battery.millivolts = 0;
    g_status.node_battery.validity = core::Validity::Unknown;
    g_status.node_battery.received_at = {};
  } else if (std::strcmp(name, "battery-stale") == 0) {
    g_status.node_battery.validity = core::Validity::Stale;
  } else if (std::strcmp(name, "battery-low") == 0) {
    // A reported low voltage, not a fabricated state-of-charge threshold.
    g_status.node_battery.millivolts = 3000;
  } else if (std::strcmp(name, "integrated") == 0) {
    g_status.node_battery = {};
  }
  return true;
}

void build_mesh_screen_sim(const platform::BoardProfile &board, ui::Theme theme) {
  g_config = {
      board.display.width_px,
      board.display.height_px,
      theme,
      board.display.technology == platform::PanelTechnology::Amoled
          ? ui::PixelCost::PerPixel
          : ui::PixelCost::Fixed,
      ui::Metrics::for_dpi(board.display.dpi()),
  };
  set_theme_toggle(toggle_mesh_theme);
  rebuild_mesh_screen();
}

void rebuild_mesh_screen() {
  // The locale is read at the rebuild, the way the clock and the readout do it,
  // so `L` at runtime switches this screen too.
  const auto text = apps::format_mesh(g_status, l10n::locale());
  g_face.clear();
  g_frame.build(lv_screen_active(), {g_config.width_px, g_config.height_px,
                g_config.theme, g_config.pixel_cost, g_config.metrics});
  auto content = g_config;
  content.height_px = g_frame.content_height();
  g_face.build(g_frame.content(), content, text);
  g_frame.restore_content_geometry();
  g_frame.update(text);
}

} // namespace attadipa::sim
