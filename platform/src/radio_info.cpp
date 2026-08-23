#include "attadipa/platform/radio_info.h"

#include <cstring>

namespace attadipa::platform {
namespace {

// A power ceiling we have not established. Zero would read as "1 mW", which is
// a different lie from "we do not know", and the whole point of this file is
// not to make that substitution.
constexpr std::int8_t kUnknownDbm = -128;

}  // namespace

bool RadioInfo::covers(const BandRange& band) const
{
    for (std::uint8_t i = 0; i < band_count && i < 3; ++i) {
        if (bands[i].overlaps(band)) {
            return true;
        }
    }
    return false;
}

// docs/research/HARDWARE_MATRIX.md, "The radio is one of five chips".
//
// Evidence status: PARTIAL. Modulations and bands are read from RadioLib 7.7.1
// driver limits and from MeshCore d929643's build configuration — not from the
// TI and Silicon Labs datasheets, which refused automated retrieval. That is
// open question R1. Conducted power is UNKNOWN for every part, and is recorded
// as unknown rather than guessed, because a transmit power is a regulatory
// number before it is an engineering one.
RadioInfo radio_info_for(RadioChip chip)
{
    RadioInfo info;
    info.chip              = chip;
    info.max_conducted_dbm = kUnknownDbm;

    switch (chip) {
        case RadioChip::Sx1262:
            info.modulations = Modulation::Lora | Modulation::Gfsk | Modulation::LrFhss;
            info.bands[0]    = {150'000'000u, 960'000'000u};
            info.band_count  = 1;
            info.meshcore    = MeshCoreSupport::Supported;
            break;

        case RadioChip::Sx1280:
            // LoRa, but only up here. It cannot hear a sub-GHz mesh, which is
            // why "supports LoRa" is not the question worth asking.
            info.modulations = Modulation::Lora | Modulation::Gfsk | Modulation::Flrc;
            info.bands[0]    = {2'400'000'000u, 2'500'000'000u};
            info.band_count  = 1;
            info.meshcore    = MeshCoreSupport::NeedsWork;  // absent upstream
            break;

        case RadioChip::Lr1121:
            info.modulations = Modulation::Lora | Modulation::Gfsk | Modulation::LrFhss;
            info.bands[0]    = {150'000'000u, 960'000'000u};
            info.bands[1]    = {1'900'000'000u, 2'200'000'000u};
            info.bands[2]    = {2'400'000'000u, 2'500'000'000u};
            info.band_count  = 3;
            info.meshcore    = MeshCoreSupport::NeedsWork;
            break;

        case RadioChip::Cc1101:
            // No LoRa modulator. RadioLib drives it as an FSK/OOK part, and
            // MeshCore compiles it out (RADIOLIB_EXCLUDE_CC1101=1). No wrapper
            // makes this chip join a LoRa mesh.
            info.modulations = Modulation::Fsk | Modulation::Gfsk | Modulation::Ook |
                               Modulation::Ask | Modulation::Msk;
            info.bands[0]    = {300'000'000u, 348'000'000u};
            info.bands[1]    = {387'000'000u, 464'000'000u};
            info.bands[2]    = {779'000'000u, 928'000'000u};
            info.band_count  = 3;
            info.meshcore    = MeshCoreSupport::Impossible;
            break;

        case RadioChip::Si4432:
            info.modulations = Modulation::Fsk | Modulation::Gfsk | Modulation::Ook;
            info.bands[0]    = {240'000'000u, 930'000'000u};
            info.band_count  = 1;
            info.meshcore    = MeshCoreSupport::Impossible;
            break;

        case RadioChip::Unknown:
            // A radio is fitted and A2 now names it -- SX1262 at 868 MHz,
            // from the order listing (OWNER_DECISIONS.md, A1-A3, 2026-08-22,
            // issue #54). Cited by what it decides rather than by its OD
            // number: four open pull requests each insert `## OD-16` at the
            // same line of that file for four different decisions, they touch
            // no file in common so git merges them clean, and this is the only
            // one that writes the number into C++. A citation that names the
            // question cannot come to mean somebody else's answer.
            // This stays Unknown anyway: a listing is a seller's claim, and
            // ADR-0003 moves this enum on a marking read off the part, which
            // needs the watch in hand. Claiming no modulations is right until
            // then -- every derived answer becomes "we cannot say", which is
            // true.
            info.modulations = 0;
            info.band_count  = 0;
            info.meshcore    = MeshCoreSupport::Untested;
            break;
    }

    return info;
}

const char* to_string(RadioChip chip)
{
    switch (chip) {
        case RadioChip::Unknown: return "unknown";
        case RadioChip::Sx1262:  return "SX1262";
        case RadioChip::Sx1280:  return "SX1280";
        case RadioChip::Lr1121:  return "LR1121";
        case RadioChip::Cc1101:  return "CC1101";
        case RadioChip::Si4432:  return "Si4432";
    }
    return "?";
}

const char* to_string(MeshCoreSupport support)
{
    switch (support) {
        case MeshCoreSupport::Untested:   return "untested";
        case MeshCoreSupport::Supported:  return "supported";
        case MeshCoreSupport::NeedsWork:  return "needs driver work";
        case MeshCoreSupport::Impossible: return "impossible";
    }
    return "?";
}

bool parse_radio_chip(const char* name, RadioChip& out)
{
    if (name == nullptr) {
        return false;
    }

    struct Entry {
        const char* name;
        RadioChip   chip;
    };
    static constexpr Entry kEntries[] = {
        {"unknown", RadioChip::Unknown}, {"sx1262", RadioChip::Sx1262},
        {"sx1280", RadioChip::Sx1280},   {"lr1121", RadioChip::Lr1121},
        {"cc1101", RadioChip::Cc1101},   {"si4432", RadioChip::Si4432},
    };

    for (const Entry& entry : kEntries) {
        if (std::strcmp(entry.name, name) == 0) {
            out = entry.chip;
            return true;
        }
    }
    return false;
}

}  // namespace attadipa::platform
