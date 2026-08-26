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
    test_room_login_then_private_message();
    test_bad_frames_and_disconnect_fail_closed();
    test_signed_message_does_not_render_signature_as_text();
    test_channel_message_is_rendered_without_a_contact_prefix();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("meshcore companion: all host checks passed (SIMULATED transport)\n");
    return 0;
}
