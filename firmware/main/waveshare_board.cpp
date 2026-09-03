// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveshare_board.h"
#include "board_power.h"
#include "boot_rollback.h"

#include "pcf85063_time.h"
#include "provision_time.h"

#include <array>
#include <cinttypes>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
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
#include "attadipa/apps/provisioning.h"
#include "attadipa/core/time_service.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/clock_face.h"
#include "attadipa/ui/provision_face.h"
#include "attadipa_fonts.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "meshcore_ble.h"
#endif
#include "meshcore_passkey.h" // plain C++, no NimBLE behind it: every image

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
// One blob, not one key per field; `provision_time.h` says why. The two keys
// this replaced, `tz_min` and `last_utc`, are not read and not erased: only
// bench and HIL images ever wrote them, and their watches re-enter the time
// once rather than carry a migration forever.
constexpr char kTimeMetadataNvsKey[] = "meta";

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
  // Every handle boot creates, kept so that boot can un-create it. A null is
  // a step that did not run, and `abandon_board()` reads the journal from
  // these rather than from a stage number (#367 item 6).
  i2c_master_bus_handle_t i2c = nullptr;
  i2c_master_dev_handle_t pmu = nullptr;
  i2c_master_dev_handle_t rtc = nullptr;
  bool spi_bus_up = false;
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  bool lvgl_up = false;
  lv_timer_t *ui_timer = nullptr;  // create_ui()'s refresh tick
  esp_lcd_panel_io_handle_t touch_io = nullptr;
  esp_lcd_touch_handle_t touch = nullptr;
  lv_display_t *display = nullptr;
  attadipa::ui::ClockFace clock_face;
  attadipa::ui::ProvisionFace provision_face;
  // Present while the entry screen is up; a fresh one for each visit, so a
  // half-typed date from last time is not waiting on the next.
  std::optional<attadipa::apps::ProvisioningEntry> entry;
  // Ticks of `refresh_ui` the Done screen has been showing; the clock comes
  // back after kDoneTicks of them.
  unsigned done_ticks = 0;
  attadipa::core::TimeService time_service;
  // Default NVS, classified once at boot: ESP_OK, or the `nvs_flash_init()`
  // verdict that stands for the rest of this boot. Read by `BoardTimeOps`.
  esp_err_t metadata_storage = ESP_ERR_NVS_NOT_INITIALIZED;
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
  // The rail writes themselves live in board_power.cpp, unchanged. ADR-0016 §1
  // puts every AXP2101 enable and voltage write in one translation unit, and a
  // boot-time write is still a rail write; tools/flash/one_power_owner.py is
  // what turns that from a paragraph into a rule.
  return attadipa::firmware::board_power_bring_up_rails(state.pmu);
}

esp_err_t read_rtc(attadipa::firmware::RtcDateTime *time,
                   attadipa::firmware::RtcDecodeStatus *status) {
  constexpr std::uint8_t kSecondsRegister = 0x04;
  std::uint8_t raw[7]{};
  if (state.rtc == nullptr) {
    return ESP_ERR_INVALID_STATE;  // boot could not add it; the clock is unavailable
  }
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
  if (state.rtc == nullptr) {
    return ESP_ERR_INVALID_STATE;
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

// The one reader of `attadipa_time`, for boot and for provisioning. A blob of
// the wrong size is reported as absent, not as an error: it is a schema this
// image does not know, the next synchronization overwrites it, and refusing
// to synchronize over it would leave the watch stuck with a clock it cannot
// set.
esp_err_t read_time_metadata(attadipa::firmware::TimeMetadata *out,
                             bool *present) {
  *out = {};
  *present = false;
  nvs_handle_t handle{};
  const esp_err_t opened = nvs_open(kTimeNvsNamespace, NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(opened, kTag, "open time metadata");
  attadipa::firmware::TimeMetadataBytes bytes{};
  std::size_t size = bytes.size();
  const esp_err_t err =
      nvs_get_blob(handle, kTimeMetadataNvsKey, bytes.data(), &size);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  if (err == ESP_ERR_NVS_INVALID_LENGTH || (err == ESP_OK && size != bytes.size())) {
    ESP_LOGW(kTag, "time metadata has an unknown size; ignored");
    *out = {};
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(err, kTag, "read time metadata");
  *out = attadipa::firmware::decode_time_metadata(bytes);
  *present = true;
  return ESP_OK;
}

// Initialises default NVS once and keeps the verdict, then puts the stored
// UTC offset back into the time service. Runs in the product image; a torn
// write cannot reach here, because a single `nvs_set_blob` lands whole or
// reads as absent (`provision_time.h`).
//
// A verdict other than ESP_OK is logged here, once, and then answered from
// `BoardTimeOps::read_metadata` for every synchronization of this boot, so
// the RTC is never written over metadata that cannot be stored. Nothing here
// erases NVS: ESP_ERR_NVS_NO_FREE_PAGES and ESP_ERR_NVS_NEW_VERSION_FOUND are
// the two verdicts ESP-IDF answers with "erase the partition and try again",
// and that erase takes the BLE bonds and the MeshCore pin with the time
// metadata. It is a factory reset a person performs, not a boot path
// (ADR-0014).
esp_err_t restore_time_metadata() {
  state.metadata_storage = nvs_flash_init();
  if (state.metadata_storage != ESP_OK) {
    const bool factory_reset =
        state.metadata_storage == ESP_ERR_NVS_NO_FREE_PAGES ||
        state.metadata_storage == ESP_ERR_NVS_NEW_VERSION_FOUND;
    ESP_LOGW(kTag,
             "time metadata storage unavailable (%s): the clock runs without "
             "it and no synchronization will write the RTC%s",
             esp_err_to_name(state.metadata_storage),
             factory_reset ? "; factory reset required, this image never "
                             "erases NVS on its own"
                           : "");
    return state.metadata_storage;
  }
  attadipa::firmware::TimeMetadata stored{};
  bool present = false;
  ESP_RETURN_ON_ERROR(read_time_metadata(&stored, &present), kTag,
                      "restore time metadata");
  if (!present) {
    return ESP_OK;
  }
  ESP_RETURN_ON_FALSE(
      state.time_service.set_provisional_timezone(stored.offset_minutes),
      ESP_ERR_INVALID_STATE, kTag, "stored timezone metadata is invalid");
  ESP_LOGI(kTag, "restored provisional UTC offset %+d minutes",
           stored.offset_minutes);
  return ESP_OK;
}

esp_err_t save_time_metadata(const attadipa::firmware::TimeMetadata &metadata) {
  nvs_handle_t handle{};
  ESP_RETURN_ON_ERROR(nvs_open(kTimeNvsNamespace, NVS_READWRITE, &handle),
                      kTag, "open time metadata for write");
  const auto bytes = attadipa::firmware::encode_time_metadata(metadata);
  esp_err_t err =
      nvs_set_blob(handle, kTimeMetadataNvsKey, bytes.data(), bytes.size());
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

// A key that is not there is the outcome asked for, not a failure.
esp_err_t erase_time_metadata() {
  nvs_handle_t handle{};
  const esp_err_t opened = nvs_open(kTimeNvsNamespace, NVS_READWRITE, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(opened, kTag, "open time metadata to erase");
  esp_err_t err = nvs_erase_key(handle, kTimeMetadataNvsKey);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    err = ESP_OK;
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

// The PCF85063 half of a synchronization: write, read back, and check that
// what came back is the UTC that was asked for. Its three failures are one
// answer to the caller because the caller does the same thing for each --
// roll the metadata back and report Failed.
bool write_and_verify_rtc(const attadipa::firmware::RtcDateTime &rtc,
                          std::int64_t utc_seconds) {
  const esp_err_t write_result = write_rtc(rtc);
  if (write_result != ESP_OK) {
    ESP_LOGW(kTag, "write PCF85063 time: %s", esp_err_to_name(write_result));
    return false;
  }
  attadipa::firmware::RtcDateTime verified{};
  attadipa::firmware::RtcDecodeStatus status{};
  const esp_err_t read_result = read_rtc(&verified, &status);
  if (read_result != ESP_OK) {
    ESP_LOGW(kTag, "read PCF85063 after write failed: %s",
             esp_err_to_name(read_result));
    return false;
  }
  attadipa::core::WallTime verified_utc;
  if (status != attadipa::firmware::RtcDecodeStatus::Valid ||
      !wall_time_from_rtc(verified, verified_utc) ||
      attadipa::core::seconds_between(attadipa::core::WallTime{utc_seconds},
                                      verified_utc) > 1) {
    ESP_LOGW(kTag, "PCF85063 readback did not match synchronized UTC");
    return false;
  }
  return true;
}

// This board's storage and chip, as `provision_time()` sees them. The
// sequence -- what is written, in which order, and what is put back -- is in
// `provision_time.h` and tested there; this is only the wiring.
struct BoardTimeOps {
  attadipa::firmware::MetadataRead
  read_metadata(attadipa::firmware::TimeMetadata *out) {
    if (state.metadata_storage != ESP_OK) {  // boot's verdict: not opened, so not read
      return attadipa::firmware::MetadataRead::Unreadable;
    }
    bool present = false;
    if (read_time_metadata(out, &present) != ESP_OK) {
      return attadipa::firmware::MetadataRead::Unreadable;
    }
    return present ? attadipa::firmware::MetadataRead::Present
                   : attadipa::firmware::MetadataRead::Absent;
  }
  bool save_metadata(const attadipa::firmware::TimeMetadata &metadata) {
    const esp_err_t err = save_time_metadata(metadata);
    if (err != ESP_OK) {
      ESP_LOGW(kTag, "persist time metadata failed: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
  }
  bool erase_metadata() {
    const esp_err_t err = erase_time_metadata();
    if (err != ESP_OK) {
      ESP_LOGE(kTag,
               "could not roll back time metadata: %s -- NVS may hold a UTC "
               "offset for a synchronization that did not happen",
               esp_err_to_name(err));
    }
    return err == ESP_OK;
  }
  bool write_and_verify_rtc(const attadipa::firmware::RtcDateTime &rtc,
                            std::int64_t utc_seconds) {
    return ::write_and_verify_rtc(rtc, utc_seconds);
  }
};

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

// What the entry screen hands the board, in every image. The same sequence
// the HIL sink runs, with the entry's own terms: a person typing the time is
// trusted for a day and is allowed to move the clock by any amount, because
// the alternative is a watch that refuses the first time it is ever set.
class BoardProvisioner final : public attadipa::core::Provisioner {
public:
  attadipa::core::ProvisionOutcome
  set_wall_clock(const attadipa::core::WallClockEntry &entry) override {
    const attadipa::core::MonotonicTime now{
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
    BoardTimeOps ops;
    const attadipa::firmware::TimeProvisionRequest request{
        entry.utc_seconds, entry.timezone_offset_minutes, kManualValidForMs,
        true};
    switch (attadipa::firmware::provision_time(ops, request,
                                               state.time_service, now)) {
    case attadipa::firmware::ProvisionTimeResult::Rejected:
      return attadipa::core::ProvisionOutcome::Rejected;
    case attadipa::firmware::ProvisionTimeResult::Failed:
      return attadipa::core::ProvisionOutcome::Failed;
    case attadipa::firmware::ProvisionTimeResult::Accepted:
      break;
    }
    ESP_LOGI(kTag, "PCF85063 set from the watch");
    return attadipa::core::ProvisionOutcome::Accepted;
  }

  // Only a pairing passkey, and the rule is the one `meshcore_passkey.h`
  // states rather than a copy of it: 000000 is not a pin the node could
  // show, it is what the firmware reads as "do not pair" -- the unpaired
  // probe the HIL opcode below accepts on purpose and never stores. A
  // product screen must not be able to start an unpaired scan, so it is
  // refused here, and the hint says "check it" because six zeros typed on a
  // watch are a typo before they are anything else.
  attadipa::core::ProvisionOutcome
  set_mesh_passkey(std::uint32_t passkey) override {
    if (!attadipa::firmware::is_pairing_passkey(passkey)) {
      return attadipa::core::ProvisionOutcome::Rejected;
    }
    // The same NVS the clock refused on: a passkey the worker cannot store
    // would arm one scan and be gone at the next boot, so it is refused
    // before it is posted, as #405 does for the RTC one field earlier.
    if (state.metadata_storage != ESP_OK) {
      return attadipa::core::ProvisionOutcome::Failed;
    }
#if CONFIG_BT_NIMBLE_ENABLED
    // Queued, and that is all this says. The passkey is armed and written on
    // the radio worker, and `mesh_passkey_outcome()` below is where that
    // finishes -- #416, where this returned `Accepted` for a post and the
    // screen printed "the watch is set up" over a stack that had not been
    // asked yet.
    if (meshcore_ble_configure_passkey(passkey, passkey_ticket_) != ESP_OK) {
      return attadipa::core::ProvisionOutcome::Failed;
    }
    return attadipa::core::ProvisionOutcome::Pending;
#else
    return attadipa::core::ProvisionOutcome::Failed;
#endif
  }

  attadipa::core::ProvisionOutcome mesh_passkey_outcome() override {
#if CONFIG_BT_NIMBLE_ENABLED
    switch (meshcore_ble_passkey_outcome(passkey_ticket_)) {
    case attadipa::firmware::PasskeyOutcome::InFlight:
      return attadipa::core::ProvisionOutcome::Pending;
    case attadipa::firmware::PasskeyOutcome::Armed:
      passkey_ticket_ = 0;
      return attadipa::core::ProvisionOutcome::Accepted;
    default:
      // Refused, NotStored, and Idle with them. Idle means the answer was
      // already taken or the ticket is not this slot's any more, so there is
      // no operation left to wait for; reporting that as Pending would hold
      // the screen open for an answer that is never coming.
      passkey_ticket_ = 0;
      return attadipa::core::ProvisionOutcome::Failed;
    }
#else
    return attadipa::core::ProvisionOutcome::Failed;
#endif
  }

  bool mesh_node(attadipa::core::MeshPeerId &out) override {
#if CONFIG_BT_NIMBLE_ENABLED
    const attadipa::core::MeshStatus status = meshcore_ble_status();
    if (!status.has_pinned) {
      return false;
    }
    out = status.pinned_id;
    return true;
#else
    (void)out;
    return false;
#endif
  }

  // Queued, and that is all this says, as the passkey above: the clears run
  // on the radio worker and `mesh_forget_outcome()` is where they finish.
  attadipa::core::ProvisionOutcome forget_mesh_node() override {
#if CONFIG_BT_NIMBLE_ENABLED
    switch (meshcore_ble_forget_node(forget_ticket_)) {
    case ESP_OK:
      return attadipa::core::ProvisionOutcome::Pending;
    case ESP_ERR_INVALID_STATE:
      return attadipa::core::ProvisionOutcome::Rejected;
    default:
      return attadipa::core::ProvisionOutcome::Failed;
    }
#else
    return attadipa::core::ProvisionOutcome::Failed;
#endif
  }

  attadipa::core::MeshForgetOutcome mesh_forget_outcome() override {
#if CONFIG_BT_NIMBLE_ENABLED
    using attadipa::firmware::ForgetNodeOutcome;
    const ForgetNodeOutcome outcome =
        meshcore_ble_forget_node_outcome(forget_ticket_);
    if (outcome != ForgetNodeOutcome::InFlight) {
      forget_ticket_ = 0;
    }
    switch (outcome) {
    case ForgetNodeOutcome::InFlight:
      return attadipa::core::MeshForgetOutcome::Pending;
    case ForgetNodeOutcome::Forgotten:
      return attadipa::core::MeshForgetOutcome::Forgotten;
    case ForgetNodeOutcome::Unpinned:
      return attadipa::core::MeshForgetOutcome::Unpinned;
    case ForgetNodeOutcome::PinOnFlash:
      return attadipa::core::MeshForgetOutcome::PinOnFlash;
    case ForgetNodeOutcome::Nothing:
      return attadipa::core::MeshForgetOutcome::Nothing;
    case ForgetNodeOutcome::ReplayInhibited:
      return attadipa::core::MeshForgetOutcome::ReplayInhibited;
    case ForgetNodeOutcome::BondKept:
    case ForgetNodeOutcome::Idle:
      // Idle: the answer was already taken, or the ticket is not the
      // slot's any more. Nothing of this screen's is outstanding, and
      // "nothing changed" is the one answer that cannot be a stale success.
      break;
    }
    return attadipa::core::MeshForgetOutcome::BondKept;
#else
    return attadipa::core::MeshForgetOutcome::BondKept;
#endif
  }

private:
  // A day. The RTC keeps counting after that; what expires is the trust in
  // the offset a person typed, the same way it would for a host's.
  static constexpr std::uint32_t kManualValidForMs = 24U * 60U * 60U * 1000U;

  // Not atomic and does not need to be: both halves of a passkey request are
  // made by the entry screen, which is the LVGL task. The worker's half of the
  // handover is the slot in `meshcore_passkey_outcome.h`, which is.
  std::uint32_t passkey_ticket_ = 0;
  std::uint32_t forget_ticket_ = 0;
};

BoardProvisioner provisioner;

#if CONFIG_ATTADIPA_WATCH_CONTROL
class BoardTimeSink final : public attadipa::debug::TimeSink {
public:
  attadipa::debug::TimeSinkResult
  synchronize(const attadipa::debug::TimeSyncBody &request) override {
    const attadipa::core::MonotonicTime now{
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
    BoardTimeOps ops;
    const attadipa::firmware::TimeProvisionRequest provision{
        request.utc_seconds, request.timezone_offset_minutes,
        request.valid_for_ms,
        (request.flags & attadipa::debug::kTimeSyncAllowLargeCorrection) != 0};
    switch (attadipa::firmware::provision_time(ops, provision,
                                               state.time_service, now)) {
    case attadipa::firmware::ProvisionTimeResult::Rejected:
      return attadipa::debug::TimeSinkResult::Rejected;
    case attadipa::firmware::ProvisionTimeResult::Failed:
      return attadipa::debug::TimeSinkResult::Failed;
    case attadipa::firmware::ProvisionTimeResult::Accepted:
      break;
    }
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
      // Accepted, not done. The bond is deleted on the mesh worker and the
      // answer comes back through forget_bond_outcome() below (#378).
      return attadipa::debug::MeshSinkResult::Pending;
    case ESP_ERR_INVALID_STATE:
      return attadipa::debug::MeshSinkResult::Rejected;
    case ESP_ERR_NOT_FINISHED:
      // The worker still holds the slot from an earlier request. The bridge
      // refuses a second forget-bond before it reaches here, so this is the
      // case where the host that asked for the first one went away while the
      // deletion was still running -- busy, not failed, and the operator is
      // told to wait rather than told a deletion was refused.
      return attadipa::debug::MeshSinkResult::Busy;
    default:
      return attadipa::debug::MeshSinkResult::Failed;
    }
  }

  attadipa::debug::MeshSinkResult forget_bond_outcome() override {
    switch (meshcore_ble_forget_bond_outcome()) {
    case attadipa::firmware::ForgetOutcome::Deleted:
      return attadipa::debug::MeshSinkResult::Accepted;
    case attadipa::firmware::ForgetOutcome::InFlight:
      return attadipa::debug::MeshSinkResult::Pending;
    case attadipa::firmware::ForgetOutcome::Nothing:
      // Not a failed deletion: the conflict record was gone before the worker
      // looked, so nothing was deleted and nothing is left behind. Same code
      // as the synchronous empty record above, because it is the same answer.
      return attadipa::debug::MeshSinkResult::Rejected;
    default:
      // Refused, and Idle with it. Idle means the answer was already consumed
      // or the slot was given back, and there is no operation left to wait
      // for; reporting that as Pending would hold the request open for an
      // answer that is never coming, and there is no deadline behind it.
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

// A boot without the touch controller (#367 item 6) shows a correct clock
// that ignores every finger, and the long press that opens the entry screen
// is gone with it; the face says so.
attadipa::apps::ClockText clock_text() {
  attadipa::apps::ClockState clock = read_clock_state();
  clock.touch_absent = state.touch == nullptr;
  return attadipa::apps::format_clock(clock, false);
}

void refresh_clock(lv_timer_t *timer) {
  state.clock_face.update(clock_text());
  if (timer != nullptr) {
    lv_timer_set_period(timer,
                        attadipa::apps::clock_manifest().tick_period.value);
  }
}

#if CONFIG_BT_NIMBLE_ENABLED
// Four bytes of a node's public key as hex. The bench reports identify nodes by
// exactly this much (`5c62d9bc…`, `044e2de8…`), and eight characters is what
// fits beside a name.
void key_prefix(const attadipa::core::MeshPeerId &id, char (&out)[9]) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 4; ++i) {
    out[i * 2] = kHex[id.public_key[i] >> 4U];
    out[i * 2 + 1] = kHex[id.public_key[i] & 0x0FU];
  }
  out[8] = '\0';
}

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
  // The key prefix shares the "Node:" line rather than taking one of its own:
  // the label wraps, the name below it can already be two lines, and there are
  // 90 px to the message label under it. A name is not an identity -- the two
  // bench nodes were `Beta test companion` and a name differing by an emoji,
  // and an operator reading a screenshot could not say which mesh a run was on
  // (docs/research/MESHCORE_T114_FIRST_CONTACT.md:54 "There are two MeshCore
  // nodes in range"). Four bytes is what the bench reports already identify
  // nodes by.
  char node_key[11] = {};
  if (status.has_node_id) {
    char hex[9];
    key_prefix(status.node_id, hex);
    std::snprintf(node_key, sizeof(node_key), " %s", hex);
  }
  // A REFUSAL IS THE ONE THING ON THIS SCREEN THAT IS NOT SESSION STATE, and it
  // is here because a refused watch has no session: the state line says
  // SCANNING, the name is blank, and without this the screen is silent about
  // the one fact that explains all of it. Since #411 the recovery is the
  // entry screen's node field, a long press on the clock away, and this line
  // is how an operator learns that there is something to go there for.
  //
  // Third line, and the label has room for it: line_height is 22 px
  // (assets/fonts/generated/attadipa_nunito_sans_20.c:2892 ".line_height = 22,")
  // and there are 90 px from this label's y=135 to mesh_message's y=225, so
  // four lines (88 px) fit and five do not. Worst case is exactly four: the key
  // line, a 32-byte name wrapping to two on a 354 px label, and this. This line
  // cannot itself wrap -- its content is fixed, two 8-character prefixes, and
  // it measures 331.8 px against the 354 px label (COMPUTED from the font's own
  // `adv_w` table, not measured on glass). NOT EXECUTED -- HARDWARE REQUIRED:
  // `sim/` has a boot screen and a diagnostic screen and no mesh screen, so
  // there is no screenshot of this to look at that would be this screen.
  char refused[40] = {};
  if (status.has_refused && status.has_pinned) {
    char bad[9];
    char want[9];
    key_prefix(status.refused_id, bad);
    key_prefix(status.pinned_id, want);
    std::snprintf(refused, sizeof(refused), "\nrefused %s, pinned %s", bad,
                  want);
  }
  lv_label_set_text_fmt(state.mesh_node, "Node:%s\n%s%s", node_key,
                        status.node_name[0] != '\0' ? status.node_name.data()
                                                     : "—",
                        refused);
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

void build_clock_screen() {
  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  state.clock_face.build(lv_screen_active(),
                         {kWidth, kHeight, attadipa::ui::Theme::Night,
                          attadipa::ui::PixelCost::PerPixel,
                          attadipa::ui::Metrics::for_dpi(
                              profile != nullptr ? profile->display.dpi() : 0)},
                         clock_text());
}

constexpr unsigned kDoneTicks = 3;

// A long press on the clock opens the entry screen. It is on the screen
// object, which both faces share, so it needs adding once; the clock face
// leaves its children unclickable and the press lands here, while the
// keypad's buttons take theirs and never let one through.
void long_press(lv_event_t *) {
  if (state.entry.has_value() || state.mesh_screen) {
    return;
  }
  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  state.clock_face.clear();
  state.entry.emplace(provisioner);
  state.done_ticks = 0;
  state.provision_face.build(
      lv_screen_active(),
      {kWidth, kHeight, attadipa::ui::Theme::Night,
       attadipa::ui::PixelCost::PerPixel,
       attadipa::ui::Metrics::for_dpi(profile != nullptr ? profile->display.dpi()
                                                          : 0),
       attadipa::l10n::Locale::En},
      *state.entry);
}

void refresh_ui(lv_timer_t *timer) {
#if CONFIG_BT_NIMBLE_ENABLED
  if (mesh_screen_requested.load()) {
    // The mesh screen cleans the LVGL screen under whatever is on it. An
    // entry in progress goes with its objects, or it would pin every later
    // long press behind a face that no longer exists.
    state.provision_face.clear();
    state.entry.reset();
    refresh_mesh();
    if (timer != nullptr) {
      lv_timer_set_period(timer, 500);
    }
    return;
  }
#endif
  if (state.entry.has_value()) {
    // The radio's half of a passkey arrives here, on the tick, and not on a
    // key press -- so this is the only thing that can move the screen off
    // "still setting up the node" (#416). One second is the clock's tick and
    // therefore the longest a person waits for an answer the worker usually
    // has in milliseconds.
    if (state.entry->poll()) {
      state.provision_face.update();
    }
    if (!state.entry->finished() || ++state.done_ticks < kDoneTicks) {
      return;
    }
    state.provision_face.clear();
    state.entry.reset();
    build_clock_screen();
  }
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
  state.spi_bus_up = true;

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.cs_gpio_num = kLcdCs;
  io_config.dc_gpio_num = -1;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &state.panel_io), kTag,
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
      esp_lcd_new_panel_co5300(state.panel_io, &panel_config, &state.panel), kTag,
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
  state.lvgl_up = true;

  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  ESP_RETURN_ON_FALSE(profile != nullptr, ESP_ERR_NOT_FOUND, kTag,
                      "board profile missing");

  lvgl_port_display_cfg_t display{};
  display.io_handle = state.panel_io;
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

  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_i2c(state.i2c, &io_config, &state.touch_io), kTag,
      "create touch IO");

  esp_lcd_touch_config_t touch_config{};
  touch_config.x_max = kWidth - 1;
  touch_config.y_max = kHeight - 1;
  touch_config.rst_gpio_num = kTouchReset;
  touch_config.int_gpio_num = kTouchInterrupt;
  touch_config.levels.reset = 0;
  touch_config.levels.interrupt = 0;

  ESP_RETURN_ON_ERROR(
      esp_lcd_touch_new_i2c_ft5x06(state.touch_io, &touch_config, &state.touch), kTag,
      "initialize FT3168 via FT5x06 driver");
  ESP_LOGI(kTag, "FT3168: I2C 0x38, reset GPIO 9, interrupt GPIO 38");
  return ESP_OK;
}

void create_ui() {
  build_clock_screen();
  lv_obj_add_event_cb(lv_screen_active(), long_press, LV_EVENT_LONG_PRESSED,
                      nullptr);
  state.ui_timer = lv_timer_create(
      refresh_ui, attadipa::apps::clock_manifest().tick_period.value, nullptr);
}

// One teardown step: issue it, keep the first failure, and null the handle
// either way -- a handle whose delete failed is not one anybody may use again.
template <typename Handle, typename Undo>
void undo(Handle &handle, esp_err_t &first_failure, const char *what,
          Undo undo_step) {
  if (handle == nullptr) {
    return;
  }
  const esp_err_t err = undo_step(handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "boot rollback: %s failed: %s", what, esp_err_to_name(err));
    if (first_failure == ESP_OK) {
      first_failure = err;
    }
  }
  handle = nullptr;
}

// Undo every boot step that succeeded, in reverse, so a boot that fails at
// step n does not leave n-1 subsystems up and unreported (POWER_OWNERSHIP §2.5,
// #367 item 6). The journal is the handles above: null means the step never
// ran. Which steps a registered display forbids, and why, is `boot_rollback.h`.
//
// The one thing rollback cannot undo is the rails: `board_power_bring_up_rails()`
// wrote them, and switching any of them off is authorised by a measurement
// nobody has made (ADR-0016 Consequences; ALDO2 is a pull-up, not a supply).
// They stay as written, and the log says so, because "the PMU is programmed
// and nothing else is" is a known state and a dark board is not.
//
// `abandon_board()` returns the first teardown failure. A failed step is logged
// and skipped, not retried: the board is then in a state nobody read back, and
// the honest report to the caller is that error, not ESP_OK.
// The steps, as the board can undo them. The decision that orders them --
// what a registered display forbids freeing -- is `boot_rollback.h`, shared
// with the T-Watch and pinned by `tests/test_boot_rollback.cpp` off a board.
struct WaveshareRollbackOps {
  esp_err_t first_failure = ESP_OK;

  bool display_registered() const { return state.display != nullptr; }

  void retain_display_stack() {
    ESP_LOGE(kTag, "boot rollback: display transfer is not proven idle; LVGL, "
                   "the panel and QSPI are left in place rather than freed");
  }
  void stop_lvgl() {
    if (state.lvgl_up) {
      (void)lvgl_port_deinit();  // always ESP_OK: a request, not a result
      state.lvgl_up = false;
    }
  }
  void remove_panel() {
    undo(state.panel, first_failure, "delete panel", esp_lcd_panel_del);
  }
  void remove_panel_io() {
    undo(state.panel_io, first_failure, "delete panel IO", esp_lcd_panel_io_del);
  }
  void free_spi() {
    if (!state.spi_bus_up) {
      return;
    }
    const esp_err_t err = spi_bus_free(SPI2_HOST);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "boot rollback: free QSPI failed: %s", esp_err_to_name(err));
      if (first_failure == ESP_OK) {
        first_failure = err;
      }
    }
    state.spi_bus_up = false;
  }
  void detach_power_owner() { attadipa::firmware::board_power_detach(); }
  void remove_touch() {
    undo(state.touch, first_failure, "delete touch", esp_lcd_touch_del);
  }
  void remove_touch_io() {
    undo(state.touch_io, first_failure, "delete touch IO", esp_lcd_panel_io_del);
  }
  void remove_rtc() {
    undo(state.rtc, first_failure, "remove PCF85063", i2c_master_bus_rm_device);
  }
  void remove_pmu() {
    if (state.pmu != nullptr) {
      ESP_LOGW(kTag, "boot rollback: AXP2101 rails stay as written -- no "
                     "measurement authorises switching one off (ADR-0016)");
    }
    undo(state.pmu, first_failure, "remove AXP2101", i2c_master_bus_rm_device);
  }
  void remove_main_bus() {
    undo(state.i2c, first_failure, "delete I2C bus", i2c_del_master_bus);
  }
};

esp_err_t abandon_board() {
  WaveshareRollbackOps ops;
  attadipa::firmware::rollback_boot_retaining_display(ops);
  return ops.first_failure;
}

// A required step failed: roll back, and report the step's error -- a rollback
// that itself failed is already in the log, and the caller cannot act on two.
esp_err_t abandon_board_after(esp_err_t err, const char *step) {
  ESP_LOGE(kTag, "%s failed: %s; rolling the boot back", step,
           esp_err_to_name(err));
  (void)abandon_board();
  return err;
}

} // namespace

// Boot is a transaction. Three steps are required -- the bus, the rails and
// the display -- and a failure in any of them rolls back the ones before it
// and returns that error. The rest is reported, never fatal: a watch with a
// dead RTC shows an unavailable clock, and a watch with dead touch still shows
// the clock and answers its buttons, which is more use than a dark one
// (TWATCH_S3_PLUS_BSP_REUSE §10, rules 3 and 6).
esp_err_t start_waveshare_ui() {
  esp_err_t err = initialize_i2c();
  if (err != ESP_OK) {
    return abandon_board_after(err, "initialize I2C");
  }
  err = initialize_pmu();
  if (err != ESP_OK) {
    return abandon_board_after(err, "initialize AXP2101");
  }
  err = add_i2c_device(kPcf85063Address, &state.rtc);
  if (err != ESP_OK) {
    state.rtc = nullptr;
    ESP_LOGW(kTag, "PCF85063 not added (%s): the clock is unavailable and "
                   "nothing will set it this boot",
             esp_err_to_name(err));
  }
  // Every failure inside says so itself, and the clock runs without the
  // metadata either way.
  (void)restore_time_metadata();
  const attadipa::apps::ClockState clock = read_clock_state();
  ESP_LOGI(kTag, "PCF85063: %s",
           clock.availability == attadipa::core::Availability::Ready
               ? "ready"
               : "unavailable");
  err = initialize_display();
  if (err != ESP_OK) {
    return abandon_board_after(err, "initialize display");
  }
  err = initialize_touch();
  if (err != ESP_OK) {
    // A half-made touch is torn down here rather than kept as a journal
    // entry, so the rest of boot sees one thing: no touch.
    esp_err_t ignored = ESP_OK;
    undo(state.touch, ignored, "delete touch", esp_lcd_touch_del);
    undo(state.touch_io, ignored, "delete touch IO", esp_lcd_panel_io_del);
    ESP_LOGW(kTag, "FT3168 not started (%s): no touch this boot; the buttons "
                   "and the clock still work",
             esp_err_to_name(err));
  }

  if (!lvgl_port_lock(1000)) {
    return abandon_board_after(ESP_ERR_TIMEOUT, "lock LVGL");
  }
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
  if (physical_result != ESP_OK) {
    // Still under the LVGL lock: disarm both callbacks create_ui() installed
    // before rollback removes the RTC and I2C handles they can reach.
    lv_obj_remove_event_cb(lv_screen_active(), long_press);
    if (state.ui_timer != nullptr) {
      lv_timer_delete(state.ui_timer);
      state.ui_timer = nullptr;
    }
  }
  lvgl_port_unlock();
  if (physical_result != ESP_OK) {
    // create_ui() may already have queued a DMA-backed flush, and the LVGL
    // mutex above does not prove its callback completed. Keep the whole display
    // stack because the public port has no bounded drain operation.
    return abandon_board_after(physical_result, "start physical input");
  }
#if CONFIG_ATTADIPA_WATCH_CONTROL
  if (watch_control_result != ESP_OK) {
    // The bench endpoint is the HIL image's reason to exist, but not the
    // watch's: without it this boot is a product image, and the bench tool
    // says "no watch found" rather than the wearer seeing nothing.
    ESP_LOGE(kTag, "watch-control endpoint not started (%s): this boot "
                   "answers no cable",
             esp_err_to_name(watch_control_result));
  }
#endif

  // Past here the input service is running and has no stop, so a panel that
  // stops answering now is reported, not rolled back: the owner's sleep path
  // is what talks to it next, and ADR-0016 §4 is what it does with a refusal.
  err = esp_lcd_panel_disp_on_off(state.panel, true);
  if (err == ESP_OK) {
    err = esp_lcd_panel_co5300_set_brightness(state.panel, kBrightnessPercent);
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "CO5300 did not turn on (%s): the UI runs on a dark panel",
             esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(kTag, "UI ready: AMOLED brightness %d%%, touch %s, RTC %s",
           kBrightnessPercent, state.touch != nullptr ? "present" : "absent",
           state.rtc != nullptr ? "present" : "absent");
  return ESP_OK;
}
