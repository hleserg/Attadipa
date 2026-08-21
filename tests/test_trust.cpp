#include <cstdio>

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
    const MotionEvidence walking{true, true};

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

// The canonical detector, and the one ADR-0011 names the BMA423 for. Note what
// is being asserted: `known == false` is not evidence of stillness. A device
// that has not asked the accelerometer knows nothing, and must not treat that
// as an answer.
void test_a_still_wrist_is_evidence_and_an_unasked_one_is_not()
{
    const MotionEvidence still{true, false};
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

// Disagreement between two providers is evidence about both of them and belongs
// to neither — ADR-0011 §4.
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

// ADR-0004 §3: no state survives implicitly. When a provider detaches, what it
// told us goes with it — including the fallback position, which would otherwise
// be a stranger's idea of where this device is.
void test_reset_leaves_nothing_behind()
{
    TrustEvaluator evaluator;
    GnssObservation o = good_fix(0);
    o.spoofing        = ReceiverIndication::Critical;
    evaluator.observe(o, PositionValidity::Valid, MotionEvidence{}, {}, at(0));
    CHECK_STATE(evaluator.state(), TrustState::Untrusted);

    evaluator.reset();
    CHECK_STATE(evaluator.state(), TrustState::Trusted);
    CHECK(evaluator.engine().reasons() == 0);
    CHECK(evaluator.engine().score() == 0);
    CHECK(!evaluator.engine().has_last_trusted());
    CHECK(evaluator.engine().transitions_recorded() == 0);
    CHECK(evaluator.engine().transitions_logged() == 0);

    // And the rate detectors start again rather than comparing against a fix
    // from before the reset: a first observation after a detach cannot be a
    // jump, because there is nothing to have jumped from.
    evaluator.observe(good_fix(1000, kLat + 100000), PositionValidity::Valid, MotionEvidence{}, {},
                      at(1000));
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
    test_a_still_wrist_is_evidence_and_an_unasked_one_is_not();
    test_satellites_used_cannot_exceed_satellites_in_view();
    test_receiver_time_is_compared_against_device_time();
    test_recovery_is_held_and_descent_is_not();
    test_untrusted_climbs_through_degraded();
    test_the_band_between_the_thresholds_does_not_move();
    test_silence_expires_but_only_after_the_ttl();
    test_the_log_is_bounded_and_admits_it();
    test_the_last_trusted_position_becomes_a_circle();
    test_an_unstated_accuracy_is_not_a_perfect_one();
    test_no_remembered_position_is_not_a_precise_one();
    test_only_a_trusted_and_valid_fix_is_remembered();
    test_two_providers_must_be_talking_about_the_same_moment();
    test_provider_disagreement_is_evidence_about_both();
    test_reset_leaves_nothing_behind();
    test_everything_has_a_name();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("trust: all checks passed (host only — no receiver involved)\n");
    return 0;
}
