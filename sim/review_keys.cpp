#include "review_keys.h"

#include <cstdint>
#include <cstdio>

#include "attadipa/l10n/tr.h"

namespace attadipa::sim {
namespace {

// Nobody, until a screen says otherwise. A `T` pressed before anything is on
// the panel changes nothing and says so, which is the honest answer rather than
// a theme name for a screen that does not exist.
ThemeToggle g_theme_toggle = nullptr;

} // namespace

void set_theme_toggle(ThemeToggle toggle) { g_theme_toggle = toggle; }

void on_screen_key(lv_event_t *event) {
  (void)event;
  const std::uint32_t key = lv_indev_get_key(lv_indev_active());
  if (key == 'l' || key == 'L') {
    const l10n::Locale next = l10n::locale() == l10n::Locale::En
                                  ? l10n::Locale::Ru
                                  : l10n::Locale::En;
    std::printf("locale: %s\n", l10n::to_string(next));
    l10n::set_locale(next);
  }
  if (key == 't' || key == 'T') {
    if (g_theme_toggle == nullptr) {
      std::printf("theme: unchanged — nothing on this screen follows T\n");
      return;
    }
    // Printed after the screen has been rebuilt, and from the theme the screen
    // reports it is now drawn in, rather than from a variable this file flipped
    // and hoped something downstream would honour.
    std::printf("theme: %s\n", ui::name_of(g_theme_toggle()));
  }
}

} // namespace attadipa::sim
