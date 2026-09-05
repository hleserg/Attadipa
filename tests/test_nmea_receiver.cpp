#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "attadipa/apps/navigation.h"
#include "attadipa/core/location_service.h"
#include "attadipa/gnss/nmea_receiver.h"

// Host tests for the NMEA front-end and the provider it feeds.
//
// The fixture is `gnss/bench-epochs.nmea`: sentences two real receivers
// actually emitted on 2026-09-04, with every coordinate replaced by the replay
// rig's deliberately-nowhere position and every checksum recomputed. That is
// the whole point of it — a synthetic sentence tests the shape somebody
// imagined, and the shape that broke every parser in the reuse ledger was the
// one a real receiver sent: NMEA 4.10 field counts, an empty course field
// while standing still, five GSAs to one RMC, and a no-fix epoch whose UTC
// field is empty too.
//
// **NOT EXECUTED — HARDWARE REQUIRED** for anything about a receiver on a
// board. Nothing here opens a UART. What it proves is that given these bytes
// this parser produces these observations, which is a claim about software.

using namespace attadipa;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// One second later, each call. Epochs arrive at 1 Hz from both bench modules,
// so the test's clock runs at the rate the fixture was captured at.
core::MonotonicTime g_now{};

std::vector<std::string> fixture_lines()
{
    const std::string path = std::string(ATTADIPA_GNSS_FIXTURE_DIR) + "/bench-epochs.nmea";
    std::vector<std::string> out;
    std::FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        std::fprintf(stderr, "FAIL: cannot open %s\n", path.c_str());
        ++failures;
        return out;
    }
    char line[256];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        // `#` is the reader's, not the receiver's. Blank lines go too: what is
        // handed to the parser is what a UART would deliver.
        if (text.empty() || text[0] == '#') continue;
        out.push_back(text);
    }
    std::fclose(file);
    return out;
}

// Hand one line to the receiver the way a UART would, terminator included.
void deliver(gnss::NmeaReceiver& receiver, const std::string& line)
{
    const std::string wire = line + "\r\n";
    receiver.feed(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size(), g_now);
}

core::WallTime utc(std::int64_t year, unsigned month, unsigned day, unsigned hour,
                   unsigned minute, unsigned second)
{
    core::WallTime out{};
    CHECK(core::wall_time_from_civil({year, month, day, 0, hour, minute, second}, out));
    return out;
}

// ---------------------------------------------------------------------------

void a_receiver_nobody_has_wired_up_says_so()
{
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    // Nothing has been read yet, so nothing is bound. This is the Waveshare
    // today: the pads are there and no module is on them.
    CHECK(receiver.availability() == core::Availability::Unprovisioned);
    CHECK(!receiver.sample(sample));

    // The UART is open and silent. A provider is bound now, and it is not
    // answering — which is a different sentence and a different remedy.
    receiver.feed(nullptr, 0, g_now);
    CHECK(receiver.availability() == core::Availability::Unreachable);
    CHECK(!receiver.sample(sample));
}

void the_cold_start_epochs_are_a_fix_type_not_a_silence()
{
    const std::vector<std::string> lines = fixture_lines();
    gnss::NmeaReceiver receiver;

    // The first three epochs of the fixture: RMC status V, GGA quality 0, GSA
    // mode 1, and no UTC anywhere. Feed exactly those and stop at the first
    // sentence that carries a timestamp, which is the first epoch with a fix.
    std::size_t fed = 0;
    for (const std::string& line : lines) {
        if (line.rfind("$GNRMC,", 0) == 0) {
            if (line.rfind("$GNRMC,,V", 0) != 0) break;
            ++fed;
        }
        g_now.ms += 100;
        deliver(receiver, line);
    }
    CHECK(fed == 3);

    core::PositionSample sample;
    // Two of the three epochs have closed — the third is still open, waiting
    // for the RMC that would end it — and that is enough to answer.
    CHECK(receiver.sample(sample));
    CHECK(receiver.availability() == core::Availability::Ready);
    CHECK(sample.receiver == core::ReceiverPresence::Running);

    // THE INTERESTING PART. The receiver is powered, talking and correctly
    // reporting that it cannot see the sky. That is `NoFix`, not "waiting for
    // GPS", and the two are different sentences: one says the device is fine
    // and the sky is not, the other says nobody knows anything yet.
    CHECK(sample.observation.fix_type == core::FixType::NoFix);
    CHECK(!sample.observation.position.has_value());
    CHECK(!sample.observation.receiver_time.has_value());
    CHECK(!sample.observation.receiver_time_valid);
    CHECK(!sample.has_origin);
    CHECK(sample.observation.source == core::PositionSource::LocalGnss);

    // 99.99 is what a receiver prints for "no solution", and it is carried
    // through as the number it is rather than dropped for being implausible:
    // 9999 hundredths is twenty times the degraded threshold, so `classify()`
    // reaches its own conclusion from evidence rather than from an absence
    // this driver manufactured.
    CHECK(sample.observation.hdop_centi.has_value() && *sample.observation.hdop_centi == 9999);
    CHECK(sample.observation.satellites_used.has_value() &&
          *sample.observation.satellites_used == 0);

    CHECK(core::classify(sample.observation, g_now, {}) == core::PositionValidity::NoFix);
}

void a_real_epoch_arrives_whole()
{
    const std::vector<std::string> lines = fixture_lines();
    gnss::NmeaReceiver receiver;
    for (const std::string& line : lines) {
        g_now.ms += 100;
        deliver(receiver, line);
    }

    // Exactly the bytes the fixture puts outside a sentence, and not one more.
    // Two lines produce them: the 47-byte tail of a sentence whose `$` was lost
    // (`tests/gnss/bench-epochs.nmea:49` — "30.00004,N,00100.00004,E,0.085,,040926,,,D,V*1F") and the four
    // bytes of noise after it. 51 together.
    //
    // THE NUMBER IS THE CR/LF PROOF. `deliver()` ends every line `\r\n`; the
    // `\r` closes the run and clears `collecting_`, so the `\n` behind it
    // reaches the outside-a-sentence branch on *every* line of this fixture. A
    // counter that did not exclude line terminators would report one per
    // sentence on top of these, and would call a receiver reading a clean
    // stream faulty.
    CHECK(receiver.unframed() == 51);

    core::PositionSample sample;
    CHECK(receiver.sample(sample));
    const core::GnssObservation& o = sample.observation;

    // The *fourth* fix epoch, not the fifth, and that is the epoch rule
    // showing through rather than an off-by-one: an epoch closes when the next
    // RMC arrives, so the last one in a file stays open with no RMC to end it.
    // A receiver does not stop after five seconds, and the alternative —
    // publishing an epoch the moment its RMC lands — would publish before GGA
    // and GSA had said anything, so every observation would arrive without a
    // satellite count, a dilution or a fix type.
    //
    // `0030.00007,N` and `00100.00007,E`, as degrees × 10^7 out of
    // `ddmm.mmmmm` in integers: 0° 30.00007′ is 0.5000011° and 1° 0.00007′ is
    // 1.0000011°.
    CHECK(o.position.has_value());
    CHECK(o.position->latitude_e7 == 5000011);
    CHECK(o.position->longitude_e7 == 10000011);
    CHECK(core::in_range(*o.position));

    // GSA said mode 3 and GGA said quality 2, so this is a three-dimensional
    // differential fix and nothing here rounds it down or up.
    CHECK(o.fix_type == core::FixType::ThreeD);
    CHECK(o.satellites_used.has_value() && *o.satellites_used == 12);
    CHECK(o.hdop_centi.has_value() && *o.hdop_centi == 158);
    CHECK(o.pdop_centi.has_value() && *o.pdop_centi == 242);

    // 12.7 m above the geoid, 25.0 m of separation, so 37.7 m above the
    // ellipsoid. Both or neither: an ellipsoidal height computed from a
    // separation nobody sent would be arithmetic on a guess.
    CHECK(o.altitude_msl_mm.has_value() && *o.altitude_msl_mm == 12700);
    CHECK(o.altitude_ellipsoid_mm.has_value() && *o.altitude_ellipsoid_mm == 37700);

    // 0.163 knots on a desk. One knot is 1852 m/h, so 83 mm/s.
    CHECK(o.speed_mm_s.has_value() && *o.speed_mm_s == 83);

    // THE FIELD THIS LIBRARY WAS CHOSEN FOR. The module was standing still and
    // left course empty. Empty is empty — not 0°, not due north. A parser that
    // could not represent this shipped north for years (REUSE_LEDGER.md, the
    // TinyGPS++ entry), and it is the single reason minmea is vendored here
    // rather than the more popular library.
    CHECK(!o.course_centideg.has_value());

    // NMEA states no accuracy: GST is not in either module's stream. Absent,
    // never invented — `classify()` guards its accuracy test with has_value(),
    // so an absent bound costs nothing and a fabricated one would decide
    // whether a fix is trustworthy from a number nobody measured.
    CHECK(!o.horizontal_accuracy_mm.has_value());
    CHECK(!o.vertical_accuracy_mm.has_value());
    CHECK(!o.speed_accuracy_mm_s.has_value());

    // Nor does this driver say anything about interference. `Unknown` rather
    // than `Unsupported`: the part may well detect jamming and report it over
    // UBX, and all this front-end knows is that the protocol it reads has no
    // field for the answer.
    CHECK(o.jamming == core::ReceiverIndication::Unknown);
    CHECK(o.spoofing == core::ReceiverIndication::Unknown);
    CHECK(!o.protection_level.has_value());

    // The receiver's own clock, from RMC's date and time, believed exactly as
    // far as RMC's own status flag believes it.
    CHECK(o.receiver_time.has_value());
    CHECK(*o.receiver_time == utc(2026, 9, 4, 13, 52, 25));
    CHECK(o.receiver_time_valid);

    // The receiver's own words, kept verbatim for a field report: GGA quality
    // in the low byte, GSA mode in the next.
    CHECK(o.native.vendor == 1);
    CHECK((o.native.status & 0xFFu) == 2);
    CHECK(((o.native.status >> 8) & 0xFFu) == 3);

    // One sentence in the fixture has a wrong checksum and one line is a
    // fragment with no `$`. The corrupt sentence is counted; the fragment is
    // not, because the assembler never started collecting it.
    CHECK(receiver.discarded() == 1);

    CHECK(core::classify(o, g_now, {}) == core::PositionValidity::Valid);

    // And the epoch left open does close, on the next RMC and on nothing else.
    // The fifth epoch's coordinate appears only now — which is the boundary
    // rule stated as a behaviour rather than as a comment.
    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,135227.00,A,0030.00009,N,00100.00009,E,0.010,,040926,,,D,V*1B");
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.position->latitude_e7 == 5000013);
    CHECK(sample.observation.position->longitude_e7 == 10000013);
    CHECK(*sample.observation.receiver_time == utc(2026, 9, 4, 13, 52, 26));
}

void a_torn_sentence_is_dropped_whole()
{
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    // Bytes with no `$` in front of them are not a sentence and are not
    // counted: the assembler was never collecting.
    const char noise[] = "55.91895,N,03710.92055,E*1F\r\n";
    receiver.feed(reinterpret_cast<const std::uint8_t*>(noise), sizeof(noise) - 1, g_now);
    CHECK(!receiver.sample(sample));
    CHECK(receiver.discarded() == 0);

    // A `$` mid-line restarts the buffer there, which is what makes the
    // checksum offset the located one rather than a fixed one — the exact bug
    // the reuse ledger records against Meshtastic PR #11293.
    deliver(receiver, "garbage$GNRMC,135222.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*12");
    deliver(receiver, "$GNRMC,135223.00,A,0030.00005,N,00100.00005,E,0.093,,040926,,,D,V*14");
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.position.has_value());
    CHECK(sample.observation.position->latitude_e7 == 5000006);
    CHECK(receiver.discarded() == 0);

    // A sentence longer than NMEA allows is discarded rather than truncated. A
    // truncated sentence would fail its checksum anyway; one that happened not
    // to is a sentence nobody sent.
    std::string overlong = "$GNRMC,135224.00,A,0030.00006,N,00100.00006,E,0.101,";
    overlong.append(120, 'x');
    overlong += "*12";
    deliver(receiver, overlong);
    CHECK(receiver.discarded() == 1);

    // And one whose checksum simply does not match.
    deliver(receiver, "$GNRMC,135225.00,A,0030.00007,N,00100.00007,E,0.163,,040926,,,D,V*00");
    CHECK(receiver.discarded() == 2);

    // And one with no checksum at all. Strict means the checksum has to *be
    // there*, not merely to agree when present — minmea's loose mode accepts
    // this sentence, and a receiver on a wire that can corrupt a checksum is a
    // receiver on a wire that can corrupt the coordinate beside it.
    deliver(receiver, "$GNRMC,135226.00,A,0030.00008,N,00100.00008,E,0.163,,040926,,,D,V");
    CHECK(receiver.discarded() == 3);

    // Neither reached the observation: the position is still the one from the
    // epoch that closed before them.
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.position->latitude_e7 == 5000006);
}

void a_field_that_defeats_the_overflow_guard_is_not_a_position()
{
    // kosma/minmea#104, open, and the reason the reuse ledger says a field is
    // present when `scale > 0` and **never** when `scale != 0`.
    //
    // The defeat: `value = 10 * value + digit` never grows past the guard while
    // the digits are zeros, but `scale *= 10` runs on every one of them, so a
    // long enough run of leading zeros overflows `scale` into a negative
    // number while `value` is still 0. Reproduced against the vendored copy —
    // twelve fractional zeros give `scale == -727379968`.
    //
    // Under `scale != 0` that field divides by a negative scale and yields
    // latitude 0, longitude 0: null island, on the equator off the coast of
    // Ghana, rendered as a fix. Under `scale > 0` there is no position, which
    // is the truth — and the watch says NoFix instead of pointing somewhere.
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,140000.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*12");
    deliver(receiver,
            "$GPGGA,140000.00,0.000000000000,N,0.000000000000,E,1,08,1.00,10.0,M,25.0,M,,*56");
    deliver(receiver, "$GNRMC,140001.00,A,0030.00005,N,00100.00005,E,0.085,,040926,,,D,V*13");

    // The sentence itself is well formed — a correct checksum, a quality of 1 —
    // so it is not discarded. It is *believed*, and what it says about the
    // coordinate is nothing.
    CHECK(receiver.discarded() == 0);
    CHECK(receiver.sample(sample));

    // The RMC in the same epoch stated a real coordinate, so that is what
    // survives: GGA is preferred when it has one, and here it has none.
    CHECK(sample.observation.position.has_value());
    CHECK(sample.observation.position->latitude_e7 == 5000006);
    CHECK(sample.observation.position->longitude_e7 == 10000006);

    // The altitude *does* survive, and the two are separate on purpose: the
    // GGA's quality was 1 and its altitude field parsed, so the height is a
    // number the receiver stated. Only the coordinate was unreadable, and only
    // the coordinate is refused. Rejecting one field does not condemn the
    // sentence around it.
    CHECK(sample.observation.altitude_msl_mm.has_value());
}

void two_altitudes_that_each_fit_and_do_not_together()
{
    // Each field passes `millimetres()` on its own — 2 000 000 m is 2e9 mm,
    // inside `int32_t` — and their sum is 4e9, which is not. `int + int` is a
    // 32-bit add, so before the fix this was signed overflow: undefined
    // behaviour, and CI builds these tests with `-fsanitize=undefined
    // -fno-sanitize-recover=all`, so it was a crash waiting for a GGA nobody
    // had sent yet.
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,135222.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*12");
    deliver(receiver,
            "$GNGGA,135222.00,0030.00004,N,00100.00004,E,1,08,1.20,2000000.0,M,2000000.0,M,,*7E");
    deliver(receiver, "$GNRMC,135223.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*13");

    // The sentence framed and was believed. Without this the test would pass
    // for the wrong reason: a mistyped checksum discards the GGA, both
    // altitudes go missing, and the assertion below greens on an empty epoch.
    CHECK(receiver.discarded() == 0);
    CHECK(receiver.sample(sample));

    // The orthometric height passed its own range check and stands. Refusing
    // it too would throw away a number the receiver actually stated because a
    // *different* field made a sum impossible.
    CHECK(sample.observation.altitude_msl_mm.has_value());
    CHECK(*sample.observation.altitude_msl_mm == 2000000000);

    // The ellipsoidal one is absent, not wrapped. Wrapped it would be about
    // -295 000 km, which is a number no consumer has any defence against.
    CHECK(!sample.observation.altitude_ellipsoid_mm.has_value());
}

void a_clock_before_a_fix_is_a_clock_the_receiver_does_not_vouch_for()
{
    // Two real sentences from `boot4-20260904T140439Z.nmea`, verbatim — they
    // carry a time and a date and no coordinate, so there is nothing in them
    // to scrub. This is what a receiver sends in the seconds after it has
    // decoded a satellite's clock and before it can solve a position.
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,140459.20,V,,,,,,,040926,,,N,V*1F");
    deliver(receiver, "$GNRMC,140500.00,V,,,,,,,040926,,,N,V*10");

    CHECK(receiver.sample(sample));
    CHECK(sample.observation.fix_type == core::FixType::NoFix);
    CHECK(!sample.observation.position.has_value());

    // The time is carried — ADR-0011 §1, nothing the receiver said is dropped
    // — and it is carried *unbelieved*. RMC has one status flag for the whole
    // sentence, the receiver set it to V, and `receiver_time_valid` says so
    // rather than quietly promoting a clock the source did not vouch for.
    CHECK(sample.observation.receiver_time.has_value());
    CHECK(*sample.observation.receiver_time == utc(2026, 9, 4, 14, 4, 59));
    CHECK(!sample.observation.receiver_time_valid);

    // `FixType::TimeOnly` is not produced, and this is the epoch that would
    // tempt it. Telling a solved clock from one the receiver is propagating
    // needs UBX; RMC says only valid or not, and a `TimeOnly` minted from a
    // guess is an enumerator no test could ever check against a receiver.
    CHECK(sample.observation.fix_type != core::FixType::TimeOnly);
}

void one_sentence_saying_no_fix_is_enough()
{
    // The three sentences of an epoch are not obliged to agree, and where they
    // differ the pessimistic one wins. This case did not occur on the bench —
    // 0 epochs in 6.5 MB of capture from both modules — so it is a rule about
    // what a receiver *may* send rather than about what these two did, and a
    // sentence arriving off a wire is not obliged to try to agree at all.
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,141000.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*13");
    deliver(receiver, "$GNGGA,141000.00,0030.00004,N,00100.00004,E,1,08,1.00,10.0,M,25.0,M,,*7B");
    deliver(receiver, "$GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,1*33");
    deliver(receiver, "$GNRMC,141001.00,A,0030.00005,N,00100.00005,E,0.085,,040926,,,D,V*12");

    // RMC says A and GGA says quality 1. GSA says mode 1, which is "no fix",
    // and that is the answer. Taking the other two would be picking the more
    // flattering evidence.
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.fix_type == core::FixType::NoFix);
    CHECK(core::classify(sample.observation, g_now, {}) == core::PositionValidity::NoFix);

    // The coordinate the two optimistic sentences carried is still recorded —
    // ADR-0011 §1, nothing the receiver said is dropped at the driver boundary
    // — and `classify()` refuses it on the fix type rather than on an absence
    // this driver manufactured.
    CHECK(sample.observation.position.has_value());
}

void silence_is_not_the_same_as_never_having_answered()
{
    gnss::NmeaReceiver receiver{core::Millis{5000}};
    core::PositionSample sample;

    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,135222.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*12");
    deliver(receiver, "$GNRMC,135223.00,A,0030.00005,N,00100.00005,E,0.093,,040926,,,D,V*14");
    CHECK(receiver.availability() == core::Availability::Ready);
    CHECK(receiver.sample(sample) && sample.receiver == core::ReceiverPresence::Running);

    // Four seconds of nothing. Still within the window, still reachable.
    g_now.ms += 4000;
    receiver.feed(nullptr, 0, g_now);
    CHECK(receiver.availability() == core::Availability::Ready);

    // Past it. The provider is bound and is not answering — and the coordinate
    // it already stated is *not* withdrawn, because a receiver that went quiet
    // did not retract what it said. Ageing that coordinate is
    // `LocationService`'s job and it does it from the observation stamp.
    g_now.ms += 2000;
    receiver.feed(nullptr, 0, g_now);
    CHECK(receiver.availability() == core::Availability::Unreachable);
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.position.has_value());

    // `Unknown`, not `NotDetected`: something did answer once, so "nothing
    // answered" would be false. The absence of a claim is the honest state.
    CHECK(sample.receiver == core::ReceiverPresence::Unknown);
}

void bytes_that_never_frame_are_counted_as_bytes()
{
    // THE CASE `discarded()` CANNOT SEE: bytes that never frame, so no run is
    // ever thrown away and `discarded()` stays at zero however long the module
    // talks — which reads identically to a pad with nothing on it.
    gnss::NmeaReceiver receiver;
    const std::vector<std::uint8_t> quiet_noise(64, 0x00);
    g_now.ms += 1000;
    receiver.feed(quiet_noise.data(), quiet_noise.size(), g_now);

    CHECK(receiver.discarded() == 0);
    CHECK(receiver.unframed() == 64);

    // Nothing framed, so nothing was heard, and the provider says so rather
    // than inventing a presence out of traffic it could not read.
    core::PositionSample sample;
    CHECK(!receiver.sample(sample));
    CHECK(receiver.availability() != core::Availability::Ready);

    // The two numbers survive the flush, because the question they answer —
    // has anything ever been on this wire — is not re-opened by this end
    // deciding to stop trusting a gap's worth of bytes.
    receiver.reset();
    CHECK(receiver.unframed() == 64);
    CHECK(receiver.discarded() == 0);

    // And a real sentence afterwards frames normally: dropping bytes on the
    // floor did not leave the assembler mid-line. The count keeps its history
    // rather than being cleared by the recovery.
    deliver(receiver, "$GNRMC,135222.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*12");
    CHECK(receiver.unframed() == 64);
    CHECK(receiver.availability() == core::Availability::Ready);
}

void a_binary_stream_moves_both_numbers_and_neither_names_it()
{
    // THE CLAIM THIS TEST EXISTS TO REFUTE, WHICH THE HEADER USED TO MAKE:
    // that a module in a binary protocol never emits `$`, so `unframed()`
    // climbs alone and the pair therefore names the fault. It does not. `0x24`
    // is an ordinary byte and so are CR and LF, so a binary stream frames runs
    // and fails them exactly as a stream at the wrong baud rate does.
    //
    // Every byte value twice, which is the least contrived binary there is.
    gnss::NmeaReceiver receiver;
    std::vector<std::uint8_t> sweep;
    for (int pass = 0; pass < 2; ++pass) {
        for (int b = 0; b <= 255; ++b) {
            sweep.push_back(static_cast<std::uint8_t>(b));
        }
    }
    g_now.ms += 1000;
    receiver.feed(sweep.data(), sweep.size(), g_now);

    // Both numbers move. `discarded()` at zero would have meant the old claim
    // held; it is not zero, so a bench session reading these two cannot tell
    // this stream from NMEA at 4800 baud, and the log must not pretend it can.
    CHECK(receiver.unframed() == 58);
    CHECK(receiver.discarded() == 1);

    // AND THE PAIR IS A SAMPLE, NOT A CENSUS. 512 bytes went in and the two
    // counters describe 59 events: the first `$` at 0x24 opens a run that
    // overflows and is finally closed by the `\n` of the second pass — one
    // discard standing for 230 bytes — and the second pass's `$` opens a run
    // of 219 that is still open when the feed ends and is counted nowhere.
    // Nothing downstream may divide by these or scale them to a byte rate.
    CHECK(receiver.unframed() + receiver.discarded() < sweep.size() / 8);

    // Nothing was heard, which is the one thing the pair does establish.
    core::PositionSample sample;
    CHECK(!receiver.sample(sample));
    CHECK(receiver.availability() != core::Availability::Ready);
}

void the_chain_ends_in_a_location_state()
{
    // The seam this whole slice exists for: bytes off a wire, through the
    // provider, into the service an application actually reads.
    const std::vector<std::string> lines = fixture_lines();
    gnss::NmeaReceiver receiver;
    core::LocationService location(receiver);

    for (const std::string& line : lines) {
        g_now.ms += 100;
        deliver(receiver, line);
        location.poll();
    }

    const core::LocationState state = location.state(g_now);
    CHECK(state.availability == core::Availability::Ready);
    CHECK(state.has_position);
    CHECK(state.position.value.latitude_e7 == 5000011);
    CHECK(state.position.value.longitude_e7 == 10000011);
    CHECK(state.validity == core::PositionValidity::Valid);
    CHECK(state.source == core::PositionSource::LocalGnss);
    CHECK(state.fix_type == core::FixType::ThreeD);
    CHECK(state.receiver == core::ReceiverPresence::Running);

    // No origin, so nothing can be swapped underneath the service and its
    // discard-on-changed-key rule never fires. A local receiver has no public
    // key and inventing one would hand the service a fact nobody measured.
    CHECK(!state.has_origin);

    // `age_at_source` stays empty for this provider too, and that is not an
    // oversight: the field means "how old was the coordinate when its source
    // sampled it", and this provider stamps `observed_at` with the caller's
    // tick at the moment the RMC was *read*. That is this end's clock noticing,
    // not the receiver stating when it solved, so there is still no answer to
    // put in the field.
    CHECK(!location.age_at_source(g_now).has_value());

    // And `age_at_us` is small, because `observed_at` is the moment the epoch's
    // RMC arrived rather than boot or zero. It is the best stamp in this tree
    // and still an arrival stamp, which is exactly why it has to be checked:
    // the fixture's last closed epoch is seven sentences back at
    // 100 ms each, and nothing about this chain should make it look older.
    CHECK(location.age_at_us(g_now).has_value());
    CHECK(*location.age_at_us(g_now) < core::Millis{2000});

    // Ten minutes later, with the receiver gone quiet, the same coordinate is
    // still held and is no longer current. Stale, not NoFix and not Valid:
    // there *was* a position, and acting on it now would be acting on where
    // the watch was ten minutes ago.
    const core::MonotonicTime later{g_now.ms + 600000};
    receiver.feed(nullptr, 0, later);
    location.poll();
    const core::LocationState aged = location.state(later);
    CHECK(aged.availability == core::Availability::Unreachable);
    CHECK(aged.has_position);
    CHECK(aged.validity == core::PositionValidity::Stale);
}

}  // namespace

// AND THE LAST LINK, WHICH IS THE ONE THE ISSUE IS ABOUT.
//
// Everything above proves the parser reaches a `LocationState`. This proves
// the state reaches a sentence a person reads: issue #429's fourth scope item
// is that the readout "stops saying `Waiting for GPS` and starts showing a
// real distance and bearing", and until this ran, no test in this repository
// had ever put a *parsed* position into `own` — `tests/test_navigation.cpp`
// builds its own by hand, which proves the formatter and not the chain.
//
// It is still a host test. It says nothing about a module on the pads.
void the_readout_stops_saying_waiting_for_gps()
{
    gnss::NmeaReceiver receiver;
    core::LocationService location(receiver);
    for (const std::string& line : fixture_lines()) {
        g_now.ms += 100;
        deliver(receiver, line);
        location.poll();
    }

    apps::NavState nav;
    nav.own = location.state(g_now);
    // What the node link produces, and no more: a coordinate, an arrival age,
    // no fix type. Due north of the fixture's position by 66730e-7 degrees.
    nav.target.availability = core::Availability::Ready;
    nav.target.has_position = true;
    nav.target.position.value = {5066741, 10000011};
    nav.target.position.age_at_us_ms = 3000;
    nav.target.validity = core::PositionValidity::NoFix;
    nav.target.source = core::PositionSource::NodeGnss;

    const apps::NavText text = apps::format_navigation(nav);
    CHECK(text.has_distance);
    CHECK(text.has_bearing);
    CHECK(std::strcmp(text.distance, "\xE2\x80\x94") != 0);
    CHECK(std::strcmp(text.distance, "742 m") == 0);
    CHECK(std::strcmp(text.bearing, "000\xC2\xB0") == 0);

    // The caveat survives, and must: the *node* still states no fix type, so
    // the readout keeps saying so even though this device's own position is
    // now a real one. A chain that silenced that would have made the local
    // receiver launder the node's uncertainty.
    CHECK(text.caveat[0] != '\0');
    // And the status is no longer the one this test is named after.
    CHECK(text.status_code != apps::NavStatus::WaitingForGps);
}

// The driver flushes its UART ring after a gap because ESP-IDF's ring drops the
// *new* bytes when it fills, so what survives a long silence is the oldest data
// (`firmware/main/local_gnss.cpp:155` — "// WHAT IS IN THE RING AFTER A GAP IS
// NOT A FIX, IT IS A MEMORY."). Flushing the ring is only half of it: the epoch
// this class had half-assembled is made of those same bytes, and a GGA arriving
// after the gap would have completed it and published a post-gap coordinate
// under a pre-gap `observed_at` — the arrival-time-as-observation-time lie the
// flush exists to prevent, running backwards.
void a_flush_takes_the_open_epoch_with_it()
{
    gnss::NmeaReceiver receiver;
    core::PositionSample sample;

    const std::uint64_t before = g_now.ms;
    deliver(receiver, "$GNRMC,135222.00,A,0030.00004,N,00100.00004,E,0.085,,040926,,,D,V*12");
    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,135227.00,A,0030.00009,N,00100.00009,E,0.010,,040926,,,D,V*1B");
    // The first epoch closed on the second RMC, stamped when it opened. The
    // second epoch is open and unpublished — RMC is the boundary, so an epoch
    // is published by the sentence *after* it, and that is why the stamp below
    // does not advance across the gap.
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.observed_at.ms == before);

    // Ten minutes of sleep, and the driver's flush.
    g_now.ms += 600000;
    const std::uint64_t after = g_now.ms;
    receiver.reset();
    receiver.feed(nullptr, 0, g_now);

    // What was already published survives, and must: the watch did have that
    // fix, with the stamp it was really given. `classify()` ages it from there,
    // which is the honest answer — dropping it would be the opposite lie.
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.observed_at.ms == before);

    // Now the bytes that were in flight when the gap began. Without the reset
    // this GGA completes the epoch opened before the sleep and the RMC after it
    // publishes the pair.
    deliver(receiver, "$GNGGA,135222.00,0030.00004,N,00100.00004,E,2,12,1.58,12.4,M,25.0,M,,*79");
    deliver(receiver, "$GNRMC,135227.00,A,0030.00009,N,00100.00009,E,0.010,,040926,,,D,V*1B");
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.observed_at.ms == before);  // still the old one

    // And the epoch that opened after the gap closes with its own stamp.
    g_now.ms += 1000;
    deliver(receiver, "$GNRMC,135227.00,A,0030.00009,N,00100.00009,E,0.010,,040926,,,D,V*1B");
    CHECK(receiver.sample(sample));
    CHECK(sample.observation.observed_at.ms == after);
}

int main()
{
    a_receiver_nobody_has_wired_up_says_so();
    the_cold_start_epochs_are_a_fix_type_not_a_silence();
    a_real_epoch_arrives_whole();
    a_torn_sentence_is_dropped_whole();
    a_field_that_defeats_the_overflow_guard_is_not_a_position();
    two_altitudes_that_each_fit_and_do_not_together();
    one_sentence_saying_no_fix_is_enough();
    a_clock_before_a_fix_is_a_clock_the_receiver_does_not_vouch_for();
    silence_is_not_the_same_as_never_having_answered();
    a_flush_takes_the_open_epoch_with_it();
    bytes_that_never_frame_are_counted_as_bytes();
    a_binary_stream_moves_both_numbers_and_neither_names_it();
    the_chain_ends_in_a_location_state();
    the_readout_stops_saying_waiting_for_gps();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("nmea_receiver: all checks passed "
                "(host only — NOT EXECUTED, HARDWARE REQUIRED for a receiver)\n");
    return 0;
}
