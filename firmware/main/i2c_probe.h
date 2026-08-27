// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A bring-up instrument, not a board driver.
//
// It asks the bus what is on it. It is given two pin numbers and no board
// identity, which is the only honest shape for a probe whose whole purpose is
// to run on a board this firmware has never met. Nothing here may be promoted
// into a driver: a driver is written once the scan and a datasheet agree.

#pragma once

namespace attadipa::firmware {

// Sweeps the configured I2C bus and logs every address that acknowledges, then
// reads the AXP2101 charge-voltage register if — and only if — an AXP2101
// answered. Read-only: it writes no register, because on a PMU that keeps its
// configuration across ESP32 resets a write destroys the very value the scan
// exists to recover.
void run_i2c_probe();

}  // namespace attadipa::firmware
