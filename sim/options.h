#pragma once

#include <cstdint>

#include "attadipa/l10n/locale.h"
#include "attadipa/platform/board_profile.h"

namespace attadipa::sim {

// What the simulator was asked to present.
//
// Everything here is a runtime choice on purpose. The simulator has to be able
// to show a configuration it was not compiled for — that is the reason
// docs/adr/0001-capability-model.md rejected compile-time feature flags, and
// T-008's acceptance criterion is that switching geometry needs no rebuild.
struct Options {
    platform::BoardProfile board = {};   // a copy: --radio edits it

    float         zoom   = 1.0F;  // window scale; the panel resolution is unchanged
    std::uint32_t frames = 0;     // 0 = run until the window closes

    const char*   screenshot = nullptr;  // write the first rendered frame here, as PNG

    // Which language the screen starts in. It is a runtime choice for the same
    // reason the geometry is: a design review has to see both without a
    // rebuild, and Russian running 15-30% longer than English is a layout
    // question rather than a translation one (ADR-0010).
    l10n::Locale locale = l10n::Locale::En;

    bool node_attached = false;   // a paired, reachable, compatible Attadipa node
    bool bring_up      = true;    // pretend every present part came up

    bool help        = false;
    bool list_boards = false;
};

enum class ParseResult : std::uint8_t { Ok, Exit, Error };

// Parses argv. On Error, a message has already been written to stderr.
ParseResult parse_options(int argc, char** argv, Options& out);

void print_usage(const char* argv0);
void print_boards();

}  // namespace attadipa::sim
