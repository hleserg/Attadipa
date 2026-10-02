#pragma once

#include <cstdint>

namespace attadipa::firmware {

// The two T-Watch outputs that are fitted, owned, and driven by no application
// (#713). `docs/architecture/ARCHITECTURE.md:462` -- "These exist, are initialised, are put in a defined low-power state, and appear"
// -- requires exactly that of them, and until this header existed neither
// shipping T-Watch image performed either operation: a tree-wide search for
// `GPIO_NUM_2`, `CHGLED` and PMU register `0x69` outside `docs/` found nothing.
//
// Two operations rather than a framework, deliberately. The issue asked for a
// dormant-output *policy* and refused a dormant-output *mechanism*, and the two
// parts have nothing in common below the word "dormant": one is a pad on this
// SoC, the other is a pin on a chip across an I2C bus. What they share is this
// header, which owns neither of them -- it owns the *order* in which their two
// steps happen, which is the part that is wrong silently and the part a host
// can check. The hardware calls stay with their owners: the pad in
// `twatch_board.cpp`, the register in `board_power.cpp`, which ADR-0016 §1
// makes the only translation unit that may talk to the AXP2101 about power.
//
// Neither function establishes a capability. `Capability::InfraredBlast` stays
// `Availability::Off` and `HardwareState::Untouched` after both of them
// succeed: driving a pin to its documented idle level is the opposite of
// implementing an `InfraredService`, and `core/src/capability_registry.cpp:137`
// -- "        case Capability::InfraredBlast:" -- is not touched by this change.

// IR12-21C emitter base drive: latch low, *then* enable the output.
//
// The order is the whole function, and it is not the order the nearest
// board-owned output in this tree uses. `twatch_board.cpp`'s backlight calls
// `gpio_config()` first and `gpio_set_level(..., 0)` second, which is safe
// there only because that transistor is dark at reset and the pin carries a
// strap pull-down. The IR pad has neither: the ESP32-S3 datasheet v2.2
// Table 2-1 gives GPIO2 `IE` both at reset and after reset, with no `WPU` and
// no `WPD`, and the schematic puts GPIO2 high = LED conducting. Configure
// first and the output driver is enabled over whatever the latch happens to
// hold.
//
// Latching first is glitch-free against the pinned ESP-IDF v5.5.5 and this is
// why: `gpio_set_level()` gates only on `GPIO_IS_VALID_OUTPUT_GPIO`, which asks
// whether the *pad* can ever drive, not whether it is driving now, so the write
// lands in the output latch while the driver is still off. `gpio_config()` then
// reaches the enable through `gpio_output_enable()`, which points the GPIO
// matrix at the latch (`gpio_hal_matrix_out_default`) *before* it raises the
// enable (`gpio_hal_output_enable`) -- so the first level the pad ever drives
// is the one already latched. Not an inference from documentation, which is
// silent on a level written to a non-output pin; read out of
// `components/esp_driver_gpio/src/gpio.c` at the pinned tag.
//
// `false` means the pad's state is unknown, never that the emitter is silent.
template <typename Ops>
bool establish_ir_safe_idle(Ops &ops) {
  if (!ops.latch_low()) {
    return false;
  }
  return ops.enable_output();
}

// AXP2101 REG 0x69 with the CHGLED pin function disabled, and nothing else
// disturbed.
//
// Bit 0 only, because bit 0 is the only bit in that register this product has
// an opinion about and the only one whose power-on value is knowable. From the
// vendor register description (X-Powers AXP2101 switch-charger V1.0,
// §6.13.2.67, p. 40), by field:
//
//   * `0` -- "CHGLED pin enable / 0: disable CHGLED pin function", `RW`, reset
//     `POR`, default **`1b`**. Fitted hardware therefore comes up with the pin
//     function *on*, which is the defect: `docs/research/HARDWARE_MATRIX.md:108`
//     -- "driven by the AXP2101 `CHGLED` pin through R182 100 Ω"
//     -- puts a real LED on that pin on this board.
//   * `2:1` -- display-function select, `RW`, reset `POR` **`EFUSE`**. No
//     datasheet default exists to write back, so this unit's value is whatever
//     its eFuse says and preserving it is the only honest option. Clearing bit
//     0 makes the selection moot without pretending to know it.
//   * `5:4` -- the output level, and only "when the register of chgled_func
//     (REG69[2:1]) is set to 10b". Writing it would be writing a field that
//     does nothing in the mode this unit is probably in.
//   * `7:6` and `3` -- `RO`.
//
// §6.7.5 is why clearing the enable is sufficient rather than merely tidy: the
// pin "is internally pulled up to LDO", and Table 6-6 ends "Note: LED is on
// when CHGLED is low." A disabled pin function is not driven low, so the
// internal pull-up holds the cathode at the LED's own supply and the part does
// not conduct. That is a datasheet argument about a circuit, not a measurement
// -- what the LED and the branch current actually do on the owner's unit is
// UNKNOWN until somebody looks.
constexpr std::uint8_t chgled_pin_disabled(std::uint8_t current) {
  return static_cast<std::uint8_t>(current & 0xFEU);
}

// Read REG 0x69, clear bit 0, write it back.
//
// Read-modify-write rather than a literal because of the `EFUSE` row above: a
// whole-register write would have to invent a value for bits `2:1`, and the one
// thing established about them is that this repository cannot know them. A
// failed read therefore cannot be substituted for with a guess -- it has to
// fail, and it does.
//
// `false` means the CHGLED state is unknown. It specifically does not mean the
// LED is off, and the caller must not log it as though it were.
template <typename Ops>
bool establish_chgled_off(Ops &ops) {
  std::uint8_t current = 0;
  if (!ops.read(current)) {
    return false;
  }
  return ops.write(chgled_pin_disabled(current));
}

} // namespace attadipa::firmware
