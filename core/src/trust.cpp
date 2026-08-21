#include "attadipa/core/trust.h"

#include "attadipa/core/geo.h"

namespace attadipa::core {
namespace {

constexpr std::uint8_t index_of(TrustReason reason)
{
    return static_cast<std::uint8_t>(reason);
}

// Untrusted < Degraded < Trusted, so "worse" is a comparison rather than a
// table. The enum is declared in that order for exactly this.
constexpr bool worse_than(TrustState a, TrustState b)
{
    return static_cast<std::uint8_t>(a) < static_cast<std::uint8_t>(b);
}

constexpr TrustState one_better(TrustState state)
{
    switch (state) {
        case TrustState::Untrusted: return TrustState::Degraded;
        case TrustState::Degraded:  return TrustState::Trusted;
        case TrustState::Trusted:   return TrustState::Trusted;
    }
    return TrustState::Trusted;
}

// Report or clear in one place, so a detector cannot accidentally latch a
// condition by only ever calling report().
void set(TrustEngine& engine, TrustReason reason, bool active, MonotonicTime now)
{
    if (active) {
        engine.report(reason, now);
    } else {
        engine.clear(reason);
    }
}

std::uint32_t saturating_add(std::uint32_t a, std::uint32_t b)
{
    const std::uint64_t sum = static_cast<std::uint64_t>(a) + b;
    return sum > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(sum);
}

}  // namespace

TrustPolicy default_trust_policy()
{
    TrustPolicy policy;

    // The ordering of these weights is the argument, not the values.
    //
    // The receiver's own spoofing alarm alone reaches Untrusted, because it is
    // the one input that can see the correlator. Jamming alone reaches Degraded
    // and no further: a jammed receiver is being denied, not lied to, and the
    // honest response is a caveat rather than a refusal. And a large movement
    // while the accelerometer says the wrist is still — the canonical case —
    // is Degraded alone and Untrusted the moment anything else agrees with it.
    policy.weight[index_of(TrustReason::ReceiverSpoofing)]        = 70;
    policy.weight[index_of(TrustReason::MotionDisagreement)]      = 45;
    policy.weight[index_of(TrustReason::PositionJump)]            = 40;
    policy.weight[index_of(TrustReason::ImplausibleSpeed)]        = 40;
    policy.weight[index_of(TrustReason::ReceiverJamming)]         = 35;
    policy.weight[index_of(TrustReason::ProtectionLevelInvalid)]  = 30;
    policy.weight[index_of(TrustReason::ClockDisagreement)]       = 30;
    policy.weight[index_of(TrustReason::ProviderDisagreement)]    = 30;
    policy.weight[index_of(TrustReason::ProtectionLevelExceeded)] = 25;
    policy.weight[index_of(TrustReason::ImplausibleAltitudeRate)] = 25;
    policy.weight[index_of(TrustReason::ConstellationAnomaly)]    = 25;
    policy.weight[index_of(TrustReason::StalePosition)]           = 20;
    policy.weight[index_of(TrustReason::FixLost)]                 = 20;
    policy.weight[index_of(TrustReason::AccuracyPoor)]            = 15;
    policy.weight[index_of(TrustReason::InsufficientSatellites)]  = 15;

    return policy;
}

// ---------------------------------------------------------------------------
// TrustEngine

TrustEngine::TrustEngine(const TrustPolicy& policy) : policy_(policy) {}

void TrustEngine::report(TrustReason reason, MonotonicTime at)
{
    evidence_at_[index_of(reason)] = at;
    live_ |= trust_reason_bit(reason);
}

void TrustEngine::clear(TrustReason reason)
{
    live_ &= ~trust_reason_bit(reason);
}

bool TrustEngine::holds(TrustReason reason) const
{
    return (live_ & trust_reason_bit(reason)) != 0;
}

void TrustEngine::update(MonotonicTime now)
{
    // Silence is not the same as an all-clear, but it cannot mean "forever
    // suspect" either — a device that walks out of an interference source would
    // never recover. Evidence therefore decays, and a detector that wants a
    // condition to persist has to keep saying so.
    for (std::uint8_t i = 0; i < kTrustReasonCount; ++i) {
        const std::uint32_t bit = 1u << i;
        if ((live_ & bit) != 0 && elapsed(evidence_at_[i], now) >= policy_.evidence_ttl) {
            live_ &= ~bit;
        }
    }

    std::uint32_t total = 0;
    for (std::uint8_t i = 0; i < kTrustReasonCount; ++i) {
        if ((live_ & (1u << i)) != 0) {
            total += policy_.weight[i];
        }
    }
    score_ = total > 0xFFFFU ? 0xFFFFU : static_cast<std::uint16_t>(total);

    evaluate(now);
}

void TrustEngine::evaluate(MonotonicTime now)
{
    TrustState target = TrustState::Trusted;
    if (score_ >= policy_.untrust_at) {
        target = TrustState::Untrusted;
    } else if (score_ >= policy_.degrade_at) {
        target = TrustState::Degraded;
    }

    // Down is immediate. This is a safety property: the moment the evidence
    // says the position may be wrong, the interface must stop asserting it, and
    // waiting out a hold first would be waiting while pointing the wrong way.
    if (worse_than(target, state_)) {
        clean_since_valid_ = false;
        enter(target, now);
        return;
    }

    // Up is earned. The score has to be *clean* — below the recovery
    // threshold, not merely below the degrade threshold — and stay there. The
    // band between recover_below and degrade_at is the hysteresis, and a state
    // sitting inside it does not move in either direction.
    if (score_ > policy_.recover_below) {
        clean_since_valid_ = false;
        return;
    }

    if (!clean_since_valid_) {
        clean_since_valid_ = true;
        clean_since_       = now;
        return;
    }

    if (state_ != TrustState::Trusted && elapsed(clean_since_, now) >= policy_.recover_hold) {
        // One step per hold, so Untrusted climbs through Degraded rather than
        // jumping straight back to Trusted. The intermediate state is not
        // ceremony: it is what the interface shows while the evidence is being
        // re-earned.
        enter(one_better(state_), now);
        clean_since_ = now;
    }
}

void TrustEngine::enter(TrustState next, MonotonicTime now)
{
    if (next == state_) {
        return;
    }

    Transition entry;
    entry.from    = state_;
    entry.to      = next;
    entry.reasons = live_;
    entry.at      = now;

    log_[recorded_ % kLogCapacity] = entry;
    ++recorded_;

    state_ = next;
}

void TrustEngine::remember(const GnssObservation& observation, MonotonicTime now)
{
    if (!observation.position.has_value() || !in_range(*observation.position)) {
        return;
    }
    has_last_trusted_ = true;
    last_trusted_     = *observation.position;
    last_trusted_at_  = now;
    // A receiver that did not publish an accuracy has not published a good one.
    // Falling back to zero would turn silence into a claim of exactness, which
    // is the same mistake as reading an Unknown spoofing verdict as an
    // all-clear — see policy_.assumed_accuracy_mm.
    last_trusted_accuracy_mm_ = observation.horizontal_accuracy_mm.has_value()
                                    ? *observation.horizontal_accuracy_mm
                                    : policy_.assumed_accuracy_mm;
}

std::optional<Position> TrustEngine::last_trusted_position() const
{
    if (!has_last_trusted_) {
        return std::nullopt;
    }
    return last_trusted_;
}

std::uint32_t TrustEngine::uncertainty_mm(MonotonicTime now) const
{
    if (!has_last_trusted_) {
        return 0;  // not "certain" — no answer. Check has_last_trusted() first.
    }
    const std::uint64_t seconds = elapsed(last_trusted_at_, now).value / 1000ULL;
    const std::uint64_t grown   = seconds * policy_.uncertainty_growth_mm_s;
    const std::uint32_t capped =
        grown > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(grown);
    return saturating_add(last_trusted_accuracy_mm_, capped);
}

std::size_t TrustEngine::transitions_logged() const
{
    return recorded_ < kLogCapacity ? recorded_ : kLogCapacity;
}

TrustEngine::Transition TrustEngine::transition(std::size_t index) const
{
    const std::size_t kept = transitions_logged();
    if (index >= kept) {
        return Transition{};
    }
    const std::size_t oldest = recorded_ < kLogCapacity ? 0 : recorded_ % kLogCapacity;
    return log_[(oldest + index) % kLogCapacity];
}

void TrustEngine::reset()
{
    state_             = TrustState::Trusted;
    live_              = 0;
    score_             = 0;
    clean_since_valid_ = false;
    has_last_trusted_  = false;
    recorded_          = 0;
    for (std::uint8_t i = 0; i < kTrustReasonCount; ++i) {
        evidence_at_[i] = MonotonicTime{};
    }
}

// ---------------------------------------------------------------------------
// TrustEvaluator

TrustEvaluator::TrustEvaluator(const TrustPolicy& policy) : engine_(policy) {}

void TrustEvaluator::observe(const GnssObservation& observation, PositionValidity validity,
                             MotionEvidence motion, std::optional<WallTime> device_time,
                             MonotonicTime now)
{
    const TrustPolicy& policy = engine_.policy();

    // --- what the receiver says about itself --------------------------------
    //
    // First, because it sees what we cannot. `Unknown` and `Unsupported` are
    // left alone rather than cleared: a receiver that cannot detect spoofing is
    // not a receiver reporting that there is none, and the difference is the
    // whole of OD-5 §2.
    if (observation.spoofing == ReceiverIndication::Warning ||
        observation.spoofing == ReceiverIndication::Critical) {
        engine_.report(TrustReason::ReceiverSpoofing, now);
    } else if (observation.spoofing == ReceiverIndication::None) {
        engine_.clear(TrustReason::ReceiverSpoofing);
    }

    if (observation.jamming == ReceiverIndication::Warning ||
        observation.jamming == ReceiverIndication::Critical) {
        engine_.report(TrustReason::ReceiverJamming, now);
    } else if (observation.jamming == ReceiverIndication::None) {
        engine_.clear(TrustReason::ReceiverJamming);
    }

    if (observation.protection_level.has_value()) {
        const ProtectionLevel& level = *observation.protection_level;
        set(engine_, TrustReason::ProtectionLevelInvalid, !level.valid, now);
        set(engine_, TrustReason::ProtectionLevelExceeded,
            level.valid && level.horizontal_mm > policy.protection_level_limit_mm, now);
    } else {
        engine_.clear(TrustReason::ProtectionLevelInvalid);
        engine_.clear(TrustReason::ProtectionLevelExceeded);
    }

    // --- what the solution itself admits ------------------------------------
    set(engine_, TrustReason::FixLost, validity == PositionValidity::NoFix, now);
    set(engine_, TrustReason::StalePosition, validity == PositionValidity::Stale, now);
    set(engine_, TrustReason::AccuracyPoor,
        observation.horizontal_accuracy_mm.has_value() &&
            *observation.horizontal_accuracy_mm > policy.accuracy_poor_mm,
        now);
    set(engine_, TrustReason::InsufficientSatellites,
        observation.satellites_used.has_value() &&
            *observation.satellites_used < policy.min_satellites,
        now);

    set(engine_, TrustReason::ImplausibleSpeed,
        observation.speed_mm_s.has_value() &&
            *observation.speed_mm_s > policy.implausible_speed_mm_s,
        now);

    // The satellite picture. A receiver using more satellites than it can see
    // is not a marginal reading, it is an impossible one — the strongest form
    // of this check and the only one available without per-signal data. The
    // real detector wants carrier-to-noise per signal and a constellation set
    // that can be compared across epochs; both are blocked on T-051 and T-052,
    // and pretending otherwise here would be inventing a capability.
    bool constellation_anomaly = false;
    if (observation.satellites_used.has_value() && observation.satellites_in_view.has_value()) {
        constellation_anomaly = *observation.satellites_used > *observation.satellites_in_view;
    }
    if (observation.satellites_in_view.has_value()) {
        if (have_previous_in_view_ && previous_in_view_ >= 8 &&
            *observation.satellites_in_view == 0) {
            // Every satellite gone between two consecutive epochs, from a sky
            // that was open. Physically that is a lid closing over the antenna;
            // it is also what a transmitter capturing the receiver looks like.
            constellation_anomaly = true;
        }
        previous_in_view_      = *observation.satellites_in_view;
        have_previous_in_view_ = true;
    }
    set(engine_, TrustReason::ConstellationAnomaly, constellation_anomaly, now);

    // --- time -----------------------------------------------------------------
    //
    // Two *absolute instants* compared, which is what a wall clock is for. The
    // rule clock.h enforces is the other one — never measure a duration with
    // it — and this is exactly the detector that rule protects: a spoofer that
    // could step the clock we measure timeouts against would switch off the
    // detector meant to catch it.
    bool clock_disagrees = false;
    if (observation.receiver_time.has_value() && observation.receiver_time_valid &&
        device_time.has_value()) {
        clock_disagrees = seconds_between(*observation.receiver_time, *device_time) >
                          policy.clock_disagreement_s;
    }
    set(engine_, TrustReason::ClockDisagreement, clock_disagrees, now);

    // --- physics, which needs the previous fix --------------------------------
    //
    // Each interval is taken from the timestamp paired with the value it is
    // being compared against, and is read before anything is overwritten.
    //
    // Both halves of that sentence are bugs this code has had. Reading the
    // interval after updating the timestamp made every dt zero and switched
    // both rate detectors silently off. Sharing one timestamp between them made
    // a fix dropout look like a teleport: no-fix observations kept advancing it
    // while the last known position stood still, so the first fix after a
    // minute under a bridge was divided by one second instead of sixty.
    //
    // Neither is visible to a test of a single observation, which is why the
    // replay fixtures walk several epochs and why one of them walks through a
    // dropout.
    bool jumped         = false;
    bool moved_at_rest  = false;
    bool climbed_absurd = false;

    if (observation.position.has_value() && in_range(*observation.position)) {
        const Millis dt = have_previous_ ? elapsed(previous_position_at_, now) : Millis{0};

        if (have_previous_) {
            const std::uint32_t moved = distance_mm(*observation.position, previous_position_);

            if (dt.value > 0) {
                // Millimetres per second without forming a product that could
                // overflow: moved is at most 1e9, and multiplying by 1000 keeps
                // it inside 64 bits before the division.
                const std::uint64_t implied =
                    (static_cast<std::uint64_t>(moved) * 1000ULL) / dt.value;
                jumped = implied > policy.implausible_speed_mm_s;
            }

            // The canonical detector, and the reason the BMA423 is named in
            // ADR-0011: a still wrist is genuinely still, so a position that
            // walks away from a stationary device is evidence in a way it would
            // not be on a device that cannot tell.
            moved_at_rest = motion.known && !motion.moving && moved > policy.jump_while_still_mm;
        }
        previous_position_    = *observation.position;
        previous_position_at_ = now;
        have_previous_        = true;

        latest_position_      = *observation.position;
        latest_position_at_   = now;
        have_latest_position_ = true;
    }

    if (observation.altitude_msl_mm.has_value()) {
        const Millis dt =
            have_previous_altitude_ ? elapsed(previous_altitude_at_, now) : Millis{0};
        if (have_previous_altitude_ && dt.value > 0) {
            std::int64_t delta =
                static_cast<std::int64_t>(*observation.altitude_msl_mm) - previous_altitude_mm_;
            if (delta < 0) {
                delta = -delta;
            }
            const std::uint64_t rate = (static_cast<std::uint64_t>(delta) * 1000ULL) / dt.value;
            climbed_absurd           = rate > policy.implausible_altitude_rate_mm_s;
        }
        previous_altitude_mm_   = *observation.altitude_msl_mm;
        previous_altitude_at_   = now;
        have_previous_altitude_ = true;
    }

    set(engine_, TrustReason::PositionJump, jumped, now);
    set(engine_, TrustReason::MotionDisagreement, moved_at_rest, now);
    set(engine_, TrustReason::ImplausibleAltitudeRate, climbed_absurd, now);

    engine_.update(now);

    // Only a position that is both trusted and valid is worth keeping as the
    // one to fall back to. A degraded fix is fine to display and a poor thing
    // to remember for the next twenty minutes.
    if (engine_.state() == TrustState::Trusted && validity == PositionValidity::Valid) {
        engine_.remember(observation, now);
    }
}

void TrustEvaluator::refresh(PositionValidity validity, MonotonicTime now)
{
    // Exactly the two conclusions that follow from the clock rather than from
    // any new measurement. Nothing that remembers a position is touched, which
    // is the whole point — see the header.
    set(engine_, TrustReason::FixLost, validity == PositionValidity::NoFix, now);
    set(engine_, TrustReason::StalePosition, validity == PositionValidity::Stale, now);
    engine_.update(now);
}

void TrustEvaluator::compare_provider(const GnssObservation& other, MonotonicTime now)
{
    if (!have_latest_position_ || !other.position.has_value() || !in_range(*other.position)) {
        return;
    }

    // Only a comparison of two roughly simultaneous answers means anything. If
    // either side is older than the window, this is not evidence of
    // disagreement and must not be recorded as such — but neither is it
    // evidence of agreement, so any live reason is left standing rather than
    // cleared. Silence, not an all-clear: the same rule OD-5 applies to a
    // receiver that stops reporting.
    const Millis window = engine_.policy().provider_comparison_window;
    if (elapsed(latest_position_at_, now) > window ||
        elapsed(other.observed_at, now) > window) {
        return;
    }

    const std::uint32_t apart = distance_mm(latest_position_, *other.position);
    set(engine_, TrustReason::ProviderDisagreement,
        apart > engine_.policy().provider_disagreement_mm, now);
    engine_.update(now);
}

void TrustEvaluator::reset()
{
    engine_.reset();
    have_previous_          = false;
    have_previous_altitude_ = false;
    have_previous_in_view_  = false;
    have_latest_position_   = false;
}

// ---------------------------------------------------------------------------

const char* to_string(TrustState state)
{
    switch (state) {
        case TrustState::Untrusted: return "Untrusted";
        case TrustState::Degraded:  return "Degraded";
        case TrustState::Trusted:   return "Trusted";
    }
    return "?";
}

const char* to_string(TrustReason reason)
{
    switch (reason) {
        case TrustReason::ReceiverJamming:         return "ReceiverJamming";
        case TrustReason::ReceiverSpoofing:        return "ReceiverSpoofing";
        case TrustReason::ProtectionLevelInvalid:  return "ProtectionLevelInvalid";
        case TrustReason::ProtectionLevelExceeded: return "ProtectionLevelExceeded";
        case TrustReason::MotionDisagreement:      return "MotionDisagreement";
        case TrustReason::ImplausibleSpeed:        return "ImplausibleSpeed";
        case TrustReason::ImplausibleAltitudeRate: return "ImplausibleAltitudeRate";
        case TrustReason::PositionJump:            return "PositionJump";
        case TrustReason::ClockDisagreement:       return "ClockDisagreement";
        case TrustReason::ProviderDisagreement:    return "ProviderDisagreement";
        case TrustReason::ConstellationAnomaly:    return "ConstellationAnomaly";
        case TrustReason::AccuracyPoor:            return "AccuracyPoor";
        case TrustReason::InsufficientSatellites:  return "InsufficientSatellites";
        case TrustReason::StalePosition:           return "StalePosition";
        case TrustReason::FixLost:                 return "FixLost";
    }
    return "?";
}

}  // namespace attadipa::core
