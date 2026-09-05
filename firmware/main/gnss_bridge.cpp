// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sdkconfig.h"  // before the first CONFIG_ test: Kconfig leaves an off
                        // symbol undefined, so an unreachable sdkconfig.h makes
                        // every branch here agree on false and compile clean.

#include "gnss_bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace attadipa::firmware {
namespace {

constexpr char kTag[] = "gnss-bridge";
constexpr uart_port_t kPort = UART_NUM_1;
constexpr int kRxBuffer = 4096;

// 38400 first because that is what the vendor's own bring-up opens the port at
// — LilyGoLib@38e6f8d LilyGoWatchS3.cpp:220, `Serial1.begin(38400, SERIAL_8N1,
// GPS_RX, GPS_TX)`. The rest are the usual GNSS defaults, commonest first.
constexpr int kBauds[] = {38400, 9600, 115200, 57600, 19200, 4800, 230400};

// Which of the two pins this CPU listens on. The pair is VERIFIED
// (`docs/research/HARDWARE_MATRIX.md:103` — "UART: TX 42, RX 41"), but that row
// does not say whose TX it is, and the vendor call it is transcribed from names
// its arguments from the CPU's side. Getting it backwards costs a silent port
// and an hour of explaining the silence, so both orientations are tried and the
// working one is logged as the answer rather than assumed.
struct Wiring {
  int rx;
  int tx;
  const char *note;
};

// What a listening window heard. Counted rather than parsed: this is deciding
// whether anything is there, not what it means.
struct Heard {
  int bytes;
  int stored;    // how much of `bytes` reached the sink -- the rest is gone
  int nmea;      // "$G" — a talker sentence starting
  int ubx;       // B5 62 — u-blox
  int allystar;  // F1 D9 — ALLYSTAR, the same frame with a different sync word
};

Heard listen(int ms, char *sink, std::size_t sink_len) {
  Heard heard{};
  std::size_t filled = 0;
  const std::int64_t deadline = esp_timer_get_time() + ms * 1000LL;
  std::uint8_t buf[512];
  std::uint8_t tail = 0;
  bool have_tail = false;

  while (esp_timer_get_time() < deadline) {
    const int n = uart_read_bytes(kPort, buf, sizeof(buf), pdMS_TO_TICKS(50));
    if (n <= 0) continue;
    heard.bytes += n;
    for (int i = 0; i < n; ++i) {
      // Pairs are counted across read boundaries, not only inside a buffer: a
      // sync word split by a read would otherwise never be seen, and at 4800
      // baud the splits are frequent.
      if (have_tail) {
        if (tail == 0x24 && buf[i] == 0x47) ++heard.nmea;        // "$G"
        if (tail == 0xB5 && buf[i] == 0x62) ++heard.ubx;
        if (tail == 0xF1 && buf[i] == 0xD9) ++heard.allystar;
      }
      tail = buf[i];
      have_tail = true;
    }
    if (sink != nullptr && filled + 1 < sink_len) {
      const std::size_t room = sink_len - filled - 1;
      const std::size_t take = (static_cast<std::size_t>(n) < room)
                                   ? static_cast<std::size_t>(n)
                                   : room;
      std::memcpy(sink + filled, buf, take);
      filled += take;
    }
  }
  if (sink != nullptr && sink_len > 0) sink[filled] = '\0';
  heard.stored = static_cast<int>(filled);
  return heard;
}

// `attach_tx` false leaves this end's transmitter unrouted. The sweep needs
// that: orientation A names GPIO 41 as *our* TX, and GPIO 41 is the module's TX
// -- the very thing the sweep is there to discover -- so attaching it would
// drive push-pull against the module's own driver, on a net HARDWARE_MATRIX
// records no series resistor on, for seven 1.2 s windows in which we do nothing
// but listen. Attaching it late is also the only order that works: `uart.c:854`
// -- "uart_release_pin(uart_num, (tx_io_num >= 0), (rx_io_num >= 0)," -- frees
// the old TX only when a new one is given, so a pin attached once here would
// stay driven through every later call that passes UART_PIN_NO_CHANGE, and
// through the pass-through after them.
esp_err_t open_port(const Wiring &w, int baud, bool attach_tx) {
  if (uart_is_driver_installed(kPort)) uart_driver_delete(kPort);

  uart_config_t cfg{};
  cfg.baud_rate = baud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_driver_install(kPort, kRxBuffer, 0, 0, nullptr, 0);
  if (err != ESP_OK) return err;
  err = uart_param_config(kPort, &cfg);
  if (err != ESP_OK) return err;
  return uart_set_pin(kPort, attach_tx ? w.tx : UART_PIN_NO_CHANGE, w.rx,
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// $BODY*CS\r\n. The checksum is computed rather than transcribed so a typo in a
// query string cannot turn into a silence that gets blamed on the module.
void send_nmea_query(const char *body) {
  std::uint8_t cs = 0;
  for (const char *p = body; *p != '\0'; ++p) cs ^= static_cast<std::uint8_t>(*p);
  char line[96];
  const int n = std::snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
  if (n > 0) uart_write_bytes(kPort, line, static_cast<std::size_t>(n));
}

// 32 bytes a line, hex beside text. A u-blox MON-VER reply is 40 bytes of
// software and hardware version followed by 30-byte extension fields, and it is
// the extensions that carry `MOD=`, the model string. Truncating at 48 bytes
// showed the ROM and stopped one field short of the part number.
void log_hex_and_text(const char *what, const char *data, int len, int received) {
  if (received > len) {
    ESP_LOGW(kTag, "  %s: %d bytes, TRUNCATED -- %d arrived, %d kept", what, len,
             received, len);
  } else {
    ESP_LOGI(kTag, "  %s: %d bytes", what, len);
  }
  for (int off = 0; off < len; off += 32) {
    char hex[3 * 32 + 1];
    char txt[32 + 1];
    const int show = (len - off < 32) ? len - off : 32;
    int h = 0;
    for (int i = 0; i < show; ++i) {
      h += std::snprintf(hex + h, sizeof(hex) - h, "%02X ",
                         static_cast<std::uint8_t>(data[off + i]));
      const char c = data[off + i];
      txt[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
    }
    txt[show] = '\0';
    ESP_LOGI(kTag, "  %04d | %-96s| %s", off, hex, txt);
  }
}

// Both parts answer a version poll with the same frame and the same Fletcher
// checksum; only the two sync bytes differ. u-blox is B5 62 (interface
// description UBX-21035062, UBX-MON-VER 0x0A 0x04); ALLYSTAR is F1 D9, and its
// binary protocol specification V2.3 gives this exact byte string as its own
// example, "Query firmware version F1 D9 0A 04 00 00 0E 34".
constexpr std::uint8_t kUbxMonVer[] = {0xB5, 0x62, 0x0A, 0x04,
                                       0x00, 0x00, 0x0E, 0x34};
constexpr std::uint8_t kAllystarMonVer[] = {0xF1, 0xD9, 0x0A, 0x04,
                                            0x00, 0x00, 0x0E, 0x34};

// Every distinct `$P` sentence in the window, once each. `sink` is NUL-
// terminated by `listen`, and scanning stops at `stored` so a truncated window
// cannot walk past what was actually copied.
void log_proprietary(const char *sink, int stored) {
  const char *p = sink;
  const char *end = sink + stored;
  while (p < end) {
    const char *dollar = static_cast<const char *>(
        std::memchr(p, '$', static_cast<std::size_t>(end - p)));
    if (dollar == nullptr) return;
    if (dollar + 2 < end && dollar[1] == 'P') {
      char line[96];
      std::size_t n = 0;
      while (dollar + n < end && n + 1 < sizeof(line) && dollar[n] != '\r' &&
             dollar[n] != '\n') {
        line[n] = dollar[n];
        ++n;
      }
      line[n] = '\0';
      ESP_LOGI(kTag, "  reply: %s", line);
    }
    p = dollar + 1;
  }
}

void interrogate() {
  static char sink[1536];

  struct Query {
    const char *name;
    const std::uint8_t *bytes;
    std::size_t len;
    const char *ascii;
  };
  const Query queries[] = {
      {"u-blox UBX-MON-VER", kUbxMonVer, sizeof(kUbxMonVer), nullptr},
      {"ALLYSTAR MON-VER", kAllystarMonVer, sizeof(kAllystarMonVer), nullptr},
      // $PUBX,00 is u-blox's *position* poll, not a version one. It earns its
      // place anyway: what identifies a receiver here is that it answers this
      // proprietary sentence at all, and it is read-only like the rest.
      {"u-blox $PUBX,00", nullptr, 0, "PUBX,00"},
      {"Quectel $PQTMVERNO", nullptr, 0, "PQTMVERNO"},
      {"MTK $PMTK605", nullptr, 0, "PMTK605"},
      {"CASIC $PCAS06,0", nullptr, 0, "PCAS06,0"},
  };

  for (const auto &q : queries) {
    uart_flush_input(kPort);
    if (q.bytes != nullptr) {
      uart_write_bytes(kPort, q.bytes, q.len);
    } else {
      send_nmea_query(q.ascii);
    }
    const Heard h = listen(1200, sink, sizeof(sink));
    // A periodic position stream answers every query by arriving. Report the
    // frame counts, which is what separates a reply from the traffic that was
    // going to be there anyway.
    ESP_LOGI(kTag, "%-22s -> %d bytes, %d NMEA, %d UBX, %d ALLYSTAR", q.name,
             h.bytes, h.nmea, h.ubx, h.allystar);
    // `stored`, never `bytes`: the sink is fixed and the window is not. A
    // 1.2 s window at 230400 -- which is where a sweep lands if the module
    // answers in neither NMEA nor a sync word this knows -- delivers tens of
    // kilobytes, and dumping `bytes` of a 1536-byte buffer would print
    // adjacent .bss under the heading "the module's reply". Fabricated
    // evidence is worse than no evidence, and this transcript is what the
    // read-off report quotes.
    if (h.ubx > 0 || h.allystar > 0) {
      log_hex_and_text(q.name, sink, h.stored, h.bytes);
    }
    // Four of the six questions are ASCII, and an ASCII answer carries neither
    // sync word -- so gating the dump on those two threw every one of them
    // away and left only a counter that a periodic stream makes non-zero
    // regardless. On a Quectel this instrument would have asked the one
    // question that identifies it and discarded the reply.
    //
    // A proprietary sentence is the signature: the standard stream is `$G..`,
    // and `$P..` is what a receiver answers a vendor query with. Printed
    // whole, because the answer is the whole line.
    log_proprietary(sink, h.stored);
  }
}

void report_sentences() {
  static char sink[2048];
  const Heard h = listen(3000, sink, sizeof(sink));
  ESP_LOGI(kTag, "3 s of traffic: %d bytes, %d sentences", h.bytes, h.nmea);

  // Every distinct five-character sentence id once, plus every TXT verbatim.
  // TXT is where a module names itself unprompted: today's bench read-off
  // identified an ALLYSTAR part from "$GNTXT,01,01,01,ANT_OK*50" alone.
  char seen[24][6];
  int seen_n = 0;
  char *line = sink;
  while (line != nullptr && *line != '\0') {
    char *end = std::strstr(line, "\r\n");
    if (end != nullptr) *end = '\0';
    if (line[0] == '$' && std::strlen(line) >= 6) {
      char id[6];
      std::memcpy(id, line + 1, 5);
      id[5] = '\0';
      bool known = false;
      for (int i = 0; i < seen_n; ++i) {
        if (std::strcmp(seen[i], id) == 0) known = true;
      }
      if (!known && seen_n < 24) {
        std::strcpy(seen[seen_n++], id);
        ESP_LOGI(kTag, "  sentence %s", id);
      }
      if (std::strstr(line, "TXT") != nullptr) ESP_LOGI(kTag, "  TXT: %s", line);
    }
    line = (end != nullptr) ? end + 2 : nullptr;
  }
}

#if CONFIG_ATTADIPA_GNSS_BRIDGE_PASSTHROUGH
// Hand the module's port to the host and get out of the way, so the probes
// written against the bench modules run against the watch unchanged.
//
// The console is USB Serial/JTAG on this board, which is the same peripheral
// this has to carry bytes over, so logging is turned off first: one stray
// ESP_LOGI in the middle of a UBX frame is a corrupt reply the host cannot tell
// from a corrupt module. Nothing after this point may log, and this call is
// last for that reason. Power-cycle to get the log stream back.
void pass_through(int baud) {
  ESP_LOGW(kTag, "PASS-THROUGH: the console now carries the module's port at");
  ESP_LOGW(kTag, "%d baud, 8N1, and logging stops here. Point a host probe at", baud);
  ESP_LOGW(kTag, "this device. Power-cycle the watch to get the log back.");
  vTaskDelay(pdMS_TO_TICKS(200));

  // ESP_ERR_INVALID_STATE means the console already installed it, which is the
  // common case and not a failure -- the read/write calls below work either way.
  usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  const esp_err_t err = usb_serial_jtag_driver_install(&usb);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "USB Serial/JTAG driver: %s", esp_err_to_name(err));
    return;
  }

  esp_log_level_set("*", ESP_LOG_NONE);
  std::uint8_t buf[256];
  for (;;) {
    const int from_host =
        usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(5));
    if (from_host > 0) {
      uart_write_bytes(kPort, buf, static_cast<std::size_t>(from_host));
    }
    const int from_module =
        uart_read_bytes(kPort, buf, sizeof(buf), pdMS_TO_TICKS(5));
    if (from_module > 0) {
      // portMAX_DELAY, not a timeout: with logging off there is nowhere to
      // report a short write, and a frame truncated in the middle is exactly
      // the corruption a host cannot tell from a broken module -- the same
      // thing this mode turns logging off to avoid. Blocking pushes back on
      // the module's UART instead, which loses nothing.
      usb_serial_jtag_write_bytes(buf, static_cast<std::size_t>(from_module),
                                  portMAX_DELAY);
    }
    // Neither read blocks when its side is empty, so with nothing moving this
    // is a spin at app_main's priority, which is above the idle task's. The
    // idle task then never runs and the task watchdog fires -- it did, at
    // task_wdt.c:436, about five seconds into the first pass-through run. One
    // tick of sleep on an idle pass is the whole fix; at 38400 baud it costs
    // nothing that matters.
    if (from_host == 0 && from_module == 0) vTaskDelay(1);
  }
}
#endif

}  // namespace

void run_gnss_bridge() {
  ESP_LOGI(kTag, "--- GNSS bring-up bridge (#436) ---------------------------");
  ESP_LOGI(kTag, "This asks the part what it is. It configures nothing and");
  ESP_LOGI(kTag, "saves nothing: every command below is a read-only poll.");

  const Wiring wirings[] = {
      {CONFIG_ATTADIPA_GNSS_BRIDGE_PIN_A, CONFIG_ATTADIPA_GNSS_BRIDGE_PIN_B,
       "A=listen"},
      {CONFIG_ATTADIPA_GNSS_BRIDGE_PIN_B, CONFIG_ATTADIPA_GNSS_BRIDGE_PIN_A,
       "B=listen"},
  };

  Wiring best{};
  int best_baud = 0;
  int best_score = 0;

  for (const auto &w : wirings) {
    for (const int baud : kBauds) {
      const esp_err_t err = open_port(w, baud, false);
      if (err != ESP_OK) {
        ESP_LOGE(kTag, "rx %d tx %d @ %d: %s", w.rx, w.tx, baud,
                 esp_err_to_name(err));
        continue;
      }
      const Heard h = listen(1200, nullptr, 0);
      // Frames only. A wrong baud is not silence: the UART frames the same
      // waveform on the wrong bit boundaries and delivers a byte for every
      // one, so the *most* bytes come from the most wrong speed. Counting them
      // picked 230400 over the 38400 that was carrying real NMEA. Bytes are
      // still logged, because they are how a live wire is told from a dead one.
      const int frames = h.nmea + h.ubx + h.allystar;
      ESP_LOGI(kTag, "rx %2d tx %2d @ %6d: %4d bytes, %2d NMEA, %d UBX, %d ALL",
               w.rx, w.tx, baud, h.bytes, h.nmea, h.ubx, h.allystar);
      if (frames > best_score) {
        best_score = frames;
        best_baud = baud;
        best = w;
      }
    }
  }

  if (best_baud == 0) {
    ESP_LOGW(kTag, "No frame on either orientation at any baud. If the byte");
    ESP_LOGW(kTag, "counts above were non-zero the wire is alive and the list");
    ESP_LOGW(kTag, "of speeds is short; if they were zero it is not.");
    ESP_LOGW(kTag, "That is this instrument's result, not the module's");
    ESP_LOGW(kTag, "property: BLDO1 is up and documented, but DC3 -- the rail");
    ESP_LOGW(kTag, "earlier revisions used -- is deliberately not written, and");
    ESP_LOGW(kTag, "an LS550G additionally needs DC4 at 850 mV to run at all.");
    ESP_LOGW(kTag, "Next step is one of those rails, with its encoding traced");
    ESP_LOGW(kTag, "first. See issue #436, and VERIFIED_FACTS.md under");
    ESP_LOGW(kTag, "\"The T-Watch GNSS module is a variant\".");
    return;
  }

  ESP_LOGI(kTag, "-----------------------------------------------------------");
  ESP_LOGI(kTag, "PORT FOUND: rx %d, tx %d, %d baud (%s)", best.rx, best.tx,
           best_baud, best.note);
  ESP_LOGI(kTag, "So GPIO %d is the module's TX and GPIO %d its RX.", best.rx,
           best.tx);
  // Now, and only now, this end starts transmitting: the pin that carries it
  // is settled, and everything before this point was listening.
  ESP_ERROR_CHECK_WITHOUT_ABORT(open_port(best, best_baud, true));

  report_sentences();
  ESP_LOGI(kTag, "--- who is there ------------------------------------------");
  interrogate();
  ESP_LOGI(kTag, "-----------------------------------------------------------");

  // A bounded window on the live port, not an endless one: app_main already
  // has the heartbeat, and what this adds is the one thing the queries above do
  // not answer — whether the module is tracking anything. A GGA line carries
  // its own fix quality and satellite count, so it is logged whole.
  static char sink[1024];
  for (int i = 0; i < 6; ++i) {
    const Heard h = listen(5000, sink, sizeof(sink));
    const char *gsv = std::strstr(sink, "GSV,");
    if (gsv != nullptr) {
      char line[96];
      std::size_t n = 0;
      while (n + 1 < sizeof(line) && gsv[n] != '\0' && gsv[n] != '\r') {
        line[n] = gsv[n];
        ++n;
      }
      line[n] = '\0';
      ESP_LOGI(kTag, "  $..%s", line);
    }
    const char *gga = std::strstr(sink, "GGA,");
    if (gga != nullptr) {
      char line[96];
      std::size_t n = 0;
      while (n + 1 < sizeof(line) && gga[n] != '\0' && gga[n] != '\r') {
        line[n] = gga[n];
        ++n;
      }
      line[n] = '\0';
      ESP_LOGI(kTag, "%d bytes, %d sentences in 5 s | $..%s", h.bytes, h.nmea,
               line);
    } else {
      ESP_LOGI(kTag, "%d bytes, %d sentences in 5 s | no GGA", h.bytes, h.nmea);
    }
  }
  ESP_LOGI(kTag, "--- bridge done -------------------------------------------");

#if CONFIG_ATTADIPA_GNSS_BRIDGE_PASSTHROUGH
  pass_through(best_baud);
#endif
}

}  // namespace attadipa::firmware
