// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Whether the GNSS bring-up bridge may run, and what it is allowed to say
// about the rail it reads through.
//
// The bridge is an instrument, and an instrument that cannot say what its own
// prerequisite was is reporting noise. #718 is what that cost: an image where
// `initialize_pmu()` failed — main I2C, the AXP2101 attach, or the BLDO1
// transaction itself — rolled the boot back, and then the sweep ran anyway for
// ESTIMATED 16.8 s (2 orientations × 7 bauds × 1.2 s) and finished by stating
// `BLDO1 is up and documented`. On that path nothing had established it. The
// one sentence collapsed three states a bench has to keep apart:
//
//   1. the rail transaction never completed, so silence here means nothing;
//   2. the rail is up and a *later* panel or touch step failed, which has no
//      bearing on what the module is doing;
//   3. the rail is up and the port is genuinely silent — the only one of the
//      three that is a result about the module.
//
// So the prerequisite travels separately from the aggregate UI result, and the
// gate below reads only the prerequisite. `ui_ok` is taken and deliberately not
// consulted for `sweep`: state 2 must still run, and a reader who later
// "tidies" the gate into `ui_err == ESP_OK` is caught by
// `tests/test_gnss_bridge_gate.cpp` rather than by a bench session.
//
// Header-only and free of ESP-IDF so the decision is testable off a board, on
// the pattern `boot_rollback.h` set.

#pragma once

namespace attadipa::firmware {

// What this boot established about BLDO1, the rail the T-Watch's GNSS module
// sits on. Set by `board_power_enable_gnss_rail()`, which is the one place
// that writes it — see `board_power_gnss_rail_prereq()`.
//
// There is no `Measured` member and there is not going to be one from
// software. The strongest thing an AXP2101 transaction can establish is what
// the PMU reports about its own configuration, which is what
// `docs/research/GNSS_POWER_POLICY_MIA_M10Q.md:272` — "| `rail_on` | `BLDO1`
// enabled | AXP2101 register read-back |" — already names as the evidence for
// `rail_on`. A voltage at the module is a bench instrument's answer and
// belongs in a report.
enum class GnssRailPrereq {
  // The transaction never ran. On a bridge image that means a step before it
  // failed, because `board_power_bring_up_rails()` reaches it unconditionally
  // on this board — `CONFIG_ATTADIPA_GNSS_BRIDGE` depends on
  // `ATTADIPA_BOARD_TWATCH_S3_PLUS`, so there is no image that runs the bridge
  // and compiles the no-rail branch of `board_power_enable_gnss_rail()`.
  // Nothing is known about the rail.
  NotAttempted,
  // The transaction ran and a step of it failed: a write, a read, or an enable
  // bit that did not read back set. Nothing is known about the rail, and in
  // particular this does not claim it is off.
  Failed,
  // Every transfer completed and the PMU reads BLDO1's enable bit back set.
  // This is a configuration read-back, not a voltage.
  EnableReadsBack,
};

// What the bridge does with that, and the one line it may print about the rail.
struct GnssBridgePlan {
  bool sweep;        // may listen on the UART, and only then pass through
  const char *rail;  // the prerequisite in one sentence; never null
  bool ui_failed;    // the UI ended badly and the sweep runs regardless
};

// The prerequisite in the words its evidence supports. Three states, three
// sentences: one sentence for all three is the defect this file exists to
// stop, so the test pins them distinct.
constexpr const char *gnss_rail_evidence(GnssRailPrereq prereq) {
  switch (prereq) {
    case GnssRailPrereq::EnableReadsBack:
      return "BLDO1: the PMU reads its enable bit back set after the write. "
             "That is a register read-back, not a measured voltage.";
    case GnssRailPrereq::Failed:
      return "BLDO1: the rail transaction failed and the rail state is "
             "UNKNOWN. It is not claimed to be off either.";
    case GnssRailPrereq::NotAttempted:
      break;
  }
  return "BLDO1: the rail transaction never ran, because a step before it "
         "failed. The rail state is UNKNOWN.";
}

// `ui_ok` reports how `start_twatch_ui()` ended and does not decide anything.
// A confirmed rail outlives a failed panel or a dead touch bus — both are
// rolled back past the PMU write, and neither is something the module can
// notice — so gating here on the aggregate UI result would throw away the one
// run where the bridge is most worth having.
constexpr GnssBridgePlan plan_gnss_bridge(GnssRailPrereq prereq, bool ui_ok) {
  return {prereq == GnssRailPrereq::EnableReadsBack,
          gnss_rail_evidence(prereq), !ui_ok};
}

}  // namespace attadipa::firmware
