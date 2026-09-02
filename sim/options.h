#pragma once

#include <cstdint>

#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"
#include "attadipa/l10n/locale.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/color.h"

namespace attadipa::sim {

// What the simulator was asked to present.
//
// Everything here is a runtime choice on purpose. The simulator has to be able
// to show a configuration it was not compiled for — that is the reason
// docs/adr/0001-capability-model.md rejected compile-time feature flags, and
// T-008's acceptance criterion is that switching geometry needs no rebuild.
struct Options {
  platform::BoardProfile board = {}; // a copy: --radio edits it

  float zoom = 1.0F;        // window scale; the panel resolution is unchanged
  std::uint32_t frames = 0; // 0 = run until the window closes

  const char *screenshot =
      nullptr; // write the first rendered frame here, as PNG

  // Which language the screen starts in. It is a runtime choice for the same
  // reason the geometry is: a design review has to see both without a
  // rebuild, and Russian running 15-30% longer than English is a layout
  // question rather than a translation one (ADR-0010).
  l10n::Locale locale = l10n::Locale::En;

  // Which palette the screen starts in. Same argument as the locale: the
  // Definition of Done asks for both themes checked, and a reviewer who has
  // to rebuild to see the second one will check the first. T toggles it while
  // running; this flag is here so CI can screenshot both without a keyboard.
  ui::Theme theme = ui::Theme::Day;

  bool node_attached = false; // a paired, reachable, compatible Attadipa node
  bool bring_up = true;       // pretend every present part came up

  // Where the debug channel listens, or nullptr for not at all.
  //
  // Off by default and a filesystem path rather than a network port. The
  // feature exists for development and must not be something a build acquires
  // by accident -- section 10 of the request, and the same instinct that
  // keeps `--no-bring-up` explicit.
  const char *debug_socket = nullptr;

  // Which screen to build. `diagnostic` is the test pattern that makes a
  // broken screenshot visible; `boot` is the capability screen.
  bool diagnostic_screen = false;

  // The first product screen. A fixed instant makes screenshots repeatable;
  // without one, the simulator follows the host clock.
  bool clock_screen = false;
  bool clock_time_set = false;
  core::WallTime clock_time{};
  core::Availability clock_availability = core::Availability::Ready;
  core::Validity clock_validity = core::Validity::Valid;
  bool child_mode = false;

  // The entry screen, with a board that accepts everything and says so on
  // stdout. What a screenshot of it proves is the layout, not a clock.
  bool provision_screen = false;

  bool help = false;
  bool list_boards = false;
};

enum class ParseResult : std::uint8_t { Ok, Exit, Error };

// Parses argv. On Error, a message has already been written to stderr.
ParseResult parse_options(int argc, char **argv, Options &out);

void print_usage(const char *argv0);
void print_boards();

} // namespace attadipa::sim
