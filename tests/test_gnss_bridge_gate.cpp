// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// #718: a GNSS bridge image whose PMU or BLDO1 setup failed still swept the
// UART for ESTIMATED 16.8 s and finished by stating `BLDO1 is up and
// documented`. Three different boots were being described by one sentence.
//
// What this pins is the whole truth table, not the one row the defect was on,
// because the two ways to get it wrong point in opposite directions: sweeping
// on an unestablished rail is the defect, and refusing to sweep because a
// panel failed would be the over-correction. Both are one line's worth of
// edit, so both are asserted.

#include "gnss_bridge_gate.h"

#include <cassert>
#include <string_view>

namespace {

using attadipa::firmware::GnssBridgePlan;
using attadipa::firmware::GnssRailPrereq;
using attadipa::firmware::gnss_rail_evidence;
using attadipa::firmware::plan_gnss_bridge;

constexpr GnssRailPrereq kStates[] = {
    GnssRailPrereq::NotAttempted,
    GnssRailPrereq::Failed,
    GnssRailPrereq::EnableReadsBack,
};

} // namespace

int main() {
  // 1. The prerequisite never ran -- main I2C creation or the AXP2101 attach
  //    failed, so `board_power_enable_gnss_rail()` was never reached. No
  //    sweep, and therefore no passthrough: passthrough is only entered after
  //    a port is found, and no port is opened.
  const GnssBridgePlan unattempted =
      plan_gnss_bridge(GnssRailPrereq::NotAttempted, true);
  assert(!unattempted.sweep);

  // 2. The prerequisite ran and failed -- a BLDO1 voltage write, the LDO
  //    enable read, the enable write, or an enable bit that did not read back.
  const GnssBridgePlan failed = plan_gnss_bridge(GnssRailPrereq::Failed, true);
  assert(!failed.sweep);

  // 3. Rail confirmed, and a *later* display or touch step failed. The bridge
  //    still runs. This is the row that makes `ui_err == ESP_OK` the wrong
  //    gate: the boot that lost its panel is the boot a bench most needs the
  //    instrument on, and nothing a panel does reaches the GNSS module.
  const GnssBridgePlan late_ui_failure =
      plan_gnss_bridge(GnssRailPrereq::EnableReadsBack, false);
  assert(late_ui_failure.sweep);
  assert(late_ui_failure.ui_failed);

  // 4. Rail confirmed and the UI fine: the ordinary run, and the only one that
  //    reaches the silent-UART diagnostic with nothing to apologise for.
  const GnssBridgePlan ordinary =
      plan_gnss_bridge(GnssRailPrereq::EnableReadsBack, true);
  assert(ordinary.sweep);
  assert(!ordinary.ui_failed);

  // 5. The mutation guard. The gate is the prerequisite and nothing else, over
  //    every combination -- so deleting it, widening it to any state, or
  //    quietly adding `&& ui_ok` each fail here rather than on a bench.
  for (const GnssRailPrereq prereq : kStates) {
    for (const bool ui_ok : {false, true}) {
      const GnssBridgePlan plan = plan_gnss_bridge(prereq, ui_ok);
      assert(plan.sweep == (prereq == GnssRailPrereq::EnableReadsBack));
      assert(plan.ui_failed == !ui_ok);
      assert(plan.rail == gnss_rail_evidence(prereq));
    }
  }

  // 6. The wording tracks the evidence, which is the half of #718 that a gate
  //    alone does not fix. One sentence for three states is what printed a
  //    rail claim on a boot that had not established one, so the three stay
  //    distinct and non-empty.
  const std::string_view unattempted_text =
      gnss_rail_evidence(GnssRailPrereq::NotAttempted);
  const std::string_view failed_text =
      gnss_rail_evidence(GnssRailPrereq::Failed);
  const std::string_view confirmed_text =
      gnss_rail_evidence(GnssRailPrereq::EnableReadsBack);
  assert(!unattempted_text.empty());
  assert(unattempted_text != failed_text);
  assert(failed_text != confirmed_text);
  assert(confirmed_text != unattempted_text);

  // Neither unestablished state may read as a rail state, and both say so in
  // the word this repository uses for it.
  assert(unattempted_text.find("UNKNOWN") != std::string_view::npos);
  assert(failed_text.find("UNKNOWN") != std::string_view::npos);
  assert(confirmed_text.find("UNKNOWN") == std::string_view::npos);

  // And the strongest state says what it is: a register read-back, which is
  // all an AXP2101 transaction can establish, and explicitly not a voltage.
  assert(confirmed_text.find("read-back") != std::string_view::npos);
  assert(confirmed_text.find("not a measured voltage") !=
         std::string_view::npos);

  // None of the three may be the sentence #718 was filed about.
  for (const GnssRailPrereq prereq : kStates) {
    assert(std::string_view(gnss_rail_evidence(prereq)).find("BLDO1 is up") ==
           std::string_view::npos);
  }
}
