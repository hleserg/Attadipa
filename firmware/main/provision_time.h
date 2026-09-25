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

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "pcf85063_time.h"

#include "attadipa/apps/clock.h"
#include "attadipa/core/time_service.h"

namespace attadipa::firmware {

enum class ProvisionTimeResult : std::uint8_t {
    Accepted,  // RTC written and verified, metadata stored, service moved.
    Rejected,  // Not a request this watch accepts. Nothing moved.
    Failed,    // Storage or the chip refused. The chip may have moved; what
               // boot reads is from a verified synchronization.
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

// What flash holds is the two fields and nothing else: ten bytes, little
// endian, in declaration order. Writing the struct itself would put its six
// bytes of alignment padding on unencrypted flash with whatever the stack held
// -- aggregate initialisation sets members, not padding -- and would let a
// field added into that gap keep `sizeof` at sixteen, which is the whole of
// the schema check the reader has. A blob of any other length reads as absent.
constexpr std::size_t kTimeMetadataBytes = 10;
using TimeMetadataBytes = std::array<std::uint8_t, kTimeMetadataBytes>;

constexpr TimeMetadataBytes encode_time_metadata(const TimeMetadata &metadata)
{
    TimeMetadataBytes out{};
    const auto offset = static_cast<std::uint16_t>(metadata.offset_minutes);
    const auto sync = static_cast<std::uint64_t>(metadata.last_sync_utc);
    out[0] = static_cast<std::uint8_t>(offset);
    out[1] = static_cast<std::uint8_t>(offset >> 8);
    for (std::size_t i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<std::uint8_t>(sync >> (8 * i));
    }
    return out;
}

constexpr TimeMetadata decode_time_metadata(const TimeMetadataBytes &bytes)
{
    std::uint64_t sync = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        sync |= static_cast<std::uint64_t>(bytes[2 + i]) << (8 * i);
    }
    const auto offset = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1] << 8));
    return {static_cast<std::int16_t>(offset), static_cast<std::int64_t>(sync)};
}

// `Ops` is the storage and the chip:
//
//   bool stage_metadata(const TimeMetadata &);  // a key boot never reads
//   bool save_metadata(const TimeMetadata &);   // the key boot reads
//   bool write_and_verify_rtc(const RtcDateTime &, std::int64_t utc_seconds);
//
// `write_and_verify_rtc` is one operation and not three because its three
// failures -- the write, the read back, a mismatch -- get the same answer
// from this sequence.

// Stage, chip, commit (#625). Boot reads one key, and it is written only after
// the chip has been verified, so no failure leaves boot an offset for a
// synchronization that did not happen, and there is nothing to roll back.
// The staging write keeps #396's fail-closed order: a store that refuses
// writes is found before the chip is touched, and whatever it left behind is
// in a key nothing reads. A refused save is not a save that did nothing:
// `Storage::writeItem` erases the old version *after* the new one is written
// and indexed, and reports that erase failing as a failure
// (`nvs_storage.cpp:546`-`:549` in VERIFIED_FACTS). So a commit that fails
// after a verified chip write leaves boot's key holding the old blob or the
// new one, each from a verified synchronization. The chip is not put back:
// `Failed` reaches the owner as "the clock may have moved", and the next
// synchronization finishes the job.
template <typename Ops>
ProvisionTimeResult provision_time(Ops &ops, const TimeProvisionRequest &request,
                                   core::TimeService &service,
                                   core::MonotonicTime now) {
    if (request.valid_for_ms == 0 ||
        now.ms > std::numeric_limits<std::uint64_t>::max() - request.valid_for_ms) {
        return ProvisionTimeResult::Rejected;
    }

    core::CivilTime civil;
    if (!core::civil_from_wall_time(core::WallTime{request.utc_seconds}, civil) ||
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

    const TimeMetadata next{request.timezone_offset_minutes, request.utc_seconds};
    if (!ops.stage_metadata(next) || !ops.write_and_verify_rtc(rtc, request.utc_seconds) ||
        !ops.save_metadata(next)) {
        return ProvisionTimeResult::Failed;
    }

    service = candidate;
    return ProvisionTimeResult::Accepted;
}

}  // namespace attadipa::firmware
