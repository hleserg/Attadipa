#pragma once

#include <cstdint>

#include "attadipa/platform/display_info.h"
#include "attadipa/platform/hardware_feature.h"
#include "attadipa/platform/radio_info.h"

namespace attadipa::platform {

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

    bool present(HardwareFeature feature) const
    {
        return (present_mask & (1u << static_cast<std::uint32_t>(feature))) != 0;
    }
};

constexpr std::uint32_t feature_bit(HardwareFeature feature)
{
    return 1u << static_cast<std::uint32_t>(feature);
}

// The board profiles this build knows about. There are exactly two --
// `t-watch-s3-plus` and `waveshare-amoled-206` -- and the T-Watch appears
// ONCE, not once per radio variant. An earlier version of this comment said
// otherwise, and it was wrong in the direction that costs time: an agent told
// to change the default radio would grep for the per-variant siblings, find
// none, and have to guess between adding an entry and editing the one that
// exists.
//
// The radio variant is not a profile. It is a field inside the single T-Watch
// profile, initialised to `RadioChip::Unknown` and overridden in place by the
// simulator's `--radio` (sim/options.cpp). To change the default, edit
// `make_twatch()` in board_profiles.cpp.
//
// `Unknown` is the honest default and not an absence of information: A2 has an
// answer since 2026-08-22 -- SX1262 at 868 MHz by order listing
// (OWNER_DECISIONS.md, A1-A3, issue #54) -- and a listing is a seller's claim.
// It stops being the default when somebody reads the marking off the part, not
// when somebody quotes a seller.
const BoardProfile* board_profiles(std::uint8_t& count_out);
const BoardProfile* find_board_profile(const char* id);

}  // namespace attadipa::platform
