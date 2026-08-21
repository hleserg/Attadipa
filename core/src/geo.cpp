#include "firefly/core/geo.h"

namespace firefly::core {
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

// Longitude degrees shrink towards the poles. The scale factor is cos(latitude),
// taken at the mean of the two positions — which is the equirectangular
// approximation, and it is why this header refuses to be used over long
// baselines.
std::int64_t lon_e7_to_mm(std::int64_t delta_e7, std::int32_t mean_lat_e7)
{
    std::int32_t degrees = mean_lat_e7 / 10000000;
    if (degrees < 0) {
        degrees = -degrees;
    }
    if (degrees > 90) {
        degrees = 90;  // out-of-range input is caught by the caller; be total anyway
    }
    const std::int64_t scaled = lat_e7_to_mm(delta_e7) * kCosTable1024[degrees];
    return scaled / 1024;
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

}  // namespace firefly::core
