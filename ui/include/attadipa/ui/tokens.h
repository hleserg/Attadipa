#pragma once

#include <cstdint>

#include "attadipa/ui/metrics.h"

// The code half of DESIGN_SYSTEM.md (T-009, final §54).
//
// Every value here is **proposed**. None has been shown on a panel, and final
// §55 forbids preserving a concept-board value that fails on the real display,
// so these are the starting point for that measurement rather than its result.
// What is *not* provisional is the shape: a screen names a role, and the value
// behind the name is resolved in one place.

namespace attadipa::ui {

// ---------------------------------------------------------------------------
// Spacing

enum class Space : std::uint8_t { Xs, Sm, Md, Lg, Xl, Xxl };

constexpr Dp dp_of(Space s)
{
    switch (s) {
        case Space::Xs:  return Dp{4};
        case Space::Sm:  return Dp{8};
        case Space::Md:  return Dp{12};
        case Space::Lg:  return Dp{16};
        case Space::Xl:  return Dp{24};
        case Space::Xxl: return Dp{32};
    }
    return Dp{0};
}

// ---------------------------------------------------------------------------
// Radius
//
// `Pill` is the odd one out and it is not a length. DESIGN_SYSTEM writes it as
// 999, which is the CSS idiom for "round the ends completely" — but 999 dp
// resolved at 220 dpi is 1374 px, larger than either panel, so treating it as a
// measurement produces nonsense rather than a pill. It is a *rule* — half the
// shorter side of whatever is being drawn — and it says so in the type system
// instead of hiding in a magic number.

enum class Radius : std::uint8_t { Sm, Md, Lg, Pill };

constexpr bool is_pill(Radius r)
{
    return r == Radius::Pill;
}

// Meaningless for `Pill`; ask `is_pill` first. Returns 0 there rather than a
// number a caller could accidentally use.
constexpr Dp dp_of(Radius r)
{
    switch (r) {
        case Radius::Sm:   return Dp{6};
        case Radius::Md:   return Dp{12};
        case Radius::Lg:   return Dp{20};
        case Radius::Pill: return Dp{0};
    }
    return Dp{0};
}

// The pill rule, in the one place it belongs.
constexpr std::int32_t radius_px(Radius r, const Metrics& m, std::int32_t shorter_side_px)
{
    return is_pill(r) ? shorter_side_px / 2 : m.px(dp_of(r));
}

// ---------------------------------------------------------------------------
// Motion
//
// Milliseconds, and deliberately not scaled by density: a transition is a
// duration, and a denser panel does not make time pass differently.
//
// `Instant` exists so that "reduce motion" and low-power modes have somewhere to
// go without an `if` in every animation — DESIGN_SYSTEM §5.

enum class Motion : std::uint8_t { Instant, Fast, Base, Slow };

constexpr std::uint16_t milliseconds_of(Motion m)
{
    switch (m) {
        case Motion::Instant: return 0;
        case Motion::Fast:    return 120;
        case Motion::Base:    return 200;
        case Motion::Slow:    return 320;
    }
    return 0;
}

enum class Easing : std::uint8_t { Standard, Enter, Exit };

// ---------------------------------------------------------------------------
// Sizes

enum class IconSize : std::uint8_t { Sm, Md, Lg, Xl };

constexpr Dp dp_of(IconSize s)
{
    switch (s) {
        case IconSize::Sm: return Dp{16};
        case IconSize::Md: return Dp{20};
        case IconSize::Lg: return Dp{24};
        case IconSize::Xl: return Dp{32};
    }
    return Dp{0};
}

enum class ImageSize : std::uint8_t { Inline, Spot, Hero, HeroLarge };

constexpr Dp dp_of(ImageSize s)
{
    switch (s) {
        case ImageSize::Inline:    return Dp{32};
        case ImageSize::Spot:      return Dp{64};
        case ImageSize::Hero:      return Dp{120};
        case ImageSize::HeroLarge: return Dp{200};
    }
    return Dp{0};
}

// The minimum a finger can be asked to hit. Physical, which is the whole reason
// these are Dp: 44 dp is about 7 mm on any panel, and 44 *pixels* is 3.5 mm on
// the Waveshare and 5.1 mm on the T-Watch — both under the guidance, on
// different sides of it, for the same source line.
enum class TouchTarget : std::uint8_t { Adult, ChildMode };

constexpr Dp dp_of(TouchTarget t)
{
    switch (t) {
        case TouchTarget::Adult:     return Dp{44};
        case TouchTarget::ChildMode: return Dp{56};
    }
    return Dp{0};
}

// Realised as a border and a tint, never a blurred shadow — a blur costs fill
// rate on both panels and buys nothing at these sizes (DESIGN_SYSTEM §5).
enum class Elevation : std::uint8_t { Flat, Raised, Overlay };

// ---------------------------------------------------------------------------
// Typography
//
// Roles only. No font is pinned and no size is given, and that is not an
// omission: final §51 requires licence, Cyrillic coverage, legibility at real
// pixel size and generated flash size to be checked before either candidate is
// adopted, and none of the four has been. A size written here now would be a
// guess wearing a token's clothes.

enum class TypeRole : std::uint8_t { Display, Title, Body, Label, Caption, MonoDiag };

// ---------------------------------------------------------------------------
// Feedback
//
// Semantic, not effects (final §48). An application chooses `Success`; what the
// motor does about it is a platform decision, and it differs — the T-Watch has a
// DRV2605L driver IC and the Waveshare has a bare motor on a GPIO through a
// transistor. The same token must feel as similar as the hardware permits and
// must not fail on the weaker one.
//
// `HardwareCoordinator` may delay a non-critical haptic to protect a sensitive
// measurement. `Sos` is never delayed.

enum class Haptic : std::uint8_t { Tap, Success, Warning, Message, Navigation, Error, Sos };

enum class SoundCategory : std::uint8_t { System, Notifications, Mesh, Alarms, Navigation };

// ---------------------------------------------------------------------------
// Names, for diagnostics and for the test that proves every token has one.

const char* name_of(Space s);
const char* name_of(Radius r);
const char* name_of(Motion m);
const char* name_of(Easing e);
const char* name_of(IconSize s);
const char* name_of(ImageSize s);
const char* name_of(TouchTarget t);
const char* name_of(Elevation e);
const char* name_of(TypeRole r);
const char* name_of(Haptic h);
const char* name_of(SoundCategory c);

}  // namespace attadipa::ui
