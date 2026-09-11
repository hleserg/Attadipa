#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "attadipa/core/location_service.h"
#include "attadipa/core/mesh_service.h"
#include "attadipa/core/position.h"
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
    // A dropped notification can be the answer to an outstanding drain
    // request, and nothing downstream will ever see it -- this path bypasses
    // receive() by construction. So the drop is where that drain has to end:
    // otherwise the node's backlog waits out `draining_since_` below for no
    // reason. Clearing it when the dropped frame was something else costs one
    // duplicate CMD_SYNC_NEXT_MESSAGE, which the node answers like any other.
    // The push that was coalesced into that drain is not lost with it:
    // `pending_push_` outlives the flag and `tick()` spends it.
    void drop_oversize_frame() { ++malformed_frames_; draining_ = false; }

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
    // The reverse, and the only one: the pin, the refusal it caused and the
    // two lines the mesh screen shows for them are all cleared, so the next
    // SELF_INFO is adopted instead of compared. Says whether there was a pin
    // to clear. It is `pinned_set_` that decides -- a clear that zeroed the
    // key and left it set would refuse every node (#411). Where the pin is
    // *kept* is still the transport's business, and so is erasing it there.
    bool unpin();
    bool pinned(core::MeshPeerId& out) const;
    bool node_id(core::MeshPeerId& out) const;

    // True once the handshake has read a public key that is not the pinned one.
    // It latches until the next session, so a poll that happens after
    // `disconnected()` still sees why.
    //
    // This class acts on it by declining to answer, and in two places rather
    // than one. `receive()` drops every frame that arrives after it latches,
    // which covers every ask that leaves from inside the dispatcher; and
    // `tick()` withholds the `pending_push_` sweep below, which is the one ask
    // that does not. That is not the link being torn down -- this class does
    // not own the link, and one that tore it down would tear it down again on
    // the reconnect that follows -- it is this class declining to answer.
    // Round 2 of #388 measured what "acts on it in no way" cost:
    // `kPushMessageWaiting` enqueued CMD_SYNC_NEXT_MESSAGE unconditionally, so
    // the watch could ask a node it had just refused for its queued messages
    // and put the reply on the mesh screen. A push the node sends *before* it
    // identifies itself is how that reaches the sweep: it is remembered while a
    // drain is outstanding, the refusal arrives, and the deadline then hands
    // the bit to a `tick()` that `receive()`'s guard never sees.
    bool wrong_node() const { return wrong_node_; }

    // THE COORDINATE THE NODE PUTS IN ITS OWN ADVERTISEMENT, and when this
    // session read it. False until a RESP_CODE_SELF_INFO from an accepted node
    // has been parsed, and false again after a disconnect -- it is session
    // state, exactly like `node_id`.
    //
    // It is deliberately not a `MeshStatus` field. `MeshStatus` is what the
    // mesh screen renders, and a coordinate is not mesh status; the reader that
    // wants this is `NodePositionProvider`, which turns it into a
    // `core::GnssObservation` and hands it to the one owner of position in this
    // tree. Growing the status struct instead would have put a wire fact on a
    // screen with no owner in between, which is the shape P0.3 exists to end.
    //
    // **What it is not** is a fix. The node transmits no fix flag, no satellite
    // count and no observation time, and writes these bytes only when its own
    // receiver is solving -- so a receiver that has stopped leaves the last
    // coordinate here, unchanged and unmarked. Everything above this treats it
    // accordingly.
    bool node_position(core::Position& out, core::MonotonicTime& arrived) const;

    // What RESP_CODE_CUSTOM_VARS said about the node's receiver, which is a
    // different question from whether the coordinate is any good. `Unknown`
    // until an answer arrives, and `Unknown` for good on a node that does not
    // define the command.
    core::ReceiverPresence node_receiver() const { return node_receiver_; }

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

    // The narrower question, and the one an untagged response has to be matched
    // against. `send_busy()` is about the *operation* -- it stays true through
    // `awaiting_confirm_`, which is a phase the node has already answered with
    // RESP_CODE_SENT and is now waiting out a radio round trip for. A response
    // arriving during that phase cannot belong to the send, because the send's
    // response has been and gone.
    //
    // THE TWO HALVES ARE NOT SYMMETRIC, AND `op_answered_` IS WHY. RESP_CODE_SENT
    // clears `awaiting_send_` and cannot clear `awaiting_login_`: the login's
    // slot has to stay claimed until PUSH_CODE_LOGIN_SUCCESS or the budget
    // decides it. So an *answered* login still reads as `awaiting_login_`, and
    // reasoning from that flag alone charges it for errors whose own answer had
    // already been and gone -- the same mistake as `awaiting_confirm_`, one
    // phase over.
    bool op_owed_an_answer() const
    {
        return (awaiting_send_ || awaiting_login_) && !op_answered_;
    }

    bool enqueue(const std::uint8_t* data, std::size_t size);
    bool enqueue_private(const core::MeshPeerId& peer, std::string_view text,
                         core::WallTime timestamp);
    void end_operation();
    void reset_session();
    void update_availability();
    void accept_contact(const std::uint8_t* data, std::size_t size);
    void accept_self_position(const std::uint8_t* data, core::MonotonicTime now);
    void accept_custom_vars(const std::uint8_t* data, std::size_t size);
    bool accept_message(const std::uint8_t* data, std::size_t size, bool v3);
    bool accept_channel_message_v3(const std::uint8_t* data, std::size_t size);
    bool request_next_message(core::MonotonicTime now);
    bool spend_pending_push(core::MonotonicTime now);
    void drain_after(bool accepted, core::MonotonicTime now);
    const core::MeshPeer* find_peer_prefix(const std::uint8_t* prefix) const;
    void fail_battery_request(bool ambiguous_error);
    void invalidate_node_battery();

    // Liveness zero: disabled. BLE reports connection and disconnection, so a
    // silence timer would only invent a second, worse answer to a question the
    // transport already answers -- `link/src/link_state.cpp:211` --
    // "if (config_.liveness.value == 0) {". This is the only place
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
    enum class BatteryRequest : std::uint8_t { Idle, Queued, Waiting };
    BatteryRequest battery_request_ = BatteryRequest::Idle;
    core::MonotonicTime battery_started_{};
    core::MonotonicTime poll_now_{};
    bool battery_polled_ = false;
    bool battery_due_ = false;
    // An identity change within a connection cannot correlate an old voltage
    // reply. A new transport session is required before polling again.
    bool battery_identity_blocked_ = false;
    // ERR carries no request id. After a timeout, typed responses and the
    // existing send deadline decide delivery until the connection resets.
    bool battery_errors_ambiguous_ = false;
    // A CMD_SYNC_NEXT_MESSAGE is outstanding, so the node is already going
    // to hand over what it has and a second ask would only fill the ring
    // with commands whose answers are on their way. Cleared by
    // `reset_session()` with the rest of the session, which is what starts
    // a fresh drain after a reconnect rather than resuming a dead one.
    //
    // `draining_since_` bounds it, and the bound is not tidiness: every other
    // path that clears this flag runs in the dispatcher, on a frame that
    // reached it *and* was accepted. An answer the node never sends, one a
    // full ring refuses, or one dropped before receive() sees it would leave
    // the flag latched with no request outstanding -- and from then on every
    // PUSH_CODE_MSG_WAITING is coalesced into a request that is never going to
    // be answered, so the whole backlog strands for the life of the session.
    // The timestamp is read only while `draining_` is true, so the stale value
    // the deadline leaves behind is never consulted.
    bool draining_ = false;
    core::MonotonicTime draining_since_{};
    // WHAT THE COALESCING COSTS, AND WHO PAYS IT BACK.
    //
    // Swallowing a push while `draining_` is true is only free if the drain
    // ends the way the node ends it. `RESP_CODE_NO_MORE_MESSAGES` proves the
    // queue was empty when that sync was processed, so a message queued
    // before the swallowed push had already been handed over and one queued
    // after it pushes again behind the terminator -- there, and only there,
    // the push is genuinely spent and this bit is cleared.
    //
    // Every other way a drain ends leaves the node holding messages: an
    // answer this build cannot parse, one that never arrived, one
    // `drop_oversize_frame()` threw away, a full ring, or a RESP_CODE_ERR
    // belonging to some other command. Before the coalescing every push
    // enqueued its own request and none of that mattered; after it, a push
    // dropped in one of those windows is a backlog nobody asks for again,
    // because nothing in this repository establishes that the node emits a
    // second PUSH_CODE_MSG_WAITING for a message it has already announced.
    //
    // So the push is remembered rather than dropped, and `tick()` spends it
    // whenever no request is outstanding. `tick()` rather than each of those
    // five sites: two of them have no `now` to stamp a request with, the
    // worker calls `tick()` unconditionally on every event and every poll
    // timeout, and one place that asks "the node has a message and we are not
    // asking for it" cannot be added to and forgotten the way five can.
    bool pending_push_ = false;
    core::Position node_position_{};
    core::MonotonicTime node_position_at_{};
    bool has_node_position_ = false;
    core::ReceiverPresence node_receiver_ = core::ReceiverPresence::Unknown;
    // Asked once per session, and tracked only so that the error a node too old
    // for opcode 40 answers with can be told apart from a send's error. All
    // three clear at `reset_session()`.
    //
    // `custom_vars_since_` bounds the wait, and the bound is what keeps the
    // attribution honest rather than merely tidy. A node that answers this
    // command with neither RESP_CODE_CUSTOM_VARS nor RESP_CODE_ERR would
    // otherwise leave the flag set for the session, and every later error that
    // arrives outside a send's response window would be charged to a request
    // that is never going to be answered -- a real send's failure silently
    // absorbed by a receiver hint nobody is waiting on.
    bool custom_vars_requested_ = false;
    bool awaiting_custom_vars_ = false;
    core::MonotonicTime custom_vars_since_{};
    // WHICH COMMAND AN UNTAGGED ERROR BELONGS TO IS A QUESTION ONLY THE ORDER
    // ANSWERS. The frame carries no correlation field, and a defined command
    // that fails its own guard is refused with the code an undefined one gets:
    // `docs/research/MESHCORE_COMPANION_PROTOCOL.md:525` -- "indistinguishable
    // from a" genuinely unknown opcode. So there is nothing on the wire to read
    // and no flag combination to infer it from; what is left is that the node
    // answers in the order it was asked, and this queue is FIFO, so the order
    // it was asked in is the order `enqueue()` handed out. `tx_seq_` records
    // it, and the two stamps below are the only frames whose place in it we
    // ever need. Neither stamp is read unless its own flag says that command is
    // still outstanding, so a stale one from a finished operation is never
    // compared against anything.
    std::uint32_t tx_seq_ = 0;
    std::uint32_t custom_vars_seq_ = 0;
    std::uint32_t op_seq_ = 0;
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
    // Set by RESP_CODE_SENT for either half of an operation, and the reason
    // `op_owed_an_answer()` above can tell an answered login from a waiting
    // one. `op_budget_` cannot stand in for it: tick() gives an unanswered
    // operation `kMaxAckWait` on its first pass, so a non-zero budget does not
    // mean the node has said anything.
    bool op_answered_ = false;
    core::MeshPeerId room_peer_{};
    std::array<char, core::kMeshTextBytes + 1> room_text_{};
    core::WallTime room_timestamp_{};
};

}  // namespace attadipa::link
