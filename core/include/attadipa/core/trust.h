#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "attadipa/core/clock.h"
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
//   * and the other half of that same rule: recovery is earned from a
//     *retraction*, never from the clock **while the detector is still there**.
//     A score that fell to nothing because a detector said the condition was
//     over and a score that fell to nothing because a detector stopped talking
//     are not the same fact, and only the first may move the state upwards
//     (OD-5 §4 and §8, and `clear()` below). The qualifier is not a softening:
//     a detector whose SUBJECT has gone is a third case, it does let the state
//     climb on the clock afterwards, and it is `stop_awaiting()` below --
//     reachable by exactly one reason, for the reason written there. An earlier
//     version of this list stated the rule absolutely and the code had already
//     stopped honouring it. And "still there" is ITSELF measured in time, which
//     is where the care goes: judged on one uncomparable frame it read a node
//     under canopy as a node that had left, so `provider_departure_grace` is
//     the bound and `provider_detached()` is the answer whenever an owner can
//     give it. What the clock decides is whether anyone remains who COULD
//     retract -- never whether a retraction happened;
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

// And the name for "no verdict has been reached", which is a state a stored
// verdict can be in even though it is not a `TrustState`
// (`GnssStatus::trust`, diagnostics.h).
//
// Like both overloads above it, this is a **diagnostic identifier** — for a
// log line, a replay trace, a support bundle — and not a string anybody is
// shown. Core does not speak English and neither external protocol may carry
// display text (ADR-0010 §4): a screen showing this state goes through `l10n`,
// which is where its Russian lives. What the identifier buys is that a reader
// of a bundle meets a word rather than a blank, a zero or the first enumerator,
// each of which prints a confidence nobody has.
const char* to_string(std::optional<TrustState> state);

// Read a stored verdict, saying at the call site what its absence means.
//
// This exists because `std::optional`'s comparisons against a bare `TrustState`
// all compile and several of them fail *open*. Checked, not assumed — for an
// empty optional, `== Untrusted` is false, so a guard that refuses on
// `Untrusted` does not fire; `!= Untrusted` is true, so a guard that permits on
// anything-but-`Untrusted` does; and `< Degraded` is true, sorting "nobody
// looked" below the worst verdict there is. None of those orderings was
// decided by anyone, and the most natural-reading guard of the set —
// `if (trust != TrustState::Untrusted) draw_the_arrow();` — points a confident
// arrow using a position no evaluator has seen. That is reachable today, in the
// permanent state of a board with no receiver.
//
// So absence is resolved before any comparison, and the caller supplies the
// answer rather than inheriting one. Deliberately **not** a `may_navigate()`
// boolean: whether a `Degraded` fix is good enough depends on whether the user
// is reading a map or recording a track, and collapsing three states into one
// answer here would move the policy back into the detector, which is the first
// thing ADR-0011 §5 refuses.
constexpr TrustState trust_or(std::optional<TrustState> stored,
                              TrustState                when_not_evaluated)
{
    return stored.value_or(when_not_evaluated);
}

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

    // How long the second source must be UNABLE to answer before its own
    // allegation stops being awaited. `provider_comparison_window` above asks
    // whether one frame is comparable; this asks whether the source has stopped
    // being one, and they are different questions — a node under canopy, in a
    // doorway, or between its own GNSS duty cycles relays fix-less frames for
    // seconds to minutes and has not gone anywhere. Keying the lift on a single
    // uncomparable frame let one such frame retract an allegation nobody
    // withdrew; found in the fourth review round of #153.
    //
    // `ESTIMATED`. Two minutes is chosen to sit above an ordinary urban dropout
    // and well below a boot, and it is a guess: nobody has measured how long a
    // node's receiver stays fixless under cover, and the duty cycle of the
    // second source is not ours to know. It is deliberately NOT the exit for a
    // node that actually leaves — `provider_detached()` is that, immediately —
    // so being generous here costs a pinned `Degraded` for a node that
    // disappears without telling us, and being stingy costs a false all-clear.
    // Measuring it is part of T-152.
    Millis        provider_departure_grace{120000};
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
    //
    // That distinction is load-bearing and not only documentation. This is the
    // only call that *retracts* an allegation, and only a retracted allegation
    // lets the state climb again — see `unconfirmed_reasons()`.
    void clear(TrustReason reason);

    // A third thing, and it is neither of the two above: the allegation is no
    // longer ABOUT anything, so nobody can either repeat it or withdraw it.
    //
    // `report()` says the condition is on, `clear()` says a detector looked and
    // it is off. This says the detector's subject has gone: the reason stops
    // being awaited without ever having been retracted, and a live one is left
    // strictly alone — if the evidence is still fresh it still counts, and this
    // call cannot be used to talk a device out of a current allegation.
    //
    // It exists for exactly one shape, and adding a second use is a decision,
    // not a convenience. `ProviderDisagreement` is evidence about a **pair**,
    // and only `compare_provider()` can produce or withdraw it. When the two
    // sides stop being comparable — one of them is older than the comparison
    // window, which happens whenever the receiver is duty-cycled or the node
    // link delivers a backlog — the retraction becomes unreachable while the
    // TTL has already moved the bit into `unconfirmed_`. Left as it was, the
    // device is pinned below `Trusted` for the rest of the boot with
    // `score() == 0` and `reasons() == 0`: no timer, no exit, and nothing on a
    // screen to explain it. Found in review of #153.
    //
    // This is NOT the silence-is-an-all-clear rule coming back. A reason whose
    // detector is still there and merely quiet keeps waiting, which is the whole
    // of that rule; and refusing to be compared is not the same as being
    // exonerated. What a node gains by going uncomparable is only that it stops
    // *contradicting* the local receiver — never that its own position is
    // believed, because a fix's own trust comes from the detectors that ran on
    // it. A permanent pin, by contrast, punishes the local receiver for the
    // second source's behaviour, for ever, with no way back that is not
    // `reset()`.
    //
    // It does not re-evaluate, and that leaves a **one-tick window** with
    // exactly the shape `GnssStatus::trust_unconfirmed` was added to remove:
    // between this call and the next `observe()` or `refresh()`, a snapshot
    // reads `trust != Trusted`, `trust_reasons == 0` **and**
    // `trust_unconfirmed == 0` — a device stuck with nothing on a screen to
    // explain it. Not re-evaluating is deliberate: this path carries no new
    // evidence, and running the TTL here would turn "the subject went away"
    // into "and therefore the live allegation expired". The window is
    // self-healing and bounded by the caller's own tick, so it is stated rather
    // than closed. Found in review of #153.
    void stop_awaiting(TrustReason reason);

    // Expire stale evidence and re-evaluate. Safe and cheap to call often; the
    // state only moves when the score crosses a threshold or a hold completes.
    //
    // Calling it is not itself evidence of anything. A poll that carries no new
    // detector output can lower the score, by expiry, and can never raise the
    // state: a service ticking once a second while its receiver says nothing
    // must not be able to talk the device back into `Trusted`.
    void update(MonotonicTime now);

    // Record this observation as the last position worth remembering. The
    // caller decides when — TrustEvaluator does it only while Trusted.
    void remember(const GnssObservation& observation, MonotonicTime now);

    TrustState    state() const { return state_; }
    std::uint32_t reasons() const { return live_; }
    std::uint16_t score() const { return score_; }
    bool          holds(TrustReason reason) const;

    // The allegations that lapsed without anybody withdrawing them.
    //
    // A reason that expires stops counting towards the score — evidence has to
    // decay, or a device that walks out of an interference source stays suspect
    // for ever. What it does not become is an all-clear: the detector did not
    // say the condition was over, it stopped saying anything, and those are
    // different inputs. While any bit is set here the state cannot climb, and
    // the bit says *which* detector is being waited on, so a diagnostic screen
    // can name it rather than showing a device stuck for no visible reason.
    //
    // Per reason rather than one flag, deliberately: "recovery is blocked" is
    // not an answer anybody can act on, and a single boolean would lose both
    // the source and — because the hold restarts from the retraction — the
    // instant that mattered.
    //
    // Like `holds()` and `reasons()`, this reads state that only `update()`
    // advances, so a reader that has not called `update(now)` is looking at the
    // answer as of the last one. That is a property of the whole class and it
    // is a separate open finding in T-062, not a new one here.
    std::uint32_t unconfirmed_reasons() const { return unconfirmed_; }
    bool          awaiting_confirmation(TrustReason reason) const;

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

    // Everything back to boot state, including the log, the remembered position
    // and every reason live or lapsed. Used when **the link resets** —
    // ADR-0005 §5: "the epoch is reset unconditionally on link loss — no state
    // survives a reconnect implicitly". Not ADR-0004 §3, which an earlier
    // version cited and which is *Availability is not validity — and a remote
    // datum has two ages*.
    //
    // NOT the call for a provider going away. `TrustEvaluator::provider_detached()`
    // is that one, and the difference is whose history is being discarded: a
    // link reset invalidates everything stamped with the epoch, including this
    // device's own observations of its own receiver; a node walking off the end
    // of the garden invalidates one allegation and nothing else. Reaching for
    // this one there throws away the local receiver's entire evidence and the
    // last trusted position with it, and asserts `Trusted` outright — which is
    // the all-clear this class exists to refuse. An earlier version of this
    // comment named both triggers and left the choice to whoever read the names
    // first; found in the fourth review round of #153.
    void reset();

private:
    void evaluate(MonotonicTime now);
    void enter(TrustState next, MonotonicTime now);

    TrustPolicy   policy_;
    TrustState    state_ = TrustState::Trusted;
    std::uint32_t live_  = 0;

    // Reasons that left `live_` by the TTL rather than by clear(). Four bytes,
    // and they are the difference between "nothing is wrong" and "nobody has
    // said anything for a while" — see unconfirmed_reasons().
    std::uint32_t unconfirmed_ = 0;

    // Reasons whose SUBJECT has gone. `stop_awaiting()` sets a bit here and
    // `update()`'s TTL loop consults it, so a live allegation whose subject
    // left never becomes an unconfirmed one — it simply ends when its evidence
    // does. Without the latch `stop_awaiting()` was a one-shot that had to be
    // timed against another subsystem's constant: at a detach edge the reason
    // is usually still `live_`, the call did nothing, `evidence_ttl` later the
    // bit moved into `unconfirmed_`, and nothing called again because the node
    // had already gone — pinning `Degraded` with `score() == 0`, `reasons() == 0`
    // and no exit but `reset()`, which is the pin this branch exists to remove,
    // reached through the branch's own new hook. Whether that happened depended
    // on whether the link-loss timeout exceeded `evidence_ttl`: an accidental
    // coupling between two constants in two subsystems, documented in neither.
    // Found in the fourth review round of #153.
    //
    // Cleared by `report()` and `clear()`: a new allegation, or a retraction,
    // means there is a subject again.
    std::uint32_t abandoned_ = 0;

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

// What the accelerometer has to say. Three states, not two: "the device is not
// moving" and "nobody asked the accelerometer" are different facts, and only
// the first is evidence.
//
// The T-Watch's BMA423 is an accelerometer — no gyroscope, no magnetometer. It
// is exactly the right part for this one question and it is not an inertial
// navigation system; nothing here may quietly become dead reckoning
// (ADR-0009, ADR-0011 §6).
struct MotionEvidence {
    bool known  = false;
    bool moving = false;
};

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

    // The second source is gone — the node detached, the link dropped, the
    // capability was withdrawn. Whoever owns the provider knows this; nothing
    // inside the evaluator can.
    //
    // NOT `TrustEngine::reset()`, which is for a link reset and discards this
    // device's own history along with the node's. Here the local receiver has
    // seen nothing new and its evidence still counts; exactly one allegation
    // loses the party that could withdraw it.
    //
    // It exists because `compare_provider()` only *approximates* it. That path
    // reaches `stop_awaiting()` when the other side has been unable to answer
    // for `provider_departure_grace`, which is a decent proxy for a provider
    // that has left and a poor one for a provider that is still there and
    // merely stale: a node relaying fixes at 1 Hz whose measurement times are
    // consistently older than `provider_comparison_window` is present, is
    // disagreeing, and would still stop being awaited. Review of #153 named
    // that gap and it is real; closing it inside `compare_provider()` would
    // need a reason meaning "a second source is present and permanently
    // uncomparable", which is a new enumerator and a decision — **T-152**.
    // Until then this is the call that says the subject actually went, and it
    // is the one to prefer wherever the owner can make it — the grace exists to
    // bound the damage when nobody does, not to substitute for it.
    //
    // Idempotent, and it does not re-evaluate: like the path it replaces, it
    // carries no new evidence, so running the TTL here would turn "the provider
    // left" into "and therefore the live allegation expired". A live
    // `ProviderDisagreement` is left strictly alone — see `stop_awaiting()`.
    //
    // **It latches.** The documented trigger is an EDGE, and at that edge a
    // disagreement reported inside `evidence_ttl` is `live_`, which
    // `stop_awaiting()` deliberately does not touch — so as a one-shot this
    // call did nothing, the TTL moved the bit into `unconfirmed_` fifteen
    // seconds later, and nothing called again because the node had already
    // gone. Pinned `Degraded` for the rest of the boot, reached through the
    // very hook added to prevent it, and whether it happened at all depended on
    // whether the link-loss timeout exceeded `evidence_ttl` — two constants in
    // two subsystems, coupled by accident and documented in neither. The latch
    // is `TrustEngine::abandoned_`; it clears the moment anything reports that
    // reason again, because a node that comes back is a subject again. Found in
    // the fourth review round of #153.
    void provider_detached();

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

    // The local side of `compare_provider()`: a coordinate this receiver
    // MEASURED, and the instant it measured it.
    //
    // Not "the last position field that arrived", which is what stood here and
    // is a different fact. A receiver that loses its fix keeps sending the
    // coordinate it last solved for, with a `PositionValidity` of `NoFix`
    // beside it saying there is no position at all — the shape the rate
    // baselines above already refuse, and which `tests/test_trust.cpp` already
    // reproduces. Stored unconditionally and stamped with ARRIVAL time, one
    // such frame per second kept this side of the comparison permanently
    // "fresh": a node reporting the place the wearer had actually walked to was
    // measured against a coordinate the local receiver had disowned, and the
    // difference was reported as `ProviderDisagreement` — 30 points, which
    // reaches `degrade_at` unaided — refreshed for as long as the dropout
    // lasted. The evaluator had two models of the same observation's fitness,
    // and only one of them read `validity`. Found by the review of
    // `6965191..8d757a7`, issue #178.
    //
    // So the invariant is the name. This is present only while the local
    // receiver has produced a comparable measurement, and `measured_at` is
    // `observation.observed_at` and never `now` — the same discipline as the
    // baselines above and for the same reason, so that a comparison is between
    // two measurement ages rather than between one measurement and one arrival.
    //
    // It advances in lockstep with `previous_position_` and is deliberately
    // still its own field: the two answer different questions — the fix a rate
    // is computed FROM, and this device's current answer to *where are you* —
    // and collapsing them would make any later change to one silently change
    // the other.
    struct ComparablePosition {
        Position      position{};
        MonotonicTime measured_at{};
    };
    std::optional<ComparablePosition> local_comparable_;

    // When the second source last produced a frame this device could compare
    // against — the anchor `provider_departure_grace` is measured from. Not
    // "when a frame last arrived": a node relaying fix-less frames at 1 Hz is
    // arriving constantly and answering nothing, and that difference is the
    // whole of the fourth review round's finding.
    bool          have_other_comparable_    = false;
    MonotonicTime other_last_comparable_at_{};
};

}  // namespace attadipa::core
