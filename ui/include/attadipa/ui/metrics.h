#pragma once

#include <cstdint>

namespace attadipa::ui {

// Density-independent pixels.
//
// A distinct type rather than an int, because the whole point of this file is
// that a spacing value is *not* a pixel count and the two must not be assignable
// to each other by accident. 8 px on the T-Watch's 1.54-inch 240x240 panel and
// 8 px on a 2.06-inch 410x502 one are 0.92 mm and 0.65 mm — a 1.4x difference
// in a quantity that fingers measure physically. Both figures are what this
// build computes: the T-Watch's 1.54 inches are MEASURED (D15, 2026-08-28), not
// the 1.3-inch placeholder that used to make this 0.78 mm.
struct Dp {
    std::int16_t value = 0;

    constexpr bool operator==(Dp other) const { return value == other.value; }
    constexpr bool operator!=(Dp other) const { return value != other.value; }
};

// The density every Dp is expressed against: 160 dpi, so one Dp is one pixel on
// a 160-dpi panel and 1/160 inch — 0.159 mm — everywhere.
//
// 160 is not derived from anything about these two boards. It is the reference
// the touch-target literature and the platform conventions are already written
// in, which is what makes "44 dp" mean the ~7 mm that guidance intends rather
// than a number this project invented. Changing it would silently rescale every
// token in the system.
inline constexpr std::uint16_t kReferenceDpi = 160;

// What a panel needs to say for a Dp to become a pixel.
//
// Deliberately just a number, not a DisplayInfo and not a board. The ui library
// does not link platform and must not: an application asks for `space.md`, and
// the composition root is the only thing that knows which panel answered. That
// is the same rule the capability registry enforces, applied to pixels.
class Metrics
{
public:
    // The unscaled identity: one Dp is one pixel.
    //
    // For the case where no panel has said anything yet — an early boot screen,
    // a host test, a tool. It is honest rather than convenient: a layout drawn
    // through it is *unscaled*, not correct-by-default, and `scaled()` says so
    // to anybody who asks.
    static constexpr Metrics unscaled() { return Metrics{kReferenceDpi}; }

    // A panel's own density. Zero dpi is refused rather than accepted and
    // quietly treated as 160: a DisplayInfo that could not compute a dpi returns
    // 0, and letting that through would make an unknown panel look like a
    // reference one.
    static constexpr Metrics for_dpi(std::uint16_t dpi)
    {
        return Metrics{dpi == 0 ? kReferenceDpi : dpi};
    }

    constexpr std::uint16_t dpi() const { return dpi_; }
    constexpr bool          scaled() const { return dpi_ != kReferenceDpi; }

    // Dp to pixels, rounded to nearest, in integer arithmetic.
    //
    // Never returns 0 for a non-zero Dp. A 4 dp gap that rounds to nothing is
    // not a small gap, it is a collapsed layout — two elements touching — and it
    // would appear only on the lowest-density panel somebody happened to test
    // last. One pixel is the smallest thing a gap can be and still be one.
    constexpr std::int32_t px(Dp dp) const
    {
        if (dp.value == 0) {
            return 0;
        }
        const std::int32_t scaled_up =
            static_cast<std::int32_t>(dp.value) * static_cast<std::int32_t>(dpi_);
        const std::int32_t half     = static_cast<std::int32_t>(kReferenceDpi) / 2;
        const std::int32_t rounded  = (scaled_up + (dp.value > 0 ? half : -half)) /
                                     static_cast<std::int32_t>(kReferenceDpi);
        if (rounded == 0) {
            return dp.value > 0 ? 1 : -1;
        }
        return rounded;
    }

    // The physical size of a Dp span, in micrometres.
    //
    // Here so that a test can assert the thing the tokens are actually for —
    // that the same token comes out about the same physical size on both panels
    // — rather than asserting the pixel counts, which differ on purpose and
    // prove nothing on their own.
    constexpr std::int32_t micrometres(Dp dp) const
    {
        // 1 inch = 25400 um. px / dpi * 25400, done in the order that keeps the
        // intermediate inside int32 for every token in this system.
        return px(dp) * 25400 / static_cast<std::int32_t>(dpi_);
    }

private:
    explicit constexpr Metrics(std::uint16_t dpi) : dpi_(dpi) {}

    std::uint16_t dpi_ = kReferenceDpi;
};

}  // namespace attadipa::ui
