#include <cstdio>
#include <limits>

#include "attadipa/core/trust.h"

// Host tests for the GNSS trust state (docs/adr/0011-gnss-integrity.md §5, §6).
//
// Nothing here has been near a satellite. These are tests of a decision rule:
// given evidence, does the state go where ADR-0011 says it goes, and does the
// reason survive the journey. Whether the evidence itself is ever produced
// correctly is a question about a receiver, and no receiver has been powered on
// — T-051 and T-052.
//
// Most of this file walks sequences rather than checking single calls, and that
// is deliberate. The detectors that matter are rate detectors, and a rate needs
// two epochs; a suite of one-observation tests passes cheerfully while every
// one of them is switched off.

using namespace attadipa::core;

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

void check_state(TrustState actual, TrustState expected, int line)
{
    if (actual != expected) {
        std::fprintf(stderr, "FAIL line %d: state is %s, expected %s\n", line,
                     to_string(actual), to_string(expected));
        ++failures;
    }
}

#define CHECK_STATE(actual, expected) check_state((actual), (expected), __LINE__)

void check_reason(const TrustEngine& engine, TrustReason reason, bool expected, int line)
{
    if (engine.holds(reason) != expected) {
        std::fprintf(stderr, "FAIL line %d: %s is %s, expected %s\n", line, to_string(reason),
                     engine.holds(reason) ? "held" : "absent", expected ? "held" : "absent");
        ++failures;
    }
}

#define CHECK_REASON(engine, reason) check_reason((engine), (reason), true, __LINE__)
#define CHECK_NO_REASON(engine, reason) check_reason((engine), (reason), false, __LINE__)

MonotonicTime at(std::uint64_t ms) { return MonotonicTime{ms}; }

// Synthetic, and deliberately obviously nowhere: half a degree north, one
// degree east, in the Gulf of Guinea. The same place every replay trace uses.
//
// The first draft of this file used a coordinate picked as "arbitrary" that
// resolved to a real city. That is exactly what must not be committed to a
// public repository in a file about somebody's location, and "arbitrary" is not
// a defence. The habit that prevents it is choosing numbers that could not be
// anybody's, rather than numbers that probably are not.
constexpr std::int32_t kLat =  5000000;   // 0.5°N
constexpr std::int32_t kLon = 10000000;   // 1.0°E

// A fix a receiver would be pleased with: enough satellites, tight accuracy,
// nothing to report about its own signals.
GnssObservation good_fix(std::uint64_t ms, std::int32_t lat = kLat, std::int32_t lon = kLon)
{
    GnssObservation o;
    o.observed_at            = at(ms);
    o.fix_type               = FixType::ThreeD;
    o.position               = Position{lat, lon};
    o.altitude_msl_mm        = 150000;
    o.horizontal_accuracy_mm = 3500;
    o.satellites_used        = 11;
    o.satellites_in_view     = 14;
    o.hdop_centi             = 90;
    o.jamming                = ReceiverIndication::None;
    o.spoofing               = ReceiverIndication::None;
    o.source                 = PositionSource::LocalGnss;
    return o;
}

// ---------------------------------------------------------------------------

void test_a_clean_fix_is_trusted()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
    CHECK(evaluator.engine().score() == 0);
    CHECK(evaluator.engine().reasons() == 0);
}

// ADR-0011 §5, and the ordering is the argument rather than the numbers: the
// receiver's spoofing alarm alone is enough to stop navigating; its jamming
// alarm alone is a caveat, because a jammed receiver is being denied rather
// than lied to.
void test_the_receiver_alarms_are_weighted_differently()
{
    {
        TrustEvaluator evaluator;
        GnssObservation o = good_fix(0);
        o.spoofing        = ReceiverIndication::Critical;
        evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
        CHECK_STATE(evaluator.state(), TrustState::Untrusted);
        CHECK_REASON(evaluator.engine(), TrustReason::ReceiverSpoofing);
    }
    {
        TrustEvaluator evaluator;
        GnssObservation o = good_fix(0);
        o.jamming         = ReceiverIndication::Warning;
        evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
        CHECK_STATE(evaluator.state(), TrustState::Degraded);
        CHECK_REASON(evaluator.engine(), TrustReason::ReceiverJamming);
    }
}

// OWNER_DECISIONS OD-5 §2, and the reason this test exists at all: on the
// LS550G, anti-spoofing is `UNKNOWN`, not `SUPPORTED`. A receiver that cannot
// detect spoofing is not a receiver reporting that there is none, and a field
// that silently maps the first onto the second turns an absent capability into
// a reassurance.
void test_unknown_is_not_an_all_clear()
{
    TrustEvaluator evaluator;

    GnssObservation alarming = good_fix(0);
    alarming.spoofing        = ReceiverIndication::Critical;
    evaluator.observe(alarming, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    // A receiver that has stopped saying anything must not thereby say "fine".
    GnssObservation silent = good_fix(1000);
    silent.spoofing        = ReceiverIndication::Unknown;
    evaluator.observe(silent, PositionValidity::Valid, MotionEvidence{}, {}, at(1000));
    CHECK_REASON(evaluator.engine(), TrustReason::ReceiverSpoofing);
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    // Nor may a receiver that has no such feature at all.
    GnssObservation incapable = good_fix(2000);
    incapable.spoofing        = ReceiverIndication::Unsupported;
    evaluator.observe(incapable, PositionValidity::Valid, MotionEvidence{}, {}, at(2000));
    CHECK_REASON(evaluator.engine(), TrustReason::ReceiverSpoofing);

    // Only a positive all-clear clears it.
    GnssObservation clear = good_fix(3000);
    clear.spoofing        = ReceiverIndication::None;
    evaluator.observe(clear, PositionValidity::Valid, MotionEvidence{}, {}, at(3000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ReceiverSpoofing);
}

// THE REGRESSION TEST.
//
// The interval between epochs was once read after the previous timestamp had
// been overwritten, which made every dt zero. Both rate detectors then divided
// by nothing, found nothing, and reported nothing — and every single-observation
// test in the world still passed. This walks two epochs and demands the answer.
void test_the_interval_is_taken_before_it_is_overwritten()
{
    // 0.01° of latitude is about 1.1 km. Covered in one second, that is roughly
    // 1100 m/s — comfortably past the 55 m/s plausibility limit.
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);

    evaluator.observe(good_fix(1000, kLat + 100000), PositionValidity::Valid, MotionEvidence{}, {},
                      at(1000));
    CHECK_REASON(evaluator.engine(), TrustReason::PositionJump);
    CHECK_STATE(evaluator.state(), TrustState::Degraded);
}

// A fix dropout is the most ordinary event there is — a bridge, a tunnel, a
// doorway — and it must not read as a teleport.
//
// This was a real defect, and a bad one: the interval was taken from a single
// shared "previous epoch" which no-fix observations kept advancing while the
// last known position stood still. A minute under a bridge followed by a
// five-hundred-metre walk was therefore divided by one second instead of sixty
// and reported five hundred metres per second. A detector that fires on walking
// out of a tunnel is worse than no detector, because it teaches the wearer to
// ignore the warning that matters.
void test_a_fix_dropout_is_not_a_teleport()
{
    TrustEvaluator evaluator;
    const MotionEvidence walking{SensorBody::Watch, true, true};

    evaluator.observe(good_fix(0), PositionValidity::Valid, walking, {}, at(0));

    // Sixty seconds with no fix at all. Each one still arrives.
    for (std::uint64_t t = 1000; t <= 60000; t += 1000) {
        GnssObservation lost = good_fix(t);
        lost.fix_type        = FixType::NoFix;
        lost.position.reset();
        lost.satellites_used    = 0;
        lost.satellites_in_view = 2;
        evaluator.observe(lost, PositionValidity::NoFix, walking, {}, at(t));
    }

    // 45000 units of latitude is about 500 m: 8.2 m/s over the true 61 seconds,
    // and 500 m/s over the one second since the last observation.
    evaluator.observe(good_fix(61000, kLat + 45000), PositionValidity::Valid, walking, {},
                      at(61000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);

    // And the detector is still alive: the same gap followed by something
    // nothing travels must still fire.
    TrustEvaluator other;
    other.observe(good_fix(0), PositionValidity::Valid, walking, {}, at(0));
    for (std::uint64_t t = 1000; t <= 60000; t += 1000) {
        GnssObservation lost = good_fix(t);
        lost.fix_type        = FixType::NoFix;
        lost.position.reset();
        other.observe(lost, PositionValidity::NoFix, walking, {}, at(t));
    }
    // 500 km in the same sixty seconds.
    other.observe(good_fix(61000, kLat + 45000000), PositionValidity::Valid, walking, {},
                  at(61000));
    CHECK_REASON(other.engine(), TrustReason::PositionJump);
}

// The altitude rate has its own timestamp for the same reason, and needs its own
// witness: an observation can carry a position and no altitude, or the reverse.
void test_the_altitude_rate_has_its_own_clock()
{
    TrustEvaluator evaluator;
    GnssObservation start = good_fix(0);
    evaluator.observe(start, PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    // A minute of fixes that carry no altitude at all.
    for (std::uint64_t t = 1000; t <= 60000; t += 1000) {
        GnssObservation flat = good_fix(t, kLat + static_cast<std::int32_t>(t / 10));
        flat.altitude_msl_mm.reset();
        evaluator.observe(flat, PositionValidity::Valid, MotionEvidence{}, {}, at(t));
    }

    // 60 m higher than the last altitude anybody reported, which was a minute
    // ago: 1 m/s, a gentle slope. Divided by one second it would be 60 m/s.
    GnssObservation higher = good_fix(61000, kLat + 6100);
    higher.altitude_msl_mm = 150000 + 60000;
    evaluator.observe(higher, PositionValidity::Valid, MotionEvidence{}, {}, at(61000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ImplausibleAltitudeRate);
}

// The other half of the same property: the detector must also stay quiet when
// the same distance is covered over a plausible interval. A detector that fires
// on distance rather than on speed would pass the test above and fail this one.
void test_the_same_distance_over_a_longer_interval_is_a_walk()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    // ~1.1 km over 600 s is about 1.85 m/s. A brisk walk.
    evaluator.observe(good_fix(600000, kLat + 100000), PositionValidity::Valid, MotionEvidence{},
                      {}, at(600000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
}

// The altitude rate uses the same interval, so it fails the same way and needs
// its own witness.
void test_the_altitude_rate_uses_that_same_interval()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    GnssObservation rising    = good_fix(1000);
    rising.altitude_msl_mm    = 150000 + 60000;  // 60 m in one second
    evaluator.observe(rising, PositionValidity::Valid, MotionEvidence{}, {}, at(1000));
    CHECK_REASON(evaluator.engine(), TrustReason::ImplausibleAltitudeRate);

    TrustEvaluator patient;
    patient.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    GnssObservation climbing = good_fix(60000);
    climbing.altitude_msl_mm = 150000 + 60000;  // 60 m in a minute
    patient.observe(climbing, PositionValidity::Valid, MotionEvidence{}, {}, at(60000));
    CHECK_NO_REASON(patient.engine(), TrustReason::ImplausibleAltitudeRate);
}

// THE REGRESSION TEST for the arrival-time bug, issue #26: a position relayed
// over a link that queues and retries is measured seconds apart and can
// arrive milliseconds apart. `now` is the arrival time; `observed_at` is the
// measurement. Dividing by the former reads a walk as a teleport.
void test_relayed_fix_is_measured_not_by_arrival_time()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(500000));

    // ~500 m measured ten seconds apart — 50 m/s, under the 55 m/s limit —
    // but delivered to this evaluator one millisecond after the first fix.
    GnssObservation relayed = good_fix(10000, kLat + 45000);
    evaluator.observe(relayed, PositionValidity::Valid, MotionEvidence{}, {}, at(500001));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
}

// The altitude branch shares the same baseline policy, so it shares the same
// witness.
void test_altitude_rate_is_measured_not_by_arrival_time()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(500000));

    // 60 m climbed, measured over sixty seconds — 1 m/s, a gentle slope — but
    // delivered one millisecond after the first sample.
    GnssObservation risen = good_fix(60000);
    risen.altitude_msl_mm = 150000 + 60000;
    evaluator.observe(risen, PositionValidity::Valid, MotionEvidence{}, {}, at(500001));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ImplausibleAltitudeRate);
}

// A receiver that has lost its fix can still hold the last coordinate in the
// position field while reporting `NoFix`. That coordinate is retained state,
// not a new measurement, and must not become the movement baseline: it would
// pull the timestamp closer to the *next* valid fix and divide that fix's
// distance by too short an interval.
void test_retained_coordinate_no_fix_does_not_move_the_baseline()
{
    TrustEvaluator evaluator;
    const MotionEvidence walking{SensorBody::Watch, true, true};
    evaluator.observe(good_fix(0), PositionValidity::Valid, walking, {}, at(0));

    // A no-fix sample that keeps last known coordinate on the wire.
    GnssObservation retained  = good_fix(9000);
    retained.fix_type         = FixType::NoFix;
    retained.satellites_used  = 0;
    evaluator.observe(retained, PositionValidity::NoFix, walking, {}, at(9000));

    // ~500 m, ten seconds after the *valid* baseline at t=0: 50 m/s, under
    // the limit. If the retained no-fix sample had become the baseline, this
    // would be measured from t=9000 instead — one second, five hundred
    // metres per second, and a false PositionJump.
    evaluator.observe(good_fix(10000, kLat + 45000), PositionValidity::Valid, walking, {},
                       at(10000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
}

// The altitude equivalent: a no-fix sample still reporting the old altitude
// must not shorten the interval the next real altitude reading is divided by.
void test_no_fix_altitude_sample_does_not_move_the_baseline()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    GnssObservation retained  = good_fix(9000);
    retained.fix_type         = FixType::NoFix;
    retained.satellites_used  = 0;
    retained.altitude_msl_mm  = 150000;  // the same old altitude, still on the wire
    evaluator.observe(retained, PositionValidity::NoFix, MotionEvidence{}, {}, at(9000));

    // 250 m of climb over the true ten-second interval is 25 m/s, under the
    // 30 m/s limit. Divided by the one second since the retained no-fix
    // sample it would be 250 m/s.
    GnssObservation risen = good_fix(10000);
    risen.altitude_msl_mm = 150000 + 250000;
    evaluator.observe(risen, PositionValidity::Valid, MotionEvidence{}, {}, at(10000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ImplausibleAltitudeRate);
}

// Two observations measured at the same instant are not an error — they are
// what "simultaneous" means — and must be handled without dividing by zero or
// fabricating a rate.
void test_equal_measurement_timestamps_are_handled_safely()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    evaluator.observe(good_fix(0, kLat + 100000), PositionValidity::Valid, MotionEvidence{}, {},
                       at(0));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
}

// An observation that reports an earlier measurement instant than the
// baseline already accepted must not replace it. If it did, the next
// legitimately-ordered fix would be divided by whatever tiny interval
// separates it from the reordered sample instead of the true one.
void test_out_of_order_observation_does_not_poison_the_baseline()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(10000), PositionValidity::Valid, MotionEvidence{}, {}, at(10000));

    // Arrives late (processed at 10500) but claims to have been measured a
    // millisecond *before* the baseline already accepted, hundreds of
    // kilometres away.
    GnssObservation out_of_order = good_fix(9999, kLat + 45000000);
    evaluator.observe(out_of_order, PositionValidity::Valid, MotionEvidence{}, {}, at(10500));

    // The wearer has not moved. Measured against the baseline this call must
    // still be using — t=10000, at the original position — one millisecond
    // later and no distance at all is unremarkable. Measured against the
    // reordered sample it would be hundreds of kilometres in two
    // milliseconds.
    evaluator.observe(good_fix(10001), PositionValidity::Valid, MotionEvidence{}, {}, at(11000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
}

// The poisoning sequence itself, not just the single sample that starts it: a
// genuine baseline, one observation dated far in the future, and then real
// observations afterward. A fix that only refuses the future-dated sample but
// leaves it as the baseline — or that refuses it correctly but then never
// accepts a baseline again — would still pass a test that checks nothing past
// the second observe() call. This one does not stop there.
void test_a_future_dated_observation_is_rejected_without_freezing_the_baseline()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    // Claims to have been measured ~11.5 days in the future, ~500 km away,
    // and is handed to observe() one millisecond later — the review's own
    // reproduction. If this became the baseline, the interval to it would be
    // enormous and its implied speed would round to nothing; the review is
    // right that it must not fire PositionJump on the strength of a claimed
    // interval that large, but it must not be believed either.
    GnssObservation poisoned = good_fix(1000000000, kLat + 45000000);
    evaluator.observe(poisoned, PositionValidity::Valid, MotionEvidence{}, {}, at(1));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
    CHECK_REASON(evaluator.engine(), TrustReason::ClockDisagreement);

    // A real jump, measured two milliseconds after the *original* baseline at
    // t=0 — ~500 km in 2 ms. This is the test that would fail against an
    // implementation that freezes: if the poisoned sample had become the
    // baseline, this observation's observed_at (2) would be "older" than the
    // poisoned one's (1 000 000 000), fail in_order, and never reach the
    // speed calculation at all.
    evaluator.observe(good_fix(2, kLat + 45000000), PositionValidity::Valid, MotionEvidence{}, {},
                       at(2));
    CHECK_REASON(evaluator.engine(), TrustReason::PositionJump);

    // And the detector is not merely alive for one more call — it keeps
    // working. An ordinary walk (about 56 m over 10 s, 5.6 m/s) measured
    // against the baseline the previous, genuine observation just set is
    // neither a jump nor a permanently latched one.
    evaluator.observe(good_fix(10002, kLat + 45005000), PositionValidity::Valid, MotionEvidence{},
                       {}, at(10002));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
}

// THE DETECTORS RUN ON A SAMPLE THAT IS NOT FIT TO BE THE BASELINE.
//
// Refusing an out-of-order or future-dated sample as a baseline is right.
// Refusing to *look* at it is not, and for a while this file could not tell the
// two apart: every existing test above passes `MotionEvidence{}` — motion
// unknown — which can never raise MotionDisagreement whatever the code does.
// So the detector could be skipped entirely for exactly the class of sample
// these tests are about, and nothing here would notice. It was, and review
// found it rather than the suite.
//
// All three below use `still` — known, and not moving — which is the only
// evidence that makes MotionDisagreement possible, and each ends by proving the
// baseline was still not adopted. A test that only checked the reason fires
// would pass against an implementation that reopened the freeze.
void test_a_reordered_sample_is_still_checked_against_the_standing_baseline()
{
    const MotionEvidence still{SensorBody::Watch, true, false};

    TrustEvaluator evaluator;
    evaluator.observe(good_fix(10000), PositionValidity::Valid, still, {}, at(10000));

    // Measured a second BEFORE the accepted baseline, and reporting the wrist
    // ~500 km away while the accelerometer says it never left the desk. It must
    // not become the baseline — it is older — and it must not pass unexamined
    // either, which is what it did.
    evaluator.observe(good_fix(9000, kLat + 45000000), PositionValidity::Valid, still, {},
                      at(10500));
    CHECK_REASON(evaluator.engine(), TrustReason::MotionDisagreement);

    // And the baseline is still the t=10000 one, not the reordered sample's.
    // Proved from the other side: a fix at the ORIGINAL position, measured
    // later, is not a movement at all. Against an implementation that had
    // adopted the reordered position, this would be a 500 km move from it and
    // MotionDisagreement would latch again rather than clear.
    evaluator.observe(good_fix(20000), PositionValidity::Valid, still, {}, at(20000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::MotionDisagreement);
}

// The same gap seen through the poisoning sequence, which the existing
// future-dated test walks with motion unknown and therefore cannot see.
void test_a_future_dated_sample_is_checked_as_well_as_refused()
{
    const MotionEvidence still{SensorBody::Watch, true, false};

    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, still, {}, at(0));

    // ~11.5 days in the future, ~500 km away, one millisecond after the first
    // fix. ClockDisagreement for the timestamp, MotionDisagreement for the
    // movement — the second is the one that used to be missing. PositionJump
    // still must not fire: its implied speed is computed over a claimed
    // interval nobody should believe, and the whole point is not to act on
    // that number.
    GnssObservation poisoned = good_fix(1000000000, kLat + 45000000);
    evaluator.observe(poisoned, PositionValidity::Valid, still, {}, at(1));
    CHECK_REASON(evaluator.engine(), TrustReason::ClockDisagreement);
    CHECK_REASON(evaluator.engine(), TrustReason::MotionDisagreement);
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);

    // The baseline is untouched, so a genuine jump measured against t=0 is
    // still caught — the freeze fix has to survive the detector fix.
    evaluator.observe(good_fix(2, kLat + 45000000), PositionValidity::Valid, MotionEvidence{}, {},
                      at(2));
    CHECK_REASON(evaluator.engine(), TrustReason::PositionJump);
}

// The altitude block had the identical shape and nothing covered it at all.
//
// It also has a limit worth stating rather than testing around: altitude has no
// equivalent of MotionDisagreement. Every altitude check is a RATE, so a sample
// dated far enough ahead defeats it arithmetically — the claimed interval grows
// with the lie, and 2 km over a claimed eleven days is a gentle drift. That is
// why the position test above asserts MotionDisagreement rather than
// PositionJump for its poisoned sample: the interval-free detector is the one
// that survives a bad timestamp, and altitude does not have one.
//
// So the case this split actually recovers for altitude is the sample dated
// only a little ahead — past the 50 ms skew tolerance, not past plausibility.
// The interval stays short, the rate stays absurd, and before the split nothing
// looked at it at all.
void test_a_future_dated_altitude_is_checked_as_well_as_refused()
{
    TrustEvaluator evaluator;

    GnssObservation ground = good_fix(0);
    ground.altitude_msl_mm = 150000;
    evaluator.observe(ground, PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    // Measured, it claims, at t=200 — but handed over at t=100, so 100 ms in
    // the future against a 50 ms tolerance. Refused as a baseline, correctly.
    // Two kilometres of climb over that 200 ms interval is 10 km/s, and the
    // refusal must not stop anybody noticing.
    GnssObservation absurd = good_fix(200);
    absurd.altitude_msl_mm = 2150000;
    evaluator.observe(absurd, PositionValidity::Valid, MotionEvidence{}, {}, at(100));
    CHECK_REASON(evaluator.engine(), TrustReason::ImplausibleAltitudeRate);
    CHECK_REASON(evaluator.engine(), TrustReason::ClockDisagreement);

    // And it was refused: the baseline is still t=0 at 150 m, so an ordinary
    // climb against it — 100 m over 100 s, 1 m/s — is not implausible. Against
    // an implementation that had adopted the poisoned altitude this would be a
    // 2 km descent in the same interval and would fire.
    GnssObservation gentle = good_fix(100000);
    gentle.altitude_msl_mm = 250000;
    evaluator.observe(gentle, PositionValidity::Valid, MotionEvidence{}, {}, at(100000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ImplausibleAltitudeRate);
}

// The canonical detector, and the one ADR-0011 names the BMA423 for. Note what
// is being asserted: `known == false` is not evidence of stillness. A device
// that has not asked the accelerometer knows nothing, and must not treat that
// as an answer.
void test_a_still_wrist_is_evidence_and_an_unasked_one_is_not()
{
    const MotionEvidence still{SensorBody::Watch, true, false};
    const MotionEvidence unasked{};

    {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, still, {}, at(0));
        // 500 m away, ten minutes later: no speed problem at all, but the wrist
        // says it never moved.
        evaluator.observe(good_fix(600000, kLat + 45000), PositionValidity::Valid, still, {},
                          at(600000));
        CHECK_REASON(evaluator.engine(), TrustReason::MotionDisagreement);
        CHECK_STATE(evaluator.state(), TrustState::Degraded);
    }
    {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, unasked, {}, at(0));
        evaluator.observe(good_fix(600000, kLat + 45000), PositionValidity::Valid, unasked, {},
                          at(600000));
        CHECK_NO_REASON(evaluator.engine(), TrustReason::MotionDisagreement);
        CHECK_STATE(evaluator.state(), TrustState::Trusted);
    }
}

// A watch IMU says nothing about a node GNSS receiver. Both instruments can be
// correct while the two physical bodies move differently.
void test_motion_evidence_is_scoped_to_the_position_body()
{
    GnssObservation first = good_fix(0);
    first.source = PositionSource::NodeGnss;
    GnssObservation later = good_fix(600000, kLat + 45000);
    later.source = PositionSource::NodeGnss;

    TrustEvaluator wrist_evidence;
    const MotionEvidence wrist_still{SensorBody::Watch, true, false};
    wrist_evidence.observe(first, PositionValidity::Valid, wrist_still, {}, at(0));
    wrist_evidence.observe(later, PositionValidity::Valid, wrist_still, {}, at(600000));
    CHECK_NO_REASON(wrist_evidence.engine(), TrustReason::MotionDisagreement);

    TrustEvaluator node_evidence;
    const MotionEvidence node_still{SensorBody::Node, true, false};
    node_evidence.observe(first, PositionValidity::Valid, node_still, {}, at(0));
    node_evidence.observe(later, PositionValidity::Valid, node_still, {}, at(600000));
    CHECK_REASON(node_evidence.engine(), TrustReason::MotionDisagreement);

    CHECK(body_of(PositionSource::LocalGnss) == SensorBody::Watch);
    CHECK(body_of(PositionSource::NodeGnss) == SensorBody::Node);
    CHECK(body_of(PositionSource::Companion) == SensorBody::Companion);
    CHECK(body_of(PositionSource::Manual) == SensorBody::Unknown);
}

// And the half the test above cannot see, because it uses one source for every
// observation: the body has to be checked on the BASELINE as well as on the
// sample. `moved` is the distance from the previous fix, so a previous fix
// measured on another body makes it a distance between two bodies — and the
// detector then judged that with one body's stillness. A watch falling back to
// its node's receiver is the ordinary case, not an exotic one.
void test_a_change_of_body_is_not_a_movement()
{
    GnssObservation from_node = good_fix(0);
    from_node.source = PositionSource::NodeGnss;
    // Five hundred metres away: the node is in a bag on the other side of the
    // building, and the watch has never left the desk.
    GnssObservation from_watch = good_fix(600000, kLat + 45000);
    from_watch.source = PositionSource::LocalGnss;

    const MotionEvidence wrist_still{SensorBody::Watch, true, false};

    TrustEvaluator evaluator;
    evaluator.observe(from_node, PositionValidity::Valid, wrist_still, {}, at(0));
    evaluator.observe(from_watch, PositionValidity::Valid, wrist_still, {}, at(600000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::MotionDisagreement);
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
    CHECK_STATE(evaluator.state(), TrustState::Trusted);

    // The discontinuity costs one sample and no more: the watch's own next fix
    // is compared against the watch's own previous one, and a teleport there is
    // still a teleport.
    GnssObservation watch_teleport = good_fix(1200000, kLat + 90000);
    watch_teleport.source = PositionSource::LocalGnss;
    evaluator.observe(watch_teleport, PositionValidity::Valid, wrist_still, {}, at(1200000));
    CHECK_REASON(evaluator.engine(), TrustReason::MotionDisagreement);
}

// A receiver using more satellites than it can see has not made a marginal
// reading; it has made an impossible one.
void test_satellites_used_cannot_exceed_satellites_in_view()
{
    TrustEvaluator evaluator;
    GnssObservation o    = good_fix(0);
    o.satellites_used    = 12;
    o.satellites_in_view = 9;
    evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ConstellationAnomaly);
}

// Two absolute instants compared, which is what a wall clock is for. clock.h
// forbids the other use — measuring a duration — and this is the detector that
// rule protects: a spoofer able to step the clock we time out against would
// switch off the detector meant to catch it.
void test_receiver_time_is_compared_against_device_time()
{
    TrustEvaluator evaluator;
    GnssObservation o     = good_fix(0);
    o.receiver_time       = WallTime{1'700'000'000};
    o.receiver_time_valid = true;
    evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, WallTime{1'700'000'600}, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ClockDisagreement);

    // A receiver that says its own time is not to be trusted is not evidence
    // about anything.
    TrustEvaluator honest;
    GnssObservation unsure     = good_fix(0);
    unsure.receiver_time       = WallTime{1'700'000'000};
    unsure.receiver_time_valid = false;
    honest.observe(unsure, PositionValidity::Valid, MotionEvidence{}, WallTime{1'700'000'600},
                   at(0));
    CHECK_NO_REASON(honest.engine(), TrustReason::ClockDisagreement);
}

// The same detector, given the input it exists for.
//
// A receiver's reported time is a field on the wire. clock.h says so in as many
// words — WallTime is signed "because dates before 1970 are representable in a
// corrupted or hostile input and clamping them to zero would hide the
// corruption" — so the whole int64 range is in-domain here by design, not by
// oversight.
//
// It was computed with `a - b` and then `-difference`, both of which are
// undefined for the ends of that range: the subtraction overflows when the two
// timestamps are far apart, and negating INT64_MIN has no representable answer
// at all. One hostile field was enough. The tests were green because every
// fixture supplied a plausible timestamp, which is the one kind of input this
// detector is not for.
void test_a_hostile_receiver_time_is_still_a_disagreement()
{
    constexpr std::int64_t lowest  = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t highest = std::numeric_limits<std::int64_t>::max();

    // The magnitude itself, first, because that is where the arithmetic lives
    // and a wrong answer here is invisible at the call site.
    CHECK(seconds_between(WallTime{0}, WallTime{0}) == 0);
    CHECK(seconds_between(WallTime{100}, WallTime{40}) == 60);
    CHECK(seconds_between(WallTime{40}, WallTime{100}) == 60);   // symmetric
    CHECK(seconds_between(WallTime{-40}, WallTime{20}) == 60);   // across the epoch

    // The two that used to be undefined. The gap between the ends of the range
    // is 2^64 - 1, which is exactly what a uint64 holds and what an int64
    // cannot — the reason the answer is unsigned.
    CHECK(seconds_between(WallTime{lowest}, WallTime{0}) ==
          static_cast<std::uint64_t>(highest) + 1U);
    CHECK(seconds_between(WallTime{lowest}, WallTime{highest}) == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(seconds_between(WallTime{highest}, WallTime{lowest}) == 0xFFFFFFFFFFFFFFFFULL);

    // And through the detector, which is the part a caller sees. Each of these
    // is a receiver claiming a time no receiver can honestly claim, and each
    // must raise the reason rather than trap or quietly wrap into agreement.
    const struct {
        std::int64_t receiver;
        std::int64_t device;
        const char*  what;
    } hostile[] = {
        {lowest,       0,             "the receiver reports the lowest representable instant"},
        {lowest + 30,  highest,       "both clocks at opposite ends of the range"},
        {highest,      1'700'000'000, "the receiver reports the highest representable instant"},
        {-1,           1'700'000'000, "the receiver reports a date before the epoch"},
    };

    for (const auto& one : hostile) {
        TrustEvaluator  evaluator;
        GnssObservation o     = good_fix(0);
        o.receiver_time       = WallTime{one.receiver};
        o.receiver_time_valid = true;
        evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, WallTime{one.device},
                          at(0));
        if (!evaluator.engine().holds(TrustReason::ClockDisagreement)) {
            std::fprintf(stderr, "FAIL: no disagreement when %s\n", one.what);
            ++failures;
        }
    }

    // The threshold is `>`, so exactly clock_disagreement_s apart is agreement.
    // Pinned here because the rewrite moved the comparison and a rewrite that
    // moves a boundary by one is the quiet kind of regression.
    const TrustPolicy policy = default_trust_policy();
    const struct {
        std::uint32_t apart;
        bool          disagrees;
    } boundary[] = {
        {policy.clock_disagreement_s,      false},
        {policy.clock_disagreement_s + 1U, true},
    };

    for (const auto& one : boundary) {
        TrustEvaluator  evaluator;
        GnssObservation o     = good_fix(0);
        o.receiver_time       = WallTime{1'700'000'000};
        o.receiver_time_valid = true;
        evaluator.observe(o, PositionValidity::Valid, MotionEvidence{},
                          WallTime{1'700'000'000 + static_cast<std::int64_t>(one.apart)}, at(0));
        if (evaluator.engine().holds(TrustReason::ClockDisagreement) != one.disagrees) {
            std::fprintf(stderr, "FAIL: %u seconds apart should%s disagree\n", one.apart,
                         one.disagrees ? "" : " not");
            ++failures;
        }
    }
}

// ADR-0011 §5: down is immediate, up is earned.
void test_recovery_is_held_and_descent_is_not()
{
    TrustEvaluator evaluator;
    GnssObservation jammed = good_fix(0);
    jammed.jamming         = ReceiverIndication::Warning;
    evaluator.observe(jammed, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Degraded);

    // The all-clear arrives. The state does not move on the strength of it.
    GnssObservation clear = good_fix(1000);
    evaluator.observe(clear, PositionValidity::Valid, MotionEvidence{}, {}, at(1000));
    CHECK_STATE(evaluator.state(), TrustState::Degraded);

    // Still not, one millisecond before the hold completes.
    evaluator.engine().update(at(1000 + 4999));
    CHECK_STATE(evaluator.state(), TrustState::Degraded);

    // And now.
    evaluator.engine().update(at(1000 + 5000));
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
}

// One step per hold. Untrusted climbs through Degraded rather than jumping
// back, because the intermediate state is what the interface shows while the
// evidence is being re-earned.
void test_untrusted_climbs_through_degraded()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverSpoofing, at(0));
    engine.update(at(0));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.clear(TrustReason::ReceiverSpoofing);
    engine.update(at(100));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.update(at(100 + 5000));
    CHECK_STATE(engine.state(), TrustState::Degraded);

    engine.update(at(100 + 5000 + 4999));
    CHECK_STATE(engine.state(), TrustState::Degraded);

    engine.update(at(100 + 5000 + 5000));
    CHECK_STATE(engine.state(), TrustState::Trusted);
}

// The hysteresis band. A score that has fallen below the degrade threshold but
// not to the recovery threshold moves in neither direction — that band is the
// whole point, and a policy that treated `< degrade_at` as recovery would flap
// once per epoch on a marginal fix.
void test_the_band_between_the_thresholds_does_not_move()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverSpoofing, at(0));  // 70
    engine.update(at(0));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.clear(TrustReason::ReceiverSpoofing);
    engine.report(TrustReason::StalePosition, at(0));  // 20: under 30, over 15
    engine.update(at(0));
    CHECK(engine.score() == 20);

    // Ten minutes of it, and it neither recovers nor degrades further.
    for (std::uint64_t ms = 1000; ms <= 600000; ms += 1000) {
        engine.report(TrustReason::StalePosition, at(ms));
        engine.update(at(ms));
    }
    CHECK_STATE(engine.state(), TrustState::Untrusted);
}

// Expiry and an all-clear are different facts. The TTL is silence; clear() is a
// detector positively saying the condition is over. Both end the evidence, and
// only one of them is information.
void test_silence_expires_but_only_after_the_ttl()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverJamming, at(0));
    engine.update(at(0));
    CHECK_REASON(engine, TrustReason::ReceiverJamming);

    engine.update(at(14999));
    CHECK_REASON(engine, TrustReason::ReceiverJamming);

    engine.update(at(15000));
    CHECK_NO_REASON(engine, TrustReason::ReceiverJamming);

    // An all-clear does not wait for the TTL.
    TrustEngine told;
    told.report(TrustReason::ReceiverJamming, at(0));
    told.update(at(0));
    told.clear(TrustReason::ReceiverJamming);
    told.update(at(1));
    CHECK_NO_REASON(told, TrustReason::ReceiverJamming);

    // And re-reporting a live reason refreshes it rather than counting twice —
    // evidence is a set, not a tally.
    TrustEngine insistent;
    for (std::uint64_t ms = 0; ms <= 60000; ms += 1000) {
        insistent.report(TrustReason::ReceiverJamming, at(ms));
        insistent.update(at(ms));
    }
    CHECK(insistent.score() == 35);
    CHECK_REASON(insistent, TrustReason::ReceiverJamming);
}

// THE REGRESSION TEST FOR THE OTHER HALF OF THAT SENTENCE.
//
// Expiry and an all-clear were different facts in `holds()` and the same fact
// in `evaluate()`. Twenty-five seconds of complete silence after a spoofing
// alarm therefore ended with the device asserting `Trusted` again: the TTL took
// the alarm out of the score at 15 s, the zero that was left started the clean
// hold, and the two holds ran on it. No observation, no clear(), nothing.
//
// The sequence is the one from the report, and the two assertions that matter
// are at 20 000 and 25 000 — before the fix they read Degraded and Trusted.
void test_silence_after_a_spoofing_alarm_never_restores_trust()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverSpoofing, at(0));
    engine.update(at(0));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    // The TTL. The allegation stops counting towards the score...
    engine.update(at(15000));
    CHECK(engine.score() == 0);
    CHECK_NO_REASON(engine, TrustReason::ReceiverSpoofing);

    // ...and does not thereby become an all-clear. It is an allegation nobody
    // withdrew, and it says which one it is.
    CHECK(engine.awaiting_confirmation(TrustReason::ReceiverSpoofing));
    CHECK(engine.unconfirmed_reasons() == trust_reason_bit(TrustReason::ReceiverSpoofing));

    engine.update(at(20000));
    CHECK_STATE(engine.state(), TrustState::Untrusted);
    engine.update(at(25000));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    // Five minutes of polling, which is what a service that keeps ticking looks
    // like. Calling update() is not evidence of anything.
    for (std::uint64_t ms = 26000; ms <= 300000; ms += 1000) {
        engine.update(at(ms));
    }
    CHECK_STATE(engine.state(), TrustState::Untrusted);
    CHECK(engine.awaiting_confirmation(TrustReason::ReceiverSpoofing));
    CHECK(engine.transitions_recorded() == 1);  // the descent, and nothing since
}

// The same door, one weight lower: jamming reaches Degraded, and silence used
// to carry it back to Trusted in twenty seconds.
void test_silence_after_a_jamming_alarm_never_restores_trust()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverJamming, at(0));
    engine.update(at(0));
    CHECK_STATE(engine.state(), TrustState::Degraded);

    engine.update(at(15000));
    CHECK(engine.score() == 0);
    CHECK(engine.awaiting_confirmation(TrustReason::ReceiverJamming));

    engine.update(at(20000));
    CHECK_STATE(engine.state(), TrustState::Degraded);

    for (std::uint64_t ms = 21000; ms <= 120000; ms += 1000) {
        engine.update(at(ms));
    }
    CHECK_STATE(engine.state(), TrustState::Degraded);
    CHECK(engine.transitions_recorded() == 1);
}

// What silence costs is the hold, not the recovery. When a detector does
// eventually say the condition is over, the state climbs — and the hold is
// measured from the retraction rather than from the quiet in front of it, so a
// minute of hearing nothing buys no part of the five seconds.
void test_a_retraction_after_the_silence_is_where_the_hold_starts()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverSpoofing, at(0));
    engine.update(at(0));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    for (std::uint64_t ms = 1000; ms <= 60000; ms += 1000) {
        engine.update(at(ms));
    }
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.clear(TrustReason::ReceiverSpoofing);
    engine.update(at(60000));
    CHECK(engine.unconfirmed_reasons() == 0);
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    // One step per hold, exactly as before — the retraction bought a recovery,
    // not a shortcut through Degraded.
    engine.update(at(64999));
    CHECK_STATE(engine.state(), TrustState::Untrusted);
    engine.update(at(65000));
    CHECK_STATE(engine.state(), TrustState::Degraded);
    engine.update(at(69999));
    CHECK_STATE(engine.state(), TrustState::Degraded);
    engine.update(at(70000));
    CHECK_STATE(engine.state(), TrustState::Trusted);
}

// A hold is discarded by new evidence, not paused by it. Three seconds of quiet
// followed by the alarm returning does not leave two seconds owing.
void test_new_evidence_during_a_hold_starts_it_over()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverSpoofing, at(0));
    engine.update(at(0));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.clear(TrustReason::ReceiverSpoofing);
    engine.update(at(1000));  // the hold starts here
    engine.update(at(4000));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.report(TrustReason::ReceiverSpoofing, at(4500));
    engine.update(at(4500));
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.clear(TrustReason::ReceiverSpoofing);
    engine.update(at(5000));  // and starts again here
    engine.update(at(6001));  // past the first hold, and it counts for nothing
    CHECK_STATE(engine.state(), TrustState::Untrusted);

    engine.update(at(10000));
    CHECK_STATE(engine.state(), TrustState::Degraded);
}

// The case this actually costs on a device, rather than in an engine driven by
// hand: a receiver that keeps producing perfectly good fixes and stops
// asserting anything about spoofing.
//
// `Unknown` and `Unsupported` are left alone rather than cleared, so the alarm
// stays live until the TTL and then lapses. Every fix here is a good one, so no
// other reason weighs anything and the score is a genuine zero from 15 s
// onwards — which is exactly the zero the old code promoted on. On the LS550G
// anti-spoofing is `UNKNOWN` (OD-5 §2), so this is not a hypothetical receiver.
void test_a_receiver_that_stops_asserting_does_not_recover_by_the_clock()
{
    TrustEvaluator evaluator;

    GnssObservation alarming = good_fix(0);
    alarming.spoofing        = ReceiverIndication::Critical;
    evaluator.observe(alarming, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    // A minute of them, one a second, drifting a few centimetres so that this
    // is a receiver at work rather than a frozen sample.
    for (std::uint64_t ms = 1000; ms <= 60000; ms += 1000) {
        GnssObservation quiet =
            good_fix(ms, kLat + static_cast<std::int32_t>(ms / 1000), kLon);
        quiet.spoofing = (ms % 2000 == 0) ? ReceiverIndication::Unknown
                                          : ReceiverIndication::Unsupported;
        evaluator.observe(quiet, PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
    }
    CHECK(evaluator.engine().score() == 0);
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);
    CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ReceiverSpoofing));

    // And the positive all-clear, which is information rather than silence.
    GnssObservation all_clear = good_fix(61000, kLat + 61, kLon);
    all_clear.spoofing        = ReceiverIndication::None;
    evaluator.observe(all_clear, PositionValidity::Valid, MotionEvidence{}, {}, at(61000));
    CHECK(evaluator.engine().unconfirmed_reasons() == 0);
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    evaluator.engine().update(at(66000));
    CHECK_STATE(evaluator.state(), TrustState::Degraded);
    evaluator.engine().update(at(71000));
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
}

// The surrounding call site, checked rather than assumed: a location service's
// own tick cannot manufacture a confirmation.
//
// refresh() reaches exactly two reasons — FixLost and StalePosition — and says
// nothing whatever about the receiver's spoofing alarm. It is handed `Valid`
// here, the most favourable answer classify() could give, so that nothing but
// the tick itself is under test: no other reason is holding the state down and
// the score is zero the whole way.
void test_the_polling_tick_is_not_a_confirmation()
{
    TrustEvaluator evaluator;

    GnssObservation alarming = good_fix(0);
    alarming.spoofing        = ReceiverIndication::Critical;
    evaluator.observe(alarming, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    for (std::uint64_t ms = 1000; ms <= 120000; ms += 1000) {
        evaluator.refresh(PositionValidity::Valid, at(ms));
    }
    CHECK(evaluator.engine().score() == 0);
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);
    CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ReceiverSpoofing));
}

// ADR-0011 §7: a diagnostic that fills the flash it was diagnosing is not a
// diagnostic. The log is bounded — and it says so, which is the part that
// matters, because "sixteen transitions" and "sixteen of forty" are different
// field reports.
void test_the_log_is_bounded_and_admits_it()
{
    TrustEngine engine;
    std::uint64_t ms = 0;

    // Each cycle is two transitions: down to Untrusted, then up through
    // Degraded and Trusted. Twelve cycles is far more than the log holds.
    for (int cycle = 0; cycle < 12; ++cycle) {
        engine.report(TrustReason::ReceiverSpoofing, at(ms));
        engine.update(at(ms));
        engine.clear(TrustReason::ReceiverSpoofing);
        ms += 6000;
        engine.update(at(ms));   // Untrusted -> Degraded
        ms += 6000;
        engine.update(at(ms));   // Degraded  -> Trusted
        ms += 1000;
    }

    CHECK(engine.transitions_logged() == TrustEngine::kLogCapacity);
    CHECK(engine.transitions_recorded() > TrustEngine::kLogCapacity);

    // Oldest first, and monotonic in time — a log that reads backwards after an
    // overflow is worse than none.
    for (std::size_t i = 1; i < engine.transitions_logged(); ++i) {
        CHECK(engine.transition(i - 1).at.ms <= engine.transition(i).at.ms);
    }

    // A transition carries the evidence that caused it, not just the states.
    bool any_reason = false;
    for (std::size_t i = 0; i < engine.transitions_logged(); ++i) {
        if (engine.transition(i).reasons != 0) {
            any_reason = true;
        }
    }
    CHECK(any_reason);
}

// A position that was good sixty seconds ago is a circle, not a point.
void test_the_last_trusted_position_becomes_a_circle()
{
    TrustEvaluator evaluator;
    GnssObservation o = good_fix(0);
    evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK(evaluator.engine().has_last_trusted());

    const std::uint32_t immediately = evaluator.engine().uncertainty_mm(at(0));
    CHECK(immediately == 3500);  // the receiver's own estimate, nothing added

    // 1.5 m/s of growth: a minute of not knowing is 90 m of not knowing.
    CHECK(evaluator.engine().uncertainty_mm(at(60000)) == 3500 + 90000);

    const std::optional<Position> remembered = evaluator.engine().last_trusted_position();
    CHECK(remembered.has_value());
    CHECK(remembered->latitude_e7 == kLat);
}

// A receiver that did not publish an accuracy has not published a good one.
// Falling back to zero would turn silence into a claim of exactness — the same
// mistake as reading an Unknown spoofing verdict as an all-clear, in the one
// number an application would draw a circle from.
void test_an_unstated_accuracy_is_not_a_perfect_one()
{
    TrustEvaluator evaluator;
    GnssObservation quiet = good_fix(0);
    quiet.horizontal_accuracy_mm.reset();
    // Still a clean, valid fix — this receiver simply does not publish the
    // field, which several do not.
    evaluator.observe(quiet, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK(evaluator.engine().has_last_trusted());

    const std::uint32_t immediately = evaluator.engine().uncertainty_mm(at(0));
    CHECK(immediately > 0);
    CHECK(immediately == default_trust_policy().assumed_accuracy_mm);

    // And it still grows, from that starting radius rather than from nothing.
    CHECK(evaluator.engine().uncertainty_mm(at(60000)) == immediately + 90000);
}

// The header warns callers about this and the warning deserves a witness: zero
// uncertainty with no remembered position means "no answer", not "certain".
void test_no_remembered_position_is_not_a_precise_one()
{
    TrustEngine engine;
    CHECK(!engine.has_last_trusted());
    CHECK(engine.uncertainty_mm(at(999999)) == 0);
    CHECK(!engine.last_trusted_position().has_value());
}

// A degraded fix is fine to display and a poor thing to fall back on for the
// next twenty minutes.
void test_only_a_trusted_and_valid_fix_is_remembered()
{
    {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Degraded, MotionEvidence{}, {}, at(0));
        CHECK(!evaluator.engine().has_last_trusted());
    }
    {
        TrustEvaluator evaluator;
        GnssObservation o = good_fix(0);
        o.jamming         = ReceiverIndication::Warning;
        evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
        CHECK_STATE(evaluator.state(), TrustState::Degraded);
        CHECK(!evaluator.engine().has_last_trusted());
    }
}

// Two providers can only disagree about the same moment. A node's position
// arriving after the watch's own fix has aged is measured against wherever the
// wearer was standing several minutes ago, and reporting disagreement there
// would be reporting that somebody walked.
//
// The absence of a comparison is silence, not an all-clear: a live
// ProviderDisagreement must be left standing rather than cleared, which is the
// same rule OD-5 applies to a receiver that stops reporting.
void test_two_providers_must_be_talking_about_the_same_moment()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    // Half a kilometre apart, but the watch's fix is four minutes old.
    GnssObservation late = good_fix(240000, kLat + 50000);
    late.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(late, at(240000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);

    // A live disagreement is not cleared by a comparison that could not be made.
    TrustEvaluator standing;
    standing.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    GnssObservation elsewhere = good_fix(0, kLat + 50000);
    elsewhere.source          = PositionSource::NodeGnss;
    standing.compare_provider(elsewhere, at(0));
    CHECK_REASON(standing.engine(), TrustReason::ProviderDisagreement);

    GnssObservation stale_other = good_fix(240000, kLat);
    stale_other.source          = PositionSource::NodeGnss;
    standing.compare_provider(stale_other, at(240000));
    CHECK_REASON(standing.engine(), TrustReason::ProviderDisagreement);
}

// AND THE ALLEGATION MUST NOT OUTLIVE EVERYTHING THAT COULD WITHDRAW IT.
//
// The test above passes only because it never calls `update()` between the two
// comparisons, so the TTL never runs and the reason stays LIVE. Run the clock
// and it lapses into `unconfirmed_` -- and `ProviderDisagreement` is the one
// reason whose only retraction sits BEHIND the freshness gate, so once that
// gate is closed nothing in the system can ever reach the `clear()`. The device
// is pinned below `Trusted` for the rest of the boot with `score() == 0`,
// `reasons() == 0` and no exit but a full `reset()`, which asserts `Trusted`
// outright and skips both holds.
//
// Neither half of the gate is exotic: `latest_position_at_` advances only
// inside `observe()`, and a duty-cycled receiver is what `gnss_power.h` is for;
// `other.observed_at` is a relayed fix's MEASUREMENT time, which
// `tests/replay/scenarios/14-a-relayed-fix-arrives-old.trace` records at 40 s
// when a stalled link delivers its backlog. Found in review of #153.
void test_a_disagreement_stops_being_awaited_when_it_can_no_longer_be_compared()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    GnssObservation elsewhere = good_fix(0, kLat + 50000);
    elsewhere.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(elsewhere, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK(evaluator.engine().state() != TrustState::Trusted);

    // The reproduction from the review: the receiver keeps running at 1 Hz, the
    // node never leaves, and each relayed frame was measured 8 s ago, so the
    // comparison window refuses it.
    //
    // The relayed position is deliberately the DISAGREEING one, not an agreeing
    // one. The second review round pointed out that an agreeing frame proves
    // nothing here: the gate closes before `distance_mm()` is ever computed, so
    // "every node fix agrees exactly" was a fact the code never learned and the
    // test would have passed identically either way. Sending the far-away fix
    // makes the assertion say what it actually means -- a frame outside the
    // window is not evidence in EITHER direction, whatever it contains -- and
    // states the bound this branch accepts: a node that is present and
    // permanently uncomparable does stop being awaited. That bound is T-152.
    const std::uint64_t window = evaluator.engine().policy().provider_comparison_window.value;
    const std::uint64_t behind = window + 3000;
    const std::uint64_t grace  = evaluator.engine().policy().provider_departure_grace.value;

    // Run to just inside the grace first. The fourth review round is why this
    // half exists: keyed on one uncomparable frame, the pin lifted the moment
    // the TTL had moved the bit, five seconds in. Uncomparable is not gone.
    for (std::uint64_t ms = 1000; ms <= grace; ms += 1000) {
        evaluator.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
        if (ms % 5000 == 0 && ms > behind) {
            GnssObservation too_old = good_fix(ms - behind, kLat + 50000);
            too_old.source          = PositionSource::NodeGnss;
            evaluator.compare_provider(too_old, at(ms));
        }
    }
    CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement) ||
          (evaluator.engine().reasons() & trust_reason_bit(TrustReason::ProviderDisagreement)) != 0);
    CHECK(evaluator.engine().state() != TrustState::Trusted);

    // Past it, and this really is a source that has stopped being one: every
    // frame it has sent for two minutes was measured outside the comparison
    // window, so the retraction it owes has been unreachable that whole time.
    for (std::uint64_t ms = grace + 1000; ms <= grace + 60000; ms += 1000) {
        evaluator.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
        if (ms % 5000 == 0) {
            GnssObservation too_old = good_fix(ms - behind, kLat + 50000);
            too_old.source          = PositionSource::NodeGnss;
            evaluator.compare_provider(too_old, at(ms));
        }
    }

    // Before this change the end state was Degraded with score 0, reasons 0 and
    // ProviderDisagreement awaited for ever -- a stuck device with nothing on a
    // screen to explain it.
    CHECK(!evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK(evaluator.engine().state() == TrustState::Trusted);
}

// The narrowness of that is the whole of its safety, so it is asserted rather
// than described. `stop_awaiting()` touches `unconfirmed_` alone: a LIVE
// allegation, whose evidence has not expired, is current evidence and an
// uncomparable frame must not talk the device out of it. Otherwise a node could
// disagree once and then clear itself by going uncomparable, which is the
// silence-is-an-all-clear bug this whole branch exists to remove.
void test_an_uncomparable_frame_does_not_withdraw_a_live_disagreement()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    GnssObservation elsewhere = good_fix(0, kLat + 50000);
    elsewhere.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(elsewhere, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);

    // Well inside the evidence TTL, so the reason is still live -- and an
    // uncomparable frame arrives. The node's fix agrees exactly; it is simply
    // too old to be evidence of anything, which is precisely the frame a node
    // that wanted to clear itself would send.
    const std::uint64_t window = evaluator.engine().policy().provider_comparison_window.value;
    const std::uint64_t soon   = window + 3000;
    CHECK(soon < evaluator.engine().policy().evidence_ttl.value);
    evaluator.observe(good_fix(soon), PositionValidity::Valid, MotionEvidence{}, {}, at(soon));

    GnssObservation uncomparable = good_fix(0, kLat);
    uncomparable.source          = PositionSource::NodeGnss;
    CHECK(soon > window);  // the frame really is outside the window
    evaluator.compare_provider(uncomparable, at(soon));

    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK(evaluator.engine().state() != TrustState::Trusted);
}

// The half of the gate that must NOT lift the pin, which the first version of
// this branch did not distinguish. `stop_awaiting()` means the detector's
// subject has gone; for `ProviderDisagreement` the subject is the second
// source. A gate closed because OUR OWN receiver went quiet is the device
// having stopped listening while a present, fresh, still-disagreeing node keeps
// talking -- so lifting the pin there releases the state on the one input that
// has not moved.
//
// The review's reproduction, followed to its end: duty-cycle the receiver off,
// let the node keep relaying fresh disagreeing fixes at 1 Hz, and before the
// fix the device reached `Trusted` about twenty seconds later and then stored
// the disputed coordinate as `last_trusted_position()`. Nothing exotic --
// duty-cycling the receiver is what `gnss_power.h` is for. Found in review of
// #153, second round.
void test_our_own_receiver_going_quiet_does_not_lift_a_node_s_allegation()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    GnssObservation elsewhere = good_fix(0, kLat + 50000);
    elsewhere.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(elsewhere, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);

    // From here the receiver says nothing at all: no observe(), so
    // `latest_position_at_` never advances. The node is fresh every second and
    // still 550 m away.
    //
    // `refresh()` is passed `Valid` on purpose. A duty-cycled receiver's last
    // fix classifies `Valid` for 30 s (`position.h`), and passing `Stale` would
    // put `StalePosition` -- weight 20, above `recover_below` -- on the score,
    // which holds the state down by itself and would make this test pass no
    // matter what `compare_provider()` did. The point is that the ALLEGATION
    // survives, so nothing else may be holding it up. It is `refresh()` rather
    // than nothing at all because the TTL runs there: without it the bit never
    // reaches `unconfirmed_` and the pin under test never forms.
    for (std::uint64_t ms = 1000; ms <= 60000; ms += 1000) {
        GnssObservation fresh_and_disagreeing = good_fix(ms, kLat + 50000);
        fresh_and_disagreeing.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(fresh_and_disagreeing, at(ms));
        evaluator.refresh(PositionValidity::Valid, at(ms));
    }

    // Never withdrawn: either still live, or lapsed and still awaited. And the
    // device never climbs back, with no other reason available to hold it.
    CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement) ||
          (evaluator.engine().reasons() & trust_reason_bit(TrustReason::ProviderDisagreement)) != 0);
    CHECK(evaluator.engine().state() != TrustState::Trusted);
}

// The two frames a node sends when it cannot answer: no fix at all, and a
// position `in_range` refuses. Both are used by the pair of tests below, which
// are the two halves of one question -- how long uncomparable has to last
// before it means gone.
const GnssObservation kNodeNoFix = []{
    GnssObservation o = good_fix(0);
    o.position.reset();
    return o;
}();
const GnssObservation kNodeOutOfRange = []{
    GnssObservation o = good_fix(0);
    o.position        = Position{1'000'000'000, 0};  // 100 degrees north
    return o;
}();

// THE FOURTH ROUND'S BLOCKING FINDING, as a test. A node whose receiver goes
// under canopy keeps relaying at 1 Hz with no position, and the previous commit
// read the first such frame as the node having gone: `stop_awaiting()` fired,
// the allegation nobody withdrew was lifted, the device reached `Trusted` about
// five seconds later, and `remember()` then committed the disputed coordinate
// as `last_trusted_position()`. No attacker, no hardware, our own receiver
// healthy at 1 Hz throughout -- and the test that stood here asserted exactly
// that outcome and called it correct.
//
// A receiver losing its fix is the most TRANSIENT of the three things the
// record calls a departure, not the most permanent: a doorway, a canopy, the
// node's own GNSS duty cycle. The retraction is not unreachable on this path,
// only deferred -- one frame with a fix and `compare_provider()` reports or
// clears -- which is verbatim the argument this same file uses to protect our
// own half. The two halves had different tests; now they have the same rule.
void test_a_node_under_cover_is_not_a_node_that_has_gone()
{
    for (int variant = 0; variant < 2; ++variant) {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

        GnssObservation elsewhere = good_fix(0, kLat + 50000);
        elsewhere.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(elsewhere, at(0));
        CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
        CHECK(evaluator.engine().state() != TrustState::Trusted);

        // 1 Hz from the node, fix-less, for just under the grace. The review's
        // table put the false all-clear at 21 s; this runs six times that.
        const std::uint64_t grace = evaluator.engine().policy().provider_departure_grace.value;
        for (std::uint64_t ms = 1000; ms < grace; ms += 1000) {
            evaluator.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
            GnssObservation useless = variant == 0 ? kNodeNoFix : kNodeOutOfRange;
            useless.source          = PositionSource::NodeGnss;
            useless.observed_at     = at(ms);
            evaluator.compare_provider(useless, at(ms));
        }

        // Never withdrawn: still live, or lapsed and still awaited. And the
        // device has not talked itself back into asserting the position the
        // node is still disputing.
        CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement) ||
              (evaluator.engine().reasons() & trust_reason_bit(TrustReason::ProviderDisagreement)) != 0);
        CHECK(evaluator.engine().state() != TrustState::Trusted);

        // And `remember()` ran exactly ONCE, at t=0, before the node ever
        // disagreed -- which is the half of the finding that outlives the
        // state. `remember()` is gated on `Trusted`, so a device that talks
        // itself back in re-commits the coordinate the node is disputing as
        // `last_trusted_position()`, and the circle around it collapses to
        // nothing. Reading the uncertainty is how that becomes visible: it has
        // grown for the whole span, so nothing re-stamped it partway.
        const std::uint64_t last_ms = grace - 1000;
        const std::uint32_t expected =
            3500 + static_cast<std::uint32_t>(last_ms / 1000) *
                       evaluator.engine().policy().uncertainty_growth_mm_s;
        CHECK(evaluator.engine().has_last_trusted());
        CHECK(evaluator.engine().uncertainty_mm(at(last_ms)) == expected);
    }
}

// The other half, and the reason the pin lifts at all: a second source that has
// been unable to answer for longer than a retraction could plausibly take has
// stopped being a second source. Without this the device is pinned for the rest
// of the boot with `score() == 0`, `reasons() == 0` and no exit but `reset()` --
// which is the failure this branch exists to remove, and the second round found
// the first version returning before `stop_awaiting()` on both these paths.
// The bound is T-152, and `provider_departure_grace` is `ESTIMATED`.
void test_a_node_uncomparable_past_the_grace_stops_being_awaited()
{
    for (int variant = 0; variant < 2; ++variant) {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

        GnssObservation elsewhere = good_fix(0, kLat + 50000);
        elsewhere.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(elsewhere, at(0));
        CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);

        const std::uint64_t grace = evaluator.engine().policy().provider_departure_grace.value;
        for (std::uint64_t ms = 1000; ms <= grace + 60000; ms += 1000) {
            evaluator.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
            if (ms % 5000 == 0) {
                GnssObservation useless = variant == 0 ? kNodeNoFix : kNodeOutOfRange;
                useless.source          = PositionSource::NodeGnss;
                useless.observed_at     = at(ms);
                evaluator.compare_provider(useless, at(ms));
            }
        }

        CHECK(!evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));
        CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
        CHECK(evaluator.engine().state() == TrustState::Trusted);
    }
}

// `provider_detached()` is the call that says the subject actually went, rather
// than inferring it from a frame that could not be compared. It lifts a lapsed
// allegation and -- like every other path here -- leaves a live one strictly
// alone, because a provider walking away is not a retraction.
void test_a_detached_provider_stops_being_awaited_and_cannot_clear_a_live_reason()
{
    TrustEvaluator lapsed;
    lapsed.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    GnssObservation elsewhere = good_fix(0, kLat + 50000);
    elsewhere.source          = PositionSource::NodeGnss;
    lapsed.compare_provider(elsewhere, at(0));
    CHECK_REASON(lapsed.engine(), TrustReason::ProviderDisagreement);

    // Past the TTL, so the bit has moved into `unconfirmed_` and the device is
    // pinned with nothing live.
    const std::uint64_t ttl = lapsed.engine().policy().evidence_ttl.value;
    for (std::uint64_t ms = 1000; ms <= ttl + 5000; ms += 1000) {
        lapsed.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
    }
    CHECK(lapsed.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));

    lapsed.provider_detached();
    CHECK(!lapsed.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));
    // Idempotent.
    lapsed.provider_detached();
    CHECK(!lapsed.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));

    TrustEvaluator live;
    live.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    GnssObservation far = good_fix(0, kLat + 50000);
    far.source          = PositionSource::NodeGnss;
    live.compare_provider(far, at(0));
    CHECK_REASON(live.engine(), TrustReason::ProviderDisagreement);
    live.provider_detached();
    CHECK_REASON(live.engine(), TrustReason::ProviderDisagreement);
    CHECK(live.engine().state() != TrustState::Trusted);

    // AND THE LATCH, which is what makes the call above worth making. The
    // documented trigger is an EDGE -- the node detached, the link dropped, the
    // capability was withdrawn -- and at that edge a disagreement reported
    // inside `evidence_ttl` is `live_`, which `stop_awaiting()` deliberately
    // does not touch. So the call did nothing, the TTL moved the bit into
    // `unconfirmed_` fifteen seconds later, and nothing called again because
    // the node had already gone: pinned `Degraded` for the rest of the boot,
    // reached through the very hook added to prevent it. Whether that happened
    // at all depended on whether the link-loss timeout exceeded `evidence_ttl`
    // -- an accidental coupling between two constants in two subsystems,
    // documented in neither. Found in the fourth review round of #153.
    //
    // Now the detach latches, so the expiry ends the allegation instead of
    // remembering it as unanswered: there is no longer anyone who could answer.
    const std::uint64_t live_ttl = live.engine().policy().evidence_ttl.value;
    for (std::uint64_t ms = 1000; ms <= live_ttl + 10000; ms += 1000) {
        live.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
    }
    CHECK(!live.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));
    CHECK_NO_REASON(live.engine(), TrustReason::ProviderDisagreement);
    CHECK(live.engine().state() == TrustState::Trusted);

    // The latch is about a SUBJECT, not a reason code: a node that comes back
    // and disagrees again is a new subject, so the allegation stands and is
    // awaited exactly as before. Otherwise one detach would deafen the device
    // to that reason for the rest of the boot, which is the opposite failure.
    GnssObservation again = good_fix(60000, kLat + 50000);
    again.source          = PositionSource::NodeGnss;
    live.observe(good_fix(60000), PositionValidity::Valid, MotionEvidence{}, {}, at(60000));
    live.compare_provider(again, at(60000));
    CHECK_REASON(live.engine(), TrustReason::ProviderDisagreement);
    CHECK(live.engine().state() != TrustState::Trusted);
    for (std::uint64_t ms = 61000; ms <= 60000 + live_ttl + 2000; ms += 1000) {
        live.observe(good_fix(ms), PositionValidity::Valid, MotionEvidence{}, {}, at(ms));
    }
    CHECK(live.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));
}

// Disagreement between two providers is evidence about both of them and belongs
// to neither — the comment on `compare_provider` in `trust.h`. What ADR-0011
// governs here is §5, which requires a reason code to record *which* evidence
// moved the state; §4 is *Differential corrections belong to a provider, not to
// GNSS* and an earlier version of this comment cited it.
void test_provider_disagreement_is_evidence_about_both()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

    GnssObservation elsewhere = good_fix(0, kLat + 50000);  // ~550 m away
    elsewhere.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(elsewhere, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);

    // Two providers that agree to within the tolerance are not evidence of
    // anything, and must not be made to look like it.
    TrustEvaluator agreeing;
    agreeing.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    GnssObservation nearby = good_fix(0, kLat + 500);  // ~5 m
    nearby.source          = PositionSource::NodeGnss;
    agreeing.compare_provider(nearby, at(0));
    CHECK_NO_REASON(agreeing.engine(), TrustReason::ProviderDisagreement);
}

// A receiver that has lost its fix keeps the last coordinate it solved for in
// the position field — the frame
// `test_retained_coordinate_no_fix_does_not_move_the_baseline` above already
// reproduces, arriving at the other consumer of the same field.
//
// `satellites_used` is left UNSAID rather than zero, which is the one liberty
// these fixtures take. A real receiver reporting zero satellites used also
// raises `InsufficientSatellites`, correctly and for its own reasons; saying
// nothing keeps the score to exactly the reasons under test, so an assertion
// about a number means what it says.
GnssObservation retained_no_fix(std::uint64_t ms, std::int32_t lat = kLat)
{
    GnssObservation o = good_fix(ms, lat);
    o.fix_type        = FixType::NoFix;
    o.satellites_used.reset();
    return o;
}

// AND THAT COORDINATE IS NOT THIS DEVICE'S ANSWER TO A SECOND PROVIDER.
//
// The evaluator held two models of the same observation's fitness. The rate
// baselines read `validity` and refused retained state; the local side of
// `compare_provider()` took the last in-range position field unconditionally
// and stamped it with the ARRIVAL time, so at 1 Hz a receiver with no fix kept
// that side permanently fresh. A node that still had a fix, reporting the place
// the wearer had actually walked to, was then measured against a coordinate the
// local receiver had disowned, and the difference reported as
// `ProviderDisagreement`. Found by the review of `6965191..8d757a7`, #178.
//
// The state assertion is the point. `ProviderDisagreement` weighs 30, which
// reaches `degrade_at` unaided, so the reason alone moved the device to
// `Degraded` and every retained frame refreshed it: not one transient epoch but
// the whole dropout, with a diagnostic screen naming a conflict between two
// providers only one of which had a position at all.
void test_a_retained_coordinate_does_not_answer_a_second_provider()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Trusted);

    const std::uint64_t window = evaluator.engine().policy().provider_comparison_window.value;

    // The fix goes at t=1 s and the coordinate stays on the wire. The wearer
    // walks to B, 550 m away; the node still has a fix and says so, once a
    // second for a minute.
    //
    // The node is not consulted until our own last real measurement is outside
    // the comparison window, because inside it the disagreement would be REAL
    // and reporting it would be right: a device that measured A four seconds
    // ago cannot be 550 m from it now, and one of the two sources is wrong.
    for (std::uint64_t ms = 1000; ms <= 60000; ms += 1000) {
        evaluator.observe(retained_no_fix(ms), PositionValidity::NoFix, MotionEvidence{}, {},
                          at(ms));
        if (ms > window) {
            GnssObservation node_at_b = good_fix(ms, kLat + 50000);
            node_at_b.source          = PositionSource::NodeGnss;
            evaluator.compare_provider(node_at_b, at(ms));
        }
    }

    // Sixty frames saying "there is no position at all", and not one of them
    // was made to contradict a node that was right.
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK(!evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));

    // What is on the score is the local receiver's own evidence and nothing
    // else. Before the fix it was that plus 30, which is 50 and a `Degraded`
    // this device had not earned.
    const TrustPolicy&  policy   = evaluator.engine().policy();
    const std::uint16_t fix_lost = policy.weight[static_cast<std::uint8_t>(TrustReason::FixLost)];
    CHECK_REASON(evaluator.engine(), TrustReason::FixLost);
    CHECK(evaluator.engine().score() == fix_lost);

    // `Trusted` with no position is not a contradiction: trust and validity are
    // separate questions (`position.h`), and this answers the first one only —
    // nothing here is lying. The interface is told there is no position by
    // `PositionValidity::NoFix`, which is what that enumerator is for.
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
}

// `Stale` is the same refusal for the other reason, and it needs saying twice
// because the two are stopped by different halves of the same rule.
//
// A relayed fix that was already old on arrival is refused by its measurement
// age — `14-a-relayed-fix-arrives-old.trace` records exactly that at 40 s for a
// link that queues and retries. A fix a *consumer* calls too old to act on may
// still be inside the comparison window, and is refused by its validity alone:
// `PositionValidity` is the caller's verdict, not the evaluator's, `classify()`
// takes a `ValidityPolicy` whose `stale_after` is policy rather than physics,
// and what the evaluator does with `Stale` cannot depend on which threshold
// produced it.
void test_a_stale_coordinate_is_not_a_fresh_side_of_the_comparison()
{
    {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));

        // Measured at t=20 s, delivered at t=60 s: forty seconds in a backlog,
        // which `classify()` calls `Stale` under the default 30 s.
        evaluator.observe(good_fix(20000), PositionValidity::Stale, MotionEvidence{}, {},
                          at(60000));

        GnssObservation node_at_b = good_fix(60000, kLat + 50000);
        node_at_b.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(node_at_b, at(60000));

        CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
        CHECK_REASON(evaluator.engine(), TrustReason::StalePosition);
    }
    {
        TrustEvaluator evaluator;
        // Three seconds old and inside the five-second comparison window, so
        // the measurement age would let this through. The service that fed it
        // has already said it is too old to act on.
        const std::uint64_t window = evaluator.engine().policy().provider_comparison_window.value;
        CHECK(3000 < window);
        evaluator.observe(good_fix(0), PositionValidity::Stale, MotionEvidence{}, {}, at(3000));

        GnssObservation node_at_b = good_fix(3000, kLat + 50000);
        node_at_b.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(node_at_b, at(3000));

        CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    }
}

// And the gate is not simply shut, which is the assertion that keeps the two
// above honest: a refusal that refused everything would satisfy them both and
// switch the detector off.
//
// `Degraded` answers as readily as `Valid` — it is a caveat the interface must
// show, not a receiver declining to say where it is — and both halves are
// checked in both directions, so that neither an invented agreement nor a
// missed disagreement passes.
void test_a_measured_fix_still_answers_whether_valid_or_degraded()
{
    for (int variant = 0; variant < 2; ++variant) {
        const PositionValidity validity =
            variant == 0 ? PositionValidity::Valid : PositionValidity::Degraded;
        GnssObservation local = good_fix(0);
        if (variant == 1) {
            local.fix_type = FixType::TwoD;  // no altitude solution: what classify() calls Degraded
        }

        TrustEvaluator disagreeing;
        disagreeing.observe(local, validity, MotionEvidence{}, {}, at(0));
        GnssObservation far = good_fix(0, kLat + 50000);  // ~550 m
        far.source          = PositionSource::NodeGnss;
        disagreeing.compare_provider(far, at(0));
        CHECK_REASON(disagreeing.engine(), TrustReason::ProviderDisagreement);

        TrustEvaluator agreeing;
        agreeing.observe(local, validity, MotionEvidence{}, {}, at(0));
        GnssObservation close_by = good_fix(0, kLat + 500);  // ~5 m
        close_by.source          = PositionSource::NodeGnss;
        agreeing.compare_provider(close_by, at(0));
        CHECK_NO_REASON(agreeing.engine(), TrustReason::ProviderDisagreement);
    }
}

// NOT BEING ABLE TO ANSWER IS NOT A RETRACTION — and on this path the previous
// code was not fail-safe, it was the other direction.
//
// A node that reports the coordinate the local receiver is still retaining
// "agrees" with it. The comparison was made, `set()` took the false branch, and
// `clear()` — the one retraction this class has, and the only thing that lets
// the state climb — was reached by a position nobody was asserting. Silence
// from our own half must leave a live allegation exactly where it is.
void test_a_local_dropout_does_not_withdraw_a_live_disagreement()
{
    for (int variant = 0; variant < 2; ++variant) {
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
        GnssObservation elsewhere = good_fix(0, kLat + 50000);
        elsewhere.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(elsewhere, at(0));
        CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);

        // Past the comparison window, so our own last measurement can no longer
        // answer, and well inside `evidence_ttl`, so the allegation is LIVE and
        // this is a test about a retraction rather than about an expiry.
        const std::uint64_t soon = 6000;
        CHECK(soon > evaluator.engine().policy().provider_comparison_window.value);
        CHECK(soon < evaluator.engine().policy().evidence_ttl.value);

        if (variant == 0) {
            evaluator.observe(retained_no_fix(soon), PositionValidity::NoFix, MotionEvidence{}, {},
                              at(soon));
        } else {
            // The consumer's `Stale`, as above: its own `stale_after`, not ours.
            evaluator.observe(good_fix(0), PositionValidity::Stale, MotionEvidence{}, {}, at(soon));
        }

        GnssObservation agreeing_with_the_retained = good_fix(soon, kLat);
        agreeing_with_the_retained.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(agreeing_with_the_retained, at(soon));

        CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
        CHECK(evaluator.state() != TrustState::Trusted);
    }
}

// And the comparison reopens on the next real fix, in both directions. A gate
// that closed for the rest of the boot would be the pin #153 exists to remove,
// arrived at from the other side: the local receiver is the one source this
// device can always ask again.
void test_the_comparison_reopens_on_the_next_real_fix()
{
    TrustEvaluator evaluator;
    evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    GnssObservation elsewhere = good_fix(0, kLat + 50000);
    elsewhere.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(elsewhere, at(0));
    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK_STATE(evaluator.state(), TrustState::Degraded);

    // A minute of retained no-fix frames while the node keeps saying B. The
    // allegation is never withdrawn — it lapses into `unconfirmed_` when the
    // TTL runs and stays awaited, because the node is still there to withdraw
    // it and has not.
    for (std::uint64_t ms = 1000; ms <= 60000; ms += 1000) {
        evaluator.observe(retained_no_fix(ms), PositionValidity::NoFix, MotionEvidence{}, {},
                          at(ms));
        GnssObservation node_at_b = good_fix(ms, kLat + 50000);
        node_at_b.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(node_at_b, at(ms));
    }
    CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));
    CHECK(evaluator.state() != TrustState::Trusted);

    // The receiver solves a fix again, and it is at B — which is where the
    // wearer was the whole time and what the node had been saying. 550 m over
    // sixty-one seconds is 9 m/s, so nothing here is a jump.
    evaluator.observe(good_fix(61000, kLat + 50000), PositionValidity::Valid, MotionEvidence{}, {},
                      at(61000));
    GnssObservation node_still_at_b = good_fix(61000, kLat + 50000);
    node_still_at_b.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(node_still_at_b, at(61000));

    // An explicit agreement, which is a retraction and not an expiry: the bit
    // is gone from both masks, and only that lets the hold start.
    CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK(!evaluator.engine().awaiting_confirmation(TrustReason::ProviderDisagreement));

    for (std::uint64_t ms = 62000; ms <= 67000; ms += 1000) {
        evaluator.observe(good_fix(ms, kLat + 50000), PositionValidity::Valid, MotionEvidence{}, {},
                          at(ms));
    }
    CHECK_STATE(evaluator.state(), TrustState::Trusted);

    // And a real disagreement is reported again straight away. The gate was
    // shut for the dropout, not for the session.
    evaluator.observe(good_fix(68000, kLat + 50000), PositionValidity::Valid, MotionEvidence{}, {},
                      at(68000));
    GnssObservation node_at_c = good_fix(68000, kLat + 200000);  // ~1.6 km from B
    node_at_c.source          = PositionSource::NodeGnss;
    evaluator.compare_provider(node_at_c, at(68000));
    CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    CHECK_STATE(evaluator.state(), TrustState::Degraded);
}

// The other half of the same invariant: which clock the local side is judged
// by. A measurement age, symmetrical with `other.observed_at` on the far side,
// and never the moment the observation happened to be processed.
//
// `observe()` is fed by whoever holds the receiver, and that is not always
// prompt: a driver draining a FIFO, or a service catching up after a busy tick,
// delivers a fix measured seconds before it is looked at.
// `14-a-relayed-fix-arrives-old.trace` records the same shape at 40 s for a
// node link that queues and retries.
void test_the_local_side_of_a_comparison_is_a_measurement_age()
{
    {
        // Twenty seconds in flight. Still `Valid` — the default `stale_after`
        // is 30 s — and not a fresh answer to anything.
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(0), PositionValidity::Valid, MotionEvidence{}, {}, at(20000));
        GnssObservation node = good_fix(20000, kLat + 50000);
        node.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(node, at(20000));
        CHECK_NO_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    }
    {
        // Two seconds in flight, well inside the window, and the disagreement
        // is real and reported. Without this the test above would pass on a
        // gate that had simply been welded shut.
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(18000), PositionValidity::Valid, MotionEvidence{}, {},
                          at(20000));
        GnssObservation node = good_fix(20000, kLat + 50000);
        node.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(node, at(20000));
        CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    }
    {
        // And a sample delivered out of order does not displace the measurement
        // already held. Had the reordered one been adopted its age would be 6 s
        // — outside the window — and a node's real disagreement would have been
        // lost to a late delivery rather than judged.
        TrustEvaluator evaluator;
        evaluator.observe(good_fix(10000), PositionValidity::Valid, MotionEvidence{}, {},
                          at(10000));
        evaluator.observe(good_fix(4000), PositionValidity::Valid, MotionEvidence{}, {}, at(10000));
        GnssObservation node = good_fix(10000, kLat + 50000);
        node.source          = PositionSource::NodeGnss;
        evaluator.compare_provider(node, at(10000));
        CHECK_REASON(evaluator.engine(), TrustReason::ProviderDisagreement);
    }
}

// ADR-0005 §5: no state survives a reconnect implicitly. When a provider
// detaches, what it
// told us goes with it — including the fallback position, which would otherwise
// be a stranger's idea of where this device is.
void test_reset_leaves_nothing_behind()
{
    TrustEvaluator evaluator;
    GnssObservation o = good_fix(0);
    o.spoofing        = ReceiverIndication::Critical;
    evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    // Past the TTL, so there is an allegation nobody withdrew for the reset to
    // leave behind as well. It is the one piece of state that would otherwise
    // outlive a detach and pin the next provider's position to Untrusted for
    // something the previous one reported.
    evaluator.engine().update(at(16000));
    CHECK(evaluator.engine().awaiting_confirmation(TrustReason::ReceiverSpoofing));

    evaluator.reset();
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
    CHECK(evaluator.engine().reasons() == 0);
    CHECK(evaluator.engine().unconfirmed_reasons() == 0);
    CHECK(evaluator.engine().score() == 0);
    CHECK(!evaluator.engine().has_last_trusted());
    CHECK(evaluator.engine().transitions_recorded() == 0);
    CHECK(evaluator.engine().transitions_logged() == 0);

    // And the rate detectors start again rather than comparing against a fix
    // from before the reset: a first observation after a detach cannot be a
    // jump, because there is nothing to have jumped from.
    evaluator.observe(good_fix(17000, kLat + 100000), PositionValidity::Valid, MotionEvidence{},
                      {}, at(17000));
    CHECK_NO_REASON(evaluator.engine(), TrustReason::PositionJump);
}

// Every reason and every state prints as something a person can read. A field
// report full of `TrustReason(7)` is a field report nobody acts on.
void test_everything_has_a_name()
{
    for (std::uint8_t i = 0; i < kTrustReasonCount; ++i) {
        const char* name = to_string(static_cast<TrustReason>(i));
        CHECK(name != nullptr && name[0] != '\0');
    }
    CHECK(to_string(TrustState::Trusted)[0] != '\0');
    CHECK(to_string(TrustState::Degraded)[0] != '\0');
    CHECK(to_string(TrustState::Untrusted)[0] != '\0');
}

}  // namespace

int main()
{
    test_a_clean_fix_is_trusted();
    test_the_receiver_alarms_are_weighted_differently();
    test_unknown_is_not_an_all_clear();
    test_the_interval_is_taken_before_it_is_overwritten();
    test_a_fix_dropout_is_not_a_teleport();
    test_the_altitude_rate_has_its_own_clock();
    test_the_same_distance_over_a_longer_interval_is_a_walk();
    test_the_altitude_rate_uses_that_same_interval();
    test_relayed_fix_is_measured_not_by_arrival_time();
    test_altitude_rate_is_measured_not_by_arrival_time();
    test_retained_coordinate_no_fix_does_not_move_the_baseline();
    test_no_fix_altitude_sample_does_not_move_the_baseline();
    test_equal_measurement_timestamps_are_handled_safely();
    test_out_of_order_observation_does_not_poison_the_baseline();
    test_a_future_dated_observation_is_rejected_without_freezing_the_baseline();
    test_a_reordered_sample_is_still_checked_against_the_standing_baseline();
    test_a_future_dated_sample_is_checked_as_well_as_refused();
    test_a_future_dated_altitude_is_checked_as_well_as_refused();
    test_a_still_wrist_is_evidence_and_an_unasked_one_is_not();
    test_motion_evidence_is_scoped_to_the_position_body();
    test_a_change_of_body_is_not_a_movement();
    test_satellites_used_cannot_exceed_satellites_in_view();
    test_receiver_time_is_compared_against_device_time();
    test_a_hostile_receiver_time_is_still_a_disagreement();
    test_recovery_is_held_and_descent_is_not();
    test_untrusted_climbs_through_degraded();
    test_the_band_between_the_thresholds_does_not_move();
    test_silence_expires_but_only_after_the_ttl();
    test_silence_after_a_spoofing_alarm_never_restores_trust();
    test_silence_after_a_jamming_alarm_never_restores_trust();
    test_a_retraction_after_the_silence_is_where_the_hold_starts();
    test_new_evidence_during_a_hold_starts_it_over();
    test_a_receiver_that_stops_asserting_does_not_recover_by_the_clock();
    test_the_polling_tick_is_not_a_confirmation();
    test_the_log_is_bounded_and_admits_it();
    test_the_last_trusted_position_becomes_a_circle();
    test_an_unstated_accuracy_is_not_a_perfect_one();
    test_no_remembered_position_is_not_a_precise_one();
    test_only_a_trusted_and_valid_fix_is_remembered();
    test_two_providers_must_be_talking_about_the_same_moment();
    test_a_disagreement_stops_being_awaited_when_it_can_no_longer_be_compared();
    test_an_uncomparable_frame_does_not_withdraw_a_live_disagreement();
    test_our_own_receiver_going_quiet_does_not_lift_a_node_s_allegation();
    test_a_node_under_cover_is_not_a_node_that_has_gone();
    test_a_node_uncomparable_past_the_grace_stops_being_awaited();
    test_a_detached_provider_stops_being_awaited_and_cannot_clear_a_live_reason();
    test_provider_disagreement_is_evidence_about_both();
    test_a_retained_coordinate_does_not_answer_a_second_provider();
    test_a_stale_coordinate_is_not_a_fresh_side_of_the_comparison();
    test_a_measured_fix_still_answers_whether_valid_or_degraded();
    test_a_local_dropout_does_not_withdraw_a_live_disagreement();
    test_the_comparison_reopens_on_the_next_real_fix();
    test_the_local_side_of_a_comparison_is_a_measurement_age();
    test_reset_leaves_nothing_behind();
    test_everything_has_a_name();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("trust: all checks passed (host only — no receiver involved)\n");
    return 0;
}
