#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "attadipa/core/clock.h"
#include "attadipa/core/motion.h"
#include "attadipa/core/position.h"

// Whether the position is worth navigating by, and — kept, not discarded — why.
//
// docs/adr/0011-gnss-integrity.md §5 and §6. The shape is the decision:
//
//   * three states, not a boolean. `gps_ok` forces the *policy* into the
//     *detector*, and whether a degraded fix is good enough depends on whether
//     the user is reading a map or recording a track;
//   * weighted evidence from several sources, not one flag. In particular not
//     `spoofFlag || jumpDetected || jamming`, which is worse than a boolean
//     because it looks like it considered something;
//   * hysteresis, so one bad epoch does not flip the state and one good one
//     does not clear it;
//   * reason codes, kept. A user-facing sentence, an application's decision to
//     hide the compass, and a diagnostic screen are three consumers of the same
//     evidence, and a collapsed verdict serves none of them;
//   * the last trusted position, with an uncertainty that grows after trust is
//     lost — because a position that was good sixty seconds ago is a circle,
//     not a point;
//   * a bounded transition log, so a field report can be read afterwards.
//     Bounded, because a diagnostic that fills the flash it was diagnosing is
//     not a diagnostic (ADR-0011 §7).
//
// The receiver's own verdict is the strongest single input here and it is not
// the truth. It goes first because it can see the RF front end, per-signal
// carrier-to-noise and the correlator, and none of that reaches us. It does not
// go last because it can be fooled — and because on one of the two receivers
// this watch may carry, we do not yet know what it detects at all (T-051/T-052).
//
// Everything in this header is deterministic and reads no clock of its own.
// That is what makes tests/replay/ possible: the same fixture yields the same
// verdict on every machine, every run.

namespace attadipa::core {

enum class TrustState : std::uint8_t {
    Untrusted,  // do not navigate by this
    Degraded,   // usable with a caveat the interface must show
    Trusted,    // usable
};

// Why the state is where it is. One bit each, so a snapshot carries the whole
// set rather than the most recent one — "jamming *and* a jump while stationary"
// is a different situation from either alone, and the difference is what a
// field report needs.
enum class TrustReason : std::uint8_t {
    ReceiverJamming,          // the receiver says the band is being jammed
    ReceiverSpoofing,         // the receiver says the signals are fabricated
    ProtectionLevelInvalid,   // it published a protection level and disowned it
    ProtectionLevelExceeded,  // the bound it promised is wider than we can use
    MotionDisagreement,       // the position moved; the accelerometer says still
    ImplausibleSpeed,         // faster than anything this device travels on
    ImplausibleAltitudeRate,  // climbing or falling faster than is possible
    PositionJump,             // consecutive fixes imply a speed nothing reaches
    ClockDisagreement,        // receiver time and device time have diverged,
                              // or an observation claims to be measured after
                              // the instant it is being processed
    ProviderDisagreement,     // two providers report different places
    ConstellationAnomaly,     // the satellite picture is not physically sensible
    AccuracyPoor,             // the receiver's own estimate is wide
    InsufficientSatellites,   // too few to have solved what it claims to
    StalePosition,            // nothing fresh has arrived
    FixLost,                  // there is no solution at all
};

inline constexpr std::uint8_t kTrustReasonCount =
    static_cast<std::uint8_t>(TrustReason::FixLost) + 1;

constexpr std::uint32_t trust_reason_bit(TrustReason reason)
{
    return 1u << static_cast<std::uint32_t>(reason);
}

const char* to_string(TrustState state);
const char* to_string(TrustReason reason);

// The numbers that turn evidence into a verdict.
//
// Policy, deliberately: none of this is physics, and a wrist in a forest and a
// wrist in a city need different answers. Every default below is `ESTIMATED` —
// nobody has yet walked anywhere with one of these boards, and the moment
// somebody does, these are the first numbers to change.
struct TrustPolicy {
    // Significance of each reason, 0..100, indexed by TrustReason.
    std::uint8_t weight[kTrustReasonCount] = {};

    // The thresholds, and the gap between the last two is the hysteresis.
    std::uint16_t degrade_at    = 30;  // leave Trusted at or above this score
    std::uint16_t untrust_at    = 60;  // reach Untrusted at or above this
    std::uint16_t recover_below = 15;  // climb back only at or below this

    Millis evidence_ttl{15000};  // evidence stops counting after this
    Millis recover_hold{5000};   // and the score must stay clean this long

    // How fast the last trusted position stops being a point. 1.5 m/s is a
    // brisk walk; it is the honest growth rate for a device that has lost its
    // fix and cannot know which way its wearer went.
    std::uint32_t uncertainty_growth_mm_s = 1500;

    // Plausibility limits.
    std::uint32_t implausible_speed_mm_s          = 55000;  // ~200 km/h
    std::uint32_t implausible_altitude_rate_mm_s  = 30000;  // ~30 m/s
    std::uint32_t jump_while_still_mm             = 50000;  // 50 m with a still wrist
    std::uint32_t clock_disagreement_s            = 60;

    // How far after `now` an observation's `observed_at` may claim to have
    // been measured and still be believed. Not zero: `observed_at` and `now`
    // are not read atomically together even for a purely local fix, so a
    // hard `>` would reject honest samples on jitter alone. 50 ms is small on
    // every scale that matters here: at implausible_speed_mm_s (55 000 mm/s)
    // it bounds the distance a skewed-but-accepted sample could hide to
    // 2 750 mm — under 6% of jump_while_still_mm and negligible next to any
    // real inter-fix interval — while remaining three orders of magnitude
    // tighter than clock_disagreement_s, which answers a different, much
    // coarser question about a different clock. A claim wider than this is
    // not jitter, it is a clock that does not add up, and is treated the same
    // as one (ClockDisagreement) rather than believed.
    Millis        observed_at_forward_skew{50};

    std::uint32_t protection_level_limit_mm       = 50000;  // 50 m
    std::uint32_t provider_disagreement_mm        = 250000; // 250 m between sources

    // Two providers can only be compared if they are talking about the same
    // moment. Without this, a node's position arriving after the watch's own
    // fix went stale is measured against wherever the wearer was standing
    // several minutes ago, and disagreement is reported for two answers that
    // were both correct when they were given.
    Millis        provider_comparison_window{5000};
    std::uint32_t accuracy_poor_mm                = 50000;  // 50 m

    // What to assume when the receiver did not publish an accuracy at all.
    //
    // Not zero. Zero is a number and a number is an answer: it would make
    // "nobody said how good this is" read as "this position is exact", and
    // uncertainty_mm() would then report a point where the truth is a circle
    // of unknown radius. Erring wide is the only safe direction, so the default
    // is the widest accuracy still considered usable.
    std::uint32_t assumed_accuracy_mm            = 50000;
    std::uint8_t  min_satellites                  = 4;
};

TrustPolicy default_trust_policy();

// The evidence store and the state machine over it.
//
// Deliberately not a service and not a task: it owns no clock, no thread and no
// I/O. `now` is always passed in.
class TrustEngine {
public:
    struct Transition {
        TrustState    from    = TrustState::Trusted;
        TrustState    to      = TrustState::Trusted;
        std::uint32_t reasons = 0;
        MonotonicTime at{};
    };

    // Sixteen is enough to read a session's story and small enough that the
    // whole log fits in a diagnostics snapshot. When it overflows the oldest
    // entry is dropped and `transitions_recorded()` keeps counting, so a reader
    // can tell "sixteen transitions" from "sixteen of forty".
    static constexpr std::size_t kLogCapacity = 16;

    explicit TrustEngine(const TrustPolicy& policy = default_trust_policy());

    // A detector saw something. Re-reporting a live reason refreshes it rather
    // than counting twice — evidence is a set, not a tally.
    void report(TrustReason reason, MonotonicTime at);

    // A detector says the condition is over. Distinct from letting it expire:
    // this is positive evidence of absence, the TTL is merely silence.
    void clear(TrustReason reason);

    // Expire stale evidence and re-evaluate. Safe and cheap to call often; the
    // state only moves when the score crosses a threshold or a hold completes.
    void update(MonotonicTime now);

    // Record this observation as the last position worth remembering. The
    // caller decides when — TrustEvaluator does it only while Trusted.
    void remember(const GnssObservation& observation, MonotonicTime now);

    TrustState    state() const { return state_; }
    std::uint32_t reasons() const { return live_; }
    std::uint16_t score() const { return score_; }
    bool          holds(TrustReason reason) const;

    bool                    has_last_trusted() const { return has_last_trusted_; }
    std::optional<Position> last_trusted_position() const;

    // How wide the last trusted position has become. Zero when there has never
    // been one — which is not "certain", it is "no answer", and callers must
    // check has_last_trusted() rather than reading zero as precision.
    std::uint32_t uncertainty_mm(MonotonicTime now) const;

    std::size_t transitions_recorded() const { return recorded_; }
    std::size_t transitions_logged() const;
    Transition  transition(std::size_t index) const;  // 0 is the oldest kept

    const TrustPolicy& policy() const { return policy_; }

    // Everything back to boot state, including the log. Used when a provider
    // detaches or the link resets — ADR-0004 §3: no state survives implicitly.
    void reset();

private:
    void evaluate(MonotonicTime now);
    void enter(TrustState next, MonotonicTime now);

    TrustPolicy   policy_;
    TrustState    state_ = TrustState::Trusted;
    std::uint32_t live_  = 0;
    std::uint16_t score_ = 0;

    MonotonicTime evidence_at_[kTrustReasonCount] = {};

    bool          clean_since_valid_ = false;
    MonotonicTime clean_since_{};

    bool          has_last_trusted_ = false;
    Position      last_trusted_{};
    MonotonicTime last_trusted_at_{};
    std::uint32_t last_trusted_accuracy_mm_ = 0;

    Transition  log_[kLogCapacity] = {};
    std::size_t recorded_          = 0;
};

// `MotionEvidence` — what the accelerometer has to say, and about which object
// — is attadipa/core/motion.h. It moved there when the node acquired an IMU of
// its own (OD-16): one motion fact now has two consumers, this file and the
// GNSS power model, and both of them have to be told whose motion it is
// (docs/adr/0013-node-motion.md).

// The detectors, and the previous observation they need.
//
// One entry point, so that a caller cannot run half the detectors and believe
// it ran them all.
class TrustEvaluator {
public:
    explicit TrustEvaluator(const TrustPolicy& policy = default_trust_policy());

    // `device_time` is the device's own wall clock, used only to notice that
    // the receiver's idea of absolute time has diverged from it. Comparing two
    // absolute instants is a legitimate use of a wall clock; measuring elapsed
    // time with one is not, and clock.h makes the second impossible.
    //
    // `motion` is only evidence about the body it names. The observation says
    // which body it was measured on through its `source`, and the
    // motion-disagreement detector is inert unless the two are demonstrably the
    // same object — an accelerometer on a wrist has nothing to say about a
    // receiver in somebody else's bag, in either direction
    // (docs/adr/0013-node-motion.md §3).
    void observe(const GnssObservation& observation, PositionValidity validity,
                 MotionEvidence motion, std::optional<WallTime> device_time,
                 MonotonicTime now);

    // Time passed and nothing arrived.
    //
    // The evaluator learns only by being told, so a receiver that simply stops
    // talking would otherwise leave the last verdict standing indefinitely: the
    // engine can *name* StalePosition and could never reach it. A state a
    // machine can print and cannot enter is worse than one it does not have,
    // because a reader of the diagnostics assumes its absence means something.
    // This is the call that closes it, and a location service makes it on its
    // own tick with `classify(retained_observation, now)`.
    //
    // Deliberately *not* observe() with the observation already held. That
    // would re-run every detector against a fix they have already seen — it
    // would compare the position with itself, and, worse, re-stamp the epoch
    // the next real fix is measured from. A minute of silence followed by an
    // ordinary five-hundred-metre walk would then read as five hundred metres
    // per second. Only the two conclusions that change with the clock alone
    // are touched here.
    void refresh(PositionValidity validity, MonotonicTime now);

    // A second provider's position for the same moment. Kept separate because
    // disagreement is evidence about both of them and belongs to neither.
    void compare_provider(const GnssObservation& other, MonotonicTime now);

    TrustEngine&       engine() { return engine_; }
    const TrustEngine& engine() const { return engine_; }

    TrustState state() const { return engine_.state(); }

    void reset();

private:
    TrustEngine engine_;

    // Each remembered value carries the time it was *measured*
    // (`observation.observed_at`, never `now`), and every rate is computed
    // against its own.
    //
    // A single shared "previous epoch" looks tidier and is wrong: an
    // observation with no fix still arrives, so a minute spent under a bridge
    // advances the shared timestamp while the last known position stays where
    // it was. The next real fix is then divided by one second instead of by
    // sixty, and a five-hundred-metre walk reads as five hundred metres per
    // second. That is a detector firing on the most ordinary event there is,
    // which trains the wearer to ignore it.
    //
    // Stamping it with `now` instead of `observed_at` has the same shape of
    // failure by a different door: a position relayed over a link that queues
    // and retries is measured seconds apart and can arrive milliseconds apart,
    // and arrival time reads that as a teleport. Only an observation whose
    // `PositionValidity` is `Valid` or `Degraded`, whose `observed_at` is not
    // older than what is already stored, and whose `observed_at` is not
    // implausibly *after* `now` either, may advance either baseline — a
    // future-dated `observed_at` is exactly as able to poison this state as a
    // reordered one, and is rejected the same way: evaluated for its own
    // trust reasons, never adopted as the baseline. See the comment in
    // trust.cpp above the rate blocks.
    bool          have_previous_    = false;
    Position      previous_position_{};
    MonotonicTime previous_position_at_{};

    bool          have_previous_altitude_ = false;
    std::int32_t  previous_altitude_mm_   = 0;
    MonotonicTime previous_altitude_at_{};

    bool        have_previous_in_view_ = false;
    std::uint8_t previous_in_view_     = 0;

    bool          have_latest_position_ = false;
    Position      latest_position_{};
    MonotonicTime latest_position_at_{};
};

}  // namespace attadipa::core
