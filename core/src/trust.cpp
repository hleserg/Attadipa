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
    // The allegation is standing again, so it is no longer one that lapsed
    // unanswered. Which of the two doors it leaves by is decided when it
    // leaves, not now.
    unconfirmed_ &= ~trust_reason_bit(reason);
}

void TrustEngine::clear(TrustReason reason)
{
    live_ &= ~trust_reason_bit(reason);
    // The retraction, and the only one there is. Everything that separates an
    // all-clear from silence in this class comes down to which of these two
    // lines cleared the bit.
    unconfirmed_ &= ~trust_reason_bit(reason);
}

void TrustEngine::stop_awaiting(TrustReason reason)
{
    // `unconfirmed_` only. Deliberately not `live_`: an allegation whose
    // evidence has not expired is still current evidence, and this call is
    // about a subject that has gone, not about a condition that has ended.
    // The two masks are disjoint by construction, so this is a no-op on
    // anything live.
    unconfirmed_ &= ~trust_reason_bit(reason);
}

bool TrustEngine::holds(TrustReason reason) const
{
    return (live_ & trust_reason_bit(reason)) != 0;
}

bool TrustEngine::awaiting_confirmation(TrustReason reason) const
{
    return (unconfirmed_ & trust_reason_bit(reason)) != 0;
}

void TrustEngine::update(MonotonicTime now)
{
    // Silence is not the same as an all-clear, but it cannot mean "forever
    // suspect" either — a device that walks out of an interference source would
    // never recover. Evidence therefore decays, and a detector that wants a
    // condition to persist has to keep saying so.
    //
    // What decays is the *score*. The allegation itself is remembered as one
    // nobody withdrew, because those two are exactly what the first sentence
    // above distinguishes and the code used to collapse them one line later.
    for (std::uint8_t i = 0; i < kTrustReasonCount; ++i) {
        const std::uint32_t bit = 1u << i;
        if ((live_ & bit) != 0 && elapsed(evidence_at_[i], now) >= policy_.evidence_ttl) {
            live_ &= ~bit;
            unconfirmed_ |= bit;
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

    // And a clean score has to be clean for a reason.
    //
    // This is the other half of "up is earned", and it was missing in the
    // direction that costs. The TTL above takes an allegation out of the score
    // without anybody having withdrawn it, so fifteen seconds of a spoofing
    // alarm followed by nothing at all left `score_` at zero — and this code
    // read that zero as a detector's all-clear, started the hold on it, and
    // walked Untrusted → Degraded → Trusted over the next ten seconds. No
    // observation, no clear(), no evidence of any kind arrived in that window:
    // the device announced the position was fit to navigate by at precisely the
    // moment its receiver had stopped talking. OD-5 §4 (*do not collapse the
    // states*) and §8 (*the receiver's own verdict is strong evidence, not
    // truth*) are the rule, `clear()` is the contract, and this line is where
    // both were being lost. NOT §2, which an earlier version of this comment
    // cited: §2 is about the LS550G's anti-spoofing CAPABILITY being `UNKNOWN`
    // rather than `SUPPORTED` — a datasheet claim about a part, not a per-epoch
    // indication from a running one. Found in review.
    //
    // The anchor is dropped rather than frozen, so when a retraction does
    // arrive the hold is measured from *it* and not from the silence in front
    // of it. And the consequence is stated rather than discovered later: a
    // device that never hears another positive word does not climb on the clock
    // alone. The way out is a detector saying so, or reset() when the provider
    // goes away — never a timer.
    if (unconfirmed_ != 0) {
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
    unconfirmed_       = 0;
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

    // A monotonic measurement instant claimed to be after the instant we are
    // processing it is not jitter, it is a clock — or an attacker — that does
    // not add up, and is exactly as suspect as one claimed to be implausibly
    // old (below). Computed once here, shared by both rate blocks, because it
    // is a property of the observation, not of either baseline.
    const bool observed_in_future =
        elapsed(now, observation.observed_at) > policy.observed_at_forward_skew;
    clock_disagrees = clock_disagrees || observed_in_future;

    set(engine_, TrustReason::ClockDisagreement, clock_disagrees, now);

    // --- physics, which needs the previous fix --------------------------------
    //
    // Each interval is taken from the timestamp paired with the value it is
    // being compared against, and is read before anything is overwritten.
    //
    // Three bugs this code has had, in the order they were found. Reading the
    // interval after updating the timestamp made every dt zero and switched
    // both rate detectors silently off. Sharing one timestamp between them made
    // a fix dropout look like a teleport: no-fix observations kept advancing it
    // while the last known position stood still, so the first fix after a
    // minute under a bridge was divided by one second instead of sixty. And
    // measuring the interval from `now` — when the observation was *processed*
    // — rather than `observation.observed_at` — when it was *measured* — read
    // arrival delay as travel time: a fix relayed over a link that queues and
    // retries could be measured ten seconds apart and arrive one millisecond
    // apart, which is an ordinary walk reported as a teleport.
    //
    // Measurement time only settles half of it: a receiver that has lost its
    // fix can still retain the last coordinate in the position field while
    // reporting `NoFix`, and an out-of-order relay can deliver an
    // `observed_at` older than the one already accepted. Neither is a sample
    // this baseline may advance on — the first because it is not a new
    // position, the second because it would make the *next* legitimate sample
    // divide by an inflated interval and hide a real jump. Both are still
    // evaluated for their own trust reasons; only the baseline they would seed
    // for the following observation is refused.
    //
    // Trusting measurement time unconditionally opens a fourth door of the
    // same shape, and it is worse than the third: an `observed_at` claimed to
    // be *after* `now` (`observed_in_future`, above) is in_order by the check
    // below — future is never less than past — so nothing stopped it from
    // becoming the baseline. Once it did, its own implied speed rounded to
    // nothing (the interval is enormous), and every genuine sample afterward
    // was "older" than the poisoned baseline and was rejected the same way,
    // forever — no path back short of reset(). `observed_in_future` closes
    // that door the same way the reorder case is closed: refused as a
    // baseline, still evaluated on its own, never adopted.
    //
    // None of this is visible to a test of a single observation, which is why
    // the replay fixtures walk several epochs and why some of them walk
    // through a dropout, a reorder, or a poisoning attempt.
    bool jumped         = false;
    bool moved_at_rest  = false;
    bool climbed_absurd = false;

    // Only a fix good enough to navigate by may seed or advance a rate
    // baseline. `NoFix` and `Stale` are excluded even when they carry a
    // coordinate, because that coordinate is retained state, not a new
    // measurement.
    const bool usable_for_rate =
        validity == PositionValidity::Valid || validity == PositionValidity::Degraded;

    if (observation.position.has_value() && in_range(*observation.position)) {
        const bool in_order = !observed_in_future &&
            (!have_previous_ || observation.observed_at >= previous_position_at_);

        // DETECTING AND ADOPTING ARE TWO DECISIONS, NOT ONE.
        //
        // They were one, and it was wrong in the direction that matters. Both
        // sat inside `usable_for_rate && in_order`, so a sample that arrived
        // out of order, or claimed to be measured in the future, skipped the
        // detectors entirely — it was neither adopted NOR checked. That made
        // the refusal above worse than useless against the case it exists for:
        // a hostile sample could report the wrist five hundred kilometres from
        // a wrist the accelerometer says never moved, and raise nothing, while
        // `remember()` below still stored it as the last trusted position.
        //
        // The comment on `observed_in_future` promised the opposite, and so did
        // the pull request that introduced it — "still evaluated for its own
        // trust reasons, never adopted as the baseline". Only the second half
        // was implemented. Found by review on #71, twice, before it merged.
        //
        // So: detect against whatever baseline currently stands, whether or not
        // this sample is fit to replace it; adopt only in order. A backward dt
        // needs no guard of its own — `elapsed()` saturates `to <= from` to
        // zero and the `dt.value > 0` test below already skips it, so an
        // out-of-order sample yields no rate rather than a fabricated one.
        if (usable_for_rate && have_previous_) {
            const Millis dt = elapsed(previous_position_at_, observation.observed_at);
            const std::uint32_t moved =
                distance_mm(*observation.position, previous_position_);

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
            // not be on a device that cannot tell. It needs no interval at all,
            // which is exactly why gating it on `in_order` cost the most.
            moved_at_rest =
                motion.known && !motion.moving && moved > policy.jump_while_still_mm;
        }

        if (usable_for_rate && in_order) {
            previous_position_    = *observation.position;
            previous_position_at_ = observation.observed_at;
            have_previous_        = true;
        }

        latest_position_      = *observation.position;
        latest_position_at_   = now;
        have_latest_position_ = true;
    }

    if (observation.altitude_msl_mm.has_value()) {
        const bool in_order = !observed_in_future &&
            (!have_previous_altitude_ || observation.observed_at >= previous_altitude_at_);

        // Same split, same reason — see the position block above.
        if (usable_for_rate && have_previous_altitude_) {
            const Millis dt = elapsed(previous_altitude_at_, observation.observed_at);
            if (dt.value > 0) {
                std::int64_t delta = static_cast<std::int64_t>(*observation.altitude_msl_mm) -
                                      previous_altitude_mm_;
                if (delta < 0) {
                    delta = -delta;
                }
                const std::uint64_t rate =
                    (static_cast<std::uint64_t>(delta) * 1000ULL) / dt.value;
                climbed_absurd = rate > policy.implausible_altitude_rate_mm_s;
            }
        }

        if (usable_for_rate && in_order) {
            previous_altitude_mm_   = *observation.altitude_msl_mm;
            previous_altitude_at_   = observation.observed_at;
            have_previous_altitude_ = true;
        }
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
    //
    // What it does do is stop *awaiting* a retraction that has become
    // unreachable. `ProviderDisagreement` is the one reason whose only
    // retraction lives past this gate, so once the gate closes and the TTL has
    // moved the bit into `unconfirmed_`, nothing in the system can ever
    // withdraw it — the device is pinned for the rest of the boot with
    // `score() == 0`, `reasons() == 0` and no exit but `reset()`. Both halves
    // of the gate close in ordinary operation: `latest_position_at_` advances
    // only inside `observe()`, and a duty-cycled receiver is the point of
    // `gnss_power.h`; and `other.observed_at` is a relayed fix's MEASUREMENT
    // time, which `tests/replay/scenarios/14-a-relayed-fix-arrives-old.trace`
    // records at 40 s when a stalled link delivers its backlog. So this is the
    // ordinary path, not the pathological one. Found in review of #153; see
    // `stop_awaiting()` for why it is not the all-clear rule returning.
    const Millis window = engine_.policy().provider_comparison_window;
    if (elapsed(latest_position_at_, now) > window ||
        elapsed(other.observed_at, now) > window) {
        engine_.stop_awaiting(TrustReason::ProviderDisagreement);
        // No `update()` here, and that is deliberate. This path carries no new
        // evidence, and `update()` would run the TTL -- turning "the comparison
        // could not be made" into "and therefore the live allegation expired",
        // which is the collapse the whole branch removes. The next `observe()`
        // or `refresh()` re-evaluates; nothing waits on this call to do it.
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
