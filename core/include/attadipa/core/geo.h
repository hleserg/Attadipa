#pragma once

#include <cstdint>

#include "attadipa/core/position.h"

// Just enough geometry to ask "how far did that move?" — and no more.
//
// This is not a geodesy library and must not become one. It exists for one
// consumer, the trust engine's jump detector (docs/adr/0011-gnss-integrity.md
// §6), which compares two positions taken seconds apart and asks whether the
// distance between them is physically possible. At that scale an
// equirectangular approximation is correct to well within the error of the fixes
// being compared, and it costs no floating point, no trigonometry at runtime and
// no library.
//
// What it is NOT good for, written here because the temptation is real: route
// distances, anything crossing a pole, anything spanning more than a few
// kilometres, or a bearing. A real geodesic belongs to whoever needs one, and
// the reuse ledger already names GeographicLib (MIT) with a reference data set.
//
// Integers throughout, and saturating rather than wrapping — a distance that
// overflows must come back as "very far", never as "very near", because the
// caller's test is `distance > threshold` and a wrapped value silently passes.

namespace attadipa::core {

// cos(latitude), scaled by 1024, one entry per whole degree. Generated, then
// pasted, because a table of 91 constants in the binary is cheaper in both
// flash and cycles than linking libm into a path that runs on every fix.
//
// One entry per degree is the resolution of the *table*, not of the answer.
// `distance_mm()` interpolates between two entries by the fractional part of
// the latitude; indexing by the truncated degree is a step function, and near
// the pole a step is the whole value — see the error envelope in geo.cpp.
inline constexpr std::uint16_t kCosTable1024[91] = {
    1024, 1024, 1023, 1023, 1022, 1020, 1018, 1016, 1014, 1011,   // 0
    1008, 1005, 1002,  998,  994,  989,  984,  979,  974,  968,   // 10
     962,  956,  949,  943,  935,  928,  920,  912,  904,  896,   // 20
     887,  878,  868,  859,  849,  839,  828,  818,  807,  796,   // 30
     784,  773,  761,  749,  737,  724,  711,  698,  685,  672,   // 40
     658,  644,  630,  616,  602,  587,  573,  558,  543,  527,   // 50
     512,  496,  481,  465,  449,  433,  416,  400,  384,  367,   // 60
     350,  333,  316,  299,  282,  265,  248,  230,  213,  195,   // 70
     178,  160,  143,  125,  107,   89,   71,   54,   36,   18,   // 80
       0,                                                          // 90
};

// The distance one unit of the 1e-7-degree grid covers along a meridian.
// 1° of latitude is about 111 320 m on the WGS-84 mean, so 1e-7° is 11.132 mm.
// Kept as a numerator over 1000 so the whole calculation stays in integers.
inline constexpr std::int64_t kMillimetresPerLatE7Num = 11132;
inline constexpr std::int64_t kMillimetresPerLatE7Den = 1000;

// Anything beyond this is reported as exactly this. A thousand kilometres is
// several orders of magnitude past every threshold this code is asked about, and
// clamping here keeps the squares below the range of a signed 64-bit integer.
inline constexpr std::uint32_t kDistanceSaturated = 1000000000U;  // 1000 km in mm

// Straight-line distance between two positions, in millimetres.
//
// Returns kDistanceSaturated for anything far away or out of range rather than
// failing: the callers all ask "is this further than X" for a small X, and for
// them "further than you can measure" and "1000 km" are the same answer.
//
// Accuracy, so a caller choosing a threshold has a number rather than a hope:
// within **0.9%** of a spherical reference at every latitude from the equator
// to 89.999°, which is everywhere outside the last 111 metres to the pole.
// Closer in than that the fixed-point cosine runs out of bits and the relative
// error grows without limit — but a whole degree of longitude is under two
// metres there and the absolute error stays under 20 mm. Both bounds are
// measured by tests/test_position.cpp on every run rather than asserted here.
//
// That envelope is the arithmetic. It is not an estimate of how wrong the
// *method* is: the equirectangular approximation and the single 111 320 m/deg
// constant are worth a few tenths of a percent of their own, and neither is
// improved by anything in here.
std::uint32_t distance_mm(Position a, Position b);

}  // namespace attadipa::core
