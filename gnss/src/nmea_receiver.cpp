#include "attadipa/gnss/nmea_receiver.h"

#include <cstring>

extern "C" {
#include "minmea.h"
}

namespace attadipa::gnss {
namespace {

using core::Availability;
using core::FixType;
using core::GnssObservation;
using core::MonotonicTime;
using core::Position;

// A field is present when its scale is **positive**, never when it is merely
// non-zero. minmea's overflow guard can be defeated into producing a negative
// scale (kosma/minmea#104, open, recorded in the reuse ledger), and
// `scale != 0` would read that as a value.
constexpr bool present(const minmea_float& f)
{
    return f.scale > 0;
}

// `ddmm.mmmmm` plus its own hemisphere letter to degrees × 10^7, in integers.
//
// The magnitude and the letter are one fact and are checked as one, because
// neither is a coordinate without the other. NMEA gives the field an **unsigned**
// `ddmm.mmmmm` and puts the direction in a separate field, so three things have
// to hold before this is a place: the letter belongs to *this* axis, the
// magnitude is not itself signed, and the minutes are minutes.
//
//   - `0060.00000,N` is not 1° N. Sixty minutes is a degree the sentence
//     declined to carry, and normalising it here would invent a coordinate out
//     of a field the receiver got wrong — silently, and inside the globe range,
//     where nothing downstream can see it any more.
//   - `-0030.00004,S` is not 30′ S. minmea's `f` scanner takes a leading sign
//     (`gnss/vendor/minmea/minmea.c:168` — "                        } else if (*field == '-' && !sign && value == -1) {")
//     and `minmea_parse_rmc` then multiplies by the direction, so two negatives
//     land the watch 30′ **north** of where the sentence said. A leading `+` is
//     let through: it yields exactly the value the unsigned field would, so
//     there is nothing to be wrong about.
//   - Zero is a real place and stays one. Refusing every coordinate that came
//     out as zero would refuse a fix on the equator or the prime meridian — a
//     band 1.85 cm wide, but a lie of the same kind in the other direction, and
//     one nothing in the stream would explain.
//
// Not `minmea_rescale`, and not `minmea_tocoord`. The first multiplies when it
// scales up — `f->value * (new_scale/f->scale)` — which a sentence claiming
// `scale == 1` overflows on the way to 10^5. The second returns a float, and
// this tree does not decide where you are in floating point (`position.h`: with
// `-ffast-math` both `isnan()` and `x != x` answer false for an actual NaN).
//
// Everything below is int64 with room to spare: the largest honest numerator is
// a minute count times 10^7, six orders below the type's range, and the minutes
// are bounded *before* they are multiplied.
bool degrees_e7(const minmea_float& f, char letter, char positive, char negative,
                std::int32_t limit, std::int32_t& out)
{
    if (!present(f)) return false;
    if (letter != positive && letter != negative) return false;
    if (f.value < 0) return false;

    const std::int64_t value = f.value;
    const std::int64_t scale = f.scale;
    const std::int64_t degrees = value / scale / 100;             // dd
    const std::int64_t minutes = value - degrees * 100 * scale;   // mm.mmmm × scale
    if (minutes >= 60 * scale) return false;

    // 60 minutes to the degree, and the division is exact enough: 10^7 degrees
    // per degree over 60 minutes is 1.85 cm per unit, finer than any receiver
    // here resolves.
    const std::int64_t magnitude = degrees * 10000000 + (minutes * 10000000) / (60 * scale);
    if (magnitude > limit) return false;

    out = static_cast<std::int32_t>(letter == negative ? -magnitude : magnitude);
    return true;
}

// THE HEMISPHERE IS READ AS A LETTER, BECAUSE BY THE TIME MINMEA IS DONE WITH IT
// IT IS NO LONGER ONE.
// `gnss/vendor/minmea/minmea.c:138` — "            case 'd': { // Single character direction field (int)."
// maps `N` and `E` alike to `+1` and `S` and `W` alike to `-1`, so a latitude
// stamped `E` and a longitude stamped `N` are indistinguishable downstream from
// the letters that belong there: the sign that arrives says which half, never
// which axis. The same scanner answers 0 for an *absent* field rather than
// failing, and `minmea_parse_rmc()` then does `frame->latitude.value *=
// latitude_direction`, so `$GNRMC,135222.00,A,0030.00004,,00100.00004,E,...`
// parses clean, carries the `A` status, and states a latitude of exactly zero:
// the `(0, 0)` this slice exists not to show, wearing a valid fix flag.
//
// `c` keeps the character (`'\0'` when the field is empty), so the four fields
// are re-read here raw and validated as NMEA wrote them, ahead of any sign
// minmea applied. What `minmea_parse_rmc`/`_gga` made of the same bytes is not
// consulted for the position at all — there is nothing left in it to check.
//
// `_` skips a field, so each format walks to the four coordinate fields and
// nowhere else: after the talker, RMC's are the 3rd to 6th and GGA's the 2nd to
// 5th. Both formats consume the same five arguments.
bool coordinates(const char* sentence, bool rmc, Position& out)
{
    minmea_type type{};
    minmea_float latitude{};
    minmea_float longitude{};
    char ns = '\0';
    char ew = '\0';
    if (!minmea_scan(sentence, rmc ? "t__fcfc" : "t_fcfc", &type, &latitude, &ns, &longitude,
                     &ew)) {
        return false;
    }
    return degrees_e7(latitude, ns, 'N', 'S', core::kLatitudeMaxE7, out.latitude_e7) &&
           degrees_e7(longitude, ew, 'E', 'W', core::kLongitudeMaxE7, out.longitude_e7);
}

// Knots to millimetres per second: one knot is 1852 m/h.
bool speed_mm_s(const minmea_float& f, std::uint32_t& out)
{
    if (!present(f) || f.value < 0) return false;
    const std::int64_t mm_s =
        (static_cast<std::int64_t>(f.value) * 1852000) / (static_cast<std::int64_t>(f.scale) * 3600);
    if (mm_s > 0xFFFFFFFF) return false;
    out = static_cast<std::uint32_t>(mm_s);
    return true;
}

// Degrees to hundredths of a degree, for course over ground and for dilution.
// A value outside the range is refused rather than wrapped: a course of 400°
// is not a course of 40°, it is a receiver or a wire saying something wrong.
bool centi(const minmea_float& f, std::int64_t maximum, std::uint16_t& out)
{
    if (!present(f) || f.value < 0) return false;
    const std::int64_t value =
        (static_cast<std::int64_t>(f.value) * 100) / static_cast<std::int64_t>(f.scale);
    if (value > maximum) return false;
    out = static_cast<std::uint16_t>(value);
    return true;
}

// Metres to millimetres, signed: an altitude below the geoid is ordinary.
bool millimetres(const minmea_float& f, std::int32_t& out)
{
    if (!present(f)) return false;
    const std::int64_t mm =
        (static_cast<std::int64_t>(f.value) * 1000) / static_cast<std::int64_t>(f.scale);
    if (mm < -2147483648LL || mm > 2147483647LL) return false;
    out = static_cast<std::int32_t>(mm);
    return true;
}

}  // namespace

void NmeaReceiver::feed(const std::uint8_t* bytes, std::size_t count, MonotonicTime now)
{
    started_ = true;
    now_ = now;
    if (bytes == nullptr) return;
    for (std::size_t i = 0; i < count; ++i) {
        assemble(static_cast<char>(bytes[i]), now);
    }
}

void NmeaReceiver::assemble(char byte, MonotonicTime now)
{
    // Resynchronise on `$` and nowhere else. This is the assembler the reuse
    // ledger warns about by name: Meshtastic computed a checksum from a fixed
    // offset rather than from the located `$`, folded a leading newline into
    // it, and shipped that (PR #11293). Starting the buffer *at* the dollar
    // makes the offset the located one by construction.
    if (byte == '$') {
        collecting_ = true;
        overflowed_ = false;
        length_ = 0;
        line_[length_++] = byte;
        return;
    }
    if (!collecting_) {
        // Bytes before the first `$`, or the tail of a line already discarded.
        //
        // CR AND LF ARE NOT FAULTS HERE AND MUST NOT BE COUNTED. Every healthy
        // sentence ends `\r\n`; the `\r` closes the run below and clears
        // `collecting_`, so the `\n` that follows arrives on this branch on
        // every good line. Counting it would put a rising fault number on a
        // perfectly framed stream, which is the opposite of what the caller
        // reads this for.
        if (byte != '\r' && byte != '\n') {
            ++unframed_;
        }
        return;
    }
    if (byte == '\r' || byte == '\n') {
        collecting_ = false;
        if (overflowed_) {
            ++discarded_;
            return;
        }
        line_[length_] = '\0';
        take_sentence(now);
        return;
    }
    if (length_ >= kMaxSentence) {
        // Too long for NMEA. Discarded whole, never truncated: a truncated
        // sentence would fail its checksum anyway, and one that happened not to
        // is a sentence nobody sent.
        overflowed_ = true;
        return;
    }
    line_[length_++] = byte;
}

void NmeaReceiver::take_sentence(MonotonicTime now)
{
    // Strict, which requires the checksum to be there rather than accepting a
    // sentence that merely does not contradict one.
    if (!minmea_check(line_, true)) {
        ++discarded_;
        return;
    }

    heard_ = true;
    last_sentence_ = now;

    switch (minmea_sentence_id(line_, false)) {
    case MINMEA_SENTENCE_RMC: {
        minmea_sentence_rmc frame{};
        if (!minmea_parse_rmc(&frame, line_)) {
            ++discarded_;
            return;
        }
        // The epoch boundary. Whatever was open is complete: RMC is first and
        // once per second in every capture from both bench modules.
        close_epoch();

        open_ = GnssObservation{};
        open_.observed_at = now;
        open_.source = core::PositionSource::LocalGnss;
        open_valid_ = true;
        rmc_active_ = frame.valid;
        saw_gga_ = false;
        gga_quality_ = 0;
        gsa_fix_ = 0;

        if (frame.valid) {
            Position position{};
            if (coordinates(line_, true, position)) {
                open_.position = position;
            }
        }

        std::uint32_t speed = 0;
        if (speed_mm_s(frame.speed, speed)) open_.speed_mm_s = speed;

        // Absent, not north. This is the field the whole library was chosen
        // for: a receiver leaves course empty when it is standing still, and a
        // parser that could not represent that reported 0° for years.
        std::uint16_t course = 0;
        if (centi(frame.course, 35999, course)) open_.course_centideg = course;

        if (frame.date.year >= 0 && frame.time.hours >= 0) {
            const core::CivilTime civil{
                static_cast<std::int64_t>(frame.date.year) + 2000,
                static_cast<unsigned>(frame.date.month),
                static_cast<unsigned>(frame.date.day),
                0,
                static_cast<unsigned>(frame.time.hours),
                static_cast<unsigned>(frame.time.minutes),
                static_cast<unsigned>(frame.time.seconds),
            };
            core::WallTime utc{};
            // Range-checked by the conversion, February 29 in a common year
            // included. A receiver's claim about the date is input off a wire.
            if (core::wall_time_from_civil(civil, utc)) {
                open_.receiver_time = utc;
                // The receiver believes its own clock only when it says the fix
                // is valid; RMC has no separate flag for the time.
                open_.receiver_time_valid = frame.valid;
            }
        }
        break;
    }
    case MINMEA_SENTENCE_GGA: {
        if (!open_valid_) return;  // no epoch open yet; wait for the first RMC
        minmea_sentence_gga frame{};
        if (!minmea_parse_gga(&frame, line_)) {
            ++discarded_;
            return;
        }
        saw_gga_ = true;
        gga_quality_ = frame.fix_quality < 0 || frame.fix_quality > 255
                           ? 0
                           : static_cast<std::uint8_t>(frame.fix_quality);

        if (frame.satellites_tracked >= 0 && frame.satellites_tracked <= 255) {
            open_.satellites_used = static_cast<std::uint8_t>(frame.satellites_tracked);
        }

        std::uint16_t hdop = 0;
        if (centi(frame.hdop, 65535, hdop)) open_.hdop_centi = hdop;

        if (gga_quality_ != 0) {
            Position position{};
            if (coordinates(line_, false, position)) {
                // GGA over RMC when both are present: this is the sentence that
                // states the fix quality beside the coordinate. When GGA's
                // coordinate is the one refused, the RMC's stands rather than
                // the epoch going blank: a refusal here says this sentence has
                // no position to offer, not that the one already in hand was
                // wrong. The epoch is still `NoFix` if neither sentence had one.
                open_.position = position;
            }
            std::int32_t altitude = 0;
            if (frame.altitude_units == 'M' && millimetres(frame.altitude, altitude)) {
                open_.altitude_msl_mm = altitude;
                std::int32_t geoid = 0;
                // Height above the ellipsoid is the orthometric height plus the
                // geoid separation. Both, or neither: an ellipsoidal altitude
                // computed from a separation nobody sent is a guess.
                //
                // Summed in 64 bits and range-checked again, the way the
                // helpers above check each field alone. Both fit `int32_t`
                // separately and their sum need not: `int + int` is a 32-bit
                // add, so a GGA stating two absurd metre values would be signed
                // overflow — undefined behaviour, and this repository builds
                // its tests with `-fsanitize=undefined`. Out of range refuses
                // the ellipsoidal height rather than wrapping it; the
                // orthometric one passed its own check and stands.
                if (frame.height_units == 'M' && millimetres(frame.height, geoid)) {
                    const std::int64_t sum =
                        static_cast<std::int64_t>(altitude) + static_cast<std::int64_t>(geoid);
                    if (sum >= -2147483648LL && sum <= 2147483647LL) {
                        open_.altitude_ellipsoid_mm = static_cast<std::int32_t>(sum);
                    }
                }
            }
        }
        break;
    }
    case MINMEA_SENTENCE_GSA: {
        if (!open_valid_) return;
        minmea_sentence_gsa frame{};
        if (!minmea_parse_gsa(&frame, line_)) {
            ++discarded_;
            return;
        }
        // One GSA per constellation — 351 of them against 70 RMC in
        // `fix-20260904T1353Z.nmea` — all reporting the same solution and the
        // same dilutions. Latched, never accumulated: `satellites_used` is
        // GGA's count of the satellites in the fix, and counting the slots
        // across five GSAs would produce a number no receiver stated.
        if (frame.fix_type >= MINMEA_GPGSA_FIX_NONE && frame.fix_type <= MINMEA_GPGSA_FIX_3D) {
            gsa_fix_ = static_cast<std::uint8_t>(frame.fix_type);
        }
        std::uint16_t pdop = 0;
        if (centi(frame.pdop, 65535, pdop)) open_.pdop_centi = pdop;
        if (!open_.hdop_centi.has_value()) {
            std::uint16_t hdop = 0;
            if (centi(frame.hdop, 65535, hdop)) open_.hdop_centi = hdop;
        }
        break;
    }
    default:
        // VTG, GLL, GSV and the rest. Read past deliberately: the issue's scope
        // is RMC, GGA and GSA, and every field they would add is already here
        // or is one this parser refuses to guess at.
        break;
    }
}

void NmeaReceiver::reset()
{
    length_     = 0;
    collecting_ = false;
    overflowed_ = false;
    open_       = GnssObservation{};
    open_valid_ = false;
    rmc_active_ = false;
    saw_gga_    = false;
    gga_quality_ = 0;
    gsa_fix_     = 0;
}

void NmeaReceiver::close_epoch()
{
    if (!open_valid_) return;

    // Conservative on disagreement: any one of the three saying there is no fix
    // is enough.
    //
    // They never actually disagreed on the bench — 0 epochs in 6.5 MB of
    // capture from both modules have a GGA reporting a fix while every GSA of
    // the same second still says mode 1 — so this is a rule about what these
    // two receivers happened not to do, not about what a receiver may do.
    // Nothing in NMEA obliges the three sentences to agree, and a sentence
    // arriving off a wire is not obliged to try. Where they differ, taking the
    // better answer would mean picking the more flattering one.
    const bool no_fix = !rmc_active_ || (saw_gga_ && gga_quality_ == 0) || gsa_fix_ == 1 ||
                        !open_.position.has_value();

    if (no_fix) {
        open_.fix_type = FixType::NoFix;
    } else if (gga_quality_ == 6) {
        open_.fix_type = FixType::DeadReckoning;  // GGA's own code for it
    } else if (gsa_fix_ == 3) {
        open_.fix_type = FixType::ThreeD;
    } else {
        // Two-dimensional when GSA said so, and *also* when no GSA arrived at
        // all. A coordinate with nothing stating it was solved in three
        // dimensions is not a three-dimensional solution, and `classify()`
        // turns TwoD into a caveat rather than a refusal. GGA's altitude field
        // is not the missing evidence: a receiver publishes one whether or not
        // the height was solved.
        open_.fix_type = FixType::TwoD;
    }

    // The receiver's own words, for a field report that has to be diagnosable
    // after the fact. Opaque above this driver, so the packing is this file's
    // business: GGA quality in the low byte, GSA mode in the next. Two bytes,
    // not three: the RMC FAA mode character used to occupy a third and was
    // always zero, because `minmea_sentence_rmc` has no member for it and this
    // driver never read the field by hand. A byte reserved for a value nothing
    // writes is worse than an absent one — a field report would have read it as
    // "the receiver reported no mode".
    open_.native.vendor = 1;  // 1 is NMEA 0183 text, this driver
    open_.native.status = static_cast<std::uint32_t>(gga_quality_) |
                          (static_cast<std::uint32_t>(gsa_fix_) << 8);

    published_ = open_;
    has_published_ = true;
    open_valid_ = false;
}

Availability NmeaReceiver::availability() const
{
    // Nothing has been read yet, so nothing is bound. This is the state of a
    // board with empty pads, and it is what the Waveshare reports until a
    // module is soldered to `RXD`/`TXD`.
    if (!started_) return Availability::Unprovisioned;
    if (!heard_) return Availability::Unreachable;
    return core::elapsed(last_sentence_, now_) < silence_after_ ? Availability::Ready
                                                                : Availability::Unreachable;
}

bool NmeaReceiver::sample(core::PositionSample& out) const
{
    if (!has_published_) return false;

    out = core::PositionSample{};
    out.observation = published_;

    // A local receiver has no identity to be swapped underneath us, so
    // `has_origin` stays false and `LocationService`'s discard-on-changed-key
    // rule never fires. That rule is about a node public key; there is no
    // equivalent here and inventing one would give the service a fact to act on
    // that nothing measured.
    out.has_origin = false;

    // `Running` only while sentences are arriving. After the silence timeout
    // this goes back to `Unknown` rather than to `NotDetected`: something did
    // answer once, so "nothing answered" would be false, and `Unknown` is the
    // absence of a claim. The availability above carries the story.
    if (!started_) {
        out.receiver = core::ReceiverPresence::Unknown;
    } else if (!heard_) {
        out.receiver = core::ReceiverPresence::NotDetected;
    } else if (core::elapsed(last_sentence_, now_) < silence_after_) {
        out.receiver = core::ReceiverPresence::Running;
    } else {
        out.receiver = core::ReceiverPresence::Unknown;
    }
    return true;
}

}  // namespace attadipa::gnss
