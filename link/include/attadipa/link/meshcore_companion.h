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

// How many leading bytes of a Companion frame may be written to a transcript.
// CMD_SEND_LOGIN carries the Room Server password immediately after the 32-byte
// room public key, so a hex dump of the whole frame publishes the credential on
// whatever console is attached. The rule lives here rather than in the logger
// because this translation unit is the one that lays the frame out and so is the
// only place that knows where the secret starts; a logger that hardcoded the
// opcode would be a second copy of the protocol to keep in step.
//
// Everything else prints whole: the transcript is the only record of this link
// (there is no sniffer on the bench), and #316 asks for source-level redaction
// of credential opcodes, not for the transcript's removal.
std::size_t meshcore_loggable_prefix(const std::uint8_t* data, std::size_t size);

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

    // Whether a send is still being tracked. There is exactly one slot, and
    // #315 is what it cost to have two ways of being in it and only one place
    // to record the answer: two overlapping sends were both accepted, both
    // reported success to the caller, and the second `RESP_CODE_SENT` was
    // counted as malformed because the single `expected_ack_` was already
    // spoken for. The slot is claimed by an accepted send and released only by
    // a terminal outcome -- confirmed, an explicit error, the ack timeout, or
    // the session resetting -- so a `MeshDelivery` read while this is false
    // describes the operation that just ended and nothing else.
    //
    // The transport asks this the same way: `meshcore_ble.cpp` claims a slot of
    // its own before it posts the request, so a second `mesh-send` is refused
    // where the caller can still be told, rather than accepted and then lost.
    bool send_busy() const
    {
        return awaiting_send_ || awaiting_confirm_ || awaiting_login_;
    }

    // The transport claimed the slot and then could not hand the request to the
    // node -- a contact prefix that is not in the retained chat contacts is the
    // shipping case, and it is decided by the worker, outside this object.
    // Nothing here is waiting on that operation, but a caller that was told
    // MeshOk must not then read the *previous* send's verdict as this one's.
    void send_abandoned() { status_.delivery = core::MeshDelivery::Failed; }

private:
    static constexpr std::size_t kRetainedPeers = 16;
    static constexpr std::size_t kTxDepth = 4;

    // How long to wait for PUSH_CODE_SEND_CONFIRMED. `RESP_CODE_SENT` carries
    // the node's own estimate of the round trip in bytes 6..9, and that
    // estimate is the budget: MEASURED 0x0966 = 2406 ms against an actual
    // 720 ms on the T114 bench (MESHCORE_T114_FIRST_CONTACT.md:326-329
    // "estimated round trip"). It is clamped, because the node's output is a
    // peer's output (MESHCORE_PARSER_BOUNDS.md §5): a zero would fail a send
    // that is merely fast, and a 0xFFFFFFFF would hold the one in-flight slot
    // for seven weeks. The ceiling is the link's own liveness window below --
    // an operation cannot outlive the link that carries it.
    static constexpr core::Millis kMinAckWait{1000};
    static constexpr core::Millis kMaxAckWait{15000};

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
    // The half of a send that used to have no state at all. `awaiting_send_`
    // ends at `RESP_CODE_SENT`; the operation does not, because the ack bytes
    // that arrived with it are what the confirmation is matched against.
    bool awaiting_confirm_ = false;
    core::MonotonicTime ack_since_{};
    core::Millis ack_budget_{};
    bool awaiting_login_ = false;
    core::MeshPeerId room_peer_{};
    std::array<char, core::kMeshTextBytes + 1> room_text_{};
    core::WallTime room_timestamp_{};
};

}  // namespace attadipa::link
