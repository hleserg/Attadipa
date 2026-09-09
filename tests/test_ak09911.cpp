// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ak09911.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace attadipa::firmware;
namespace {
int failures = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "%d: %s\n", __LINE__, #x);                          \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

// Transport fake only. Initialization, decoding, status and stop logic are the
// shipping template; no copied driver implementation is tested here.
struct Bus {
  std::array<std::uint8_t, 256> regs{};
  std::vector<std::array<std::uint8_t, 2>> writes;
  int operation = 0, fail_operation = -1, release_reads = 0, asa_reads = 0;
  int mode_readback = -1;
  bool fail_burst = false, unstable_asa = false, fuse_reads_zero = false;
  std::int64_t now = 1000000, last_power_down = -1000000;
  Bus() {
    regs[0] = 0x48;
    regs[1] = 5;
    regs[0x60] = 23;
    regs[0x61] = 23;
    regs[0x62] = 19;
  }
  bool read(std::uint8_t reg, std::uint8_t *out, std::size_t n) {
    ++operation;
    if (reg == 0x18)
      ++release_reads;
    if (operation == fail_operation || (reg == 0x11 && fail_burst))
      return false;
    CHECK(unsigned(reg) + n <= regs.size());
    std::memcpy(out, &regs[reg], n);
    if (reg == 0x31 && mode_readback >= 0)
      out[0] = static_cast<std::uint8_t>(mode_readback);
    if (reg == 0x31 && regs[reg] == 0x1f && fuse_reads_zero)
      out[0] = 0;
    if (reg == 0x60 && unstable_asa && ++asa_reads == 2)
      out[0] ^= 1;
    if (reg == 0x11 && n == 8)
      regs[0x10] = 0; // consumed through ST2
    return true;
  }
  bool write(std::uint8_t reg, std::uint8_t value) {
    ++operation;
    if (operation == fail_operation)
      return false;
    CHECK(reg == 0x31); // no ID, PMU or arbitrary control writes
    if (value == 0)
      last_power_down = now;
    else
      CHECK(now - last_power_down >= 100);
    regs[reg] = value;
    writes.push_back({reg, value});
    return true;
  }
  void wait_us(unsigned us) { now += us; }
  std::int64_t now_us() { return now; }
  void frame(int x, int y, int z, std::uint8_t st1 = 1, std::uint8_t st2 = 0) {
    const int values[] = {x, y, z};
    for (unsigned i = 0; i < 3; ++i) {
      const unsigned bits = static_cast<unsigned>(values[i]) & 0xffff;
      regs[0x11 + i * 2] = bits & 0xff;
      regs[0x12 + i * 2] = static_cast<std::uint8_t>(bits >> 8);
    }
    regs[0x10] = st1;
    regs[0x18] = st2;
    now += 100000;
  }
};
} // namespace
int main() {
  for (unsigned bad = 0; bad < 2; ++bad) {
    Bus bus;
    bus.regs[bad] = 0xff;
    Ak09911 sensor(bus);
    CHECK(sensor.start() == Ak09911Result::WrongIdentity);
    CHECK(sensor.stop() == Ak09911Result::Ok);
    CHECK(bus.writes.empty());
  }
  Bus bus;
  bus.fuse_reads_zero = true; // observed compatible-unit discrepancy
  Ak09911 sensor(bus);
  CHECK(sensor.start() == Ak09911Result::Ok);
  CHECK(sensor.info().fuse_mode_readback == 0 && sensor.info().asa[2] == 19);
  Ak09911Sample attempt;
  bus.frame(-123, 456, -789, 3);
  CHECK(sensor.read(attempt) == Ak09911Result::Sample);
  CHECK(attempt.raw[0] == -123 && attempt.raw[1] == 456 &&
        attempt.raw[2] == -789);

  CHECK(attempt.st1 == 3 && attempt.st2 == 0);
  const auto accepted_at = sensor.latest().received_at_us;
  CHECK(accepted_at == bus.now);
  CHECK(sensor.read(attempt) == Ak09911Result::NotReady);
  CHECK(attempt.received_at_us == 0 &&
        sensor.latest().received_at_us == accepted_at);
  bus.frame(1, 2, 3, 1, 8);
  CHECK(sensor.read(attempt) == Ak09911Result::Overflow);
  CHECK(attempt.st2 == 8 && attempt.received_at_us == 0);
  CHECK(sensor.latest().received_at_us == accepted_at);
  bus.frame(8191, 0, 0);
  CHECK(sensor.read(attempt) == Ak09911Result::InvalidData);
  bus.frame(-32768, 0, 0);
  CHECK(sensor.read(attempt) == Ak09911Result::InvalidData);
  bus.frame(1, 2, 3, 5);
  CHECK(sensor.read(attempt) == Ak09911Result::InvalidData);
  bus.frame(-8190, 8190, -1);
  CHECK(sensor.read(attempt) == Ak09911Result::Sample);
  const auto before_failure = sensor.latest().received_at_us;
  bus.frame(1, 2, 3);
  bus.fail_burst = true;
  const int releases = bus.release_reads;
  CHECK(sensor.read(attempt) == Ak09911Result::IoError);
  CHECK(bus.release_reads == releases + 1);
  CHECK(sensor.latest().received_at_us == before_failure);
  CHECK(sensor.read(attempt) == Ak09911Result::NotStarted);
  CHECK(sensor.stop() == Ak09911Result::Ok && bus.regs[0x31] == 0);
  CHECK(sensor.start() == Ak09911Result::Ok); // explicit recovery
  CHECK(sensor.stop() == Ak09911Result::Ok);

  Bus unstable;
  unstable.unstable_asa = true;
  Ak09911 rejected(unstable);
  CHECK(rejected.start() == Ak09911Result::InvalidData);
  CHECK(rejected.stop() == Ak09911Result::Ok && unstable.regs[0x31] == 0);
  for (const auto &write : unstable.writes)
    CHECK(write[1] != 0x02);

  Bus reference;
  Ak09911 normal(reference);
  CHECK(normal.start() == Ak09911Result::Ok);
  const int start_operations = reference.operation;
  CHECK(normal.info().fuse_mode_readback == 0x1f);
  // Confirmed and discrepant fuse modes expose the same raw-only sample API.
  reference.frame(-123, 456, -789);
  CHECK(normal.read(attempt) == Ak09911Result::Sample);
  CHECK(attempt.raw[0] == -123 && attempt.raw[1] == 456 &&
        attempt.raw[2] == -789);
  CHECK(normal.stop() == Ak09911Result::Ok);
  // Every transport failure during initialization is visible; cleanup is still
  // attempted after identity succeeded and never writes after a failed ID read.
  for (int fail = 1; fail <= start_operations; ++fail) {
    Bus broken;
    broken.fail_operation = fail;
    Ak09911 driver(broken);
    CHECK(driver.start() == Ak09911Result::IoError);
    CHECK(driver.latest().received_at_us == 0);
    CHECK(driver.stop() == Ak09911Result::Ok);
    if (fail == 1)
      CHECK(broken.writes.empty());
    else
      CHECK(broken.regs[0x31] == 0);
  }
  Bus stuck;
  Ak09911 stuck_sensor(stuck);
  CHECK(stuck_sensor.start() == Ak09911Result::Ok);
  stuck.mode_readback = 2;
  CHECK(stuck_sensor.stop() ==
        Ak09911Result::InvalidData); // ACK alone is not stop
  stuck.mode_readback = -1;
  CHECK(stuck_sensor.stop() == Ak09911Result::Ok);

  Bus status_bus;
  Ak09911 status_sensor(status_bus);
  CHECK(status_sensor.start() == Ak09911Result::Ok);
  status_bus.frame(1, 2, 3, 1, 1);
  CHECK(status_sensor.read(attempt) == Ak09911Result::InvalidData);
  status_bus.frame(1, 2, 3);
  CHECK(status_sensor.read(attempt) == Ak09911Result::Sample);
  const auto status_time = status_sensor.latest().received_at_us;
  status_bus.fail_operation = status_bus.operation + 1;
  CHECK(status_sensor.read(attempt) == Ak09911Result::IoError);
  CHECK(status_sensor.latest().received_at_us == status_time);
  CHECK(status_sensor.stop() == Ak09911Result::Ok);

  Bus stop_bus;
  Ak09911 stopping(stop_bus);
  CHECK(stopping.start() == Ak09911Result::Ok);
  stop_bus.fail_operation = stop_bus.operation + 1;
  CHECK(stopping.stop() == Ak09911Result::IoError);
  CHECK(stopping.read(attempt) == Ak09911Result::NotStarted);
  CHECK(stopping.stop() == Ak09911Result::Ok);
  std::printf(
      "AK09911 host sequence checks: %d failure(s); hardware not tested here\n",
      failures);
  return failures ? 1 : 0;
}
