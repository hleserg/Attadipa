#include "twatch_dormant_outputs.h"

#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

// The IR pad's two steps, recorded in the order they are asked for. `failing`
// names the step that refuses, because the two failures are not the same
// finding: a refused latch must stop the sequence before the driver is ever
// enabled, and a refused enable must not be reported as success just because
// the latch worked.
struct FakeIrOps {
  std::vector<std::string_view> calls;
  std::string_view failing;

  bool latch_low() {
    calls.push_back("latch low");
    return failing != "latch low";
  }
  bool enable_output() {
    calls.push_back("enable output");
    return failing != "enable output";
  }
};

// REG 0x69 as a byte with a bus in front of it.
struct FakeChgLedOps {
  std::uint8_t reg = 0;
  std::vector<std::string_view> calls;
  std::vector<std::uint8_t> written;
  bool read_fails = false;
  bool write_fails = false;

  bool read(std::uint8_t &value) {
    calls.push_back("read");
    if (read_fails) {
      // A refused read leaves the out-parameter alone. The template must not
      // then write a byte derived from it -- that is the guess the
      // read-modify-write exists to avoid, and it would clear the eFuse-chosen
      // mode bits of a register nobody managed to look at.
      return false;
    }
    value = reg;
    return true;
  }
  bool write(std::uint8_t value) {
    calls.push_back("write");
    written.push_back(value);
    if (write_fails) {
      return false;
    }
    reg = value;
    return true;
  }
};

} // namespace

int main() {
  // The order, which is the whole reason this template is not two lines inline
  // in `twatch_board.cpp`. The latch is written while the output driver is
  // still off, so enabling it cannot present a high level to Q15's base.
  FakeIrOps ir;
  assert(attadipa::firmware::establish_ir_safe_idle(ir));
  assert((ir.calls == std::vector<std::string_view>{"latch low",
                                                    "enable output"}));

  // A refused latch stops there. Enabling the output after it would hand the
  // pad to the driver over an unknown latch value -- the exact glitch the order
  // above exists to prevent, arriving by way of the error path.
  FakeIrOps latch_refused;
  latch_refused.failing = "latch low";
  assert(!attadipa::firmware::establish_ir_safe_idle(latch_refused));
  assert((latch_refused.calls == std::vector<std::string_view>{"latch low"}));

  // A refused enable is still a failure, and this is the case a sequence that
  // returned the first call's result would have got wrong.
  FakeIrOps enable_refused;
  enable_refused.failing = "enable output";
  assert(!attadipa::firmware::establish_ir_safe_idle(enable_refused));
  assert((enable_refused.calls ==
          std::vector<std::string_view>{"latch low", "enable output"}));

  // Bit 0 falls and bits 7:1 survive, for every byte the register can hold.
  // Exhaustive rather than sampled because the field that must survive is
  // `EFUSE`-defaulted: this repository does not know which of the 256 values a
  // given unit presents, so "the ones we thought to try" is not a basis.
  for (unsigned value = 0; value <= 0xFFU; ++value) {
    const auto before = static_cast<std::uint8_t>(value);
    const std::uint8_t after = attadipa::firmware::chgled_pin_disabled(before);
    assert((after & 0x01U) == 0U);
    assert((after & 0xFEU) == (before & 0xFEU));
  }

  // The shipping case: the POR default has bit 0 set, and the two mode bits
  // below it are whatever the eFuse chose. 0x07 is "enabled, type B" -- a value
  // with every bit this function must not touch set, so a mask that was too
  // wide shows up here rather than on a bench.
  FakeChgLedOps enabled;
  enabled.reg = 0x07;
  assert(attadipa::firmware::establish_chgled_off(enabled));
  assert((enabled.calls == std::vector<std::string_view>{"read", "write"}));
  assert(enabled.written == std::vector<std::uint8_t>{0x06});
  assert(enabled.reg == 0x06);

  // Already clear, and still written. The register is asserted rather than
  // reconciled: an image that skipped the write would have no transaction to
  // point at on the one boot where the read was the stale answer.
  FakeChgLedOps already_off;
  already_off.reg = 0x06;
  assert(attadipa::firmware::establish_chgled_off(already_off));
  assert((already_off.calls == std::vector<std::string_view>{"read", "write"}));
  assert(already_off.written == std::vector<std::uint8_t>{0x06});

  // A refused read writes nothing at all. This is the assertion that keeps the
  // read-modify-write honest: with no byte in hand there is no value to send,
  // and sending a literal would blank bits 2:1.
  FakeChgLedOps read_refused;
  read_refused.reg = 0x07;
  read_refused.read_fails = true;
  assert(!attadipa::firmware::establish_chgled_off(read_refused));
  assert((read_refused.calls == std::vector<std::string_view>{"read"}));
  assert(read_refused.written.empty());

  // A refused write fails the operation, and the byte it tried to send was
  // still the right one -- so a bench reading a FAILED line knows the fault is
  // the bus, not the value.
  FakeChgLedOps write_refused;
  write_refused.reg = 0x07;
  write_refused.write_fails = true;
  assert(!attadipa::firmware::establish_chgled_off(write_refused));
  assert((write_refused.calls == std::vector<std::string_view>{"read", "write"}));
  assert(write_refused.written == std::vector<std::uint8_t>{0x06});
}
