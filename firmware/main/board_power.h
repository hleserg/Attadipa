#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#include "attadipa/core/power_owner.h"

// The board's power owner: the only translation unit that may stop this CPU,
// arm a wake source, or write an AXP2101 rail register.
//
// docs/adr/0016-one-power-owner.md §1 states that as a rule with a list —
// sleep entry, `esp_sleep_enable_*`, and registers 0x80, 0x90, 0x82 and
// 0x92–0x95 — and `tools/flash/one_power_owner.py` is what makes it a rule
// rather than a paragraph. Everything above this file talks to
// `attadipa::core::PowerOwner`, which knows nothing about ESP-IDF.
//
// Two board facts shape the implementation, and both are why the generic
// mechanism could not be written to assume them:
//
//   * **The power button is not an SoC wake source.** The AXP2101 latches an
//     edge in register 0x49 and the firmware reads it during a timer wake, so
//     a button press arrives as `WakeCauses::derived` and never as something
//     that was armed. `arm_wake(Button)` therefore refuses: a board that said
//     yes here would have invented the exact state the ADR exists to prevent.
//   * **No rail may be switched.** `set_rail()` refuses every request. ALDO2
//     is not a supply on this board but a 10 K pull-up holding `DSI_PWR_EN`
//     high (D13, resolved 2026-08-28), and the measurements that would justify
//     gating anything are all UNKNOWN. The seam exists; the authorisation does
//     not.

namespace attadipa::firmware {

// Bring the rails this board needs up, and own only those.
//
// Moved here unchanged from `waveshare_board.cpp`'s `initialize_pmu()`: same
// registers, same values, same order, same log line. It lives here because
// ADR-0016 §1 puts every AXP2101 rail write in one translation unit, and a
// boot-time write is still a rail write.
esp_err_t board_power_bring_up_rails(i2c_master_dev_handle_t pmu);

// Bind the owner to this board's hardware. Called once, before any sleep.
esp_err_t board_power_attach(i2c_master_dev_handle_t pmu,
                            esp_lcd_panel_handle_t panel,
                            gpio_num_t touch_interrupt,
                            std::uint8_t awake_brightness);

// The one owner. Valid after `board_power_attach()` has returned ESP_OK; before
// that its `PowerHardware` refuses everything, which is the honest answer for a
// board nobody has bound yet.
attadipa::core::PowerOwner &board_power_owner();

// The next light sleep wakes on this timer instead of polling the PMU, once.
//
// The debug wake the USB control plane uses to prove a sleep/wake cycle
// happened without a finger on the panel. It is a property of the next sleep,
// not of the owner, and it clears itself.
//
// It takes the value rather than raising the flag, because the caller knows
// both answers: a button-up from a real finger says `false`, and it has to say
// it, or a remote press followed by a local one still sleeps on the debug
// timer and the local press waits out the debug delay before it is noticed.
void board_power_set_debug_timer_wake(bool on);

} // namespace attadipa::firmware
