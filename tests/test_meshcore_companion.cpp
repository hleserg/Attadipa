#include <array>
#include <cstdio>
#include <cstring>

#include "attadipa/core/mesh_service.h"
#include "attadipa/link/meshcore_companion.h"

namespace {

using attadipa::core::Availability;
using attadipa::core::MeshDelivery;
using attadipa::core::MeshPeer;
using attadipa::core::MeshService;
using attadipa::core::MonotonicTime;
using attadipa::core::WallTime;
using attadipa::link::MeshCoreCompanion;
using attadipa::link::MeshCoreFrame;

int failures = 0;

#define CHECK(value) check((value), #value, __LINE__)

void check(bool value, const char* expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}

MonotonicTime at(std::uint64_t ms) { return MonotonicTime{ms}; }

void connect_and_handshake(MeshCoreCompanion& client)
{
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);
    CHECK(!client.next_tx(frame));

    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(3)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 2 && frame.bytes[0] == 22 && frame.bytes[1] == 3);
    CHECK(!client.next_tx(frame));

    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    CHECK(client.receive(device, sizeof(device), at(4)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 4);
    CHECK(!client.next_tx(frame));

    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(5)));

    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(client.receive(contact, sizeof(contact), at(6)));

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(7)));
    CHECK(client.status().availability == Availability::Ready);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(!client.next_tx(frame));
}

void test_handshake_contacts_and_service_boundary()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    CHECK(service.peer_count() == 1);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(std::strcmp(peer.name.data(), "Peer") == 0);
    CHECK(std::strcmp(service.status().node_name.data(), "Node") == 0);
}

void test_room_send_does_not_wait_for_contact_sync()
{
    MeshCoreCompanion client;
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    std::uint8_t self[62]{};
    self[0] = 5;
    CHECK(client.receive(self, sizeof(self), at(3)));
    CHECK(client.next_tx(frame));
    std::uint8_t device[82]{};
    device[0] = 13;
    CHECK(client.receive(device, sizeof(device), at(4)));
    CHECK(client.next_tx(frame));

    std::array<std::uint8_t, 32> room{};
    CHECK(client.status().availability == Availability::Unreachable);
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
}

void test_send_and_receive()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}));
    CHECK(service.status().delivery == MeshDelivery::Queued);

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);
    CHECK(std::memcmp(&frame.bytes[7], peer.id.public_key.data(), 6) == 0);
    CHECK(std::memcmp(&frame.bytes[13], "Hello", 5) == 0);

    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 10, 0, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(9)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);

    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(10)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    std::uint8_t incoming[21]{};
    incoming[0] = 16;
    incoming[1] = static_cast<std::uint8_t>(-8);
    std::memcpy(&incoming[4], peer.id.public_key.data(), 6);
    incoming[10] = 0xff;
    incoming[11] = 0;
    std::memcpy(&incoming[16], "Reply", 5);
    CHECK(client.receive(incoming, sizeof(incoming), at(11)));
    CHECK(service.status().has_snr);
    CHECK(service.status().snr_quarter_db == -8);
    CHECK(std::strcmp(service.status().last_sender.data(), "Peer") == 0);
    CHECK(std::strcmp(service.status().last_message.data(), "Reply") == 0);
}

void test_connected_ble_does_not_expire_while_idle()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    client.tick(at(20000));
    CHECK(client.status().availability == Availability::Ready);
}

// #316. The bench transcript prints every frame that crosses this link, and
// CMD_SEND_LOGIN carries the Room Server password on the wire by protocol -- the
// wire cannot change. What must change is what gets printed. This is the exact
// composition pump_tx performs (queue a login, pop it, ask how much of it may be
// written to the console), so what passes here is what the console sees.
void test_a_room_password_never_reaches_the_transcript()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) room[i] = static_cast<std::uint8_t>(0x80 + i);
    // Not a credential: a canary picked so that a leak is unmistakable in a diff
    // or a capture. No real password appears anywhere in this tree.
    const char* const canary = "CANARY-NOTREAL";
    const std::size_t canary_len = std::strlen(canary);
    CHECK(client.send_room(room, canary, "Hello", WallTime{1000}));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    // The wire is untouched: the radio still sends the password, as the protocol
    // requires, and the room key is still public.
    CHECK(frame.size == 1 + room.size() + canary_len);
    CHECK(frame.bytes[0] == 26);
    CHECK(std::memcmp(&frame.bytes[33], canary, canary_len) == 0);

    const std::size_t printable =
        attadipa::link::meshcore_loggable_prefix(frame.bytes.data(), frame.size);
    // Opcode and the public room key print; the length is reported by the caller
    // from frame.size, so a redacted capture is not mistakable for a short frame.
    CHECK(printable == 1 + room.size());
    CHECK(printable < frame.size);
    bool leaked = false;
    for (std::size_t i = 0; canary_len <= printable && i + canary_len <= printable; ++i) {
        if (std::memcmp(&frame.bytes[i], canary, canary_len) == 0) leaked = true;
    }
    CHECK(!leaked);
    // The boundary is exactly where the credential starts. memchr for canary[0]
    // over the prefix would pass on the room key's byte values alone; this fails
    // if the prefix grows by one byte or shrinks by one.
    CHECK(std::memcmp(&frame.bytes[printable], canary, canary_len) == 0);
}

// The other half of the same rule. #316 asks for redaction of the credential
// opcodes, not for the transcript's removal -- a frame that carries no secret is
// still dumped whole, which is what the hardware evidence in docs/research needs.
void test_a_frame_without_a_credential_still_prints_whole()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}));
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.bytes[0] == 2);
    CHECK(attadipa::link::meshcore_loggable_prefix(frame.bytes.data(), frame.size) ==
          frame.size);
}

void test_room_login_then_private_message()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) room[i] = static_cast<std::uint8_t>(0x80 + i);
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
    CHECK(!client.send_room(room, "0123456789abcdef", "Hello", WallTime{1000}));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 41 && frame.bytes[0] == 26);
    CHECK(std::memcmp(&frame.bytes[1], room.data(), room.size()) == 0);
    CHECK(std::memcmp(&frame.bytes[33], "password", 8) == 0);
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 10, 0, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(client.status().delivery == MeshDelivery::Queued);

    std::uint8_t login_ok[] = {0x85, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&login_ok[2], room.data(), 6);
    CHECK(client.receive(login_ok, sizeof(login_ok), at(9)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);
    CHECK(std::memcmp(&frame.bytes[7], room.data(), 6) == 0);
    CHECK(std::memcmp(&frame.bytes[13], "Hello", 5) == 0);
    CHECK(client.receive(sent, sizeof(sent), at(10)));
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(11)));
    CHECK(client.status().delivery == MeshDelivery::Confirmed);
}

void test_room_login_success_during_a_contact_burst_still_sends()
{
    // The bench produced exactly this order: send_room() accepted while the
    // contact burst was in flight, LOGIN_SUCCESS arrived before
    // END_OF_CONTACTS, and the message was silently dropped because
    // availability was still Unreachable.
    MeshCoreCompanion client;
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    std::uint8_t self[62]{};
    self[0] = 5;
    CHECK(client.receive(self, sizeof(self), at(3)));
    CHECK(client.next_tx(frame));
    std::uint8_t device[82]{};
    device[0] = 13;
    CHECK(client.receive(device, sizeof(device), at(4)));
    CHECK(client.next_tx(frame));

    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) room[i] = static_cast<std::uint8_t>(0x40 + i);
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 41 && frame.bytes[0] == 26);

    const std::uint8_t start[] = {2, 9, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(5)));
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(client.receive(contact, sizeof(contact), at(6)));
    CHECK(client.status().availability == Availability::Unreachable);

    std::uint8_t login_ok[] = {0x85, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&login_ok[2], room.data(), 6);
    CHECK(client.receive(login_ok, sizeof(login_ok), at(7)));

    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);
    CHECK(std::memcmp(&frame.bytes[7], room.data(), 6) == 0);
    CHECK(std::memcmp(&frame.bytes[13], "Hello", 5) == 0);
    CHECK(client.status().delivery == MeshDelivery::Queued);
}

void test_a_fault_survives_reconnect_until_begin_restarts_the_session()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);

    // A write that never completes faults the link while the peer is still
    // attached, so the phase is Faulted rather than Attached.
    client.fault(at(20));
    CHECK(client.status().availability == Availability::Failed);

    // Faulted refuses PeerGone, PeerArriving and PeerEstablished alike, so
    // replaying the arrival sequence is not enough on its own: no CMD_APP_START
    // is queued and the session stays dead. MEASURED on the bench 2026-08-27 --
    // the link came back at MTU 247 and not one Companion frame was sent again
    // for the rest of the boot.
    client.disconnected(at(21));
    client.peer_arriving(at(22));
    client.connected(at(23));

    MeshCoreFrame frame{};
    CHECK(!client.next_tx(frame));
    CHECK(client.status().availability == Availability::Failed);
    const std::uint8_t push[] = {0x88, 0, 0, 0};
    CHECK(!client.receive(push, sizeof(push), at(24)));

    // begin() is the one call that resets the link model, and it is what the
    // transport now makes on every disconnect it intends to reconnect after.
    client.begin(at(25));
    client.peer_arriving(at(26));
    client.connected(at(27));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);

    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(28)));
}

void test_bad_frames_and_disconnect_fail_closed()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    const std::uint8_t short_contact[] = {3};
    CHECK(!client.receive(short_contact, sizeof(short_contact), at(8)));
    CHECK(client.malformed_frames() == 1);
    client.disconnected(at(9));
    CHECK(client.status().availability == Availability::Unreachable);
    MeshPeer peer{};
    CHECK(!client.peer(0, peer));
    CHECK(!client.send_private(peer.id, "no", WallTime{1000}));
    std::uint8_t frame = 13;
    CHECK(!client.receive(&frame, 1, at(10)));
}

// The frames a hostile or broken node can put on the wire, at the seam the BLE
// transport actually hands over.
//
// MESHCORE_COMPANION_PROTOCOL.md:169-173 is the reason this is one test rather
// than a reassembly test: "No length prefix, no delimiter, no checksum, no
// chunking and no reassembly code anywhere in the repository. One GATT
// operation carries one whole companion frame." So a frame that arrives split
// is not a frame to be rebuilt -- every piece is its own malformed frame, and
// the client must never accumulate across notifications. Every case below is
// counted and refused, and none of them may end the session:
// MESHCORE_PARSER_BOUNDS.md §5 puts the node on the far side of a trust
// boundary that a third party on the air can provoke.
void test_hostile_frames_are_bounded_and_the_session_survives()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    std::uint32_t expected = client.malformed_frames();

    // Over size. kMeshCoreFrameBytes is MeshCore's own MAX_FRAME_SIZE 176
    // (MESHCORE_BLE_FRAME_CAPACITY.md:31); on nRF52 the buffer binds and not
    // the link, so 176 is the ceiling whatever MTU was negotiated (:58-60).
    // One byte past it is refused before a payload byte is read.
    std::uint8_t oversize[attadipa::link::kMeshCoreFrameBytes + 1]{};
    oversize[0] = 16;
    CHECK(!client.receive(oversize, sizeof(oversize), at(20)));
    CHECK(client.malformed_frames() == ++expected);

    // The transport cannot copy an over-size notification into a 176-byte
    // frame at all, so it drops it before the copy and records it here. The
    // session must survive: tearing the link down on one malformed frame is
    // how a peer we do not trust ends mesh for the whole boot.
    client.drop_oversize_frame();
    CHECK(client.malformed_frames() == ++expected);
    CHECK(client.status().availability == Availability::Ready);

    // A garbage first byte. The payload's own first byte is the response code
    // (MESHCORE_COMPANION_PROTOCOL.md:171). One this build does not know is
    // counted and refused, never accepted by silence.
    for (const std::uint8_t code : {std::uint8_t{0x00}, std::uint8_t{0x7f},
                                    std::uint8_t{0xa5}, std::uint8_t{0xff}}) {
        const std::uint8_t garbage[] = {code, 1, 2, 3};
        CHECK(!client.receive(garbage, sizeof(garbage), at(21)));
        CHECK(client.malformed_frames() == ++expected);
    }

    // No frame at all.
    CHECK(!client.receive(oversize, 0, at(22)));
    CHECK(client.malformed_frames() == ++expected);
    CHECK(!client.receive(nullptr, 4, at(23)));
    CHECK(client.malformed_frames() == ++expected);

    // Truncated: every response this client parses, one byte short of the
    // length its own reader requires. Each is refused before the read.
    const struct { std::uint8_t code; std::size_t minimum; } truncated[] = {
        {13, 82},   // device info
        {5, 58},    // self info
        {2, 5},     // contacts start
        {3, 148},   // contact
        {4, 5},     // contacts end
        {16, 13},   // contact message
        {0x84, 16}, // contact message v3
    };
    for (const auto& shape : truncated) {
        std::uint8_t frame[176]{};
        frame[0] = shape.code;
        CHECK(!client.receive(frame, shape.minimum - 1, at(24)) ||
              client.malformed_frames() > expected);
        expected = client.malformed_frames();
    }
    CHECK(client.status().availability == Availability::Ready);

    // Fragmentation at every boundary. This is the frame test_send_and_receive
    // delivers whole and sees rendered; here the same bytes are split at each
    // internal offset and delivered as two notifications, which is what a
    // stack that did not preserve message boundaries would produce. Neither
    // half may be accumulated, and the message must never appear.
    std::uint8_t whole[21]{};
    whole[0] = 16;
    whole[1] = static_cast<std::uint8_t>(-8);
    std::memcpy(&whole[4], peer.id.public_key.data(), 6);
    whole[10] = 0xff;
    std::memcpy(&whole[16], "Split", 5);

    for (std::size_t cut = 1; cut < sizeof(whole); ++cut) {
        MeshCoreCompanion fragmented;
        connect_and_handshake(fragmented);
        const std::uint32_t before = fragmented.malformed_frames();

        (void)fragmented.receive(whole, cut, at(30));
        (void)fragmented.receive(&whole[cut], sizeof(whole) - cut, at(31));

        // The only way "Split" can be in the status is reassembly across two
        // notifications, and there is no such thing in this protocol.
        CHECK(std::strcmp(fragmented.status().last_message.data(), "Split") != 0);
        CHECK(fragmented.malformed_frames() > before);
        // And the session is still usable: the whole frame still lands.
        CHECK(fragmented.receive(whole, sizeof(whole), at(32)));
        CHECK(std::strcmp(fragmented.status().last_message.data(), "Split") == 0);
        // Not `== Ready`, and the reason is a property of the protocol rather
        // than a weaker test. With no framing, a byte from the middle of one
        // frame is indistinguishable from the first byte of another: at
        // cut == 5 the tail begins with 0x02, which *is* CONTACTS_START, and a
        // node legitimately restarting contact sync leaves availability
        // Unreachable until CONTACTS_END. What must never happen is the link
        // being torn down by a frame we could not parse.
        CHECK(fragmented.status().availability != Availability::Failed);
    }
}

// #315: two `mesh-send` requests before the first reached a terminal state were
// both accepted, and there was one `expected_ack_` to correlate them with. The
// slot is claimed by an accepted send and released only by a terminal outcome,
// so a second send is refused where the caller can still be told.
void test_one_send_is_in_flight_at_a_time()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) {
        room[i] = static_cast<std::uint8_t>(0x80 + i);
    }

    CHECK(!client.send_busy());
    CHECK(service.send_private(peer.id, "first", WallTime{1000}));
    CHECK(client.send_busy());

    // Before RESP_CODE_SENT. Neither a private nor a Room send may start.
    CHECK(!service.send_private(peer.id, "second", WallTime{1001}));
    CHECK(!client.send_room(room, "password", "second", WallTime{1001}));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(std::memcmp(&frame.bytes[13], "first", 5) == 0);
    CHECK(!client.next_tx(frame));  // and nothing was queued behind it

    // Between RESP_CODE_SENT and the confirmation is the window the old code
    // released the slot in: `expected_ack_` is spoken for, and a second send
    // would overwrite it and leave this operation with no way to reach a
    // verdict. est_timeout is 0x0966 = 2406 ms, the value MEASURED on the T114.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(client.send_busy());
    CHECK(!service.send_private(peer.id, "second", WallTime{1002}));
    CHECK(!client.send_room(room, "password", "second", WallTime{1002}));

    // An ack for a different message does not end this operation, and the slot
    // it does not free stays claimed.
    const std::uint8_t other_ack[] = {0x82, 9, 9, 9, 9};
    CHECK(client.receive(other_ack, sizeof(other_ack), at(9)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(client.send_busy());

    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(10)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);
    CHECK(!client.send_busy());

    // Terminal, so the next one may start -- and a late duplicate of the
    // confirmation cannot end it.
    CHECK(service.send_private(peer.id, "third", WallTime{1003}));
    CHECK(client.send_busy());
    CHECK(client.receive(ack, sizeof(ack), at(11)));
    CHECK(service.status().delivery == MeshDelivery::Queued);
    CHECK(client.send_busy());
}

// The Room flow is one owned operation from CMD_SEND_LOGIN to the ack for the
// text it carries: nothing may start in the middle of it, and the text send it
// makes itself is not refused by the guard that refuses everyone else.
void test_a_room_send_owns_the_slot_through_its_login()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) {
        room[i] = static_cast<std::uint8_t>(0x80 + i);
    }

    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
    CHECK(client.send_busy());
    CHECK(!service.send_private(peer.id, "cuts in", WallTime{1001}));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.bytes[0] == 26);
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(client.send_busy());

    std::uint8_t login_ok[] = {0x85, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&login_ok[2], room.data(), 6);
    CHECK(client.receive(login_ok, sizeof(login_ok), at(9)));
    // The login's own send made it through the guard, which is the whole point
    // of clearing `awaiting_login_` before enqueueing the text.
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);
    CHECK(client.send_busy());
    CHECK(!service.send_private(peer.id, "cuts in", WallTime{1002}));

    CHECK(client.receive(sent, sizeof(sent), at(10)));
    CHECK(client.send_busy());
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(11)));
    CHECK(client.status().delivery == MeshDelivery::Confirmed);
    CHECK(!client.send_busy());
    CHECK(service.send_private(peer.id, "now it may", WallTime{1003}));
}

// The three other ways an operation ends. A node that accepts a message and
// then says nothing is the reason the last one exists: without a bound the one
// slot would be held for the life of the session by a send that already failed.
// A room login is a mesh round trip, and every way it can fail to complete used
// to leave `awaiting_login_` set for the life of the session. Once send_busy()
// gates every send and the transport's own claim, that is #315 again in the one
// phase the first fix did not bound: one Room attempt took the private path
// down with it until the BLE link was dropped and re-established.
void test_a_room_login_that_is_never_answered_still_ends()
{
    MeshPeer peer{};
    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) {
        room[i] = static_cast<std::uint8_t>(0x80 + i);
    }
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};

    // The node refuses the login. MESHCORE_COMPANION_PROTOCOL.md section 5: a
    // defined command failing its guard answers RESP_CODE_ERR, which is where a
    // login for a room the node does not hold arrives.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
        const std::uint8_t error[] = {1, 2};
        CHECK(client.receive(error, sizeof(error), at(8)));
        CHECK(client.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        // The private path is not down with it. This is the assertion the
        // reproduction in the review turns on.
        CHECK(service.send_private(peer.id, "still works", WallTime{1001}));
    }

    // The node queues the login and the room never answers: no LOGIN_SUCCESS
    // and no LOGIN_FAIL, ever. The estimate in RESP_CODE_SENT is the budget --
    // 0x0966 = 2406 ms -- and the login's own SENT used to discard it.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        client.tick(at(8 + 2405));
        CHECK(client.send_busy());
        client.tick(at(8 + 2406));
        CHECK(client.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "still works", WallTime{1001}));
    }

    // The node answers nothing at all -- not even RESP_CODE_SENT -- so there is
    // no estimate to run on. kMaxAckWait is the budget, armed on the first tick
    // that sees the operation.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
        client.tick(at(8));
        client.tick(at(8 + 14999));
        CHECK(client.send_busy());
        client.tick(at(8 + 15000));
        CHECK(client.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "still works", WallTime{1002}));
    }

    // The same absence one state over: a CMD_SEND_TXT_MSG the node takes over
    // BLE and answers with nothing wedges the slot identically, and a fix that
    // covered only the login would leave it.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        client.tick(at(8));
        client.tick(at(8 + 14999));
        CHECK(client.send_busy());
        client.tick(at(8 + 15000));
        CHECK(client.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
    }

    // A second send does not inherit the first one's deadline. One operation
    // can begin and end between two ticks, so a stamp left behind by the last
    // one would fail the next one on its first tick -- the opposite defect, and
    // just as reachable.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "one", WallTime{1000}));
        CHECK(client.receive(sent, sizeof(sent), at(100)));  // budget 2406 ms
        const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
        CHECK(client.receive(ack, sizeof(ack), at(101)));
        CHECK(!client.send_busy());
        // No tick between the two. The second send is queued and then seen for
        // the first time well past the first one's deadline.
        CHECK(service.send_private(peer.id, "two", WallTime{1001}));
        client.tick(at(100 + 2406));
        CHECK(client.send_busy());
        CHECK(client.status().delivery == MeshDelivery::Queued);
    }

    // And a room login does not inherit one either.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "one", WallTime{1000}));
        CHECK(client.receive(sent, sizeof(sent), at(100)));
        const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
        CHECK(client.receive(ack, sizeof(ack), at(101)));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1001}));
        client.tick(at(100 + 2406));
        CHECK(client.send_busy());
        CHECK(client.status().delivery == MeshDelivery::Queued);
    }
}

void test_a_send_that_is_never_confirmed_still_ends()
{
    MeshPeer peer{};
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};

    // An explicit RESP_CODE_ERR, before the node answered at all.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        const std::uint8_t error[] = {1, 4};
        CHECK(client.receive(error, sizeof(error), at(8)));
        CHECK(service.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "again", WallTime{1001}));
    }

    // An explicit RESP_CODE_ERR after RESP_CODE_SENT. This half used to be
    // unreachable: the slot was already free, so the error had nothing to end.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        const std::uint8_t error[] = {1, 4};
        CHECK(client.receive(error, sizeof(error), at(9)));
        CHECK(service.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
    }

    // The node's own estimate runs out. 0x0966 = 2406 ms from at(8).
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        client.tick(at(8 + 2405));
        CHECK(service.status().delivery == MeshDelivery::Accepted);
        CHECK(client.send_busy());
        client.tick(at(8 + 2406));
        CHECK(service.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "again", WallTime{1001}));
    }

    // A node that reports no estimate at all does not fail a send that is
    // merely fast: the budget has a floor of one second.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        const std::uint8_t no_estimate[] = {6, 0, 1, 2, 3, 4, 0, 0, 0, 0};
        CHECK(client.receive(no_estimate, sizeof(no_estimate), at(8)));
        client.tick(at(8 + 999));
        CHECK(client.send_busy());
        client.tick(at(8 + 1000));
        CHECK(!client.send_busy());
    }

    // And one that reports seven weeks does not hold the slot for seven weeks:
    // kMaxAckWait is the ceiling.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        const std::uint8_t forever[] = {6, 0, 1, 2, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF};
        CHECK(client.receive(forever, sizeof(forever), at(8)));
        client.tick(at(8 + 15000));
        CHECK(!client.send_busy());
    }

    // The link dropping ends it too, from either phase, and the session that
    // follows starts with the slot free rather than with the dead one's.
    for (const bool after_response : {false, true}) {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        if (after_response) CHECK(client.receive(sent, sizeof(sent), at(8)));
        CHECK(client.send_busy());
        client.disconnected(at(9));
        CHECK(!client.send_busy());
        CHECK(client.status().delivery == MeshDelivery::None);
    }
}

void test_signed_message_does_not_render_signature_as_text()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    std::uint8_t incoming[24]{};
    incoming[0] = 16;
    std::memcpy(&incoming[4], peer.id.public_key.data(), 6);
    incoming[10] = 0xff;
    incoming[11] = 2;
    incoming[16] = 0xde;
    incoming[17] = 0xad;
    incoming[18] = 0xbe;
    incoming[19] = 0xef;
    std::memcpy(&incoming[20], "Text", 4);
    CHECK(client.receive(incoming, sizeof(incoming), at(8)));
    CHECK(std::strcmp(client.status().last_message.data(), "Text") == 0);
}

void test_channel_message_is_rendered_without_a_contact_prefix()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    std::uint8_t incoming[16]{};
    incoming[0] = 17;
    incoming[1] = static_cast<std::uint8_t>(-4);
    incoming[4] = 3;
    incoming[5] = 0xff;
    incoming[6] = 0;
    std::memcpy(&incoming[11], "Room", 4);
    CHECK(client.receive(incoming, sizeof(incoming), at(8)));
    CHECK(client.status().has_snr);
    CHECK(client.status().snr_quarter_db == -4);
    CHECK(client.status().last_sender[0] == '\0');
    CHECK(std::strcmp(client.status().last_message.data(), "Room") == 0);
}

}  // namespace

int main()
{
    test_handshake_contacts_and_service_boundary();
    test_room_send_does_not_wait_for_contact_sync();
    test_send_and_receive();
    test_connected_ble_does_not_expire_while_idle();
    test_a_room_password_never_reaches_the_transcript();
    test_a_frame_without_a_credential_still_prints_whole();
    test_room_login_then_private_message();
    test_room_login_success_during_a_contact_burst_still_sends();
    test_a_fault_survives_reconnect_until_begin_restarts_the_session();
    test_bad_frames_and_disconnect_fail_closed();
    test_hostile_frames_are_bounded_and_the_session_survives();
    test_one_send_is_in_flight_at_a_time();
    test_a_room_send_owns_the_slot_through_its_login();
    test_a_room_login_that_is_never_answered_still_ends();
    test_a_send_that_is_never_confirmed_still_ends();
    test_signed_message_does_not_render_signature_as_text();
    test_channel_message_is_rendered_without_a_contact_prefix();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("meshcore companion: all host checks passed (SIMULATED transport)\n");
    return 0;
}
