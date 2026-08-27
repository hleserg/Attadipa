// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "i2c_probe.h"

#include <cinttypes>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

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

  i2c_del_master_bus(bus);
}

}  // namespace attadipa::firmware
