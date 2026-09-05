#pragma once

#include <cstddef>
#include <cstdint>

#include "attadipa/core/location_service.h"
#include "attadipa/core/position.h"

// A local NMEA 0183 receiver, as a `core::PositionProvider`.
//
// The second provider this repository has, and the one with the best
// observation time in it — which is not the same as a real one, and the
// difference is worth stating because a later diagnostics or trust surface
// will be written against whichever of the two it believes.
//
// `link::NodePositionProvider` stamps its observation with the moment a
// coordinate *arrived*, because the node states nothing better. Here
// `observed_at` is the caller's tick at the moment the epoch's RMC was read,
// which is after the receiver solved by however long the caller waited between
// reads — in the direction that makes a fix look younger than it is. Bounded
// and small against `classify()`'s staleness window, so the staleness test is
// the first one this tree can meaningfully reach; still this end's clock
// reporting when it noticed, not the source stating when it sampled.
//
// **So `age_at_source_ms` stays empty for this provider too**, and
// `core::LocationService`'s rule that no producer states an observation age
// (`core/include/attadipa/core/location_service.h:192` — "    // How old the coordinate was when its source sampled it. **Always empty in")
// still holds. Writing that field from RMC's own UTC would need a wall clock
// trusted before the fix that is supposed to establish it.
//
// Bytes in, observations out. There is no task, no timer, no queue and no
// reconnect loop: the caller reads its UART however it already does and hands
// over what it got, on whatever tick it already has (ADR-0016). A caller that
// has nothing to hand over still calls `feed()` with zero bytes, because the
// silence timeout below is measured from the `now` this class is given and
// there is nowhere else it could come from.
//
// ## What NMEA cannot say, and is therefore not said
//
// `horizontal_accuracy_mm`, `vertical_accuracy_mm` and `speed_accuracy_mm_s`
// stay empty: no sentence in the set this parses carries an accuracy estimate,
// and GST — which does — is not in the stream either module emits. That is not
// a gap to paper over. `classify()` guards its accuracy test with
// `has_value()`, so an absent bound costs nothing, while a fabricated one would
// be a number nobody measured deciding whether a fix is trustworthy.
//
// `jamming` and `spoofing` stay at `ReceiverIndication::Unknown`, and the
// choice between that and `Unsupported` is deliberate. `Unsupported` means
// *this part cannot detect it*, which is a claim about the hardware; all this
// driver knows is that the protocol it reads has no field for the answer. The
// part may well detect interference and report it over UBX. Saying `Unknown`
// leaves that question where it belongs, with whoever writes that driver.
//
// `FixType::TimeOnly` is never produced. Telling a solved clock from one the
// receiver is propagating needs UBX; RMC says only valid or not.
//
// ## The epoch
//
// RMC, GGA and the several GSAs of one second are one observation, and RMC is
// the boundary: an arriving RMC closes the epoch that was open and starts a
// new one. It is first in every capture from both bench modules and appears
// exactly once per second (`fix-20260904T1353Z.nmea`: 70 GNRMC, 70 GNGGA, 351
// GNGSA — one RMC and one GGA per epoch, one GSA per constellation).
//
// Not the UTC stamp, which is the obvious key and is wrong: a receiver with no
// fix yet emits `$GNRMC,,V,,,,,,,,,,N,V*37` and `$GNGGA,,,,,,0,00,99.99,,,,,,*56`
// with the time field *empty*, so every no-fix epoch would share one absent key,
// nothing would ever close, and the watch would show "waiting" for a receiver
// that is powered, talking, and correctly reporting that it cannot see the sky.
//
// ponytail: an RMC lost to a bad checksum merges two epochs into one. The
// fields latch rather than accumulate, so the result is one observation with
// the later epoch's values and the earlier epoch's `observed_at` — up to a
// second early, against a 30 s staleness threshold. A sequence counter would
// close that and is not worth a field until something measures a stream where
// it matters.
//
// ## One known ceiling, measured rather than assumed
//
// ponytail: `LocationService::poll()` does not refresh either age when a
// sample repeats the coordinate it already holds — "an unchanged read is
// evidence against a live fix" — and that rule was written for the node
// provider, which restates one retained coordinate on every poll. A local
// receiver standing still can legitimately repeat a coordinate to the last
// digit, and a long enough run of them would age a live fix past 30 s and show
// `Stale` for a receiver that is solving perfectly well.
//
// It is left alone because the measurement says it does not happen here: over
// the 6.5 MB of bench capture from both modules, exact epoch-to-epoch repeats
// of the GGA coordinate run at 0–8.1% and **the longest consecutive run is two
// epochs** — two seconds against a thirty-second threshold. 10^-5 of a minute
// is 1.85 cm, and a consumer receiver jitters metres.
//
// AND THE CAPTURE WAS THE CASE THAT STRESSES THE RULE HARDEST, which is what
// makes the number load-bearing rather than lucky. The hazard is a receiver
// standing still, so a capture taken while carrying one about would measure the
// easy half and prove nothing. The GT-U12 half of the 6.5 MB was not carried:
// `docs/research/GNSS_MODULES_READOFF_2026-09-04.md:558` -- "the owner
// confirmed on 2026-09-05 that it" -- had sat untouched overnight, §5.1, which
// labels it an owner attestation rather than a MEASURED quantity and records
// that the AN3126 half has no such account. Hours of a receiver that never
// moved at all, and its longest run of identical coordinates is still two
// epochs.
//
// The upgrade path, if a module that latches its output ever turns up: give
// `core::PositionSample` a flag saying its `observed_at` is an observation time
// rather than an arrival stamp, and let `poll()` refresh on a repeat only for
// providers that set it. That cannot be done by comparing `observed_at`
// directly — `link/src/meshcore_companion.cpp:416` — "node_position_at_ = now;"
// — restamps on every accepted frame whether or not the coordinate moved, so a
// stamp comparison would switch the rule off exactly where it belongs.

namespace attadipa::gnss {

class NmeaReceiver final : public core::PositionProvider {
public:
    // How long a receiver may be silent before this stops calling it reachable.
    // Both bench modules emit at 1 Hz, so five seconds is five missed epochs.
    // The retained observation is *not* dropped when it expires — that is
    // `LocationService`'s decision and it keeps ageing the coordinate instead,
    // which is the honest answer for a fix that was real a moment ago.
    static constexpr core::Millis kDefaultSilence{5000};

    explicit NmeaReceiver(core::Millis silence_after = kDefaultSilence)
        : silence_after_(silence_after)
    {
    }

    // Hand over whatever the UART had. `count` may be zero, and calling with
    // zero on every idle tick is the intended use: it is what advances this
    // class's idea of now.
    void feed(const std::uint8_t* bytes, std::size_t count, core::MonotonicTime now);

    // Throw away the half-assembled sentence and the half-assembled epoch,
    // because the caller has just thrown away the bytes that would have
    // finished them. Only the caller knows that: a driver that flushes its
    // UART ring after a gap (`firmware/main/local_gnss.cpp`) leaves this class
    // holding an RMC stamped before the gap, which the next GGA would complete
    // and publish as if it had been observed now.
    //
    // What survives is deliberate: the last *published* observation stays, with
    // the `observed_at` it was really given — the watch did have that fix, and
    // `LocationService` ages it honestly from that stamp. So do `heard_`,
    // `last_sentence_`, `discarded_` and `unframed_`: a flush is not evidence
    // about the module, only about this end's attention, and the silence
    // timeout is measured from when a sentence last actually arrived.
    void reset();

    // `Unprovisioned` until the first `feed()` — nothing is bound yet, which is
    // the state of a board with no module soldered to the pads. After that,
    // `Ready` while sentences are arriving and `Unreachable` when they stop.
    // Never `Off`, and the reason is this class rather than the board: nothing
    // is fed in from which a rail state could be learned. `feed()` takes bytes
    // and a time; a provider cannot report a supply it is not told about.
    //
    // The board fact is deliberately not asserted here, because this repository
    // records it as unknown:
    // `docs/research/MAGNETOMETER_RETROFIT.md:1878` — "**Q5 · Is the `+3V3` expansion pad always-on or `ALDO1`-switched?**"
    // If Q5 resolves to `ALDO1`-switched then a rail on this device *does*
    // control a module on that pad, and `Off` becomes reachable — but only for
    // a caller that owns the rail and passes the state in. Nothing gates ALDO1
    // on this board today: `firmware/main/board_power.cpp:110` — "    {0x92, \"ALDO1\", RailPolicy::NotAuthorised," —
    // leaves it as the PMU brings it up.
    core::Availability availability() const override;

    bool sample(core::PositionSample& out) const override;

    // Framed sentences that arrived and were thrown away: a bad checksum, a
    // line longer than NMEA allows, or a field that would not parse. Framed
    // only — a byte that never got inside a `$`...CRLF run is not here, it is
    // in `unframed()`.
    std::uint32_t discarded() const { return discarded_; }

    // Bytes that arrived outside any sentence, CR and LF excluded because
    // every healthy sentence ends with them and counting those would report a
    // fault on a clean stream.
    //
    // THIS IS THE HALF THAT ANSWERS "IS ANYTHING ON THE WIRE AT ALL", which
    // `discarded()` cannot: that one counts only runs that framed, so a stream
    // which never frames leaves it at zero however long it talks, and zero
    // reads exactly like a pad with nothing on it.
    //
    // It does not say *what* is wrong, and no arithmetic on the pair will.
    // Binary at the right baud and NMEA at the wrong one produce the same
    // statistics — an incidental `0x24` about every 256 bytes and a terminator
    // about every 128 — so both counters climb in both cases and any ratio
    // named here would be a guess dressed as a tell. Two states, not more:
    // nothing at all, or bytes arriving that never frame.
    //
    // And the pair is a sample, not a census. Bytes inside a run that resyncs
    // on the next `$` without ever seeing a terminator are counted by neither,
    // which on such a stream is most of the traffic — 512 bytes of binary in
    // `tests/test_nmea_receiver.cpp` produce 58 and 1.
    std::uint32_t unframed() const { return unframed_; }

private:
    void assemble(char byte, core::MonotonicTime now);
    void take_sentence(core::MonotonicTime now);
    void close_epoch();

    // NMEA 0183 allows 82 characters including the leading `$` and the closing
    // CRLF. One more for the terminator this hands to minmea, which takes a
    // NUL-terminated string. Anything longer is discarded rather than truncated:
    // a truncated sentence with a recomputed checksum is a sentence nobody sent.
    static constexpr std::size_t kMaxSentence = 82;

    core::Millis        silence_after_;
    char                line_[kMaxSentence + 1] = {};
    std::size_t         length_    = 0;
    bool                collecting_ = false;
    bool                overflowed_ = false;

    bool                started_    = false;  // feed() has been called at least once
    core::MonotonicTime now_{};
    core::MonotonicTime last_sentence_{};
    bool                heard_      = false;  // a well-formed sentence, ever
    std::uint32_t       discarded_  = 0;
    std::uint32_t       unframed_   = 0;   // bytes outside any sentence, no CR/LF

    // The epoch being assembled, and the last one that closed.
    core::GnssObservation open_{};
    bool                  open_valid_ = false;   // an RMC has opened one
    bool                  rmc_active_ = false;   // RMC status was 'A'
    bool                  saw_gga_    = false;
    std::uint8_t          gga_quality_ = 0;
    std::uint8_t          gsa_fix_     = 0;      // 1 none, 2 two-d, 3 three-d

    core::GnssObservation published_{};
    bool                  has_published_ = false;
};

}  // namespace attadipa::gnss
