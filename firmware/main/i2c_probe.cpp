// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "i2c_probe.h"

#include <cinttypes>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_ATTADIPA_AK09911_PROBE
#include "ak09911.h"
#include "ak09911_i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace attadipa::firmware {
namespace {

constexpr char kTag[] = "i2c-probe";

// The 7-bit range that is actually addressable: 0x00-0x07 and 0x78-0x7F are
// reserved by the I2C specification and a device answering there would be a
// fault rather than a finding.
constexpr std::uint8_t kFirstAddress = 0x08;
constexpr std::uint8_t kLastAddress = 0x77;

// 100 kHz, not the 400 kHz the known board runs at. This bus has never been
// exercised by us; the slower clock tolerates weaker pull-ups and longer
// traces, and a bring-up scan has no throughput to lose.
constexpr std::uint32_t kProbeHz = 100000;

// Bounded, because a bus held low by a stuck device must produce a clean "no
// device answered" rather than a hang. A hang here costs a physical bench pass.
constexpr int kProbeTimeoutMs = 50;

constexpr std::uint8_t kAxp2101Address = 0x34;

// AXP2101 CHG_V_CFG. Logged as a raw byte: the decode belongs in the bench
// report, against expectations written down before the scan ran, not in
// firmware where it would read like a conclusion.
constexpr std::uint8_t kAxp2101ChargeVoltageRegister = 0x64;

void read_axp2101_charge_voltage(i2c_master_bus_handle_t bus) {
  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kAxp2101Address;
  device_config.scl_speed_hz = kProbeHz;

  i2c_master_dev_handle_t device = nullptr;
  esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &device);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "AXP2101 0x%02x: add device failed: %s", kAxp2101Address,
             esp_err_to_name(err));
    return;
  }

  const std::uint8_t reg = kAxp2101ChargeVoltageRegister;
  std::uint8_t value = 0;
  err = i2c_master_transmit_receive(device, &reg, 1, &value, 1, kProbeTimeoutMs);
  if (err == ESP_OK) {
    ESP_LOGI(kTag, "AXP2101 REG 0x%02x = 0x%02x", reg, value);
  } else {
    ESP_LOGE(kTag, "AXP2101 REG 0x%02x: read failed: %s", reg,
             esp_err_to_name(err));
  }
  i2c_master_bus_rm_device(device);
}

#if CONFIG_ATTADIPA_AK09911_PROBE
void read_ak09911(i2c_master_bus_handle_t bus) {
  Ak09911I2c io;
  const auto opened = io.open(bus, 0x0d);  // this module's CAD-high address
  if (opened != ESP_OK) {
    ESP_LOGE(kTag, "AK09911 add device failed: %s", esp_err_to_name(opened));
    return;
  }
  Ak09911<Ak09911I2c> sensor(io);
  auto result = sensor.start();
  const auto &info = sensor.info();
  ESP_LOGI(kTag, "AK09911 ID=%02x %02x ASA=%02x %02x %02x fuse_mode=%02x start=%d",
           info.id[0], info.id[1], info.asa[0], info.asa[1], info.asa[2],
           info.fuse_mode_readback, static_cast<int>(result));
  if (result == Ak09911Result::Ok && info.fuse_mode_readback != 0x1f)
    ESP_LOGW(kTag, "AK09911 fuse-mode discrepancy: ASA scaling is nominal; "
                   "silicon authenticity and calibration are not established");
  unsigned samples = 0, not_ready = 0, overflow = 0, invalid = 0, dor = 0;
  const auto began = io.now_us();
  auto last_fresh = began;
  // Policy: a 20-second 10 Hz bring-up, with a 500 ms no-fresh-data deadline.
  // The loop is never a replacement for the product's acquisition owner.
  while (result == Ak09911Result::Ok && io.now_us() - began < 20000000) {
    Ak09911Sample sample;
    const auto read = sensor.read(sample);
    if (read == Ak09911Result::Sample) {
      ++samples;
      dor += (sample.st1 & 2) != 0;
      last_fresh = sample.received_at_us;
      ESP_LOGI(kTag, "AKRAW,%" PRId64 ",%d,%d,%d,%02x,%02x",
               sample.received_at_us, sample.raw[0], sample.raw[1], sample.raw[2],
               sample.st1, sample.st2);
    } else if (read == Ak09911Result::NotReady) {
      ++not_ready;
    } else if (read == Ak09911Result::Overflow) {
      ++overflow;
      ESP_LOGW(kTag, "AK09911 overflow ST1=%02x ST2=%02x", sample.st1, sample.st2);
    } else if (read == Ak09911Result::InvalidData) {
      ++invalid;
      ESP_LOGW(kTag, "AK09911 invalid frame ST1=%02x ST2=%02x", sample.st1, sample.st2);
    } else {
      result = read;
      ESP_LOGE(kTag, "AK09911 acquisition stopped: result=%d", static_cast<int>(read));
      break;
    }
    if (io.now_us() - last_fresh > 500000) {
      result = Ak09911Result::NotReady;
      ESP_LOGE(kTag, "AK09911 no usable fresh sample for 500 ms; stopping");
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
  }
  const bool power_down_attempted = sensor.identified();
  const auto stopped = sensor.stop();
  const auto closed = io.close();
  ESP_LOGI(kTag, "AK09911 summary duration_us=%" PRId64
                 " samples=%u not_ready=%u overflow=%u invalid=%u dor=%u"
                 " result=%d power_down_attempted=%d power_down_result=%d close=%s",
           io.now_us() - began, samples, not_ready, overflow, invalid, dor,
           static_cast<int>(result), power_down_attempted, static_cast<int>(stopped),
           esp_err_to_name(closed));
}
#endif

}  // namespace

void run_i2c_probe() {
  // Said once, and deliberately: everything the boot banner printed above about
  // the board came from the build configuration, not from this silicon. On an
  // unknown board that banner is a claim about the build and nothing else.
  ESP_LOGW(kTag, "probe build — the board identity printed above is the build's,"
                 " not this unit's");
  ESP_LOGI(kTag, "scanning SDA %d / SCL %d at %" PRIu32 " Hz",
           CONFIG_ATTADIPA_I2C_PROBE_SDA, CONFIG_ATTADIPA_I2C_PROBE_SCL,
           kProbeHz);

  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = static_cast<gpio_num_t>(CONFIG_ATTADIPA_I2C_PROBE_SDA);
  bus_config.scl_io_num = static_cast<gpio_num_t>(CONFIG_ATTADIPA_I2C_PROBE_SCL);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus = nullptr;
  esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "bus init failed: %s", esp_err_to_name(err));
    return;
  }
  err = i2c_master_bus_reset(bus);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "bus reset failed: %s", esp_err_to_name(err));
  }

  bool axp2101_answered = false;
  unsigned found = 0;
  for (std::uint8_t address = kFirstAddress; address <= kLastAddress; ++address) {
    if (i2c_master_probe(bus, address, kProbeTimeoutMs) != ESP_OK) {
      continue;
    }
    ESP_LOGI(kTag, "ACK 0x%02x", address);
    ++found;
    axp2101_answered = axp2101_answered || address == kAxp2101Address;
  }

  if (found == 0) {
    // Not "no devices". A bus held low by a stuck slave, absent pull-ups, or
    // two wrong pin numbers all look exactly like this from here.
    ESP_LOGW(kTag, "no address acknowledged — check the pins, the pull-ups and "
                   "whether the rail is up before reading this as an empty bus");
  } else {
    ESP_LOGI(kTag, "%u address(es) acknowledged", found);
  }

  if (axp2101_answered) {
    read_axp2101_charge_voltage(bus);
  } else {
    ESP_LOGW(kTag, "no AXP2101 at 0x%02x — CHG_V_CFG not read",
             kAxp2101Address);
  }

#if CONFIG_ATTADIPA_AK09911_PROBE
  read_ak09911(bus);
#endif
  const auto deleted = i2c_del_master_bus(bus);
  if (deleted != ESP_OK)
    ESP_LOGE(kTag, "probe bus cleanup failed: %s", esp_err_to_name(deleted));
}

}  // namespace attadipa::firmware
