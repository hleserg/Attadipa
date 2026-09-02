#pragma once

// Setting the wall clock: one sequence, one place, in every image.
//
// Today the sequence lives inside `#if CONFIG_ATTADIPA_WATCH_CONTROL` in
// `waveshare_board.cpp`, and `firmware/sdkconfig.defaults` sets that to `n`,
// so a product image cannot write its own clock. #356's first
// Definition-of-Done bullet is that it can. This header is included
// unconditionally; the gated `TimeSink` becomes one instantiation of it and
// the entry screen the next one. The instantiation in `tests/` is what checks
// the order below, on the `meshcore_write_outcome.h` pattern: a rule tested
// through a copy is not tested.
//
// `attadipa::debug::` is not reachable here -- `firmware/main/CMakeLists.txt`
// adds the debug layer under the same gate -- so the request is scalars, not
// `debug::TimeSyncBody`.

#include <cstdint>
#include <limits>

#include "pcf85063_time.h"

#include "attadipa/apps/clock.h"
#include "attadipa/core/time_service.h"

namespace attadipa::firmware {

enum class ProvisionTimeResult : std::uint8_t {
    Accepted,  // RTC written and verified, metadata stored, service moved.
    Rejected,  // Not a request this watch accepts. Nothing moved.
    Failed,    // Storage or the chip refused. Nothing moved -- see below.
};

struct TimeProvisionRequest {
    std::int64_t utc_seconds = 0;
    std::int16_t timezone_offset_minutes = 0;
    std::uint32_t valid_for_ms = 0;
    bool allow_large_correction = false;
};

// The flash side of one synchronization, stored as **one NVS blob**. One blob
// rather than one key per field because a single `nvs_set_blob` is atomic on
// the pinned ESP-IDF and a pair of `nvs_set_*` is not: `nvs_commit()` is a
// no-op there, each set reaches flash on its own, and a sequence of two can
// fail after the first has landed. `Storage::writeItem` writes a blob's new
// version whole before it erases the old one, and the index a reader looks up
// first is written after the data chunks, so a torn blob reads as absent and
// a reader gets the old value or the new one, never a mixture. Both facts,
// with the vendor lines, are in `docs/research/VERIFIED_FACTS.md`; they are
// two facts, and the second does not follow from the first.
//
// `last_sync_utc` has no reader yet. It is stored because it is free and
// because a watch that shows a UTC offset should be able to say when it got
// it.
struct TimeMetadata {
    std::int16_t offset_minutes = 0;
    std::int64_t last_sync_utc = 0;
};

enum class MetadataRead : std::uint8_t { Present, Absent, Unreadable };

// `Ops` is the storage and the chip:
//
//   MetadataRead read_metadata(TimeMetadata *out);
//   bool save_metadata(const TimeMetadata &);      // atomic; false = untouched
//   bool erase_metadata();                          // absent = success
//   bool write_and_verify_rtc(const RtcDateTime &, std::int64_t utc_seconds);
//
// `write_and_verify_rtc` is one operation and not three because its three
// failures -- the write, the read back, a mismatch -- get the same answer
// from this sequence, and three copies of a rollback is how one is forgotten.

// Metadata first, chip second. The order is #396's: the write that boot can
// already know is doomed -- an unusable NVS -- runs before the one that
// rewrites hardware, so it fails closed. A refused save leaves the old blob
// readable (see `TimeMetadata`) and needs no rollback. A refused chip write
// comes after the new blob is on flash, and the product image restores that
// blob into the time service on every boot, so `previous` goes back on that
// path -- as the blob it was, or as no blob at all.
template <typename Ops>
ProvisionTimeResult provision_time(Ops &ops, const TimeProvisionRequest &request,
                                   core::TimeService &service,
                                   core::MonotonicTime now) {
    if (request.valid_for_ms == 0 ||
        now.ms > std::numeric_limits<std::uint64_t>::max() - request.valid_for_ms) {
        return ProvisionTimeResult::Rejected;
    }

    apps::CivilTime civil;
    if (!apps::civil_from_wall_time(core::WallTime{request.utc_seconds}, civil) ||
        civil.year < 2000 || civil.year > 2099) {
        return ProvisionTimeResult::Rejected;
    }
    const RtcDateTime rtc{static_cast<unsigned>(civil.year), civil.month,
                          civil.day,  civil.weekday,  civil.hour,  civil.minute,
                          civil.second};

    core::TimeService candidate = service;
    const core::MonotonicTime valid_until{now.ms + request.valid_for_ms};
    if (!candidate.set_timezone(request.timezone_offset_minutes, valid_until, now) ||
        !candidate.observe({core::WallTime{request.utc_seconds}, now,
                            core::Millis{request.valid_for_ms}, 0,
                            core::TimeSource::Manual, core::TimeQuality::Trusted,
                            request.allow_large_correction})) {
        return ProvisionTimeResult::Rejected;
    }

    TimeMetadata previous{};
    const MetadataRead had = ops.read_metadata(&previous);
    if (had == MetadataRead::Unreadable) {
        return ProvisionTimeResult::Failed;
    }
    if (!ops.save_metadata({request.timezone_offset_minutes, request.utc_seconds})) {
        return ProvisionTimeResult::Failed;
    }
    if (!ops.write_and_verify_rtc(rtc, request.utc_seconds)) {
        // Best effort: a rollback that fails is logged by the ops and leaves
        // an offset on flash for a synchronization that did not happen, which
        // the next successful one replaces.
        if (had == MetadataRead::Present) {
            ops.save_metadata(previous);
        } else {
            ops.erase_metadata();
        }
        return ProvisionTimeResult::Failed;
    }

    service = candidate;
    return ProvisionTimeResult::Accepted;
}

}  // namespace attadipa::firmware
