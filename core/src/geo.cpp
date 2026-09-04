#include "attadipa/core/geo.h"

#include <cmath>

namespace attadipa::core {
namespace {

// Integer square root by Newton's method. No <cmath>, no float, and exact for
// every input: the loop converges downward and the final guard corrects the
// one-off that Newton leaves on perfect squares.
std::uint64_t isqrt(std::uint64_t value)
{
    if (value == 0) {
        return 0;
    }
    std::uint64_t guess = value;
    std::uint64_t next  = (guess + 1) / 2;
    while (next < guess) {
        guess = next;
        next  = (guess + value / guess) / 2;
    }
    return guess;
}

std::int64_t lat_e7_to_mm(std::int64_t delta_e7)
{
    return delta_e7 * kMillimetresPerLatE7Num / kMillimetresPerLatE7Den;
}

constexpr std::int64_t kE7PerDegree = 10000000;

// The cosine factor is carried as an integer scaled by this, 2^14 finer than
// the table it is read from. Those fourteen bits are what the interpolation
// below spends, and it has to have them: at the table's own 1024 the
// interpolated factor inside the last degree rounds to zero, which turns every
// polar longitude difference into no distance at all — the same defect as the
// one being fixed, pointed the other way, and the dangerous direction for a
// caller whose test is `distance > threshold`. Not finer than 2^24, because the
// product in lon_e7_to_mm() is then 2.0e10 * 2^24 ≈ 3.4e17, and that factor of
// 27 below the top of a signed 64-bit integer is the margin worth keeping.
constexpr std::int64_t kCosScale = 1 << 24;

// cos(latitude) as an integer scaled by kCosScale, read off the whole-degree
// table and slid between two entries by the fractional part of the latitude.
//
// The interpolation is not a refinement, it is the correctness of this
// function. Indexing the table by the truncated degree is a step function, and
// a step function is worst exactly where the value it stands in for is
// collapsing: across [89°, 90°) the real factor falls from 0.01745 to zero
// while the truncating version held 0.01745 the whole way. That read a 194 m
// longitude difference at 89.9°N as 1.96 km — ten times over, a thousand times
// over by 89.999° — which is a valid high-latitude fix teleporting, in the eyes
// of the jump detector this file exists to serve.
//
// Error envelope, measured against a double-precision reference and re-measured
// by tests/test_position.cpp on every run rather than trusted from a comment:
//
//   * absolute, in the cosine factor: below 4.9e-4 at every latitude, worst
//     near 66°. That is the rounding of the table entries themselves, which the
//     interpolation neither helps nor hurts;
//   * relative, in the distance: below 0.9% from the equator to 89.999°, which
//     is everywhere outside the last 111 metres to the pole, and it first
//     exceeds 1% at 89.99944°, 63 m out. Flat rather than growing, because the
//     entry the polar interval interpolates from — 18 against a true 17.871 —
//     is itself 0.72% high, and interpolation towards an exact zero carries
//     that ratio down rather than amplifying it;
//   * inside those last 111 metres the 2^-24 quantum takes over and the
//     relative error is eventually total: in the final 20 cm a whole degree of
//     longitude is a third of a millimetre and this returns zero. What stays
//     bounded there is the absolute error — under 20 mm across the band, on a
//     distance that is itself under two metres.
//
// 0.9% is smaller than the equirectangular approximation's own error at the
// same latitude, so a finer table would buy nothing that survives the method it
// feeds.
std::int64_t cos_scaled(std::int32_t lat_e7)
{
    std::int64_t magnitude_e7 = lat_e7;
    if (magnitude_e7 < 0) {
        magnitude_e7 = -magnitude_e7;  // widened first, so INT32_MIN cannot overflow
    }
    if (magnitude_e7 > kLatitudeMaxE7) {
        magnitude_e7 = kLatitudeMaxE7;  // out-of-range input is caught by the caller; be total anyway
    }

    const std::int64_t degrees = magnitude_e7 / kE7PerDegree;  // 0..90
    const std::int64_t frac_e7 = magnitude_e7 % kE7PerDegree;  // 0..9999999

    const std::int64_t lower = kCosTable1024[degrees];
    const std::int64_t upper = degrees < 90 ? kCosTable1024[degrees + 1] : 0;

    // A weighted mean of the two endpoints, and written as a weighted mean on
    // purpose: the algebraically identical `lower + (upper - lower) * frac`
    // subtracts two nearly equal large numbers in the last degree — 294912
    // minus 294882 at 89.9999° — and loses three digits of a value that only
    // has four, taking the relative error from 0.7% to 2.4% in exactly the
    // region this change exists to fix. There is no such subtraction here.
    //
    // Rounded to nearest rather than truncated, for one addition: near the pole
    // the factor is a single-digit number of kCosScale units, and there a whole
    // unit of truncation is a third of the answer.
    //
    // Both products are at most 1024 * 1e7, so the sum is at most 2.05e10 and
    // the 2^14 lift reaches 3.4e14, far inside the type. Both are non-negative,
    // so the factor is too, and a negative scale can never flip the sign of a
    // distance component.
    const std::int64_t weighted = lower * (kE7PerDegree - frac_e7) + upper * frac_e7;
    return (weighted * (kCosScale / 1024) + kE7PerDegree / 2) / kE7PerDegree;
}

// Longitude degrees shrink towards the poles. The scale factor is cos(latitude),
// taken at the mean of the two positions — which is the equirectangular
// approximation, and it is why this header refuses to be used over long
// baselines.
std::int64_t lon_e7_to_mm(std::int64_t delta_e7, std::int32_t mean_lat_e7)
{
    return (lat_e7_to_mm(delta_e7) * cos_scaled(mean_lat_e7)) / kCosScale;
}

std::int64_t clamp_component(std::int64_t mm)
{
    const std::int64_t limit = static_cast<std::int64_t>(kDistanceSaturated);
    if (mm > limit) {
        return limit;
    }
    if (mm < -limit) {
        return -limit;
    }
    return mm;
}

}  // namespace

std::uint32_t distance_mm(Position a, Position b)
{
    // A coordinate that is not on the globe has no distance to anywhere. The
    // saturated value is the honest answer and it fails every "is it near"
    // test, which is what a caller holding hostile input needs.
    if (!in_range(a) || !in_range(b)) {
        return kDistanceSaturated;
    }

    const std::int64_t dlat_e7 = static_cast<std::int64_t>(a.latitude_e7) - b.latitude_e7;
    std::int64_t       dlon_e7 = static_cast<std::int64_t>(a.longitude_e7) - b.longitude_e7;

    // The antimeridian. Going the short way round is not an optimisation here,
    // it is correctness: without this, two points either side of 180° read as
    // half a world apart, and a jump detector would fire every time somebody
    // crossed it. The reuse ledger records a real crash from mishandled
    // coordinates arriving over the air; this is the same family of bug, and it
    // is an explicit test case rather than a hope.
    const std::int64_t full_turn = 2LL * kLongitudeMaxE7;
    if (dlon_e7 > kLongitudeMaxE7) {
        dlon_e7 -= full_turn;
    } else if (dlon_e7 < -kLongitudeMaxE7) {
        dlon_e7 += full_turn;
    }

    const std::int32_t mean_lat_e7 =
        static_cast<std::int32_t>((static_cast<std::int64_t>(a.latitude_e7) + b.latitude_e7) / 2);

    const std::int64_t dy = clamp_component(lat_e7_to_mm(dlat_e7));
    const std::int64_t dx = clamp_component(lon_e7_to_mm(dlon_e7, mean_lat_e7));

    // Both components are now at most 1e9, so each square is at most 1e18 and
    // the sum at most 2e18 — inside the range of a signed 64-bit integer, which
    // is what the clamp above exists to guarantee.
    const std::uint64_t squared =
        static_cast<std::uint64_t>(dy * dy) + static_cast<std::uint64_t>(dx * dx);
    const std::uint64_t result = isqrt(squared);

    return result >= kDistanceSaturated ? kDistanceSaturated : static_cast<std::uint32_t>(result);
}

bool initial_bearing(Position a, Position b, std::uint16_t& out_centideg)
{
    if (!in_range(a) || !in_range(b)) {
        return false;
    }
    if (a.latitude_e7 == b.latitude_e7 && a.longitude_e7 == b.longitude_e7) {
        return false;
    }
    // STANDING ON A POLE THERE IS NO NORTH TO MEASURE FROM, so there is no
    // bearing to state either — every direction is the same one. This also
    // takes the case the comparison above cannot: two different longitudes at
    // the same pole are two coordinates and one physical point. Only the
    // *origin* is refused; the bearing **to** a pole is due north or due south
    // and perfectly ordinary.
    //
    // It is a test on the integer coordinate rather than on the arithmetic,
    // because the arithmetic does not degenerate cleanly: cos(pi/2) in double
    // is 6.1e-17 and not 0, so the products below stay non-zero and `atan2`
    // goes on answering with a direction assembled from rounding error.
    if (a.latitude_e7 == kLatitudeMaxE7 || a.latitude_e7 == -kLatitudeMaxE7) {
        return false;
    }

    constexpr double kPi        = 3.14159265358979323846;
    constexpr double kRadPerE7  = kPi / 180.0 / 10000000.0;
    constexpr double kDegPerRad = 180.0 / kPi;

    // The one subtraction that matters, done in int64 where it is exact. Two
    // points a metre apart differ by about 9 in this unit; taken as a
    // difference of two doubles already scaled to radians it would be a
    // difference of two numbers near 1.0, and most of the mantissa would go
    // into agreeing about the part that cancels.
    const double dlon =
        static_cast<double>(static_cast<std::int64_t>(b.longitude_e7) -
                            static_cast<std::int64_t>(a.longitude_e7)) * kRadPerE7;

    const double lat_a = static_cast<double>(a.latitude_e7) * kRadPerE7;
    const double lat_b = static_cast<double>(b.latitude_e7) * kRadPerE7;

    const double y = std::sin(dlon) * std::cos(lat_b);
    const double x = std::cos(lat_a) * std::sin(lat_b) -
                     std::sin(lat_a) * std::cos(lat_b) * std::cos(dlon);

    double degrees = std::atan2(y, x) * kDegPerRad;
    if (degrees < 0.0) {
        degrees += 360.0;
    }

    // Rounding is what puts a value on the 0/360 seam: 359.998° rounds to
    // 36000 centidegrees, which is not in the range this function promises.
    // Folded rather than clamped, because the answer there is north, not
    // "just short of north".
    long centi = std::lround(degrees * 100.0);
    if (centi >= 36000) {
        centi -= 36000;
    }
    if (centi < 0) {
        centi += 36000;
    }
    out_centideg = static_cast<std::uint16_t>(centi);
    return true;
}

std::uint32_t great_circle_mm(Position a, Position b)
{
    // The same answer `distance_mm()` gives a coordinate that is not on the
    // globe, and for the same reason: saturated fails every "is it near" test,
    // and this function's caller is holding bytes off a radio.
    if (!in_range(a) || !in_range(b)) {
        return kDistanceSaturated;
    }

    constexpr double kPi       = 3.14159265358979323846;
    constexpr double kRadPerE7 = kPi / 180.0 / 10000000.0;

    // WGS-84's semi-major axis, which is the sphere the rest of this file
    // already implies: kMillimetresPerLatE7Num of 11 132 is 111 320 m per
    // degree, and that is a radius of 6 378 137 m to six figures. Taking the
    // mean radius 6 371 009 m instead would put a fixed 0.11% disagreement
    // about *which* sphere between this function and its neighbour, on top of
    // the method difference the two are here to have.
    constexpr double kSphereRadiusMm = 6378137000.0;

    // Formed in int64 where they are exact, then scaled — the same trade as in
    // `initial_bearing()` above, and for the same reason: two points a metre
    // apart differ by about 9 in this unit, and taking that difference between
    // two doubles already near 1.0 radian would spend most of the mantissa
    // agreeing about the part that cancels.
    const double dlat =
        static_cast<double>(static_cast<std::int64_t>(b.latitude_e7) -
                            static_cast<std::int64_t>(a.latitude_e7)) * kRadPerE7;
    const double dlon =
        static_cast<double>(static_cast<std::int64_t>(b.longitude_e7) -
                            static_cast<std::int64_t>(a.longitude_e7)) * kRadPerE7;

    const double lat_a = static_cast<double>(a.latitude_e7) * kRadPerE7;
    const double lat_b = static_cast<double>(b.latitude_e7) * kRadPerE7;

    // Haversine. No antimeridian branch, unlike `distance_mm()`: the half-angle
    // sines are squared, so a longitude difference of d and one of d - 360°
    // give the same term, and a pair straddling 180° comes out right with no
    // help. It is the one place this method is simpler than the integer one.
    const double sin_half_lat = std::sin(dlat / 2.0);
    const double sin_half_lon = std::sin(dlon / 2.0);
    double       h            = sin_half_lat * sin_half_lat +
               std::cos(lat_a) * std::cos(lat_b) * sin_half_lon * sin_half_lon;

    // Algebraically h is in [0, 1]; in doubles a near-antipodal pair can round
    // a hair past 1, and `asin` of that is NaN. A NaN cast to an unsigned
    // integer is undefined behaviour, not "very far", so it is clamped here
    // rather than caught below.
    if (h < 0.0) {
        h = 0.0;
    } else if (h > 1.0) {
        h = 1.0;
    }

    const double millimetres = 2.0 * kSphereRadiusMm * std::asin(std::sqrt(h));

    // Written as a refuted `<` so that a NaN which somehow survived the clamp
    // saturates rather than falling through to the cast.
    if (!(millimetres < static_cast<double>(kDistanceSaturated))) {
        return kDistanceSaturated;
    }
    return static_cast<std::uint32_t>(millimetres + 0.5);
}

}  // namespace attadipa::core
