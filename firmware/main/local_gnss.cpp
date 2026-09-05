// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdkconfig.h"  // before the first CONFIG_ test: Kconfig leaves an off
                        // symbol undefined, so an unreachable sdkconfig.h makes
                        // every branch here agree on false and compile clean.

#include "local_gnss.h"

#include <atomic>
#include <cstdint>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#if CONFIG_ATTADIPA_GNSS_LOCAL
// Guarded with the feature, because the layer is: firmware/main/CMakeLists.txt
// adds gnss/ to the build only when the symbol is on, so with it off this
// header is not on the include path and must not be reached.
#include "attadipa/gnss/nmea_receiver.h"
#endif

namespace attadipa::firmware {
namespace {

#if CONFIG_ATTADIPA_GNSS_LOCAL

constexpr char kTag[] = "gnss";

constexpr uart_port_t kPort = UART_NUM_1;

// UART1, not UART0, although the pad is labelled for UART0 and GPIO 44 is this
// chip's `U0RXD`. Nothing is taken from the console by doing so: this project
// builds with `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` and
// `CONFIG_ESP_CONSOLE_UART_NUM=-1`, so there is no UART console on this image
// at all — checked in the generated `build-gnss/sdkconfig`, not assumed from
// the defaults file. The GPIO matrix makes the peripheral independent of the
// pin anyway, and leaving UART0 unclaimed means a bench session can still open
// a serial console on these pads without fighting this driver for them.
constexpr int kRxPin = CONFIG_ATTADIPA_GNSS_LOCAL_RX;
constexpr int kBaud  = CONFIG_ATTADIPA_GNSS_LOCAL_BAUD;

// THESE TWO ARE A PAIR, AND THE INEQUALITY BETWEEN THEM IS THE POINT: the ring
// must hold at least `kStaleGapMs` of traffic. A gap shorter than that keeps
// its bytes rather than flushing them, so the ring has to have held them all —
// otherwise the ESP-IDF driver has dropped the *new* bytes to keep the old
// ones, and this tick reads a backlog whose newest sentence is already seconds
// behind, which is precisely the stamp `drain()` exists to refuse.
//
// MEASURED, over the sixteen #427 bench captures: the most bytes any three
// consecutive seconds of them contain is **4127**, counted in
// `docs/research/GNSS_MODULES_READOFF_2026-09-04.md:410` — "### 3.1 How much a receiver says in a second — MEASURED, and a buffer depends on it".
// So 4096 would have been 31 bytes short of the worst case actually observed,
// and 8192 clears it by 98%.
//
// That worst window is a power cycle — a burst of `$GNTXT` banner on top of a
// full sky — which is the same event that produces a long gap, so it is the
// right case to size against rather than an outlier to set aside.
constexpr int kRxRing = 8192;

// The UI timer runs at 500–1000 ms, so three seconds is past the slowest of
// those with margin — a tick that merely ran late does not throw bytes away —
// and short enough that a flush discards seconds, not minutes.
constexpr std::uint64_t kStaleGapMs = 3000;

// One tick may not spend longer than this reading. Twice the ring, so a tick
// after a busy second always empties it; a module babbling faster than the
// captures would otherwise hold the LVGL task for as long as it kept talking.
constexpr std::size_t kBytesPerTick = 16384;

attadipa::gnss::NmeaReceiver     receiver;
attadipa::core::LocationService  location(receiver);
attadipa::core::LocationState    published;

// `local_gnss_start()` runs on the boot task; the tick runs on the LVGL task,
// which is already ticking by then. This is the handover, and it is set last,
// after the pins are routed — `uart_is_driver_installed()` would answer yes in
// the middle of setup, which is the window this exists to close.
std::atomic<bool> port_open{false};

std::uint64_t millis()
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

std::uint64_t last_tick_ms = 0;

// The one production reader of the two byte counters, and the only place a
// wrong pin, a wrong baud rate or an unpowered module says so out loud. The
// screen's `OwnReceiverSilent` tells the wearer the receiver is not answering;
// this tells a bench session which *kind* of nothing it is, and there are two
// of those, not three:
//
//   0 unframed, 0 discarded — nothing is on the wire. A pad not connected, a
//     module with no supply, or the wrong GPIO.
//   anything else — bytes are arriving and are not framing as NMEA. Check the
//     baud rate, the line level, and whether the module emits NMEA at all; the
//     GT-U12 on this bench does not: `docs/research/OPEN_QUESTIONS.md:93` — "speaking ALLYSTAR binary behind the sync word **`F1 D9`**"
//
// WHICH OF THOSE THREE IT IS CANNOT BE READ OFF THE RATIO, and an earlier
// version of this comment claimed it could. A binary stream contains `0x24`
// like any other byte — about every 256 — and a CR or LF about every 128, so
// it frames runs and fails them exactly as a stream at the wrong baud does.
// Both numbers move in both cases. The pair separates "something" from
// "nothing" and hands the rest to a person with an oscilloscope.
//
// Neither number is reset by the flush at a gap, so a bench session reads a
// total since boot rather than since the last wake.
//
// Five seconds before it fires, because `Unreachable` is also the honest answer
// for the first second of every boot and every wake, and a warning on every
// healthy boot is a warning nobody reads. Edge-triggered on top of that: it
// logs once per silence, and a receiver that comes back and goes quiet again
// gets a second line.
constexpr std::uint64_t kQuietWarnAfterMs = 5000;
std::uint64_t quiet_since_ms = 0;  // 0 while sentences are arriving
bool          quiet_logged   = false;

void warn_if_quiet(attadipa::core::MonotonicTime now)
{
    if (receiver.availability() != attadipa::core::Availability::Unreachable) {
        quiet_since_ms = 0;
        quiet_logged   = false;
        return;
    }
    if (quiet_since_ms == 0) {
        quiet_since_ms = now.ms;
        return;
    }
    if (!quiet_logged && now.ms - quiet_since_ms >= kQuietWarnAfterMs) {
        quiet_logged = true;
        ESP_LOGW(kTag, "nothing framable from GPIO %d for %llu ms: %lu bytes "
                       "outside a sentence, %lu sentences discarded; if both "
                       "are zero check the module's power and this pad's "
                       "wiring, and if they are not check %d baud, the line "
                       "level, and that the module emits NMEA at all",
                 kRxPin, static_cast<unsigned long long>(now.ms - quiet_since_ms),
                 static_cast<unsigned long>(receiver.unframed()),
                 static_cast<unsigned long>(receiver.discarded()), kBaud);
    }
}

// WHAT IS IN THE RING AFTER A GAP IS NOT A FIX, IT IS A MEMORY.
//
// ESP-IDF's RX ring drops *new* bytes when it is full, so the bytes that
// survive a long gap are the oldest ones. Parsing them would hand
// `NmeaReceiver` an epoch from before the gap and it would stamp it with the
// `now` it is given, which is the one thing this whole vertical slice exists
// not to do: an arrival time presented as an observation time. The watch would
// show a minute-old position as a current fix.
//
// So a gap longer than a tick could honestly explain throws the ring away. The
// board sleeps inside an LVGL timer callback (`physical_input.cpp`), so this
// fires on every wake, and the receiver's own silence timeout then reports
// `Unreachable` for as long as it takes real sentences to come back — which is
// the true answer for a device that was not listening.
void drain(attadipa::core::MonotonicTime now)
{
    const std::uint64_t gap = now.ms - last_tick_ms;
    last_tick_ms = now.ms;
    if (gap > kStaleGapMs) {
        const esp_err_t err = uart_flush_input(kPort);
        ESP_LOGI(kTag, "%llu ms since the last read; %s the ring rather than "
                       "stamping stale bytes with the time they were noticed",
                 static_cast<unsigned long long>(gap),
                 err == ESP_OK ? "discarded" : "failed to discard");
        // Order matters: the ring is gone, so the sentence and the epoch it
        // was going to finish have to go with it, and only then is the
        // receiver told what time it is. Without the reset an RMC stamped
        // before the gap survives, and the first GGA after it closes that
        // epoch — publishing a coordinate observed after the gap under a
        // stamp from before it, which is the exact lie the flush is here to
        // prevent.
        receiver.reset();
        receiver.feed(nullptr, 0, now);
        return;
    }

    std::uint8_t buf[256];
    std::size_t taken = 0;
    while (taken < kBytesPerTick) {
        // Zero ticks: this is a poll of a ring somebody else is filling, on a
        // task that is drawing a watch face. It never blocks.
        const int n = uart_read_bytes(kPort, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        receiver.feed(buf, static_cast<std::size_t>(n), now);
        taken += static_cast<std::size_t>(n);
    }
    if (taken == 0) {
        // Still tell the receiver what time it is: its silence timeout is
        // measured from the `now` it is handed and there is nowhere else it
        // could come from.
        receiver.feed(nullptr, 0, now);
    }
}

#endif  // CONFIG_ATTADIPA_GNSS_LOCAL

}  // namespace

esp_err_t local_gnss_start()
{
#if CONFIG_ATTADIPA_GNSS_LOCAL
    uart_config_t cfg{};
    cfg.baud_rate = kBaud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity    = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    // Queue size 0 and a null queue handle: no event queue is created, which is
    // what ADR-0016 asks of anything joining this session. The ring is the
    // driver's own and is the single allocation named in the header.
    esp_err_t err = uart_driver_install(kPort, kRxRing, 0, 0, nullptr, 0);
    if (err == ESP_OK) {
        err = uart_param_config(kPort, &cfg);
    }
    if (err == ESP_OK) {
        // RX only. `UART_PIN_NO_CHANGE` for TX leaves this end's transmitter
        // unrouted, so pad 8 is never driven -- the header says why.
        err = uart_set_pin(kPort, UART_PIN_NO_CHANGE, kRxPin, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        if (uart_is_driver_installed(kPort)) {
            (void)uart_driver_delete(kPort);
        }
        ESP_LOGE(kTag, "UART%d on GPIO %d not opened (%s): no local receiver "
                       "this boot; the readout says so",
                 kPort, kRxPin, esp_err_to_name(err));
        return err;
    }
    last_tick_ms = millis();
    port_open.store(true);
    ESP_LOGI(kTag, "listening on GPIO %d at %d baud, NMEA, receive only",
             kRxPin, kBaud);
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

void local_gnss_tick()
{
#if CONFIG_ATTADIPA_GNSS_LOCAL
    if (!port_open.load()) {
        return;
    }
    const attadipa::core::MonotonicTime now{millis()};
    drain(now);
    warn_if_quiet(now);
    location.poll();
    published = location.state(now);
#endif
}

attadipa::core::LocationState local_gnss_location()
{
#if CONFIG_ATTADIPA_GNSS_LOCAL
    return published;
#else
    // The default is the honest one: `Unprovisioned`, "a supported provider
    // would give it; none is bound". Not `Unsupported` -- this board's
    // configuration is exactly what would change the answer.
    return {};
#endif
}

}  // namespace attadipa::firmware
