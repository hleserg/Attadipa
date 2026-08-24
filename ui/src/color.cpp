#include "attadipa/ui/color.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace attadipa::ui {
namespace {

// The seeds, transcribed from DESIGN_SYSTEM §3, which took them from final §42
// as text rather than by sampling the style boards — raster is lossy about
// intent. This is the only place in the repository where a colour is written as
// a number.
constexpr Rgb kWarmIvory{0xFF, 0xF6, 0xE8};
constexpr Rgb kSandBeige{0xF3, 0xE8, 0xD1};
constexpr Rgb kSoftClay{0xE9, 0xDC, 0xC2};
constexpr Rgb kInkOlive{0x2F, 0x3A, 0x2E};
constexpr Rgb kCocoaBrown{0x7A, 0x5E, 0x3A};
constexpr Rgb kAttadipaOrange{0xFF, 0x8A, 0x40};
constexpr Rgb kGlowAmber{0xFF, 0xC8, 0x57};
constexpr Rgb kMeadowGreen{0x6F, 0xA0, 0x7A};
constexpr Rgb kSkyTeal{0x6F, 0xB7, 0xB5};
constexpr Rgb kLeafSage{0xA7, 0xB4, 0x9C};
constexpr Rgb kDarkOlive{0x3C, 0x40, 0x33};

struct Entry {
    ColorRole          role;
    ColorKind          kind;
    std::optional<Rgb> day;
    std::optional<Rgb> night;
    // Day, on a panel where `PixelCost::PerPixel` — OD-16. Explicit rather than
    // derived from the other two columns, for the same reason they are not
    // derived from each other: a role added to one and forgotten in another
    // is visible in the source rather than at runtime.
    //
    // Every value below already exists in the seed set above; this column
    // invents no colour. Backgrounds and text reuse the night column, which
    // DESIGN_SYSTEM already audited for contrast against a dark page.
    // `AccentPrimary` is the one deliberate exception — it keeps Day's own
    // Attadipa Orange rather than Night's Amber, so that day and night remain
    // visually distinct on an emissive panel instead of becoming the same
    // theme under two names.
    std::optional<Rgb> day_emissive;
};

// One row per role, every theme side by side, so that a role added to one
// column and forgotten in another is visible in the source rather than at
// runtime.
//
// `std::nullopt` in the night column of a *foreground* row means "falls through
// to day, and the contrast test has to agree". In a *background* row it means
// UNKNOWN, and DESIGN_SYSTEM's night table genuinely does not define
// BackgroundRaised — recorded there as a gap rather than filled in here. The
// `day_emissive` column never falls through: every role that has an answer at
// all states it, because it does not share `Theme::Day`'s fall-through rule
// (it is consulted only through `color()`'s own `pixel_cost` branch).
const std::array<Entry, 12> kTable{{
    {ColorRole::BackgroundPrimary, ColorKind::Background, kWarmIvory, kInkOlive, kInkOlive},
    {ColorRole::BackgroundSurface, ColorKind::Background, kSandBeige, kDarkOlive, kDarkOlive},
    {ColorRole::BackgroundRaised, ColorKind::Background, kSoftClay, std::nullopt, std::nullopt},
    {ColorRole::TextPrimary, ColorKind::Foreground, kInkOlive, kWarmIvory, kWarmIvory},
    {ColorRole::TextMuted, ColorKind::Foreground, kCocoaBrown, kLeafSage, kLeafSage},
    {ColorRole::AccentPrimary, ColorKind::Foreground, kAttadipaOrange, kGlowAmber, kAttadipaOrange},
    {ColorRole::AccentGlow, ColorKind::Foreground, kGlowAmber, kGlowAmber, kGlowAmber},
    {ColorRole::Success, ColorKind::Foreground, kMeadowGreen, std::nullopt, kMeadowGreen},
    {ColorRole::Warning, ColorKind::Foreground, kAttadipaOrange, std::nullopt, kAttadipaOrange},
    {ColorRole::Danger, ColorKind::Foreground, std::nullopt, std::nullopt, std::nullopt},
    {ColorRole::Navigation, ColorKind::Foreground, kSkyTeal, std::nullopt, kSkyTeal},
    {ColorRole::BorderSubtle, ColorKind::Foreground, kLeafSage, std::nullopt, kLeafSage},
}};

const Entry& entry_for(ColorRole role)
{
    for (const Entry& e : kTable) {
        if (e.role == role) {
            return e;
        }
    }
    return kTable[0];
}

// WCAG 2.1: linearise each channel, then weight.
double channel(std::uint8_t v)
{
    const double s = static_cast<double>(v) / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double luminance(Rgb c)
{
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}

}  // namespace

ColorKind kind_of(ColorRole role)
{
    return entry_for(role).kind;
}

std::optional<Rgb> color(ColorRole role, Theme theme, PixelCost pixel_cost)
{
    const Entry& e = entry_for(role);
    if (theme == Theme::Day) {
        return pixel_cost == PixelCost::PerPixel ? e.day_emissive : e.day;
    }
    if (e.night.has_value()) {
        return e.night;
    }
    // The fall-through, and its one exception. A background has no defensible
    // day-to-night mapping, so an undefined night background is UNKNOWN rather
    // than a bright card on a dark page.
    return e.kind == ColorKind::Foreground ? e.day : std::nullopt;
}

bool is_defined_for(ColorRole role, Theme theme, PixelCost pixel_cost)
{
    const Entry& e = entry_for(role);
    if (theme == Theme::Day) {
        return pixel_cost == PixelCost::PerPixel ? e.day_emissive.has_value() : e.day.has_value();
    }
    return e.night.has_value();
}

std::uint16_t contrast_ratio_centi(Rgb a, Rgb b)
{
    const double la      = luminance(a);
    const double lb      = luminance(b);
    const double lighter = la > lb ? la : lb;
    const double darker  = la > lb ? lb : la;
    const double ratio   = (lighter + 0.05) / (darker + 0.05);
    return static_cast<std::uint16_t>(ratio * 100.0 + 0.5);
}

std::uint16_t contrast_against_page_centi(ColorRole role, Theme theme, PixelCost pixel_cost)
{
    const std::optional<Rgb> ink  = color(role, theme, pixel_cost);
    const std::optional<Rgb> page = color(ColorRole::BackgroundPrimary, theme, pixel_cost);
    if (!ink.has_value() || !page.has_value()) {
        return 0;
    }
    return contrast_ratio_centi(*ink, *page);
}

bool legible_as_graphic(ColorRole role, Theme theme, PixelCost pixel_cost)
{
    return contrast_against_page_centi(role, theme, pixel_cost) >= kContrastLargeOrGraphic;
}

bool legible_as_body_text(ColorRole role, Theme theme, PixelCost pixel_cost)
{
    return contrast_against_page_centi(role, theme, pixel_cost) >= kContrastBodyText;
}

const char* name_of(ColorRole role)
{
    switch (role) {
        case ColorRole::BackgroundPrimary: return "color.background.primary";
        case ColorRole::BackgroundSurface: return "color.background.surface";
        case ColorRole::BackgroundRaised:  return "color.background.raised";
        case ColorRole::TextPrimary:       return "color.text.primary";
        case ColorRole::TextMuted:         return "color.text.muted";
        case ColorRole::AccentPrimary:     return "color.accent.primary";
        case ColorRole::AccentGlow:        return "color.accent.glow";
        case ColorRole::Success:           return "color.success";
        case ColorRole::Warning:           return "color.warning";
        case ColorRole::Danger:            return "color.danger";
        case ColorRole::Navigation:        return "color.navigation";
        case ColorRole::BorderSubtle:      return "color.border.subtle";
    }
    return "color.unknown";
}

const char* name_of(Theme theme)
{
    switch (theme) {
        case Theme::Day:   return "day";
        case Theme::Night: return "night";
    }
    return "unknown";
}

const char* name_of(PixelCost pixel_cost)
{
    switch (pixel_cost) {
        case PixelCost::Fixed:    return "fixed";
        case PixelCost::PerPixel: return "per-pixel";
    }
    return "unknown";
}

}  // namespace attadipa::ui
