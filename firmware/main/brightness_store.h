#pragma once

#include "attadipa/apps/brightness.h"

namespace attadipa::firmware {

// The native write transaction, compiled by the board and fault tests.
// ESP-IDF setters can report failure after writing the new value, including
// REMOVE_FAILED and FLASH_OP_FAIL. No readback before NVS recovery establishes
// its reboot outcome. Opening never writes the requested brightness value.
template <typename Nvs>
apps::BrightnessWrite store_brightness(Nvs &nvs, std::uint8_t percent) {
  if (!nvs.open()) return apps::BrightnessWrite::Failed;
  const bool written = nvs.write(percent);
  const bool committed = written && nvs.commit();
  nvs.close();
  return committed ? apps::BrightnessWrite::Saved
                   : apps::BrightnessWrite::Uncertain;
}

} // namespace attadipa::firmware
