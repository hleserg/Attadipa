// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace attadipa::firmware {

// AKM MS1526-E-01 (2014/7), sections 6.3, 6.4.3, 6.4.5 and 8.3.
// https://www.ecsimple.com/files/b7/ak09911c.pdf
// Sensor axes only: no mounting transform, iron calibration or true north.
enum class Ak09911Result {
  Ok,
  Sample,
  NotReady,
  Overflow,
  InvalidData,
  IoError,
  WrongIdentity,
  NotStarted
};

struct Ak09911Info {
  std::uint8_t id[2]{};
  // Raw register bytes: equal reads alone do not establish valid factory ASA.
  std::uint8_t asa[3]{};
  std::uint8_t fuse_mode_readback = 0;
};

struct Ak09911Sample {
  std::int16_t raw[3]{};
  // Raw sensor-axis counts only, even when fuse-mode readback is confirmed.
  // No adjusted field units until the calibration prerequisites are
  // established.
  std::uint8_t st1 = 0;
  std::uint8_t st2 = 0;
  // Host monotonic read-completion time, not the chip's conversion time.
  // Zero on an unsuccessful attempt; latest() changes only for Sample.
  std::int64_t received_at_us = 0;
};

// The same register sequence runs with native I2C and in the host check.
// Io supplies bounded read/write, wait_us and now_us; it owns the device
// handle. Call stop(), including after a failed start(), before releasing that
// handle.
template <typename Io> class Ak09911 {
public:
  explicit Ak09911(Io &io) : io_(io) {}
  Ak09911(const Ak09911 &) = delete;
  Ak09911 &operator=(const Ak09911 &) = delete;

  bool identified() const { return identified_; }
  const Ak09911Info &info() const { return info_; }
  const Ak09911Sample &latest() const { return latest_; }

  Ak09911Result start() {
    if (identified_)
      return Ak09911Result::InvalidData; // stop before restart
    info_ = {};
    latest_ = {};
    if (!io_.read(0x00, info_.id, sizeof(info_.id)))
      return Ak09911Result::IoError;
    if (info_.id[0] != 0x48 || info_.id[1] != 0x05)
      return Ak09911Result::WrongIdentity; // no configuration writes
    identified_ = true;
    auto result = power_down();
    if (result != Ak09911Result::Ok)
      return result;
    if (!io_.write(0x31, 0x1f))
      return Ak09911Result::IoError;
    io_.wait_us(2000);
    std::uint8_t again[3]{};
    if (!io_.read(0x31, &info_.fuse_mode_readback, 1) ||
        !io_.read(0x60, info_.asa, sizeof(info_.asa)) ||
        !io_.read(0x60, again, sizeof(again)))
      return Ak09911Result::IoError;
    // The received compatible unit reads back 00 after an acknowledged 1F.
    // Preserve the discrepancy; do not infer authenticity from ID or ASA.
    // Both 00 and 1F are recorded, rather than inventing substitute ASA values.
    result = power_down();
    if (result != Ak09911Result::Ok)
      return result;
    for (unsigned i = 0; i < 3; ++i)
      if (info_.asa[i] != again[i])
        return Ak09911Result::InvalidData;
    if (info_.fuse_mode_readback != 0x00 && info_.fuse_mode_readback != 0x1f)
      return Ak09911Result::InvalidData;
    std::uint8_t discard = 0;
    // Release any old protected sample before starting continuous 10 Hz.
    if (!io_.read(0x18, &discard, 1) || !io_.write(0x31, 0x02))
      return Ak09911Result::IoError;
    std::uint8_t mode = 0;
    if (!io_.read(0x31, &mode, 1))
      return Ak09911Result::IoError;
    if (mode != 0x02)
      return Ak09911Result::InvalidData;
    running_ = true;
    return Ak09911Result::Ok;
  }

  Ak09911Result read(Ak09911Sample &attempt) {
    attempt = {};
    if (!running_)
      return Ak09911Result::NotStarted;
    if (!io_.read(0x10, &attempt.st1, 1)) {
      running_ = false;
      return Ak09911Result::IoError;
    }
    if ((attempt.st1 & 0xfc) != 0)
      return Ak09911Result::InvalidData;
    if ((attempt.st1 & 1) == 0)
      return Ak09911Result::NotReady;
    // HXL..ST2 includes dummy TMPS. Reading ST2 releases data protection.
    std::uint8_t bytes[8]{};
    if (!io_.read(0x11, bytes, sizeof(bytes))) {
      std::uint8_t discard = 0;
      (void)io_.read(0x18, &discard,
                     1); // bounded partial-burst release attempt
      running_ = false;  // restart needed after transport failure
      return Ak09911Result::IoError;
    }
    attempt.st2 = bytes[7];
    bool in_range = true;
    for (unsigned i = 0; i < 3; ++i) {
      const unsigned bits = bytes[i * 2] | (unsigned(bytes[i * 2 + 1]) << 8);
      const int value = bits >= 0x8000 ? int(bits) - 65536 : int(bits);
      attempt.raw[i] = static_cast<std::int16_t>(value);
      in_range = in_range && value >= -8190 && value <= 8190;
    }
    if ((attempt.st2 & 0x08) != 0)
      return Ak09911Result::Overflow;
    // Reserved bits must be zero in Standard/Fast mode (not Hs-mode).
    if (!in_range || (attempt.st2 & 0xf7) != 0)
      return Ak09911Result::InvalidData;
    attempt.received_at_us = io_.now_us();
    latest_ = attempt;
    // DOR remains in st1: skipped updates, not an exact count or disturbance.
    return Ak09911Result::Sample;
  }

  Ak09911Result stop() {
    running_ = false;
    if (!identified_)
      return Ak09911Result::Ok;
    const auto result = power_down();
    if (result == Ak09911Result::Ok)
      identified_ = false;
    return result;
  }

private:
  Ak09911Result power_down() {
    if (!io_.write(0x31, 0x00))
      return Ak09911Result::IoError;
    io_.wait_us(2000); // >=100 us Twat before another mode, section 6.3
    std::uint8_t mode = 0xff;
    if (!io_.read(0x31, &mode, 1))
      return Ak09911Result::IoError;
    return mode == 0 ? Ak09911Result::Ok : Ak09911Result::InvalidData;
  }

  Io &io_;
  Ak09911Info info_{};
  Ak09911Sample latest_{};
  bool identified_ = false;
  bool running_ = false;
};

} // namespace attadipa::firmware
