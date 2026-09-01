#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "attadipa/core/mesh_service.h"
#include "esp_err.h"

esp_err_t start_meshcore_ble();
bool configure_meshcore_ble(std::uint32_t passkey);
bool stop_meshcore_ble();
bool meshcore_ble_send(const std::array<std::uint8_t, 6>& peer_prefix,
                       std::string_view text,
                       attadipa::core::WallTime timestamp);
bool meshcore_ble_send_room(
    const std::array<std::uint8_t, attadipa::core::kMeshPublicKeyBytes>& room,
    std::string_view password, std::string_view text,
    attadipa::core::WallTime timestamp);
// Deletes the bond of the peer whose repeat-pairing attempt faulted the
// transport, and arms one fresh pairing. Refuses -- returns false -- when no
// such conflict was recorded, which is what keeps it from being a way to walk
// the bond store. See firmware/main/meshcore_bond_recovery.h.
bool meshcore_ble_forget_bond();

attadipa::core::MeshStatus meshcore_ble_status();
