#include "attadipa/apps/navigation.h"

#include <cstdio>
#include <cstring>

#include "attadipa/core/geo.h"
#include "attadipa/l10n/tr.h"

namespace attadipa::apps {
namespace {

// A cut that lands inside a UTF-8 sequence draws a replacement glyph, so the
// trailing continuation bytes come off. The clock does the same thing for the
// same reason (`apps/src/clock.cpp:150` — "void prepend_touch_absent(const
// ClockState &state, ClockText &text) {").
void trim_partial_utf8(char *s) {
  std::size_t lead = std::strlen(s);
  while (lead > 0 && (static_cast<unsigned char>(s[lead - 1]) & 0xC0U) == 0x80U) {
    --lead;
  }
  if (lead > 0 && (static_cast<unsigned char>(s[lead - 1]) & 0x80U) != 0) {
    s[lead - 1] = '\0';
  }
}

void put(char *out, std::size_t size, const char *text) {
  if (std::snprintf(out, size, "%s", text) >= static_cast<int>(size)) {
    trim_partial_utf8(out);
  }
}

bool usable(const core::LocationState &state) {
  return state.has_position && core::in_range(state.position.value);
}

// A distance a person reads at a glance. Metres while metres are the unit
// somebody would walk, then kilometres with one decimal, then whole kilometres.
// The unit is part of the sentence, not glue added afterwards: Russian
// abbreviates every one of these differently, and a unit spelled in C++ is a
// string `l10n/strings.toml:1` — "# Every user-facing string in Attadipa, in
// both locales." cannot see. The generator has already checked that the two
// locales agree about their placeholders, which is what makes these runtime
// format strings safe.
void format_distance(std::uint32_t millimetres, l10n::Locale locale, char *out,
                     std::size_t size) {
  if (millimetres >= core::kDistanceSaturated) {
    // `great_circle_mm()` stops measuring here rather than overflowing, and
    // this is the readout saying the same thing instead of printing the clamp
    // as though it were a measurement.
    put(out, size, l10n::tr(l10n::StringId::NavDistanceSaturated, locale));
    return;
  }
  const std::uint32_t metres = millimetres / 1000U;
  if (metres < 1000U) {
    std::snprintf(out, size, l10n::tr(l10n::StringId::NavDistanceM, locale),
                  static_cast<unsigned>(metres));
  } else if (metres < 100000U) {
    std::snprintf(out, size,
                  l10n::tr(l10n::StringId::NavDistanceKmTenths, locale),
                  static_cast<unsigned>(metres / 1000U),
                  static_cast<unsigned>((metres % 1000U) / 100U));
  } else {
    std::snprintf(out, size, l10n::tr(l10n::StringId::NavDistanceKm, locale),
                  static_cast<unsigned>(metres / 1000U));
  }
}

void format_age(std::uint32_t milliseconds, l10n::Locale locale, char *out,
                std::size_t size) {
  const std::uint32_t seconds = milliseconds / 1000U;
  if (seconds < 120U) {
    std::snprintf(out, size, l10n::tr(l10n::StringId::NavAgeSeconds, locale),
                  static_cast<unsigned>(seconds));
  } else if (seconds < 7200U) {
    std::snprintf(out, size, l10n::tr(l10n::StringId::NavAgeMinutes, locale),
                  static_cast<unsigned>(seconds / 60U));
  } else {
    std::snprintf(out, size, l10n::tr(l10n::StringId::NavAgeHours, locale),
                  static_cast<unsigned>(seconds / 3600U));
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

const char *cardinal_of(std::uint16_t bearing_centideg, l10n::Locale locale) {
  static constexpr l10n::StringId kPoints[8] = {
      l10n::StringId::NavCardinalN,  l10n::StringId::NavCardinalNe,
      l10n::StringId::NavCardinalE,  l10n::StringId::NavCardinalSe,
      l10n::StringId::NavCardinalS,  l10n::StringId::NavCardinalSw,
      l10n::StringId::NavCardinalW,  l10n::StringId::NavCardinalNw};
  // Each sector is 45° wide and centred on its point, so the half-sector offset
  // goes in before the divide: 337.5°..22.5° is north, not 0°..45°.
  const unsigned index =
      ((static_cast<unsigned>(bearing_centideg) + 2250U) / 4500U) % 8U;
  return l10n::tr(kPoints[index], locale);
}

const char *to_string(NavStatus status, l10n::Locale locale) {
  switch (status) {
    case NavStatus::WaitingForGps:
      return l10n::tr(l10n::StringId::NavWaitingForGps, locale);
    case NavStatus::NoFix:
      return l10n::tr(l10n::StringId::NavNoFix, locale);
    case NavStatus::OwnPositionStale:
      return l10n::tr(l10n::StringId::NavOwnPositionStale, locale);
    case NavStatus::OwnPositionDegraded:
      return l10n::tr(l10n::StringId::NavOwnPositionDegraded, locale);
    case NavStatus::NodeUnavailable:
      return l10n::tr(l10n::StringId::NavNodeUnavailable, locale);
    case NavStatus::NodePositionUnknown:
      return l10n::tr(l10n::StringId::NavNodePositionUnknown, locale);
    case NavStatus::NodePositionStale:
      return l10n::tr(l10n::StringId::NavNodePositionStale, locale);
    case NavStatus::Ready:
      return l10n::tr(l10n::StringId::NavReady, locale);
  }
  return l10n::tr(l10n::StringId::NavWaitingForGps, locale);
}

NavText format_navigation(const NavState &state) {
  NavText text{};
  put(text.title, sizeof(text.title),
      l10n::tr(l10n::StringId::NavTitle, state.locale));

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
  } else if (state.own.validity == core::PositionValidity::Degraded) {
    // Current, and solved on too few satellites or too wide an error. The
    // numbers below still render, exactly as they do for `Stale`: a degraded
    // fix is usable, and `position.h` says so. What it is not is `Ready`.
    text.status_code = NavStatus::OwnPositionDegraded;
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
  put(text.status, sizeof(text.status),
      to_string(text.status_code, state.locale));
  put(text.north, sizeof(text.north),
      l10n::tr(l10n::StringId::NavCardinalN, state.locale));

  // ---- the numbers -------------------------------------------------------
  //
  // Both positions, or neither number. A distance needs two coordinates and a
  // bearing needs the same two, so there is no state in which one of them can
  // be shown and the other cannot.
  if (own_ok && usable(state.target)) {
    const core::Position from = state.own.position.value;
    const core::Position to   = state.target.position.value;

    // `great_circle_mm()`, not `distance_mm()`: the latter is the jump
    // detector's, right over the seconds-apart baseline it was written for and
    // 58% over the truth at 89°N across a half-turn of longitude (#433). A
    // screen that refuses `0 m` for an unknown must not print that as a
    // measurement, and the trade the equirectangular form buys — no libm on
    // every fix — is not one a once-per-refresh readout is paying for.
    format_distance(core::great_circle_mm(from, to), state.locale,
                    text.distance, sizeof(text.distance));
    text.has_distance = true;

    std::uint16_t centideg = 0;
    if (core::initial_bearing(from, to, centideg)) {
      text.bearing_centideg = centideg;
      text.has_bearing = true;
      std::snprintf(text.bearing, sizeof(text.bearing), "%03u°",
                    static_cast<unsigned>(centideg / 100U));
      put(text.cardinal, sizeof(text.cardinal),
          cardinal_of(centideg, state.locale));
    }
    // No bearing and a distance is the arrival case: standing on it, or on the
    // same coordinate it reported. The distance still reads, and it reads 0 m
    // because that is measured rather than defaulted.
  }

  // ---- the caveat --------------------------------------------------------
  //
  // Own trouble first, the same order the status line takes and for the same
  // reason. A node coordinate states no fix type, so the node's caveat is owed
  // on *every* coordinate that will ever arrive; testing it first made the
  // wearer's sentence unreachable the moment a paired node said anything, and
  // `Unsupported` then read exactly like a receiver that has simply not fixed
  // yet. The node's caveat is also about numbers that are not on the screen
  // when own position is unusable -- no distance and no bearing are drawn --
  // while the wearer's sentence is the one they can act on.
  if (!own_ok) {
    // Two different answers, and `availability.h` draws the line.
    // `core/include/attadipa/core/availability.h:17` — "    Unsupported,    //
    // no configuration of this device can provide it. Terminal." against
    // `core/include/attadipa/core/availability.h:18` — "    Unprovisioned,  //
    // a supported provider would give it; none is bound". Telling the second
    // reader there is no receiver sends them to buy hardware they already own.
    if (state.own.availability == core::Availability::Unsupported) {
      put(text.caveat, sizeof(text.caveat),
          l10n::tr(l10n::StringId::NavCaveatNoReceiver, state.locale));
    } else if (state.own.availability == core::Availability::Unprovisioned) {
      put(text.caveat, sizeof(text.caveat),
          l10n::tr(l10n::StringId::NavCaveatNoProvider, state.locale));
    }
  } else if (usable(state.target) && !target_states_a_fix(state.target)) {
    // What is not known, said even when everything is working, because the
    // thing that is not known here does not go away on a good day: a node
    // coordinate arrives with no fix type, no observation time and no
    // satellite count, so its age is the age of its *arrival* and its quality
    // is not stated at all.
    char age[16] = {};
    format_age(state.target.position.age_at_us_ms, state.locale, age,
               sizeof(age));
    if (std::snprintf(text.caveat, sizeof(text.caveat),
                      l10n::tr(l10n::StringId::NavCaveatNodeUnverified,
                               state.locale),
                      age) >= static_cast<int>(sizeof(text.caveat))) {
      trim_partial_utf8(text.caveat);
    }
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
