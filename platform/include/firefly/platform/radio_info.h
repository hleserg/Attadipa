#pragma once

#include <cstdint>

// The radio, described as a radio.
//
// docs/adr/0003-radio-not-lora.md: the part fitted to a T-Watch S3 Plus is one
// of five chips chosen at purchase, and two of them — CC1101 and Si4432 — have
// no LoRa modulator at all. A third, SX1280, does LoRa only at 2.4 GHz and
// cannot hear a sub-GHz mesh. "A radio is present" and "this device can join
// the mesh" are therefore different sentences, and this header exists so the
// second one is derived rather than assumed.
//
// The evidence behind the compatibility matrix is marked PARTIAL: two vendor
// datasheets could not be retrieved (open question R1 in
// docs/research/OPEN_QUESTIONS.md). Nothing here is verified until that closes.

namespace firefly::platform {

enum class RadioChip : std::uint8_t {
    Unknown,  // a radio is fitted and we do not know which — the honest default
    Sx1262,
    Sx1280,
    Lr1121,
    Cc1101,
    Si4432,
};

// A bitmask, because real parts do several. "Which of these can it do" is the
// question every caller actually asks.
enum class Modulation : std::uint16_t {
    None   = 0,
    Lora   = 1u << 0,
    Fsk    = 1u << 1,
    Gfsk   = 1u << 2,
    Msk    = 1u << 3,
    Ook    = 1u << 4,
    Ask    = 1u << 5,
    Flrc   = 1u << 6,
    LrFhss = 1u << 7,
};

constexpr std::uint16_t operator|(Modulation a, Modulation b)
{
    return static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b);
}

constexpr std::uint16_t operator|(std::uint16_t a, Modulation b)
{
    return a | static_cast<std::uint16_t>(b);
}

constexpr bool has_modulation(std::uint16_t mask, Modulation m)
{
    return (mask & static_cast<std::uint16_t>(m)) != 0;
}

// Hertz, integer. Never float: a band edge is a regulatory boundary, and
// rounding one is not a rendering detail.
struct BandRange {
    std::uint32_t lo_hz = 0;
    std::uint32_t hi_hz = 0;

    constexpr bool empty() const { return lo_hz == 0 && hi_hz == 0; }
    constexpr bool contains(std::uint32_t hz) const { return hz >= lo_hz && hz <= hi_hz; }
    constexpr bool overlaps(const BandRange& other) const
    {
        return !empty() && !other.empty() && lo_hz <= other.hi_hz && other.lo_hz <= hi_hz;
    }
};

// Whether the pinned MeshCore revision can drive this chip.
//
// NeedsWork is deliberately distinct from Untested. NeedsWork means we read the
// upstream source and it does not handle this part; Untested means we have not
// looked. Collapsing them turns an absence of evidence into evidence of absence.
enum class MeshCoreSupport : std::uint8_t {
    Untested,
    Supported,
    NeedsWork,
    Impossible,  // the silicon cannot: no LoRa modulator, so no wrapper will help
};

// How the chip is wired. Filled in by the BSP for the fitted variant. A wrong
// value here fails silently, so every field must trace to a schematic for the
// specific board revision — CLAUDE.md's rule, and the reason -1 is the default.
struct RadioControl {
    std::uint8_t spi_host      = 0;
    std::int8_t  pin_nss       = -1;
    std::int8_t  pin_reset     = -1;
    std::int8_t  pin_busy      = -1;
    std::int8_t  pin_irq       = -1;  // which DIO is routed to an interrupt
    std::int8_t  pin_rf_switch = -1;
    bool         tcxo_on_dio3  = false;
};

struct RadioInfo {
    RadioChip       chip              = RadioChip::Unknown;
    std::uint16_t   modulations       = 0;  // Modulation bitmask
    BandRange       bands[3]          = {};
    std::uint8_t    band_count        = 0;
    std::int8_t     max_conducted_dbm = 0;  // the PA ceiling, not a permission
    RadioControl    control           = {};
    MeshCoreSupport meshcore          = MeshCoreSupport::Untested;

    bool can_do_lora() const { return has_modulation(modulations, Modulation::Lora); }
    bool covers(const BandRange& band) const;
};

// The five chips a T-Watch S3 Plus may be fitted with, as
// docs/research/HARDWARE_MATRIX.md records them. Evidence: PARTIAL — see R1.
RadioInfo radio_info_for(RadioChip chip);

const char* to_string(RadioChip chip);
const char* to_string(MeshCoreSupport support);

// Parses "sx1262", "cc1101", … Returns false and leaves `out` untouched on an
// unknown name, so the simulator can reject a typo instead of guessing.
bool parse_radio_chip(const char* name, RadioChip& out);

}  // namespace firefly::platform
