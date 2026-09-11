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
  // The part that acknowledges nothing: `REQ_FIFO` leaves read mode clear and
  // the count where it was, and 0x17 hands back 0x8000 words. That is what the
  // bench Waveshare did on 2026-09-11 when the request came from bypass, and a
  // driver that trusts the payload anyway reports those words as somebody's
  // residue.
  bool request_ignored = false;
  // A part that reflects CmdDone in STATUSINT only once CTRL8 bit 7 has
  // selected that handshake (`docs/research/VERIFIED_FACTS.md:2170`). OFF by
  // default, and deliberately: both bench sessions on the Waveshare completed
  // their commands with `CTRL8=00`, so requiring the write is stronger than the
  // evidence and would be a fact this repository has not established. What it
  // is for is the ordering question -- a driver that issues a CTRL9 command
  // before that write is correct on the part in hand and undefined on the one
  // the datasheet describes, and only a model that can be both asks it.
  bool cmddone_needs_handshake = false;
  // A part that empties its queue on the bypass-to-FIFO transition. This is the
  // bench Waveshare, MEASURED twice on 2026-09-11: `entry_fifo_words=3` and
  // `stale_words=0` on the same entry. A payload sized from the count seen
  // before that write reads filler out of an emptied queue and publishes it as
  // somebody's residue, which is what the previous image did.
  bool flush_on_fifo_entry = false;
  bool retain_on_reset = false, mismatch_watermark_restore = false;
  bool sticky_fifo = false; // a count that a payload read does not consume
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
      if ((request_ignored && !(regs[0x14] & 0x80)) ||
          (flush_on_fifo_entry && cursor + n > fifo.size())) {
        // 0x8000, word after word: what a part that ignored the request hands
        // back, and the byte the drain must not read as a sample.
        for (std::size_t i = 0; i < n; ++i)
          out[i] = static_cast<std::uint8_t>((i % 2) ? 0x80 : 0x00);
        return true;
      }
      CHECK((regs[0x14] & 0x80) != 0);
      CHECK((regs[2] & 0x60) == 0x40); // explicit byte order / burst setup
      CHECK(cursor + n <= fifo.size());
      if (fail_payload) { ++cursor; return false; }
      std::memcpy(out, fifo.data() + cursor, n);
      if (!sticky_fifo)
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
    if (reg == 0x14 && flush_on_fifo_entry && (value & 3) && !(regs[0x14] & 3)) {
      fifo.clear(); cursor = 0; extra_status = 0;
    }
    regs[reg] = value;
    if (reg == 0x13 && value == 0 && mismatch_watermark_restore)
      pending_watermark_mismatch = true;
    if (reg == 0x0a) {
      CHECK(value == 0 || value == 4 || value == 5); // never step/chip reset
      if (value == 0) regs[0x2d] &= 0x7f;
      else if (!never_done &&
               (!cmddone_needs_handshake || (regs[9] & 0x80))) {
        regs[0x2d] |= 0x80;
        // A request only takes effect from a FIFO mode. In bypass the bench
        // Waveshare kept its count and handed back 0x8000 words, 2026-09-11.
        if (value == 5 && (regs[0x14] & 3) != 0 && !request_ignored)
          regs[0x14] |= 0x80;
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
    CHECK(sensor.entry_words() == 0 && !sensor.drained());
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
  {
    // A FIFO nobody here filled: refused by default, and the refusal writes
    // nothing. This is the state the bench Waveshare was left in.
    Bus bus;
    bus.frame(0x1234, -5, 0x0678); // asymmetric: a byte swap would show
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start() == QmiResult::Busy);
    CHECK(bus.writes.empty());
    CHECK(sensor.stale_words() == 0);
    CHECK(sensor.entry_words() == 3 && !sensor.drained());
    CHECK(sensor.stop() == QmiResult::Ok);
  }
  {
    // Explicitly asked to drain it: entry succeeds, and the words it threw
    // away are readable rather than lost.
    Bus bus;
    const auto original = bus.regs;
    bus.frame(0x1234, -5, 0x0678);
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start(true) == QmiResult::Ok);
    CHECK(sensor.stale_words() == 3);
    CHECK(sensor.drained() && sensor.entry_words() == 3);
    CHECK(sensor.stale_bytes()[0] == 0x34 && sensor.stale_bytes()[1] == 0x12);
    CHECK(sensor.stale_bytes()[2] == 0xfb && sensor.stale_bytes()[3] == 0xff);
    CHECK(sensor.stale_bytes()[4] == 0x78 && sensor.stale_bytes()[5] == 0x06);
    QmiBatch batch;
    bus.frame(7, 8, 9);
    CHECK(sensor.read(batch) == QmiResult::Samples);
    CHECK(batch.count == 1 && batch.accel[0][0] == 7 && batch.accel[0][2] == 9);
    CHECK(sensor.stop() == QmiResult::Ok);
    CHECK(bus.regs == original);
  }
  {
    // The drain's own command, on a part that needs the handshake selected
    // first. With the CTRL8 write after the drain, `wait_done` never sees
    // CmdDone: the drain times out, `command_pending_` stays true with
    // `owned_` already set, and every later entry is refused above the drain
    // admission that was supposed to recover it.
    // The idle profile, because that is the one with `CTRL8=00` -- the state
    // the bench Waveshare was actually in, and the only one where the order of
    // the two writes can matter at all.
    Bus bus(false, true);
    bus.cmddone_needs_handshake = true;
    bus.frame(0x1234, -5, 0x0678);
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start(true) == QmiResult::Ok);
    CHECK(sensor.stale_words() == 3);
    CHECK(sensor.stop() == QmiResult::Ok);
  }
  {
    // A REQUEST THE PART IGNORED IS NOT A RESIDUE, AND MUST NOT BE LOGGED AS ONE.
    //
    // The refused run of 2026-09-11 read three words of `0x8000` with the
    // count unmoved after: the request had done nothing and the payload was
    // whatever the register reads when nothing is queued. The re-count in
    // `start()` refuses entry either way; what this case is about is the other
    // half, `stale_words()`, which is the only record of what the previous
    // owner left and was reporting the fill value as that record.
    Bus bus;
    bus.request_ignored = true;
    bus.frame(0x1234, -5, 0x0678);
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start(true) == QmiResult::InvalidData);
    CHECK(sensor.stale_words() == 0);
    CHECK(sensor.stop() == QmiResult::Ok);
  }
  {
    // A COUNT CAPTURED BEFORE THE FREEZE IS NOT THE COUNT THE PAYLOAD HAS.
    //
    // The drain does the bypass-to-FIFO transition itself, and on the bench
    // Waveshare that transition empties the queue: the three words `start()`
    // saw are gone before `0x17` is read. A payload sized from the entry count
    // reads filler and publishes it as "what the previous owner left" -- the
    // same false record the ignored-request case is about, reached the other
    // way. Sizing from the count re-read after `REQ_FIFO` reports zero, which
    // is what the board reports.
    Bus bus;
    bus.flush_on_fifo_entry = true;
    bus.frame(0x1234, -5, 0x0678);
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start(true) == QmiResult::Ok);
    CHECK(sensor.stale_words() == 0);
    // Zero words moved is a result, not a silence: the probe logs the drain
    // block on `drained()`, so this run is distinguishable from one that never
    // called it and from a build without the option.
    CHECK(sensor.drained() && sensor.entry_words() == 3);
    QmiBatch batch;
    bus.frame(7, 8, 9);
    CHECK(sensor.read(batch) == QmiResult::Samples);
    CHECK(batch.count == 1 && batch.accel[0][0] == 7 && batch.accel[0][2] == 9);
    CHECK(sensor.stop() == QmiResult::Ok);
  }
  {
    // A drain that does not clear the count is still a refusal. The bench
    // failure this path exists for is a FIFO that survived its own reset, so
    // a FIFO that also survives being read must not become a successful entry.
    Bus bus;
    bus.sticky_fifo = true;
    bus.frame(0x1234, -5, 0x0678);
    Qmi8658Fifo sensor(bus);
    CHECK(sensor.start(true) == QmiResult::Busy);
    CHECK(sensor.stale_words() == 3); // what it discarded is still reported
    CHECK(sensor.stop() == QmiResult::Ok);
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
