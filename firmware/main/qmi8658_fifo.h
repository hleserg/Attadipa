// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace attadipa::firmware {

// QST 13-52-27 Rev A: Tables 22/23, sections 5.10, 8 and 11.
// https://github.com/hleserg/Attadipa/issues/450#issuecomment-5599504463
// FIFO works in Non-SyncSample; SyncSample would stop the pedometer.
enum class QmiResult {
  Ok, Samples, NotReady, Overflow, InvalidData, IoError, WrongIdentity,
  UnsupportedConfig, Busy, Timeout, NotStarted
};

struct QmiState {
  // CTRL1/2/3/5/7/8, FIFO watermark/control. Single-byte reads also work
  // before the caller has established address increment and byte order.
  std::uint8_t regs[8]{};
  std::uint32_t steps = 0;
  bool complete = false;
};

struct QmiBatch {
  std::int16_t accel[16][3]{};
  unsigned count = 0;
  std::uint8_t status = 0;
  std::int64_t requested_at_us = 0;
  std::int64_t frozen_at_us = 0;
  // Successful host release/readback time, NOT the sensing time of any item.
  std::int64_t received_at_us = 0;
};

template <typename Io> class Qmi8658Fifo {
public:
  explicit Qmi8658Fifo(Io &io) : io_(io) {}
  Qmi8658Fifo(const Qmi8658Fifo &) = delete;
  Qmi8658Fifo &operator=(const Qmi8658Fifo &) = delete;

  const QmiState &before() const { return before_; }
  const QmiState &after() const { return after_; }
  const QmiBatch &latest() const { return latest_; }
  bool temporary_accel() const { return temporary_accel_; }
  unsigned frame_bytes() const { return frame_bytes_; }
  unsigned discarded_words() const { return discarded_words_; }

  QmiResult start() {
    if (owned_)
      return QmiResult::Busy;
    before_ = {};
    after_ = {};
    latest_ = {};
    discarded_words_ = 0;
    std::uint8_t who = 0, rev = 0;
    if (!byte(0x00, who) || !byte(0x01, rev))
      return QmiResult::IoError;
    if (who != 0x05 || rev != 0x7c)
      return QmiResult::WrongIdentity; // supported map, not authenticity
    if (!snapshot(before_))
      return QmiResult::IoError;
    const auto *r = before_.regs;
    std::uint8_t cmd = 0, status = 0;
    unsigned words = 0;
    if (!byte(0x0a, cmd) || !byte(0x2d, status) || !fifo_words(words))
      return QmiResult::IoError;
    if (cmd != 0 || (status & 0x80) || (r[7] & 0x83) || words != 0)
      return QmiResult::Busy; // no acknowledgement/reset of another owner
    if ((r[0] & 1) || (r[4] & 0x80))
      return QmiResult::UnsupportedConfig;
    temporary_accel_ = (r[4] & 3) == 0 && (r[5] & 0x1f) == 0;
    if (!temporary_accel_ && !supported_active(r))
      return QmiResult::UnsupportedConfig;
    frame_bytes_ = !temporary_accel_ && (r[4] & 2) ? 12 : 6;

    owned_ = true; // even a failed write may have reached the device
    auto result = set(0x02, (r[0] | 0x40) & 0xdf); // AI=1, little endian
    if (result != QmiResult::Ok)
      return result;
    result = set(0x09, r[5] | 0x80); // preserve every motion-engine bit
    if (result != QmiResult::Ok)
      return result;
    if (temporary_accel_) {
      result = set(0x03, 0x26); // idle only: +/-8 g, accel-only 125 Hz
      if (result != QmiResult::Ok)
        return result;
      result = set(0x08, r[4] | 1);
      if (result != QmiResult::Ok)
        return result;
      io_.wait_us(30000); // startup + >=3/ODR settling, section 7.3
    }
    result = set(0x13, 1);
    if (result != QmiResult::Ok)
      return result;
    result = set(0x14, kFifoMode);
    if (result == QmiResult::Ok)
      running_ = true;
    return result;
  }

  QmiResult read(QmiBatch &out) {
    out = {};
    if (!running_)
      return QmiResult::NotStarted;
    if (!byte(0x16, out.status))
      return fail(QmiResult::IoError);
    if (out.status & 0x20)
      return fail(QmiResult::Overflow);
    if (!(out.status & 0x10))
      return QmiResult::NotReady;
    out.requested_at_us = io_.now_us();
    auto result = command(0x05);
    if (result != QmiResult::Ok)
      return fail(result);
    out.frozen_at_us = io_.now_us();
    std::uint8_t control = 0;
    unsigned words = 0;
    if (!byte(0x14, control) || !fifo_words(words, &out.status))
      return fail(QmiResult::IoError);
    if (!(control & 0x80) || words * 2 > 16 * frame_bytes_ ||
        (words * 2) % frame_bytes_ != 0)
      return fail(QmiResult::InvalidData);
    if (out.status & 0x20)
      return fail(QmiResult::Overflow);
    std::uint8_t bytes[16 * 12]{};
    if (words && !io_.read(0x17, bytes, words * 2))
      return fail(QmiResult::IoError); // consumed prefix/alignment is unknown
    result = set(0x14, kFifoMode);
    if (result != QmiResult::Ok)
      return fail(result);
    if (!words)
      return QmiResult::NotReady;
    out.count = words * 2 / frame_bytes_;
    for (unsigned n = 0; n < out.count; ++n) {
      for (unsigned axis = 0; axis < 3; ++axis) {
        const unsigned offset = n * frame_bytes_ + axis * 2;
        const unsigned bits = bytes[offset] | (unsigned(bytes[offset + 1]) << 8);
        out.accel[n][axis] = static_cast<std::int16_t>(
            bits >= 0x8000 ? int(bits) - 65536 : int(bits));
      }
    }
    out.received_at_us = io_.now_us();
    latest_ = out;
    return QmiResult::Samples;
  }

  // Always call after start, including a failed start. Restore settings, not
  // consumed samples. A failed command's completion must be resolved first.
  QmiResult stop() {
    running_ = false;
    if (!owned_)
      return QmiResult::Ok;
    QmiResult result = QmiResult::Ok;
    auto keep = [&result](QmiResult next) {
      if (result == QmiResult::Ok && next != QmiResult::Ok)
        result = next;
    };
    if (command_pending_)
      keep(finish_command());
    keep(set(0x14, 0)); // stop FIFO filling; sensors/step engine keep running
    if (!fifo_words(discarded_words_))
      keep(QmiResult::IoError);
    if (!command_pending_) {
      // Only this owner's unused-on-entry FIFO is reset, never step count.
      keep(command(0x04)); // section 8.10, distinct from pedometer reset 0x0f
      unsigned remaining = 0;
      if (!fifo_words(remaining))
        keep(QmiResult::IoError);
      else if (remaining != 0)
        keep(QmiResult::InvalidData);
    }
    keep(set(0x13, before_.regs[6]));
    keep(set(0x14, before_.regs[7]));
    if (temporary_accel_) {
      keep(set(0x08, before_.regs[4]));
      io_.wait_us(16000); // up to 2/ODR to stop the owned 125 Hz accelerometer
      keep(set(0x03, before_.regs[1]));
    }
    keep(set(0x09, before_.regs[5]));
    keep(set(0x02, before_.regs[0]));
    if (!snapshot(after_))
      keep(QmiResult::IoError);
    else {
      for (unsigned i = 0; i < 8; ++i)
        if (after_.regs[i] != before_.regs[i])
          keep(QmiResult::InvalidData);
    }
    if (result == QmiResult::Ok)
      owned_ = false;
    return result;
  }

private:
  static constexpr std::uint8_t kFifoMode = 1; // 16 samples, stop on full
  static bool supported_active(const std::uint8_t *r) {
    const unsigned odr = r[1] & 15;
    if (!(r[4] & 1) || (r[1] & 0x80) || ((r[1] >> 4) & 7) > 3)
      return false;
    if (r[4] & 2)
      return !(r[4] & 0x10) && !(r[2] & 0x80) && odr <= 8 &&
             odr == (r[2] & 15); // section 8.2, equal 6DOF ODR
    return (odr >= 3 && odr <= 8) || odr >= 12;
  }
  bool byte(std::uint8_t reg, std::uint8_t &value) {
    return io_.read(reg, &value, 1);
  }
  bool snapshot(QmiState &state) {
    state = {};
    constexpr std::uint8_t addresses[] = {2, 3, 4, 6, 8, 9, 0x13, 0x14};
    for (unsigned i = 0; i < 8; ++i)
      if (!byte(addresses[i], state.regs[i]))
        return false;
    // Bounded matching snapshots, rather than treating a torn/error read as 0.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      std::uint32_t counts[2]{};
      for (unsigned n = 0; n < 2; ++n) {
        for (unsigned i = 0; i < 3; ++i) {
          std::uint8_t value = 0;
          if (!byte(static_cast<std::uint8_t>(0x5a + i), value))
            return false;
          counts[n] |= std::uint32_t(value) << (8 * i);
        }
      }
      if (counts[0] == counts[1]) {
        state.steps = counts[0];
        state.complete = true;
        return true;
      }
    }
    return false;
  }
  bool fifo_words(unsigned &words, std::uint8_t *status = nullptr) {
    std::uint8_t lo = 0, hi = 0;
    if (!byte(0x15, lo) || !byte(0x16, hi))
      return false;
    words = lo | (unsigned(hi & 3) << 8);
    if (status)
      *status = hi;
    return true;
  }
  QmiResult set(std::uint8_t reg, std::uint8_t value) {
    std::uint8_t actual = 0;
    if (!io_.write(reg, value) || !byte(reg, actual))
      return QmiResult::IoError;
    return actual == value ? QmiResult::Ok : QmiResult::InvalidData;
  }
  QmiResult wait_done(bool expected) {
    const auto began = io_.now_us();
    do {
      std::uint8_t status = 0;
      if (!byte(0x2d, status))
        return QmiResult::IoError;
      if (bool(status & 0x80) == expected)
        return QmiResult::Ok;
      io_.wait_us(100);
    } while (io_.now_us() - began < 20000); // bounded software policy
    return QmiResult::Timeout;
  }
  QmiResult finish_command() {
    auto result = wait_done(true);
    if (result != QmiResult::Ok)
      return result;
    if (!io_.write(0x0a, 0))
      return QmiResult::IoError;
    result = wait_done(false);
    if (result == QmiResult::Ok)
      command_pending_ = false;
    return result;
  }
  QmiResult command(std::uint8_t cmd) {
    command_pending_ = true;
    if (!io_.write(0x0a, cmd))
      return QmiResult::IoError;
    return finish_command();
  }
  QmiResult fail(QmiResult result) {
    running_ = false;
    return result;
  }
  Io &io_;
  QmiState before_{}, after_{};
  QmiBatch latest_{};
  unsigned frame_bytes_ = 6, discarded_words_ = 0;
  bool owned_ = false, running_ = false, temporary_accel_ = false;
  bool command_pending_ = false;
};

} // namespace attadipa::firmware
