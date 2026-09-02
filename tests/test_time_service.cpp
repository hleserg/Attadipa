#include <cstdio>
#include <limits>

#include "attadipa/core/time_service.h"
#include "pcf85063_time.h"
#include "provision_time.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

attadipa::core::TimeObservation sample(
    attadipa::core::WallTime utc, attadipa::core::MonotonicTime observed_at,
    attadipa::core::TimeSource source, attadipa::core::TimeQuality quality,
    attadipa::core::Millis fresh_for = {},
    std::uint32_t age_at_source_ms = 0)
{
    return {utc, observed_at, fresh_for, age_at_source_ms, source, quality,
            false};
}

// ---- provision_time.h: the one sequence that sets the clock ----------------

// The storage and the chip, as a test sees them. The store is one blob that is
// there or is not -- `nvs_set_blob` lands whole or leaves the old value -- but
// it can land *and* report failure, when the erase of the old version after
// it fails, so a refused save has two shapes and the fake has both.
struct FakeTimeOps {
    bool has_blob = false;
    attadipa::firmware::TimeMetadata blob{};
    bool fail_read = false, fail_save = false, fail_rtc = false;
    bool save_lands_then_fails = false;  // once: the next save only
    int saves = 0, erases = 0, rtc_writes = 0;

    attadipa::firmware::MetadataRead read_metadata(
        attadipa::firmware::TimeMetadata *out)
    {
        if (fail_read) return attadipa::firmware::MetadataRead::Unreadable;
        if (!has_blob) return attadipa::firmware::MetadataRead::Absent;
        *out = blob;
        return attadipa::firmware::MetadataRead::Present;
    }
    bool save_metadata(const attadipa::firmware::TimeMetadata &m)
    {
        ++saves;
        if (fail_save) return false;
        blob = m;
        has_blob = true;
        if (save_lands_then_fails) {
            save_lands_then_fails = false;
            return false;
        }
        return true;
    }
    bool erase_metadata()
    {
        ++erases;
        has_blob = false;
        blob = {};
        return true;
    }
    bool write_and_verify_rtc(const attadipa::firmware::RtcDateTime &,
                              std::int64_t)
    {
        ++rtc_writes;
        return !fail_rtc;
    }
};

const attadipa::core::MonotonicTime kProvisionNow{1000};
// 2026-09-02T00:00:00Z, inside the years the PCF85063 can hold.
const std::int64_t kProvisionUtc = 1788307200;

attadipa::firmware::TimeProvisionRequest provision_request()
{
    attadipa::firmware::TimeProvisionRequest r;
    r.utc_seconds = kProvisionUtc;
    r.timezone_offset_minutes = 300;
    r.valid_for_ms = 60000;
    return r;
}

void test_provision_time()
{
    using attadipa::core::TimeService;
    using attadipa::core::TimeSource;
    using attadipa::firmware::ProvisionTimeResult;
    using attadipa::firmware::provision_time;

    // Accepted: the service moves and the blob holds this synchronization.
    {
        FakeTimeOps ops;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Accepted);
        CHECK(svc.state(kProvisionNow).source == TimeSource::Manual);
        CHECK(ops.has_blob && ops.blob.offset_minutes == 300 &&
              ops.blob.last_sync_utc == kProvisionUtc);
        CHECK(ops.rtc_writes == 1 && ops.erases == 0);
    }
    // Rejected before anything is touched: no validity window ...
    {
        FakeTimeOps ops;
        TimeService svc;
        auto r = provision_request();
        r.valid_for_ms = 0;
        CHECK(provision_time(ops, r, svc, kProvisionNow) ==
              ProvisionTimeResult::Rejected);
        CHECK(ops.saves == 0 && ops.rtc_writes == 0);
        CHECK(svc.state(kProvisionNow).source == TimeSource::None);
    }
    // ... a window whose deadline overflows ...
    {
        FakeTimeOps ops;
        TimeService svc;
        auto r = provision_request();
        r.valid_for_ms = 0xFFFFFFFFu;
        CHECK(provision_time(ops, r, svc,
                             attadipa::core::MonotonicTime{
                                 0xFFFFFFFFFFFFFF00ull}) ==
              ProvisionTimeResult::Rejected);
        CHECK(ops.rtc_writes == 0);
    }
    // ... and a year the chip cannot hold.
    {
        FakeTimeOps ops;
        TimeService svc;
        auto r = provision_request();
        r.utc_seconds = 0;  // 1970
        CHECK(provision_time(ops, r, svc, kProvisionNow) ==
              ProvisionTimeResult::Rejected);
        CHECK(ops.rtc_writes == 0);
    }
    // Unreadable metadata stops the sequence before the chip: the fail-closed
    // check, whose whole value is the order.
    {
        FakeTimeOps ops;
        ops.fail_read = true;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Failed);
        CHECK(ops.saves == 0 && ops.rtc_writes == 0);
        CHECK(svc.state(kProvisionNow).source == TimeSource::None);
    }
    // A refused save likewise stops before the chip, and the old blob stays.
    {
        FakeTimeOps ops;
        ops.has_blob = true;
        ops.blob = {-60, 42};
        ops.fail_save = true;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Failed);
        CHECK(ops.rtc_writes == 0 && ops.erases == 0);
        CHECK(ops.has_blob && ops.blob.offset_minutes == -60 &&
              ops.blob.last_sync_utc == 42);
    }
    // A save that landed and then said it failed -- the old version's erase
    // refused -- is put back too, or a reboot would restore an offset for a
    // synchronization the host was told did not happen.
    {
        FakeTimeOps ops;
        ops.has_blob = true;
        ops.blob = {-60, 42};
        ops.save_lands_then_fails = true;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Failed);
        CHECK(ops.rtc_writes == 0 && ops.saves == 2 && ops.erases == 0);
        CHECK(ops.has_blob && ops.blob.offset_minutes == -60 &&
              ops.blob.last_sync_utc == 42);
        CHECK(svc.state(kProvisionNow).source == TimeSource::None);
    }
    {
        FakeTimeOps ops;
        ops.save_lands_then_fails = true;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Failed);
        CHECK(ops.rtc_writes == 0 && ops.saves == 1 && ops.erases == 1);
        CHECK(!ops.has_blob);
    }
    // The chip refuses and the store had a blob: it comes back exactly.
    {
        FakeTimeOps ops;
        ops.has_blob = true;
        ops.blob = {-60, 42};
        ops.fail_rtc = true;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Failed);
        CHECK(ops.has_blob && ops.blob.offset_minutes == -60 &&
              ops.blob.last_sync_utc == 42);
        CHECK(ops.saves == 2 && ops.erases == 0);
        CHECK(svc.state(kProvisionNow).source == TimeSource::None);
    }
    // The chip refuses and the store had nothing: it is absent again, not a
    // zero. A watch that never synchronized must not boot holding an offset it
    // never accepted.
    {
        FakeTimeOps ops;
        ops.fail_rtc = true;
        TimeService svc;
        CHECK(provision_time(ops, provision_request(), svc, kProvisionNow) ==
              ProvisionTimeResult::Failed);
        CHECK(!ops.has_blob && ops.erases == 1 && ops.saves == 1);
        CHECK(svc.state(kProvisionNow).source == TimeSource::None);
    }
}

}  // namespace

int main()
{
    using namespace attadipa::core;

    std::uint8_t raw_rtc[7] = {0x56, 0x34, 0x12, 0x25, 0x02, 0x08, 0x26};
    attadipa::firmware::RtcDateTime rtc{};
    CHECK(attadipa::firmware::decode_pcf85063(raw_rtc, rtc) ==
          attadipa::firmware::RtcDecodeStatus::Valid);
    CHECK(rtc.year == 2026 && rtc.month == 8 && rtc.day == 25);
    CHECK(rtc.hour == 12 && rtc.minute == 34 && rtc.second == 56);
    CHECK(rtc.weekday == 2);
    std::uint8_t encoded_rtc[7]{};
    CHECK(attadipa::firmware::encode_pcf85063(rtc, encoded_rtc));
    for (unsigned i = 0; i < 7; ++i) {
        CHECK(encoded_rtc[i] == raw_rtc[i]);
    }
    rtc.year = 2100;
    CHECK(!attadipa::firmware::encode_pcf85063(rtc, encoded_rtc));
    rtc.year = 2026;
    raw_rtc[0] |= 0x80;
    CHECK(attadipa::firmware::decode_pcf85063(raw_rtc, rtc) ==
          attadipa::firmware::RtcDecodeStatus::VoltageLow);
    raw_rtc[0] = 0x56;
    raw_rtc[1] = 0x6A;
    CHECK(attadipa::firmware::decode_pcf85063(raw_rtc, rtc) ==
          attadipa::firmware::RtcDecodeStatus::InvalidData);
    raw_rtc[1] = 0x34;
    raw_rtc[3] = 0x30;
    raw_rtc[5] = 0x02;
    CHECK(attadipa::firmware::decode_pcf85063(raw_rtc, rtc) ==
          attadipa::firmware::RtcDecodeStatus::InvalidData);

    TimeService service;
    TimeState state = service.state({0});
    CHECK(state.availability == Availability::Unprovisioned);
    CHECK(state.utc.validity == Validity::Unknown);

    CHECK(service.observe(sample({1000}, {100}, TimeSource::Rtc,
                                 TimeQuality::Provisional)));
    state = service.state({2100});
    CHECK(state.availability == Availability::Ready);
    CHECK(state.source == TimeSource::Rtc);
    CHECK(state.quality == TimeQuality::Provisional);
    CHECK(state.utc.value == WallTime{1002});
    CHECK(state.utc.validity == Validity::Stale);
    CHECK(!state.has_last_sync);

    service.report(TimeSource::Rtc, Availability::Unprovisioned, Validity::Unknown);
    CHECK(service.state({2100}).availability == Availability::Unprovisioned);
    service.report(TimeSource::Rtc, Availability::Failed, Validity::Invalid);
    CHECK(service.state({2100}).availability == Availability::Failed);
    service.report(TimeSource::Rtc, Availability::Unreachable, Validity::Invalid);
    CHECK(service.state({2100}).availability == Availability::Unreachable);

    CHECK(service.set_timezone(120, {100000}, {2000}));
    CHECK(service.observe(sample({10000}, {3000}, TimeSource::Companion,
                                 TimeQuality::Trusted, {10000})));
    state = service.state({4000});
    CHECK(state.availability == Availability::Ready);
    CHECK(state.utc.value == WallTime{10001});
    CHECK(state.local.value == WallTime{17201});
    CHECK(state.utc.validity == Validity::Valid);
    CHECK(state.local.validity == Validity::Valid);
    CHECK(state.has_last_sync && state.last_sync == MonotonicTime{3000});
    CHECK(state.timezone_valid);

    CHECK(!service.observe(sample({10002}, {5000}, TimeSource::Manual,
                                  TimeQuality::Trusted, {10000})));
    CHECK(service.state({5000}).source == TimeSource::Companion);
    CHECK(service.state({14000}).utc.validity == Validity::Stale);
    CHECK(service.observe(sample({10020}, {14000}, TimeSource::Manual,
                                 TimeQuality::Trusted, {10000})));
    CHECK(service.state({14000}).source == TimeSource::Manual);
    CHECK(service.observe(sample({10020}, {25000}, TimeSource::Rtc,
                                 TimeQuality::Provisional)));
    state = service.state({25000});
    CHECK(state.source == TimeSource::Rtc);
    CHECK(state.quality == TimeQuality::Provisional);
    CHECK(state.utc.validity == Validity::Stale);

    TimeService corrections;
    CHECK(corrections.set_timezone(0, {std::numeric_limits<std::uint64_t>::max()}, {0}));
    CHECK(corrections.observe(sample({100000}, {1000}, TimeSource::Gnss,
                                     TimeQuality::Trusted, {60000})));
    const MonotonicTime deadline = MonotonicTime{1000} + Millis{5000};
    CHECK(!corrections.observe(sample({200000}, {2000}, TimeSource::Gnss,
                                      TimeQuality::Trusted, {60000})));
    TimeObservation confirmed = sample({200000}, {2000}, TimeSource::Gnss,
                                       TimeQuality::Trusted, {60000});
    confirmed.allow_large_correction = true;
    CHECK(corrections.observe(confirmed));
    CHECK(deadline == MonotonicTime{6000});
    CHECK(corrections.state({2000}).recently_corrected);

    TimeObservation backwards = sample({50000}, {3000}, TimeSource::Gnss,
                                       TimeQuality::Trusted, {60000});
    backwards.allow_large_correction = true;
    CHECK(corrections.observe(backwards));
    CHECK(deadline == MonotonicTime{6000});
    CHECK(corrections.state({3000}).utc.value == WallTime{50000});

    TimeService timezone;
    CHECK(timezone.observe(sample({5000}, {0}, TimeSource::Manual,
                                  TimeQuality::Trusted, {60000})));
    CHECK(timezone.set_timezone(-60, {5000}, {0}));
    state = timezone.state({1000});
    CHECK(state.utc.value == WallTime{5001});
    CHECK(state.local.value == WallTime{1401});
    CHECK(state.utc.validity == Validity::Valid);
    CHECK(state.local.validity == Validity::Valid);
    CHECK(state.timezone_valid);
    state = timezone.state({5000});
    CHECK(state.utc.validity == Validity::Valid);
    CHECK(state.local.validity == Validity::Stale);
    CHECK(!state.timezone_valid);
    CHECK(!timezone.set_timezone(841, {6000}, {5000}));

    TimeService restored;
    CHECK(restored.set_provisional_timezone(300));
    CHECK(restored.observe(sample({10000}, {0}, TimeSource::Rtc,
                                  TimeQuality::Provisional, {})));
    state = restored.state({1000});
    CHECK(state.local.value == WallTime{28001});
    CHECK(state.local.validity == Validity::Stale);
    CHECK(!state.timezone_valid);
    CHECK(!restored.set_provisional_timezone(-841));

    TimeService fallback;
    CHECK(fallback.observe(sample({10000}, {1000}, TimeSource::Manual,
                                  TimeQuality::Trusted, {60000})));
    fallback.report(TimeSource::Rtc, Availability::Unreachable,
                    Validity::Invalid);
    CHECK(fallback.state({2000}).availability == Availability::Ready);
    CHECK(fallback.state({2000}).source == TimeSource::Manual);
    fallback.report(TimeSource::Manual, Availability::Failed,
                    Validity::Invalid);
    CHECK(fallback.state({2000}).availability == Availability::Failed);
    CHECK(fallback.observe(sample({10001}, {2000}, TimeSource::Rtc,
                                  TimeQuality::Provisional, {})));
    CHECK(fallback.state({2000}).availability == Availability::Ready);
    CHECK(fallback.state({2000}).source == TimeSource::Rtc);

    TimeService guarded_fallback;
    CHECK(guarded_fallback.observe(sample({10000}, {1000}, TimeSource::Gnss,
                                          TimeQuality::Trusted, {60000})));
    guarded_fallback.report(TimeSource::Gnss, Availability::Failed,
                            Validity::Invalid);
    CHECK(!guarded_fallback.observe(sample({100000}, {2000},
                                           TimeSource::Manual,
                                           TimeQuality::Trusted, {60000})));
    TimeObservation allowed_fallback = sample(
        {100000}, {2000}, TimeSource::Manual, TimeQuality::Trusted, {60000});
    allowed_fallback.allow_large_correction = true;
    CHECK(guarded_fallback.observe(allowed_fallback));

    TimeService bounds;
    CHECK(bounds.observe(sample({std::numeric_limits<std::int64_t>::max() - 1},
                                {0}, TimeSource::Manual,
                                TimeQuality::Trusted, {10000})));
    CHECK(bounds.state({5000}).utc.value ==
          WallTime{std::numeric_limits<std::int64_t>::max()});
    TimeService lower_bound;
    CHECK(lower_bound.observe(sample(
        {std::numeric_limits<std::int64_t>::min() + 1}, {0},
        TimeSource::Manual, TimeQuality::Trusted, {10000})));
    CHECK(lower_bound.set_timezone(-60, {10000}, {0}));
    CHECK(lower_bound.state({0}).local.value ==
          WallTime{std::numeric_limits<std::int64_t>::min()});

    // #264, ledger of 2026-08-26. GNSS outranks Manual, so the priority guard
    // admits this candidate; it is Stale the moment it arrives, because
    // age_at_source_ms has already spent fresh_for. Fresh trusted time is not
    // given up for it. The correction is zero, so the large-correction guard
    // is not what does the rejecting here.
    TimeService stale_candidate;
    CHECK(stale_candidate.observe(sample({10000}, {1000}, TimeSource::Manual,
                                         TimeQuality::Trusted, Millis{5000})));
    CHECK(!stale_candidate.observe(sample({10001}, {2000}, TimeSource::Gnss,
                                          TimeQuality::Trusted, Millis{1000},
                                          1000)));
    CHECK(stale_candidate.state({2000}).source == TimeSource::Manual);
    CHECK(stale_candidate.state({2000}).utc.validity == Validity::Valid);
    // One millisecond younger is Valid, and priority decides as it always did.
    CHECK(stale_candidate.observe(sample({10001}, {2000}, TimeSource::Gnss,
                                         TimeQuality::Trusted, Millis{1000},
                                         999)));
    CHECK(stale_candidate.state({2000}).source == TimeSource::Gnss);

    // Nothing is held, so a stale sample is still better than no time at all.
    TimeService from_nothing;
    CHECK(from_nothing.observe(sample({10000}, {1000}, TimeSource::Gnss,
                                      TimeQuality::Trusted, Millis{1000},
                                      1000)));
    CHECK(from_nothing.state({1000}).utc.validity == Validity::Stale);

    // The guard protects time that is currently Valid and nothing else: a
    // stale candidate still replaces an observation that has gone stale.
    TimeService stale_current;
    CHECK(stale_current.observe(sample({10000}, {1000}, TimeSource::Manual,
                                       TimeQuality::Trusted, Millis{500})));
    CHECK(stale_current.state({2000}).utc.validity == Validity::Stale);
    CHECK(stale_current.observe(sample({10001}, {2000}, TimeSource::Gnss,
                                       TimeQuality::Trusted, Millis{1000},
                                       1000)));
    CHECK(stale_current.state({2000}).source == TimeSource::Gnss);

    test_provision_time();

    return failures == 0 ? 0 : 1;
}
