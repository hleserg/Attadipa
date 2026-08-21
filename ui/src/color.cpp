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
};

// One row per role, both themes side by side, so that a role added to one theme
// and forgotten in the other is visible in the source rather than at runtime.
//
// `std::nullopt` in the night column of a *foreground* row means "falls through
// to day, and the contrast test has to agree". In a *background* row it means
// UNKNOWN, and DESIGN_SYSTEM's night table genuinely does not define
// BackgroundRaised — recorded there as a gap rather than filled in here.
const std::array<Entry, 12> kTable{{
    {ColorRole::BackgroundPrimary, ColorKind::Background, kWarmIvory, kInkOlive},
    {ColorRole::BackgroundSurface, ColorKind::Background, kSandBeige, kDarkOlive},
    {ColorRole::BackgroundRaised, ColorKind::Background, kSoftClay, std::nullopt},
    {ColorRole::TextPrimary, ColorKind::Foreground, kInkOlive, kWarmIvory},
    {ColorRole::TextMuted, ColorKind::Foreground, kCocoaBrown, kLeafSage},
    {ColorRole::AccentPrimary, ColorKind::Foreground, kAttadipaOrange, kGlowAmber},
    {ColorRole::AccentGlow, ColorKind::Foreground, kGlowAmber, kGlowAmber},
    {ColorRole::Success, ColorKind::Foreground, kMeadowGreen, std::nullopt},
    {ColorRole::Warning, ColorKind::Foreground, kAttadipaOrange, std::nullopt},
    {ColorRole::Danger, ColorKind::Foreground, std::nullopt, std::nullopt},
    {ColorRole::Navigation, ColorKind::Foreground, kSkyTeal, std::nullopt},
    {ColorRole::BorderSubtle, ColorKind::Foreground, kLeafSage, std::nullopt},
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

std::optional<Rgb> color(ColorRole role, Theme theme)
{
    const Entry& e = entry_for(role);
    if (theme == Theme::Day) {
        return e.day;
    }
    if (e.night.has_value()) {
        return e.night;
    }
    // The fall-through, and its one exception. A background has no defensible
    // day-to-night mapping, so an undefined night background is UNKNOWN rather
    // than a bright card on a dark page.
    return e.kind == ColorKind::Foreground ? e.day : std::nullopt;
}

bool is_defined_for(ColorRole role, Theme theme)
{
    const Entry& e = entry_for(role);
    return theme == Theme::Day ? e.day.has_value() : e.night.has_value();
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

std::uint16_t contrast_against_page_centi(ColorRole role, Theme theme)
{
    const std::optional<Rgb> ink  = color(role, theme);
    const std::optional<Rgb> page = color(ColorRole::BackgroundPrimary, theme);
    if (!ink.has_value() || !page.has_value()) {
        return 0;
    }
    return contrast_ratio_centi(*ink, *page);
}

bool legible_as_graphic(ColorRole role, Theme theme)
{
    return contrast_against_page_centi(role, theme) >= kContrastLargeOrGraphic;
}

bool legible_as_body_text(ColorRole role, Theme theme)
{
    return contrast_against_page_centi(role, theme) >= kContrastBodyText;
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

}  // namespace attadipa::ui
