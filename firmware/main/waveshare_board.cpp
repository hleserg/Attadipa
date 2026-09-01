// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waveshare_board.h"

#include "board_power.h"
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

// Reads the stored UTC offset back at boot. This one is **outside**
// CONFIG_ATTADIPA_WATCH_CONTROL and runs in the product image, which is the
// whole reason the key it reads is the one that matters.
//
// An interrupted write is **not** caught here. It is prevented by the write
// order below: the timezone key is written last and erased first, so whatever
// this reads is a value somebody accepted, whole. Requiring both keys cannot
// catch a torn write and never could -- a watch that has synchronized once
// already has `last_sync_utc` on flash, so both keys are present no matter
// where the interruption landed. That was round 3's finding.
//
// It requires both keys anyway, for the one store the ordering does not cover:
// a bench image from before this fix wrote the timezone first, so a first-ever
// synchronization cut between the two sets left a timezone with no stamp behind
// it. Rejecting that pair is what gets such a watch back without another bench
// image. `last_sync_utc` is read and thrown away -- nothing in the tree
// consumes it -- and its presence is the whole of what is being asked.
esp_err_t restore_time_metadata() {
  ESP_RETURN_ON_ERROR(nvs_flash_init(), kTag, "initialize time metadata");
  nvs_handle_t handle{};
  esp_err_t err = nvs_open(kTimeNvsNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(err, kTag, "open time metadata");
  std::int16_t offset = 0;
  std::int64_t last_sync_utc = 0;
  err = nvs_get_i16(handle, kTimezoneNvsKey, &offset);
  if (err == ESP_OK) {
    err = nvs_get_i64(handle, kLastSyncNvsKey, &last_sync_utc);
  }
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
// What `attadipa_time` held before a synchronization started, and whether that
// was a *complete* stored state.
//
// One flag rather than one per key -- but not because the two keys move
// together, which they do not. A successful `nvs_set_*` is already on flash --
// in ESP-IDF v5.5.5, nvs_commit() in components/nvs_flash/src/nvs_api.cpp is
// commented "no-op for now, to be used when intermediate cache is added" and
// NVSHandleSimple::commit() returns ESP_OK without touching storage -- so a
// `save_time_metadata()` that fails between its two sets leaves `tz_min`
// behind without `last_utc`. A version bump re-reads that. The flag means
// complete, and a
// half-written store is deliberately not complete: restoring it would put back
// a UTC offset from a synchronization that failed, which is the one thing this
// type exists to prevent. So `present == false` is restored by erasing both
// keys, and it covers "there was nothing" and "there was half of something"
// with the same answer, on purpose.
struct TimeMetadata {
  std::int16_t offset;
  std::int64_t last_sync_utc;
  bool present;
};

esp_err_t read_time_metadata(TimeMetadata *out) {
  *out = TimeMetadata{};
  nvs_handle_t handle{};
  const esp_err_t open_err =
      nvs_open(kTimeNvsNamespace, NVS_READONLY, &handle);
  if (open_err == ESP_ERR_NVS_NOT_FOUND) {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(open_err, kTag, "open time metadata to read");
  esp_err_t err = nvs_get_i16(handle, kTimezoneNvsKey, &out->offset);
  if (err == ESP_OK) {
    err = nvs_get_i64(handle, kLastSyncNvsKey, &out->last_sync_utc);
  }
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // Either key missing means there is no complete state to restore. Clear
    // what the first read may already have filled in, so that a half-read
    // `offset` never travels beside `present == false`.
    *out = TimeMetadata{};
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(err, kTag, "read time metadata to read");
  out->present = true;
  return ESP_OK;
}

esp_err_t erase_if_present(nvs_handle_t handle, const char *key) {
  const esp_err_t err = nvs_erase_key(handle, key);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

// **The order of the two writes is the mechanism, not a detail.** The timezone
// key is the one the product image reads, so it is written **last** here and
// erased **first** in the roll-back below. A power loss cannot be reported, the
// caller never returns and no roll-back can run, so within this function the
// only thing protecting the product image is that at every instant the key it
// reads holds the last value anybody accepted. Writing it first -- which this
// did until round 3 of #396 -- left a watch that had synchronized before with
// the new offset beside the old stamp: two present keys, a read that accepts
// them, and an offset from a synchronization that never reached the PCF85063.
//
// **Within this function is the whole of the claim.** `synchronize()` calls
// this before it writes the chip, so between the two there is a window in which
// the key holds an offset for a synchronization the PCF85063 has not taken --
// and a cut there is no more reportable than a cut in here. That window is
// named and priced where it is opened; do not read this paragraph as covering
// it.
//
// `nvs_commit()` does not make the pair a transaction and nothing here pretends
// it does: it is a no-op on ESP-IDF v5.5.5 and a successful `nvs_set_*` is
// already on flash. See the note on `TimeMetadata` above.
esp_err_t save_time_metadata(std::int16_t offset, std::int64_t last_sync_utc) {
  nvs_handle_t handle{};
  ESP_RETURN_ON_ERROR(
      nvs_open(kTimeNvsNamespace, NVS_READWRITE, &handle), kTag,
      "open time metadata for write");
  esp_err_t err = nvs_set_i64(handle, kLastSyncNvsKey, last_sync_utc);
  if (err == ESP_OK) {
    err = nvs_set_i16(handle, kTimezoneNvsKey, offset);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

// Puts `previous` back after a refused write, chip or metadata. No complete
// state means erasing rather than writing, which is what the flag on
// `TimeMetadata` is for -- and either key may already be absent, because
// `previous` can predate any synchronization at all and because the store can
// be half-written. `ESP_ERR_NVS_NOT_FOUND` from an erase is therefore the
// outcome asked for and not a failure; treating it as one used to abandon the
// second erase and report an error for a store that had ended up correct.
esp_err_t roll_back_time_metadata(const TimeMetadata &previous) {
  if (previous.present) {
    return save_time_metadata(previous.offset, previous.last_sync_utc);
  }
  nvs_handle_t handle{};
  ESP_RETURN_ON_ERROR(nvs_open(kTimeNvsNamespace, NVS_READWRITE, &handle),
                      kTag, "open time metadata to roll back");
  esp_err_t err = erase_if_present(handle, kTimezoneNvsKey);
  if (err == ESP_OK) {
    err = erase_if_present(handle, kLastSyncNvsKey);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}

// Rolls back, and says so when even that fails. Two callers -- a refused
// metadata write and a refused chip write -- and a second copy of this log
// line is how they end up saying different things about the same state.
void roll_back_or_log(const TimeMetadata &previous) {
  const esp_err_t rollback = roll_back_time_metadata(previous);
  if (rollback != ESP_OK) {
    ESP_LOGE(kTag,
             "could not roll back time metadata: %s -- NVS may hold a UTC "
             "offset for a synchronization that did not happen",
             esp_err_to_name(rollback));
  }
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

    // The metadata write goes first, and it IS the fail-closed check ahead of
    // the RTC write. #264's ledger names the shape: boot may already know the
    // metadata layer is unusable -- `firmware/main/waveshare_board.cpp:941` --
    // "time metadata unavailable; continuing without it" logs it and carries
    // on -- and this function used to write and verify the PCF85063 anyway,
    // discovering only afterwards that nvs_open() could not succeed. That is a
    // repeatable partial update of physical hardware, with state.time_service
    // left holding the old observation and no recovery path. Ordering the
    // metadata first also covers a write failure boot cannot predict, which a
    // boot-time readiness flag would not.
    //
    // Ordering alone only moves the partial update, which is what round 1 of
    // this pull request's review said and the first version of this comment
    // got wrong. It called the residue inert because nothing reads the
    // last-sync stamp. That is true of `last_utc` and false of `tz_min`:
    // `firmware/main/waveshare_board.cpp:217` --
    // "err = nvs_get_i16(handle, kTimezoneNvsKey, &offset);" -- puts it back
    // into the time service on every boot, so a refused RTC write would have
    // left the watch displaying an offset it never accepted, for good. Hence
    // `previous`: the metadata is restored on every path out of here that does
    // not reach `state.time_service = candidate`, which is what makes the
    // sequence all-or-nothing rather than differently partial.
    //
    // That includes a refused metadata write, which the first version of this
    // comment claimed it had covered and did not. It is not only a wrong
    // sentence: `save_time_metadata()` has four exits and two of them happen
    // after the first `nvs_set` has already returned ESP_OK, and on the pinned
    // ESP-IDF that set is already on flash -- see the note on `TimeMetadata`,
    // which carries the source. So that exit leaves a `tz_min` the wearer
    // never accepted, exactly as the RTC path did.
    TimeMetadata previous{};
    const esp_err_t previous_result = read_time_metadata(&previous);
    if (previous_result != ESP_OK) {
      ESP_LOGW(kTag, "read time metadata before write failed: %s",
               esp_err_to_name(previous_result));
      return attadipa::debug::TimeSinkResult::Failed;
    }

    // Metadata first, chip second, and the order is a **trade rather than a
    // win** -- there is no ordering of a flash key and an I2C chip with no
    // window between them, so the only choice is which residue a power cut
    // leaves.
    //
    // This way round, the write that can be predicted to fail is the one that
    // runs first, which is what makes the sequence fail-closed and is the whole
    // of #264's second defect: a doomed `nvs_open()` used to be discovered only
    // after the PCF85063 had already been rewritten. The cost is the residue a
    // cut between here and `write_and_verify_rtc()` leaves -- the new offset
    // beside the old UTC, an offset for a synchronization that never happened.
    // `main` has the opposite residue, the old offset beside the new UTC, which
    // is the better of the two whenever the timezone did not change; it pays
    // for that by discovering an unusable metadata layer too late to matter.
    //
    // Neither residue can be cleared by a product image today, because no
    // product image can synchronize at all. That is what #356 is for, and it is
    // also where this stops being a trade: the sequence moves behind one seam
    // there, and one `nvs_set_blob` of the pair makes the flash side a single
    // atomic write.
    const esp_err_t metadata_result = save_time_metadata(
        request.timezone_offset_minutes, request.utc_seconds);
    if (metadata_result != ESP_OK) {
      ESP_LOGW(kTag, "persist time metadata failed: %s",
               esp_err_to_name(metadata_result));
      roll_back_or_log(previous);
      return attadipa::debug::TimeSinkResult::Failed;
    }

    if (!write_and_verify_rtc(rtc, request.utc_seconds)) {
      roll_back_or_log(previous);
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

void refresh_clock(lv_timer_t *timer) {
  const attadipa::apps::ClockState clock = read_clock_state();
  state.clock_face.update(attadipa::apps::format_clock(clock, false));
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
  // the one fact that explains all of it. Until #356 the recovery is
  // `idf.py erase-flash`, and a serial cable is not how an operator should have
  // to discover that they need one.
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
