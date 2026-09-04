#include <cmath>
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

// Three invariants that hold at every latitude, independent of how accurate the
// answer is. They are cheap, and they catch the class of interpolation bug the
// envelope test above can miss: a sign slip or an off-by-one at a table
// boundary can be well under 1% of the reference and still be wrong in a way
// that means the arithmetic is not doing what the comment says.
//
// The whole degrees are where the table entries change, so the coarse step is
// chosen to land on every one of them exactly, and the two boundaries that
// matter most — 89°, the first degree of the region that was broken, and 90°,
// where the scale reaches zero — are swept at every representable step.
//
// Verified once at full resolution while writing this: 28 800 001 latitudes,
// zero violations of any of the three. What is committed is the sampled version,
// because the exhaustive one is minutes under a sanitizer and buys nothing that
// this does not.
//
// Unlike every other test added with it, this one is **not** red against the
// defect it was written alongside, and saying so is the point: a step function
// is monotone, symmetric and bounded — it was wrong by a factor of a thousand
// while satisfying all three. These are the invariants the *next* change has to
// keep, not evidence about the last one.
void test_the_longitude_scale_is_monotonic_symmetric_and_bounded()
{
    const std::uint32_t equator = distance_mm(at_lat(0), east_of(0, 10000000));

    struct Band { std::int64_t from; std::int64_t to; std::int64_t step; };
    const Band bands[] = {
        {0, 900000000, 100000},              // every 0.01°, landing on every whole degree
        {889990000, 890010000, 1},           // across the 89° table boundary, every step
        {899990000, 900000000, 1},           // and the last 111 m to the pole
    };

    for (const Band& band : bands) {
        std::uint32_t previous = 0xFFFFFFFFu;
        for (std::int64_t lat_e7 = band.from; lat_e7 <= band.to; lat_e7 += band.step) {
            const std::int32_t lat  = static_cast<std::int32_t>(lat_e7);
            const std::uint32_t here = distance_mm(at_lat(lat), east_of(lat, 10000000));

            // A degree of longitude never grows as you walk towards the pole,
            // and never exceeds what it is at the equator.
            CHECK(here <= previous);
            CHECK(here <= equator);

            // North and south are the same distance, exactly and not
            // approximately — the magnitude is taken before the table is read.
            CHECK(here == distance_mm(at_lat(-lat), east_of(-lat, 10000000)));

            previous = here;
        }
    }
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

// ---------------------------------------------------------------------------
// The initial great-circle bearing.
//
// Every expected value below is the spherical formula evaluated in double
// precision, and two of them are checked against geometry instead, because a
// test that re-derives the implementation's own arithmetic proves only that it
// was typed twice. Units are centidegrees clockwise from true north, the same
// unit `GnssObservation::course_centideg` uses.

constexpr std::int32_t kDeg = 10000000;

std::uint16_t bearing_of(Position a, Position b, int line)
{
    std::uint16_t centideg = 0xFFFFU;
    if (!initial_bearing(a, b, centideg)) {
        std::fprintf(stderr, "FAIL line %d: bearing refused, expected an answer\n", line);
        ++failures;
        return 0xFFFFU;
    }
    return centideg;
}

#define BEARING(a, b) bearing_of((a), (b), __LINE__)

void test_the_cardinal_directions_are_exact()
{
    // On a sphere these four are not approximations, so the test does not carry
    // a tolerance: a meridian *is* a great circle, and so is the equator.
    const Position origin{0, 0};
    const Position north{kDeg, 0};
    const Position east{0, kDeg};
    const Position west{0, -kDeg};
    CHECK(BEARING(origin, north) == 0);
    CHECK(BEARING(north, origin) == 18000);
    CHECK(BEARING(origin, east) == 9000);
    CHECK(BEARING(origin, west) == 27000);
}

void test_a_great_circle_bulges_towards_the_pole()
{
    // The independent check. Set out due east from 60°N and the shortest path
    // does not stay on the parallel — it climbs, so the initial bearing is
    // *north* of east, and by symmetry it is south of east from 60°S. No
    // arithmetic from the implementation is reused to say so; it is the same
    // fact as a long flight between two same-latitude places crossing higher
    // ground than either of them.
    const Position from_north{60 * kDeg, 0};
    const Position to_north{60 * kDeg, kDeg};
    const Position from_south{-60 * kDeg, 0};
    const Position to_south{-60 * kDeg, kDeg};
    const int north = BEARING(from_north, to_north);
    const int south = BEARING(from_south, to_south);
    CHECK(north < 9000);
    CHECK(south > 9000);
    // Symmetric about due east, to the centidegree.
    CHECK(9000 - north == south - 9000);
    // The value itself, from the spherical formula: 89.56698°.
    CHECK(north == 8957);
}

void test_the_antimeridian_is_not_a_wall_for_a_bearing_either()
{
    // 179.9°E to 179.9°W is a fifth of a degree eastward, not 359.8° westward.
    // Nothing in the implementation unwraps the longitude difference; sine and
    // cosine do it, and this is the test that says so.
    const Position east_side{0, 1799000000};
    const Position west_side{0, -1799000000};
    CHECK(BEARING(east_side, west_side) == 9000);
    CHECK(BEARING(west_side, east_side) == 27000);
}

void test_there_is_no_bearing_to_where_you_already_are()
{
    std::uint16_t centideg = 4242;
    const Position here{45 * kDeg, 9 * kDeg};
    CHECK(!initial_bearing(here, here, centideg));
    // Untouched, so a caller that ignored the return value cannot read a
    // direction that was never written.
    CHECK(centideg == 4242);

    // Standing on a pole there is no north to measure from. Two longitudes at
    // the same pole are also two coordinates and one physical point, which the
    // comparison above cannot see.
    const Position pole{90 * kDeg, 0};
    const Position pole_again{90 * kDeg, 10 * kDeg};
    const Position south_pole{-90 * kDeg, 0};
    CHECK(!initial_bearing(pole, pole_again, centideg));
    CHECK(!initial_bearing(pole, here, centideg));
    CHECK(!initial_bearing(south_pole, here, centideg));
    CHECK(centideg == 4242);

    // The bearing *to* a pole is ordinary, and is due north or due south.
    CHECK(BEARING(here, pole) == 0);
    CHECK(BEARING(here, south_pole) == 18000);
}

void test_a_coordinate_off_the_globe_has_no_bearing()
{
    std::uint16_t centideg = 4242;
    const Position sane{45 * kDeg, 9 * kDeg};
    const Position off_latitude{kLatitudeMaxE7 + 1, 0};
    const Position off_longitude{0, -kLongitudeMaxE7 - 1};
    CHECK(!initial_bearing(off_latitude, sane, centideg));
    CHECK(!initial_bearing(sane, off_latitude, centideg));
    CHECK(!initial_bearing(off_longitude, sane, centideg));
    CHECK(!initial_bearing(sane, off_longitude, centideg));
    CHECK(centideg == 4242);
}

void test_the_seam_at_north_folds_rather_than_overflowing()
{
    // One unit of the 1e-7 grid west of due north. The true bearing is
    // 359.999996°, which multiplied by 100 and rounded is 36000 — outside the
    // range this function promises, and a value that would print as "360°".
    // It has to come back as north.
    const Position from{45 * kDeg, 0};
    const Position just_west_of_north{46 * kDeg, -1};
    CHECK(BEARING(from, just_west_of_north) == 0);

    // And nothing on a full fan of directions leaves the range. The fan
    // crosses the seam once by construction.
    for (int degree = 0; degree < 360; ++degree) {
        const double radians = degree * 3.14159265358979323846 / 180.0;
        const Position target{
            static_cast<std::int32_t>(45.0 * kDeg + std::cos(radians) * kDeg),
            static_cast<std::int32_t>(std::sin(radians) * kDeg)};
        std::uint16_t value = 0xFFFFU;
        CHECK(initial_bearing(from, target, value));
        CHECK(value < 36000);
    }
}

void test_the_return_bearing_reverses_over_a_short_hop()
{
    // Over a hop this short the convergence of the meridians is well under a
    // centidegree, so the two bearings differ by exactly 180°. Over a long one
    // they would not, and that is a property of the sphere rather than a
    // defect — which is why this test stays short and says so.
    const Position from{30 * kDeg, 0};
    const Position to{30 * kDeg + 1000, 1000};  // about 11 m north-east
    const int out = BEARING(from, to);
    const int back = BEARING(to, from);
    const int difference = back > out ? back - out : out - back;
    CHECK(difference == 18000);
}

// ---------------------------------------------------------------------------
// `great_circle_mm()` — the screen's distance.
//
// What these check that the `distance_mm()` tests above cannot: those bound the
// *arithmetic* of an approximation whose method error is left to the header.
// This function has no method error worth stating, so the tests are closed
// forms instead — arcs whose length is known without a haversine — and the
// haversine reference is used only where a closed form is not available.
//
// `reference_distance_mm()` is an *independent* reference for `distance_mm()`
// and is deliberately not one for this function: both are haversines on the
// same sphere, so agreement between them checks the arguments, the radius and
// the saturation, and says nothing about the geometry. The geometry is what the
// closed forms below are for, and they are stated first for that reason.

// One degree of great-circle arc on the reference sphere, in millimetres. Not
// read from `kMillimetresPerLatE7Num`: that constant is 111 320 000 mm rounded
// to five figures, and the whole point of these two ways of arriving at the
// same number is that neither is derived from the other.
constexpr double kDegreeOfArcMm = kReferenceRadiusMm * kPi / 180.0;

// The bug #433 was raised for, as a length anybody can compute on paper.
//
// 89°N 0°E and 89°N 180°E are one degree from the pole on opposite meridians,
// so the short way between them goes straight over the pole and is exactly two
// degrees of arc. `distance_mm()` cannot see that: it takes one cosine at the
// mean latitude and walks 180° of longitude around a circle of radius 111 km,
// which is a longer path *and* the wrong one.
void test_the_short_way_between_two_polar_points_goes_over_the_pole()
{
    const Position a{890000000, 0};          // 89°N, 0°E
    const Position b{890000000, 1800000000}; // 89°N, 180°E

    CHECK_RELATIVE(great_circle_mm(a, b), 2.0 * kDegreeOfArcMm, 0.01);
    CHECK(great_circle_mm(a, b) == great_circle_mm(b, a));

    // And the old answer, asserted rather than described, so this test records
    // the defect it fixed instead of only the fix. Half a great circle around
    // the 89th parallel is pi * 111 km against 222 km over the pole: about
    // 58% over, which is the number in the issue.
    CHECK_RELATIVE(distance_mm(a, b), kPi * kDegreeOfArcMm, 1.0);
    CHECK(distance_mm(a, b) > great_circle_mm(a, b) + 100000000U);  // 100 km apart, blunt

    // A quarter turn instead of a half. cos(d) = sin^2(89 deg) there, because
    // cos(dlon) is zero -- another closed form, and a different one.
    const Position q{890000000, 900000000};  // 89 deg N, 90 deg E
    const double   sin89    = __builtin_sin(radians(89.0));
    const double   expected = __builtin_acos(sin89 * sin89) * kReferenceRadiusMm;
    CHECK_RELATIVE(great_circle_mm(a, q), expected, 0.01);
}

// A pole is an ordinary destination for a distance, unlike for a bearing. One
// degree of arc, straight up the meridian, from either side.
void test_a_pole_has_a_distance_even_though_it_has_no_bearing()
{
    const Position near_pole{890000000, 1234567};
    const Position pole{kLatitudeMaxE7, 0};

    CHECK_RELATIVE(great_circle_mm(near_pole, pole), kDegreeOfArcMm, 0.01);
    CHECK_RELATIVE(great_circle_mm(pole, near_pole), kDegreeOfArcMm, 0.01);

    // The refusal `initial_bearing()` makes at the same coordinates, side by
    // side with the answer this function gives, because the difference between
    // them is a decision and not an accident.
    std::uint16_t centideg = 0;
    CHECK(!initial_bearing(pole, near_pole, centideg));
    CHECK(initial_bearing(near_pole, pole, centideg));

    // Two longitudes at the same pole are one physical point.
    CHECK(great_circle_mm(pole, Position{kLatitudeMaxE7, kLongitudeMaxE7}) == 0U);
}

// Where the screen already had the right number, it still has it. This is the
// half of the change that must not move: every baseline a person actually
// walks or rides is a place the two functions agree, and a rewrite that shifted
// those would be a regression dressed as a fix.
void test_the_ordinary_baselines_did_not_move()
{
    struct Case {
        Position a;
        Position b;
    };
    // Each label is the distance, computed and then checked, not eyeballed from
    // the coordinates. An earlier draft of this list said 150 m, 15 km and
    // 130 km for baselines that were 15.7 m, 1.6 km and 13.2 km, which pinned
    // "the half that must not move" a decade short of where it claimed to.
    const Case cases[] = {
        {{5000000, 10000000}, {5010000, 10010000}},        // 157 m
        {{5000000, 10000000}, {5100000, 10100000}},        // 1.57 km
        {{-350000000, 1500000000}, {-350500000, 1500500000}},  // 7.19 km
        {{500000000, 100000000}, {510000000, 110000000}},  // 132 km at 50°N
    };
    for (const Case& c : cases) {
        CHECK_RELATIVE(great_circle_mm(c.a, c.b), reference_distance_mm(c.a, c.b), 0.01);
        // Half a percent of each other: the header promises the equirectangular
        // form is right at this scale, and this is that promise as a test.
        CHECK_RELATIVE(distance_mm(c.a, c.b), static_cast<double>(great_circle_mm(c.a, c.b)), 0.5);
        CHECK(great_circle_mm(c.a, c.b) == great_circle_mm(c.b, c.a));
    }

    // And where they begin to part, so the band above is bounded from the other
    // side rather than trailing off into the closed forms. 70°N across 20° of
    // longitude is 758 km on the great circle and about 761 km equirectangular:
    // the header's own "half a percent at 70°N across 20°", asserted from both
    // ends so that neither a shrinking nor a growing gap passes.
    const Position a{700000000, 0};
    const Position b{700000000, 200000000};
    CHECK_RELATIVE(great_circle_mm(a, b), reference_distance_mm(a, b), 0.01);
    const double gap = (static_cast<double>(distance_mm(a, b)) -
                        static_cast<double>(great_circle_mm(a, b))) /
                       static_cast<double>(great_circle_mm(a, b)) * 100.0;
    CHECK(gap > 0.35 && gap < 0.65);
}

// Saturation is a readout choice, not a limit of the method, and the boundary
// is where `distance_mm()` puts it so the two never disagree about whether a
// number exists.
void test_the_screen_distance_saturates_where_the_other_one_does()
{
    // 1107.7 km on the great circle, and 1000 km exactly from `distance_mm()`,
    // which saturated there by accident. Now it is on purpose.
    CHECK(great_circle_mm({800000000, 0}, {800000000, 600000000}) == kDistanceSaturated);

    // Pole to pole is half a great circle, twenty thousand kilometres.
    CHECK(great_circle_mm({kLatitudeMaxE7, 0}, {-kLatitudeMaxE7, 0}) == kDistanceSaturated);

    // Just inside: nine degrees of arc up a meridian, about 1002 km, is past
    // the clamp; eight and a half, about 946 km, is a number.
    CHECK(great_circle_mm({0, 0}, {90000000, 0}) == kDistanceSaturated);
    CHECK(great_circle_mm({0, 0}, {85000000, 0}) < kDistanceSaturated);
    CHECK_RELATIVE(great_circle_mm({0, 0}, {85000000, 0}), 8.5 * kDegreeOfArcMm, 0.01);

    // A coordinate that is not on the globe: the same answer, for the same
    // reason -- saturated fails every "is it near" test, and this one is
    // reached with bytes off a radio.
    const Position origin{0, 0};
    CHECK(great_circle_mm(origin, Position{kLatitudeMaxE7 + 1, 0}) == kDistanceSaturated);
    CHECK(great_circle_mm(origin, Position{0, kLongitudeMaxE7 + 1}) == kDistanceSaturated);
    const Position absurd{2147483647, -2147483647 - 1};
    CHECK(great_circle_mm(origin, absurd) == kDistanceSaturated);
    CHECK(great_circle_mm(absurd, absurd) == kDistanceSaturated);
}

// Zero is an answer here, not a refusal -- the one place the readout prints
// `0 m` honestly -- and the antimeridian needs no wrap because the haversine is
// periodic in the longitude difference.
void test_the_screen_distance_is_zero_at_home_and_short_across_the_seam()
{
    const Position p{5000000, 10000000};
    CHECK(great_circle_mm(p, p) == 0U);

    const Position west{0, 1799995000};
    const Position east{0, -1799995000};
    CHECK_NEAR(great_circle_mm(west, east), 111000ULL, 1);
    CHECK(great_circle_mm(west, east) == great_circle_mm(east, west));

    // The same span away from the seam, to show the seam changed nothing.
    CHECK(great_circle_mm(Position{0, 5000}, Position{0, -5000}) ==
          great_circle_mm(west, east));
}

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
    test_the_longitude_scale_is_monotonic_symmetric_and_bounded();
    test_the_antimeridian_is_not_a_wall();
    test_the_antimeridian_still_is_not_a_wall_near_the_pole();
    test_the_grid_boundaries_are_answers();
    test_distance_is_zero_symmetric_and_bounded();
    test_the_cosine_table_is_monotonic_and_ends_where_it_should();
    test_short_distances_keep_their_resolution();

    test_the_cardinal_directions_are_exact();
    test_a_great_circle_bulges_towards_the_pole();
    test_the_antimeridian_is_not_a_wall_for_a_bearing_either();
    test_there_is_no_bearing_to_where_you_already_are();
    test_a_coordinate_off_the_globe_has_no_bearing();
    test_the_seam_at_north_folds_rather_than_overflowing();
    test_the_return_bearing_reverses_over_a_short_hop();

    test_the_short_way_between_two_polar_points_goes_over_the_pole();
    test_a_pole_has_a_distance_even_though_it_has_no_bearing();
    test_the_ordinary_baselines_did_not_move();
    test_the_screen_distance_saturates_where_the_other_one_does();
    test_the_screen_distance_is_zero_at_home_and_short_across_the_seam();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("position: all checks passed (host only — no receiver involved)\n");
    return 0;
}
