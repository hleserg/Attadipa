// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include <cstddef>
#include <cstdint>

namespace attadipa::firmware {

// Shared by the AK and QMI owners. Device identity is checked by their
// sequences before writes. Never resets or deletes its caller's bus.
class I2cRegisterDevice {
public:
  I2cRegisterDevice() = default;
  I2cRegisterDevice(const I2cRegisterDevice &) = delete;
  I2cRegisterDevice &operator=(const I2cRegisterDevice &) = delete;
  ~I2cRegisterDevice() { (void)close(); }

  esp_err_t open(i2c_master_bus_handle_t bus, std::uint8_t address) {
    if (!bus || device_ || address < 0x08 || address > 0x77)
      return ESP_ERR_INVALID_ARG;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    return i2c_master_bus_add_device(bus, &config, &device_);
  }
  esp_err_t close() {
    if (!device_)
      return ESP_OK;
    const auto err = i2c_master_bus_rm_device(device_);
    if (err == ESP_OK)
      device_ = nullptr;
    return err;
  }
  bool read(std::uint8_t reg, std::uint8_t *out, std::size_t size) {
    return device_ && i2c_master_transmit_receive(device_, &reg, 1, out, size,
                                                  kTimeoutMs) == ESP_OK;
  }
  bool write(std::uint8_t reg, std::uint8_t value) {
    const std::uint8_t bytes[] = {reg, value};
    return device_ && i2c_master_transmit(device_, bytes, sizeof(bytes),
                                          kTimeoutMs) == ESP_OK;
  }
  void wait_us(unsigned us) { esp_rom_delay_us(us); }
  std::int64_t now_us() { return esp_timer_get_time(); }

private:
  static constexpr int kTimeoutMs = 50;
  i2c_master_dev_handle_t device_ = nullptr;
};

} // namespace attadipa::firmware
