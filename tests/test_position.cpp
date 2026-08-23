#include <cstdio>
#include <cstdlib>

#include "attadipa/core/geo.h"
#include "attadipa/core/position.h"

// Host tests for the position observation, the validity classifier and the
// integer distance function.
//
// ADR-0011 §2 is the shape being defended here: availability, health, fix,
// freshness, accuracy, integrity, interference, spoofing and trust are separate
// axes and none of them may be collapsed into a single `quality`. The tests
// below are mostly about the boundaries between them — a fix that is fresh and
// wrong, a fix that is excellent and old, a receiver that said nothing at all.
//
// The distance function is integer arithmetic on a cosine table, and its errors
// are therefore bounded and knowable rather than "floating point, probably
// fine". The tolerances below are stated as percentages of a hand-computed
// answer, and where the method itself breaks down the test says so instead of
// widening the tolerance until it passes.

using namespace attadipa::core;

// Every coordinate in this file is synthetic and deliberately obviously
// nowhere — half a degree north, one degree east, in the Gulf of Guinea, the
// same place the replay traces use. No real location belonging to anybody
// appears in this repository, and "arbitrary" is not a defence: a number that
// resolves to a city where somebody lives is exactly what must not be
// committed to a public repository, whatever the intention behind picking it.

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void check_validity(PositionValidity actual, PositionValidity expected, int line)
{
    if (actual != expected) {
        std::fprintf(stderr, "FAIL line %d: validity is %s, expected %s\n", line,
                     to_string(actual), to_string(expected));
        ++failures;
    }
}

#define CHECK_VALID(actual, expected) check_validity((actual), (expected), __LINE__)

// Within `percent` of the expected value. Stated rather than assumed: the
// equirectangular approximation with a 1-degree cosine table has a real error
// budget, and a test that hides it behind a huge tolerance is not measuring
// anything.
void check_near(std::uint32_t actual, std::uint64_t expected, unsigned percent, const char* what,
                int line)
{
    const std::uint64_t slack = (expected * percent) / 100u + 1000u;
    const std::uint64_t lo    = expected > slack ? expected - slack : 0u;
    const std::uint64_t hi    = expected + slack;
    if (actual < lo || actual > hi) {
        std::fprintf(stderr, "FAIL line %d: %s is %u mm, expected %llu ±%u%%\n", line, what, actual,
                     static_cast<unsigned long long>(expected), percent);
        ++failures;
    }
}

#define CHECK_NEAR(actual, expected, percent) \
    check_near((actual), (expected), (percent), #actual, __LINE__)

// CHECK_NEAR's one metre of unconditional slack is right for a continental
// distance and useless at the pole, where the entire answer is 194 mm and a
// metre of slack would pass an implementation returning zero. These two carry
// no floor: one states the tolerance as a fraction, the other in millimetres,
// and which one a test wants depends on whether the error being bounded scales
// with the answer.
void check_relative(std::uint32_t actual, double expected, double percent, const char* what,
                    int line)
{
    const double deviation = (static_cast<double>(actual) - expected) / expected * 100.0;
    const double magnitude = deviation < 0.0 ? -deviation : deviation;
    if (!(magnitude <= percent)) {
        std::fprintf(stderr, "FAIL line %d: %s is %u mm, expected %.1f mm — %.3f%% out, %.3f%% allowed\n",
                     line, what, actual, expected, magnitude, percent);
        ++failures;
    }
}

#define CHECK_RELATIVE(actual, expected, percent) \
    check_relative((actual), (expected), (percent), #actual, __LINE__)

void check_within(std::uint32_t actual, double expected, double tolerance_mm, const char* what,
                  int line)
{
    const double deviation = static_cast<double>(actual) - expected;
    const double magnitude = deviation < 0.0 ? -deviation : deviation;
    if (!(magnitude <= tolerance_mm)) {
        std::fprintf(stderr, "FAIL line %d: %s is %u mm, expected %.3f mm — %.3f mm out, %.3f allowed\n",
                     line, what, actual, expected, magnitude, tolerance_mm);
        ++failures;
    }
}

#define CHECK_WITHIN(actual, expected, tolerance_mm) \
    check_within((actual), (expected), (tolerance_mm), #actual, __LINE__)

// ---------------------------------------------------------------------------
// An independent reference for the distance tests.
//
// Computed by a different method than the thing it checks: haversine, in double
// precision, on a sphere. No cosine table, no whole-degree index, no
// mean-latitude reduction, and no explicit antimeridian case — `sin(dlon / 2)`
// is periodic, so a wrap that the implementation has to handle deliberately
// falls out of the reference for free. A reference assembled from the same
// parts as the implementation agrees with it about its own mistakes; this one
// cannot.
//
// The radius is WGS-84's semi-major axis, because that is the sphere the
// implementation already implies: kMillimetresPerLatE7Num of 11 132 is
// 111 320 m per degree, which is 6 378 137 m of radius to six figures. Choosing
// a different sphere — the mean radius 6 371 009 m, say — would fold a fixed
// 0.11% disagreement about *which* sphere into every tolerance below and hide
// the arithmetic these tests exist to measure. That 0.11% is the method's, it
// predates this file, and the one test that cares about it says so.
constexpr double kPi                 = 3.14159265358979323846;
constexpr double kReferenceRadiusMm  = 6378137000.0;

double radians(double degrees) { return degrees * kPi / 180.0; }

double reference_distance_mm(Position a, Position b)
{
    const double lat1 = radians(a.latitude_e7 / 1e7);
    const double lat2 = radians(b.latitude_e7 / 1e7);
    const double dlat = lat2 - lat1;
    const double dlon = radians(b.longitude_e7 / 1e7 - a.longitude_e7 / 1e7);

    const double sin_half_lat = __builtin_sin(dlat / 2.0);
    const double sin_half_lon = __builtin_sin(dlon / 2.0);
    const double h            = sin_half_lat * sin_half_lat +
                     __builtin_cos(lat1) * __builtin_cos(lat2) * sin_half_lon * sin_half_lon;

    return 2.0 * kReferenceRadiusMm * __builtin_asin(__builtin_sqrt(h));
}

// A position at `lat_e7`, and the same one `dlon_e7` to the east.
Position at_lat(std::int32_t lat_e7) { return Position{lat_e7, 0}; }
Position east_of(std::int32_t lat_e7, std::int32_t dlon_e7) { return Position{lat_e7, dlon_e7}; }

MonotonicTime at(std::uint64_t ms) { return MonotonicTime{ms}; }

GnssObservation fix_at(std::uint64_t ms)
{
    GnssObservation o;
    o.observed_at            = at(ms);
    o.fix_type               = FixType::ThreeD;
    o.position               = Position{5000000, 10000000};
    o.horizontal_accuracy_mm = 4000;
    o.satellites_used        = 10;
    o.hdop_centi             = 110;
    return o;
}

// ---------------------------------------------------------------------------

// All four values are reachable, which sounds trivial and is the thing that
// stops being true when somebody adds a guard clause.
void test_all_four_validities_are_reachable()
{
    CHECK_VALID(classify(fix_at(0), at(0), ValidityPolicy{}), PositionValidity::Valid);

    GnssObservation none;
    CHECK_VALID(classify(none, at(0), ValidityPolicy{}), PositionValidity::NoFix);

    CHECK_VALID(classify(fix_at(0), at(30000), ValidityPolicy{}), PositionValidity::Stale);

    GnssObservation two_d = fix_at(0);
    two_d.fix_type        = FixType::TwoD;
    CHECK_VALID(classify(two_d, at(0), ValidityPolicy{}), PositionValidity::Degraded);
}

// The interesting case in the enum. A receiver with TimeOnly is working, has
// satellites and can set the clock — and has no place to report. That is not a
// bad position; it is not a position, and an interface that showed it as a
// degraded fix would draw a marker on the equator.
void test_time_only_is_not_a_bad_position()
{
    GnssObservation o = fix_at(0);
    o.fix_type        = FixType::TimeOnly;
    CHECK_VALID(classify(o, at(0), ValidityPolicy{}), PositionValidity::NoFix);

    // Even with a position field filled in, which a receiver may leave from the
    // previous epoch.
    CHECK(o.position.has_value());
    CHECK_VALID(classify(o, at(0), ValidityPolicy{}), PositionValidity::NoFix);
}

// Freshness outranks quality, and the ordering is the design. An excellent fix
// from ten minutes ago is not a slightly worse fix; the device may have moved
// arbitrarily far since, so it is a circle whose radius nobody measured.
void test_freshness_is_decided_before_quality()
{
    GnssObservation excellent    = fix_at(0);
    excellent.horizontal_accuracy_mm = 800;   // 0.8 m
    excellent.satellites_used        = 18;
    excellent.hdop_centi             = 60;

    CHECK_VALID(classify(excellent, at(0), ValidityPolicy{}), PositionValidity::Valid);
    CHECK_VALID(classify(excellent, at(29999), ValidityPolicy{}), PositionValidity::Valid);
    CHECK_VALID(classify(excellent, at(30000), ValidityPolicy{}), PositionValidity::Stale);
    CHECK_VALID(classify(excellent, at(600000), ValidityPolicy{}), PositionValidity::Stale);

    // A poor fix that is fresh is Degraded rather than Stale — the two are
    // different answers and the interface says different things about them.
    GnssObservation poor            = fix_at(0);
    poor.horizontal_accuracy_mm     = 90000;
    CHECK_VALID(classify(poor, at(1000), ValidityPolicy{}), PositionValidity::Degraded);
}

// "Did not say" is not "is fine". A receiver reporting no satellite count is
// one we know less about, and less is a caveat rather than a pass.
void test_silence_from_the_receiver_is_a_caveat()
{
    GnssObservation quiet = fix_at(0);
    quiet.satellites_used.reset();
    CHECK_VALID(classify(quiet, at(0), ValidityPolicy{}), PositionValidity::Degraded);

    // But an absent *accuracy* is not, on its own, enough to degrade a fix that
    // has satellites and a good HDOP — otherwise every receiver that does not
    // publish an accuracy estimate would be permanently degraded, which is a
    // statement about the protocol rather than about the position.
    GnssObservation no_accuracy = fix_at(0);
    no_accuracy.horizontal_accuracy_mm.reset();
    CHECK_VALID(classify(no_accuracy, at(0), ValidityPolicy{}), PositionValidity::Valid);
}

// Hostile or corrupt input lands in NoFix rather than being clamped into
// something plausible. A clamped coordinate is indistinguishable from a real
// one two lines later, which is how bad data becomes a map marker.
void test_a_coordinate_off_the_globe_is_not_a_position()
{
    GnssObservation o = fix_at(0);

    o.position = Position{kLatitudeMaxE7 + 1, 0};
    CHECK_VALID(classify(o, at(0), ValidityPolicy{}), PositionValidity::NoFix);

    o.position = Position{0, kLongitudeMaxE7 + 1};
    CHECK_VALID(classify(o, at(0), ValidityPolicy{}), PositionValidity::NoFix);

    o.position = Position{-kLatitudeMaxE7 - 1, 0};
    CHECK_VALID(classify(o, at(0), ValidityPolicy{}), PositionValidity::NoFix);

    // The poles and the antimeridian are legitimate places to stand.
    CHECK(in_range(Position{kLatitudeMaxE7, kLongitudeMaxE7}));
    CHECK(in_range(Position{-kLatitudeMaxE7, -kLongitudeMaxE7}));
    CHECK(in_range(Position{0, 0}));
}

// Dead reckoning is a caveat, always. ADR-0009 and ADR-0011 §6: neither board
// has a magnetometer and the T-Watch's BMA423 is an accelerometer, so a
// propagated position on this hardware is a guess wearing a fix's clothes.
void test_dead_reckoning_is_never_a_clean_fix()
{
    GnssObservation o = fix_at(0);
    o.fix_type        = FixType::DeadReckoning;
    o.satellites_used = 20;
    o.horizontal_accuracy_mm = 500;
    o.hdop_centi             = 50;
    CHECK_VALID(classify(o, at(0), ValidityPolicy{}), PositionValidity::Degraded);
}

// The policy is policy: a device recording a track and a device drawing a map
// want different answers from the same numbers, and that belongs in a struct
// the caller owns rather than in a constant inside the classifier.
void test_the_thresholds_belong_to_the_caller()
{
    GnssObservation o = fix_at(0);
    o.horizontal_accuracy_mm = 20000;   // 20 m

    ValidityPolicy lenient;
    CHECK_VALID(classify(o, at(0), lenient), PositionValidity::Valid);

    ValidityPolicy strict;
    strict.degraded_accuracy_mm = 10000;
    CHECK_VALID(classify(o, at(0), strict), PositionValidity::Degraded);

    ValidityPolicy patient;
    patient.stale_after = Millis{300000};
    CHECK_VALID(classify(o, at(60000), lenient), PositionValidity::Stale);
    CHECK_VALID(classify(o, at(60000), patient), PositionValidity::Valid);
}

// ADR-0011 §1: the observation keeps both the normalised value and what the
// receiver actually said. Everything optional starts absent, because a default
// of zero is a number, and a number is an answer.
void test_nothing_defaults_to_a_confident_zero()
{
    const GnssObservation o;
    CHECK(!o.position.has_value());
    CHECK(!o.altitude_msl_mm.has_value());
    CHECK(!o.horizontal_accuracy_mm.has_value());
    CHECK(!o.satellites_used.has_value());
    CHECK(!o.satellites_in_view.has_value());
    CHECK(!o.hdop_centi.has_value());
    CHECK(!o.speed_mm_s.has_value());
    CHECK(!o.protection_level.has_value());
    CHECK(!o.receiver_time.has_value());
    CHECK(!o.receiver_time_valid);
    CHECK(o.fix_type == FixType::Unknown);
    CHECK(o.source == PositionSource::Unknown);

    // OD-5 §2, and the single most important default in this header: a receiver
    // that has not been asked about jamming or spoofing reports Unknown, never
    // None. On the LS550G anti-spoofing is UNKNOWN rather than SUPPORTED, and a
    // default of None would turn an absent capability into a reassurance.
    CHECK(o.jamming == ReceiverIndication::Unknown);
    CHECK(o.spoofing == ReceiverIndication::Unknown);
}

// ---------------------------------------------------------------------------
// Distance.

// One degree of latitude is about 111.32 km anywhere on the globe, which makes
// it the one case that can be checked against a number rather than against
// another implementation.
void test_a_degree_of_latitude_is_the_same_everywhere()
{
    const std::uint64_t expected = 111320000ULL;  // 111.32 km in mm

    CHECK_NEAR(distance_mm(Position{0, 0}, Position{10000000, 0}), expected, 1);
    CHECK_NEAR(distance_mm(Position{450000000, 300000000}, Position{460000000, 300000000}),
               expected, 1);
    CHECK_NEAR(distance_mm(Position{-600000000, -300000000}, Position{-590000000, -300000000}),
               expected, 1);
}

// A degree of longitude shrinks with the cosine of the latitude, and getting
// that wrong is the classic bug: at 60° north it is half what it is at the
// equator, so a detector calibrated at the equator fires at twice the real
// distance in Scandinavia and never fires at all in the tropics.
void test_a_degree_of_longitude_shrinks_with_latitude()
{
    CHECK_NEAR(distance_mm(Position{0, 0}, Position{0, 10000000}), 111320000ULL, 2);
    CHECK_NEAR(distance_mm(Position{600000000, 0}, Position{600000000, 10000000}), 55660000ULL, 3);
    CHECK_NEAR(distance_mm(Position{-600000000, 0}, Position{-600000000, 10000000}), 55660000ULL,
               3);

    // Near the pole a degree of longitude has almost collapsed: cos 89° is
    // about 0.01745, so 111.32 km becomes about 1.94 km.
    CHECK_NEAR(distance_mm(Position{890000000, 0}, Position{890000000, 10000000}), 1943000ULL, 3);

    // 89° is where this test used to stop, and stopping there is what let the
    // defect below live: every latitude in [89°, 90°) returned this same
    // 1.94 km, and only the whole degrees were ever asked.
}

// The regression this file was reopened for.
//
// `distance_mm()` indexed the cosine table by the *truncated* degree of the
// mean latitude, so everything from 89.0° to 89.999999° was scaled by cos 89°.
// A degree of longitude at 89.9°N is about 194 m; the implementation returned
// 1 956 796 mm — ten times over — and by 89.999° it was a thousand times over,
// because inside that last degree the true scale falls to zero while a step
// function holds its last value.
//
// Ten times a distance is not a rounding error to a jump detector. ADR-0011 §6
// makes implied speed and motion disagreement evidence against the fix, so a
// stationary device at a high latitude, whose longitude wanders by the metre as
// any receiver's does, would have produced kilometre-scale movement, an
// implausible speed, and a position the trust engine degrades — for being at a
// high latitude, which is not a fault.
//
// The check is deliberately blunt as well as precise: kilometres and metres are
// far enough apart that the loose bound alone would have caught it, and a
// future change that reintroduces the truncation fails the loose bound whatever
// it does to the tolerances.
void test_a_degree_of_longitude_near_the_pole_is_metres_not_kilometres()
{
    const Position west = at_lat(899000000);              // 89.9°N
    const Position east = east_of(899000000, 10000000);   // one degree east

    CHECK(distance_mm(west, east) < 1000000U);            // under a kilometre, blunt
    CHECK_RELATIVE(distance_mm(west, east), reference_distance_mm(west, east), 1.0);

    // The southern hemisphere is the same physics and a different sign, which is
    // exactly the sort of thing an abs() in the wrong place gets wrong.
    const Position south_west = at_lat(-899000000);
    const Position south_east = east_of(-899000000, 10000000);
    CHECK(distance_mm(south_west, south_east) == distance_mm(west, east));

    // And it keeps collapsing rather than sticking. Each rung stands a tenth as
    // far from the pole as the one above it, so each answer must be about a
    // tenth of the one before — a shape the step function cannot produce at any
    // tolerance, because it returned the same number for all four.
    const std::int32_t ladder[] = {890000000, 899000000, 899900000, 899990000};
    std::uint64_t previous = 0;
    for (std::int32_t lat_e7 : ladder) {
        const Position a = at_lat(lat_e7);
        const Position b = east_of(lat_e7, 10000000);
        CHECK_RELATIVE(distance_mm(a, b), reference_distance_mm(a, b), 1.0);

        const std::uint64_t here = distance_mm(a, b);
        if (previous != 0) {
            CHECK(here * 9U < previous);   // shrank by at least nine
            CHECK(here * 11U > previous);  // but not by more than eleven
        }
        previous = here;
    }
}

// The matrix the review asked for, every entry against the independent
// reference rather than against a number this file also derives from a cosine
// table. 1° of longitude at each latitude, north and south.
void test_the_longitude_scale_matches_a_spherical_reference()
{
    const std::int32_t latitudes[] = {
        0,           // equator
        450000000,   // 45°
        800000000,   // 80°
        890000000,   // 89°
        895000000,   // 89.5° — inside the interval the old code could not see
        899000000,   // 89.9°
        899900000,   // 89.99°
        899990000,   // 89.999°
    };

    for (std::int32_t lat_e7 : latitudes) {
        for (std::int32_t sign : {1, -1}) {
            const Position a = at_lat(sign * lat_e7);
            const Position b = east_of(sign * lat_e7, 10000000);
            CHECK_RELATIVE(distance_mm(a, b), reference_distance_mm(a, b), 1.0);
            CHECK(distance_mm(a, b) == distance_mm(b, a));
        }
    }
}

// The error envelope as a measurement rather than a claim. Every documented
// bound in geo.h and geo.cpp is re-derived here on every run, so a change that
// quietly widens it fails rather than being noticed by whoever next reads a
// comment.
//
// 1% against a haversine on the sphere the implementation implies. The measured
// worst case is 0.76%, and it is not the interpolation: it is the rounding of
// kCosTable1024's own entries, which is why the bound is flat from the equator
// to 89.999° instead of growing towards the pole.
void test_the_error_envelope_holds_at_every_latitude()
{
    const std::int32_t dlon_e7 = 10000000;  // 1°, the widest baseline worth checking here

    // Two bands, two resolutions: coarse where the old code was merely
    // imprecise, a hundred times finer over the degree where it was wrong. The
    // last thousandth of a degree belongs to the test below, which asks a
    // different question about it.
    struct Band { std::int64_t from; std::int64_t to; std::int64_t step; };
    const Band bands[] = {
        {0, 890000000, 10000},          // 0° .. 89°, every 0.001°
        {890000000, 899990000, 100},    // 89° .. 89.999°, every 0.00001°
    };

    double worst          = 0.0;
    std::int64_t worst_at = 0;
    for (const Band& band : bands) {
        for (std::int64_t lat_e7 = band.from; lat_e7 <= band.to; lat_e7 += band.step) {
            const Position a = at_lat(static_cast<std::int32_t>(lat_e7));
            const Position b = east_of(static_cast<std::int32_t>(lat_e7), dlon_e7);
            const double reference = reference_distance_mm(a, b);
            const double got       = static_cast<double>(distance_mm(a, b));
            const double deviation = (got - reference) / reference * 100.0;
            const double magnitude = deviation < 0.0 ? -deviation : deviation;
            if (magnitude > worst) {
                worst    = magnitude;
                worst_at = lat_e7;
            }
        }
    }

    if (!(worst <= 1.0)) {
        std::fprintf(stderr,
                     "FAIL line %d: worst relative error %.4f%% at latitude %.5f, 1%% allowed\n",
                     __LINE__, worst, static_cast<double>(worst_at) / 1e7);
        ++failures;
    }

    // And the other side of it, which is not a bug guard but a documentation
    // guard. The envelope is written into geo.h and geo.cpp as a number a caller
    // may choose a threshold against, and the measured worst case is 0.76% —
    // all of it kCosTable1024's own rounding. If that drops below half a
    // percent, the table has been made finer and three comments now understate
    // what this function can do. That is a good change and a stale document, so
    // it should be noticed rather than passed over in silence.
    if (!(worst > 0.5)) {
        std::fprintf(stderr,
                     "FAIL line %d: worst relative error is now %.4f%%, better than the 0.76%% "
                     "recorded — the documented envelope in geo.h and geo.cpp needs updating\n",
                     __LINE__, worst);
        ++failures;
    }
}

// Where the arithmetic runs out, and what it does there.
//
// Past about 63 m from the pole the fixed-point cosine has single digits left
// and the *relative* error starts to grow — 100% of it in the final 20 cm,
// where a degree of longitude is a third of a millimetre and the answer is
// zero. Saying so is the point of this test. What has to stay bounded at that
// scale is the absolute error, and it does: across the whole last 111 m a full
// degree of longitude is under two metres and never more than 20 mm out.
//
// The failure being guarded against is the old one — a kilometre where there
// should be a metre — and not the last digit of a millimetre, which no receiver
// this firmware will ever see could supply anyway.
void test_the_final_metres_of_the_pole_degrade_in_millimetres()
{
    double worst = 0.0;
    for (std::int64_t lat_e7 = 899990000; lat_e7 <= 900000000; ++lat_e7) {
        const Position a = at_lat(static_cast<std::int32_t>(lat_e7));
        const Position b = east_of(static_cast<std::int32_t>(lat_e7), 10000000);
        const double reference = reference_distance_mm(a, b);
        const double got       = static_cast<double>(distance_mm(a, b));
        const double deviation = got > reference ? got - reference : reference - got;
        if (deviation > worst) {
            worst = deviation;
        }
        CHECK(distance_mm(a, b) <= 2000U);  // never more than two metres, whatever else
    }

    if (!(worst <= 20.0)) {
        std::fprintf(stderr, "FAIL line %d: worst absolute error in the last 111 m is %.3f mm, 20 allowed\n",
                     __LINE__, worst);
        ++failures;
    }

    // At the pole itself every longitude is the same place, and the answer is
    // zero rather than a leftover scale.
    CHECK(distance_mm(at_lat(900000000), east_of(900000000, 1800000000)) == 0U);
    CHECK(distance_mm(at_lat(-900000000), east_of(-900000000, -1800000000)) == 0U);
}

// The antimeridian again, this time at a latitude where the old code's scale
// error and the wrap would have compounded. Both are in the same expression and
// a fix to one is exactly the sort of change that breaks the other.
void test_the_antimeridian_still_is_not_a_wall_near_the_pole()
{
    const Position west{899000000, 1799995000};   // 89.9°N, 179.9995°E
    const Position east{899000000, -1799995000};  // 89.9°N, 179.9995°W

    // A thousandth of a degree of longitude, at a latitude where that is under
    // two tenths of a metre rather than the 111 m it is at the equator. Stated
    // in millimetres rather than as a percentage: the whole answer is 194 mm,
    // and a percentage of that is smaller than the unit it is returned in.
    CHECK_WITHIN(distance_mm(west, east), reference_distance_mm(west, east), 5.0);
    CHECK(distance_mm(west, east) < 1000U);
    CHECK(distance_mm(west, east) == distance_mm(east, west));

    // The same longitudes at the equator, to show the wrap itself is unchanged.
    CHECK_NEAR(distance_mm(Position{0, 1799995000}, Position{0, -1799995000}), 111000ULL, 1);
}

// The corners of the coordinate grid are answers, not crashes — and one step
// outside them is the saturated value, not a wrapped small one.
void test_the_grid_boundaries_are_answers()
{
    const Position north_east{kLatitudeMaxE7, kLongitudeMaxE7};
    const Position north_west{kLatitudeMaxE7, -kLongitudeMaxE7};
    const Position south_east{-kLatitudeMaxE7, kLongitudeMaxE7};

    CHECK(distance_mm(north_east, north_east) == 0U);
    CHECK(distance_mm(north_east, north_west) == 0U);  // both are the north pole
    CHECK(distance_mm(north_east, south_east) == kDistanceSaturated);
    CHECK(distance_mm(north_east, south_east) == distance_mm(south_east, north_east));

    // One unit outside the grid in each of the four directions. Saturated,
    // because a coordinate that is not on the globe has no distance to
    // anywhere — and saturated rather than wrapped, because the callers' test
    // is `distance > threshold` and a wrapped value passes it silently.
    const Position origin{0, 0};
    CHECK(distance_mm(origin, Position{kLatitudeMaxE7 + 1, 0}) == kDistanceSaturated);
    CHECK(distance_mm(origin, Position{-kLatitudeMaxE7 - 1, 0}) == kDistanceSaturated);
    CHECK(distance_mm(origin, Position{0, kLongitudeMaxE7 + 1}) == kDistanceSaturated);
    CHECK(distance_mm(origin, Position{0, -kLongitudeMaxE7 - 1}) == kDistanceSaturated);

    // The extremes of the type, which is what arrives from a hostile or
    // corrupted packet rather than from a receiver.
    const Position absurd{2147483647, -2147483647 - 1};
    CHECK(distance_mm(origin, absurd) == kDistanceSaturated);
    CHECK(distance_mm(absurd, origin) == kDistanceSaturated);
    CHECK(distance_mm(absurd, absurd) == kDistanceSaturated);
}

// The antimeridian. Two points a hundred metres apart across 180° are a hundred
// metres apart, and an implementation that subtracts the longitudes naively
// reports forty thousand kilometres — which, in a jump detector, is a
// permanently untrusted device for anybody who sails past Fiji.
void test_the_antimeridian_is_not_a_wall()
{
    const Position west{0, 1799995000};    // 179.9995°E
    const Position east{0, -1799995000};   // 179.9995°W

    // A thousandth of a degree total, or about 111 m.
    CHECK_NEAR(distance_mm(west, east), 111000ULL, 10);

    // And it is symmetric, which a wrapping bug usually is not.
    CHECK(distance_mm(west, east) == distance_mm(east, west));

    // The same on the other side of the world, where no wrap is involved, to
    // show the wrap did not change the scale.
    CHECK_NEAR(distance_mm(Position{0, 5000}, Position{0, -5000}), 111000ULL, 10);
}

void test_distance_is_zero_symmetric_and_bounded()
{
    const Position p{5000000, 10000000};
    CHECK(distance_mm(p, p) == 0);

    const Position q{6000000, 11000000};
    CHECK(distance_mm(p, q) == distance_mm(q, p));

    // Antipodes saturate rather than overflow. A number that wrapped would be a
    // small distance, which in a jump detector is the dangerous direction.
    const std::uint32_t far = distance_mm(Position{900000000, 0}, Position{-900000000, 0});
    CHECK(far == kDistanceSaturated);
    CHECK(distance_mm(Position{0, 0}, Position{0, 1800000000}) == kDistanceSaturated);
}

// The cosine table is data, and data can be typed wrong. Checking its shape
// catches a transposed pair that a distance test at one latitude would miss.
void test_the_cosine_table_is_monotonic_and_ends_where_it_should()
{
    CHECK(kCosTable1024[0] == 1024);
    CHECK(kCosTable1024[90] == 0);
    CHECK(kCosTable1024[60] == 512);   // cos 60° is exactly a half

    for (int i = 1; i <= 90; ++i) {
        CHECK(kCosTable1024[i] <= kCosTable1024[i - 1]);
    }

    // Against the real cosine, to a tolerance the table can meet.
    for (int i = 0; i <= 90; ++i) {
        const double want = 1024.0 * __builtin_cos(static_cast<double>(i) * 3.14159265358979 / 180.0);
        const double got  = static_cast<double>(kCosTable1024[i]);
        const double diff = got > want ? got - want : want - got;
        if (diff > 1.0) {
            std::fprintf(stderr, "FAIL line %d: cos table entry %d is %.0f, expected %.1f\n",
                         __LINE__, i, got, want);
            ++failures;
        }
    }
}

// Short distances are where the detectors live — a fifty-metre jump while the
// wrist is still is the canonical case — so the arithmetic has to keep its
// resolution down there rather than only at continental scale.
void test_short_distances_keep_their_resolution()
{
    const Position origin{5000000, 10000000};

    // About 1.1 m north.
    CHECK_NEAR(distance_mm(origin, Position{origin.latitude_e7 + 100, origin.longitude_e7}),
               1113ULL, 15);

    // About 11 m north.
    CHECK_NEAR(distance_mm(origin, Position{origin.latitude_e7 + 1000, origin.longitude_e7}),
               11132ULL, 5);

    // About 55 m north — the jump-while-still threshold.
    CHECK_NEAR(distance_mm(origin, Position{origin.latitude_e7 + 5000, origin.longitude_e7}),
               55660ULL, 5);

    // Monotonic in the small: each step further away must measure further.
    std::uint32_t previous = 0;
    for (std::int32_t step = 100; step <= 5000; step += 100) {
        const std::uint32_t d =
            distance_mm(origin, Position{origin.latitude_e7 + step, origin.longitude_e7});
        CHECK(d > previous);
        previous = d;
    }
}

}  // namespace

int main()
{
    test_all_four_validities_are_reachable();
    test_time_only_is_not_a_bad_position();
    test_freshness_is_decided_before_quality();
    test_silence_from_the_receiver_is_a_caveat();
    test_a_coordinate_off_the_globe_is_not_a_position();
    test_dead_reckoning_is_never_a_clean_fix();
    test_the_thresholds_belong_to_the_caller();
    test_nothing_defaults_to_a_confident_zero();

    test_a_degree_of_latitude_is_the_same_everywhere();
    test_a_degree_of_longitude_shrinks_with_latitude();
    test_a_degree_of_longitude_near_the_pole_is_metres_not_kilometres();
    test_the_longitude_scale_matches_a_spherical_reference();
    test_the_error_envelope_holds_at_every_latitude();
    test_the_final_metres_of_the_pole_degrade_in_millimetres();
    test_the_antimeridian_is_not_a_wall();
    test_the_antimeridian_still_is_not_a_wall_near_the_pole();
    test_the_grid_boundaries_are_answers();
    test_distance_is_zero_symmetric_and_bounded();
    test_the_cosine_table_is_monotonic_and_ends_where_it_should();
    test_short_distances_keep_their_resolution();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("position: all checks passed (host only — no receiver involved)\n");
    return 0;
}
