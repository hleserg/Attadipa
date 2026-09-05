#pragma once

#include <cstdint>

// Two clocks, and only one of them may be used to measure elapsed time.
//
// MeshCore already draws this line — mesh::RTCClock against
// mesh::MillisecondClock — and Attadipa adopts it rather than rediscovering it
// (docs/upstream/meshcore-1.17-review.md §7, TASKS T-047). The owner states the
// rule directly: *wall clock нельзя использовать для измерения elapsed time.*
//
// The enforcement is in the types rather than in a comment. `MonotonicTime`
// subtracts and yields a duration. `WallTime` does not subtract at all — there
// is no operator- for it anywhere in this header, and adding one would delete
// the rule silently. If you need "how long since", you need the monotonic
// clock; if you genuinely need "what date is it", you need the wall clock and
// you may not derive a duration from it.
//
// Why it matters here and not only in the abstract: a GNSS fix can step the
// wall clock, forwards or backwards, by an arbitrary amount. Every timeout,
// retry, connection expiry and scheduler deadline measured against it would
// fire early, late, or never. And in the one subsystem that has to *notice* a
// spoofed time step (docs/adr/0011-gnss-integrity.md §6), a detector built on
// the stepped clock is a detector that a spoofer switches off for free.

namespace attadipa::core {

// A duration. Milliseconds, because that is the resolution every timeout in
// this system is expressed in and a smaller unit would invite false precision.
struct Millis {
    std::uint32_t value = 0;

    constexpr bool operator==(Millis other) const { return value == other.value; }
    constexpr bool operator!=(Millis other) const { return value != other.value; }
    constexpr bool operator<(Millis other) const { return value < other.value; }
    constexpr bool operator<=(Millis other) const { return value <= other.value; }
    constexpr bool operator>(Millis other) const { return value > other.value; }
    constexpr bool operator>=(Millis other) const { return value >= other.value; }
};

// Time since boot, in milliseconds. Never wall time, never adjusted, never
// stepped. 64 bits because a 32-bit millisecond counter wraps after 49.7 days
// and upstream has already shipped that bug — MeshCore's #2937, "GPS time sync
// stall on long uptime nodes". A wearable that is worn is a long-uptime node.
struct MonotonicTime {
    std::uint64_t ms = 0;

    constexpr bool operator==(MonotonicTime other) const { return ms == other.ms; }
    constexpr bool operator!=(MonotonicTime other) const { return ms != other.ms; }
    constexpr bool operator<(MonotonicTime other) const { return ms < other.ms; }
    constexpr bool operator<=(MonotonicTime other) const { return ms <= other.ms; }
    constexpr bool operator>(MonotonicTime other) const { return ms > other.ms; }
    constexpr bool operator>=(MonotonicTime other) const { return ms >= other.ms; }
};

// How much time passed between two monotonic readings.
//
// `to` before `from` is not an error the caller can recover from by ignoring
// it — a monotonic clock going backwards is a broken clock — so it yields zero
// rather than an enormous unsigned number. A silent 4-billion-millisecond
// timeout is worse than a zero one.
constexpr Millis elapsed(MonotonicTime from, MonotonicTime to)
{
    if (to.ms <= from.ms) {
        return Millis{0};
    }
    const std::uint64_t delta = to.ms - from.ms;
    // Saturate rather than wrap. A duration longer than 49 days is already
    // outside every policy in this codebase, and the saturated value keeps
    // comparisons ordered.
    return Millis{delta > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(delta)};
}

constexpr MonotonicTime operator+(MonotonicTime at, Millis d)
{
    return MonotonicTime{at.ms + d.value};
}

// Absolute time, UNIX epoch seconds. Signed, because dates before 1970 are
// representable in a corrupted or hostile input and clamping them to zero would
// hide the corruption.
//
// Deliberately has no arithmetic. Comparison is allowed — "is this timestamp
// before that one" is a legitimate question about two absolute instants — but
// there is no subtraction, so a duration cannot be derived from it by accident.
struct WallTime {
    std::int64_t unix_seconds = 0;

    constexpr bool operator==(WallTime other) const { return unix_seconds == other.unix_seconds; }
    constexpr bool operator!=(WallTime other) const { return unix_seconds != other.unix_seconds; }
    constexpr bool operator<(WallTime other) const { return unix_seconds < other.unix_seconds; }
    constexpr bool operator>(WallTime other) const { return unix_seconds > other.unix_seconds; }
};

// How far apart two wall clocks are, in seconds, as a magnitude.
//
// The paragraph above still holds: WallTime has no subtraction, because a gap
// between two *absolute* instants is not a monotonic interval and must never be
// used as one. This is not that subtraction. It answers one question — "how far
// apart are these two clocks" — and it is here rather than at the call site
// because the alternative was tried and it was wrong.
//
// What it replaces: a caller reached through `.unix_seconds`, wrote `a - b` and
// then negated the result if it came out below zero. Both steps are undefined
// behaviour for input this type deliberately admits. `a - b` overflows when the
// two are at opposite ends of the range, and negating INT64_MIN has no
// representable answer at all — which one hostile `receiver_time` field is
// enough to reach, in the anti-spoofing detector, whose entire job is hostile
// input.
//
// Unsigned is not a detail. The magnitude of the difference of two int64 values
// does not fit in an int64 and does fit exactly in a uint64, so this is the only
// integer type in which the question has a total answer. Nothing here can
// overflow: unsigned wraparound is defined, and larger-minus-smaller is exact
// for every pair.
constexpr std::uint64_t seconds_between(WallTime a, WallTime b)
{
    const auto first  = static_cast<std::uint64_t>(a.unix_seconds);
    const auto second = static_cast<std::uint64_t>(b.unix_seconds);
    return a < b ? second - first : first - second;
}

// The calendar, in integers.
//
// This lived in `apps/clock.cpp` while the clock face was the only thing that
// had a date to render. It is here now because a second layer needs it and that
// layer is *below* applications: an NMEA receiver states its own idea of the
// date in RMC, and `GnssObservation::receiver_time` is a `WallTime`, so the
// driver has to do this conversion before it can report what the receiver said.
// A driver library reaching up into `apps/` to borrow calendar arithmetic would
// invert the layer order for a function that was never about applications.
//
// Howard Hinnant's civil-from-days algorithm, unchanged in the move. No
// `mktime`, no `timegm`, no `struct tm`, no locale and no timezone: this is UTC
// arithmetic over int64 seconds, which is what both callers actually want and
// is the only version that is the same on the host and on the device.
struct CivilTime {
    std::int64_t year    = 1970;
    unsigned     month   = 1;
    unsigned     day     = 1;
    unsigned     weekday = 4;  // 0 = Sunday
    unsigned     hour    = 0;
    unsigned     minute  = 0;
    unsigned     second  = 0;
};

constexpr bool is_leap_year(std::int64_t year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr unsigned days_in_month(std::int64_t year, unsigned month)
{
    constexpr unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 0 || month > 12
               ? 0
               : (month == 2 && is_leap_year(year) ? 29 : days[month - 1]);
}

constexpr std::int64_t days_from_civil(std::int64_t year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned     yoe = static_cast<unsigned>(year - era * 400);
    const unsigned     doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned     doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// False when the result is outside year 1..9999 — `out` is still written, so a
// caller that only wants to render something has a value, and a caller that
// needs a date it can trust has an answer about it.
constexpr bool civil_from_wall_time(WallTime time, CivilTime& out)
{
    std::int64_t days    = time.unix_seconds / 86400;
    std::int64_t seconds = time.unix_seconds % 86400;
    if (seconds < 0) {
        seconds += 86400;
        --days;
    }

    const std::int64_t shifted = days + 719468;
    const std::int64_t era     = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const unsigned     doe     = static_cast<unsigned>(shifted - era * 146097);
    const unsigned     yoe     = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t       year    = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned     doy     = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned     mp      = (5 * doy + 2) / 153;
    const unsigned     day     = doy - (153 * mp + 2) / 5 + 1;
    const unsigned     month   = mp < 10 ? mp + 3 : mp - 9;
    year += month <= 2;

    std::int64_t weekday = (days + 4) % 7;
    if (weekday < 0) {
        weekday += 7;
    }
    out = {year,
           month,
           day,
           static_cast<unsigned>(weekday),
           static_cast<unsigned>(seconds / 3600),
           static_cast<unsigned>((seconds / 60) % 60),
           static_cast<unsigned>(seconds % 60)};
    return year >= 1 && year <= 9999;
}

// False leaves `out` untouched. Every field is range-checked first, February 29
// in a common year included, because the callers are a typed date and a
// receiver's own claim about what day it is — the second of which is untrusted
// input arriving over a wire.
constexpr bool wall_time_from_civil(const CivilTime& civil, WallTime& out)
{
    if (civil.year < 1 || civil.year > 9999 || civil.day == 0 ||
        civil.day > days_in_month(civil.year, civil.month) || civil.hour > 23 ||
        civil.minute > 59 || civil.second > 59) {
        return false;
    }
    out.unix_seconds = days_from_civil(civil.year, civil.month, civil.day) * 86400 +
                       static_cast<std::int64_t>(civil.hour * 3600 + civil.minute * 60 +
                                                 civil.second);
    return true;
}

}  // namespace attadipa::core
