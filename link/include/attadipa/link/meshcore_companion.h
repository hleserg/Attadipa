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

    // WHICH NODE THIS IS, AND WHETHER IT IS THE RIGHT ONE.
    //
    // `advertises_meshcore()` matches the Companion service UUID or the name
    // substring, and connects to whichever advertisement arrives first. With
    // two nodes in range that is a coin toss: the bench reached one five times
    // and the other four across nine runs, and the two disagreed about which
    // room was reachable, so an unannounced swap is a silently different mesh
    // (docs/research/MESHCORE_T114_FIRST_CONTACT.md:54 "There are two MeshCore
    // nodes in range").
    //
    // The identity is the 32-byte public key at offset 4 of
    // RESP_CODE_SELF_INFO, which this class already parses for the name at
    // offset 58. It is read here and compared here; where the expected value
    // is *kept* is the transport's business, because this class has no storage
    // that outlives a session.
    void pin(const core::MeshPeerId& node);
    bool pinned(core::MeshPeerId& out) const;
    bool node_id(core::MeshPeerId& out) const;

    // True once the handshake has read a public key that is not the pinned one.
    // Nothing here acts on it: the frame was well formed, the session is not
    // faulted, and tearing a connection down is the transport's decision to
    // make and the transport's to not repeat. It latches until the next
    // session, so a poll that happens after `disconnected()` still sees why.
    bool wrong_node() const { return wrong_node_; }

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

    // How long an operation may stay in flight. `RESP_CODE_SENT` carries the
    // node's own estimate of the round trip in bytes 6..9, and that estimate is
    // the budget for the phase it answers: MEASURED 0x0966 = 2406 ms against an
    // actual 720 ms on the T114 bench (MESHCORE_T114_FIRST_CONTACT.md:326-329
    // "estimated round trip"). It is clamped, because the node's output is a
    // peer's output (MESHCORE_PARSER_BOUNDS.md §5): a zero would fail a send
    // that is merely fast, and a 0xFFFFFFFF would hold the one in-flight slot
    // for seven weeks.
    //
    // kMaxAckWait is also the budget for a phase the node has *not* estimated --
    // a CMD_SEND_TXT_MSG or a CMD_SEND_LOGIN that is answered by nothing at all.
    // It is chosen, not derived, and nothing else in the system depends on it:
    // six times the largest estimate the bench has produced, and short enough
    // that an owner retrying by hand is not told the slot is still busy. It is
    // deliberately not sourced to `link_`'s liveness window, which is zero on
    // this transport -- BLE has a real connection signal, so nothing expires a
    // session on inactivity and there is no window to inherit.
    static constexpr core::Millis kMinAckWait{1000};
    static constexpr core::Millis kMaxAckWait{15000};

    bool enqueue(const std::uint8_t* data, std::size_t size);
    bool enqueue_private(const core::MeshPeerId& peer, std::string_view text,
                         core::WallTime timestamp);
    void end_operation();
    void reset_session();
    void update_availability();
    void accept_contact(const std::uint8_t* data, std::size_t size);
    void accept_message(const std::uint8_t* data, std::size_t size, bool v3);
    void accept_channel_message_v3(const std::uint8_t* data, std::size_t size);
    const core::MeshPeer* find_peer_prefix(const std::uint8_t* prefix) const;

    // Liveness zero: disabled. BLE reports connection and disconnection, so a
    // silence timer would only invent a second, worse answer to a question the
    // transport already answers (link_state.cpp:211). This is the only place
    // the config is written -- the constructor used to override a value stated
    // here, which is how a comment came to source a constant to a window that
    // did not exist.
    LinkState link_{{core::TransportKind::Bluetooth, core::Millis{0}, true}};
    core::MeshStatus status_{};
    // The node this session is talking to, and the one it is supposed to be
    // talking to. `pinned_` is handed in by the transport from storage and
    // survives `reset_session()`; the other two belong to the session.
    core::MeshPeerId pinned_{};
    bool pinned_set_ = false;
    bool wrong_node_ = false;
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
    bool awaiting_login_ = false;
    // One operation, one deadline, covering all three flags above rather than
    // one of them. `awaiting_login_` had no bound at all: a room the node
    // cannot reach, or a CMD_SEND_LOGIN answered by RESP_CODE_ERR, left it set
    // for the life of the session -- and once send_busy() gates every send and
    // the transport's own claim, that is the exact failure #315 exists to
    // remove, reappearing in the phase the first fix did not bound.
    //
    // Zero budget means no operation is being timed. The stamp is taken on the
    // first tick() that sees the operation, not when it is queued, because a
    // send carries a wall clock and not a monotonic one; each RESP_CODE_SENT
    // then re-arms it with the node's estimate for the phase it starts.
    core::MonotonicTime op_since_{};
    core::Millis op_budget_{};
    core::MeshPeerId room_peer_{};
    std::array<char, core::kMeshTextBytes + 1> room_text_{};
    core::WallTime room_timestamp_{};
};

}  // namespace attadipa::link
