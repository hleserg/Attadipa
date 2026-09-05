// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The board's own GNSS receiver, on the one uncommitted channel it has.
//
// `docs/research/WAVESHARE_BOARD_RECEIVED.md:159` — "`RXD`/`TXD` pair: **UART0, `RXD` = GPIO 44, `TXD` = GPIO 43**, traced in §4 of"
// — is the whole hardware story: pad 7 is this CPU's receive line, and a module
// wired to it talks. Nothing on this board is a GNSS receiver
// (`docs/research/HARDWARE_MATRIX.md:407` — "| GNSS | — | **not present** | — | — | VERIFIED |"),
// which is why the feature is off by default: turning it on is the
// configuration change that makes the navigation readout's `Unprovisioned`
// answer stop being the true one.
//
// **Receive only.** Issue #429's Definition of Done names no transmit path, so
// the port's TX stays unrouted and pad 8 is never driven. A driver that
// attached it would be pushing against whatever the module's own transmitter
// does on a net nothing here has measured, for the sake of commands nobody has
// asked it to send.
//
// ## Everything below runs on the LVGL task, and there is no lock
//
// `local_gnss_tick()` is called from `refresh_ui()`, `local_gnss_location()`
// from `refresh_nav()`, and both of those are LVGL timer callbacks —
// `physical_input.cpp:105` — "    lv_timer_t *timer = lv_timer_create(sleep_timer, kPollMs, this);"
// shows the sleep path is one too, so even the wake redraw is that same task.
// One task, no snapshot, no critical section. `meshcore_ble.cpp` copies its
// `LocationState` under a lock because its worker is a *different* task; this
// is not that, and adding a lock here would be cargo.
//
// `local_gnss_start()` is the exception: it runs on the boot task while the UI
// timer is already ticking, which is what the atomic guard inside is for.

#pragma once

#include "esp_err.h"

#include "attadipa/core/location_service.h"

namespace attadipa::firmware {

// Open the port. Non-fatal by construction: a failure is logged and leaves the
// provider unbound, so the readout says `Unprovisioned` — "a supported provider
// would give it; none is bound" — rather than the watch refusing to boot over a
// receiver it may not even have. Returns the driver's error for the log.
//
// The one allocation is ESP-IDF's RX ring, made once here and never freed:
// this is called after the last step boot can roll back, so no teardown path
// can reach it (ADR-0016 asks for no *new* task, timer or queue; the UART
// driver's event queue is explicitly not created).
esp_err_t local_gnss_start();

// Read whatever the ring holds, hand it to the parser, age the result. Cheap
// and safe to call when the feature is off or the port never opened.
void local_gnss_tick();

// This device's own position, as `core::LocationService` judges it.
attadipa::core::LocationState local_gnss_location();

}  // namespace attadipa::firmware
