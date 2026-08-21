#pragma once

#include <cstdint>
#include <optional>

#include "attadipa/core/clock.h"

// A position, and everything the receiver said about how much it is worth.
//
// docs/adr/0011-gnss-integrity.md §1 and §2. Two rules shape this file and
// nothing here makes sense without them:
//
//   1. Nothing the receiver reports is dropped at the driver boundary because
//      the current consumer does not use it. A driver that normalizes and
//      discards has destroyed the evidence for its own bugs.
//   2. The states do not collapse. Availability, fix presence, freshness,
//      accuracy, integrity and trust are separate questions with separate
//      answers, and a provider can be Ready with a numerically perfect fix and
//      still be unusable.
//
// Every optional field means "the receiver did not say", which is not zero.
// This is the TinyGPS++ lesson from the reuse ledger, in the type system: a
// parser that could not represent "absent" reported an empty course-over-ground
// as 0° — due north — and it shipped that way for years.
//
// A note on std::optional: use `*opt` after checking `has_value()`, or
// `value_or()`. Do not call `.value()`. On a firmware build with -fno-exceptions
// its throw becomes an abort, which turns a missing field into a reset.

namespace attadipa::core {

// Integers, not floats, everywhere. Three reasons, and the third is the one
// that bites: fixed point has no NaN to be mistaken for a sentinel; it costs
// nothing on a part without an FPU in the paths that matter; and the reuse
// ledger records that with -ffast-math both isnan() and (x != x) return false
// for an actual NaN, so a float-based validity test is a correctness hazard
// rather than a style preference.
//
// 1e-7 degrees is about 1.1 cm at the equator, which is finer than any receiver
// here can produce, and int32 spans ±214.7 degrees — comfortably past ±180.
struct Position {
    std::int32_t latitude_e7  = 0;  // degrees × 10^7, WGS-84
    std::int32_t longitude_e7 = 0;  // degrees × 10^7, WGS-84
};

constexpr std::int32_t kLatitudeMaxE7  =  900000000;
constexpr std::int32_t kLongitudeMaxE7 = 1800000000;

// Coordinates arriving from a node, a mesh or a phone are untrusted input
// (ADR-0002 rule 4, final §11). The reuse ledger records a real crash from
// exactly this: geodesic code fed a hostile coordinate over the air,
// Meshtastic PR #10862. Range-check before anything narrows, indexes or
// multiplies.
constexpr bool in_range(Position p)
{
    return p.latitude_e7 >= -kLatitudeMaxE7 && p.latitude_e7 <= kLatitudeMaxE7 &&
           p.longitude_e7 >= -kLongitudeMaxE7 && p.longitude_e7 <= kLongitudeMaxE7;
}

// What kind of solution the receiver produced.
//
// No RTK entry, and that absence is deliberate: neither candidate receiver is
// an RTK part, and an enumerator nobody can produce is an invitation to write a
// branch that can never be tested. It arrives if a receiver that can do it
// does — ADR-0011 §4.
enum class FixType : std::uint8_t {
    Unknown,        // the receiver has not said yet
    NoFix,          // it said, and the answer is no
    TimeOnly,       // enough for a clock, not for a place
    TwoD,           // latitude and longitude; altitude is not solved
    ThreeD,         // latitude, longitude and altitude
    DeadReckoning,  // propagated, not observed. Only real if the part does it
};

inline constexpr std::uint8_t kFixTypeCount = static_cast<std::uint8_t>(FixType::DeadReckoning) + 1;

// Where the position came from. Applications never see this — ADR-0004 §2 —
// but Settings, Diagnostics and the trust engine all need it, the last of them
// because two sources disagreeing is evidence.
enum class PositionSource : std::uint8_t {
    Unknown,
    LocalGnss,      // a receiver on this board
    NodeGnss,       // an Attadipa node's receiver, over the node link
    Companion,      // a phone
    Manual,         // the user typed it
    Simulated,      // the simulator or a replayed fixture. Never shipped as real
};

// What the receiver itself says about interference, and about whether it thinks
// it is being lied to.
//
// `Unsupported` and `Unknown` are different answers and both are common here:
// the MIA-M10Q's capabilities are not yet read from u-blox's own documents
// (T-051), and the LS550G's anti-spoofing is `UNKNOWN` until a primary source
// says otherwise — the vendor's marketing page is not a source (OD-5 §2).
enum class ReceiverIndication : std::uint8_t {
    Unsupported,  // this part cannot detect it. Absence of alarm means nothing
    Unknown,      // it can, and has not reported yet
    None,         // it can, and reports nothing wrong
    Warning,      // it can, and something is off
    Critical,     // it can, and it is bad
};

// A receiver's own bound on its error, where it produces one. Not the same as
// an accuracy estimate: accuracy is "how wrong I probably am", a protection
// level is "how wrong I am willing to promise I am not".
struct ProtectionLevel {
    std::uint32_t horizontal_mm = 0;
    std::uint32_t vertical_mm   = 0;
    bool          valid         = false;  // the receiver may publish an invalid one
};

// The receiver's own words, kept verbatim.
//
// Opaque above the driver — nothing here may be interpreted by a service or an
// application, and the vocabulary differs per vendor. It exists so that a field
// report is diagnosable and so that a normalization discovered to be wrong can
// be re-derived from what was actually received, rather than from what we
// decided it meant at the time.
struct ReceiverNative {
    std::uint16_t vendor       = 0;  // driver-assigned; 0 means "not recorded"
    std::uint32_t status       = 0;
    std::uint32_t security     = 0;
    std::uint32_t interference = 0;
};

// One observation from one provider.
//
// Wide on purpose. ADR-0011 §1 makes the width a rule rather than an accident,
// and the cost — this is the largest value type in core — is accepted knowingly
// against the alternative, which is a watch that points confidently in the
// wrong direction.
struct GnssObservation {
    // --- when, and by which clock -----------------------------------------
    //
    // `observed_at` is monotonic, and that is not a detail. Freshness measured
    // against the wall clock stops working the moment a fix steps it, which is
    // exactly the moment freshness matters most (clock.h).
    MonotonicTime observed_at{};

    // The receiver's own idea of absolute time, and whether it believes it.
    // Kept separate from the device clock so that disagreement between them is
    // available as evidence rather than resolved silently.
    std::optional<WallTime> receiver_time;
    bool receiver_time_valid = false;

    // --- the solution -------------------------------------------------------
    FixType fix_type = FixType::Unknown;
    std::optional<Position>     position;
    std::optional<std::int32_t> altitude_msl_mm;
    std::optional<std::int32_t> altitude_ellipsoid_mm;
    std::optional<std::uint32_t> speed_mm_s;
    std::optional<std::uint16_t> course_centideg;  // 0..35999; absent when still

    // --- how good the receiver thinks it is ---------------------------------
    std::optional<std::uint32_t> horizontal_accuracy_mm;
    std::optional<std::uint32_t> vertical_accuracy_mm;
    std::optional<std::uint32_t> speed_accuracy_mm_s;
    std::optional<std::uint16_t> hdop_centi;  // dilution × 100
    std::optional<std::uint16_t> pdop_centi;

    // --- what it can see ----------------------------------------------------
    std::optional<std::uint8_t> satellites_used;
    std::optional<std::uint8_t> satellites_in_view;
    std::optional<std::uint8_t> cn0_max_dbhz;
    std::optional<std::uint8_t> cn0_mean_dbhz;

    // --- what it thinks of its own signals ----------------------------------
    ReceiverIndication jamming  = ReceiverIndication::Unknown;
    ReceiverIndication spoofing = ReceiverIndication::Unknown;
    std::optional<ProtectionLevel> protection_level;

    // --- provenance ---------------------------------------------------------
    PositionSource source = PositionSource::Unknown;
    ReceiverNative native{};
};

// How good a position is *as a position* — not whether a provider exists
// (Availability), and not whether anything is lying (TrustState).
//
// Ordered worst to best so that "at least Degraded" is a comparison rather than
// a switch, and so a fold over several providers can take the best without a
// table.
enum class PositionValidity : std::uint8_t {
    NoFix,     // there is no position at all
    Stale,     // there was one, and it is too old to act on
    Degraded,  // usable, with a caveat the interface must show
    Valid,     // usable
};

inline constexpr std::uint8_t kPositionValidityCount =
    static_cast<std::uint8_t>(PositionValidity::Valid) + 1;

// The thresholds that turn numbers into one of those four.
//
// Policy, not physics — which is why it is a parameter rather than a constant.
// The defaults are pedestrian-scale and are `ESTIMATED`, not measured: nobody
// has yet walked around with one of these boards. The ledger's warning applies
// to the speed gate in ADR-0009 too — every documented figure found so far was
// designed for a vehicle, not a wrist.
struct ValidityPolicy {
    Millis        stale_after{30000};           // 30 s without a fresh fix
    std::uint32_t degraded_accuracy_mm = 25000;  // worse than 25 m is a caveat
    std::uint16_t degraded_hdop_centi  = 500;    // HDOP > 5.0
    std::uint8_t  min_satellites       = 4;      // fewer cannot solve 3D
};

// Classify an observation as of `now`.
//
// Pure, total and deterministic — no clock read inside, no allocation, no
// hidden state. That is what makes the replay rig possible: the same fixture
// produces the same verdict on every machine, every run.
PositionValidity classify(const GnssObservation& observation, MonotonicTime now,
                          const ValidityPolicy& policy);

const char* to_string(FixType type);
const char* to_string(PositionSource source);
const char* to_string(ReceiverIndication indication);
const char* to_string(PositionValidity validity);

}  // namespace attadipa::core
