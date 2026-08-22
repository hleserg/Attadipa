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
    test_the_antimeridian_is_not_a_wall();
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
