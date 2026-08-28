#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "attadipa/core/mesh_service.h"
#include "attadipa/link/link_state.h"

namespace attadipa::link {

inline constexpr std::size_t kMeshCoreFrameBytes = 176;

struct MeshCoreFrame {
    std::array<std::uint8_t, kMeshCoreFrameBytes> bytes{};
    std::uint16_t size = 0;
};

class MeshCoreCompanion final : public core::MeshProvider {
public:
    MeshCoreCompanion();

    void begin(core::MonotonicTime now);
    void peer_arriving(core::MonotonicTime now);
    void connected(core::MonotonicTime now);
    void disconnected(core::MonotonicTime now);
    void fault(core::MonotonicTime now);
    void tick(core::MonotonicTime now);

    bool receive(const std::uint8_t* data, std::size_t size,
                 core::MonotonicTime now);
    bool next_tx(MeshCoreFrame& out);

    core::MeshStatus status() const override { return status_; }
    std::size_t peer_count() const override { return peer_count_; }
    bool peer(std::size_t index, core::MeshPeer& out) const override;
    bool send_private(const core::MeshPeerId& peer, std::string_view text,
                      core::WallTime timestamp) override;
    // Debug-only Room Server seam: the password is serialized directly into
    // CMD_SEND_LOGIN and never retained in provider state.
    bool send_room(const std::array<std::uint8_t, core::kMeshPublicKeyBytes>& room,
                   std::string_view password, std::string_view text,
                   core::WallTime timestamp);

    // A notification longer than kMeshCoreFrameBytes has no buffer to arrive
    // in, so the transport drops it before a copy and records it here instead
    // of through receive(). Dropping and counting -- rather than tearing the
    // link down -- is what keeps one malformed frame from a peer we do not
    // trust (MESHCORE_PARSER_BOUNDS.md §5) out of the recovery path.
    void drop_oversize_frame() { ++malformed_frames_; }

    std::uint32_t malformed_frames() const { return malformed_frames_; }
    std::uint8_t firmware_version_code() const { return firmware_version_code_; }

private:
    static constexpr std::size_t kRetainedPeers = 16;
    static constexpr std::size_t kTxDepth = 4;

    bool enqueue(const std::uint8_t* data, std::size_t size);
    bool enqueue_private(const core::MeshPeerId& peer, std::string_view text,
                         core::WallTime timestamp);
    void reset_session();
    void update_availability();
    void accept_contact(const std::uint8_t* data, std::size_t size);
    void accept_message(const std::uint8_t* data, std::size_t size, bool v3);
    void accept_channel_message_v3(const std::uint8_t* data, std::size_t size);
    const core::MeshPeer* find_peer_prefix(const std::uint8_t* prefix) const;

    LinkState link_{{core::TransportKind::Bluetooth, core::Millis{15000}, true}};
    core::MeshStatus status_{};
    std::array<core::MeshPeer, kRetainedPeers> peers_{};
    std::size_t peer_count_ = 0;
    std::array<MeshCoreFrame, kTxDepth> tx_{};
    std::size_t tx_head_ = 0;
    std::size_t tx_size_ = 0;
    std::array<std::uint8_t, 4> expected_ack_{};
    std::uint32_t malformed_frames_ = 0;
    std::uint8_t firmware_version_code_ = 0;
    bool device_info_seen_ = false;
    bool self_info_seen_ = false;
    bool contacts_complete_ = false;
    bool awaiting_send_ = false;
    bool awaiting_login_ = false;
    core::MeshPeerId room_peer_{};
    std::array<char, core::kMeshTextBytes + 1> room_text_{};
    core::WallTime room_timestamp_{};
};

}  // namespace attadipa::link
