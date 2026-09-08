#pragma once

#include <cstdint>

#include "attadipa/apps/app_manifest.h"

namespace attadipa::apps {

// The installed applications, and the two answers a composition root needs
// about them.
//
// Until this existed, `launcher_entry()` had no production caller anywhere in
// the tree: the rule ADR-0007 §3 states was implemented, tested, and invisible
// on a device, because nothing enumerated the manifests to ask about. A board
// that iterates this asks the question for every application it ships instead
// of naming one by hand.

// Static storage and a fixed count. This is what the device ships with, not a
// list anything adds to at runtime, so the pointer outlives every caller and
// nothing is allocated.
struct AppRegistry {
  const AppManifest *const *apps = nullptr;
  std::uint8_t count = 0;
};

const AppRegistry &installed_apps();

// Settings requires nothing, and that is the whole declaration.
//
// A settings screen that can vanish because a capability is missing is a device
// an operator cannot get back out of. With `required_count == 0`,
// `launcher_entry()` answers `Available` on every board in every configuration
// — which is what "a permanent entry" means in code rather than in prose.
//
// It is declared here rather than in a settings.cpp because there is no
// Settings application yet, only its promise. It moves next to the application
// the day one exists.
const AppManifest &settings_manifest();

// The period a composition root gives its UI timer while `manifest` is the
// application on screen.
//
// `board_period` is the composition root's own obligation rather than the
// application's: the same timer may carry work that has to happen whatever page
// is up — a receive ring to drain, say — so this never returns zero and the
// timer is never a candidate for pausing.
//
// A manifest declaring `tick_period{}` asks for no cadence of its own and gets
// `board_period`. A manifest asking to be refreshed *less* often than that also
// gets `board_period`: refreshing an application more often than it asked for is
// harmless, and leaving the board's own work undone is not.
core::Millis ui_period(const AppManifest &manifest, core::Millis board_period);

} // namespace attadipa::apps
