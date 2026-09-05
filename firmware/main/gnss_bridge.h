// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A bring-up instrument, not a GNSS driver.
//
// The T-Watch carries a GNSS module this firmware has never spoken to. Which
// one is `UNKNOWN` — `docs/research/OPEN_QUESTIONS.md:35` says the answer rests
// on "the owner's recollection" and that "a listing is a seller's claim and a
// recollection is weaker still". This asks the part instead.
//
// It knows the pins and the candidate protocols and nothing else. It asserts no
// module identity, writes no GNSS configuration, and saves nothing anywhere:
// every command it sends is a version query. Nothing here may be promoted into
// a driver — a driver is written once this and a datasheet agree.

#pragma once

namespace attadipa::firmware {

// Sweeps baud rate and UART orientation until something answers, logs what the
// port carries, then asks in u-blox, ALLYSTAR and three ASCII dialects who is
// there. A silent port is logged as a silent port: the absence of an answer is
// recorded as this instrument's result, never as the module's property.
void run_gnss_bridge();

}  // namespace attadipa::firmware
