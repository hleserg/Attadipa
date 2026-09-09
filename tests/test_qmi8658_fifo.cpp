// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "qmi8658_fifo.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace attadipa::firmware;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (false)

// Transport/register model only: the same owner is called by the native probe.
struct Bus {
  std::array<std::uint8_t, 256> regs{};
  std::vector<std::array<std::uint8_t, 2>> writes;
  std::vector<std::uint8_t> fifo;
  unsigned cursor = 0;
  int operation = 0, fail_operation = -1;
  std::int64_t now = 1000000;
  bool apply_failed_write = false, never_done = false, fail_payload = false;
  bool fail_release = false;
  bool retain_on_reset = false, mismatch_watermark_restore = false;
  bool pending_watermark_mismatch = false;
  std::uint8_t extra_status = 0;
  explicit Bus(bool gyro = false, bool idle = false) {
    regs[0] = 5; regs[1] = 0x7c; regs[2] = 0x60;
    regs[3] = idle ? 0 : 0x26; regs[4] = 0x36;
    regs[8] = idle ? 0 : gyro ? 3 : 1;
    regs[9] = idle ? 0 : 0x90;
    regs[0x5a] = 0x39; regs[0x5b] = 0x30;
  }
  bool read(std::uint8_t reg, std::uint8_t *out, std::size_t n) {
    now += 100;
    if (++operation == fail_operation)
      return false;
    const unsigned words = static_cast<unsigned>((fifo.size() - cursor) / 2);
    if (reg == 0x15) { CHECK(n == 1); *out = words & 0xff; return true; }
    if (reg == 0x16) {
      CHECK(n == 1);
      *out = static_cast<std::uint8_t>((words >> 8) | (words ? 0x10 : 0) | extra_status);
      return true;
    }
    if (reg == 0x17) {
      CHECK((regs[0x14] & 0x80) != 0);
      CHECK((regs[2] & 0x60) == 0x40); // explicit byte order / burst setup
      CHECK(cursor + n <= fifo.size());
      if (fail_payload) { ++cursor; return false; }
      std::memcpy(out, fifo.data() + cursor, n);
      cursor += static_cast<unsigned>(n);
      return true;
    }
    CHECK(n == 1);
    if (reg == 0x13 && pending_watermark_mismatch) {
      pending_watermark_mismatch = false;
      *out = 1;
      return true;
    }
    *out = regs[reg];
    return true;
  }
  bool write(std::uint8_t reg, std::uint8_t value) {
    now += 100;
    if (fail_release && reg == 0x14 && value == 1 && (regs[0x14] & 0x80))
      return false;
    const bool failed = ++operation == fail_operation;
    if (failed && !apply_failed_write)
      return false;
    CHECK(reg == 2 || reg == 3 || reg == 8 || reg == 9 || reg == 0x0a ||
          reg == 0x13 || reg == 0x14);
    if (reg == 9)
      CHECK((value & 0x1f) == (regs[9] & 0x1f)); // no engine toggles
    writes.push_back({reg, value});
    regs[reg] = value;
    if (reg == 0x13 && value == 0 && mismatch_watermark_restore)
      pending_watermark_mismatch = true;
    if (reg == 0x0a) {
      CHECK(value == 0 || value == 4 || value == 5); // never step/chip reset
      if (value == 0) regs[0x2d] &= 0x7f;
      else if (!never_done) {
        regs[0x2d] |= 0x80;
        if (value == 5) regs[0x14] |= 0x80;
        if (value == 4 && !retain_on_reset) { fifo.clear(); cursor = 0; extra_status = 0; }
      }
    }
    return !failed;
  }
  void wait_us(unsigned us) { now += us; }
  std::int64_t now_us() { return now; }
  void frame(int x, int y, int z, bool gyro = false) {
    const int values[] = {x, y, z, 777, 888, 999};
    for (unsigned i = 0; i < (gyro ? 6U : 3U); ++i) {
      const unsigned bits = static_cast<unsigned>(values[i]) & 0xffff;
      fifo.push_back(bits & 0xff);
      fifo.push_back(static_cast<std::uint8_t>(bits >> 8));
    }
  }
};
}

int main() {
  for (unsigned kind = 0; kind < 3; ++kind) {
    const bool gyro = kind == 1;
    Bus bus(gyro, kind == 2);
    const auto original = bus.regs;
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start() == QmiResult::Ok);
    CHECK(sensor.before().complete && sensor.before().steps == 12345);
    CHECK(sensor.temporary_accel() == (kind == 2));
    CHECK(bus.regs[8] == (gyro ? 3 : 1));
    QmiBatch batch;
    CHECK(sensor.read(batch) == QmiResult::NotReady);
    bus.frame(-32768, 32767, -1, gyro);
    bus.frame(123, -456, 789, gyro);
    CHECK(sensor.read(batch) == QmiResult::Samples);
    CHECK(batch.count == 2 && batch.accel[0][0] == -32768 &&
          batch.accel[0][1] == 32767 && batch.accel[0][2] == -1 &&
          batch.accel[1][0] == 123 && batch.accel[1][1] == -456 &&
          batch.accel[1][2] == 789);
    CHECK(batch.requested_at_us < batch.frozen_at_us &&
          batch.frozen_at_us < batch.received_at_us);
    CHECK((bus.regs[0x14] & 0x80) == 0);
    const auto last = sensor.latest().received_at_us;
    CHECK(sensor.read(batch) == QmiResult::NotReady);
    CHECK(batch.received_at_us == 0 && sensor.latest().received_at_us == last);
    bus.frame(10, 20, 30, gyro); // late owned data is explicitly discarded
    CHECK(sensor.stop() == QmiResult::Ok);
    CHECK(std::strcmp(sensor.stop_diagnostic().failed_step, "none") == 0);
    CHECK(sensor.stop_diagnostic().remaining_valid &&
          sensor.stop_diagnostic().remaining_words == 0 &&
          !sensor.stop_diagnostic().mismatch_valid);
    CHECK(sensor.discarded_words() == (gyro ? 6U : 3U));
    CHECK(sensor.after().complete && sensor.after().steps == 12345);
    CHECK(bus.regs == original && bus.fifo.empty());
    CHECK(sensor.read(batch) == QmiResult::NotStarted);
    if (kind != 2)
      for (auto w : bus.writes) CHECK(w[0] != 3 && w[0] != 8);
  }
  // Two distinct causes of InvalidData can leave the same final control
  // snapshot. Diagnose them without changing the sensor sequence or verdict.
  for (bool retained : {false, true}) {
    Bus bus;
    const auto original = bus.regs;
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start() == QmiResult::Ok);
    bus.frame(1, 2, 3);
    bus.retain_on_reset = retained;
    bus.mismatch_watermark_restore = true; // later failure must not hide first
    CHECK(sensor.stop() == QmiResult::InvalidData);
    const auto &diag = sensor.stop_diagnostic();
    CHECK(std::strcmp(diag.failed_step,
                      retained ? "count_after_reset" : "restore_watermark") == 0);
    CHECK(diag.remaining_valid && diag.remaining_words == (retained ? 3U : 0U));
    CHECK(diag.remaining_status == (retained ? 0x10 : 0));
    CHECK(diag.mismatch_valid && diag.mismatch_reg == 0x13 &&
          diag.expected == 0 && diag.actual == 1);
    CHECK(sensor.after().complete && bus.regs == original);
  }
  for (unsigned kind = 0; kind < 5; ++kind) {
    Bus bus;
    if (kind == 0) bus.regs[0] = 0xff;
    if (kind == 1) bus.regs[1] = 0;
    if (kind == 2) bus.regs[8] |= 0x80;
    if (kind == 3) bus.regs[0x14] = 2;
    if (kind == 4) bus.regs[0x0a] = 5;
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start() != QmiResult::Ok);
    CHECK(sensor.stop() == QmiResult::Ok && bus.writes.empty());
  }
  for (bool idle : {false, true}) {
    Bus reference(false, idle);
    Qmi8658Fifo normal(reference);
    CHECK(normal.start() == QmiResult::Ok);
    const int operations = reference.operation;
    CHECK(normal.stop() == QmiResult::Ok);
    for (bool applied : {false, true}) {
      for (int fail = 1; fail <= operations; ++fail) {
        Bus bus(false, idle);
        const auto original = bus.regs;
        bus.fail_operation = fail; bus.apply_failed_write = applied;
        Qmi8658Fifo sensor(bus);
        CHECK(sensor.start() != QmiResult::Ok);
        CHECK(sensor.stop() == QmiResult::Ok);
        CHECK(bus.regs == original);
      }
    }
  }
  for (unsigned kind = 0; kind < 6; ++kind) {
    Bus bus;
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start() == QmiResult::Ok);
    QmiBatch batch;
    bus.frame(1, 2, 3);
    CHECK(sensor.read(batch) == QmiResult::Samples);
    const auto last = sensor.latest().received_at_us;
    bus.frame(4, 5, 6);
    if (kind == 0) bus.fail_payload = true;
    if (kind == 1) bus.extra_status = 0x20;
    if (kind == 2) { bus.fifo.push_back(0); bus.fifo.push_back(0); }
    if (kind == 3) bus.never_done = true;
    if (kind == 4) bus.fail_release = true;
    if (kind == 5) for (unsigned i = 0; i < 16; ++i) bus.frame(7, 8, 9);
    const auto read = sensor.read(batch);
    CHECK(read != QmiResult::Samples);
    if (kind == 3) CHECK(read == QmiResult::Timeout);
    CHECK(batch.received_at_us == 0 && sensor.latest().received_at_us == last);
    CHECK(sensor.read(batch) == QmiResult::NotStarted);
    const auto stopped = sensor.stop();
    CHECK(stopped == (kind == 3 ? QmiResult::Timeout : QmiResult::Ok));
    CHECK((bus.regs[0x14] & 0x80) == 0);
    CHECK(bus.regs[0x5a] == 0x39 && bus.regs[0x5b] == 0x30);
  }
  std::printf("QMI FIFO production-sequence checks: %d failure(s); hardware not tested here\n", failures);
  return failures ? 1 : 0;
}
