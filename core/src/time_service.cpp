#include "attadipa/core/time_service.h"

#include <limits>

namespace attadipa::core {
namespace {

int priority(TimeSource source)
{
    switch (source) {
        case TimeSource::Gnss:      return 60;
        case TimeSource::Network:   return 50;
        case TimeSource::Companion: return 40;
        case TimeSource::Mesh:      return 30;
        case TimeSource::Manual:    return 20;
        case TimeSource::Rtc:       return 10;
        case TimeSource::Simulated: return 0;
        case TimeSource::None:      return -1;
    }
    return -1;
}

std::uint64_t age_ms(const TimeObservation& observation, MonotonicTime now)
{
    if (now < observation.observed_at) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t transit = now.ms - observation.observed_at.ms;
    return transit > std::numeric_limits<std::uint64_t>::max() - observation.age_at_source_ms
               ? std::numeric_limits<std::uint64_t>::max()
               : transit + observation.age_at_source_ms;
}

std::uint32_t age_field(std::uint64_t age)
{
    return age > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(age);
}

WallTime add_seconds(WallTime time, std::uint64_t seconds)
{
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (seconds > static_cast<std::uint64_t>(maximum) ||
        time.unix_seconds > maximum - static_cast<std::int64_t>(seconds)) {
        return WallTime{maximum};
    }
    return WallTime{time.unix_seconds + static_cast<std::int64_t>(seconds)};
}

WallTime add_offset(WallTime utc, std::int16_t minutes)
{
    const std::int64_t seconds = static_cast<std::int64_t>(minutes) * 60;
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (seconds > 0 && utc.unix_seconds > maximum - seconds) {
        return WallTime{maximum};
    }
    if (seconds < 0 && utc.unix_seconds < minimum - seconds) {
        return WallTime{minimum};
    }
    return WallTime{utc.unix_seconds + seconds};
}

WallTime projected(const TimeObservation& observation, MonotonicTime now)
{
    return add_seconds(observation.utc, age_ms(observation, now) / 1000);
}

Validity validity(const TimeObservation& observation, MonotonicTime now)
{
    if (now < observation.observed_at) {
        return Validity::Invalid;
    }
    if (observation.quality != TimeQuality::Trusted) {
        return Validity::Stale;
    }
    return observation.fresh_for.value != 0 &&
                   age_ms(observation, now) < observation.fresh_for.value
               ? Validity::Valid
               : Validity::Stale;
}

}  // namespace

TimeService::TimeService(TimePolicy policy) : policy_(policy) {}

bool TimeService::observe(const TimeObservation& candidate)
{
    if (candidate.source == TimeSource::None || candidate.quality == TimeQuality::Unknown ||
        (candidate.quality == TimeQuality::Trusted && candidate.fresh_for.value == 0)) {
        return false;
    }
    if (has_observation_ && candidate.observed_at < observation_.observed_at) {
        return false;
    }

    bool corrected = false;
    if (has_observation_) {
        const Validity current_validity = validity(observation_, candidate.observed_at);
        if (current_validity == Validity::Valid) {
            if (candidate.quality < observation_.quality ||
                (candidate.quality == observation_.quality &&
                 priority(candidate.source) < priority(observation_.source))) {
                return false;
            }
        }

        const WallTime expected = projected(observation_, candidate.observed_at);
        const std::uint64_t correction = seconds_between(expected, candidate.utc);
        if (observation_.quality == TimeQuality::Trusted &&
            candidate.quality == TimeQuality::Trusted &&
            current_validity == Validity::Valid &&
            correction > policy_.max_silent_correction_seconds &&
            !candidate.allow_large_correction) {
            return false;
        }
        corrected = correction != 0;
    }

    observation_ = candidate;
    has_observation_ = true;
    has_report_ = false;
    if (candidate.quality == TimeQuality::Trusted) {
        last_sync_ = candidate.observed_at;
        has_last_sync_ = true;
    }
    if (corrected) {
        corrected_at_ = candidate.observed_at;
        has_correction_ = true;
    }
    return true;
}

bool TimeService::set_timezone(std::int16_t minutes_east_of_utc,
                               MonotonicTime valid_until, MonotonicTime now)
{
    constexpr std::int16_t kMaximumOffsetMinutes = 14 * 60;
    if (minutes_east_of_utc < -kMaximumOffsetMinutes ||
        minutes_east_of_utc > kMaximumOffsetMinutes || valid_until <= now) {
        return false;
    }
    timezone_ = {minutes_east_of_utc, valid_until, true};
    return true;
}

bool TimeService::set_provisional_timezone(std::int16_t minutes_east_of_utc)
{
    constexpr std::int16_t kMaximumOffsetMinutes = 14 * 60;
    if (minutes_east_of_utc < -kMaximumOffsetMinutes ||
        minutes_east_of_utc > kMaximumOffsetMinutes) {
        return false;
    }
    timezone_ = {minutes_east_of_utc, {}, true};
    return true;
}

void TimeService::report(TimeSource source, Availability availability,
                         Validity validity_value)
{
    if (availability == Availability::Ready) {
        return;
    }
    reported_source_ = source;
    reported_availability_ = availability;
    reported_validity_ = validity_value;
    has_report_ = true;
}

TimeState TimeService::state(MonotonicTime now) const
{
    TimeState result;
    result.source = has_observation_ ? observation_.source : reported_source_;
    result.quality = has_observation_ ? observation_.quality : TimeQuality::Unknown;
    result.availability = has_report_ ? reported_availability_
                                      : (has_observation_ ? Availability::Ready
                                                          : Availability::Unprovisioned);
    result.has_last_sync = has_last_sync_;
    result.last_sync = last_sync_;
    result.timezone_valid = timezone_.configured && now < timezone_.valid_until;

    if (!has_observation_) {
        result.utc.validity = has_report_ ? reported_validity_ : Validity::Unknown;
        result.local.validity = result.utc.validity;
        return result;
    }

    result.utc = {projected(observation_, now),
                  observation_.age_at_source_ms,
                  age_field(now < observation_.observed_at
                                ? std::numeric_limits<std::uint64_t>::max()
                                : now.ms - observation_.observed_at.ms),
                  validity(observation_, now)};
    result.local = result.utc;
    result.local.value = add_offset(result.utc.value, timezone_.minutes_east_of_utc);
    if (result.local.validity == Validity::Valid && !result.timezone_valid) {
        result.local.validity = Validity::Stale;
    }
    if (has_report_) {
        result.utc.validity = reported_validity_;
        result.local.validity = reported_validity_;
    }
    result.recently_corrected = has_correction_ && now >= corrected_at_ &&
                                elapsed(corrected_at_, now) < policy_.recently_corrected_for;
    return result;
}

const char* to_string(TimeSource source)
{
    switch (source) {
        case TimeSource::None:      return "None";
        case TimeSource::Rtc:       return "Rtc";
        case TimeSource::Manual:    return "Manual";
        case TimeSource::Companion: return "Companion";
        case TimeSource::Network:   return "Network";
        case TimeSource::Gnss:      return "Gnss";
        case TimeSource::Mesh:      return "Mesh";
        case TimeSource::Simulated: return "Simulated";
    }
    return "?";
}

const char* to_string(TimeQuality quality)
{
    switch (quality) {
        case TimeQuality::Unknown:     return "Unknown";
        case TimeQuality::Provisional: return "Provisional";
        case TimeQuality::Trusted:     return "Trusted";
    }
    return "?";
}

}  // namespace attadipa::core
