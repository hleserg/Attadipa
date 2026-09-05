#pragma once

#include <cstdint>

#include "attadipa/core/power_owner.h"
#include "attadipa/core/power_state.h"

// What ended a light-sleep episode, decided once for every route out of it.
//
// The board wakes for two different reasons and reads one register for both.
// The SoC says what it woke on -- a GPIO edge from the touch line, the PMU
// poll timer, or something nobody armed -- and the AXP2101 says, separately and
// after the fact, whether its power key was pressed while the CPU was stopped.
// The second reading is destructive: `0x49` is write-one-to-clear, so the read
// that finds a latched edge is also the read that spends it, and there is no
// second chance to ask.
//
// That is the whole reason this is one operation rather than a branch per wake
// route. #367's P3 finding is what happens otherwise: the GPIO route cleared
// the latch to keep it from firing on the next descent and threw the answer
// away (`(void)consume_power_edge();`), while the timer route derived
// `WakeSource::Button` from the identical read. A touch and a press arriving
// together were reported as a touch alone, and the evidence that would have
// said otherwise had already been consumed. ADR-0016 section 6 replaced the
// single-cause API precisely so that two simultaneous sources both survive, and
// a board that consumes a cause without reporting it loses one just as
// completely as the API that could only name one.
//
// Nothing here calls ESP-IDF. The SoC's bitmap has already been translated into
// `WakeCauses::from_soc` by the caller, which is the only step that needs the
// `esp_sleep_wakeup_cause_t` values, so this decision is host-testable against
// the shipping code rather than against a copy of it -- the same reason
// `power_button_edges.h` is a header.

namespace attadipa::firmware {

enum class WakeVerdict : std::uint8_t {
  // The episode is over and `causes` says why. The caller returns.
  Report,
  // Nothing happened that anyone asked to be woken for: the PMU poll fired,
  // the button was not pressed. Re-arm the poll and go back down.
  PollAgain,
};

// Classify one wake, and spend the PMU latch at most once.
//
// `causes.from_soc` is what the silicon reported, already in this project's
// vocabulary; `consume_power_edge` reads and clears the AXP2101's latched power
// key edges, returning true when it found and cleared one. `debug_wake` is the
// one-shot debug descent, which is its own reason to come back up.
//
// The order below is the contract, and each step is a claim about the hardware:
//
//   * A wake this transaction did not arm, *and nothing else*, is reported
//     without touching the PMU. Nothing armed it, so nothing here can explain
//     it, and clearing a latch would destroy evidence to answer a question that
//     was not asked.
//   * An unarmed cause that arrives *alongside* an armed one does not get that
//     treatment, and the limit is worth stating rather than discovering. This
//     function is handed `WakeCauses`, not the wake plan, so it cannot tell
//     `Timer` armed from `Timer` unarmed; all it can see is that something
//     armable is set. So the armed route runs, the latch is spent, and if the
//     latch was empty on a pure poll the verdict is `PollAgain` -- and the
//     unarmed bits do not survive the descent, because the caller *assigns*
//     `from_soc` and `unmapped_from_soc` on each wake instead of accumulating
//     them (`soc_causes_to_wake_sources` in `board_power.cpp`, whose own
//     comment gives the reason: three thousand poll wakes must not accumulate
//     into a report claiming every source at once). Reporting on any unarmed
//     bit instead would wake the device fully for a cause nobody asked about,
//     which is a power decision, not a classification one, and there is no host
//     seam for the re-arm loop to prove it against.
//   * A debug descent that ended on the timer alone is over on arrival. It did
//     not sleep to poll the PMU, and consuming a press it never went looking
//     for would swallow one the *next* real descent would have reported.
//   * Otherwise the latch is read exactly once, whatever brought the CPU back,
//     and a found edge is always `Button` in `causes.derived` -- beside Touch,
//     beside Timer, beside both.
//   * Only the pure poll goes back down, and only when the latch was empty.
template <typename ConsumePowerEdge>
WakeVerdict classify_wake(attadipa::core::WakeCauses &causes, bool debug_wake,
                          ConsumePowerEdge consume_power_edge) {
  const bool by_touch =
      (causes.from_soc &
       attadipa::core::wake_bit(attadipa::core::WakeSource::Touch)) != 0;
  const bool by_timer =
      (causes.from_soc &
       attadipa::core::wake_bit(attadipa::core::WakeSource::Timer)) != 0;

  if (!by_touch && !by_timer) {
    // Something woke this that we did not arm. The owner reports it as an
    // unexpected cause rather than swallowing it, which is the point.
    return WakeVerdict::Report;
  }
  if (!by_touch && debug_wake) {
    return WakeVerdict::Report;
  }
  if (consume_power_edge()) {
    causes.derived |=
        attadipa::core::wake_bit(attadipa::core::WakeSource::Button);
    return WakeVerdict::Report;
  }
  return by_touch ? WakeVerdict::Report : WakeVerdict::PollAgain;
}

} // namespace attadipa::firmware
