#include "attadipa/apps/app_registry.h"

#include "attadipa/apps/clock.h"
#include "attadipa/apps/navigation.h"

namespace attadipa::apps {

const AppManifest &settings_manifest() {
  // No `required`, no `enhanced_by`, and no cadence: nothing on a settings
  // screen changes unless the operator changes it, so it is redrawn on input
  // and never on a tick.
  static const AppManifest manifest{"settings", nullptr, 0,
                                    nullptr,    0,       core::Millis{}};
  return manifest;
}

const AppRegistry &installed_apps() {
  // Launcher order. Settings last because it is the one entry that is always
  // there — a permanent row at the end is easier to reach for than one that
  // moves as applications appear and disappear above it.
  static const AppManifest *const apps[] = {
      &clock_manifest(),
      &navigation_manifest(),
      &settings_manifest(),
  };
  static const AppRegistry registry{
      apps, static_cast<std::uint8_t>(sizeof(apps) / sizeof(apps[0]))};
  return registry;
}

core::Millis ui_period(const AppManifest &manifest, core::Millis board_period) {
  if (manifest.tick_period.value == 0 ||
      manifest.tick_period.value > board_period.value) {
    return board_period;
  }
  return manifest.tick_period;
}

} // namespace attadipa::apps
