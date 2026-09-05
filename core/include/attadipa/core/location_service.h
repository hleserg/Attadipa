#pragma once

#include <cstdint>
#include <optional>

#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"
#include "attadipa/core/mesh_service.h"
#include "attadipa/core/position.h"
#include "attadipa/core/trust.h"

// WHO OWNS A POSITION, AND WHAT IT IS ALLOWED TO CLAIM.
//
// `docs/architecture/PLATFORM_AUDIT.md` P0.3 named the gap this closes: the tree
// had `GnssObservation`, `PositionValidity`, `TrustState` and `Timed<T>` and
// nothing that produced any of them, so the first module wanting a coordinate
// had nowhere to put it except an application. This is that owner, and it is
// deliberately the smallest one that can exist.
//
// Three rules shape every line below, and each is a refusal rather than a
// feature. They come from `docs/research/NODE_POSITION_FROM_MESHCORE.md`, which
// read the node's firmware rather than its documentation.
//
//   1. **A coordinate is not a fix.** A MeshCore node holds the fix flag, the
//      satellite count and the receiver's UTC and transmits none of them.
//      Worse, `isValid()` there gates the *write*, not the send, so a receiver
//      that loses its fix leaves the last coordinate in place and nothing on
//      the wire changes. So a provider fed from that wire states
//      `FixType::Unknown` and `classify()` answers `PositionValidity::NoFix`
//      for every observation it can build -- at age zero and at an hour alike.
//      That is not a bug to route around later; it is the truth about *that*
//      input, and the tests assert it so that an ADR-0011 amendment cannot land
//      silently.
//
//      THE RULE IS ABOUT THE WIRE, NOT ABOUT THIS SERVICE, and the difference
//      became visible the moment a second producer arrived.
//      `gnss::NmeaReceiver` reads a receiver directly, so it states a real
//      `FixType` from GGA's quality and GSA's mode, and `classify()` reaches
//      `Valid`, `Degraded` and `Stale` for it. One classifier, two producers,
//      and the verdicts differ because the inputs do — which is the whole
//      point of putting the judgement in `classify()` rather than in either
//      provider.
//
//   2. **A node's coordinate is not the wearer's.** `PositionSource::NodeGnss`
//      says where it came from, and ADR-0009 already makes the same refusal for
//      a companion's heading. Nothing here promotes it.
//
//   3. **Absence is not zero.** `age_at_source_ms` is never written by this
//      slice: the node states no observation time, arrival is not observation,
//      and `Timed<T>` has no representation for "unknown". The published
//      `Timed<Position>` therefore carries `Validity::Unknown`, and *that* is
//      the field a consumer must read first. A consumer reading the age before
//      the validity is the defect the tests exist to catch.
//
// The shape follows `attadipa/core/mesh_service.h` -- a provider interface plus
// a service holding a reference to one -- because that is the pattern this tree
// has already accepted for a capability served from a node. There is no second
// forwarding HAL, and applications never name a provider.

namespace attadipa::core {

// WHAT IS KNOWN ABOUT THE RECEIVER BEHIND A COORDINATE.
//
// A different question from whether the coordinate is any good, and the reason
// it is asked separately: the verdict is `NoFix` in every one of these states
// (rule 1), so folding them into the validity would lose the only thing a
// provider *can* establish about the hardware at the other end.
//
// The one producer maps `RESP_CODE_CUSTOM_VARS`' `gps` key onto this, and the
// mapping is exact -- three states the key distinguishes, plus one for never
// having asked:
//
//   no `gps` key -> NotDetected, `gps:0` -> PoweredOff, `gps:1` -> Running.
//
// `NotDetected` is deliberately not called `Absent`. MeshCore's sensor manager
// opens the GPS UART, waits one second and sets `gps_detected` from whether any
// byte arrived, so a receiver that has not started emitting NMEA inside that
// second is undetected for the whole session and the same board can disagree
// with itself across two power cycles. The state says "nothing answered", never
// "this board has no receiver".
enum class ReceiverPresence : std::uint8_t {
    Unknown,      // nobody asked, or the source did not answer
    NotDetected,  // the source looked for a receiver and nothing answered
    PoweredOff,   // a receiver is there and is not running
    Running,      // a receiver is there and is running
};

// One provider's current answer.
//
// `origin` is which physical source this coordinate belongs to, and it exists
// for exactly one rule: a changed identity **discards** the retained
// observation rather than re-attributing it, because a new key is a new node.
// It is `MeshPeerId` rather than a fresh 32-byte identity type of its own --
// core already owns that type, the identity of the only producer that *has*
// one is a node public key, and a parallel struct with the same shape and the
// same `operator==` would be an abstraction with one implementation. A provider
// with no identity to offer — `gnss::NmeaReceiver`, which reads a receiver
// soldered to this board — leaves `has_origin` false and the discard rule then
// never fires, which is the correct behaviour for a source that cannot be
// swapped underneath us.
struct PositionSample {
    GnssObservation  observation{};
    MeshPeerId       origin{};
    bool             has_origin = false;
    ReceiverPresence receiver   = ReceiverPresence::Unknown;
};

// ONE CONTRACT. There is no second one, and PLATFORM_AUDIT.md's
// "One provider contract is enough" is why: a forwarding HAL underneath this
// would be a layer whose only job is to be a layer.
class PositionProvider {
public:
    virtual ~PositionProvider() = default;

    // Where this provider stands. Seven states, seven sentences (ADR-0004 §3),
    // and a provider may not collapse them: in particular a source that hands
    // over a coordinate with its receiver switched off is `Ready`, not `Off` --
    // `Off` is a remedy this device can perform, and no remedy here reaches a
    // receiver on the far side of a radio link.
    virtual Availability availability() const = 0;

    // The provider's current sample, or false when it has none. Pull, not push:
    // this slice adds no task, timer, queue or reconnect loop of its own
    // (ADR-0015 owns the transport, ADR-0016 the power).
    virtual bool sample(PositionSample& out) const = 0;
};

// What a consumer sees. Deliberately not a `GnssObservation`: an application
// gets availability, validity and two ages, exactly as `TimeService` publishes
// them, and never learns which provider answered.
struct LocationState {
    Availability     availability = Availability::Unprovisioned;

    // `age_at_us_ms` is measured and real. `age_at_source_ms` is never written
    // (rule 3) and `validity` is `Validity::Unknown` to say so.
    Timed<Position>  position{};
    bool             has_position = false;

    // The verdict, from `classify()` and from nothing else, so that the one
    // classifier this repository has is the one that judges a node coordinate
    // too. `NoFix` for everything a node can produce (rule 1), and the full
    // range for a local receiver, which states a fix type of its own.
    PositionValidity validity  = PositionValidity::NoFix;
    PositionSource   source    = PositionSource::Unknown;
    FixType          fix_type  = FixType::Unknown;
    ReceiverPresence receiver  = ReceiverPresence::Unknown;

    // Whose coordinate this is, for the engineering consumer to show. Present
    // only when the provider offered one.
    MeshPeerId       origin{};
    bool             has_origin = false;

    // Empty means no verdict has been reached, and that is the only value this
    // slice produces -- the same argument `GnssStatus::trust` makes in
    // `attadipa/core/diagnostics.h`. No `TrustEngine` runs here: a trust
    // evaluation over a source that cannot state a fix would be a conclusion
    // drawn from nothing.
    std::optional<TrustState> trust;
};

class LocationService {
public:
    explicit LocationService(PositionProvider& provider,
                             ValidityPolicy policy = {})
        : provider_(provider), policy_(policy)
    {
    }

    // Read the provider once. The caller owns the cadence; there is no timer
    // here, and this takes no clock either -- the arrival stamp it keeps is the
    // provider's own, so a poll that runs late does not make a coordinate look
    // younger or older than it is. Everything the class retains is decided in
    // this call:
    //
    //   * a sample whose origin differs from the retained one **discards** the
    //     retained observation -- a new key is a new node, not a moved one;
    //   * a sample carrying the coordinate already held does **not** refresh
    //     either age. An unchanged read is evidence against a live fix, not for
    //     one, and refreshing on it is how a dead receiver comes to look alive;
    //   * anything else is adopted, stamped `now`.
    //
    // A provider with no sample changes nothing but the availability, which is
    // what makes a disconnect retain and age rather than clear.
    void poll();

    LocationState state(MonotonicTime now) const;

    // How long ago this device *received* the coordinate. Empty when there is
    // none.
    std::optional<Millis> age_at_us(MonotonicTime now) const;

    // How old the coordinate was when its source sampled it. **Always empty in
    // this slice**, and that is rule 3 stated as an API rather than as a
    // comment: no producer states an observation time, so there is no answer to
    // round down to zero.
    std::optional<Millis> age_at_source(MonotonicTime now) const;

    // The retained observation, for a diagnostics surface that wants the whole
    // thing. Empty until one arrives, and it outlives a disconnect.
    const std::optional<GnssObservation>& observation() const { return observation_; }

    // THE SOURCE WAS REPUDIATED, WHICH IS NOT "NO SAMPLE THIS PASS".
    //
    // `poll()` retains an observation across a disconnect deliberately: a node
    // that went away did not withdraw what it said, and the stamp keeps saying
    // it about the moment it arrived. Forgetting a node *is* a withdrawal.
    // After it the watch is not paired with that node, will not reconnect to
    // it and has deleted its bond, so going on reporting its coordinate and
    // four bytes of its key states a source this watch has repudiated -- with
    // an age that only grows and nothing that can ever end it, because the one
    // thing that clears a retained observation is another node stating one.
    //
    // Only what the node said is dropped. `availability` and `receiver` are
    // re-derived from the provider on the next `poll()`, so they are left for
    // it rather than guessed at here.
    void forget();

private:
    PositionProvider& provider_;
    ValidityPolicy    policy_{};
    // The retained observation, and the age of everything is measured from
    // its own `observed_at`. Not from the poll that collected it: a poll is
    // this device's cadence and has nothing to do with when the coordinate
    // reached it, and a second stamp taken here would drift from the provider's
    // by however long the loop happened to take.
    std::optional<GnssObservation> observation_;
    MeshPeerId        origin_{};
    bool              has_origin_ = false;
    ReceiverPresence  receiver_   = ReceiverPresence::Unknown;
    Availability      availability_ = Availability::Unprovisioned;
};

const char* to_string(ReceiverPresence presence);

// The engineering line: the coordinate, **both** ages, the validity, the
// receiver state and the origin key, in one string.
//
// It is here rather than in a screen because the first consumer of a position
// in this repository must make the uncertainty the subject, and a formatter in
// core is testable on a host where a screen is not. `UNKNOWN` is written out in
// full wherever a number would otherwise imply a measurement -- which is every
// place rule 3 applies.
//
// Writes at most `size` bytes including the terminator and always terminates.
// Returns snprintf-style -- the length the line would have had -- so a caller
// can detect truncation, the same convention `attadipa/l10n/tr.h` already uses.
std::size_t format_location_line(const LocationState& state, char* out,
                                 std::size_t size);

}  // namespace attadipa::core
