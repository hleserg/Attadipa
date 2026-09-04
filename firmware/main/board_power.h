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
// That list is three operations and the ADR's sentence names five: the two it
// leaves out are turning the AMOLED off or on and publishing `Availability`.
// The three named are exactly the ones the ADR's *enforcement* clause names,
// which is why the check has no panel pattern. The owner does turn the panel
// off and on for a sleep, but `waveshare_board.cpp` also switches it at boot,
// so the AMOLED clause is the one part of section 1 that is neither exclusive
// nor checked. Harmless today -- both sites take brightness from the same
// `kBrightnessPercent` and boot cannot race a sleep -- and recorded rather
// than fixed, because moving the boot call means reordering it against
// `lvgl_port_init()` and `start_physical_input()`. What it would cost silent
// is a second display-power site added during T-Watch bring-up by somebody
// who read the CI step as enforcing the whole sentence.
//
// `Availability` is uncovered the other way round: the owner publishes it and
// nothing reads it. `PowerOwner::availability()` has no caller outside `core/`
// and `tests/`, so a `Failed` latch after a failed unwind leaves the watch
// showing nothing and one `ESP_LOGE` on a serial port a wearer does not have.
// ADR-0016 section 4 wants the layer above to decide to reboot, and there is no
// such layer yet. The pattern for it already exists -- `MeshCoreCompanion`
// writes `status_.availability` and the registry carries it to the UI through
// `apps/src/app_manifest.cpp` -- so wiring power in is a capability entry, not a
// new mechanism. It is a feature rather than part of this seam, and `recover()`
// consumes the latch inside the owner, so reading it late costs nothing.
//
// Two board facts shape the implementation, and both are why the generic
// mechanism could not be written to assume them:
//
//   * **The power button is not an SoC wake source.** The AXP2101 latches an
//     edge in register 0x49 and the firmware reads it on any wake this
//     transaction armed -- the poll timer, the touch line, or both together --
//     so a button press arrives as `WakeCauses::derived` and never as something
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
// registers, same values, same order, same format string. The tag in front of
// it is not the same -- it is `board-power` here and was `waveshare` there, so
// a bench transcript from before this move greps differently. It lives here
// because ADR-0016 §1 puts every AXP2101 rail write in one translation unit,
// and a boot-time write is still a rail write.
esp_err_t board_power_bring_up_rails(i2c_master_dev_handle_t pmu);

// Bind the owner to this board's hardware. Called once, before any sleep.
// `GPIO_NUM_NC` for the touch line means boot found no touch controller: the
// owner then refuses `WakeSource::Touch` rather than arm an undriven pin.
esp_err_t board_power_attach(i2c_master_dev_handle_t pmu,
                            esp_lcd_panel_handle_t panel,
                            gpio_num_t touch_interrupt,
                            std::uint8_t awake_brightness);

// Make the owner inert before boot rollback releases the handles it was given.
void board_power_detach();

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
