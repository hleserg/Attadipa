#pragma once

#include <cstdint>

// Two clocks, and only one of them may be used to measure elapsed time.
//
// MeshCore already draws this line — mesh::RTCClock against
// mesh::MillisecondClock — and Attadipa adopts it rather than rediscovering it
// (docs/upstream/meshcore-1.17-review.md §7, TASKS T-047). The owner states the
// rule directly: the wall clock must never be used to measure elapsed time.
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

}  // namespace attadipa::core
