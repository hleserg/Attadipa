#pragma once

#include <cstdint>

// Two clocks, and only one of them may be used to measure elapsed time.
//
// MeshCore already draws this line — mesh::RTCClock against
// mesh::MillisecondClock — and Firefly adopts it rather than rediscovering it
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

namespace firefly::core {

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

}  // namespace firefly::core
