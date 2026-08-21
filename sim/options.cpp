#include "options.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace firefly::sim {
namespace {

bool parse_uint(const char* text, std::uint32_t& out)
{
    char*             end   = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_float(const char* text, float& out)
{
    char*        end   = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0' || value <= 0.0) {
        return false;
    }
    out = static_cast<float>(value);
    return true;
}

// A flag that needs a value, with the "you forgot the value" case handled once.
const char* take_value(int argc, char** argv, int& i, const char* flag)
{
    if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", flag);
        return nullptr;
    }
    return argv[++i];
}

}  // namespace

void print_usage(const char* argv0)
{
    std::printf(
        "Firefly OS simulator\n"
        "\n"
        "usage: %s [options]\n"
        "\n"
        "  --board <id>     which board to present (default: the first one)\n"
        "  --radio <chip>   fit a specific radio: unknown, sx1262, sx1280, lr1121,\n"
        "                   cc1101, si4432. Only meaningful on a board with a radio\n"
        "  --zoom <factor>  scale the window. The panel resolution does not change\n"
        "  --frames <n>     render n frames and exit. For CI, with SDL_VIDEODRIVER=dummy\n"
        "  --screenshot <p> write the rendered screen to p as a PNG, then continue\n"
        "  --node           present a paired, reachable Firefly node\n"
        "  --no-bring-up    leave every part untouched instead of pretending it came up\n"
        "  --list-boards    print the board profiles this build knows about\n"
        "  --help\n"
        "\n"
        "Geometry follows the board. Nothing here needs a rebuild.\n",
        argv0);
}

void print_boards()
{
    std::uint8_t                count    = 0;
    const platform::BoardProfile* profiles = platform::board_profiles(count);

    for (std::uint8_t i = 0; i < count; ++i) {
        const platform::BoardProfile& p = profiles[i];
        std::printf("%-22s %4u x %-4u  %3u dpi  %s\n", p.id, p.display.width_px,
                    p.display.height_px, p.display.dpi(), p.name);
    }
}

ParseResult parse_options(int argc, char** argv, Options& out)
{
    std::uint8_t                count    = 0;
    const platform::BoardProfile* profiles = platform::board_profiles(count);
    if (count == 0) {
        std::fprintf(stderr, "no board profiles are compiled in\n");
        return ParseResult::Error;
    }
    out.board = profiles[0];

    bool radio_requested = false;
    platform::RadioChip radio = platform::RadioChip::Unknown;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

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
        if (std::strcmp(arg, "--board") == 0) {
            const char* value = take_value(argc, argv, i, arg);
            if (value == nullptr) {
                return ParseResult::Error;
            }
            const platform::BoardProfile* found = platform::find_board_profile(value);
            if (found == nullptr) {
                std::fprintf(stderr, "unknown board '%s'. Known boards:\n", value);
                print_boards();
                return ParseResult::Error;
            }
            out.board = *found;
            continue;
        }
        if (std::strcmp(arg, "--radio") == 0) {
            const char* value = take_value(argc, argv, i, arg);
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
        if (std::strcmp(arg, "--screenshot") == 0) {
            const char* value = take_value(argc, argv, i, arg);
            if (value == nullptr) {
                return ParseResult::Error;
            }
            out.screenshot = value;
            continue;
        }
        if (std::strcmp(arg, "--zoom") == 0) {
            const char* value = take_value(argc, argv, i, arg);
            if (value == nullptr || !parse_float(value, out.zoom)) {
                std::fprintf(stderr, "--zoom needs a positive number\n");
                return ParseResult::Error;
            }
            continue;
        }
        if (std::strcmp(arg, "--frames") == 0) {
            const char* value = take_value(argc, argv, i, arg);
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

    return ParseResult::Ok;
}

}  // namespace firefly::sim
