#pragma once

#include "brightness_store.h"
#include "nvs.h"

namespace attadipa::firmware {

// Both boards use the same key and the same uncertain-write transaction.
// The composition root supplies its one default-NVS initialization verdict.
inline apps::BrightnessRead load_brightness(esp_err_t storage,
                                             std::uint8_t &percent) {
  using apps::BrightnessRead;
  if (storage != ESP_OK) return BrightnessRead::Failed;
  nvs_handle_t handle{};
  esp_err_t err = nvs_open("attadipa_screen", NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return BrightnessRead::Missing;
  if (err != ESP_OK) return BrightnessRead::Failed;
  err = nvs_get_u8(handle, "brightness", &percent);
  nvs_close(handle);
  return err == ESP_OK ? BrightnessRead::Present
      : err == ESP_ERR_NVS_NOT_FOUND ? BrightnessRead::Missing
                                    : BrightnessRead::Failed;
}

inline apps::BrightnessWrite persist_brightness(esp_err_t storage,
                                                std::uint8_t percent) {
  if (storage != ESP_OK) return apps::BrightnessWrite::Failed;
  struct Write {
    nvs_handle_t handle{};
    bool open() { return nvs_open("attadipa_screen", NVS_READWRITE, &handle) == ESP_OK; }
    bool write(std::uint8_t value) { return nvs_set_u8(handle, "brightness", value) == ESP_OK; }
    bool commit() { return nvs_commit(handle) == ESP_OK; }
    void close() { nvs_close(handle); }
  } write;
  return store_brightness(write, percent);
}

} // namespace attadipa::firmware
