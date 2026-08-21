#pragma once

#include <cstdint>

#include "firefly/platform/board_profile.h"

namespace firefly::sim {

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

    bool node_attached = false;   // a paired, reachable, compatible Firefly node
    bool bring_up      = true;    // pretend every present part came up

    bool help        = false;
    bool list_boards = false;
};

enum class ParseResult : std::uint8_t { Ok, Exit, Error };

// Parses argv. On Error, a message has already been written to stderr.
ParseResult parse_options(int argc, char** argv, Options& out);

void print_usage(const char* argv0);
void print_boards();

}  // namespace firefly::sim
