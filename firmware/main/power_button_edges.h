#pragma once

#include <cstddef>
#include <cstdint>

namespace attadipa::firmware {

constexpr std::uint8_t kAxpPowerPositiveEdge = 1U << 0;
constexpr std::uint8_t kAxpPowerNegativeEdge = 1U << 1;
constexpr std::uint8_t kAxpPowerEdges =
    kAxpPowerPositiveEdge | kAxpPowerNegativeEdge;

enum class PowerEdgeDelivery : std::uint8_t {
  None,
  Deferred,
  ClearFailed,
  Delivered,
};

template <typename Clear, typename Publish>
PowerEdgeDelivery deliver_power_edges(std::uint8_t status,
                                      std::size_t free_slots, Clear clear,
                                      Publish publish) {
  const std::uint8_t edges = status & kAxpPowerEdges;
  const std::size_t needed =
      ((edges & kAxpPowerNegativeEdge) != 0 ? 1U : 0U) +
      ((edges & kAxpPowerPositiveEdge) != 0 ? 1U : 0U);
  if (needed == 0) {
    return PowerEdgeDelivery::None;
  }
  if (needed > free_slots) {
    return PowerEdgeDelivery::Deferred;
  }
  if (!clear(edges)) {
    return PowerEdgeDelivery::ClearFailed;
  }
  if ((edges & kAxpPowerNegativeEdge) != 0) {
    (void)publish(true);
  }
  if ((edges & kAxpPowerPositiveEdge) != 0) {
    (void)publish(false);
  }
  return PowerEdgeDelivery::Delivered;
}

} // namespace attadipa::firmware
