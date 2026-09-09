#pragma once

#include <cstdint>

#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"

namespace attadipa::core {

enum class TimeSource : std::uint8_t {
    None,
    Rtc,
    Manual,
    Companion,
    Network,
    Gnss,
    Mesh,
    Simulated,
};

enum class TimeQuality : std::uint8_t { Unknown, Provisional, Trusted };

struct TimeObservation {
    WallTime      utc{};
    MonotonicTime observed_at{};
    Millis        fresh_for{};
    std::uint32_t age_at_source_ms      = 0;
    TimeSource    source                = TimeSource::None;
    TimeQuality   quality               = TimeQuality::Unknown;
    bool          allow_large_correction = false;
};

struct TimePolicy {
    std::uint32_t max_silent_correction_seconds = 300;
    Millis        recently_corrected_for{300000};
};

struct TimeZoneOffset {
    std::int16_t  minutes_east_of_utc = 0;
    MonotonicTime valid_until{};
    bool          configured = false;
};

struct TimeState {
    Timed<WallTime> utc{};
    Timed<WallTime> local{};
    Availability    availability = Availability::Unprovisioned;
    TimeSource      source       = TimeSource::None;
    TimeQuality     quality      = TimeQuality::Unknown;
    MonotonicTime   last_sync{};
    bool            has_last_sync      = false;
    bool            timezone_valid     = false;
    // The offset `local` was made with, as the service holds it. Here because
    // the only other way to get it back is `local - utc` through
    // `.unix_seconds`, which is the shape `clock.h` removes `operator-` to
    // stop; a caller that needs the number should be given it, not left to
    // subtract two `WallTime`s and hope one is the other plus an offset.
    // Meaningless unless `timezone_valid`, exactly like `local`.
    std::int16_t    timezone_offset_minutes = 0;
    bool            recently_corrected = false;
};

class TimeService {
public:
    explicit TimeService(TimePolicy policy = {});

    bool observe(const TimeObservation& observation);
    bool set_timezone(std::int16_t minutes_east_of_utc,
                      MonotonicTime valid_until, MonotonicTime now);
    bool set_provisional_timezone(std::int16_t minutes_east_of_utc);
    void report(TimeSource source, Availability availability, Validity validity);
    TimeState state(MonotonicTime now) const;

private:
    TimePolicy      policy_{};
    TimeObservation observation_{};
    TimeZoneOffset  timezone_{};
    MonotonicTime   last_sync_{};
    MonotonicTime   corrected_at_{};
    TimeSource      reported_source_       = TimeSource::None;
    Availability    reported_availability_ = Availability::Unprovisioned;
    Validity        reported_validity_     = Validity::Unknown;
    bool            has_observation_       = false;
    bool            has_last_sync_         = false;
    bool            has_correction_        = false;
    bool            has_report_            = false;
};

const char* to_string(TimeSource source);
const char* to_string(TimeQuality quality);

}  // namespace attadipa::core
