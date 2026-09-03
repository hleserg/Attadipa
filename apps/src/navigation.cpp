#include "attadipa/apps/navigation.h"

#include <cstdio>
#include <cstring>

#include "attadipa/core/geo.h"

namespace attadipa::apps {
namespace {

void put(char *out, std::size_t size, const char *text) {
  std::snprintf(out, size, "%s", text);
}

bool usable(const core::LocationState &state) {
  return state.has_position && core::in_range(state.position.value);
}

// A distance a person reads at a glance. Metres while metres are the unit
// somebody would walk, then kilometres with one decimal, then whole kilometres.
void format_distance(std::uint32_t millimetres, char *out, std::size_t size) {
  if (millimetres >= core::kDistanceSaturated) {
    // `distance_mm()` stops measuring here rather than overflowing, and this is
    // the readout saying the same thing instead of printing the clamp as though
    // it were a measurement.
    put(out, size, "> 1000 km");
    return;
  }
  const std::uint32_t metres = millimetres / 1000U;
  if (metres < 1000U) {
    std::snprintf(out, size, "%u m", static_cast<unsigned>(metres));
  } else if (metres < 100000U) {
    std::snprintf(out, size, "%u.%u km", static_cast<unsigned>(metres / 1000U),
                  static_cast<unsigned>((metres % 1000U) / 100U));
  } else {
    std::snprintf(out, size, "%u km", static_cast<unsigned>(metres / 1000U));
  }
}

void format_age(std::uint32_t milliseconds, char *out, std::size_t size) {
  const std::uint32_t seconds = milliseconds / 1000U;
  if (seconds < 120U) {
    std::snprintf(out, size, "%u s", static_cast<unsigned>(seconds));
  } else if (seconds < 7200U) {
    std::snprintf(out, size, "%u min", static_cast<unsigned>(seconds / 60U));
  } else {
    std::snprintf(out, size, "%u h", static_cast<unsigned>(seconds / 3600U));
  }
}

// Whether the target coordinate carries a stated fix of its own, or is a bare
// coordinate somebody sent. The MeshCore node link produces the second kind and
// always will — stock MeshCore states no fix type — and this is the one place
// that difference is turned into words rather than assumed.
bool target_states_a_fix(const core::LocationState &target) {
  return target.fix_type != core::FixType::Unknown;
}

}  // namespace

const char *cardinal_of(std::uint16_t bearing_centideg) {
  static const char *const kPoints[8] = {"N",  "NE", "E",  "SE",
                                         "S",  "SW", "W",  "NW"};
  // Each sector is 45° wide and centred on its point, so the half-sector offset
  // goes in before the divide: 337.5°..22.5° is north, not 0°..45°.
  const unsigned index =
      ((static_cast<unsigned>(bearing_centideg) + 2250U) / 4500U) % 8U;
  return kPoints[index];
}

const char *to_string(NavStatus status) {
  switch (status) {
    case NavStatus::WaitingForGps:       return "Waiting for GPS";
    case NavStatus::NoFix:               return "No fix";
    case NavStatus::OwnPositionStale:    return "Own position stale";
    case NavStatus::NodeUnavailable:     return "Node unavailable";
    case NavStatus::NodePositionUnknown: return "Node position unknown";
    case NavStatus::NodePositionStale:   return "Node position stale";
    case NavStatus::Ready:               return "Ready";
  }
  return "Waiting for GPS";
}

NavText format_navigation(const NavState &state) {
  NavText text{};

  // ---- the status line, own position first ------------------------------
  //
  // Own trouble outranks the node's, because it is the half the person holding
  // the watch can do something about: going outside fixes a missing fix and
  // nothing fixes a node that has not said where it is.
  //
  // A local coordinate whose validity is `NoFix` counts as no coordinate. That
  // is not the same rule the target gets three lines below, and the asymmetry
  // is the point: a receiver on this body reports its own fix state, so `NoFix`
  // there is the receiver saying so and the coordinate beside it is whatever it
  // last held. A node states no fix state at all, so `NoFix` there means only
  // that nobody asked — refusing on it would refuse every node coordinate ever
  // sent, which is the whole product.
  const bool own_ok = usable(state.own) &&
                      state.own.validity != core::PositionValidity::NoFix;
  if (!own_ok) {
    text.status_code = state.own.fix_type == core::FixType::NoFix
                           ? NavStatus::NoFix
                           : NavStatus::WaitingForGps;
  } else if (state.own.validity == core::PositionValidity::Stale) {
    text.status_code = NavStatus::OwnPositionStale;
  } else if (state.target.availability != core::Availability::Ready) {
    // The link, not the coordinate. `LocationService` deliberately retains what
    // a node said across a disconnect, so the numbers below may still render
    // from a coordinate this line is telling the reader is no longer being
    // refreshed.
    text.status_code = NavStatus::NodeUnavailable;
  } else if (!usable(state.target)) {
    text.status_code = NavStatus::NodePositionUnknown;
  } else if (state.target.position.age_at_us_ms >= state.target_stale_after.value) {
    text.status_code = NavStatus::NodePositionStale;
  } else {
    text.status_code = NavStatus::Ready;
  }
  put(text.status, sizeof(text.status), to_string(text.status_code));

  // ---- the numbers -------------------------------------------------------
  //
  // Both positions, or neither number. A distance needs two coordinates and a
  // bearing needs the same two, so there is no state in which one of them can
  // be shown and the other cannot.
  if (own_ok && usable(state.target)) {
    const core::Position from = state.own.position.value;
    const core::Position to   = state.target.position.value;

    format_distance(core::distance_mm(from, to), text.distance,
                    sizeof(text.distance));
    text.has_distance = true;

    std::uint16_t centideg = 0;
    if (core::initial_bearing(from, to, centideg)) {
      text.bearing_centideg = centideg;
      text.has_bearing = true;
      std::snprintf(text.bearing, sizeof(text.bearing), "%03u°",
                    static_cast<unsigned>(centideg / 100U));
      put(text.cardinal, sizeof(text.cardinal), cardinal_of(centideg));
    }
    // No bearing and a distance is the arrival case: standing on it, or on the
    // same coordinate it reported. The distance still reads, and it reads 0 m
    // because that is measured rather than defaulted.
  }

  // ---- the caveat --------------------------------------------------------
  //
  // What is not known, said even when everything is working, because the thing
  // that is not known here does not go away on a good day: a node coordinate
  // arrives with no fix type, no observation time and no satellite count, so
  // its age is the age of its *arrival* and its quality is not stated at all.
  if (usable(state.target) && !target_states_a_fix(state.target)) {
    char age[16] = {};
    format_age(state.target.position.age_at_us_ms, age, sizeof(age));
    std::snprintf(text.caveat, sizeof(text.caveat),
                  "node fix unverified, heard %s ago", age);
  } else if (!own_ok && state.own.availability == core::Availability::Unprovisioned) {
    put(text.caveat, sizeof(text.caveat), "no receiver on this device");
  }

  return text;
}

const AppManifest &navigation_manifest() {
  // Position and nothing else. A node's coordinate is a feed rather than a
  // capability (docs/adr/0004-capability-sources.md §4), and
  // `core/include/attadipa/core/capability.h:46` — "//   Navigation      — an application built on Position and Heading. Adding it"
  // already settled that this application takes none of its own.
  //
  // Heading is not required, and that is this alpha's shape rather than an
  // oversight: the bearing is stated against true north on a north-up readout,
  // so the application is useful with no magnetometer and becomes more useful
  // with one.
  static constexpr core::Capability required[] = {core::Capability::Position};
  static const AppManifest manifest{"navigation", required, 1,
                                    nullptr,      0,        core::Millis{1000}};
  return manifest;
}

}  // namespace attadipa::apps
