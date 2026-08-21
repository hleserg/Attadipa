#pragma once

#include <cstdint>
#include <optional>

namespace attadipa::ui {

// 8-bit-per-channel colour, which is what the design system is written in.
//
// Not a panel format. The T-Watch is 262K-colour IPS and the Waveshare is
// RGB565 AMOLED; converting to either is the display driver's job, and a token
// that carried a panel's bit depth would be a token that knew which board it
// was on.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    constexpr bool operator==(Rgb other) const
    {
        return r == other.r && g == other.g && b == other.b;
    }
    constexpr bool operator!=(Rgb other) const { return !(*this == other); }

    // 0xRRGGBB, for the one place that needs it: handing a value to a graphics
    // library. Deliberately a method rather than the storage format, so that
    // `0xFF8A40` cannot be written as a colour anywhere except here.
    constexpr std::uint32_t packed() const
    {
        return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) |
               static_cast<std::uint32_t>(b);
    }
};

enum class Theme : std::uint8_t { Day, Night };

// Semantic roles. A screen asks for `Accent Primary`, never for orange —
// DESIGN_SYSTEM §3. The enumerator names are the token names with the dots
// removed, so that a diagnostic can print one and a designer recognises it.
enum class ColorRole : std::uint8_t {
    BackgroundPrimary,
    BackgroundSurface,
    BackgroundRaised,
    TextPrimary,
    TextMuted,
    AccentPrimary,
    AccentGlow,
    Success,
    Warning,
    Danger,
    Navigation,
    BorderSubtle,
};

// Whether a role paints behind things or on top of them.
//
// This is not decoration: it decides what happens when the night theme does not
// define a role. See `color()`.
enum class ColorKind : std::uint8_t { Background, Foreground };

ColorKind kind_of(ColorRole role);

// The value of a role in a theme, or nothing.
//
// `std::optional` rather than a colour with a "valid" flag, because a caller
// that ignores the empty case should not compile into one that paints black.
// Two different things come back empty and both are real:
//
//   - `Danger` has no value in either theme. There is no red in either owner
//     palette (DESIGN_SYSTEM §3.1) and inventing one is a visual-identity
//     decision that belongs to the owner. It is UNKNOWN rather than quietly
//     assigned, and it stays UNKNOWN until the owner assigns it.
//   - a *background* role the night theme does not define. Final §47 says night
//     is not inverted day, so there is no rule by which a day background
//     becomes a night one — Soft Clay as a raised card on a dark page is a
//     bright rectangle, not a dark theme. Guessing would produce a screen that
//     looks deliberate and is not.
//
// Foreground roles the night theme does not define DO fall through to their day
// value, because a hue that means "delivered" should keep meaning it after
// sunset. That fall-through is only safe while the result stays legible on the
// night page, which is not a matter of taste and is checked: see
// `contrast_ratio` and the test that walks every role.
std::optional<Rgb> color(ColorRole role, Theme theme);

// Whether this role's value in this theme came from the theme's own table
// rather than from the day fall-through above. For diagnostics and for the
// tests that pin the fall-through rule; a screen has no reason to care.
bool is_defined_for(ColorRole role, Theme theme);

// WCAG 2.1 relative luminance and contrast ratio, in the standard formulation.
//
// Here rather than in a test because the fall-through rule in `color()` is only
// defensible if something checks it, and a check that lives only in a test file
// cannot be run against a value an application computes at runtime.
//
// Returned in hundredths, so that 4.5:1 is 450 and no float crosses a header
// that embedded code includes.
std::uint16_t contrast_ratio_centi(Rgb a, Rgb b);

// The contrast between a role and the page it is painted on, in hundredths.
// Zero when either side is UNKNOWN, which a caller must distinguish from "no
// contrast" — the two are not the same and only one of them is a design fault.
std::uint16_t contrast_against_page_centi(ColorRole role, Theme theme);

// WCAG 2.1 AA, the two thresholds that matter here: 4.5:1 for body text, 3:1
// for large text, icons and the boundary of a control. Expressed in hundredths
// to match `contrast_ratio_centi`.
inline constexpr std::uint16_t kContrastBodyText = 450;
inline constexpr std::uint16_t kContrastLargeOrGraphic = 300;

// Whether this role may carry meaning on its own — a word, a thin glyph, a
// one-pixel outline — on this theme's page.
//
// It is a computed property and not an opinion, and on the day palette the
// answer is mostly **no**: Attadipa Orange on Warm Ivory is 2.19:1 and Glow
// Amber is 1.44:1. Those are the owner's colours and they are not this file's
// to change, so this function exists to say what they can and cannot be asked
// to do. It is also, independently, why DESIGN_SYSTEM §3.1 forbids signalling
// any state by colour alone — a rule that was written for colour-blindness and
// turns out to be load-bearing for everyone.
bool legible_as_graphic(ColorRole role, Theme theme);
bool legible_as_body_text(ColorRole role, Theme theme);

const char* name_of(ColorRole role);
const char* name_of(Theme theme);

}  // namespace attadipa::ui
