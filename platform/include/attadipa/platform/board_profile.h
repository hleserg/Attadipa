#pragma once

#include <cstdint>

#include "attadipa/platform/display_info.h"
#include "attadipa/platform/hardware_feature.h"
#include "attadipa/platform/radio_info.h"

namespace attadipa::platform {

// One pressable button, as a board fact.
//
// Here rather than in the input layer because which keys exist is a property of
// the board, and core/ is not allowed to hold a table of board facts. The
// interesting field is `role_known`: on the Waveshare the owner counted **two**
// pressable buttons on the assembled case, and which named input each one
// reaches -- `Key1`, `Key3` or the PMU's `PWRON` -- is open question D5. A
// profile that named them "power" and "back" would be inventing the answer, so
// they are numbered and flagged, and anything that displays them can say so.
struct ButtonSpec {
    const char* id         = "";     // stable, lowercase; what a tool names on the command line
    bool        role_known = false;  // false: it is pressable, and what it does is not established
    bool        injectable = true;   // false: a service key a debug channel must not simulate
};

inline constexpr std::uint8_t kMaxBoardButtons = 3;

// Everything the hardware layer knows about a board, as data rather than as
// #ifdefs. One reason it is data: the simulator must be able to present a
// configuration it was not compiled for (docs/adr/0001-capability-model.md
// rejects compile-time feature flags for exactly this).
//
// It describes a board *variant*. A T-Watch S3 Plus with a CC1101 and one with
// an SX1262 are two profiles, because the difference decides whether the device
// can join a mesh at all.
struct BoardProfile {
    const char*   id           = "";       // stable, lowercase, for --board and logs
    const char*   name         = "";       // for a human; not localized, this is a part number
    DisplayInfo   display      = {};
    std::uint32_t present_mask = 0;        // bit per HardwareFeature
    RadioInfo     radio        = {};       // meaningless unless present(Radio)

    ButtonSpec   buttons[kMaxBoardButtons] = {};
    std::uint8_t button_count              = 0;

    bool present(HardwareFeature feature) const
    {
        return (present_mask & (1u << static_cast<std::uint32_t>(feature))) != 0;
    }
};

constexpr std::uint32_t feature_bit(HardwareFeature feature)
{
    return 1u << static_cast<std::uint32_t>(feature);
}

// The board profiles this build knows about.
//
// The T-Watch appears more than once, once per radio variant, because the chip
// is chosen at purchase and open question A2 records that we do not know which
// one this project has. A profile named "t-watch, radio unknown" is the honest
// default and is deliberately the first one.
const BoardProfile* board_profiles(std::uint8_t& count_out);
const BoardProfile* find_board_profile(const char* id);

}  // namespace attadipa::platform
