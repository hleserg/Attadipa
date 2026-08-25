#include "options.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace attadipa::sim {
namespace {

bool parse_uint(const char *text, std::uint32_t &out) {
  char *end = nullptr;
  const unsigned long value = std::strtoul(text, &end, 10);
  if (end == text || *end != '\0') {
    return false;
  }
  out = static_cast<std::uint32_t>(value);
  return true;
}

bool parse_int64(const char *text, std::int64_t &out) {
  char *end = nullptr;
  errno = 0;
  const long long value = std::strtoll(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0') {
    return false;
  }
  out = static_cast<std::int64_t>(value);
  return true;
}

bool parse_float(const char *text, float &out) {
  char *end = nullptr;
  const double value = std::strtod(text, &end);
  if (end == text || *end != '\0' || value <= 0.0) {
    return false;
  }
  out = static_cast<float>(value);
  return true;
}

// A flag that needs a value, with the "you forgot the value" case handled once.
const char *take_value(int argc, char **argv, int &i, const char *flag) {
  if (i + 1 >= argc) {
    std::fprintf(stderr, "%s needs a value\n", flag);
    return nullptr;
  }
  return argv[++i];
}

} // namespace

void print_usage(const char *argv0) {
  std::printf(
      "Attadipa simulator\n"
      "\n"
      "usage: %s [options]\n"
      "\n"
      "  --board <id>     which board to present (default: the first one)\n"
      "  --radio <chip>   fit a specific radio: unknown, sx1262, sx1280, "
      "lr1121,\n"
      "                   cc1101, si4432. Only meaningful on a board with a "
      "radio\n"
      "  --zoom <factor>  scale the window. The panel resolution does not "
      "change\n"
      "  --frames <n>     render n frames and exit. For CI, with "
      "SDL_VIDEODRIVER=dummy\n"
      "  --screenshot <p> write the rendered screen to p as a PNG, then "
      "continue\n"
      "  --locale <lang>  start in en or ru. L toggles it while running\n"
      "  --theme <name>   start in day or night. T toggles it while running\n"
      "  --node           present a paired, reachable Attadipa node\n"
      "  --debug-socket <p>  listen for the remote-control tool on the Unix "
      "socket p.\n"
      "                   Off by default. Not a network port: "
      "tools/watch_control.py\n"
      "  --diagnostic     show the test pattern instead of the capability "
      "screen\n"
      "  --clock          show the Clock instead of the capability screen\n"
      "  --clock-time <s> fixed UNIX seconds for a deterministic Clock "
      "screenshot\n"
      "  --clock-state <name> ready, stale, unprovisioned, unreachable, or "
      "failed\n"
      "  --child          render the Clock in Child mode\n"
      "  --no-bring-up    leave every part untouched instead of pretending it "
      "came up\n"
      "  --list-boards    print the board profiles this build knows about\n"
      "  --help\n"
      "\n"
      "Geometry follows the board. Nothing here needs a rebuild.\n",
      argv0);
}

void print_boards() {
  std::uint8_t count = 0;
  const platform::BoardProfile *profiles = platform::board_profiles(count);

  for (std::uint8_t i = 0; i < count; ++i) {
    const platform::BoardProfile &p = profiles[i];
    std::printf("%-22s %4u x %-4u  %3u dpi  %s\n", p.id, p.display.width_px,
                p.display.height_px, p.display.dpi(), p.name);
  }
}

ParseResult parse_options(int argc, char **argv, Options &out) {
  std::uint8_t count = 0;
  const platform::BoardProfile *profiles = platform::board_profiles(count);
  if (count == 0) {
    std::fprintf(stderr, "no board profiles are compiled in\n");
    return ParseResult::Error;
  }
  out.board = profiles[0];

  bool radio_requested = false;
  platform::RadioChip radio = platform::RadioChip::Unknown;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];

    if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      print_usage(argv[0]);
      return ParseResult::Exit;
    }
    if (std::strcmp(arg, "--list-boards") == 0) {
      print_boards();
      return ParseResult::Exit;
    }
    if (std::strcmp(arg, "--node") == 0) {
      out.node_attached = true;
      continue;
    }
    if (std::strcmp(arg, "--no-bring-up") == 0) {
      out.bring_up = false;
      continue;
    }
    if (std::strcmp(arg, "--diagnostic") == 0) {
      out.diagnostic_screen = true;
      continue;
    }
    if (std::strcmp(arg, "--clock") == 0) {
      out.clock_screen = true;
      continue;
    }
    if (std::strcmp(arg, "--child") == 0) {
      out.clock_screen = true;
      out.child_mode = true;
      continue;
    }
    if (std::strcmp(arg, "--clock-time") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr ||
          !parse_int64(value, out.clock_time.unix_seconds)) {
        std::fprintf(stderr, "--clock-time needs whole UNIX seconds\n");
        return ParseResult::Error;
      }
      out.clock_screen = true;
      out.clock_time_set = true;
      continue;
    }
    if (std::strcmp(arg, "--clock-state") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      out.clock_screen = true;
      if (std::strcmp(value, "ready") == 0) {
        out.clock_availability = core::Availability::Ready;
        out.clock_validity = core::Validity::Valid;
      } else if (std::strcmp(value, "stale") == 0) {
        out.clock_availability = core::Availability::Ready;
        out.clock_validity = core::Validity::Stale;
      } else if (std::strcmp(value, "unprovisioned") == 0) {
        out.clock_availability = core::Availability::Unprovisioned;
        out.clock_validity = core::Validity::Unknown;
      } else if (std::strcmp(value, "unreachable") == 0) {
        out.clock_availability = core::Availability::Unreachable;
        out.clock_validity = core::Validity::Stale;
      } else if (std::strcmp(value, "failed") == 0) {
        out.clock_availability = core::Availability::Failed;
        out.clock_validity = core::Validity::Invalid;
      } else {
        std::fprintf(stderr,
                     "unknown Clock state '%s'. Known: ready, stale, "
                     "unprovisioned, unreachable, failed\n",
                     value);
        return ParseResult::Error;
      }
      continue;
    }
    if (std::strcmp(arg, "--debug-socket") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      out.debug_socket = value;
      continue;
    }
    if (std::strcmp(arg, "--board") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      const platform::BoardProfile *found = platform::find_board_profile(value);
      if (found == nullptr) {
        std::fprintf(stderr, "unknown board '%s'. Known boards:\n", value);
        print_boards();
        return ParseResult::Error;
      }
      out.board = *found;
      continue;
    }
    if (std::strcmp(arg, "--radio") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      if (!platform::parse_radio_chip(value, radio)) {
        std::fprintf(stderr,
                     "unknown radio '%s'. Known: unknown, sx1262, sx1280, "
                     "lr1121, cc1101, si4432\n",
                     value);
        return ParseResult::Error;
      }
      radio_requested = true;
      continue;
    }
    if (std::strcmp(arg, "--locale") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      if (std::strcmp(value, "en") == 0) {
        out.locale = l10n::Locale::En;
      } else if (std::strcmp(value, "ru") == 0) {
        out.locale = l10n::Locale::Ru;
      } else {
        std::fprintf(stderr, "unknown locale '%s'. Known: en, ru\n", value);
        return ParseResult::Error;
      }
      continue;
    }
    if (std::strcmp(arg, "--theme") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      if (std::strcmp(value, "day") == 0) {
        out.theme = ui::Theme::Day;
      } else if (std::strcmp(value, "night") == 0) {
        out.theme = ui::Theme::Night;
      } else {
        std::fprintf(stderr, "unknown theme '%s'. Known: day, night\n", value);
        return ParseResult::Error;
      }
      continue;
    }
    if (std::strcmp(arg, "--screenshot") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr) {
        return ParseResult::Error;
      }
      out.screenshot = value;
      continue;
    }
    if (std::strcmp(arg, "--zoom") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr || !parse_float(value, out.zoom)) {
        std::fprintf(stderr, "--zoom needs a positive number\n");
        return ParseResult::Error;
      }
      continue;
    }
    if (std::strcmp(arg, "--frames") == 0) {
      const char *value = take_value(argc, argv, i, arg);
      if (value == nullptr || !parse_uint(value, out.frames)) {
        std::fprintf(stderr, "--frames needs a whole number\n");
        return ParseResult::Error;
      }
      continue;
    }

    std::fprintf(stderr, "unknown option '%s'\n", arg);
    print_usage(argv[0]);
    return ParseResult::Error;
  }

  // --radio applied after --board, so the order of the two flags does not
  // matter. Refused on a board with no radio rather than silently ignored: a
  // flag that quietly does nothing is how people conclude the simulator is
  // lying to them.
  if (radio_requested) {
    if (!out.board.present(platform::HardwareFeature::Radio)) {
      std::fprintf(stderr, "--radio: %s has no radio fitted\n", out.board.id);
      return ParseResult::Error;
    }
    out.board.radio = platform::radio_info_for(radio);
  }

  if (out.diagnostic_screen && out.clock_screen) {
    std::fprintf(stderr, "--diagnostic and --clock select different screens\n");
    return ParseResult::Error;
  }

  return ParseResult::Ok;
}

} // namespace attadipa::sim
