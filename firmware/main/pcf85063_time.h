#pragma once

#include <cstdint>

namespace attadipa::firmware {

struct RtcDateTime {
  unsigned year = 2000;
  unsigned month = 1;
  unsigned day = 1;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
};

enum class RtcDecodeStatus : std::uint8_t { Valid, VoltageLow, InvalidData };

constexpr bool valid_bcd(std::uint8_t value) {
  return (value & 0x0F) <= 9 && (value >> 4) <= 9;
}

constexpr std::uint8_t from_bcd(std::uint8_t value) {
  return static_cast<std::uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

constexpr unsigned days_in_month(unsigned year, unsigned month) {
  constexpr std::uint8_t kDays[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  if (month == 0 || month > 12) {
    return 0;
  }
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return month == 2 && leap ? 29 : kDays[month - 1];
}

constexpr RtcDecodeStatus decode_pcf85063(const std::uint8_t (&raw)[7],
                                           RtcDateTime &time) {
  if ((raw[0] & 0x80) != 0) {
    return RtcDecodeStatus::VoltageLow;
  }
  const std::uint8_t values[] = {static_cast<std::uint8_t>(raw[0] & 0x7F),
                                 static_cast<std::uint8_t>(raw[1] & 0x7F),
                                 static_cast<std::uint8_t>(raw[2] & 0x3F),
                                 static_cast<std::uint8_t>(raw[3] & 0x3F),
                                 static_cast<std::uint8_t>(raw[5] & 0x1F),
                                 raw[6]};
  for (const std::uint8_t value : values) {
    if (!valid_bcd(value)) {
      return RtcDecodeStatus::InvalidData;
    }
  }

  time = {2000U + from_bcd(values[5]), from_bcd(values[4]),
          from_bcd(values[3]),         from_bcd(values[2]),
          from_bcd(values[1]),         from_bcd(values[0])};
  if (time.second > 59 || time.minute > 59 || time.hour > 23 ||
      time.day == 0 || time.day > days_in_month(time.year, time.month) ||
      (raw[4] & 0x07) > 6) {
    return RtcDecodeStatus::InvalidData;
  }
  return RtcDecodeStatus::Valid;
}

} // namespace attadipa::firmware
