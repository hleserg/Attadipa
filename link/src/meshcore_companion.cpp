#include "attadipa/link/meshcore_companion.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace attadipa::link {
namespace {

// Zeroing a local the compiler can prove is never read again is a dead store it
// may legally delete (CWE-14), and this tree has no explicit_bzero. Writing
// through a volatile pointer is the portable way to make the writes happen. The
// fills on member storage do not need this — that storage outlives the call and
// is read again, so those stores are observable.
void secure_zero(void* data, std::size_t size)
{
    volatile std::uint8_t* byte = static_cast<volatile std::uint8_t*>(data);
    while (size-- != 0) {
        *byte++ = 0;
    }
}

constexpr std::uint8_t kAppStart = 1;
constexpr std::uint8_t kSendText = 2;
constexpr std::uint8_t kGetContacts = 4;
constexpr std::uint8_t kSyncNextMessage = 10;
constexpr std::uint8_t kDeviceQuery = 22;
constexpr std::uint8_t kSendLogin = 26;
constexpr std::uint8_t kAppProtocolVersion = 3;

constexpr std::uint8_t kResponseError = 1;
constexpr std::uint8_t kResponseContactsStart = 2;
constexpr std::uint8_t kResponseContact = 3;
constexpr std::uint8_t kResponseContactsEnd = 4;
constexpr std::uint8_t kResponseSelfInfo = 5;
constexpr std::uint8_t kResponseSent = 6;
constexpr std::uint8_t kResponseContactMessage = 7;
constexpr std::uint8_t kResponseNoMoreMessages = 10;
constexpr std::uint8_t kResponseDeviceInfo = 13;
constexpr std::uint8_t kResponseContactMessageV3 = 16;
constexpr std::uint8_t kResponseChannelMessageV3 = 17;
constexpr std::uint8_t kPushSendConfirmed = 0x82;
constexpr std::uint8_t kPushMessageWaiting = 0x83;
constexpr std::uint8_t kPushLoginSuccess = 0x85;
constexpr std::uint8_t kPushLoginFail = 0x86;
constexpr std::uint8_t kAdvertTypeChat = 1;

std::uint32_t little_u32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void write_u32(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8U);
    out[2] = static_cast<std::uint8_t>(value >> 16U);
    out[3] = static_cast<std::uint8_t>(value >> 24U);
}

template <std::size_t N>
bool copy_text(std::array<char, N>& out, const std::uint8_t* data,
               std::size_t size)
{
    const std::size_t copy = std::min(size, N - 1);
    std::memcpy(out.data(), data, copy);
    out[copy] = '\0';
    return copy != size;
}

}  // namespace

MeshCoreCompanion::MeshCoreCompanion()
{
    status_.availability = core::Availability::Unreachable;
    status_.transport = link_.phase();
}

// Every phase of a send down at once, and the Room continuation with it. The
// three flags are one operation; clearing a subset is what left a login running
// after the send that owned it had already failed.
void MeshCoreCompanion::end_operation()
{
    awaiting_send_ = false;
    awaiting_confirm_ = false;
    awaiting_login_ = false;
    room_peer_ = {};
    room_text_.fill('\0');
    room_timestamp_ = {};
    op_budget_ = core::Millis{};
}

void MeshCoreCompanion::reset_session()
{
    status_.node_name.fill('\0');
    // The identity is session state and the pin is not. A reconnect must read
    // the key again rather than carry the last node's answer into a session
    // that may be with a different node -- that is the whole defect this
    // records against.
    status_.node_id = core::MeshPeerId{};
    status_.has_node_id = false;
    wrong_node_ = false;
    status_.last_sender.fill('\0');
    status_.last_message.fill('\0');
    status_.delivery = core::MeshDelivery::None;
    status_.peers_reported = 0;
    status_.peers_retained = 0;
    status_.has_snr = false;
    status_.peers_truncated = false;
    status_.message_truncated = false;
    peer_count_ = 0;
    tx_head_ = 0;
    tx_size_ = 0;
    // A dropped link leaves whatever was queued but never transmitted in these
    // slots -- including a CMD_SEND_LOGIN that never reached the radio. Moving
    // the indices alone would leave those bytes readable for the life of the
    // object, so the reset path clears the ring itself.
    for (MeshCoreFrame& queued : tx_) {
        queued.bytes.fill(0);
        queued.size = 0;
    }
    expected_ack_.fill(0);
    firmware_version_code_ = 0;
    device_info_seen_ = false;
    self_info_seen_ = false;
    contacts_complete_ = false;
    op_since_ = {};
    end_operation();
}

void MeshCoreCompanion::begin(core::MonotonicTime now)
{
    link_.reset();
    reset_session();
    (void)link_.apply(LinkEvent::Attach, now);
    update_availability();
}

void MeshCoreCompanion::peer_arriving(core::MonotonicTime now)
{
    (void)link_.apply(LinkEvent::PeerArriving, now);
    update_availability();
}

void MeshCoreCompanion::connected(core::MonotonicTime now)
{
    if (link_.phase() == core::TransportPhase::Attached) {
        (void)link_.apply(LinkEvent::PeerArriving, now);
    }
    if (link_.apply(LinkEvent::PeerEstablished, now) != EventOutcome::Applied) {
        update_availability();
        return;
    }
    reset_session();
    const std::uint8_t start[] = {kAppStart, 0, 0, 0, 0, 0, 0, 0,
                                  'A', 't', 't', 'a', 'd', 'i', 'p', 'a'};
    (void)enqueue(start, sizeof(start));
    update_availability();
}

void MeshCoreCompanion::disconnected(core::MonotonicTime now)
{
    (void)link_.apply(LinkEvent::PeerGone, now,
                      core::DisconnectReason::PeerClosed);
    reset_session();
    update_availability();
}

void MeshCoreCompanion::fault(core::MonotonicTime now)
{
    (void)link_.apply(LinkEvent::Fault, now,
                      core::DisconnectReason::Fault);
    reset_session();
    update_availability();
}

void MeshCoreCompanion::tick(core::MonotonicTime now)
{
    link_.tick(now);
    // The node accepted the message, or the login, and then said nothing.
    // Upstream MeshCore does not promise a confirmation for every send -- a
    // packet that is never acknowledged on air produces no
    // PUSH_CODE_SEND_CONFIRMED at all, and a room out of range produces no
    // PUSH_CODE_LOGIN_SUCCESS and no PUSH_CODE_LOGIN_FAIL either -- so without
    // a bound the one in-flight slot is held for the life of the session by an
    // operation that already failed. Fail-closed and observable: the verdict is
    // Failed, not a silent release.
    if (!send_busy()) {
        op_budget_ = core::Millis{};
    } else if (op_budget_.value == 0) {
        op_since_ = now;
        op_budget_ = kMaxAckWait;
    } else if (core::elapsed(op_since_, now) >= op_budget_) {
        status_.delivery = core::MeshDelivery::Failed;
        end_operation();
    }
    update_availability();
}

void MeshCoreCompanion::update_availability()
{
    status_.transport = link_.phase();
    if (link_.phase() == core::TransportPhase::Faulted) {
        status_.availability = core::Availability::Failed;
    } else if (link_.ready() && device_info_seen_ && self_info_seen_ &&
               contacts_complete_) {
        status_.availability = core::Availability::Ready;
    } else {
        status_.availability = core::Availability::Unreachable;
    }
}

std::size_t meshcore_loggable_prefix(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0) {
        return 0;
    }
    if (data[0] == kSendLogin) {
        // The opcode and the room's public key are both public; the password
        // begins at the byte after them and nothing past that point is printed.
        // A frame shorter than the public prefix carries no password to hide.
        constexpr std::size_t kPublicPrefix = 1 + core::kMeshPublicKeyBytes;
        return size < kPublicPrefix ? size : kPublicPrefix;
    }
    return size;
}

bool MeshCoreCompanion::enqueue(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0 || size > kMeshCoreFrameBytes ||
        tx_size_ == tx_.size()) {
        return false;
    }
    MeshCoreFrame& frame = tx_[(tx_head_ + tx_size_) % tx_.size()];
    std::memcpy(frame.bytes.data(), data, size);
    frame.size = static_cast<std::uint16_t>(size);
    ++tx_size_;
    return true;
}

bool MeshCoreCompanion::next_tx(MeshCoreFrame& out)
{
    if (tx_size_ == 0) {
        return false;
    }
    out = tx_[tx_head_];
    // A popped slot otherwise keeps a byte-for-byte copy of whatever was sent,
    // and CMD_SEND_LOGIN passes through here. Clearing on the way out bounds a
    // password's life in this ring to the one frame that is being transmitted.
    tx_[tx_head_].bytes.fill(0);
    tx_[tx_head_].size = 0;
    tx_head_ = (tx_head_ + 1) % tx_.size();
    --tx_size_;
    return true;
}

void MeshCoreCompanion::accept_contact(const std::uint8_t* data,
                                       std::size_t size)
{
    if (size < 148 || data[33] != kAdvertTypeChat) {
        return;
    }
    core::MeshPeer candidate{};
    std::memcpy(candidate.id.public_key.data(), &data[1],
                candidate.id.public_key.size());
    const std::size_t name_length =
        static_cast<std::size_t>(std::find(&data[100], &data[132], 0) -
                                 &data[100]);
    (void)copy_text(candidate.name, &data[100], name_length);
    for (std::size_t i = 0; i < peer_count_; ++i) {
        if (peers_[i].id == candidate.id) {
            peers_[i] = candidate;
            return;
        }
    }
    if (peer_count_ < peers_.size()) {
        peers_[peer_count_++] = candidate;
        status_.peers_retained = static_cast<std::uint16_t>(peer_count_);
    } else {
        status_.peers_truncated = true;
    }
}

const core::MeshPeer*
MeshCoreCompanion::find_peer_prefix(const std::uint8_t* prefix) const
{
    for (std::size_t i = 0; i < peer_count_; ++i) {
        if (std::memcmp(peers_[i].id.public_key.data(), prefix, 6) == 0) {
            return &peers_[i];
        }
    }
    return nullptr;
}

void MeshCoreCompanion::accept_message(const std::uint8_t* data,
                                       std::size_t size, bool v3)
{
    const std::size_t prefix = v3 ? 4 : 1;
    const std::size_t text_type = v3 ? 11 : 8;
    std::size_t text = v3 ? 16 : 13;
    if (size < text) {
        ++malformed_frames_;
        return;
    }
    if (data[text_type] == 2) {
        if (size < text + 4) {
            ++malformed_frames_;
            return;
        }
        text += 4;  // signed messages carry a four-byte signature before text
    }
    status_.has_snr = v3;
    if (v3) {
        status_.snr_quarter_db = static_cast<std::int8_t>(data[1]);
    }
    const core::MeshPeer* sender = find_peer_prefix(&data[prefix]);
    if (sender != nullptr) {
        status_.last_sender = sender->name;
    } else {
        status_.last_sender.fill('\0');
    }
    status_.message_truncated =
        copy_text(status_.last_message, &data[text], size - text);
}

void MeshCoreCompanion::accept_channel_message_v3(const std::uint8_t* data,
                                                   std::size_t size)
{
    // RESP_CODE_CHANNEL_MSG_RECV_V3: code, SNR, two reserved bytes, channel,
    // path, text type, timestamp, then text. A Room Server reply has no
    // contact-key prefix to resolve to a sender name.
    constexpr std::size_t kTextOffset = 11;
    if (size < kTextOffset || data[6] != 0) {
        ++malformed_frames_;
        return;
    }
    status_.has_snr = true;
    status_.snr_quarter_db = static_cast<std::int8_t>(data[1]);
    status_.last_sender.fill('\0');
    status_.message_truncated =
        copy_text(status_.last_message, &data[kTextOffset], size - kTextOffset);
}

bool MeshCoreCompanion::receive(const std::uint8_t* data, std::size_t size,
                                core::MonotonicTime now)
{
    if (data == nullptr || size == 0 || size > kMeshCoreFrameBytes ||
        !link_.ready()) {
        ++malformed_frames_;
        return false;
    }
    (void)link_.apply(LinkEvent::PeerData, now);
    switch (data[0]) {
    case kResponseDeviceInfo:
        if (size < 82) { ++malformed_frames_; return false; }
        firmware_version_code_ = data[1];
        if (device_info_seen_) break;
        device_info_seen_ = true;
        {
            const std::uint8_t contacts[] = {kGetContacts};
            if (!enqueue(contacts, sizeof(contacts))) {
                ++malformed_frames_;
                return false;
            }
        }
        break;
    case kResponseSelfInfo:
        if (size < 58) { ++malformed_frames_; return false; }
        // Offset 4, 32 bytes, ahead of the name this frame was already being
        // read for: MeshCore's own `docs/companion_protocol.md` gives
        // RESP_CODE_SELF_INFO as type, advert type, tx power, max tx power,
        // then the public key, and the bench capture agrees -- the name lands
        // exactly at 58 in a 72-byte frame
        // (docs/research/MESHCORE_T114_FIRST_CONTACT.md:184
        // "RESP_CODE_SELF_INFO"). The `size < 58` bound above already covers
        // it, so no second check is added for a shorter prefix.
        std::memcpy(status_.node_id.public_key.data(), &data[4],
                    core::kMeshPublicKeyBytes);
        status_.has_node_id = true;
        (void)copy_text(status_.node_name, &data[58], size - 58);
        if (pinned_set_ && !(status_.node_id == pinned_)) {
            // A well-formed frame from the wrong node. Not a malformed frame,
            // not a fault, and not a disconnect from here: this class does not
            // own the link, and a class that tore the link down would tear it
            // down again on the reconnect that follows. The handshake stops
            // here -- no CMD_DEVICE_QUERY goes out, so nothing asks this node
            // for contacts and nothing is sent through it -- and the transport
            // reads `wrong_node()` and decides.
            wrong_node_ = true;
            break;
        }
        if (self_info_seen_) break;
        self_info_seen_ = true;
        {
            const std::uint8_t query[] = {kDeviceQuery, kAppProtocolVersion};
            if (!enqueue(query, sizeof(query))) {
                ++malformed_frames_;
                return false;
            }
        }
        break;
    case kResponseContactsStart:
        if (size < 5) { ++malformed_frames_; return false; }
        status_.peers_reported = static_cast<std::uint16_t>(
            std::min<std::uint32_t>(little_u32(&data[1]),
                                    std::numeric_limits<std::uint16_t>::max()));
        peer_count_ = 0;
        status_.peers_retained = 0;
        status_.peers_truncated = false;
        contacts_complete_ = false;
        break;
    case kResponseContact:
        if (size < 148) { ++malformed_frames_; return false; }
        accept_contact(data, size);
        break;
    case kResponseContactsEnd:
        if (size < 5) { ++malformed_frames_; return false; }
        contacts_complete_ = true;
        {
            const std::uint8_t sync[] = {kSyncNextMessage};
            if (!enqueue(sync, sizeof(sync))) {
                ++malformed_frames_;
                return false;
            }
        }
        break;
    case kResponseSent:
        if (size < 10 || (!awaiting_send_ && !awaiting_login_)) {
            ++malformed_frames_;
            return false;
        }
        if (awaiting_send_) {
            std::memcpy(expected_ack_.data(), &data[2], expected_ack_.size());
            status_.delivery = core::MeshDelivery::Accepted;
            awaiting_send_ = false;
            // The operation is not over: these four bytes are what the
            // confirmation below is matched against, so the slot stays claimed
            // until one arrives or the budget runs out. Releasing it here is
            // #315 -- a second send could start, overwrite `expected_ack_`, and
            // leave the first operation with no way to reach a verdict.
            awaiting_confirm_ = true;
        }
        // Both halves get this, which is the point: the node sends
        // RESP_CODE_SENT for CMD_SEND_LOGIN too, and gating the budget on
        // `awaiting_send_` discarded the one estimate the login ever carries.
        {
            op_since_ = now;
            const std::uint32_t estimate = little_u32(&data[6]);
            op_budget_ = core::Millis{
                estimate < kMinAckWait.value   ? kMinAckWait.value
                : estimate > kMaxAckWait.value ? kMaxAckWait.value
                                               : estimate};
        }
        break;
    case kPushSendConfirmed:
        if (size >= 5 && awaiting_confirm_ &&
            std::memcmp(&data[1], expected_ack_.data(), expected_ack_.size()) == 0) {
            status_.delivery = core::MeshDelivery::Confirmed;
            awaiting_confirm_ = false;
        }
        break;
    case kPushMessageWaiting: {
        const std::uint8_t sync[] = {kSyncNextMessage};
        if (!enqueue(sync, sizeof(sync))) {
            ++malformed_frames_;
            return false;
        }
        break;
    }
    case kPushLoginSuccess:
        if (size < 8 || !awaiting_login_ ||
            std::memcmp(&data[2], room_peer_.public_key.data(), 6) != 0) {
            ++malformed_frames_;
            return false;
        }
        awaiting_login_ = false;
        if (!enqueue_private(room_peer_, std::string_view(room_text_.data()),
                             room_timestamp_)) {
            status_.delivery = core::MeshDelivery::Failed;
        }
        room_peer_ = {};
        room_text_.fill('\0');
        room_timestamp_ = {};
        break;
    case kPushLoginFail:
        if (size < 7 || !awaiting_login_ ||
            std::memcmp(&data[1], room_peer_.public_key.data(), 6) != 0) {
            ++malformed_frames_;
            return false;
        }
        end_operation();
        status_.delivery = core::MeshDelivery::Failed;
        break;
    case kResponseContactMessage:
        accept_message(data, size, false);
        break;
    case kResponseContactMessageV3:
        accept_message(data, size, true);
        break;
    case kResponseChannelMessageV3:
        accept_channel_message_v3(data, size);
        break;
    case kResponseNoMoreMessages:
        if (size != 1) { ++malformed_frames_; return false; }
        break;
    case kResponseError:
        if (size < 2) { ++malformed_frames_; return false; }
        // Including the login. MESHCORE_COMPANION_PROTOCOL.md §5: a defined
        // command that fails its guard falls through to RESP_CODE_ERR, so a
        // CMD_SEND_LOGIN for a room the node does not hold arrives here and
        // nowhere else. Leaving `awaiting_login_` set was one of the two ways
        // the slot became permanent.
        if (send_busy()) {
            status_.delivery = core::MeshDelivery::Failed;
            end_operation();
        }
        break;
    default:
        // A response code this build does not know is a frame we did not
        // understand, not a frame we accepted. The node's output is a peer's
        // output (MESHCORE_PARSER_BOUNDS.md §5), so it is counted and refused
        // rather than promoted to valid by silence. The link is deliberately
        // left alone: LinkEvent::PeerData is already applied above, so a node
        // that grows a new opcode stays reachable instead of being torn down.
        ++malformed_frames_;
        return false;
    }
    update_availability();
    return true;
}

bool MeshCoreCompanion::peer(std::size_t index, core::MeshPeer& out) const
{
    if (index >= peer_count_) {
        return false;
    }
    out = peers_[index];
    return true;
}

void MeshCoreCompanion::pin(const core::MeshPeerId& node)
{
    pinned_ = node;
    pinned_set_ = true;
}

bool MeshCoreCompanion::pinned(core::MeshPeerId& out) const
{
    if (!pinned_set_) return false;
    out = pinned_;
    return true;
}

bool MeshCoreCompanion::node_id(core::MeshPeerId& out) const
{
    if (!status_.has_node_id) return false;
    out = status_.node_id;
    return true;
}

bool MeshCoreCompanion::send_private(const core::MeshPeerId& peer,
                                     std::string_view text,
                                     core::WallTime timestamp)
{
    return enqueue_private(peer, text, timestamp);
}

bool MeshCoreCompanion::enqueue_private(const core::MeshPeerId& peer,
                                        std::string_view text,
                                        core::WallTime timestamp)
{
    // MeshCore private-message frames address the destination by its six-byte
    // public-key prefix; the full key is only used by commands such as login.
    constexpr std::size_t kPeerPrefixBytes = 6;
    constexpr std::size_t header = 7 + kPeerPrefixBytes;
    // The link being up is what makes a text frame sendable; a contact sync
    // still running is not. Availability::Ready also demands contacts_complete_,
    // and gating here on that dropped a room message whose login succeeded while
    // the burst was still arriving -- send_room() accepted the send under one
    // condition and its own continuation was refused under a stricter one.
    // MEASURED on the bench 2026-08-28: LOGIN_SUCCESS at 131698, END_OF_CONTACTS
    // at 134228, no CMD_SEND_TXT_MSG ever sent.
    // One send at a time, all the way to a terminal outcome. `send_room()`
    // below has always refused an overlapping operation; this path did not, and
    // that asymmetry is #315. The Room flow's own text send is not caught by
    // this: `kPushLoginSuccess` clears `awaiting_login_` before it calls here,
    // because the login and the text it carries are one owned operation.
    if (!link_.ready() || !device_info_seen_ || !self_info_seen_ || send_busy() ||
        text.empty() ||
        text.size() > core::kMeshTextBytes ||
        timestamp.unix_seconds < 0 ||
        static_cast<std::uint64_t>(timestamp.unix_seconds) >
            std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    std::array<std::uint8_t, header + core::kMeshTextBytes> frame{};
    frame[0] = kSendText;
    frame[1] = 0;  // plain private text
    frame[2] = 0;  // first attempt
    write_u32(&frame[3], static_cast<std::uint32_t>(timestamp.unix_seconds));
    std::memcpy(&frame[7], peer.public_key.data(), kPeerPrefixBytes);
    std::memcpy(&frame[header], text.data(), text.size());
    if (!enqueue(frame.data(), header + text.size())) {
        return false;
    }
    awaiting_send_ = true;
    op_budget_ = core::Millis{};
    status_.delivery = core::MeshDelivery::Queued;
    return true;
}

bool MeshCoreCompanion::send_room(
    const std::array<std::uint8_t, core::kMeshPublicKeyBytes>& room,
    std::string_view password, std::string_view text, core::WallTime timestamp)
{
    constexpr std::size_t kMaxRoomPasswordBytes = 15;
    if (!link_.ready() || !device_info_seen_ || !self_info_seen_ || send_busy() ||
        password.empty() || password.size() > kMaxRoomPasswordBytes ||
        text.empty() || text.size() > core::kMeshTextBytes ||
        timestamp.unix_seconds < 0 ||
        static_cast<std::uint64_t>(timestamp.unix_seconds) >
            std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    std::array<std::uint8_t, 1 + core::kMeshPublicKeyBytes + kMaxRoomPasswordBytes> frame{};
    frame[0] = kSendLogin;
    std::memcpy(&frame[1], room.data(), room.size());
    std::memcpy(&frame[1 + room.size()], password.data(), password.size());
    const bool queued = enqueue(frame.data(), 1 + room.size() + password.size());
    // This stack copy dies here on both paths. The queue slot enqueue() wrote is
    // cleared by next_tx() when it is handed to the transport, and by
    // reset_session() if the link drops before that. secure_zero() rather than
    // std::fill: frame is a local whose address escapes only into enqueue(), so
    // once that inlines a plain fill is a dead store and may be dropped.
    secure_zero(frame.data(), frame.size());
    if (!queued) {
        return false;
    }
    room_peer_.public_key = room;
    std::memcpy(room_text_.data(), text.data(), text.size());
    room_text_[text.size()] = '\0';
    room_timestamp_ = timestamp;
    awaiting_login_ = true;
    op_budget_ = core::Millis{};
    status_.delivery = core::MeshDelivery::Queued;
    return true;
}

}  // namespace attadipa::link
