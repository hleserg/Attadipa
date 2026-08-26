#include <cstdio>
#include <limits>

#include "attadipa/core/time_service.h"
#include "pcf85063_time.h"

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
    attadipa::core::Millis fresh_for = {})
{
    return {utc, observed_at, fresh_for, 0, source, quality, false};
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

    return failures == 0 ? 0 : 1;
}
