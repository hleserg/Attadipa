// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveshare_board.h"

#include "pcf85063_time.h"

#include <array>
#include <cinttypes>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "attadipa/apps/clock.h"
#include "attadipa/core/time_service.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/clock_face.h"
#include "attadipa_fonts.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "meshcore_ble.h"
#endif

#include "physical_input.h"

#if CONFIG_ATTADIPA_WATCH_CONTROL
#include "attadipa/debug/bridge.h"
#include "watch_control.h"
#endif

namespace {

constexpr char kTag[] = "waveshare";
constexpr char kBoardProfileId[] = "waveshare-amoled-206";

constexpr int kWidth = 410;
constexpr int kHeight = 502;
constexpr int kPanelGapX = 0x16;
constexpr int kBrightnessPercent = 5;

constexpr gpio_num_t kLcdCs = GPIO_NUM_12;
constexpr gpio_num_t kLcdClock = GPIO_NUM_11;
constexpr gpio_num_t kLcdData0 = GPIO_NUM_4;
constexpr gpio_num_t kLcdData1 = GPIO_NUM_5;
constexpr gpio_num_t kLcdData2 = GPIO_NUM_6;
constexpr gpio_num_t kLcdData3 = GPIO_NUM_7;
constexpr gpio_num_t kLcdReset = GPIO_NUM_8;
constexpr gpio_num_t kTouchReset = GPIO_NUM_9;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_38;
constexpr gpio_num_t kI2cSda = GPIO_NUM_15;
constexpr gpio_num_t kI2cScl = GPIO_NUM_14;

constexpr std::uint8_t kAxp2101Address = 0x34;
constexpr std::uint8_t kPcf85063Address = 0x51;
constexpr char kTimeNvsNamespace[] = "attadipa_time";
constexpr char kTimezoneNvsKey[] = "tz_min";
constexpr char kLastSyncNvsKey[] = "last_utc";

constexpr std::uint8_t kC4[] = {0x80};
constexpr std::uint8_t kTearingLine[] = {0x01, 0xD1};
constexpr std::uint8_t kTearingOn[] = {0x00};
constexpr std::uint8_t kWriteControl[] = {0x20};
constexpr std::uint8_t kUnknown63[] = {0xFF};
constexpr std::uint8_t kBrightnessOff[] = {0x00};
constexpr std::uint8_t kColumns[] = {0x00, 0x16, 0x01, 0xAF};
constexpr std::uint8_t kRows[] = {0x00, 0x00, 0x01, 0xF5};

// The exact panel sequence used by the known-working vendor implementation,
// except display-on is delayed until the black UI objects exist. Brightness
// starts at zero and is raised to the measured 5% visible floor only after
// that.
constexpr co5300_lcd_init_cmd_t kPanelInit[] = {
    {0x11, nullptr, 0, 120},
    {0xC4, kC4, sizeof(kC4), 0},
    {0x44, kTearingLine, sizeof(kTearingLine), 0},
    {0x35, kTearingOn, sizeof(kTearingOn), 0},
    {0x53, kWriteControl, sizeof(kWriteControl), 10},
    {0x63, kUnknown63, sizeof(kUnknown63), 10},
    {0x51, kBrightnessOff, sizeof(kBrightnessOff), 10},
    {0x2A, kColumns, sizeof(kColumns), 0},
    {0x2B, kRows, sizeof(kRows), 0},
};

struct BoardState {
  i2c_master_bus_handle_t i2c = nullptr;
  i2c_master_dev_handle_t pmu = nullptr;
  i2c_master_dev_handle_t rtc = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  esp_lcd_touch_handle_t touch = nullptr;
  lv_display_t *display = nullptr;
  attadipa::ui::ClockFace clock_face;
  attadipa::core::TimeService time_service;
  lv_obj_t *mesh_state = nullptr;
  lv_obj_t *mesh_node = nullptr;
  lv_obj_t *mesh_message = nullptr;
  lv_obj_t *mesh_signal = nullptr;
  bool mesh_screen = false;
};

BoardState state;
#if CONFIG_BT_NIMBLE_ENABLED
std::atomic_bool mesh_screen_requested{false};
#endif

esp_err_t add_i2c_device(std::uint8_t address, i2c_master_dev_handle_t *out) {
  i2c_device_config_t config{};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = 400000;
  return i2c_master_bus_add_device(state.i2c, &config, out);
}

esp_err_t write_reg(i2c_master_dev_handle_t device, std::uint8_t reg,
                    std::uint8_t value) {
  const std::uint8_t bytes[] = {reg, value};
  return i2c_master_transmit(device, bytes, sizeof(bytes), 100);
}

esp_err_t read_reg(i2c_master_dev_handle_t device, std::uint8_t reg,
                   std::uint8_t *value) {
  return i2c_master_transmit_receive(device, &reg, 1, value, 1, 100);
}

esp_err_t initialize_i2c() {
  i2c_master_bus_config_t config{};
  config.i2c_port = I2C_NUM_0;
  config.sda_io_num = kI2cSda;
  config.scl_io_num = kI2cScl;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &state.i2c), kTag,
                      "initialize I2C bus");
  return i2c_master_bus_reset(state.i2c);
}

esp_err_t initialize_pmu() {
  ESP_RETURN_ON_ERROR(add_i2c_device(kAxp2101Address, &state.pmu), kTag,
                      "add AXP2101");

  // Preserve unrelated rails. The known-working board implementation needs
  // DC1 plus ALDO1/2, so own only those outputs instead of blanking the PMU.
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x82, 0x12), kTag, "DC1 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x92, 0x1C), kTag, "ALDO1 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x93, 0x1C), kTag, "ALDO2 3.3 V");

  std::uint8_t dcdc = 0;
  std::uint8_t aldo = 0;
  ESP_RETURN_ON_ERROR(read_reg(state.pmu, 0x80, &dcdc), kTag,
                      "read DC enables");
  ESP_RETURN_ON_ERROR(read_reg(state.pmu, 0x90, &aldo), kTag,
                      "read LDO enables");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x80, dcdc | 0x01), kTag,
                      "enable DC1");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x90, aldo | 0x03), kTag,
                      "enable ALDO1/2");

  ESP_LOGI(kTag, "AXP2101: DC enable 0x%02x, LDO enable 0x%02x", dcdc | 0x01,
           aldo | 0x03);
  return ESP_OK;
}

esp_err_t read_rtc(attadipa::firmware::RtcDateTime *time,
                   attadipa::firmware::RtcDecodeStatus *status) {
  constexpr std::uint8_t kSecondsRegister = 0x04;
  std::uint8_t raw[7]{};
  esp_err_t err = i2c_master_transmit_receive(state.rtc, &kSecondsRegister, 1,
                                              raw, sizeof(raw), 100);
  if (err != ESP_OK) {
    return err;
  }
  *status = attadipa::firmware::decode_pcf85063(raw, *time);
  return ESP_OK;
}

esp_err_t write_rtc(const attadipa::firmware::RtcDateTime &time) {
  std::uint8_t raw[7]{};
  if (!attadipa::firmware::encode_pcf85063(time, raw)) {
    return ESP_ERR_INVALID_ARG;
  }
  std::uint8_t request[8] = {0x04};
  for (std::size_t i = 0; i < sizeof(raw); ++i) {
    request[i + 1] = raw[i];
  }
  // NXP PCF85063A Rev. 7.3 section 7.4 requires seconds through years in one
  // access shorter than one second; splitting time and date can corrupt them.
  return i2c_master_transmit(state.rtc, request, sizeof(request), 100);
}

bool wall_time_from_rtc(const attadipa::firmware::RtcDateTime &rtc,
                        attadipa::core::WallTime &out) {
  return attadipa::apps::wall_time_from_civil(
      {static_cast<std::int64_t>(rtc.year), rtc.month, rtc.day, rtc.weekday,
       rtc.hour, rtc.minute, rtc.second},
      out);
}

esp_err_t restore_time_metadata() {
  ESP_RETURN_ON_ERROR(nvs_flash_init(), kTag, "initialize time metadata");
  nvs_handle_t handle{};
  esp_err_t err = nvs_open(kTimeNvsNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(err, kTag, "open time metadata");
  std::int16_t offset = 0;
  err = nvs_get_i16(handle, kTimezoneNvsKey, &offset);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(err, kTag, "read timezone metadata");
  ESP_RETURN_ON_FALSE(state.time_service.set_provisional_timezone(offset),
                      ESP_ERR_INVALID_STATE, kTag,
                      "stored timezone metadata is invalid");
  ESP_LOGI(kTag, "restored provisional UTC offset %+d minutes", offset);
  return ESP_OK;
}

#if CONFIG_ATTADIPA_WATCH_CONTROL
esp_err_t save_time_metadata(std::int16_t offset, std::int64_t last_sync_utc) {
  nvs_handle_t handle{};
  ESP_RETURN_ON_ERROR(
      nvs_open(kTimeNvsNamespace, NVS_READWRITE, &handle), kTag,
      "open time metadata for write");
  esp_err_t err = nvs_set_i16(handle, kTimezoneNvsKey, offset);
  if (err == ESP_OK) {
    err = nvs_set_i64(handle, kLastSyncNvsKey, last_sync_utc);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}
#endif

attadipa::apps::ClockState read_clock_state() {
  attadipa::apps::ClockState clock;
  clock.locale = attadipa::l10n::Locale::En;
  const attadipa::core::MonotonicTime now{
      static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
  attadipa::firmware::RtcDateTime rtc{};
  attadipa::firmware::RtcDecodeStatus status{};
  const esp_err_t err = read_rtc(&rtc, &status);
  if (err != ESP_OK) {
    state.time_service.report(attadipa::core::TimeSource::Rtc,
                              attadipa::core::Availability::Unreachable,
                              attadipa::core::Validity::Invalid);
  } else if (status == attadipa::firmware::RtcDecodeStatus::VoltageLow) {
    state.time_service.report(attadipa::core::TimeSource::Rtc,
                              attadipa::core::Availability::Unprovisioned,
                              attadipa::core::Validity::Unknown);
  } else if (status == attadipa::firmware::RtcDecodeStatus::InvalidData) {
    state.time_service.report(attadipa::core::TimeSource::Rtc,
                              attadipa::core::Availability::Failed,
                              attadipa::core::Validity::Invalid);
  } else {
    attadipa::core::WallTime utc;
    if (!wall_time_from_rtc(rtc, utc)) {
      state.time_service.report(attadipa::core::TimeSource::Rtc,
                                attadipa::core::Availability::Failed,
                                attadipa::core::Validity::Invalid);
    } else {
      (void)state.time_service.observe(
          {utc, now, {}, 0, attadipa::core::TimeSource::Rtc,
           attadipa::core::TimeQuality::Provisional, false});
    }
  }
  const attadipa::core::TimeState time = state.time_service.state(now);
  clock.availability = time.availability;
  clock.time = time.local;
  return clock;
}

#if CONFIG_ATTADIPA_WATCH_CONTROL
class BoardTimeSink final : public attadipa::debug::TimeSink {
public:
  attadipa::debug::TimeSinkResult
  synchronize(const attadipa::debug::TimeSyncBody &request) override {
    const attadipa::core::MonotonicTime now{
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
    if (request.valid_for_ms == 0 ||
        now.ms > std::numeric_limits<std::uint64_t>::max() -
                     request.valid_for_ms) {
      return attadipa::debug::TimeSinkResult::Rejected;
    }

    attadipa::apps::CivilTime civil;
    if (!attadipa::apps::civil_from_wall_time(
            attadipa::core::WallTime{request.utc_seconds}, civil) ||
        civil.year < 2000 || civil.year > 2099) {
      return attadipa::debug::TimeSinkResult::Rejected;
    }
    const attadipa::firmware::RtcDateTime rtc{
        static_cast<unsigned>(civil.year), civil.month, civil.day,
        civil.weekday, civil.hour, civil.minute, civil.second};

    attadipa::core::TimeService candidate = state.time_service;
    const attadipa::core::MonotonicTime valid_until{
        now.ms + request.valid_for_ms};
    if (!candidate.set_timezone(request.timezone_offset_minutes, valid_until,
                                now) ||
        !candidate.observe(
            {attadipa::core::WallTime{request.utc_seconds}, now,
             attadipa::core::Millis{request.valid_for_ms}, 0,
             attadipa::core::TimeSource::Manual,
             attadipa::core::TimeQuality::Trusted,
             (request.flags &
              attadipa::debug::kTimeSyncAllowLargeCorrection) != 0})) {
      return attadipa::debug::TimeSinkResult::Rejected;
    }

    const esp_err_t write_result = write_rtc(rtc);
    if (write_result != ESP_OK) {
      ESP_LOGW(kTag, "write PCF85063 time: %s",
               esp_err_to_name(write_result));
      return attadipa::debug::TimeSinkResult::Failed;
    }
    attadipa::firmware::RtcDateTime verified{};
    attadipa::firmware::RtcDecodeStatus status{};
    const esp_err_t read_result = read_rtc(&verified, &status);
    attadipa::core::WallTime verified_utc;
    if (read_result != ESP_OK) {
      ESP_LOGW(kTag, "read PCF85063 after write failed: %s",
               esp_err_to_name(read_result));
      return attadipa::debug::TimeSinkResult::Failed;
    }
    if (status != attadipa::firmware::RtcDecodeStatus::Valid ||
        !wall_time_from_rtc(verified, verified_utc) ||
        attadipa::core::seconds_between(
            attadipa::core::WallTime{request.utc_seconds}, verified_utc) > 1) {
      ESP_LOGW(kTag, "PCF85063 readback did not match synchronized UTC");
      return attadipa::debug::TimeSinkResult::Failed;
    }

    const esp_err_t metadata_result = save_time_metadata(
        request.timezone_offset_minutes, request.utc_seconds);
    if (metadata_result != ESP_OK) {
      ESP_LOGW(kTag, "persist time metadata failed: %s",
               esp_err_to_name(metadata_result));
      return attadipa::debug::TimeSinkResult::Failed;
    }

    state.time_service = candidate;
    ESP_LOGI(kTag, "PCF85063 synchronized from host");
    return attadipa::debug::TimeSinkResult::Accepted;
  }
};

BoardTimeSink time_sink;

#if CONFIG_BT_NIMBLE_ENABLED
class BoardMeshSink final : public attadipa::debug::MeshSink {
public:
  attadipa::debug::MeshSinkResult configure(std::uint32_t passkey) override {
    if (passkey > 999999) {
      return attadipa::debug::MeshSinkResult::Rejected;
    }
    if (!configure_meshcore_ble(passkey)) {
      return attadipa::debug::MeshSinkResult::Failed;
    }
    mesh_screen_requested.store(true);
    return attadipa::debug::MeshSinkResult::Accepted;
  }

  attadipa::debug::MeshSinkResult disconnect() override {
    return stop_meshcore_ble() ? attadipa::debug::MeshSinkResult::Accepted
                               : attadipa::debug::MeshSinkResult::Failed;
  }

  attadipa::debug::MeshSinkResult forget_bond() override {
    // Rejected only for the empty record -- that is a statement about the
    // request. A full event queue is the transport failing, and reporting it
    // as an invalid request would send the operator looking for a conflict
    // that is recorded.
    switch (meshcore_ble_forget_bond()) {
    case ESP_OK:
      return attadipa::debug::MeshSinkResult::Accepted;
    case ESP_ERR_INVALID_STATE:
      return attadipa::debug::MeshSinkResult::Rejected;
    default:
      return attadipa::debug::MeshSinkResult::Failed;
    }
  }

  attadipa::debug::MeshSinkResult
  send(const std::uint8_t peer_prefix[6], const char *text,
       std::size_t text_length, std::int64_t utc_seconds) override {
    if (peer_prefix == nullptr || text == nullptr || text_length == 0 ||
        text_length > 160 ||
        std::memchr(text, '\0', text_length) != nullptr || utc_seconds < 0 ||
        utc_seconds > std::numeric_limits<std::uint32_t>::max()) {
      return attadipa::debug::MeshSinkResult::Rejected;
    }
    std::array<std::uint8_t, 6> prefix{};
    std::memcpy(prefix.data(), peer_prefix, prefix.size());
    return meshcore_ble_send(prefix, std::string_view(text, text_length),
                             attadipa::core::WallTime{utc_seconds})
               ? attadipa::debug::MeshSinkResult::Accepted
               : attadipa::debug::MeshSinkResult::Failed;
  }

  attadipa::debug::MeshSinkResult
  send_room(const std::uint8_t room[32], const char *password,
            std::size_t password_length, const char *text,
            std::size_t text_length, std::int64_t utc_seconds) override {
    if (room == nullptr || password == nullptr || text == nullptr ||
        password_length == 0 || password_length > 15 || text_length == 0 ||
        text_length > attadipa::core::kMeshTextBytes ||
        std::memchr(password, '\0', password_length) != nullptr ||
        std::memchr(text, '\0', text_length) != nullptr || utc_seconds < 0 ||
        utc_seconds > std::numeric_limits<std::uint32_t>::max()) {
      return attadipa::debug::MeshSinkResult::Rejected;
    }
    std::array<std::uint8_t, attadipa::core::kMeshPublicKeyBytes> key{};
    std::memcpy(key.data(), room, key.size());
    return meshcore_ble_send_room(key, std::string_view(password, password_length),
                                  std::string_view(text, text_length),
                                  attadipa::core::WallTime{utc_seconds})
               ? attadipa::debug::MeshSinkResult::Accepted
               : attadipa::debug::MeshSinkResult::Failed;
  }
};

BoardMeshSink mesh_sink;
#endif
#endif

void round_flush_area(lv_area_t *area) {
  area->x1 &= ~1;
  area->x2 |= 1;
}

void refresh_clock(lv_timer_t *timer) {
  const attadipa::apps::ClockState clock = read_clock_state();
  state.clock_face.update(attadipa::apps::format_clock(clock, false));
  if (timer != nullptr) {
    lv_timer_set_period(timer,
                        attadipa::apps::clock_manifest().tick_period.value);
  }
}

#if CONFIG_BT_NIMBLE_ENABLED
void build_mesh_screen() {
  lv_obj_t *screen = lv_screen_active();
  state.clock_face.clear();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x05080B), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(screen, &attadipa_nunito_sans_20, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "MESH");
  lv_obj_set_style_text_font(title, &attadipa_nunito_sans_28, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x8CE8C2), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 26);

  state.mesh_state = lv_label_create(screen);
  lv_obj_set_style_text_font(state.mesh_state, &attadipa_nunito_sans_28, 0);
  lv_obj_set_style_text_color(state.mesh_state, lv_color_hex(0xF4F7F5), 0);
  lv_obj_align(state.mesh_state, LV_ALIGN_TOP_LEFT, 28, 70);

  state.mesh_node = lv_label_create(screen);
  lv_obj_set_width(state.mesh_node, kWidth - 56);
  lv_label_set_long_mode(state.mesh_node, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(state.mesh_node, lv_color_hex(0xBBC6C1), 0);
  lv_obj_align(state.mesh_node, LV_ALIGN_TOP_LEFT, 28, 135);

  state.mesh_message = lv_label_create(screen);
  lv_obj_set_width(state.mesh_message, kWidth - 56);
  lv_label_set_long_mode(state.mesh_message, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(state.mesh_message, lv_color_hex(0xF4F7F5), 0);
  lv_obj_align(state.mesh_message, LV_ALIGN_TOP_LEFT, 28, 225);

  state.mesh_signal = lv_label_create(screen);
  lv_obj_set_style_text_color(state.mesh_signal, lv_color_hex(0x8CE8C2), 0);
  lv_obj_align(state.mesh_signal, LV_ALIGN_BOTTOM_LEFT, 28, -28);
  state.mesh_screen = true;
}

void refresh_mesh() {
  if (!state.mesh_screen) {
    build_mesh_screen();
  }
  const attadipa::core::MeshStatus status = meshcore_ble_status();
  lv_label_set_text(state.mesh_state,
                    status.availability == attadipa::core::Availability::Unprovisioned
                        ? "STOPPED"
                        : status.transport == attadipa::core::TransportPhase::Ready
                              ? "CONNECTED"
                              : attadipa::core::to_string(status.transport));
  lv_label_set_text_fmt(state.mesh_node, "Node:\n%s",
                        status.node_name[0] != '\0' ? status.node_name.data()
                                                     : "—");
  // Sender, text and delivery state are the three things a screenshot has to
  // carry to be evidence of a message going out or coming in, so they share one
  // label rather than one each.
  lv_label_set_text_fmt(state.mesh_message, "Last message:\n%s%s%s\nSent: %s",
                        status.last_sender[0] != '\0' ? status.last_sender.data()
                                                      : "",
                        status.last_sender[0] != '\0' ? ": " : "",
                        status.last_message[0] != '\0'
                            ? status.last_message.data()
                            : "—",
                        attadipa::core::to_string(status.delivery));
  if (status.has_snr) {
    const int magnitude = status.snr_quarter_db < 0
                              ? -static_cast<int>(status.snr_quarter_db)
                              : static_cast<int>(status.snr_quarter_db);
    lv_label_set_text_fmt(state.mesh_signal, "SNR: %s%d.%02d dB   Peers: %u",
                          status.snr_quarter_db < 0 ? "-" : "",
                          magnitude / 4, (magnitude % 4) * 25,
                          static_cast<unsigned>(status.peers_reported));
  } else {
    lv_label_set_text_fmt(state.mesh_signal, "SNR: —   Peers: %u   MTU: %u",
                          static_cast<unsigned>(status.peers_reported),
                          static_cast<unsigned>(status.mtu));
  }
}
#endif

void refresh_ui(lv_timer_t *timer) {
#if CONFIG_BT_NIMBLE_ENABLED
  if (mesh_screen_requested.load()) {
    refresh_mesh();
    if (timer != nullptr) {
      lv_timer_set_period(timer, 500);
    }
    return;
  }
#endif
  refresh_clock(timer);
}

esp_err_t initialize_display() {
  spi_bus_config_t bus{};
  bus.sclk_io_num = kLcdClock;
  bus.data0_io_num = kLcdData0;
  bus.data1_io_num = kLcdData1;
  bus.data2_io_num = kLcdData2;
  bus.data3_io_num = kLcdData3;
  bus.max_transfer_sz = kWidth * 20 * sizeof(std::uint16_t);
  bus.flags = SPICOMMON_BUSFLAG_QUAD;
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                      kTag, "initialize QSPI");

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.cs_gpio_num = kLcdCs;
  io_config.dc_gpio_num = -1;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io), kTag,
      "create panel IO");

  co5300_vendor_config_t vendor{};
  vendor.init_cmds = kPanelInit;
  vendor.init_cmds_size = sizeof(kPanelInit) / sizeof(kPanelInit[0]);
  vendor.flags.use_qspi_interface = true;

  esp_lcd_panel_dev_config_t panel_config{};
  panel_config.reset_gpio_num = kLcdReset;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  panel_config.vendor_config = &vendor;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_co5300(panel_io, &panel_config, &state.panel), kTag,
      "create CO5300 panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(state.panel), kTag, "reset CO5300");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(state.panel), kTag,
                      "initialize CO5300");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(state.panel, kPanelGapX, 0), kTag,
                      "set panel gap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(state.panel, 0), kTag,
                      "hold AMOLED dark");

  lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
  port.task_priority = 1;
  port.task_affinity = 1;
  ESP_RETURN_ON_ERROR(lvgl_port_init(&port), kTag, "initialize LVGL");

  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  ESP_RETURN_ON_FALSE(profile != nullptr, ESP_ERR_NOT_FOUND, kTag,
                      "board profile missing");

  lvgl_port_display_cfg_t display{};
  display.io_handle = panel_io;
  display.panel_handle = state.panel;
  display.buffer_size = kWidth * 20;
  display.hres = kWidth;
  display.vres = kHeight;
  display.color_format = LV_COLOR_FORMAT_RGB565;
  display.rounder_cb = round_flush_area;
  display.flags.buff_dma = true;
  display.flags.swap_bytes = profile->display.rgb565_swap_bytes;
  state.display = lvgl_port_add_disp(&display);
  ESP_RETURN_ON_FALSE(state.display != nullptr, ESP_ERR_NO_MEM, kTag,
                      "add LVGL display");

  ESP_LOGI(kTag, "CO5300: QSPI 40 MHz, RGB565 swap %s, brightness pending",
           profile->display.rgb565_swap_bytes ? "yes" : "no");
  return ESP_OK;
}

esp_err_t initialize_touch() {
  esp_lcd_panel_io_i2c_config_t io_config{};
  io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
  io_config.control_phase_bytes = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.scl_speed_hz = 400000;
  io_config.flags.disable_control_phase = true;

  esp_lcd_panel_io_handle_t touch_io = nullptr;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_i2c(state.i2c, &io_config, &touch_io), kTag,
      "create touch IO");

  esp_lcd_touch_config_t touch_config{};
  touch_config.x_max = kWidth - 1;
  touch_config.y_max = kHeight - 1;
  touch_config.rst_gpio_num = kTouchReset;
  touch_config.int_gpio_num = kTouchInterrupt;
  touch_config.levels.reset = 0;
  touch_config.levels.interrupt = 0;

  ESP_RETURN_ON_ERROR(
      esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch_config, &state.touch), kTag,
      "initialize FT3168 via FT5x06 driver");
  ESP_LOGI(kTag, "FT3168: I2C 0x38, reset GPIO 9, interrupt GPIO 38");
  return ESP_OK;
}

void create_ui() {
  const attadipa::apps::ClockState clock = read_clock_state();
  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  state.clock_face.build(lv_screen_active(),
                         {kWidth, kHeight, attadipa::ui::Theme::Night,
                          attadipa::ui::PixelCost::PerPixel,
                          attadipa::ui::Metrics::for_dpi(
                              profile != nullptr ? profile->display.dpi() : 0)},
                         attadipa::apps::format_clock(clock, false));
  lv_timer_create(refresh_ui,
                  attadipa::apps::clock_manifest().tick_period.value, nullptr);
}

} // namespace

esp_err_t start_waveshare_ui() {
  ESP_RETURN_ON_ERROR(initialize_i2c(), kTag, "initialize I2C");
  ESP_RETURN_ON_ERROR(initialize_pmu(), kTag, "initialize AXP2101");
  ESP_RETURN_ON_ERROR(add_i2c_device(kPcf85063Address, &state.rtc), kTag,
                      "add PCF85063");
  const esp_err_t metadata_result = restore_time_metadata();
  if (metadata_result != ESP_OK) {
    ESP_LOGW(kTag, "time metadata unavailable; continuing without it: %s",
             esp_err_to_name(metadata_result));
  }
  const attadipa::apps::ClockState clock = read_clock_state();
  ESP_LOGI(kTag, "PCF85063: %s",
           clock.availability == attadipa::core::Availability::Ready
               ? "ready"
               : "unavailable");
  ESP_RETURN_ON_ERROR(initialize_display(), kTag, "initialize display");
  ESP_RETURN_ON_ERROR(initialize_touch(), kTag, "initialize touch");

  ESP_RETURN_ON_FALSE(lvgl_port_lock(1000), ESP_ERR_TIMEOUT, kTag, "lock LVGL");
  create_ui();
  // Which of the two images this is, said out loud once per boot. The endpoint
  // is unauthenticated by construction, so an operator holding a board needs to
  // be able to tell from the log alone whether the thing in their hand will
  // answer a stranger's cable (#346).
#if CONFIG_ATTADIPA_WATCH_CONTROL
  ESP_LOGW(kTag, "HIL image: USB watch-control endpoint ENABLED and "
                 "unauthenticated -- bench use only, not a product image");
#else
  ESP_LOGI(kTag, "production image: no USB watch-control endpoint");
#endif

  // Physical first, and unconditionally: it owns the input queue the optional
  // transport below writes into, and it is what makes touch, the buttons and
  // sleep work in a production image that has no transport at all (#346).
  const esp_err_t physical_result =
      start_physical_input(state.touch, state.pmu, state.panel,
                           kBrightnessPercent, [] { refresh_ui(nullptr); });
#if CONFIG_ATTADIPA_WATCH_CONTROL
  const esp_err_t watch_control_result =
      physical_result != ESP_OK ? ESP_OK
                                : start_watch_control(&time_sink,
#if CONFIG_BT_NIMBLE_ENABLED
                                                      &mesh_sink
#else
                                                      nullptr
#endif
                                  );
#endif
  lvgl_port_unlock();
  ESP_RETURN_ON_ERROR(physical_result, kTag, "start physical input");
#if CONFIG_ATTADIPA_WATCH_CONTROL
  ESP_RETURN_ON_ERROR(watch_control_result, kTag, "start watch control");
#endif

  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(state.panel, true), kTag,
                      "turn display on");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_co5300_set_brightness(state.panel, kBrightnessPercent),
      kTag, "set safe brightness");
  ESP_LOGI(kTag, "UI ready: AMOLED brightness %d%%", kBrightnessPercent);
  return ESP_OK;
}
