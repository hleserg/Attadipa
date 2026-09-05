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

// A board that turned the receiver on without declaring where it listens.
// `Kconfig.projbuild` names a pin and a speed per board and falls through to 0,
// and 0 is not a usable value for either: GPIO 0 is a boot strap and no
// receiver speaks at 0 baud. Refusing here rather than at the prompt is
// deliberate -- the symbol is offered on every board on purpose, so the thing
// to catch is a board that took the offer without doing its half.
#if CONFIG_ATTADIPA_GNSS_LOCAL_RX <= 0
#error "ATTADIPA_GNSS_LOCAL is on but this board declares no RX pin: add a `default <gpio> if ATTADIPA_BOARD_<yours>` to ATTADIPA_GNSS_LOCAL_RX."
#endif
#if CONFIG_ATTADIPA_GNSS_LOCAL_BAUD <= 0
#error "ATTADIPA_GNSS_LOCAL is on but this board declares no baud: add a `default <baud> if ATTADIPA_BOARD_<yours>` to ATTADIPA_GNSS_LOCAL_BAUD."
#endif

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
//
// That paragraph is the Waveshare's story; the T-Watch's pin is GPIO 41, which
// is nobody's `U0RXD`. UART1 is right there for the plainer reason: the GPIO
// matrix does not care, and one peripheral for both boards is one fewer thing
// for the board to decide.
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
//
// The T-Watch's own module is inside that measurement rather than beside it:
// it is a u-blox M10 at 38400, and one of the two parts those captures were
// taken from — the AN3126 — is a u-blox M10 at 38400. Same family, same
// protocol version, half the wire rate of the GT-U12 that set the worst case.
// Not the same part number, so this is an argument and not a reading; the thing
// that would turn it into one is a capture from the watch.
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
//     baud rate, the line level, and whether the module emits NMEA at all.
//
// WHICH OF THOSE TWO IT IS, THE PAIR ANSWERS; WHICH FAULT PRODUCED IT, IT DOES
// NOT, and an earlier version of this comment claimed the ratio could say. A
// binary stream contains `0x24` like any other byte — about every 256 — and a
// CR or LF about every 128, so it frames runs and fails them exactly as a
// stream at the wrong baud does. Both numbers move in both cases. The pair
// separates "something" from "nothing" and hands the rest to a person with an
// oscilloscope.
//
// THE MODULE THIS OVERLAY IS CONFIGURED FOR IS NOT AN EXAMPLE OF THE SECOND
// ROW, and naming it as one is the error this paragraph replaces. The GT-U12
// emits GGA, GSA, GSV, RMC and VTG at 1 Hz, three of which this parser reads
// (`docs/research/GNSS_MODULES_READOFF_2026-09-04.md:210` — "| Sentence set | GGA, GSA, GSV, RMC, VTG at **1.000 Hz**"),
// What the part does behind its ALLYSTAR sync word is deliberately not claimed
// here. The read-off probed it with polls, so what it measured is what the
// module *replies*, not what it says unasked, and this driver asks nothing --
// `uart_set_pin()` below leaves TX unrouted. The measured NMEA carries the
// point on its own: with that part on this pad, the second row is about the
// baud, the wiring or the line level.
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

// ONE LINE PER CHANGE OF ANSWER, NOT ONE PER EPOCH.
//
// `core::format_location_line` is the repository's engineering line and this is
// its first production caller; until now only a host test asked for it. It
// prints the coordinate, both ages, the validity, the receiver state and the
// origin, and writes `UNKNOWN` in full wherever a number would imply a
// measurement -- which is why it is the right thing to log and why nothing here
// formats its own.
//
// Edge-triggered on the fields that change what the line *means*: availability,
// whether there is a coordinate at all, which source it came from, the fix type
// and the validity. Deliberately not the coordinate and not the ages. A
// receiver solving at 1 Hz moves the low digits and the age every epoch, so
// logging on those is logging every second -- which on a watch is a log nobody
// reads and a flash nobody wanted written. The five below change when the
// device's answer changes, which is the event worth a line.
struct LoggedAnswer {
    attadipa::core::Availability     availability{};
    bool                             has_position = false;
    attadipa::core::PositionSource   source{};
    attadipa::core::FixType          fix_type{};
    attadipa::core::PositionValidity validity{};

    bool operator==(const LoggedAnswer& other) const
    {
        return availability == other.availability &&
               has_position == other.has_position && source == other.source &&
               fix_type == other.fix_type && validity == other.validity;
    }
};

LoggedAnswer answer_of(const attadipa::core::LocationState& state)
{
    return {state.availability, state.has_position, state.source,
            state.fix_type, state.validity};
}

// Seeded from a default-constructed `LocationState`, whose availability is
// `Unprovisioned` -- the state of a port that never opened. A port that did
// open reads `Unreachable` until a sentence lands (`gnss/src/nmea_receiver.cpp`
// -- "if (!heard_) return Availability::Unreachable;"), so the first tick after
// a successful `local_gnss_start()` does log a line, and that line is the proof
// the port is open with nothing heard on it yet. Only a start that failed is
// quiet here, which is right: it has already logged its own error.
//
// `warn_if_quiet()` above is not a second voice for that. It speaks to a
// different fact at a different time -- five seconds of silence from a port
// that opened -- and it is the one that says to go and look at the wiring.
LoggedAnswer logged = answer_of(attadipa::core::LocationState{});

void log_if_answer_changed()
{
    const LoggedAnswer now = answer_of(published);
    if (now == logged) {
        return;
    }
    logged = now;

    // THE COORDINATE IS NOT IN THIS LINE, AND THAT IS THE POINT. What changed
    // is what `LoggedAnswer` holds, and it holds no position on purpose: a
    // watch that has moved thirty metres has not changed state and says
    // nothing here. So the line that announces the change has nothing to gain
    // from carrying the position, and a great deal to lose -- this log goes to
    // a USB console that asks nobody for a password, on a device worn by a
    // person, and `format_location_line` prints latitude and longitude at
    // 10^-7 of a degree, which is about a centimetre. Nothing else in this
    // firmware puts a coordinate at INFO. This is not going to be the first.
    ESP_LOGI(kTag, "avail %s src %s fix %s validity %s position %s",
             attadipa::core::to_string(now.availability),
             attadipa::core::to_string(now.source),
             attadipa::core::to_string(now.fix_type),
             attadipa::core::to_string(now.validity),
             now.has_position ? "held" : "none");

    // The engineering line, with the coordinate, one level down -- off in the
    // default image and reached by asking for it, which is the asking that
    // makes it a decision. This is still `core::format_location_line` and not
    // a second formatter: #442 said to use the existing one, and this is the
    // place where using it is safe.
    if (esp_log_level_get(kTag) < ESP_LOG_DEBUG) {
        return;
    }
    // 224 bytes: the line is three 16-byte numbers, a 16-byte age, an 8-byte
    // origin and about 110 of fixed text. `format_location_line` returns the
    // length it wanted, so a future field that overruns this shows up as a
    // truncation warning rather than as silence.
    char line[224];
    const std::size_t wanted =
        attadipa::core::format_location_line(published, line, sizeof(line));
    if (wanted >= sizeof(line)) {
        ESP_LOGW(kTag, "engineering line truncated at %u of %u bytes",
                 static_cast<unsigned>(sizeof(line) - 1),
                 static_cast<unsigned>(wanted));
    }
    ESP_LOGD(kTag, "%s", line);
}

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
// Waveshare sleeps inside an LVGL timer callback (`physical_input.cpp`), so this
// fires on every wake there; the T-Watch bring-up image does not sleep at all
// yet, so on that board it fires only if a tick is genuinely starved. Either
// way the receiver's own silence timeout then reports
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
    log_if_answer_changed();
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
