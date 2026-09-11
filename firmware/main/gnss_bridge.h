// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A bring-up instrument, not a GNSS driver.
//
// The T-Watch's GNSS module is a u-blox MIA-M10Q, and THIS FILE IS WHAT
// ESTABLISHED IT. A `UBX-MON-VER` poll over the watch's own UART on 2026-09-05
// came back with the module naming its own part number —
// `docs/research/TWATCH_GNSS_READOFF_2026-09-05.md:22` — "ext : MOD=MIA-M10Q".
// Before that the answer rested on a recollection, and an earlier draft of this
// header still said the firmware had never spoken to the module. It had; the
// run is in the report above, and the row that used to be the reason to ask
// now records the answer — `docs/research/OPEN_QUESTIONS.md:35` — "The GNSS
// half was read off the part on 2026-09-05".
//
// WHAT IS STILL OPEN IS THE DRIVER. There is none, and reading a part number is
// not agreement with a datasheet: what the module accepts, what DC4 must hold
// for it, and what assistance costs are all still unmeasured here.
//
// It knows the pins and the candidate protocols and nothing else. It asserts no
// module identity of its own — it prints what the port answers — writes no GNSS
// configuration, and saves nothing anywhere: every command it sends is a
// version query. Nothing here may be promoted into a driver — a driver is
// written once this and a datasheet agree.

#pragma once

namespace attadipa::firmware {

// Sweeps baud rate and UART orientation until something answers, logs what the
// port carries, then asks in u-blox, ALLYSTAR and three ASCII dialects who is
// there. A silent port is logged as a silent port: the absence of an answer is
// recorded as this instrument's result, never as the module's property.
void run_gnss_bridge();

}  // namespace attadipa::firmware
