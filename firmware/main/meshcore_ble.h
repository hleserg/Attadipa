#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "attadipa/core/mesh_service.h"
#include "meshcore_forget_outcome.h"
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
// Asks for the bond of the peer whose stale-bond failure faulted the transport
// to be deleted, and one fresh pairing armed.
//
// `ESP_OK` means the request was accepted, **not** that the bond is gone: the
// deletion happens on the mesh worker, and #378 was this function's `ESP_OK`
// being turned into a terminal success before `ble_store_util_delete_peer()`
// had been called at all. The answer arrives from
// `meshcore_ble_forget_bond_outcome()`.
//
// `ESP_ERR_INVALID_STATE` when no such conflict was recorded -- which is what
// keeps this from being a way to walk the bond store -- `ESP_ERR_NOT_FINISHED`
// when one is already in flight, and `ESP_ERR_NO_MEM` when the request could
// not be queued. See firmware/main/meshcore_bond_recovery.h.
esp_err_t meshcore_ble_forget_bond();

// The answer to the accepted forget-bond, consumed once. `InFlight` while the
// worker still has it; `Deleted` only after the store returned 0; `Refused`
// when the store said no and the bond is still there; `Nothing` when the
// conflict record had already gone, which leaves no bond behind and is not a
// failed deletion.
attadipa::firmware::ForgetOutcome meshcore_ble_forget_bond_outcome();

attadipa::core::MeshStatus meshcore_ble_status();
