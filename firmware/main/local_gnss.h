// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The board's own GNSS receiver, on whichever channel that board gives it.
//
// Both boards, since #442. The pin and the baud are Kconfig symbols with
// per-board defaults, so nothing below names a GPIO and nothing below asks
// which board answered: `CONFIG_ATTADIPA_GNSS_LOCAL_RX` is 44 on the Waveshare
// and 41 on the T-Watch, and the two arrived by different routes.
//
// **Waveshare — a pad, and no module on it.**
// `docs/research/WAVESHARE_BOARD_RECEIVED.md:159` — "`RXD`/`TXD` pair: **UART0, `RXD` = GPIO 44, `TXD` = GPIO 43**, traced in §4 of"
// — is the *pin* half of the hardware story. Pad 7 is that CPU's receive line
// and a module wired to it talks. Nothing on that board is a GNSS receiver
// (`docs/research/HARDWARE_MATRIX.md:407` — "| GNSS | — | **not present** | — | — | VERIFIED |"),
// so turning the feature on there is the configuration change that says a
// module was soldered to the pads.
//
// **T-Watch — a module, read off the part.** GPIO 41 is the module's TX and
// GPIO 42 its RX, measured on the bench unit 2026-09-05 by sweeping both
// orientations, in `docs/research/TWATCH_GNSS_READOFF_2026-09-05.md` §2; the
// part is a u-blox MIA-M10Q at 38400 baud on BLDO1. The product ships that or a
// Quectel LS550G and one read-off does not retire the other, which is why the
// pair lives in Kconfig rather than in this file.
//
// **The half that is open is the same on both, and it is a gate rather than a
// detail:** what a module puts on the pin when it is idle.
// `docs/research/OPEN_QUESTIONS.md:93` — "the S3 is not 5 V tolerant, so the module's TX idle voltage has to be on a meter"
// — PARTIAL. No voltage is claimed here in either direction; the fact is that
// the number is NOT MEASURED, so wiring a bench module to a Waveshare pad waits
// on that reading.
//
// **Receive only.** Issue #429's Definition of Done names no transmit path, so
// the port's TX stays unrouted and pad 8 is never driven. A driver that
// attached it would be pushing against whatever the module's own transmitter
// does on a net nothing here has measured, for the sake of commands nobody has
// asked it to send.
//
// ## Everything below runs on the LVGL task, and there is no lock
//
// On the Waveshare `local_gnss_tick()` is called from `refresh_ui()` and
// `local_gnss_location()` from `refresh_nav()`, both LVGL timer callbacks —
// `firmware/main/physical_input.cpp:105` — "    lv_timer_t *timer = lv_timer_create(sleep_timer, kPollMs, this);"
// shows the sleep path is one too, so even the wake redraw is that same task.
//
// The T-Watch bring-up image draws one static screen and has no such callback,
// so `twatch_board.cpp` registers an `lv_timer` of its own for the tick. That
// is the same task by a different door: `esp_lvgl_port` runs one, every
// `lv_timer` rides it, and nothing is added but a callback. That board does not
// call `local_gnss_location()` at all yet — it has no navigation page — so the
// published state reaches a person through the log line and nothing else.
//
// One task either way, no snapshot, no critical section. `meshcore_ble.cpp`
// copies its `LocationState` under a lock because its worker is a *different*
// task; this is not that, and adding a lock here would be cargo.
//
// `local_gnss_start()` is the exception: it runs on the boot task while the UI
// timer is already ticking, which is what the atomic guard inside is for.

#pragma once

#include "esp_err.h"

#include "attadipa/core/location_service.h"

namespace attadipa::firmware {

// Open the port. Non-fatal by construction: a failure is logged and leaves the
// provider unbound, so the state stays `Unprovisioned` — "a supported provider
// would give it; none is bound" — rather than the watch refusing to boot over a
// receiver it may not even have. On the Waveshare that is what the navigation
// readout shows; on the T-Watch it is what the log line says. Returns the
// driver's error for the log.
//
// The one allocation is ESP-IDF's RX ring, made once here and never freed:
// this is called after the last step boot can roll back, so no teardown path
// can reach it. The UART driver's event queue is explicitly not created.
//
// ADR-0016 §2 says "no heap, no new task", and says it of the lease table
// rather than of this file -- so it is a house rule borrowed here, not a
// clause that already covered GNSS. It holds either way: the T-Watch's tick
// is an `lv_timer`, which is not a task. It rides the one `esp_lvgl_port`
// already owns, the same task the Waveshare's `refresh_ui` rides.
esp_err_t local_gnss_start();

// Read whatever the ring holds, hand it to the parser, age the result. Cheap
// and safe to call when the feature is off or the port never opened.
void local_gnss_tick();

// This device's own position, as `core::LocationService` judges it.
attadipa::core::LocationState local_gnss_location();

}  // namespace attadipa::firmware
