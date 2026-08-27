#include "attadipa/link/meshcore_companion.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace attadipa::link {
namespace {

constexpr std::uint8_t kAppStart = 1;
constexpr std::uint8_t kSendText = 2;
constexpr std::uint8_t kGetContacts = 4;
constexpr std::uint8_t kSyncNextMessage = 10;
constexpr std::uint8_t kDeviceQuery = 22;
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
constexpr std::uint8_t kPushSendConfirmed = 0x82;
constexpr std::uint8_t kPushMessageWaiting = 0x83;
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

void MeshCoreCompanion::reset_session()
{
    status_.node_name.fill('\0');
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
    expected_ack_.fill(0);
    firmware_version_code_ = 0;
    device_info_seen_ = false;
    self_info_seen_ = false;
    contacts_complete_ = false;
    awaiting_send_ = false;
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
    const std::uint8_t query[] = {kDeviceQuery, kAppProtocolVersion};
    const std::uint8_t start[] = {kAppStart, 0, 0, 0, 0, 0, 0, 0,
                                  'A', 't', 't', 'a', 'd', 'i', 'p', 'a'};
    const std::uint8_t contacts[] = {kGetContacts};
    (void)enqueue(query, sizeof(query));
    (void)enqueue(start, sizeof(start));
    (void)enqueue(contacts, sizeof(contacts));
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
        device_info_seen_ = true;
        break;
    case kResponseSelfInfo:
        if (size < 58) { ++malformed_frames_; return false; }
        (void)copy_text(status_.node_name, &data[58], size - 58);
        self_info_seen_ = true;
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
        break;
    case kResponseSent:
        if (size < 10 || !awaiting_send_) {
            ++malformed_frames_;
            return false;
        }
        std::memcpy(expected_ack_.data(), &data[2], expected_ack_.size());
        status_.delivery = core::MeshDelivery::Accepted;
        awaiting_send_ = false;
        break;
    case kPushSendConfirmed:
        if (size >= 5 &&
            std::memcmp(&data[1], expected_ack_.data(), expected_ack_.size()) == 0) {
            status_.delivery = core::MeshDelivery::Confirmed;
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
    case kResponseContactMessage:
        accept_message(data, size, false);
        break;
    case kResponseContactMessageV3:
        accept_message(data, size, true);
        break;
    case kResponseNoMoreMessages:
        if (size != 1) { ++malformed_frames_; return false; }
        break;
    case kResponseError:
        if (size < 2) { ++malformed_frames_; return false; }
        if (awaiting_send_) {
            status_.delivery = core::MeshDelivery::Failed;
            awaiting_send_ = false;
        }
        break;
    default:
        break;
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

bool MeshCoreCompanion::send_private(const core::MeshPeerId& peer,
                                     std::string_view text,
                                     core::WallTime timestamp)
{
    constexpr std::size_t header = 7 + core::kMeshPublicKeyBytes;
    if (status_.availability != core::Availability::Ready || text.empty() ||
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
    std::memcpy(&frame[7], peer.public_key.data(), peer.public_key.size());
    std::memcpy(&frame[header], text.data(), text.size());
    if (!enqueue(frame.data(), header + text.size())) {
        return false;
    }
    awaiting_send_ = true;
    status_.delivery = core::MeshDelivery::Queued;
    return true;
}

}  // namespace attadipa::link
