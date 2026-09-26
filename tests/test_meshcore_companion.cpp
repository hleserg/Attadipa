#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "attadipa/apps/mesh.h"
#include "attadipa/core/mesh_service.h"
#include "attadipa/core/position.h"
#include "attadipa/link/meshcore_companion.h"

namespace {

namespace core = attadipa::core;

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
    // And CMD_GET_CUSTOM_VARS behind it, which is the one question this session
    // asks about the node's receiver. It goes out here and not earlier because
    // the contacts iteration is finished by the time RESP_CODE_END_OF_CONTACTS
    // arrives; a command sent during one is how a client aborts its own sync.
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 40);
    CHECK(!client.next_tx(frame));

    // AND THE NODE ANSWERS THE DRAIN REQUEST, because every node does. An empty
    // queue answers CMD_SYNC_NEXT_MESSAGE with RESP_CODE_NO_MORE_MESSAGES, and
    // a fixture that takes the command and never replies leaves the client's
    // drain outstanding for the rest of the test -- which is not what a node
    // does and would hide the coalescing that keeps a burst of pushes cheap.
    const std::uint8_t drained[] = {10};
    CHECK(client.receive(drained, sizeof(drained), at(7)));
    CHECK(!client.next_tx(frame));
}

// A CONTACT THE NODE COUNTS AND THE WATCH DOES NOT KEEP, WITH NOTHING CAPPED.
//
// `accept_contact()` returns before any count moves when the advert type is not
// chat, so `peers_retained` stays below the node's own `CONTACTS_START` total
// with no flag raised anywhere -- the bench fleet has a Room Server and a
// repeater, so this is the ordinary shape of a contact list. The face pairs the
// two numbers on `peers_complete`, which is what this asserts the provider
// sets, and when: not while the iteration is running.
void test_a_contact_dropped_by_type_leaves_retained_below_reported()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);

    const std::uint8_t start[] = {2, 3, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(10)));
    CHECK(client.status().peers_reported == 3);
    CHECK(client.status().peers_retained == 0);
    CHECK(!client.status().peers_complete); // an iteration is running

    for (std::uint8_t n = 0; n < 3; ++n) {
        std::uint8_t contact[148]{};
        contact[0] = 3;
        for (std::size_t i = 0; i < 32; ++i)
            contact[1 + i] = static_cast<std::uint8_t>(i + 1 + n * 32);
        // The middle one is a Room Server rather than a chat contact.
        contact[33] = n == 1 ? 3 : 1;
        std::memcpy(&contact[100], "Peer", 4);
        CHECK(client.receive(contact, sizeof(contact), at(11 + n)));
    }
    CHECK(client.status().peers_retained == 2);
    CHECK(!client.status().peers_complete); // still running, still no pair

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(20)));
    CHECK(client.status().peers_complete);
    CHECK(client.status().peers_retained == 2 &&
          client.status().peers_reported == 3);

    // A second sync clears it again, so a stale pair cannot survive one.
    const std::uint8_t restart[] = {2, 4, 0, 0, 0};
    CHECK(client.receive(restart, sizeof(restart), at(21)));
    CHECK(!client.status().peers_complete);
}

// The node's own key is the only thing on this wire that tells two MeshCore
// nodes apart. `advertises_meshcore()` matches a service UUID or a name
// substring and takes whichever advertisement arrives first, and the bench had
// two nodes answering both -- so a run's node was not a choice the firmware
// made -- `docs/research/MESHCORE_T114_FIRST_CONTACT.md:54` -- "There are two
// MeshCore nodes in range, and the transport picks either".
core::MeshPeerId key_of(std::uint8_t seed)
{
    core::MeshPeerId id{};
    for (std::size_t i = 0; i < core::kMeshPublicKeyBytes; ++i) {
        id.public_key[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

// Up to and including RESP_CODE_SELF_INFO, which is where the identity arrives
// and where a wrong node has to be stopped -- before CMD_DEVICE_QUERY asks it
// for anything.
void handshake_to_self_info(MeshCoreCompanion& client, const core::MeshPeerId& id)
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
    std::memcpy(&self[4], id.public_key.data(), core::kMeshPublicKeyBytes);
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(3)));
}

// A BACKLOG IS READ TO THE END, NOT SAMPLED.
//
// Reconnecting to a node holding three messages used to read the oldest and
// leave the other two on the node, with the link reporting ready and the screen
// showing the oldest -- one CMD_SYNC_NEXT_MESSAGE went out per push and none
// after a message arrived, so nothing ever asked for the second one.
//
// The five situations the fix has to survive are all here, because the failure
// was in how they combine and not in any of them alone.
void test_a_queued_backlog_is_drained_to_the_end()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    MeshCoreFrame frame{};

    // The node says it has something. That is the start of a drain, not a
    // delivery: one push, one request.
    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(10)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(!client.next_tx(frame));

    // A BURST OF PUSHES WHILE THAT REQUEST IS OUTSTANDING COSTS NOTHING EXTRA.
    // The node is already going to hand over what it holds, so a second ask
    // would only fill the ring with commands whose answers are on their way.
    for (std::uint64_t i = 0; i < 5; ++i) {
        CHECK(client.receive(waiting, sizeof(waiting), at(11 + i)));
    }
    CHECK(!client.next_tx(frame));

    // THREE QUEUED MESSAGES, IN THE THREE FORMS A QUEUE CAN HOLD. The bug was
    // one missing request per accepted message, so every branch that accepts
    // one has to be walked or the fix is only proved for whichever form the
    // fixture happened to pick.
    //
    // RESP_CODE_CONTACT_MSG_RECV (7): key at 1, text type at 8, text at 13.
    std::uint8_t plain[32]{};
    plain[0] = 7;
    std::memcpy(&plain[1], peer.id.public_key.data(), 6);
    std::memcpy(&plain[13], "oldest", 6);
    CHECK(client.receive(plain, 19, at(20)));
    CHECK(std::strcmp(client.status().last_message.data(), "oldest") == 0);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(!client.next_tx(frame));

    // RESP_CODE_CONTACT_MSG_RECV_V3 (16): SNR at 1, key at 4, text at 16.
    std::uint8_t v3[32]{};
    v3[0] = 16;
    v3[1] = static_cast<std::uint8_t>(-8);
    std::memcpy(&v3[4], peer.id.public_key.data(), 6);
    std::memcpy(&v3[16], "middle", 6);
    CHECK(client.receive(v3, 22, at(21)));
    CHECK(std::strcmp(client.status().last_message.data(), "middle") == 0);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(!client.next_tx(frame));

    // RESP_CODE_CHANNEL_MSG_RECV_V3 (17): text at 11, no contact prefix.
    std::uint8_t channel[16]{};
    channel[0] = 17;
    channel[4] = 3;
    channel[6] = 0;
    std::memcpy(&channel[11], "room", 4);
    CHECK(client.receive(channel, sizeof(channel), at(25)));
    CHECK(std::strcmp(client.status().last_message.data(), "room") == 0);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    // The empty queue ends message polling. The independent, due battery
    // query must not be mistaken for another backlog request.
    const std::uint8_t no_more[] = {10};
    CHECK(client.receive(no_more, sizeof(no_more), at(26)));
    CHECK(!client.next_tx(frame));
    for (std::uint64_t ms = 500; ms <= 60000; ms += 500) {
        client.tick(at(ms));
    }
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 20);
    const std::uint8_t battery[] = {12, 0x74, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(client.receive(battery, sizeof(battery), at(60001)));
    CHECK(!client.next_tx(frame));

    // AND A PUSH AFTER THE DRAIN CLOSED STARTS A NEW ONE. Coalescing must not
    // outlive the drain it was protecting, or the first message of the next
    // backlog is the one that strands.
    CHECK(client.receive(waiting, sizeof(waiting), at(61000)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
}

// A drain belongs to its session. Reconnecting must ask again rather than wait
// for the answer to a request the old link carried away.
void test_a_reconnect_starts_a_new_drain()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshCoreFrame frame{};

    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(10)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    // The link drops with that request unanswered, and the answer never comes:
    // it left with the session.
    client.disconnected(at(11));

    // A NEW SESSION, DELIBERATELY NOT CARRIED AS FAR AS ITS CONTACT SYNC. The
    // handshake would ask for a message on its own account, which is why
    // reaching for it here would prove nothing: the request would go out
    // whether or not the dead session's state had been cleared. A push arriving
    // before the sync completes is the case that tells them apart -- a stale
    // `draining_` swallows it and the backlog waits for a drain that no longer
    // has a link under it.
    client.connected(at(12));
    while (client.next_tx(frame)) {
    }
    CHECK(client.receive(waiting, sizeof(waiting), at(13)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
}

// A frame this client cannot read ends the drain instead of provoking another
// request: a node answering every ask with one would otherwise trade frames
// with it for the life of the session.
void test_an_unreadable_message_does_not_spin_the_drain()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshCoreFrame frame{};

    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(10)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    // Short enough that `accept_message` refuses it.
    std::uint8_t truncated[12]{};
    truncated[0] = 16;   // V3 needs 16 bytes before any text; this is 12.
    CHECK(client.receive(truncated, sizeof(truncated), at(11)));
    CHECK(!client.next_tx(frame));

    // And the way back in is the next push, not a retry.
    CHECK(client.receive(waiting, sizeof(waiting), at(12)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
}

// THE ONE CLEARING PATH THAT DOES NOT NEED THE NODE'S ANSWER. The others all
// run in the dispatcher on a frame that arrived and was accepted, so a node
// that takes CMD_SYNC_NEXT_MESSAGE and answers nothing at all used to latch
// the coalescing on with no request outstanding -- and every later push was
// then swallowed, for the life of the session, by the flag that exists to make
// a burst of them cheap.
void test_a_drain_nobody_answers_expires()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshCoreFrame frame{};

    // The request goes out at 100 ms and this node never answers it.
    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(100)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    // ONE MILLISECOND SHORT OF THE BOUND, THE COALESCING STILL HOLDS -- and it
    // is the millisecond that matters. A tick a round number of seconds past
    // the deadline passes against a bound that is off by one second in either
    // direction, which is to say against a bound that was never checked.
    // 15099 is 100 + 15000 - 1.
    client.tick(at(15099));
    CHECK(client.receive(waiting, sizeof(waiting), at(15099)));
    CHECK(!client.next_tx(frame));

    // AT THE BOUND IT DOES NOT -- AND THE TICK THAT DROPS IT PAYS BACK THE
    // PUSH THE DRAIN SWALLOWED, without a further push arriving to prompt it.
    // That ordering is the point of the check: the sync below is the sweep
    // spending the 15099 push, so a `tick()` that dropped the flag and left
    // the bit set would leave `next_tx` empty here.
    client.tick(at(15100));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    // And a push arriving into the drain the sweep just started is coalesced
    // into it, exactly as one arriving into any other drain is.
    CHECK(client.receive(waiting, sizeof(waiting), at(15100)));
    // One due battery poll follows that sync; there is no second sync.
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 20);
    const std::uint8_t battery[] = {12, 0x74, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(client.receive(battery, sizeof(battery), at(15101)));
    CHECK(!client.next_tx(frame));
}

// A notification too long to copy is dropped before receive() ever sees it, so
// when it was the drain's answer nothing downstream can end the drain. Waiting
// out the deadline above would work and would cost the backlog fifteen seconds
// for a loss the client already knows about at the moment it happens.
void test_a_dropped_notification_ends_the_drain()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshCoreFrame frame{};

    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(100)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    client.drop_oversize_frame();
    CHECK(client.receive(waiting, sizeof(waiting), at(101)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
}

// WHAT THE COALESCING OWES, AND WHEN IT PAYS.
//
// A push arriving inside a drain costs nothing extra only because the request
// already out is going to bring back everything the node holds. That is true
// of a drain the node ends with RESP_CODE_NO_MORE_MESSAGES and true of no
// other kind -- and the other kinds are all reachable. Before the coalescing
// every push enqueued its own request and none of this mattered; after it, a
// push swallowed in one of those windows was a backlog nobody asked for again,
// because nothing in this repository establishes that a node re-announces a
// message it has already announced once.
//
// The counter-case -- a drain the node *does* end properly, which owes
// nothing -- is `test_a_queued_backlog_is_drained_to_the_end`: five pushes are
// swallowed there, and after RESP_CODE_NO_MORE_MESSAGES it ticks out to a
// minute and requires silence.
void test_a_push_swallowed_by_a_drain_is_paid_back()
{
    const std::uint8_t waiting[] = {0x83};
    MeshCoreFrame frame{};

    // AN ANSWER THIS BUILD CANNOT PARSE.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(waiting, sizeof(waiting), at(10)));
        CHECK(client.next_tx(frame));
        CHECK(frame.size == 1 && frame.bytes[0] == 10);
        // A second message reaches the node while that request is outstanding.
        // Swallowed by design -- the request out is going to fetch it.
        CHECK(client.receive(waiting, sizeof(waiting), at(11)));
        CHECK(!client.next_tx(frame));
        // Except that the request is answered with a frame `accept_message`
        // refuses, which ends the drain and fetches nothing.
        std::uint8_t truncated[12]{};
        truncated[0] = 16;  // V3 needs 16 bytes before any text; this is 12.
        CHECK(client.receive(truncated, sizeof(truncated), at(12)));
        CHECK(!client.next_tx(frame));
        // The worker calls tick() on every event and every poll timeout, and
        // that is where the swallowed push is paid back. Without it the
        // message waits for a push the node has already sent.
        client.tick(at(13));
        CHECK(client.next_tx(frame));
        CHECK(frame.size == 1 && frame.bytes[0] == 10);
        // ONCE, NOT ONCE A TICK. A bit spent by the request that goes out for
        // it cannot become a poll.
        for (std::uint64_t ms = 14; ms <= 60; ++ms) {
            client.tick(at(ms));
        }
        CHECK(!client.next_tx(frame));
    }

    // AN ANSWER THAT NEVER ARRIVES, which is the trigger measured on the
    // bench: a dropped RX event posts nothing to the provider at all.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(waiting, sizeof(waiting), at(100)));
        CHECK(client.next_tx(frame));
        CHECK(frame.size == 1 && frame.bytes[0] == 10);
        CHECK(client.receive(waiting, sizeof(waiting), at(101)));
        CHECK(!client.next_tx(frame));
        // The drain's own deadline is what ends it, so nothing is owed until
        // the millisecond it falls. 15099 is 100 + 15000 - 1.
        client.tick(at(15099));
        CHECK(!client.next_tx(frame));
        client.tick(at(15100));
        CHECK(client.next_tx(frame));
        CHECK(frame.size == 1 && frame.bytes[0] == 10);
    }

    // ONE THE TRANSPORT DROPPED BEFORE receive() COULD SEE IT.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(waiting, sizeof(waiting), at(100)));
        CHECK(client.next_tx(frame));
        CHECK(frame.size == 1 && frame.bytes[0] == 10);
        CHECK(client.receive(waiting, sizeof(waiting), at(101)));
        CHECK(!client.next_tx(frame));
        client.drop_oversize_frame();
        CHECK(!client.next_tx(frame));
        client.tick(at(102));
        CHECK(client.next_tx(frame));
        CHECK(frame.size == 1 && frame.bytes[0] == 10);
    }
}

// A REFUSED NODE IS NOT ASKED FOR ITS QUEUE, and `tick()` is a way to ask that
// `receive()`'s refusal does not cover.
void test_a_refused_node_is_not_asked_by_the_tick_sweep()
{
    MeshCoreCompanion client;
    client.pin(key_of(0x40));
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);
    CHECK(!client.next_tx(frame));

    // Two pushes before RESP_CODE_SELF_INFO. Nothing orders a node's pushes
    // against its answer to CMD_APP_START, so this is the node's choice and not
    // this client's. The first starts a drain; the second is coalesced into it
    // and remembered in `pending_push_`.
    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(3)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(client.receive(waiting, sizeof(waiting), at(4)));
    CHECK(!client.next_tx(frame));

    // And then the node says it is not the pinned one.
    std::uint8_t self[62]{};
    self[0] = 5;
    const core::MeshPeerId stranger = key_of(0x91);
    std::memcpy(&self[4], stranger.public_key.data(), core::kMeshPublicKeyBytes);
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(5)));
    CHECK(client.wrong_node());

    // The drain's answer never arrives, because `receive()` drops everything
    // this node sends from here on. The deadline ends the drain -- and the
    // remembered push must not then be spent on the node this watch refused.
    client.tick(at(15005));
    CHECK(!client.next_tx(frame));
    for (std::uint64_t ms = 15500; ms <= 60000; ms += 500) {
        client.tick(at(ms));
    }
    CHECK(!client.next_tx(frame));
}

void test_self_info_carries_the_node_identity()
{
    MeshCoreCompanion client;
    const core::MeshPeerId id = key_of(0x40);
    handshake_to_self_info(client, id);

    CHECK(client.status().has_node_id);
    CHECK(client.status().node_id == id);
    core::MeshPeerId read{};
    CHECK(client.node_id(read));
    CHECK(read == id);
    CHECK(!client.wrong_node());

    // The name is still read, and from the offset it was always read from --
    // the key sits in front of it, not over it.
    CHECK(std::strcmp(client.status().node_name.data(), "Node") == 0);

    // Unpinned, so the handshake goes on: CMD_DEVICE_QUERY is the next frame.
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 2 && frame.bytes[0] == 22);
}

void test_the_pinned_node_is_the_one_the_handshake_continues_with()
{
    MeshCoreCompanion client;
    const core::MeshPeerId id = key_of(0x40);
    client.pin(id);
    handshake_to_self_info(client, id);

    CHECK(!client.wrong_node());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 2 && frame.bytes[0] == 22);
}

void test_another_node_answers_and_the_handshake_stops_there()
{
    MeshCoreCompanion client;
    client.pin(key_of(0x40));
    handshake_to_self_info(client, key_of(0x91));

    // Said, not acted on. The frame was well formed, so it is not counted as
    // malformed; the session is not faulted; and nothing here closed the link.
    CHECK(client.wrong_node());
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().has_node_id);
    CHECK(client.status().node_id == key_of(0x91));

    // And nothing was asked of it. No CMD_DEVICE_QUERY means no contact sync
    // and no send: the wrong node is never reached, only identified.
    MeshCoreFrame frame{};
    CHECK(!client.next_tx(frame));
    CHECK(client.status().availability != Availability::Ready);

    // Nor is anything asked of it afterwards. The transport's terminate is
    // asynchronous and unenforced, so a refused node goes on sending in the
    // window -- and PUSH_CODE_MSG_WAITING used to enqueue CMD_SYNC_NEXT_MESSAGE
    // unconditionally, which is the watch asking a node it has just refused for
    // its queued messages.
    const std::uint8_t waiting[] = {0x83};
    CHECK(!client.receive(waiting, sizeof(waiting), at(5)));
    CHECK(!client.next_tx(frame));
    // Not malformed either: the frame is well formed and the node is behaving
    // normally. Counting it would put a refused node's ordinary traffic into
    // the statistic that means "somebody is sending us rubbish".
    CHECK(client.malformed_frames() == 0);
}

// The reverse of pin(), and what it has to be for #411: afterwards `pinned()`
// answers false -- not "the key reads back as zero", which would refuse every
// node -- the refusal the old pin caused is gone with it, and the next node to
// answer SELF_INFO is adopted rather than compared.
void test_unpin_clears_the_pin_and_the_refusal_it_caused()
{
    MeshCoreCompanion client;
    client.pin(key_of(0x40));
    handshake_to_self_info(client, key_of(0x91));
    CHECK(client.wrong_node());
    CHECK(client.status().has_refused && client.status().has_pinned);

    CHECK(client.unpin());
    core::MeshPeerId out{};
    CHECK(!client.pinned(out));
    CHECK(!client.wrong_node());
    CHECK(!client.status().has_pinned);
    CHECK(!client.status().has_refused);
    // Nothing to clear the second time, and it says so.
    CHECK(!client.unpin());

    // The session that follows is with whichever node answers, and the
    // handshake goes on past its identity: CMD_DEVICE_QUERY is the next frame.
    client.disconnected(at(10));
    client.begin(at(11));
    handshake_to_self_info(client, key_of(0x91));
    CHECK(!client.wrong_node());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 2 && frame.bytes[0] == 22);
}

void test_the_pin_outlives_the_session_and_the_identity_does_not()
{
    MeshCoreCompanion client;
    const core::MeshPeerId id = key_of(0x40);
    client.pin(id);
    handshake_to_self_info(client, key_of(0x91));
    CHECK(client.wrong_node());

    // Both halves of what the mesh screen shows: which node was turned away and
    // which one this watch wants.
    CHECK(client.status().has_refused);
    CHECK(client.status().refused_id == key_of(0x91));
    CHECK(client.status().has_pinned);
    CHECK(client.status().pinned_id == id);

    client.disconnected(at(10));
    client.begin(at(11));
    client.peer_arriving(at(12));
    client.connected(at(13));

    // A reconnect starts with no answer to "which node is this", so a poll
    // between the connection and the next SELF_INFO cannot read the previous
    // node's verdict as this one's.
    CHECK(!client.status().has_node_id);
    CHECK(!client.wrong_node());

    // ...and the refusal does NOT reset with it. A refused watch has no session
    // at all, so every session-scoped field on the mesh screen is empty exactly
    // when the refusal is the only thing that explains the screen.
    CHECK(client.status().has_refused);
    CHECK(client.status().refused_id == key_of(0x91));
    CHECK(client.status().has_pinned);

    core::MeshPeerId still{};
    CHECK(client.pinned(still));
    CHECK(still == id);

    // ...and the pin still decides, on a node that is now the right one.
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);
    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[4], id.public_key.data(), core::kMeshPublicKeyBytes);
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(14)));
    CHECK(!client.wrong_node());
    // The pinned node answered, so the refusal is no longer what stands between
    // this watch and its mesh, and the screen stops saying it does.
    CHECK(!client.status().has_refused);
    CHECK(client.status().has_pinned);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 2 && frame.bytes[0] == 22);
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
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
}

void test_send_and_receive()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}).accepted());
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
    CHECK(client.send_room(room, canary, "Hello", WallTime{1000}).accepted());

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

    // Cancellation must erase an unsent login too. A shorter request then
    // reuses that same ring slot; inspect its full storage, not just its size.
    MeshCoreCompanion cancelled;
    connect_and_handshake(cancelled);
    const std::uint8_t receiver_hint[] = {21};
    CHECK(cancelled.receive(receiver_hint, sizeof(receiver_hint), at(8)));
    CHECK(cancelled.send_room(room, canary, "never sent", WallTime{1000}).accepted());
    cancelled.tick(at(100));
    cancelled.tick(at(15100));
    CHECK(cancelled.status().delivery == MeshDelivery::Unknown);
    CHECK(!cancelled.send_busy());
    CHECK(cancelled.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 20);
    for (std::size_t i = frame.size; i < frame.bytes.size(); ++i) {
        CHECK(frame.bytes[i] == 0);
    }

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
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}).accepted());
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
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
    CHECK(!client.send_room(room, "0123456789abcdef", "Hello", WallTime{1000}).accepted());

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
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
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
    CHECK(!client.send_private(peer.id, "no", WallTime{1000}).accepted());
    std::uint8_t frame = 13;
    CHECK(!client.receive(&frame, 1, at(10)));
}

// The frames a hostile or broken node can put on the wire, at the seam the BLE
// transport actually hands over.
//
// `docs/research/MESHCORE_COMPANION_PROTOCOL.md:175-177` -- "no chunking and
// no reassembly code" -- is the reason this is one test rather
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
    // (`docs/research/MESHCORE_BLE_FRAME_CAPACITY.md:31` -- "**Protocol /
    // buffer maximum**"); on nRF52 the buffer binds and not
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
    // (`docs/research/MESHCORE_COMPANION_PROTOCOL.md:177` -- "own first byte
    // is the command or response code"). One this build does not know is
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

    // Truncated: every frame this client reads at a fixed offset, one byte
    // short of the length its own reader requires. Each is refused before the
    // read. Responses and unsolicited pushes alike -- `0x82` arrives without
    // being asked for, which makes it *more* exposed to a truncation than a
    // response is, not less, and it was the one shape missing here (#478).
    const struct { std::uint8_t code; std::size_t minimum; } truncated[] = {
        {13, 82},   // device info
        {5, 58},    // self info
        {2, 5},     // contacts start
        {3, 148},   // contact
        {4, 5},     // contacts end
        {16, 13},   // contact message
        {0x84, 16}, // contact message v3
        {0x82, 5},  // send confirmed: opcode and the four ack bytes
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
    CHECK(service.send_private(peer.id, "first", WallTime{1000}).accepted());
    CHECK(client.send_busy());

    // Before RESP_CODE_SENT. Neither a private nor a Room send may start.
    CHECK(!service.send_private(peer.id, "second", WallTime{1001}).accepted());
    CHECK(!client.send_room(room, "password", "second", WallTime{1001}).accepted());

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(std::memcmp(&frame.bytes[13], "first", 5) == 0);
    CHECK(!client.next_tx(frame));  // and nothing was queued behind it

    // Between RESP_CODE_SENT and the confirmation is the window the old code
    // released the slot in: `expected_ack_` is spoken for, and a second send
    // would overwrite it and leave this operation with no way to reach a
    // verdict. est_timeout is 0x0966 = 2406 ms, the round trip the T114
    // ESTIMATED for this send -- the measured one was 720 ms, and the two are
    // different claims.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(client.send_busy());
    CHECK(!service.send_private(peer.id, "second", WallTime{1002}).accepted());
    CHECK(!client.send_room(room, "password", "second", WallTime{1002}).accepted());

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
    CHECK(service.send_private(peer.id, "third", WallTime{1003}).accepted());
    CHECK(client.send_busy());
    CHECK(client.receive(ack, sizeof(ack), at(11)));
    CHECK(service.status().delivery == MeshDelivery::Queued);
    CHECK(client.send_busy());
}

// #478: a PUSH_CODE_SEND_CONFIRMED too short to hold the ack it exists to
// carry. The table in test_hostile_frames_are_bounded_and_the_session_survives
// now covers the refusal; what it cannot see is the half of the defect that
// made it worth fixing -- the frame was silently accepted *while an operation
// was in flight*, so a truncation a third party on the air can produce left the
// single send slot claimed with no trace in the only diagnostic counter there
// is. The counting and the lifecycle are asserted together here because it was
// the pair that was wrong: the frame vanished, and the send it did not answer
// waited out its budget for it.
void test_a_short_send_confirmed_is_refused_and_the_send_still_lives()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    CHECK(service.send_private(peer.id, "first", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    // est_timeout 0x0966 = 2406 ms -- the node's own ESTIMATE of the round
    // trip, captured on the T114 and not a measurement of one. The ack this
    // operation is now waiting for is 01 02 03 04.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(client.send_busy());

    std::uint32_t expected = client.malformed_frames();

    // Every truncation, including the one carrying three of the four ack bytes
    // correctly -- a prefix of the right answer is not the right answer, and
    // the guard is the frame's shape rather than how much of it looks familiar.
    const std::uint8_t short_ack[] = {0x82, 1, 2, 3};
    for (std::size_t size = 1; size <= sizeof(short_ack); ++size) {
        CHECK(!client.receive(short_ack, size, at(9)));
        // Exactly one, per frame: not one per missing byte, and not none.
        CHECK(client.malformed_frames() == ++expected);
        CHECK(service.status().delivery == MeshDelivery::Accepted);
        // And the malformed frame did not end somebody else's operation. The
        // slot is released by the budget, a disconnect or a matching ack, and
        // a frame we refused to read is none of the three.
        CHECK(client.send_busy());
    }

    // Availability is asked through a tick on purpose. The refusal path returns
    // before `update_availability()`, so reading the field straight after a
    // refused frame reports what the handshake left there; the tick recomputes
    // it, which is what makes this an assertion about the session rather than
    // about a cached value. At ms 10 against an operation answered at ms 8 the
    // 2406 ms budget cannot have expired, so the tick decides nothing else.
    client.tick(at(10));
    CHECK(client.status().availability == Availability::Ready);
    CHECK(client.send_busy());
    CHECK(client.malformed_frames() == expected);

    // The session is not merely alive, it is still correlating: the ack this
    // send has been waiting for all along still confirms it.
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(11)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);
    CHECK(!client.send_busy());
    CHECK(client.malformed_frames() == expected);

    // The bound is a floor and not an equality. The bench transcript's own
    // confirmation was nine bytes --
    // `docs/research/MESHCORE_T114_FIRST_CONTACT.md:298`
    // "PUSH_CODE_SEND_CONFIRMED  82 38 66 6c b8 1b 03 00 00" -- so four bytes
    // this build does not read follow the ack on real hardware, and calling
    // them malformed would refuse every confirmation the T114 sends.
    CHECK(service.send_private(peer.id, "second", WallTime{1001}).accepted());
    CHECK(client.next_tx(frame));
    CHECK(client.receive(sent, sizeof(sent), at(12)));
    const std::uint8_t trailing[] = {0x82, 1, 2, 3, 4, 0x1b, 3, 0, 0};
    CHECK(client.receive(trailing, sizeof(trailing), at(13)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);
    CHECK(!client.send_busy());
    CHECK(client.malformed_frames() == expected);

    // A well-formed ack for a message this client never sent stays what it
    // always was -- a correlation outcome, not a length error. Nothing is
    // counted against the node for it.
    CHECK(service.send_private(peer.id, "third", WallTime{1002}).accepted());
    CHECK(client.next_tx(frame));
    CHECK(client.receive(sent, sizeof(sent), at(14)));
    const std::uint8_t other_ack[] = {0x82, 9, 9, 9, 9};
    CHECK(client.receive(other_ack, sizeof(other_ack), at(15)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(client.send_busy());
    CHECK(client.malformed_frames() == expected);

    // And the shape is checked before there is anything to correlate against,
    // because that is the order the counter has to be right in: a truncated
    // push arriving with no send in flight is still a frame we could not read.
    CHECK(client.receive(ack, sizeof(ack), at(16)));
    CHECK(!client.send_busy());
    CHECK(!client.receive(short_ack, sizeof(short_ack), at(17)));
    CHECK(client.malformed_frames() == ++expected);
    client.tick(at(18));
    CHECK(client.status().availability == Availability::Ready);
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

    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
    CHECK(client.send_busy());
    CHECK(!service.send_private(peer.id, "cuts in", WallTime{1001}).accepted());

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
    CHECK(!service.send_private(peer.id, "cuts in", WallTime{1002}).accepted());

    CHECK(client.receive(sent, sizeof(sent), at(10)));
    CHECK(client.send_busy());
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(11)));
    CHECK(client.status().delivery == MeshDelivery::Confirmed);
    CHECK(!client.send_busy());
    CHECK(service.send_private(peer.id, "now it may", WallTime{1003}).accepted());
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
        // The handshake's own CMD_GET_CUSTOM_VARS is answered before anything
        // else, and it has to be. It goes out at END_OF_CONTACTS, so it is the
        // *older* outstanding command here, and an untagged RESP_CODE_ERR is
        // its before it is the login's. Leaving it open would quietly turn this
        // into a test of the attribution rule instead of the one thing it is
        // for. Answering it first is not a workaround: a node that defines
        // opcode 40 does exactly this.
        {
            const std::uint8_t vars[] = {21};
            CHECK(client.receive(vars, sizeof(vars), at(7)));
        }
        CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
        const std::uint8_t error[] = {1, 2};
        CHECK(client.receive(error, sizeof(error), at(8)));
        CHECK(client.status().delivery == MeshDelivery::Refused);
        CHECK(!client.send_busy());
        // The private path is not down with it. This is the assertion the
        // reproduction in the review turns on.
        CHECK(service.send_private(peer.id, "still works", WallTime{1001}).accepted());
    }

    // The node queues the login and the room never answers: no LOGIN_SUCCESS
    // and no LOGIN_FAIL, ever. The estimate in RESP_CODE_SENT is the budget --
    // 0x0966 = 2406 ms -- and the login's own SENT used to discard it.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        client.tick(at(8 + 2405));
        CHECK(client.send_busy());
        client.tick(at(8 + 2406));
        CHECK(client.status().delivery == MeshDelivery::Unknown);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "still works", WallTime{1001}).accepted());
    }

    // The node answers nothing at all -- not even RESP_CODE_SENT -- so there is
    // no estimate to run on. kMaxAckWait is the budget, armed on the first tick
    // that sees the operation.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
        client.tick(at(8));
        client.tick(at(8 + 14999));
        CHECK(client.send_busy());
        client.tick(at(8 + 15000));
        CHECK(client.status().delivery == MeshDelivery::Unknown);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "still works", WallTime{1002}).accepted());
    }

    // The same absence one state over: a CMD_SEND_TXT_MSG the node takes over
    // BLE and answers with nothing wedges the slot identically, and a fix that
    // covered only the login would leave it.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        client.tick(at(8));
        client.tick(at(8 + 14999));
        CHECK(client.send_busy());
        client.tick(at(8 + 15000));
        CHECK(client.status().delivery == MeshDelivery::Unknown);
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
        CHECK(service.send_private(peer.id, "one", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(100)));  // budget 2406 ms
        const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
        CHECK(client.receive(ack, sizeof(ack), at(101)));
        CHECK(!client.send_busy());
        // No tick between the two. The second send is queued and then seen for
        // the first time well past the first one's deadline.
        CHECK(service.send_private(peer.id, "two", WallTime{1001}).accepted());
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
        CHECK(service.send_private(peer.id, "one", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(100)));
        const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
        CHECK(client.receive(ack, sizeof(ack), at(101)));
        CHECK(client.send_room(room, "password", "Hello", WallTime{1001}).accepted());
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
        // The handshake's own CMD_GET_CUSTOM_VARS is answered before anything
        // else, and it has to be. It goes out at END_OF_CONTACTS, so it is the
        // *older* outstanding command here, and an untagged RESP_CODE_ERR is
        // its before it is the send's. Leaving it open would quietly turn this
        // into a test of the attribution rule instead of the one thing it is
        // for. Answering it first is not a workaround: a node that defines
        // opcode 40 does exactly this.
        {
            const std::uint8_t vars[] = {21};
            CHECK(client.receive(vars, sizeof(vars), at(7)));
        }
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        const std::uint8_t error[] = {1, 4};
        CHECK(client.receive(error, sizeof(error), at(8)));
        CHECK(service.status().delivery == MeshDelivery::Refused);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "again", WallTime{1001}).accepted());
    }

    // An explicit RESP_CODE_ERR after RESP_CODE_SENT. This half used to be
    // unreachable: the slot was already free, so the error had nothing to end.
    //
    // The custom-vars answer comes first here, and it has to: with that request
    // still outstanding this error is the receiver hint's and not the send's,
    // because RESP_CODE_SENT already answered the send. That is a case of its
    // own -- test_a_custom_vars_error_does_not_fail_an_accepted_send. What this
    // one pins is the other node: nothing else is owed a response, so an
    // unattributable error fails the operation rather than vanishing.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        const std::uint8_t vars[] = {21};
        CHECK(client.receive(vars, sizeof(vars), at(7)));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        const std::uint8_t error[] = {1, 4};
        CHECK(client.receive(error, sizeof(error), at(9)));
        CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
        CHECK(!client.send_busy());
    }

    // The node's own estimate runs out. 0x0966 = 2406 ms from at(8).
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        client.tick(at(8 + 2405));
        CHECK(service.status().delivery == MeshDelivery::Accepted);
        CHECK(client.send_busy());
        client.tick(at(8 + 2406));
        CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "again", WallTime{1001}).accepted());
    }

    // A node that reports no estimate at all does not fail a send that is
    // merely fast: the budget has a floor of one second.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
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
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        const std::uint8_t forever[] = {6, 0, 1, 2, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF};
        CHECK(client.receive(forever, sizeof(forever), at(8)));
        client.tick(at(8 + 15000));
        CHECK(!client.send_busy());
    }

    // The link dropping ends it too, from either phase, and the session that
    // follows starts with the slot free rather than with the dead one's.
    //
    // AND THE VERDICT IS `Unknown`, WHICH IS THE WHOLE OF ADR-0023 DECISION 5.
    // It was `None`, which renders as *"not sent"* -- for a message this client
    // had put in its transmit ring and, in the `after_response` half, one the
    // node had answered RESP_CODE_SENT for. A frame that has left the ring may
    // already have been written to the characteristic and this object cannot
    // tell that from one still queued behind it, so "not sent" was a claim it
    // had no way to make. The difference matters to the owner and not only to
    // the vocabulary: "not sent" invites a resend, and `Unknown` says the first
    // attempt may already have gone.
    for (const bool after_response : {false, true}) {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        if (after_response) CHECK(client.receive(sent, sizeof(sent), at(8)));
        CHECK(client.send_busy());
        client.disconnected(at(9));
        CHECK(!client.send_busy());
        CHECK(client.status().delivery == MeshDelivery::Unknown);
    }
}

// RESP_CODE_ERR carries no opcode, so who it belongs to is decided by what we
// know about the order -- and `send_busy()` was never that. It stays true
// through the confirmation wait, which begins *because* the node already
// answered the send with RESP_CODE_SENT. An error arriving then is somebody
// else's, and on a node too old for opcode 40 it always arrived then: BLE
// delivers it in milliseconds while PUSH_CODE_SEND_CONFIRMED needs a radio
// round trip (720 ms MEASURED on the T114).
void test_a_custom_vars_error_does_not_fail_an_accepted_send()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);  // leaves CMD_GET_CUSTOM_VARS outstanding
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}).accepted());

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);

    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 10, 0, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);

    // The node refuses opcode 40 while the confirmation is still in the air.
    const std::uint8_t err[] = {1, 0};
    CHECK(client.receive(err, sizeof(err), at(9)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(client.send_busy());

    // And the confirmation the old code discarded still lands.
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(10)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);
    CHECK(!client.send_busy());

    // A well-formed RESP_CODE_SENT is not counted against the node either.
    CHECK(client.malformed_frames() == 0);
}

// A node that answers CMD_GET_CUSTOM_VARS with nothing at all must not keep the
// right to absorb somebody else's error for the rest of the session. Without
// the bound this send stays Accepted forever: the error is handed to a request
// that is never going to be answered, and the confirmation never comes either.
void test_an_unanswered_custom_vars_request_stops_taking_the_blame()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);  // opcode 40 goes out at at(7)
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    client.tick(at(7 + 14999));
    client.tick(at(7 + 15000));  // kMaxAckWait; the request is given up on

    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);

    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 10, 0, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(7 + 15001)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);

    // Past the confirmation boundary, so nothing is awaiting a *response* -- the
    // window the receiver hint would have taken this error in.
    const std::uint8_t err[] = {1, 4};
    CHECK(client.receive(err, sizeof(err), at(7 + 15002)));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
    CHECK(!client.send_busy());
    CHECK(client.status().availability == Availability::Ready);
}

// The failure the round-2 review reproduced, in both directions. A node too old
// for CMD_GET_CUSTOM_VARS refuses it with an error that names nothing, and the
// refusal crosses BLE in milliseconds while our own command is still waiting on
// a radio round trip. Opcode 40 went out at END_OF_CONTACTS, before either
// operation below, so it is the older outstanding command and the error is its.
//
// Charging it to the operation instead is not a lost race, it is the ordinary
// case on such a node: the room text is discarded, the LOGIN_SUCCESS that
// follows lands on an operation that no longer exists and is counted malformed,
// and a private message the node had accepted is reported Failed.
void test_an_old_node_refusing_opcode_40_does_not_fail_a_room_login()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);  // CMD_GET_CUSTOM_VARS goes out at at(7)

    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) {
        room[i] = static_cast<std::uint8_t>(0x40 + i);
    }
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 41 && frame.bytes[0] == 26);

    const std::uint8_t error[] = {1, 1};  // ERR_CODE_UNSUPPORTED_CMD
    CHECK(client.receive(error, sizeof(error), at(8)));

    // Untouched: still owed an answer, still holding the one slot.
    CHECK(client.status().delivery == MeshDelivery::Queued);
    CHECK(client.send_busy());

    std::uint8_t login_ok[] = {0x85, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&login_ok[2], room.data(), 6);
    CHECK(client.receive(login_ok, sizeof(login_ok), at(9)));

    // And the text the login was carrying reaches the wire.
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);
    CHECK(std::memcmp(&frame.bytes[13], "Hello", 5) == 0);
    CHECK(client.malformed_frames() == 0);
}

// The other half of the same rule, and the half order cannot reach. `send_room`
// waits only for the device and self info -- not for the contacts iteration --
// so a login can be queued *during* the burst, ahead of the CMD_GET_CUSTOM_VARS
// that RESP_CODE_END_OF_CONTACTS enqueues behind it. Now the outstanding
// operation is the older command, and the sequence comparison points the wrong
// way: on order alone the untagged error is the login's.
//
// What settles it is that the node has already answered the login. RESP_CODE_SENT
// arrived, so nothing is owed on that command any more and the only claimant
// left is opcode 40. Delete `&& !op_answered_` from `op_owed_an_answer()` and
// this test is the one that fails: the room text is wiped before it is ever
// transmitted, and the LOGIN_SUCCESS behind it is counted malformed.
void test_an_answered_login_does_not_take_a_later_opcode_40s_error()
{
    MeshCoreCompanion client;
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);

    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(3)));
    CHECK(client.next_tx(frame));

    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    CHECK(client.receive(device, sizeof(device), at(4)));
    CHECK(client.next_tx(frame));

    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(5)));

    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(client.receive(contact, sizeof(contact), at(6)));

    // Mid-burst, which is the whole premise: this is queued before opcode 40.
    std::array<std::uint8_t, 32> room{};
    for (std::size_t i = 0; i < room.size(); ++i) {
        room[i] = static_cast<std::uint8_t>(0x40 + i);
    }
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}).accepted());
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 41 && frame.bytes[0] == 26);

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(7)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 40);

    // The node answers the login: an estimate for the round trip, and with it
    // the fact that this command has been dealt with.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 10, 0, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8)));

    // Then it refuses opcode 40, naming nothing.
    const std::uint8_t error[] = {1, 1};  // ERR_CODE_UNSUPPORTED_CMD
    CHECK(client.receive(error, sizeof(error), at(9)));
    // The send is untouched, and `Queued` is what untouched looks like here:
    // the RESP_CODE_SENT above answered CMD_SEND_LOGIN, not a text, so it
    // publishes no `Accepted`. Written as the state rather than as "not the
    // failure state", which was one value when this line was first written and
    // is three now.
    CHECK(client.status().delivery == MeshDelivery::Queued);
    CHECK(client.send_busy());

    std::uint8_t login_ok[] = {0x85, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&login_ok[2], room.data(), 6);
    CHECK(client.receive(login_ok, sizeof(login_ok), at(10)));

    CHECK(client.next_tx(frame));
    CHECK(frame.size == 18 && frame.bytes[0] == 2);
    CHECK(std::memcmp(&frame.bytes[13], "Hello", 5) == 0);
    CHECK(client.malformed_frames() == 0);
}

void test_an_old_node_refusing_opcode_40_does_not_fail_a_queued_send()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}).accepted());

    const std::uint8_t error[] = {1, 1};
    CHECK(client.receive(error, sizeof(error), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Queued);
    CHECK(client.send_busy());

    // The node answers the send itself next, in the order it was asked, and the
    // operation runs to its own terminal outcome.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 10, 0, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(9)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(ack, sizeof(ack), at(10)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);
    CHECK(client.malformed_frames() == 0);
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

// Remote CLI output (#627): consumed so the drain continues, but neither shown
// as the last message nor read for a coordinate, even one in the grammar.
void test_cli_data_is_neither_a_message_nor_a_coordinate()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    MeshCoreFrame frame{};

    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(10)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);

    std::uint8_t human[32]{};
    human[0] = 16;
    std::memcpy(&human[4], peer.id.public_key.data(), 6);
    std::memcpy(&human[16], "Human", 5);
    CHECK(client.receive(human, 21, at(11)));
    CHECK(client.next_tx(frame));

    const char cli_text[] = "status @12.3456,65.4321";
    std::uint8_t cli[16 + sizeof(cli_text)]{};
    cli[0] = 16;
    cli[1] = static_cast<std::uint8_t>(-40);
    // From a prefix no contact matches, so a sender written before the CLI
    // check would clear the name on screen rather than repeat it (#678).
    for (std::size_t i = 0; i < 6; ++i) cli[4 + i] = 0xEE;
    cli[11] = 1;  // TXT_TYPE_CLI_DATA
    std::memcpy(&cli[16], cli_text, sizeof(cli_text) - 1);
    CHECK(client.receive(cli, sizeof(cli) - 1, at(12)));
    CHECK(std::strcmp(client.status().last_message.data(), "Human") == 0);
    CHECK(std::strcmp(client.status().last_sender.data(), "Peer") == 0);
    CHECK(client.status().snr_quarter_db == 0);
    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.cli_frames() == 1);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    // Again from the peer, whose prefix resolves, so the coordinate in it
    // would land if the CLI check did not guard it (#678).
    std::memcpy(&cli[4], peer.id.public_key.data(), 6);
    CHECK(client.receive(cli, sizeof(cli) - 1, at(12)));
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(client.cli_frames() == 2);
    CHECK(client.next_tx(frame));

    // The legacy shape, code 7: `path_len` at 7, `txt_type` at 8. A one-hop
    // message has `path_len` 1, so a type read one byte early would drop it.
    std::uint8_t legacy[32]{};
    legacy[0] = 7;
    std::memcpy(&legacy[1], peer.id.public_key.data(), 6);
    legacy[7] = 1;
    std::memcpy(&legacy[13], "OneHop", 6);
    CHECK(client.receive(legacy, 19, at(13)));
    CHECK(std::strcmp(client.status().last_message.data(), "OneHop") == 0);
    CHECK(client.next_tx(frame));

    std::uint8_t legacy_cli[13 + sizeof(cli_text)]{};
    legacy_cli[0] = 7;
    for (std::size_t i = 0; i < 6; ++i) legacy_cli[1 + i] = 0xEE;
    legacy_cli[8] = 1;  // TXT_TYPE_CLI_DATA
    std::memcpy(&legacy_cli[13], cli_text, sizeof(cli_text) - 1);
    CHECK(client.receive(legacy_cli, sizeof(legacy_cli) - 1, at(14)));
    CHECK(std::strcmp(client.status().last_message.data(), "OneHop") == 0);
    CHECK(std::strcmp(client.status().last_sender.data(), "Peer") == 0);
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.cli_frames() == 3);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    std::memcpy(&legacy_cli[1], peer.id.public_key.data(), 6);
    CHECK(client.receive(legacy_cli, sizeof(legacy_cli) - 1, at(14)));
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(client.cli_frames() == 4);
    CHECK(client.next_tx(frame));
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

// A FAULT IS NOT A NODE THE WEARER PICKED WRONG, AND THIS IS THE PATH THAT
// USED TO SAY IT WAS.
//
// The two halves of this file's subject meet here: the provider decides what
// is true and `apps::format_mesh()` decides what a wearer is told, and the
// defect lived in neither on its own. `reset_session()` keeps the refusal
// across a disconnect on purpose, which is right; the formatter ranked that
// retained refusal above every phase, which meant a watch whose transport had
// since faulted was told to go and select a different node -- an instruction
// that cannot clear a fault. Driven through the shipping calls rather than by
// assigning to a `MeshStatus`, because a hand-built status is exactly the
// fixture that would have agreed with the old code.
void test_a_terminal_fault_outranks_a_refusal_the_session_kept()
{
    MeshCoreCompanion client;
    client.pin(key_of(0x40));
    handshake_to_self_info(client, key_of(0x91));
    CHECK(client.wrong_node());
    CHECK(client.status().has_refused && client.status().has_pinned);

    client.fault(at(20));

    // What the provider says: the transport is done, and the evidence of the
    // refusal is still there to be read. Both are deliberate.
    const core::MeshStatus faulted = client.status();
    CHECK(faulted.availability == Availability::Failed);
    CHECK(faulted.transport == attadipa::core::TransportPhase::Faulted);
    CHECK(faulted.has_refused && faulted.has_pinned);

    // What the wearer is told, in both languages: the fault, and the note that
    // says a reset rather than a retry -- not "hold the clock to change it",
    // which is the way out of a refusal and does nothing to a faulted link.
    for (auto locale : {attadipa::l10n::Locale::En, attadipa::l10n::Locale::Ru}) {
        const attadipa::apps::MeshText text =
            attadipa::apps::format_mesh(faulted, locale);
        CHECK(text.link == attadipa::apps::MeshLink::Broken);
        CHECK(text.note[0] != '\0');
        CHECK(text.way_out[0] == '\0');
    }
}

// And the window the refusal exists for is untouched. An ordinary disconnect
// is what a refusal causes -- the watch turned the only node in range away, so
// there is no session -- and there the refusal is still the whole explanation
// for a screen with nothing on it.
void test_an_ordinary_disconnect_still_reports_the_refusal()
{
    MeshCoreCompanion client;
    client.pin(key_of(0x40));
    handshake_to_self_info(client, key_of(0x91));
    client.disconnected(at(20));

    const core::MeshStatus dropped = client.status();
    CHECK(dropped.availability != Availability::Failed);
    CHECK(dropped.transport != attadipa::core::TransportPhase::Faulted);

    const attadipa::apps::MeshText text =
        attadipa::apps::format_mesh(dropped, attadipa::l10n::Locale::En);
    CHECK(text.link == attadipa::apps::MeshLink::TurnedAway);
    CHECK(text.way_out[0] != '\0');
    // Both keys, because a wearer cannot act on "some other node answered".
    CHECK(std::strstr(text.pinned, "40414243") != nullptr);
    CHECK(std::strstr(text.answered, "91929394") != nullptr);
}

}  // namespace

// The length guard the coordinate rides on, and the case the suite did not
// have. `size < 58` drops a RESP_CODE_SELF_INFO shorter than the name offset
// before anything reads it, so bytes 36-43 are present in every frame a
// position provider is ever handed -- which is why the provider's own tests do
// not repeat this check and why it has to exist here instead. The suite already
// failed closed on a short *contact* frame and had no short self-info case.
void test_a_short_self_info_is_refused_before_anything_reads_it()
{
    MeshCoreCompanion client;
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));

    // One byte short of the name offset: the public key and the coordinate are
    // both fully present, and it is still refused. The bound is the frame's
    // shape, not the fields this build happens to read.
    std::uint8_t truncated[57]{};
    truncated[0] = 5;
    for (std::size_t i = 0; i < core::kMeshPublicKeyBytes; ++i) {
        truncated[4 + i] = static_cast<std::uint8_t>(i + 1);
    }
    CHECK(!client.receive(truncated, sizeof(truncated), at(3)));
    CHECK(client.malformed_frames() == 1);
    CHECK(!client.status().has_node_id);
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(!client.node_position(position, arrived));
    // And the handshake did not continue: no CMD_DEVICE_QUERY went out.
    CHECK(!client.next_tx(frame));
}

void test_attached_node_battery_uses_the_live_queue_and_public_status()
{
    const std::uint8_t hint[] = {21};
    const std::uint8_t voltage[] = {12, 0x74, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::uint8_t zero[] = {12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::uint8_t error[] = {1, 1};
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0xe8, 3, 0, 0};
    const std::uint8_t confirmed[] = {0x82, 1, 2, 3, 4, 0, 0, 0, 0};
    MeshCoreFrame frame{};

    // Real handshake -> poll -> dispatcher -> app-facing MeshService snapshot.
    // A second consumer does not own a timer or cause another query.
    {
        MeshCoreCompanion client;
        MeshService screen(client), other_screen(client);
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        CHECK(screen.status().node_battery.separate_supply);
        CHECK(screen.status().node_battery.validity == core::Validity::Unknown);
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.size == 1 && frame.bytes[0] == 20);
        CHECK(!client.next_tx(frame));
        std::uint8_t extended[sizeof(voltage) + 1]{};
        std::memcpy(extended, voltage, sizeof(voltage));
        extended[sizeof(voltage)] = 0xff; // opaque extension, not a charge flag
        CHECK(client.receive(extended, sizeof(extended), at(11)));
        CHECK(client.malformed_frames() == 0);
        CHECK(screen.status().node_battery.millivolts == 3700);
        CHECK(screen.status().node_battery.validity == core::Validity::Valid);
        CHECK(screen.status().node_battery.received_at.ms == 11);
        CHECK(other_screen.status().node_battery.received_at.ms == 11);
        CHECK(!client.next_tx(frame));
        client.tick(at(60009));
        CHECK(!client.next_tx(frame));
        client.tick(at(60010));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.receive(zero, sizeof(zero), at(60011)));
        CHECK(screen.status().node_battery.validity == core::Validity::Stale);
        CHECK(screen.status().node_battery.millivolts == 3700);
        CHECK(screen.status().node_battery.received_at.ms == 11);
        client.tick(at(120010));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(!client.receive(voltage, sizeof(voltage) - 1, at(120011)));
        CHECK(screen.status().node_battery.received_at.ms == 11);
        client.tick(at(180010));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.receive(voltage, sizeof(voltage), at(180011)));
        client.tick(at(360011));
        CHECK(screen.status().node_battery.validity == core::Validity::Stale);
        CHECK(screen.status().node_battery.received_at.ms == 180011);
        client.disconnected(at(360012));
        CHECK(screen.status().node_battery.separate_supply);
        CHECK(screen.status().node_battery.validity == core::Validity::Unknown);
        CHECK(screen.status().node_battery.millivolts == 0);
        CHECK(screen.status().node_battery.received_at.ms == 0);
        CHECK(!client.next_tx(frame));
    }

    // A foreground send prevents a due poll through its radio confirmation.
    // A prompt poll refusal cannot be mistaken for the send queued behind it.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        MeshPeer peer{};
        CHECK(client.peer(0, peer));
        CHECK(client.send_private(peer.id, "first", WallTime{1}).accepted());
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(sent, sizeof(sent), at(11)));
        CHECK(!client.next_tx(frame));
        CHECK(client.receive(confirmed, sizeof(confirmed), at(12)));
        client.tick(at(13));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.send_private(peer.id, "second", WallTime{2}).accepted());
        client.tick(at(14));
        CHECK(!client.next_tx(frame));
        CHECK(client.receive(error, sizeof(error), at(15)));
        CHECK(client.status().delivery == MeshDelivery::Queued);
        CHECK(client.send_busy());
        client.tick(at(16));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(17)));
        CHECK(client.status().delivery == MeshDelivery::Refused);
        CHECK(!client.send_busy());
    }

    // Timeout frees transmission, not the ability to attribute a late ERR.
    // Typed confirmation still succeeds; a genuinely unanswered send still
    // fails by its existing deadline, even after a later successful poll.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        MeshPeer peer{};
        CHECK(client.peer(0, peer));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.send_private(peer.id, "after poll", WallTime{1}).accepted());
        client.tick(at(12));
        CHECK(!client.next_tx(frame));
        client.tick(at(5010));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(5011)));
        CHECK(client.status().delivery == MeshDelivery::Queued);
        CHECK(client.receive(sent, sizeof(sent), at(5012)));
        CHECK(client.receive(error, sizeof(error), at(5013)));
        CHECK(client.status().delivery == MeshDelivery::Accepted);
        CHECK(client.receive(confirmed, sizeof(confirmed), at(5014)));
        CHECK(client.status().delivery == MeshDelivery::Confirmed);
        client.tick(at(60010));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.receive(voltage, sizeof(voltage), at(60011)));
        CHECK(client.send_private(peer.id, "no reply", WallTime{2}).accepted());
        client.tick(at(60012));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(60013)));
        CHECK(client.send_busy());
        client.tick(at(75012));
        CHECK(client.status().delivery == MeshDelivery::Unknown);
        CHECK(!client.send_busy());
    }

    // A finite error count cannot identify its claimant: a new send's ERR
    // can arrive before the old poll's single delayed ERR. Neither names it.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        MeshPeer peer{};
        CHECK(client.peer(0, peer));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        client.tick(at(5010));
        CHECK(client.send_private(peer.id, "A", WallTime{1}).accepted());
        client.tick(at(5011));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(5012))); // A's refusal
        CHECK(client.send_busy());
        client.tick(at(20011));
        // `Unknown` and not `Refused`, and the line above is why: the error was
        // ambiguous and did not reach the send, which is still busy. What ends
        // this one is the budget, from a phase where the node has answered
        // nothing -- and there is no acceptance there to be unsure about.
        CHECK(client.status().delivery == MeshDelivery::Unknown);
        CHECK(!client.send_busy());
        CHECK(client.send_private(peer.id, "B", WallTime{2}).accepted());
        client.tick(at(20012));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(20013))); // old poll
        CHECK(client.status().delivery == MeshDelivery::Queued);
        CHECK(client.receive(sent, sizeof(sent), at(20014)));
        CHECK(client.receive(confirmed, sizeof(confirmed), at(20015)));
        CHECK(client.status().delivery == MeshDelivery::Confirmed);
    }

    // A continuing receive drain gives the due poll one FIFO slot. Its own
    // NO_MORE_MESSAGES response must not cancel the battery wait.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        const std::uint8_t waiting[] = {0x83};
        const std::uint8_t drained[] = {10};
        for (std::uint64_t start : {10ULL, 60011ULL, 120012ULL}) {
            CHECK(client.receive(waiting, sizeof(waiting), at(start)));
            client.tick(at(start + 1));
            CHECK(client.next_tx(frame) && frame.bytes[0] == 10);
            CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
            CHECK(!client.next_tx(frame));
            if (start == 120012) {
                // The earlier sync can fail while the battery reply is pending.
                CHECK(client.receive(error, sizeof(error), at(start + 2)));
            } else {
                CHECK(client.receive(drained, sizeof(drained), at(start + 2)));
            }
            CHECK(client.receive(voltage, sizeof(voltage), at(start + 3)));
            CHECK(client.status().node_battery.received_at.ms == start + 3);
            CHECK(!client.next_tx(frame));
        }
    }

    // A queued poll has no transport-independent deadline of its own. If the
    // pump stalls, private text and room login must expire without later TX.
    for (bool room : {false, true}) {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        const std::uint8_t waiting[] = {0x83};
        CHECK(client.receive(waiting, sizeof(waiting), at(9)));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 10);
        MeshPeer peer{};
        CHECK(client.peer(0, peer));
        if (room) {
            CHECK(client.send_room(peer.id.public_key, "password", "stalled pump", WallTime{1}).accepted());
        } else {
            CHECK(client.send_private(peer.id, "stalled pump", WallTime{1}).accepted());
        }
        // Retained drain work after the operation tests FIFO compaction too.
        const std::uint8_t drained[] = {10};
        CHECK(client.receive(drained, sizeof(drained), at(11)));
        CHECK(client.receive(waiting, sizeof(waiting), at(12)));
        client.tick(at(13));
        CHECK(client.send_busy());
        client.tick(at(15013));
        CHECK(client.status().delivery == MeshDelivery::Unknown);
        CHECK(!client.send_busy());
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.receive(voltage, sizeof(voltage), at(15014)));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 10);
        CHECK(!client.next_tx(frame)); // no expired text or login remains
        CHECK(client.receive(drained, sizeof(drained), at(15015)));
        CHECK(client.send_private(peer.id, "new", WallTime{2}).accepted());
        client.tick(at(15016));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(std::memcmp(&frame.bytes[13], "new", 3) == 0);
        CHECK(client.receive(sent, sizeof(sent), at(15017)));
        CHECK(client.receive(confirmed, sizeof(confirmed), at(15018)));
        CHECK(client.status().delivery == MeshDelivery::Confirmed);
    }

    // Forget between FIFO selection of the drain and the queued poll drops
    // opcode 20 before it can cross the transport, not merely on its reply.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        const std::uint8_t waiting[] = {0x83};
        CHECK(client.receive(waiting, sizeof(waiting), at(9)));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 10);
        client.unpin();
        CHECK(!client.next_tx(frame));
        CHECK(!client.receive(voltage, sizeof(voltage), at(11)));
        CHECK(client.status().node_battery.validity == core::Validity::Unknown);
    }

    // An in-connection identity replacement/forget cannot inherit a voltage
    // or relabel an old untagged reply. A new handshake enables polling again.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.receive(voltage, sizeof(voltage), at(11)));
        std::uint8_t replacement[62]{};
        replacement[0] = 5;
        const auto replacement_key = key_of(10);
        std::memcpy(&replacement[4], replacement_key.public_key.data(),
                    core::kMeshPublicKeyBytes);
        CHECK(client.receive(replacement, sizeof(replacement), at(12)));
        CHECK(client.status().node_id == replacement_key);
        CHECK(client.status().node_battery.validity == core::Validity::Unknown);
        CHECK(client.status().node_battery.millivolts == 0);
        client.tick(at(60010));
        CHECK(!client.next_tx(frame));
        CHECK(!client.receive(voltage, sizeof(voltage), at(60011)));
        client.unpin();
        connect_and_handshake(client);
        CHECK(client.receive(hint, sizeof(hint), at(8)));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        client.pin(key_of(20));
        CHECK(!client.receive(voltage, sizeof(voltage), at(11)));
        CHECK(client.status().node_battery.validity == core::Validity::Unknown);
        client.unpin();
        client.tick(at(60010));
        CHECK(!client.next_tx(frame));
    }
}

void test_typed_battery_failure_does_not_create_err_ambiguity()
{
    const std::uint8_t hint[] = {21};
    const std::uint8_t voltage[] = {12, 0x74, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::uint8_t error[] = {1, 1};
    for (bool previous_timeout : {false, true}) {
        for (bool late : {false, true}) {
            MeshCoreCompanion client;
            connect_and_handshake(client);
            CHECK(client.receive(hint, sizeof(hint), at(8)));
            MeshPeer peer{};
            CHECK(client.peer(0, peer));
            MeshCoreFrame frame{};
            client.tick(at(10));
            CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
            const std::uint64_t started = previous_timeout ? 60010 : 10;
            if (previous_timeout) {
                client.tick(at(5010));
                client.tick(at(started));
                CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
            }
            const std::uint64_t reply_at = started + (late ? 5000 : 1);
            CHECK(!client.receive(voltage, late ? sizeof(voltage) : 3, at(reply_at)));
            CHECK(client.status().node_battery.validity == core::Validity::Unknown);
            CHECK(client.status().node_battery.millivolts == 0);
            CHECK(client.status().node_battery.received_at.ms == 0);
            CHECK(client.send_private(peer.id, "after typed reply", WallTime{1}).accepted());
            client.tick(at(reply_at + 1));
            CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
            CHECK(client.receive(error, sizeof(error), at(reply_at + 2)));
            CHECK(client.send_busy() == previous_timeout);
            CHECK(client.status().delivery == (previous_timeout ? MeshDelivery::Queued
                                                               : MeshDelivery::Refused));
            if (previous_timeout) {
                client.tick(at(reply_at + 15001));
                CHECK(client.status().delivery == MeshDelivery::Unknown);
                CHECK(!client.send_busy());
            }
        }
    }
}


// THE FRAME THAT ENDS THE WALK IS THE ONE THE TRANSPORT DROPS. MEASURED on the
// bench 2026-09-14 (#566): the node streams one contact per loop() pass -- 234
// frames of 148 bytes in about 1.5 s -- the BLE-to-worker queue overruns, and
// `RESP_CODE_END_OF_CONTACTS` is the last frame of the burst, so it is the one
// the overrun reaches. Three sessions out of three lost it, and with it the
// only CMD_SYNC_NEXT_MESSAGE the session would ever send: every message the
// node was holding stranded in a 16-deep queue that evicts when it fills.
//
// So a quiet stream has to stand in for the boundary. Delete the sweep in
// tick() and this test is the one that fails -- nothing goes out at all.
void test_a_lost_contacts_end_still_asks_for_messages()
{
    MeshCoreCompanion client;
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);

    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(3)));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 2 && frame.bytes[0] == 22);

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

    // AND NOTHING GOES OUT WHILE THE STREAM IS STILL LIVE, because a command
    // sent mid-walk is how a client aborts the node's own iteration.
    client.tick(at(6 + 2999));
    CHECK(!client.next_tx(frame));

    // RESP_CODE_END_OF_CONTACTS never arrives. The stream falling quiet is what
    // closes the iteration instead.
    client.tick(at(6 + 3000));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 40);
    CHECK(!client.next_tx(frame));

    // AND THE SNAPSHOT IS COMPLETE. `peers_complete` is what the face reads to
    // decide whether it may print the kept/reported pair --
    // `apps/src/mesh.cpp:284` -- "if (status.peers_complete && retained <
    // reported) {". Withholding it here would make the watch print the node's
    // own total alone on exactly the session where the two numbers differ.
    CHECK(client.status().peers_complete);

    // A late boundary frame is still the node's own statement, and it does not
    // ask a second time: `end_contacts()` is idempotent through
    // `contacts_complete_`.
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(6 + 4000)));
    CHECK(client.status().peers_complete);
    CHECK(!client.next_tx(frame));
}

// Handshake far enough that CMD_GET_CONTACTS has gone out and the node has
// begun answering it: one contact in, the stream live, the quiet window armed
// at `at(6)`. Frames are left in the ring unless `drain` says otherwise --
// `test_a_quiet_stream_that_cannot_send_tries_again` needs them there.
void open_a_contact_stream(MeshCoreCompanion& client, bool drain)
{
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(3)));

    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    CHECK(client.receive(device, sizeof(device), at(4)));

    if (drain) {
        MeshCoreFrame frame{};
        while (client.next_tx(frame)) {
        }
    }

    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(5)));

    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(client.receive(contact, sizeof(contact), at(6)));
}

// A WALK A PUSH INVALIDATED, opened and ended by hand because the shape is the
// point: the four codes are dirt only between `RESP_CODE_CONTACTS_START` and
// `RESP_CODE_END_OF_CONTACTS`, so the push has to land inside the stream and
// the stream has to end for anything to be published. `drain` leaves the
// handshake's commands in the ring for the caller that needs it full.
void open_a_dirty_walk(MeshCoreCompanion& client, bool drain)
{
    open_a_contact_stream(client, drain);

    // PUSH_CODE_CONTACT_DELETED, inside the walk: the node compacted its table
    // under its own iterator and the rows this walk has not reached moved.
    const std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
    CHECK(client.receive(deleted, sizeof(deleted), at(7)));

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(8)));
    CHECK(client.status().peers_complete);
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);
}

// Drains the ring and counts the `CMD_GET_CONTACTS` in it. Counting rather
// than asserting the ring is empty is deliberate: a long tick also arms the
// battery poll, which is ordinary traffic and not what these rows are about.
int drain_counting_re_reads(MeshCoreCompanion& client)
{
    MeshCoreFrame frame{};
    int asked = 0;
    while (client.next_tx(frame)) {
        if (frame.size == 1 && frame.bytes[0] == 4) ++asked;
    }
    return asked;
}

// ROW 7 OF ADR-0022 §9. `0x82` is the other push that arrives mid-walk without
// being about the walk, and the row asks for both halves at once: the snapshot
// stays consistent, and the send it *is* about still reaches `Confirmed`
// through a contact burst. The mismatched ack is the same frame aimed at some
// other operation -- it must not release the slot, which is #315's rule and
// the reason the four bytes are compared at all.
void test_a_confirmation_mid_walk_confirms_without_dirtying()
{
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    CHECK(client.send_private(peer.id, "on my way", WallTime{1000}).accepted());

    std::uint8_t sent[10]{};
    sent[0] = 6;  // RESP_CODE_SENT
    sent[2] = 0xAA;
    sent[3] = 0xBB;
    sent[4] = 0xCC;
    sent[5] = 0xDD;
    CHECK(client.receive(sent, sizeof(sent), at(7)));
    CHECK(client.status().delivery == core::MeshDelivery::Accepted);

    // A confirmation for somebody else's send, arriving inside the walk.
    const std::uint8_t other[] = {0x82, 0x11, 0x22, 0x33, 0x44};
    CHECK(client.receive(other, sizeof(other), at(8)));
    CHECK(client.status().delivery == core::MeshDelivery::Accepted);
    CHECK(client.send_busy());

    // And then the one that matches.
    const std::uint8_t mine[] = {0x82, 0xAA, 0xBB, 0xCC, 0xDD};
    CHECK(client.receive(mine, sizeof(mine), at(9)));
    CHECK(client.status().delivery == core::MeshDelivery::Confirmed);
    CHECK(!client.send_busy());

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(10)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.malformed_frames() == 0);
    CHECK(client.peer_count() == 1);
}

// ROW 8 OF ADR-0022 §9: the same invalidating push at each of the four
// boundaries. Decision 3 conditions on the code and on *where* it landed, and
// this is the row that reads the second half back: inside the stream it is
// dirt, outside it is staleness. The two boundary cases are the interesting
// ones -- a push in the same millisecond as `START` or `END` is on one side or
// the other of a line the frames themselves draw, not of a clock.
void test_where_a_push_lands_decides_whether_it_is_dirt()
{
    struct Case {
        int position;  // 0 before START, 1 after START, 2 before END, 3 after END
        bool dirties;
        const char* where;
    };
    const Case cases[] = {
        {0, false, "before CONTACTS_START -- no read is in flight"},
        {1, true, "immediately after CONTACTS_START"},
        {2, true, "immediately before END_OF_CONTACTS"},
        {3, false, "immediately after END_OF_CONTACTS -- the read had ended"},
    };

    for (const Case& c : cases) {
        MeshCoreCompanion client;
        client.begin(at(0));
        client.peer_arriving(at(1));
        client.connected(at(2));
        std::uint8_t self[62]{};
        self[0] = 5;
        std::memcpy(&self[58], "Node", 4);
        CHECK(client.receive(self, sizeof(self), at(3)));
        std::uint8_t device[82]{};
        device[0] = 13;
        device[1] = 13;
        CHECK(client.receive(device, sizeof(device), at(4)));
        MeshCoreFrame frame{};
        while (client.next_tx(frame)) {
        }

        const std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
        if (c.position == 0) CHECK(client.receive(deleted, sizeof(deleted), at(5)));

        const std::uint8_t start[] = {2, 2, 0, 0, 0};
        CHECK(client.receive(start, sizeof(start), at(6)));
        if (c.position == 1) CHECK(client.receive(deleted, sizeof(deleted), at(6)));

        std::uint8_t contact[148]{};
        contact[0] = 3;
        for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
        contact[33] = 1;
        std::memcpy(&contact[100], "Peer", 4);
        CHECK(client.receive(contact, sizeof(contact), at(7)));
        if (c.position == 2) CHECK(client.receive(deleted, sizeof(deleted), at(8)));

        const std::uint8_t end[] = {4, 0, 0, 0, 0};
        CHECK(client.receive(end, sizeof(end), at(8)));
        if (c.position == 3) CHECK(client.receive(deleted, sizeof(deleted), at(8)));

        CHECK(client.malformed_frames() == 0);
        CHECK(client.status().snapshot ==
              (c.dirties ? core::MeshSnapshot::Dirty : core::MeshSnapshot::Consistent));
        while (client.next_tx(frame)) {
        }
        client.tick(at(8 + 10001));
        CHECK(drain_counting_re_reads(client) == (c.dirties ? 1 : 0));
    }
}

// ROW 11 OF ADR-0022 §9, twice over: a disconnect mid-stream and a disconnect
// mid-retry. Neither may leave a false completion behind, and the reconnect's
// sync must be a fresh one rather than the resumption of a walk whose node is
// gone -- `_iter_started` on the node is cleared by anything that restarts the
// app session, so a client that carried its own half across would be reading
// against an iterator that no longer exists.
void test_a_disconnect_leaves_no_half_finished_snapshot()
{
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);
    CHECK(client.peer_count() == 1);
    CHECK(!client.status().peers_complete);

    client.disconnected(at(7));
    CHECK(client.status().snapshot == core::MeshSnapshot::None);
    CHECK(!client.status().peers_complete);
    CHECK(client.peer_count() == 0);
    CHECK(client.status().peers_reported == 0);
    CHECK(client.status().peers_retained == 0);

    // A CONTACTS_START that arrives after the link is gone is not the walk
    // resuming: there is no session for it to belong to.
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    client.tick(at(7 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);

    // AND THE SAME MID-RETRY, which is the state a dirty walk leaves behind.
    MeshCoreCompanion second;
    open_a_dirty_walk(second, true);
    while (second.next_tx(frame)) {
    }
    second.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(second) == 1);
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(second.receive(start, sizeof(start), at(8 + 10002)));
    CHECK(second.status().snapshot == core::MeshSnapshot::RetryPending);

    second.disconnected(at(8 + 10003));
    CHECK(second.status().snapshot == core::MeshSnapshot::None);
    CHECK(second.peer_count() == 0);
    CHECK(!second.status().peers_complete);

    CHECK(drain_counting_re_reads(second) == 0);

    // AND THE BUDGET IS RESTORED WITH THE SESSION, not carried across it. The
    // next node gets its own two attempts: a client that kept the spent count
    // would give a fresh node one re-read, or none, for a walk of its own that
    // the first node's behaviour had already paid for.
    const std::uint64_t base = 8 + 10004;
    second.peer_arriving(at(base));
    second.connected(at(base + 1));
    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(second.receive(self, sizeof(self), at(base + 2)));
    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    CHECK(second.receive(device, sizeof(device), at(base + 3)));
    while (second.next_tx(frame)) {
    }
    CHECK(second.receive(start, sizeof(start), at(base + 4)));
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(second.receive(contact, sizeof(contact), at(base + 5)));
    const std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
    CHECK(second.receive(deleted, sizeof(deleted), at(base + 6)));
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(second.receive(end, sizeof(end), at(base + 7)));
    CHECK(second.status().snapshot == core::MeshSnapshot::Dirty);

    std::uint64_t when = base + 7;
    int re_reads = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        when += 10001;
        second.tick(at(when));
        const int asked = drain_counting_re_reads(second);
        CHECK(asked <= 1);
        if (asked == 0) continue;
        ++re_reads;
        CHECK(second.receive(start, sizeof(start), at(++when)));
        CHECK(second.receive(contact, sizeof(contact), at(++when)));
        CHECK(second.receive(deleted, sizeof(deleted), at(++when)));
        CHECK(second.receive(end, sizeof(end), at(++when)));
        while (second.next_tx(frame)) {
        }
    }
    CHECK(re_reads == 2);
    CHECK(second.malformed_frames() == 0);
}

// ROW 12 OF ADR-0022 §9. Consistent and truncated are two different
// observations and the row exists to keep them independently assertable: the
// watch retains sixteen contacts, so a node with seventeen produces a walk
// that is *proven* and a list that is *short*. Reading one off the other is
// how `16/233` came to look like a broken sync on the bench.
void test_a_truncated_list_is_still_a_consistent_snapshot()
{
    MeshCoreCompanion client;
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(3)));
    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    CHECK(client.receive(device, sizeof(device), at(4)));
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    const std::uint8_t start[] = {2, 17, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(5)));
    for (std::uint8_t n = 0; n < 17; ++n) {
        std::uint8_t contact[148]{};
        contact[0] = 3;
        // Distinct in the first prefix byte, which is what `accept_contact()`
        // files them under: a key that repeats overwrites a row rather than
        // adding one, and the seventeen would silently become eight.
        for (std::size_t i = 0; i < 32; ++i)
            contact[1 + i] = static_cast<std::uint8_t>(i + 1);
        contact[1] = static_cast<std::uint8_t>(n + 1);
        contact[33] = 1;
        contact[100] = static_cast<std::uint8_t>('A' + n);
        CHECK(client.receive(contact, sizeof(contact), at(6 + n)));
    }
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(30)));

    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.status().peers_complete);
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().peers_reported == 17);
    CHECK(client.status().peers_retained == 16);
    CHECK(client.peer_count() == 16);

    // And truncation asks for nothing: the node did not move the table, this
    // watch simply cannot hold all of it, and re-reading would return the same
    // seventeen.
    while (client.next_tx(frame)) {
    }
    client.tick(at(30 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);
}

// ROW 6 OF ADR-0022 §9. `0x83` is not one of the four, and the row exists to
// say what it *is*: a message waiting behind a contact burst. The snapshot
// stays consistent and -- the half that matters on the wire -- the drain still
// happens, because a push folded into a sync that nothing asks for is a
// message lost to the contact walk.
void test_a_message_waiting_mid_walk_neither_dirties_nor_is_swallowed()
{
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);

    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(7)));

    // The second contact still arrives and is still stored: the push changed
    // nothing about the walk it landed in.
    std::uint8_t second[148]{};
    second[0] = 3;
    for (std::size_t i = 0; i < 32; ++i)
        second[1 + i] = static_cast<std::uint8_t>(0x80 + i);
    second[33] = 1;
    std::memcpy(&second[100], "Other", 5);
    CHECK(client.receive(second, sizeof(second), at(8)));

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(9)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.malformed_frames() == 0);
    CHECK(client.peer_count() == 2);

    // AND THE MESSAGE IS ASKED FOR, which is the half of this row that is
    // about the wire. Two CMD_SYNC_NEXT_MESSAGE go out, and the count is
    // pinned rather than reduced to "at least one" so that a change is
    // visible: the push found no drain outstanding mid-walk and spent itself
    // at once, and `end_contacts()` then starts the walk's own drain
    // unconditionally. The second ask is answered with "no more messages" and
    // costs one frame; what the row forbids is zero, a message folded into a
    // sync nothing asked for.
    MeshCoreFrame frame{};
    int asks = 0;
    while (client.next_tx(frame)) {
        if (frame.size == 1 && frame.bytes[0] == 10) ++asks;
    }
    CHECK(asks == 2);
}

// ROW 13 OF ADR-0022 §9, which is #3403's delivery order read as a rule: a
// `0x8F` that arrives *after* `END_OF_CONTACTS` did not invalidate the read
// that already ended. What it makes the published list is stale, and staleness
// is not inconsistency -- a snapshot that re-read on it would re-read on every
// contact the node ever deletes.
void test_a_deletion_after_the_end_is_staleness_not_inconsistency()
{
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(7)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);

    const std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
    CHECK(client.receive(deleted, sizeof(deleted), at(8)));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);

    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    client.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);
}

// ROW 19 OF ADR-0022 §9, and the seam with #567. A stream that simply fell
// quiet is ended by the sweep rather than by a frame, and a lost boundary
// frame is not evidence the table moved: the sweep's `end_contacts()` is the
// only end that arrived, and with no invalidating push the snapshot it
// publishes is consistent.
void test_a_swept_stream_with_no_push_is_consistent()
{
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);

    // Three seconds of silence, and the walk ends without its END_OF_CONTACTS.
    client.tick(at(6 + 3001));
    CHECK(client.status().peers_complete);
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.malformed_frames() == 0);
    CHECK(client.peer_count() == 1);

    while (drain_counting_re_reads(client) >= 0) break;
    client.tick(at(6 + 3001 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);
}

// ROW 9 OF ADR-0022 §9: the ordinary recovery, and the cheapest thing the
// design has to promise -- one extra `CMD_GET_CONTACTS` on the wire, not a
// poll. The count is the assertion: a re-read that fired twice for one dirty
// walk would be invisible in the published snapshot and obvious on the radio.
void test_one_dirty_walk_costs_exactly_one_re_read()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    // Nothing goes out before the quiet window is up, which is what keeps the
    // re-read off a node that is still iterating.
    client.tick(at(8 + 9000));
    CHECK(drain_counting_re_reads(client) == 0);
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);

    client.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(client) == 1);
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(8 + 10002)));
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(client.receive(contact, sizeof(contact), at(8 + 10003)));
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(8 + 10004)));

    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.status().peers_retained == 1);
    CHECK(client.malformed_frames() == 0);

    // AND EVERY LATER WINDOW PASSES WITHOUT ONE. A clean re-read ends the
    // episode; nothing re-arms on the timer it was armed by. Other traffic --
    // the battery poll -- is not what this counts.
    while (client.next_tx(frame)) {
    }
    client.tick(at(8 + 10004 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);
    client.tick(at(8 + 10004 + 60000));
    CHECK(drain_counting_re_reads(client) == 0);
}

// ROW 10 OF ADR-0022 §9, and the row the budget exists for: a node whose table
// moves under every read. The snapshot must end up saying so rather than
// asking forever, and "bounded" is a number -- two re-reads, because
// `kSnapshotRetries` is 2 and each attempt is spent when the command goes out
// rather than when it is answered.
void test_a_table_that_moves_under_every_re_read_ends_degraded()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    std::uint64_t when = 8;
    int re_reads = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        when += 10001;
        client.tick(at(when));
        const int asked = drain_counting_re_reads(client);
        CHECK(asked <= 1);
        if (asked == 0) continue;
        ++re_reads;

        // Every re-read is invalidated exactly as the first walk was.
        const std::uint8_t start[] = {2, 2, 0, 0, 0};
        CHECK(client.receive(start, sizeof(start), at(++when)));
        std::uint8_t contact[148]{};
        contact[0] = 3;
        for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
        contact[33] = 1;
        std::memcpy(&contact[100], "Peer", 4);
        CHECK(client.receive(contact, sizeof(contact), at(++when)));
        const std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
        CHECK(client.receive(deleted, sizeof(deleted), at(++when)));
        const std::uint8_t end[] = {4, 0, 0, 0, 0};
        CHECK(client.receive(end, sizeof(end), at(++when)));
        while (client.next_tx(frame)) {
        }
    }

    // NO LIVE-LOCK. Six windows, two re-reads.
    CHECK(re_reads == 2);
    CHECK(client.status().snapshot == core::MeshSnapshot::Degraded);
    CHECK(client.malformed_frames() == 0);

    // Degraded publishes the newest read rather than withholding it: §7.4
    // would leave the wearer an empty list where a probably-right one serves
    // better, provided it does not claim to be proven.
    CHECK(client.status().peers_complete);
    CHECK(client.status().peers_retained == 1);
    MeshPeer kept{};
    CHECK(client.peer(0, kept));
    CHECK(std::strcmp(kept.name.data(), "Peer") == 0);
}

// ROWS 1-5, 15 AND 16 OF ADR-0022 §9 IN ONE TABLE, because the rows differ
// only in the code byte and the claim they make together is exactly that: the
// four invalidating pushes are told from the two that merely look it by §3's
// classification of the code and by where it landed, and by nothing in the
// frame. Six codes that all used to reach `default:` and count against
// `malformed_frames_` -- the counter a dropped-frame investigation reads --
// which is why every row asserts it stayed at zero.
//
// Row 15 is the same table read from outside a walk: a `0x80` arriving while
// no iteration is running dirties nothing, because there is no read in flight
// for it to invalidate. Staleness is not inconsistency.
void test_which_pushes_dirty_a_walk_and_which_only_look_it()
{
    struct Case {
        std::uint8_t code;
        bool dirties;
        const char* row;
    };
    const Case cases[] = {
        {0x8F, true, "1 -- a contact the node deleted under its own iterator"},
        {0x80, true, "2 -- an advert changed a row, or added one not reached"},
        {0x81, true, "5 -- a path this walk may already have read was updated"},
        {0x8D, true, "16 -- a path discovery, the fourth invalidating code"},
        {0x8A, false, "3 -- a new advert names a contact never stored"},
        {0x90, false, "4 -- the contacts-full notice moves no row"},
    };

    for (const Case& c : cases) {
        MeshCoreCompanion client;
        open_a_contact_stream(client, true);
        // Full length for every code: `0x8F` refuses anything shorter than its
        // key, and the others read nothing past the code.
        std::uint8_t push[1 + core::kMeshPublicKeyBytes]{};
        push[0] = c.code;
        CHECK(client.receive(push, sizeof(push), at(7)));
        const std::uint8_t end[] = {4, 0, 0, 0, 0};
        CHECK(client.receive(end, sizeof(end), at(8)));

        CHECK(client.status().peers_complete);
        CHECK(client.malformed_frames() == 0);
        CHECK(client.status().snapshot ==
              (c.dirties ? core::MeshSnapshot::Dirty : core::MeshSnapshot::Consistent));
        // The published set is the one the walk just read either way: a dirty
        // snapshot keeps what it has and says so, rather than emptying the
        // face while it asks again.
        CHECK(client.peer_count() == 1);
        MeshPeer kept{};
        CHECK(client.peer(0, kept));
        CHECK(std::strcmp(kept.name.data(), "Peer") == 0);

        // Exactly one re-read is on the wire ten seconds later, and only for
        // the codes that invalidated the walk.
        MeshCoreFrame frame{};
        while (client.next_tx(frame)) {
        }
        client.tick(at(8 + 10001));
        if (c.dirties) {
            CHECK(client.next_tx(frame));
            CHECK(frame.size == 1 && frame.bytes[0] == 4);
            CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);
        }
        CHECK(!client.next_tx(frame));
    }

    // ROW 15. The same `0x80` outside any iteration: understood, ignored, and
    // not a reason to read the table again.
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(8)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);

    const std::uint8_t advert[] = {0x80};
    CHECK(client.receive(advert, sizeof(advert), at(9)));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);

    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    client.tick(at(9 + 10001));
    CHECK(!client.next_tx(frame));
}

// ROW 14 OF ADR-0022 §9, and the report calls it the regression risk the whole
// design has to be checked against. `RESP_CODE_ERR` carries nothing to
// correlate it by, so it is attributed by order: the oldest command still owed
// an answer takes it. A re-read is a third claimant in that order and it is
// the one command here whose error is *expected* -- a node still iterating
// answers `CMD_GET_CONTACTS` with `ERR_CODE_BAD_STATE`, which is the whole
// reason the re-read waits ten seconds before asking. Without the claim, that
// error falls through to `send_busy()` and fails a message the node accepted,
// which is #315's fail-closed rule turned against an innocent send by a
// command the wearer never asked for.
void test_an_error_owed_to_a_re_read_does_not_fail_a_send()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    const std::uint8_t drained[] = {10};
    CHECK(client.receive(drained, sizeof(drained), at(9)));

    // The one question the handshake leaves outstanding is answered here, so
    // the error below has two claimants rather than three: an unanswered
    // CMD_GET_CUSTOM_VARS is older than both and takes it first, by this same
    // order rule and correctly.
    const std::uint8_t vars[] = {21};
    CHECK(client.receive(vars, sizeof(vars), at(10)));

    // The re-read is asked first, so it is the older claimant.
    client.tick(at(9 + 10000));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 4);
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    // The wearer's message is queued after it and goes out.
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    CHECK(client.send_private(peer.id, "on my way", WallTime{1000}).accepted());
    CHECK(client.status().delivery == core::MeshDelivery::Queued);
    CHECK(client.next_tx(frame));
    CHECK(frame.bytes[0] == 2);  // CMD_SEND_TXT_MSG

    // ERR_CODE_BAD_STATE, untagged and ambiguous on its face. It is the
    // re-read's: the re-read was asked first and is still owed an answer, and
    // the send's answer has not been and gone.
    const std::uint8_t err[] = {1, 4};
    CHECK(client.receive(err, sizeof(err), at(9 + 10002)));
    CHECK(client.malformed_frames() == 0);

    // THE SEND IS UNTOUCHED. This is the assertion the row exists for.
    CHECK(client.status().delivery == core::MeshDelivery::Queued);

    // And the re-read is spent rather than retried instantly: the attempt was
    // decremented when the command went out, one is left, and the snapshot
    // goes back to saying the table moved under the last walk. A node that
    // refuses every re-read therefore costs a bounded two errors.
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);

    // #315'S DIRECTION IS KEPT, NOT TRADED AWAY. The send whose error the
    // re-read took is not cleared by it; it never receives RESP_CODE_SENT
    // either, so the ack budget fails it. Later, not softer.
    // The budget is armed on the first pass that sees a send outstanding and
    // spent one ack-wait after that, so it takes two ticks to read it.
    client.tick(at(9 + 10003));
    CHECK(client.status().delivery == core::MeshDelivery::Queued);
    client.tick(at(9 + 10003 + 15000));
    CHECK(client.status().delivery == core::MeshDelivery::Unknown);
}

// AND THE SAME ROW READ THE OTHER WAY, because "attributed by the existing
// order rule" is a claim about an order and a claimant that always wins is not
// obeying one. Here the wearer's message is the older command: it went out
// before the re-read was even armed, so the error is the send's and the
// re-read is still owed its own answer -- which arrives next and commits.
void test_an_error_older_than_the_re_read_still_fails_the_send()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    const std::uint8_t drained[] = {10};
    CHECK(client.receive(drained, sizeof(drained), at(9)));
    const std::uint8_t vars[] = {21};
    CHECK(client.receive(vars, sizeof(vars), at(10)));

    // The message goes out while the quiet window is still counting down.
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    CHECK(client.send_private(peer.id, "on my way", WallTime{1000}).accepted());
    CHECK(client.next_tx(frame));
    CHECK(frame.bytes[0] == 2);

    // Only then does the re-read go out, so it is the younger claimant.
    client.tick(at(9 + 10000));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 4);
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    const std::uint8_t err[] = {1, 4};
    CHECK(client.receive(err, sizeof(err), at(9 + 10002)));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().delivery == core::MeshDelivery::Refused);

    // The re-read kept its claim on an answer it has not had, and that answer
    // still commits the staged set.
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(9 + 10003)));
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Renamed", 7);
    CHECK(client.receive(contact, sizeof(contact), at(9 + 10004)));
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(9 + 10005)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    MeshPeer committed{};
    CHECK(client.peer(0, committed));
    CHECK(std::strcmp(committed.name.data(), "Renamed") == 0);
}

// ROUND 1 OF #564's REVIEW, FINDING 1. `kMaxAckWait` bounds the re-read's
// claim on an untagged RESP_CODE_ERR; it does not bound the re-read. The
// fifteen seconds cover the tx ring and the air as well as the node -- one
// frame leaves per completed GATT write, and a waiting battery poll holds the
// next one for its whole reply budget -- so a CONTACTS_START after the
// deadline is an ordinary slow answer. Read as a first walk it would do every
// one of the four things decision 7a exists to forbid, and this test names
// them one by one: the published list, the pair, `peers_complete` and
// `Availability::Ready` all stand until the re-read has proved itself.
void test_a_start_later_than_the_ack_budget_is_still_the_re_reads()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().peers_retained == 1);

    client.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(client) == 1);
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    // Fifteen seconds and no answer of any kind. The claim goes down -- from
    // here a stray error belongs to whatever else is outstanding -- and the
    // snapshot settles as though the attempt were over.
    client.tick(at(8 + 10001 + 15001));
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);

    // And then the node answers. This is the second CONTACTS_START of the
    // session, and it says the table now holds one contact.
    const std::uint8_t start[] = {2, 1, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(8 + 10001 + 15002)));
    CHECK(client.peer_count() == 1);
    CHECK(client.status().peers_complete);
    CHECK(client.status().peers_retained == 1);
    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().availability == Availability::Ready);

    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Late", 4);
    CHECK(client.receive(contact, sizeof(contact), at(8 + 10001 + 15003)));
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(8 + 10001 + 15004)));

    // Only now is the published set replaced, and by a walk no push touched.
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.status().peers_reported == 1);
    CHECK(client.peer_count() == 1);
    MeshPeer committed{};
    CHECK(client.peer(0, committed));
    CHECK(std::strcmp(committed.name.data(), "Late") == 0);
}

// ROUND 1 OF #564's REVIEW, FINDING 2, and the other half of finding 1. The
// deadline releases exactly one thing: the claim on an untagged error. While
// that claim stands no battery poll may go out, for the reason
// `awaiting_custom_vars_` is already in the same gate -- the re-read is the one
// command in this client whose error is *expected*, and the battery arm of the
// error ladder short-circuits before the re-read's, so a poll issued inside the
// window turns the node's correct ERR_CODE_BAD_STATE into a battery fault the
// wearer sees until the next period. After the deadline the poll is free and
// the command is not: a re-read the node has not answered is still outstanding,
// so no second one is armed behind it.
void test_an_expired_claim_frees_the_battery_poll_and_not_the_command()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    // TWO ANSWERS THE FIXTURE OWES THE GATE BEFORE IT CAN TEST IT, and both
    // were found by mutation: with either missing, the poll is withheld by
    // something other than the re-read and the test passes for the wrong
    // reason. `{10}` ends the drain the first walk's END armed -- `!draining_`
    // is in the same condition -- and `{21}` answers the receiver hint, whose
    // claim on an untagged error is the one the re-read's is modelled on.
    const std::uint8_t drained[] = {10};
    CHECK(client.receive(drained, sizeof(drained), at(8 + 1)));
    const std::uint8_t vars[] = {21};
    CHECK(client.receive(vars, sizeof(vars), at(8 + 2)));

    auto drain = [&client, &frame](int& asked, int& polled) {
        asked = 0;
        polled = 0;
        while (client.next_tx(frame)) {
            if (frame.size == 1 && frame.bytes[0] == 4) ++asked;
            if (frame.size == 1 && frame.bytes[0] == 20) ++polled;
        }
    };

    int asked = 0;
    int polled = 0;
    client.tick(at(8 + 10001));
    drain(asked, polled);
    CHECK(asked == 1);
    CHECK(polled == 0);

    client.tick(at(8 + 10001 + 15001));
    drain(asked, polled);
    CHECK(asked == 0);
    CHECK(polled == 1);
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);

    // A second delay's worth later it is still not asked again: the budget was
    // spent when the command went out, and the command has not come back.
    client.tick(at(8 + 10001 + 15001 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);
}

// ROW 17 OF ADR-0022 §9. The re-read's own `CONTACTS_START` is the second one
// of the session, and everything the first one does to the published state is
// what decision 7a forbids the second one from doing. The assertion that makes
// this a test rather than a restatement is the message in the middle: a
// contact message names its sender by resolving a six-byte prefix against
// `peers_`, so a re-read that wiped the published set would deliver the text
// with no name on it -- and the face would have watched `retained` count up
// from zero while a walk it never asked for ran.
void test_a_re_read_does_not_unname_a_sender_mid_walk()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }
    const std::uint8_t drained[] = {10};
    CHECK(client.receive(drained, sizeof(drained), at(9)));

    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().peers_retained == 1);
    CHECK(client.status().availability == Availability::Ready);

    // Ten seconds later the re-read goes out, and until its answer arrives the
    // published snapshot says a command is on the wire rather than that the
    // list is proven.
    client.tick(at(9 + 10000));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 4);
    CHECK(!client.next_tx(frame));
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    // The second CONTACTS_START of the session. Nothing below it may move.
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(9 + 10001)));
    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().peers_retained == 1);
    CHECK(client.status().peers_complete);
    CHECK(client.status().availability == Availability::Ready);

    // And the message arrives while the re-read is still streaming, which is
    // the moment the shadow copy earns its cost.
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    std::uint8_t message[16 + 11]{};
    message[0] = 16;  // RESP_CODE_CONTACT_MSG_RECV_V3
    std::memcpy(&message[4], peer.id.public_key.data(), 6);
    std::memcpy(&message[16], "still named", 11);
    CHECK(client.receive(message, sizeof(message), at(9 + 10002)));
    CHECK(std::strcmp(client.status().last_sender.data(), "Peer") == 0);
    CHECK(std::strcmp(client.status().last_message.data(), "still named") == 0);

    // The re-read finds the same table it was sent to re-read, and committing
    // it changes nothing a reader can see -- which is the outcome to assert,
    // because it is indistinguishable from the bug only if nothing is checked
    // between the two boundaries above.
    // The re-read finds both rows the first walk was told to expect -- the one
    // it read and the one the deletion moved out from under it. The pair must
    // not move while that is streaming: `1 of 2` is the last proven
    // observation and stays published until a walk replaces it.
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Peer", 4);
    CHECK(client.receive(contact, sizeof(contact), at(9 + 10003)));
    CHECK(client.status().peers_retained == 1);
    std::uint8_t second[148]{};
    second[0] = 3;
    for (std::size_t i = 0; i < 32; ++i)
        second[1 + i] = static_cast<std::uint8_t>(0x80 + i);
    second[33] = 1;
    std::memcpy(&second[100], "Other", 5);
    CHECK(client.receive(second, sizeof(second), at(9 + 10004)));
    CHECK(client.status().peers_retained == 1);
    CHECK(client.peer_count() == 1);

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(9 + 10005)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().peers_retained == 2);
    CHECK(client.peer_count() == 2);
    CHECK(client.status().availability == Availability::Ready);
    CHECK(client.peer(0, peer));
    CHECK(std::strcmp(peer.name.data(), "Peer") == 0);

    // One command follows, and it is the message's own continuation rather
    // than anything the re-read spent: a delivered message means the node may
    // be holding more. The re-read's end adds nothing to it.
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(!client.next_tx(frame));
    CHECK(client.malformed_frames() == 0);
}

// ROW 18 OF ADR-0022 §9, and the ring is full on purpose. A re-read's
// `END_OF_CONTACTS` has to reach `finish_retry()` and nothing else, and this
// test pins both halves of that separately, because they fail separately.
//
// The first half is the commit. Route the re-read's END through the session
// arm and `finish_retry()` never runs: the staged set is never published, so
// the contact the node renamed keeps its old name forever. That is what the
// `Renamed` assertion below catches, and it is the only thing that does --
// `end_contacts()` on this session short-circuits on `contacts_complete_`,
// which the re-read's START never cleared, so a re-read routed the wrong way
// is otherwise perfectly quiet.
//
// The second half is the drain. If the END did reach a live `end_contacts()`,
// it would ask for a second CMD_SYNC_NEXT_MESSAGE, find no room, and charge
// the node a malformed frame for a frame that was perfectly well formed -- and
// take the drain down with it, because `request_next_message()` clears
// `draining_` when the ring refuses it. That is right for a drain that failed
// to go out and wrong for a drain that is still outstanding, which is what the
// closing `kPushMessageWaiting` proves: it is folded into the live drain
// rather than spent on a request of its own.
void test_a_re_reads_end_spends_no_drain_on_a_full_ring()
{
    MeshCoreCompanion client;
    // Undrained: CMD_APP_START, CMD_DEVICE_QUERY and CMD_GET_CONTACTS are in
    // the ring, and the walk's own CMD_SYNC_NEXT_MESSAGE takes the fourth slot.
    // The drain is outstanding from here to the end of this test.
    open_a_dirty_walk(client, false);

    // One slot freed, and the re-read takes it back: the ring is full again
    // from the moment the re-read goes out until the test drains it.
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 16 && frame.bytes[0] == 1);

    client.tick(at(8 + 10000));
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(8 + 10001)));

    // The re-read finds the contact under a new name, which is the whole
    // reason the walk was repeated: the node moved the row while the first
    // walk was reading it. Nothing may show the new name before the END.
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Renamed", 7);
    CHECK(client.receive(contact, sizeof(contact), at(8 + 10002)));
    MeshPeer staged{};
    CHECK(client.peer(0, staged));
    CHECK(std::strcmp(staged.name.data(), "Peer") == 0);

    // The frame that would have cost a command there is no room for. It is
    // accepted, it is not counted, and it publishes the snapshot it proves --
    // and this is the one boundary that replaces the published set wholesale,
    // so the new name arrives here or nowhere.
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(8 + 10003)));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    MeshPeer committed{};
    CHECK(client.peer(0, committed));
    CHECK(std::strcmp(committed.name.data(), "Renamed") == 0);
    CHECK(client.status().peers_retained == 1);

    // Four frames, exactly one of them a sync: the session's, not a second one.
    int syncs = 0;
    for (int i = 0; i < 4; ++i) {
        CHECK(client.next_tx(frame));
        if (frame.size == 1 && frame.bytes[0] == 10) ++syncs;
    }
    CHECK(syncs == 1);
    CHECK(!client.next_tx(frame));

    // AND THE DRAIN IS STILL OUTSTANDING, which is the half of this a frame
    // count cannot see. A push while a drain is in flight costs nothing,
    // because the request already out is going to bring back everything the
    // node holds; a drain the re-read's end had quietly cleared would answer
    // this push with a second request.
    const std::uint8_t waiting[] = {0x83};
    CHECK(client.receive(waiting, sizeof(waiting), at(8 + 10004)));
    CHECK(!client.next_tx(frame));
}

// ROW 20 OF ADR-0022 §9, AND THE ONE SHAPE §1a's THIRD RUNG IS NOT GOOD ENOUGH
// FOR. A re-read swept by the quiet window has exactly the evidence the row
// above it lacks: the published set is already complete, already proven once,
// and only *suspected* of being stale. Committing a truncated staging over it
// on the strength of "the node stopped sending" trades that suspicion for a
// certainty, and decision 7 -- the last proven snapshot is what is published
// while a re-read is pending -- forbids it.
//
// The name is what catches it. A swept re-read that commits shows "Renamed"
// and calls the snapshot `Consistent`; one that discards shows "Peer" and says
// the snapshot is still unproven. Both halves are asserted, because a fix that
// kept the list and still published `Consistent` would end the session
// claiming to be the node's list, with another attempt never sent.
void test_a_re_read_the_sweep_closed_does_not_commit_what_it_swept()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    std::uint64_t when = 8;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        when += 10001;
        client.tick(at(when));
        CHECK(drain_counting_re_reads(client) == 1);
        CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

        // The re-read opens and delivers one row under a new name, and then
        // the node goes quiet without a boundary frame -- which is the frame
        // a bounded transport queue systematically drops.
        const std::uint8_t start[] = {2, 2, 0, 0, 0};
        CHECK(client.receive(start, sizeof(start), at(++when)));
        std::uint8_t contact[148]{};
        contact[0] = 3;
        for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
        contact[33] = 1;
        std::memcpy(&contact[100], "Renamed", 7);
        CHECK(client.receive(contact, sizeof(contact), at(++when)));

        when += 3000;  // kContactsQuiet: the walk goes quiet without an END.
        client.tick(at(when));

        // The published set is the first walk's, in both attempts: nothing
        // the sweep saw is good enough to replace it.
        CHECK(client.status().peers_retained == 1);
        CHECK(client.peer_count() == 1);
        MeshPeer kept{};
        CHECK(client.peer(0, kept));
        CHECK(std::strcmp(kept.name.data(), "Peer") == 0);
        CHECK(client.status().peers_complete);

        // And the snapshot goes back to saying what it said before the
        // attempt. The first sweep leaves a budget and re-arms the window;
        // the second spends the last of it.
        CHECK(client.status().snapshot ==
              (attempt == 1 ? core::MeshSnapshot::Dirty : core::MeshSnapshot::Degraded));
        while (client.next_tx(frame)) {
        }
    }

    // AND THE TAIL OF THE WALK IT ABANDONED GOES NOWHERE. Closing a re-read
    // does not stop the node sending it: the sweep fires on a three-second gap,
    // which on a busy channel is an ordinary pause mid-iteration, and the node
    // resumes afterwards. Those rows belong to a walk this client decided not to
    // trust. Routed on `retry_open_` alone they would land in the published set
    // -- a union of two walks that `peers_retained` counts and `peers_reported`
    // does not, which is the pair `apps/src/mesh.cpp:284` -- "        if (status.peers_complete && retained < reported) {"
    // -- compares. A second key, so a merge would show as a count rather than
    // as a rename.
    std::uint8_t third[148]{};
    third[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) third[1 + i] = static_cast<std::uint8_t>(i + 33);
    third[33] = 1;
    std::memcpy(&third[100], "Third", 5);
    CHECK(client.receive(third, sizeof(third), at(++when)));
    CHECK(client.peer_count() == 1);
    CHECK(client.status().peers_retained == 1);
    MeshPeer still{};
    CHECK(client.peer(0, still));
    CHECK(std::strcmp(still.name.data(), "Peer") == 0);
    CHECK(client.malformed_frames() == 0);

    // NO LIVE-LOCK EITHER. `Degraded` is terminal for the session: two
    // attempts is the whole budget whether the node answered them or not.
    when += 10001;
    client.tick(at(when));
    CHECK(drain_counting_re_reads(client) == 0);
    CHECK(client.status().snapshot == core::MeshSnapshot::Degraded);
    CHECK(client.malformed_frames() == 0);
}

// THE OTHER HALF OF THE SWEPT WALK: ITS `END`, WITH A BUDGET STILL LEFT. The
// abandoned walk's rows were already unowned; its boundary frame was not, and
// fell through to the first-walk arm. `settle_snapshot()` there re-stamps
// `dirty_end_at_`, which is the clock the next attempt is measured from -- so
// a walk this client had written off could push the attempt that replaces it
// up to a full ten seconds further away, once per late `END`.
//
// The assertion is the attempt, not a flag: tick at exactly the ten seconds
// after the sweep, and attempt two must be in the ring. With the stamp moved
// it is not, and nothing else in the class says so.
// AND THE FIRST WALK'S OWN `END` IS THE CHEAPER WAY TO DO IT -- no sweep, no
// re-read, one repeated frame.
//
// The guard that answered #593 first read `retry_swept_`, which `finish_retry()`
// alone sets, so it named the re-read's sweep and nothing else. A first walk's
// `END` arriving twice -- a node that repeats the frame, not only a hostile one
// -- found every flag false and reached `settle_snapshot()`, re-stamping
// `dirty_end_at_` on a walk that had already ended. Five bytes buy ten seconds,
// and repeated under ten seconds they buy the session: `retries_left_` never
// decrements, the snapshot never reaches `Degraded`, and `malformed_frames_`
// does not move, so nothing anywhere records that the re-read stopped
// happening.
//
// The assertion is the attempt, exactly as the row above it: with the duplicate
// counted, ticking ten seconds after the *first* `END` finds an empty ring.
void test_a_duplicate_end_does_not_delay_the_first_attempt()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);   // `END` at t=8, snapshot Dirty
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    // The same frame again, one second later, with no walk open to own it.
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(9000)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);

    client.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(client) == 1);

    // AND IT IS NOT A PARSE FAILURE. The node answered a question this client
    // asked, twice; the counter a dropped-frame investigation reads must not
    // fill up with it.
    CHECK(client.malformed_frames() == 0);
}

void test_a_swept_walks_late_end_does_not_delay_the_next_attempt()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    std::uint64_t when = 8 + 10001;
    client.tick(at(when));
    CHECK(drain_counting_re_reads(client) == 1);
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(++when)));
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Renamed", 7);
    CHECK(client.receive(contact, sizeof(contact), at(++when)));

    when += 3000;  // kContactsQuiet: attempt one is swept, one attempt left.
    client.tick(at(when));
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);
    const std::uint64_t swept_at = when;
    while (client.next_tx(frame)) {
    }

    // The node was not quiet, only slow. Its `END` for the walk that was
    // abandoned arrives a second later and is worth nothing: the staging it
    // would end is already discarded and the published set belongs to the walk
    // before it.
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(swept_at + 1000)));
    CHECK(client.malformed_frames() == 0);
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);
    CHECK(client.status().peers_retained == 1);
    MeshPeer kept{};
    CHECK(client.peer(0, kept));
    CHECK(std::strcmp(kept.name.data(), "Peer") == 0);

    // Ten seconds from the sweep, not from the stale frame.
    client.tick(at(swept_at + 10001));
    CHECK(drain_counting_re_reads(client) == 1);
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);
}

// AND IN THE WINDOW WHERE ATTEMPT TWO IS ASKED FOR BUT NOT YET ANSWERED, THE
// SAME FRAME PUBLISHED A VERDICT OVER A REQUEST STILL IN THE AIR. Arming
// attempt two spends the last of the budget and publishes `RetryPending`;
// until the node's `START` comes back, `retry_open_` is false and
// `retry_swept_` is still set, so attempt one's late `END` fell through to the
// first-walk arm. `settle_snapshot()` with `retries_left_` at zero writes
// `Degraded` there -- a terminal verdict about a session whose last attempt
// had not been answered -- and nothing puts `RetryPending` back, because that
// is assigned only where an attempt is armed.
//
// `Degraded` is the right end for this session; it is not the right end *yet*,
// and the difference is a whole re-read the wearer's list could have come from.
void test_a_swept_walks_late_end_does_not_settle_over_a_live_attempt()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    std::uint64_t when = 8 + 10001;
    client.tick(at(when));
    CHECK(drain_counting_re_reads(client) == 1);
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(++when)));
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;
    std::memcpy(&contact[100], "Renamed", 7);
    CHECK(client.receive(contact, sizeof(contact), at(++when)));
    when += 3000;  // attempt one is swept; one attempt left.
    client.tick(at(when));
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);
    while (client.next_tx(frame)) {
    }

    // Attempt two is asked for. The budget is now spent, and the node has said
    // nothing yet: this is the window the finding is about.
    when += 10001;
    client.tick(at(when));
    CHECK(drain_counting_re_reads(client) == 1);
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);

    // Attempt one's `END`, thirteen seconds late -- which is what a bounded
    // transport queue stalling mid-iteration looks like from here.
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(++when)));
    CHECK(client.status().snapshot == core::MeshSnapshot::RetryPending);
    CHECK(client.malformed_frames() == 0);

    // And attempt two then succeeds, which it could not have been credited for
    // from a session already published as terminal. The name is the proof that
    // the commit happened rather than the state merely being repaired.
    CHECK(client.receive(start, sizeof(start), at(++when)));
    CHECK(client.receive(contact, sizeof(contact), at(++when)));
    CHECK(client.receive(end, sizeof(end), at(++when)));
    CHECK(client.status().snapshot == core::MeshSnapshot::Consistent);
    MeshPeer committed{};
    CHECK(client.peer(0, committed));
    CHECK(std::strcmp(committed.name.data(), "Renamed") == 0);
}

// `retry_swept_` IS CLEARED BY ANY `START`, NOT ONLY BY AN ATTEMPT'S OWN. The
// line that does it -- `link/src/meshcore_companion.cpp:1410` -- "        retry_swept_ = false;"
// -- was uncovered: every `START` after a sweep in the suite was attempt two's,
// where `retry_open_` is set three lines later and makes the guard inert either
// way. The shape that needs it is a walk the node starts on its own, after the
// budget is spent and no attempt will ever be armed again. Without the clear,
// this client would drop that walk's rows and its `END` for the rest of the
// session -- which is the failure the guard added for the swept tail could
// create, so it is asserted rather than assumed.
void test_a_node_started_walk_after_the_budget_owns_its_frames()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    std::uint64_t when = 8;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        when += 10001;
        client.tick(at(when));
        CHECK(drain_counting_re_reads(client) == 1);
        const std::uint8_t start[] = {2, 2, 0, 0, 0};
        CHECK(client.receive(start, sizeof(start), at(++when)));
        std::uint8_t contact[148]{};
        contact[0] = 3;
        for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 1);
        contact[33] = 1;
        std::memcpy(&contact[100], "Renamed", 7);
        CHECK(client.receive(contact, sizeof(contact), at(++when)));
        when += 3000;
        client.tick(at(when));
        while (client.next_tx(frame)) {
        }
    }
    CHECK(client.status().snapshot == core::MeshSnapshot::Degraded);
    CHECK(client.status().peers_retained == 1);

    // The node volunteers a fresh walk. Nothing armed it, so `retry_unanswered_`
    // is false and this is a first walk in every sense the class has: it
    // replaces the published set as it streams.
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(++when)));
    std::uint8_t fresh[148]{};
    fresh[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) fresh[1 + i] = static_cast<std::uint8_t>(i + 65);
    fresh[33] = 1;
    std::memcpy(&fresh[100], "Volunteered", 11);
    CHECK(client.receive(fresh, sizeof(fresh), at(++when)));
    CHECK(client.peer_count() == 1);
    MeshPeer seen{};
    CHECK(client.peer(0, seen));
    CHECK(std::strcmp(seen.name.data(), "Volunteered") == 0);

    // And its `END` is its own: it completes, and it is not counted malformed.
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(++when)));
    CHECK(client.status().peers_complete);
    CHECK(client.status().peers_retained == 1);
    CHECK(client.malformed_frames() == 0);
}

// THE QUIET WINDOW OUTLIVES A REFUSAL RATHER THAN BEING SPENT ON ONE. The sweep
// is the one place that asks a question from outside `receive()`, and
// `receive()` is where the refusal guard lives:
// `link/src/meshcore_companion.cpp:1314` -- "    if (wrong_node_) return false;".
// So the sweep has to carry
// the guard itself, and the interesting half is what it does with the window
// afterwards: `unpin()` clears `wrong_node_` inside the session, so a sweep
// that closed the walk on the way past -- or that re-armed its window -- would
// leave the un-refused session waiting, or waiting forever.
//
// `pin()` is public and takes no view of where the session has got to, so this
// ordering is the class's contract, not a path the firmware walks today: it
// adopts a key on the frame that first carries one (`settle_node_pin()` in
// firmware/main/meshcore_node_pin.h:213 -- "        ops.adopt(seen);") and a
// mismatch there stops the handshake before CMD_GET_CONTACTS ever goes out.
void test_a_refused_session_keeps_its_quiet_window()
{
    MeshCoreCompanion client;
    open_a_contact_stream(client, true);

    // The watch is pinned to somebody else and the node says who it is. The
    // refusal latches on this frame, with the contact stream already open.
    client.pin(key_of(0x91));
    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    CHECK(client.receive(self, sizeof(self), at(7)));
    CHECK(client.wrong_node());

    // Quiet for a full window, twice over, and nothing goes out. This is the
    // assertion the guard exists for: CMD_SYNC_NEXT_MESSAGE to a refused node
    // is the watch asking a stranger's node for its queued messages.
    MeshCoreFrame frame{};
    client.tick(at(7 + 3000));
    CHECK(!client.next_tx(frame));
    client.tick(at(7 + 9000));
    CHECK(!client.next_tx(frame));
    CHECK(!client.status().peers_complete);

    // And the window was kept, not spent. The tick after the repudiation closes
    // the walk exactly as it would have.
    CHECK(client.unpin());
    client.tick(at(7 + 9001));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 40);
    CHECK(client.status().peers_complete);
}

// A FULL RING IS NOT AN ANSWER. `request_next_message()` returns false when the
// four-deep TX ring has no room -- `link/src/meshcore_companion.cpp:746` --
// "    if (!enqueue(sync, sizeof(sync))) {" -- and the session has exactly one
// CMD_SYNC_NEXT_MESSAGE to spend on a lost boundary. Counting a frame that
// never left would strand the node's backlog for the session, which is the
// defect #566 is about, reached by a different road.
void test_a_quiet_stream_that_cannot_send_tries_again()
{
    MeshCoreCompanion client;
    // Undrained: CMD_APP_START, CMD_DEVICE_QUERY and CMD_GET_CONTACTS are all
    // still in the ring, and the text below fills its fourth and last slot.
    open_a_contact_stream(client, false);

    core::MeshPeerId peer{};
    for (std::size_t i = 0; i < 32; ++i) {
        peer.public_key[i] = static_cast<std::uint8_t>(i + 1);
    }
    CHECK(client.send_private(peer, "Hello", core::WallTime{1000}).accepted());

    client.tick(at(6 + 3000));
    CHECK(!client.status().peers_complete);

    // Four frames, none of them the sync: the ring held what it already had.
    MeshCoreFrame frame{};
    for (int i = 0; i < 4; ++i) {
        CHECK(client.next_tx(frame));
        CHECK(frame.bytes[0] != 10);
    }
    CHECK(!client.next_tx(frame));

    // The window was kept, not spent: the ring is empty now and the next tick
    // does what the last one could not.
    client.tick(at(6 + 3001));
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 40);
    CHECK(client.status().peers_complete);
}

// WHAT A MISFIRE COSTS THE FACE, which the bench could not price. The 59-minute
// capture caught the sweep closing a walk that was not over, and on that node
// the mesh face showed nothing for it: `kRetainedPeers` is 16 against a 233-
// contact list, so `peers_retained < peers_reported` was already true and stayed
// true. The cost only exists below the cap -- on the short list `peers_complete`
// was added for, where the pair would otherwise count up:
// `apps/src/mesh.cpp:268` -- "        // final, and the pair would count up through `3/40`. Both numbers also".
// Two contacts announced, one delivered, and the sweep publishes 1 of 2.
void test_a_misfired_sweep_publishes_a_partial_pair()
{
    MeshCoreCompanion client;
    // CONTACTS_START announces two; `open_a_contact_stream` delivers one.
    open_a_contact_stream(client, true);
    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().peers_retained == 1);
    CHECK(!client.status().peers_complete);

    // The stream falls quiet mid-walk and the sweep believes it. The face's
    // condition is now satisfied on a partial list -- this is the misfire, and
    // 1/2 is what it publishes.
    client.tick(at(6 + 3000));
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 10);
    CHECK(client.status().peers_complete);
    CHECK(client.status().peers_retained == 1);
    CHECK(client.status().peers_reported == 2);

    // AND IT HEALS RATHER THAN LATCHING. The walk was not over; the second
    // contact arrives and the pair completes. `peers_complete` stays set --
    // the session has already spent its one CMD_SYNC_NEXT_MESSAGE -- so what
    // the wearer saw was seconds of 1/2, not a wrong number that stays.
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i) contact[1 + i] = static_cast<std::uint8_t>(i + 40);
    contact[33] = 1;
    std::memcpy(&contact[100], "Two", 3);
    CHECK(client.receive(contact, sizeof(contact), at(6 + 3100)));
    CHECK(client.status().peers_retained == 2);
    CHECK(client.status().peers_reported == 2);
    CHECK(client.status().peers_complete);
}

// A COORDINATE IN A MESSAGE, THROUGH THE FRAME THAT CARRIES IT.
//
// Every case here goes in as bytes and comes out of `remote_position()`,
// because a parser tested on its own proves nothing about the caller: the
// refusals that matter most in ADR-0021 -- an unresolved sender and a message
// this receiver truncated -- live in `accept_message`, not in the grammar.
void deliver_message(MeshCoreCompanion& client, const MeshPeer& peer,
                     const char* text, std::uint64_t when)
{
    std::uint8_t frame[16 + core::kMeshTextBytes + 64]{};
    frame[0] = 16;  // RESP_CODE_CONTACT_MSG_RECV_V3
    std::memcpy(&frame[4], peer.id.public_key.data(), 6);
    const std::size_t length = std::strlen(text);
    CHECK(16 + length <= sizeof(frame));
    std::memcpy(&frame[16], text, length);
    CHECK(client.receive(frame, 16 + length, at(when)));
}

// A SECOND RESOLVABLE CONTACT, delivered outside a walk on purpose:
// `RESP_CODE_CONTACT` is routed to `accept_contact()` whatever the stream state,
// which is how a node announces a contact it learned after the sync finished.
void add_a_second_contact(MeshCoreCompanion& client, std::uint64_t when)
{
    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i)
        contact[1 + i] = static_cast<std::uint8_t>(0x80 + i);
    contact[33] = 1;
    std::memcpy(&contact[100], "Other", 5);
    CHECK(client.receive(contact, sizeof(contact), at(when)));
}

// The same frame from a prefix no contact in the table matches.
void deliver_from_a_stranger(MeshCoreCompanion& client, const char* text,
                             std::uint64_t when)
{
    std::uint8_t frame[16 + core::kMeshTextBytes]{};
    frame[0] = 16;
    for (std::size_t i = 0; i < 6; ++i) frame[4 + i] = 0xEE;
    const std::size_t length = std::strlen(text);
    std::memcpy(&frame[16], text, length);
    CHECK(client.receive(frame, 16 + length, at(when)));
}

bool parsed_to(MeshCoreCompanion& client, const MeshPeer& peer, const char* text,
               std::uint64_t when, std::int32_t latitude, std::int32_t longitude)
{
    deliver_message(client, peer, text, when);
    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    if (!client.remote_position(who, position, arrived)) return false;
    return who == peer.id && position.latitude_e7 == latitude &&
           position.longitude_e7 == longitude;
}

void test_a_message_carries_a_coordinate_or_nothing()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(!client.remote_position(who, position, arrived));

    // THE SHAPE §14.2 OBSERVED, in both spacings, and with the sign it never
    // saw. A parser built only to what was captured drops every southern
    // coordinate or mirrors it, and does so silently.
    CHECK(parsed_to(client, peer, "On my way @12.3456,65.4321", 30, 123456000, 654321000));
    CHECK(parsed_to(client, peer, "Preset @ 55.9821,37.2104", 31, 559821000, 372104000));
    CHECK(parsed_to(client, peer, "South @-33.8688,-151.2093", 32, -338688000, -1512093000));
    CHECK(parsed_to(client, peer, "@0.0000,0.0001", 33, 0, 1000));
    // One to seven decimals, kept as given rather than rounded to the four that
    // were observed.
    CHECK(parsed_to(client, peer, "@1.5,2.25", 34, 15000000, 22500000));
    CHECK(parsed_to(client, peer, "@1.1234567,2.0", 35, 11234567, 20000000));

    // THE LAST MATCH WINS, so a quoted older message cannot steer the arrow.
    CHECK(parsed_to(client, peer, "was @10.0000,10.0000 now @20.0000,20.0000", 36,
                    200000000, 200000000));

    // AND A LAST MATCH THAT FAILS A BOUND TAKES NOTHING WITH IT. The earlier,
    // well-formed coordinate is not promoted: a fallback would reach for a
    // stale place exactly when the fresh one is malformed.
    deliver_message(client, peer, "was @10.0000,10.0000 now @91.0000,20.0000", 37);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(position.latitude_e7 == 200000000);  // still #36's, not #37's earlier one
    CHECK(arrived == at(36));

    // AND NEITHER DOES A LAST MATCH THAT FAILS THE *GRAMMAR*. This is the same
    // refusal paid for the other way a match can go wrong, and the shape that
    // arrives in the wild is the sender's own truncation: a cut on or before
    // the decimal point fails the grammar rather than a bound, so before this
    // was paid the quoted `10.0000` was published with message #38's fresh
    // stamp -- a place nobody sent, at a time nobody sent it.
    deliver_message(client, peer, "was @10.0000,10.0000 now @20.0000,2", 38);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(position.latitude_e7 == 200000000);  // still #36's
    CHECK(arrived == at(36));

    // A FULL STOP ENDS A SENTENCE, not a number's claim to be one. §14.2 does
    // not require the coordinate to come last, and a coordinate that is not
    // last is the one likeliest to carry punctuation.
    CHECK(parsed_to(client, peer, "@12.3456,65.4321. Буду через час", 39,
                    123456000, 654321000));
    CHECK(parsed_to(client, peer, "was @10.0000,10.0000 now @20.0000,20.0000.", 40,
                    200000000, 200000000));

    // AND AN ANCHORED `@` THAT NEVER BEGAN A NUMBER TAKES NOTHING WITH IT: a
    // mention is not a coordinate that failed, and the coordinate before it
    // still stands.
    CHECK(parsed_to(client, peer, "@12.3456,65.4321 cc @alice", 41,
                    123456000, 654321000));
}

void test_a_coordinate_that_is_not_one_is_refused()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};

    const char* refused[] = {
        "mail me at hleserg@12.3456,65.4321",  // the sigil must start or follow space
        "@name is not a place",
        "@12,65",                    // no decimal point
        "@12.,65.0",                 // no decimals
        "@1234.5678,65.4321",        // four integer digits
        "@12.12345678,65.4321",      // eight decimals
        "@100000000.0,0.5",          // the overflow §14.2 names by hand
        "@91.0000,20.0000",          // outside +-90, dropped and never clamped
        "@20.0000,181.0000",         // outside +-180
        // THE TWO THAT ONLY A 64-BIT RANGE TEST REFUSES. Both are inside the
        // grammar -- three integer digits, seven decimals -- and both are out
        // of range by four and five significant figures. Narrow them to the
        // `int32` the slot is made of first and they wrap back inside it:
        // 500.0000000 becomes 70.5032704 degrees of latitude and 999.9999999
        // becomes 141.0065407 of longitude, each a real place on the globe and
        // neither one anybody sent. This pair is the whole reason the bounds
        // are checked before the cast rather than after.
        "@500.0000000,1.0000000",
        "@1.0000000,999.9999999",
        "@0.0000,0.0000",            // exactly the null island
        "@12.3456;65.4321",          // the separator is a comma
        "@12.3456,",                 // no second number
        "@1.2,3.4.5",                // a further digit group still is not one
    };
    std::uint64_t when = 40;
    for (const char* text : refused) {
        deliver_message(client, peer, text, when++);
        CHECK(!client.remote_position(who, position, arrived));
        CHECK(std::strcmp(client.status().last_message.data(), text) == 0);
    }
}

// AN UNRESOLVED SENDER MEANS NO TARGET, NOT AN UNNAMED ONE -- ADR-0021
// decision 2. The text still reaches the screen; what it cannot do is name a
// place on behalf of nobody.
void test_a_coordinate_from_nobody_is_dropped()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);

    deliver_from_a_stranger(client, "@12.3456,65.4321", 50);
    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(std::strcmp(client.status().last_message.data(), "@12.3456,65.4321") == 0);
    CHECK(client.status().last_sender[0] == '\0');
}

// A MESSAGE OUR OWN RECEIVER CUT YIELDS NOTHING, whatever the remainder parses
// to. The tail is where the coordinate goes and the tail is what is lost, so
// what survives is a shorter number that passes every bound -- and is, in the
// report's worked case, about six hundred and fifty metres wrong.
void test_a_truncated_message_yields_no_coordinate()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));

    // 131 bytes into a buffer that keeps 128, so the coordinate loses its last
    // three characters and nothing else does. `copy_text` keeps `N - 1` where
    // `N` is `kMeshTextBytes + 1`, which is 128 -- an earlier revision of this
    // test said 127 and inherited the same error from the report, so the
    // surviving length is asserted below rather than described.
    //
    // 131 is chosen rather than round because it is the *longest* text whose
    // remainder still parses: at 132 the survivor is `@55.9821,37.` and at 133
    // it is `@55.9821,37`, and the grammar refuses both for carrying no decimal
    // place, so either would prove nothing about this guard.
    std::string text(114, 'x');
    text += " @55.9821,37.2104";
    CHECK(text.size() == 131);
    deliver_message(client, peer, text.c_str(), 60);
    CHECK(client.status().message_truncated);
    CHECK(std::strlen(client.status().last_message.data()) == core::kMeshTextBytes);
    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(!client.remote_position(who, position, arrived));

    // The mutation this is really guarding: what survived the cut is
    // `@55.9821,37.2`, which parses, is inside every bound, and is about 650 m
    // from where the sender is at this latitude.
    CHECK(std::strstr(client.status().last_message.data(), "@55.9821,37.2") != nullptr);
    CHECK(std::strstr(client.status().last_message.data(), "37.21") == nullptr);
}

// A SECOND PEER'S COORDINATE EVICTS THE FIRST, AND THE FIRST IS THEN STAMPED
// AFRESH. This is the half of ADR-0021 decision 5 that one slot cannot keep:
// A's third message is identical bytes from an unchanged sender, which the
// decision calls one observation, and it is stamped 300 anyway because B's
// coordinate erased the memory of A's. Pinned rather than fixed -- a coordinate
// per key is #304's stored table, not this branch -- and pinned rather than
// left implicit, because the comment above the guard used to promise the
// unconditional rule and this is the case that falsifies it.
void test_a_second_peer_restarts_the_first_peers_arrival()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer first{};
    CHECK(client.peer(0, first));
    add_a_second_contact(client, 90);
    MeshPeer second{};
    CHECK(client.peer(1, second));
    CHECK(!(first.id == second.id));

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};

    deliver_message(client, first, "@12.3456,65.4321", 100);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(who == first.id);
    CHECK(arrived == at(100));

    deliver_message(client, second, "@30.0000,40.0000", 200);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(who == second.id);
    CHECK(arrived == at(200));

    // Byte for byte what arrived at 100, from the same sender -- and stamped
    // 300, because nothing remembers that it was ever here.
    deliver_message(client, first, "@12.3456,65.4321", 300);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(who == first.id);
    CHECK(arrived == at(300));
}

// THE SAME COORDINATE TWICE IS ONE OBSERVATION. ADR-0021 decision 5 carries
// ADR-0020 decision 6: arrival is not an age, and re-stamping would make a
// place look fresher every time its owner said anything about it.
void test_an_unchanged_coordinate_is_not_re_stamped()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));

    deliver_message(client, peer, "@12.3456,65.4321", 70);
    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(client.remote_position(who, position, arrived));
    CHECK(arrived == at(70));

    deliver_message(client, peer, "Still here @12.3456,65.4321", 80);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(arrived == at(70));

    // A coordinate that moved is a new observation and is stamped.
    deliver_message(client, peer, "@12.3457,65.4321", 90);
    CHECK(client.remote_position(who, position, arrived));
    CHECK(position.latitude_e7 == 123457000);
    CHECK(arrived == at(90));
}

// A DISCONNECT TAKES IT, because the key it is filed under was resolved through
// a contact table that the next session rebuilds.
void test_a_reconnect_does_not_inherit_a_contact_coordinate()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    deliver_message(client, peer, "@12.3456,65.4321", 100);

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(client.remote_position(who, position, arrived));

    client.disconnected(at(110));
    CHECK(!client.remote_position(who, position, arrived));
}

// AND SO DOES A FORGET, which is the half the session teardown does not cover.
// `unpin()` clears `wrong_node_` on its way past, so after a forget the other
// half of the accessor's guard is already open and this flag is the whole of
// what stops the watch publishing a place it read out of the contact table of a
// node the owner has just repudiated. The node's own coordinate has been held
// against exactly this since it once was not; the contact's had nothing.
void test_forgetting_the_node_withdraws_a_contact_coordinate()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    deliver_message(client, peer, "@12.3456,65.4321", 100);

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(client.remote_position(who, position, arrived));

    // Bound first, so the forget is the one the owner performs rather than a
    // no-op on an unpinned session: `unpin()` returns true only when there was
    // a pin to clear, and the coordinate must go either way.
    client.pin(client.status().node_id);
    CHECK(client.unpin());
    CHECK(!client.remote_position(who, position, arrived));
}

// A CONTACT THE NODE DELETES TAKES ITS COORDINATE WITH IT (#650, ADR-0021
// decision 7): discarded, not aged. The key compared is the whole 32 bytes, so
// a deletion of a key sharing the held one's first 31 leaves the slot alone.
void test_a_deleted_contact_takes_its_coordinate()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    deliver_message(client, peer, "@12.3456,65.4321", 100);

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(client.remote_position(who, position, arrived));

    std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
    std::memcpy(&deleted[1], peer.id.public_key.data(), core::kMeshPublicKeyBytes);
    deleted[core::kMeshPublicKeyBytes] ^= 0x01;
    CHECK(client.receive(deleted, sizeof(deleted), at(110)));
    CHECK(client.remote_position(who, position, arrived));
    CHECK(who == peer.id);

    deleted[core::kMeshPublicKeyBytes] ^= 0x01;
    CHECK(client.receive(deleted, sizeof(deleted), at(120)));
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(client.malformed_frames() == 0);
}

// A SHORT DELETE IS NOT A DELETE. `0x8F` is `[opcode][pub_key x32]`; a frame
// one byte short, or the opcode alone, is refused and counted before any key is
// compared -- and the frames are exact-sized stack arrays, so an arm that
// trusted its length would over-read them.
void test_a_short_contact_deleted_push_is_malformed()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    deliver_message(client, peer, "@12.3456,65.4321", 100);

    std::uint8_t short_by_one[core::kMeshPublicKeyBytes] = {0x8F};
    std::memcpy(&short_by_one[1], peer.id.public_key.data(),
                core::kMeshPublicKeyBytes - 1);
    CHECK(!client.receive(short_by_one, sizeof(short_by_one), at(110)));
    CHECK(client.malformed_frames() == 1);

    const std::uint8_t opcode_alone[] = {0x8F};
    CHECK(!client.receive(opcode_alone, sizeof(opcode_alone), at(120)));
    CHECK(client.malformed_frames() == 2);

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(client.remote_position(who, position, arrived));
    CHECK(who == peer.id);
}

// A DELETE INSIDE A WALK DIRTIES IT, WHATEVER ITS LENGTH. The code alone is
// the evidence that the table moved (ADR-0022 decision 3); the key only decides
// whose coordinate goes. So a one-byte `0x8F` is malformed and still dirty, and
// a matching one both empties the slot and dirties the walk -- the arm's two
// halves together, which the deletion test above runs outside a walk.
void test_a_delete_inside_a_walk_dirties_it_whatever_its_length()
{
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    {
        MeshCoreCompanion client;
        open_a_contact_stream(client, true);
        const std::uint8_t opcode_alone[] = {0x8F};
        CHECK(!client.receive(opcode_alone, sizeof(opcode_alone), at(7)));
        CHECK(client.malformed_frames() == 1);
        CHECK(client.receive(end, sizeof(end), at(8)));
        CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);
    }

    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshPeer peer{};
    CHECK(client.peer(0, peer));
    deliver_message(client, peer, "@12.3456,65.4321", 100);

    const std::uint8_t start[] = {2, 1, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(110)));
    std::uint8_t deleted[1 + core::kMeshPublicKeyBytes] = {0x8F};
    std::memcpy(&deleted[1], peer.id.public_key.data(), core::kMeshPublicKeyBytes);
    CHECK(client.receive(deleted, sizeof(deleted), at(111)));
    CHECK(client.receive(end, sizeof(end), at(112)));

    core::MeshPeerId who{};
    core::Position position{};
    core::MonotonicTime arrived{};
    CHECK(!client.remote_position(who, position, arrived));
    CHECK(client.status().snapshot == core::MeshSnapshot::Dirty);
    CHECK(client.malformed_frames() == 0);
}

// ROWS 1-4 OF THE RESEARCH REPORT'S SECTION 11.1: THE CAP REFUSES AT THE BYTE.
//
// `kMeshTextBytes` is 128 and it is a count of BYTES. The whole reason the row
// exists is that a character counter would be right in English and wrong by a
// factor of two in Russian, where a letter is two bytes, and wrong by four on
// an emoji -- so the boundary is asserted at 127, 128 and 129 with the 129th
// byte being the second half of a code point, which is the case a truncating
// implementation would repair instead of refusing.
void test_the_text_budget_is_counted_in_bytes_and_refused_not_repaired()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    // Row 1: 127 and 128 go, 129 does not. One client per send, because a
    // send that is accepted holds the slot and the next would be refused as
    // Busy rather than for its length -- which is a different refusal and
    // would make this row prove nothing.
    for (const std::size_t length : {std::size_t{127}, std::size_t{128}}) {
        MeshCoreCompanion fresh;
        connect_and_handshake(fresh);
        MeshService on(fresh);
        MeshPeer to{};
        CHECK(on.peer(0, to));
        const std::string body(length, 'a');
        const auto result = on.send_private(to.id, body, WallTime{1000});
        CHECK(result.accepted());
        CHECK(result.refusal == core::MeshSendRefusal::None);
        MeshCoreFrame frame{};
        CHECK(fresh.next_tx(frame));
        // Thirteen bytes of header, then the body entire -- nothing shortened
        // on the way out.
        CHECK(frame.size == 13 + length);
    }
    {
        const std::string body(129, 'a');
        const auto result = service.send_private(peer.id, body, WallTime{1000});
        CHECK(!result.accepted());
        CHECK(result.refusal == core::MeshSendRefusal::BodyTooLong);
        CHECK(service.status().delivery == MeshDelivery::None);
    }

    // Row 2: a two-byte code point straddling byte 128. 127 bytes of Latin and
    // then one Cyrillic letter is 129, and the letter's first byte is the
    // 128th. A client that counted characters would send it; one that
    // truncated at 128 would put half a character on the air.
    {
        std::string body(127, 'a');
        body += "\xd0\xb0";  // U+0430 CYRILLIC SMALL LETTER A
        CHECK(body.size() == 129);
        const auto result = service.send_private(peer.id, body, WallTime{1000});
        CHECK(!result.accepted());
        CHECK(result.refusal == core::MeshSendRefusal::BodyTooLong);
        // AND THE REPAIR IS AVAILABLE AND IS NOT TAKEN. The boundary helper
        // says the body's longest whole-code-point prefix within 128 bytes ends
        // at 127 -- so a caller that wants to shorten can, at a boundary, and
        // this layer still refuses rather than doing it silently.
        CHECK(core::utf8_prefix_length(body, 128) == 127);
    }

    // Row 3: the same for the code point that costs four. 125 bytes of Latin
    // and one emoji is 129, and the emoji spans bytes 126..129.
    {
        std::string body(125, 'a');
        body += "\xf0\x9f\x8c\x8d";  // U+1F30D EARTH GLOBE EUROPE-AFRICA
        CHECK(body.size() == 129);
        const auto result = service.send_private(peer.id, body, WallTime{1000});
        CHECK(!result.accepted());
        CHECK(result.refusal == core::MeshSendRefusal::BodyTooLong);
        CHECK(core::utf8_prefix_length(body, 128) == 125);
    }

    // Row 4: upstream's own boundary is 160, and it is refused HERE. The
    // asymmetry is asserted rather than assumed: 128 out and 128 in is one
    // number, and raising the outbound cap alone would make this product emit
    // messages its own receiver truncates.
    {
        const std::string body(160, 'a');
        const auto result = service.send_private(peer.id, body, WallTime{1000});
        CHECK(!result.accepted());
        CHECK(result.refusal == core::MeshSendRefusal::BodyTooLong);
    }

    // AND A BODY THAT ARRIVED ALREADY CUT is refused too, with its own reason.
    // This is the shape a caller produces by shortening with `substr`: under
    // budget, and not a message.
    {
        const std::string body = std::string("\xd0") + "";  // a lone lead byte
        CHECK(body.size() == 1);
        const auto result = service.send_private(peer.id, body, WallTime{1000});
        CHECK(!result.accepted());
        CHECK(result.refusal == core::MeshSendRefusal::BodyNotUtf8);
    }
    // An empty body is its own refusal and not `BodyNotUtf8`: nothing to send
    // is a different mistake from something unsendable.
    CHECK(service.send_private(peer.id, "", WallTime{1000}).refusal ==
          core::MeshSendRefusal::EmptyBody);
    // Nothing above was accepted, so nothing above published a delivery state.
    // ADR-0023 decision 3: a local refusal is an answer to the call.
    CHECK(service.status().delivery == MeshDelivery::None);
    CHECK(service.status().request_id == 0);
}

// THE BOUNDARY HELPER ITSELF, because the send path uses it to decide and a
// caller uses it to shorten, and a helper that is wrong is wrong in both.
void test_the_utf8_boundary_stops_at_a_code_point()
{
    using core::utf8_prefix_length;
    CHECK(utf8_prefix_length("", 10) == 0);
    CHECK(utf8_prefix_length("abc", 10) == 3);
    CHECK(utf8_prefix_length("abc", 2) == 2);
    // Two bytes, cut in the middle: the prefix ends before the letter.
    CHECK(utf8_prefix_length("a\xd0\xb0", 2) == 1);
    CHECK(utf8_prefix_length("a\xd0\xb0", 3) == 3);
    // Four bytes, cut at every offset inside it.
    for (std::size_t limit = 1; limit <= 4; ++limit) {
        CHECK(utf8_prefix_length("\xf0\x9f\x8c\x8d", limit) == (limit == 4 ? 4 : 0));
    }
    // A continuation byte with nothing leading it is not a character.
    CHECK(utf8_prefix_length("\xb0", 4) == 0);
    // An over-long form -- '/' written as two bytes -- is refused rather than
    // decoded, which is the classic way a filter is walked past.
    CHECK(utf8_prefix_length("\xc0\xaf", 4) == 0);
    // A surrogate half, which UTF-8 never encodes.
    CHECK(utf8_prefix_length("\xed\xa0\x80", 4) == 0);
    // Past U+10FFFF.
    CHECK(utf8_prefix_length("\xf7\xbf\xbf\xbf", 4) == 0);
    // The limit may exceed the string; the string wins.
    CHECK(utf8_prefix_length("\xd0\xb0", 99) == 2);
}

// ROW 7: A LOCAL REQUEST ID, NON-ZERO AND DISTINCT, AND IT IS NOT THE NODE'S
// ACK TAG.
void test_a_request_id_is_local_non_zero_and_distinct()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    CHECK(service.status().request_id == 0);  // no request yet
    const auto first = service.send_private(peer.id, "one", WallTime{1000});
    CHECK(first.accepted() && first.request_id != 0);
    CHECK(service.status().request_id == first.request_id);

    // A refusal issues no id and does not disturb the live one.
    const auto refused = service.send_private(peer.id, "two", WallTime{1001});
    CHECK(!refused.accepted() && refused.request_id == 0);
    CHECK(refused.refusal == core::MeshSendRefusal::Busy);
    CHECK(service.status().request_id == first.request_id);

    // End the first, then a second send gets a different id.
    client.tick(at(8));
    client.tick(at(8 + 15000));
    CHECK(!client.send_busy());
    const auto second = service.send_private(peer.id, "two", WallTime{1002});
    CHECK(second.accepted());
    CHECK(second.request_id != first.request_id);
    CHECK(service.status().request_id == second.request_id);

    // AND IT IS NOT THE NODE'S TAG. The ack tag here is 01 02 03 04, which is
    // 0x04030201 little-endian -- a number the node computed and this client
    // copied. Nothing in the id is derived from it.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8 + 15001)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(service.status().request_id == second.request_id);
    CHECK(second.request_id != 0x04030201U);
    CHECK(second.request_id != 0x01020304U);
}

// ROWS 10, 11 AND 12: THE ACKNOWLEDGEMENT'S TIMING, AND THE ROW THIS WHOLE
// CHANGE EXISTS TO PRODUCE.
void test_an_ack_after_the_budget_upgrades_unconfirmed_to_confirmed()
{
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    const std::uint8_t ack[] = {0x82, 1, 2, 3, 4, 0, 0, 0, 0};

    // Row 12 first, and it is the defect: an expired budget is `Unconfirmed`,
    // and specifically NOT `Failed`. `Failed` said *"не доставлено"* -- not
    // delivered -- about a message the node had accepted and which the wire
    // cannot say anything negative about at all.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        MeshPeer peer{};
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        CHECK(service.status().delivery == MeshDelivery::Accepted);
        client.tick(at(8 + 2406));
        CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
        CHECK(!client.send_busy());

        // Row 11: the node has no notion of this client's budget and clears its
        // own ack table only on a match, so a confirmation after the budget is
        // ordinary traffic. It is positive proof against the absence of proof,
        // and the absence loses.
        CHECK(client.receive(ack, sizeof(ack), at(8 + 30000)));
        CHECK(service.status().delivery == MeshDelivery::Confirmed);
        CHECK(!client.send_busy());

        // Row 10: a duplicate changes nothing and is not counted malformed.
        const std::uint32_t malformed = client.malformed_frames();
        CHECK(client.receive(ack, sizeof(ack), at(8 + 30001)));
        CHECK(service.status().delivery == MeshDelivery::Confirmed);
        CHECK(client.malformed_frames() == malformed);
    }

    // AND THE UPGRADE IS BOUNDED BY THE REQUEST, NOT BY THE CLOCK. Once the
    // request has been replaced, a match has nothing to attach to -- the tag is
    // a keyed hash of timestamp, attempt and text and repeats for identical
    // messages in the same second, so an unattached match would be evidence
    // about some other message.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        MeshPeer peer{};
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "first", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        client.tick(at(8 + 2406));
        CHECK(service.status().delivery == MeshDelivery::Unconfirmed);

        // A second send replaces the request. It is `Queued`, not
        // `Unconfirmed`, so the late-ack arm cannot fire for it.
        CHECK(service.send_private(peer.id, "second", WallTime{1001}).accepted());
        CHECK(service.status().delivery == MeshDelivery::Queued);
        CHECK(client.receive(ack, sizeof(ack), at(8 + 3000)));
        CHECK(service.status().delivery == MeshDelivery::Queued);
    }

    // Row 9: an ack that matches nothing changes nothing, and is not malformed
    // either -- a well-formed acknowledgement for another message is a
    // correlation outcome, not a bad frame.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        MeshPeer peer{};
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        const std::uint32_t malformed = client.malformed_frames();
        const std::uint8_t other[] = {0x82, 9, 9, 9, 9, 0, 0, 0, 0};
        CHECK(client.receive(other, sizeof(other), at(9)));
        CHECK(service.status().delivery == MeshDelivery::Accepted);
        CHECK(client.send_busy());
        CHECK(client.malformed_frames() == malformed);
    }

    // Row 15's second half: a reconnect does not resurrect the request. The
    // disconnect leaves `Unknown`, `reset_session()` zeroes the tag, and a
    // match arriving afterwards has nothing to attach to. `Unknown` is
    // deliberately not upgradeable for exactly this reason.
    {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        MeshPeer peer{};
        CHECK(service.peer(0, peer));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        CHECK(client.receive(sent, sizeof(sent), at(8)));
        CHECK(service.status().delivery == MeshDelivery::Accepted);
        client.disconnected(at(9));
        CHECK(service.status().delivery == MeshDelivery::Unknown);
        connect_and_handshake(client);
        CHECK(service.status().delivery == MeshDelivery::Unknown);
        CHECK(client.receive(ack, sizeof(ack), at(200)));
        CHECK(service.status().delivery == MeshDelivery::Unknown);
    }
}

// ROW 18: THE NODE'S ACK TAG IS A HASH AND CAN REPEAT, AND NOTHING HERE MAY BE
// WRITTEN AS IF IT WERE A MESSAGE ID.
//
// Upstream computes it from timestamp, attempt and text -- the recipient's key
// is not an input -- so two identical messages in the same second produce the
// same four bytes. This client never computes one; it copies the node's. So the
// collision is asserted through the seam the host has: two sends, two
// RESP_CODE_SENT frames carrying the SAME tag, and each verdict belonging to
// the request that was in flight when its frame arrived.
void test_an_identical_ack_tag_does_not_confirm_the_earlier_request()
{
    const std::uint8_t sent[] = {6, 0, 7, 7, 7, 7, 0x66, 0x09, 0, 0};
    const std::uint8_t ack[] = {0x82, 7, 7, 7, 7, 0, 0, 0, 0};

    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    const auto first = service.send_private(peer.id, "same body", WallTime{1000});
    CHECK(first.accepted());
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(service.status().request_id == first.request_id);

    // The first is given up on. Its tag is still in `expected_ack_`, which is
    // what makes row 11 possible and what makes this row necessary.
    client.tick(at(8 + 2406));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);

    // The same body, the same second, so the node produces the same tag.
    const auto second = service.send_private(peer.id, "same body", WallTime{1000});
    CHECK(second.accepted());
    CHECK(second.request_id != first.request_id);
    CHECK(service.status().delivery == MeshDelivery::Queued);
    CHECK(service.status().request_id == second.request_id);

    // THE SECOND'S RESP_CODE_SENT IS THE SECOND'S. The identical tag does not
    // make it an answer to the first, and the state it publishes is the
    // second's `Accepted` -- not an upgrade of the first's `Unconfirmed`.
    CHECK(client.receive(sent, sizeof(sent), at(9)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(service.status().request_id == second.request_id);

    // And the confirmation that follows confirms the second, which is the one
    // in flight. The first stays what the wire left it: given up on, and never
    // retrospectively confirmed by a tag that belongs to a different message.
    CHECK(client.receive(ack, sizeof(ack), at(10)));
    CHECK(service.status().delivery == MeshDelivery::Confirmed);
    CHECK(service.status().request_id == second.request_id);
}

// ROW 17: `ERR_CODE_TABLE_FULL` IS NOT "THE NODE IS FULL".
//
// The same error code answers a text that is too long, so a screen that named
// the node's contact table would be telling an owner to delete contacts because
// their message was long. Every `RESP_CODE_ERR` for an accepted command reaches
// the owner as one word -- `Refused` -- and the code stays in the log.
void test_an_error_code_is_not_shown_to_the_owner_as_a_reason()
{
    // 4 is ERR_CODE_TABLE_FULL and 1 is ERR_CODE_UNSUPPORTED_CMD; upstream also
    // answers 4 for a text over its own length bound.
    for (const std::uint8_t code : {std::uint8_t{4}, std::uint8_t{1}, std::uint8_t{2}}) {
        MeshCoreCompanion client;
        connect_and_handshake(client);
        MeshService service(client);
        MeshPeer peer{};
        CHECK(service.peer(0, peer));
        // The handshake's own CMD_GET_CUSTOM_VARS is the older outstanding
        // command, so an untagged error is its before it is the send's.
        // Answering it first is what a node that defines opcode 40 does.
        const std::uint8_t vars[] = {21};
        CHECK(client.receive(vars, sizeof(vars), at(7)));
        CHECK(service.send_private(peer.id, "text", WallTime{1000}).accepted());
        const std::uint8_t error[] = {1, code};
        CHECK(client.receive(error, sizeof(error), at(8)));
        // One verdict, whatever the code was. The distinction the owner needs
        // is Refused versus Unconfirmed, which is about whether a resend can
        // duplicate -- and that is the same answer for all three codes.
        CHECK(service.status().delivery == MeshDelivery::Refused);
        CHECK(!client.send_busy());
    }
}

// ROW 6: THE SIX-BYTE NARROWING HAPPENS ONCE, IN THE ADAPTER.
//
// A full 32-byte identity goes in and the frame carries its first six bytes at
// offsets 7..12. The prefix is what MeshCore's private-message frame addresses
// by; it is not what an application holds, and this is the one place the two
// meet.
void test_the_recipient_narrows_to_six_bytes_in_the_frame_and_nowhere_else()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    // The fixture's contact is keyed 1..32, so every byte is distinct and a
    // frame built from the wrong offset would be visible.
    for (std::size_t i = 0; i < core::kMeshPublicKeyBytes; ++i) {
        CHECK(peer.id.public_key[i] == static_cast<std::uint8_t>(i + 1));
    }
    CHECK(service.send_private(peer.id, "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    CHECK(frame.bytes[0] == 2);
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(frame.bytes[7 + i] == peer.id.public_key[i]);
    }
    // And the seventh byte of the key is NOT in the frame where the text
    // begins: the body starts at 13.
    CHECK(frame.bytes[13] == 'h' && frame.bytes[14] == 'i');
    CHECK(frame.size == 13 + 2);
}

// A REFUSED SEND LEAVES THE PREVIOUS MESSAGE'S VERDICT ALONE. This is the test
// that replaced the one for `send_abandoned()`, and it asserts the opposite of
// what that function did. The previous send is `Unconfirmed`: the node accepted
// it, nothing acknowledged it, and the owner has been told a resend may
// duplicate. A *second* send is then refused locally -- over the byte budget,
// which never reaches the provider's wire path at all. ADR-0023 decision 3 says
// that refusal is not a delivery state; it must therefore not overwrite one,
// and clearing the id would be worse still, because on `Busy` the id it clears
// belongs to the request still in flight.
void test_a_refused_send_does_not_erase_the_previous_verdict()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    const auto first = service.send_private(peer.id, "unacknowledged", WallTime{1000});
    CHECK(first.accepted());
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    client.tick(at(8 + 3000));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
    CHECK(!client.send_busy());

    const std::string too_long(core::kMeshTextBytes + 1, 'x');
    const auto second = service.send_private(peer.id, too_long, WallTime{2000});
    CHECK(!second.accepted());
    CHECK(second.refusal == core::MeshSendRefusal::BodyTooLong);
    CHECK(second.request_id == 0);
    // The refusal is the caller's answer, and the panel still says what the
    // wire last supported about the message that exists.
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
    CHECK(service.status().request_id == first.request_id);
}

// THE ROOM PATH IS ONE CALL IN TWO PHASES AND MUST PUBLISH AGAINST ONE ID.
// `send_room()` returns an id while the login is on the wire; the text is
// enqueued from the login's answer, and used to mint a second one there. A
// caller holding the first would then watch every verdict -- `Accepted`,
// `Confirmed`, `Unconfirmed`, `Unknown` -- go past under a number it does not
// hold, which is ADR-0023 decision 8 read backwards. Nothing misbehaves today
// only because the one caller reads `accepted()` and throws the id away.
void test_a_room_login_keeps_the_request_id_its_caller_was_given()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    std::array<std::uint8_t, core::kMeshPublicKeyBytes> room{};
    room.fill(0x44);
    const auto result = client.send_room(room, "secret", "hi", WallTime{1000});
    CHECK(result.accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 26);

    // The login's own RESP_CODE_SENT, then the node's success push carrying the
    // room key's first six bytes -- both are what `kPushLoginSuccess` demands
    // before it will enqueue the text.
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(9)));
    std::uint8_t success[] = {0x85, 0, 0, 0, 0, 0, 0, 0};
    std::memcpy(&success[2], room.data(), 6);
    CHECK(client.receive(success, sizeof(success), at(10)));
    CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
    CHECK(client.status().request_id == result.request_id);
}

// A ZEROED `expected_ack_` IS NOT A TAG, AND `82 00 00 00 00` MUST NOT MATCH IT.
// `reset_session()` zeroes the tag but leaves a settled `Unconfirmed` alone --
// deliberately: a disconnect is not evidence against a verdict the budget
// already reached. Before the guard, those two facts combined into a false
// `Confirmed`: five bytes of zeros memcmp'd equal, and decision 2a's late-ack
// arm upgraded a message nothing had ever acknowledged to "доставлено". That is
// the one claim ADR-0023 decision 6 forbids outright, and it turns decision 7's
// "a resend may duplicate" into "it arrived".
void test_a_zero_tag_does_not_confirm_a_settled_unconfirmed()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));

    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(service.send_private(peer.id, "unacknowledged", WallTime{1000}).accepted());
    CHECK(client.receive(sent, sizeof(sent), at(8)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    client.tick(at(8 + 3000));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);

    // The reconnect. `reset_session()` runs three times over this sequence and
    // each one zeroes the tag; none of them touches the settled verdict,
    // because `send_busy()` is already false.
    client.disconnected(at(70));
    client.begin(at(71));
    client.connected(at(72));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);

    const std::uint8_t zero_ack[] = {0x82, 0, 0, 0, 0};
    CHECK(client.receive(zero_ack, sizeof(zero_ack), at(73)));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);

    // And the guard is about the *absence* of a tag, not about this frame: a
    // non-zero tag nothing is waiting for is still simply a mismatch.
    const std::uint8_t other_ack[] = {0x82, 9, 9, 9, 9};
    CHECK(client.receive(other_ack, sizeof(other_ack), at(74)));
    CHECK(service.status().delivery == MeshDelivery::Unconfirmed);
}


// ---------------------------------------------------------------------------
// The node is the address book (#573 §8).
//
// `connect_and_handshake` leaves exactly one contact retained, keyed 1..32.
// Every test below sends to a key the window does *not* hold, which is the
// whole point: sixteen slots against a node MEASURED holding 233 contacts.

// A key made of one repeated byte, so it cannot collide with the fixture's
// 1..32 contact in any prefix and a frame built from the wrong offset shows.
core::MeshPeerId absent_key(std::uint8_t byte)
{
    core::MeshPeerId id{};
    id.public_key.fill(byte);
    return id;
}

// The reply the node sends to CMD_GET_CONTACT_BY_KEY: the same response code
// the walk uses, which is hazard 1.
void fetched_contact(std::uint8_t (&out)[148], const core::MeshPeerId& id,
                     std::uint8_t advert_type)
{
    std::memset(out, 0, sizeof(out));
    out[0] = 3;
    std::memcpy(&out[1], id.public_key.data(), id.public_key.size());
    out[33] = advert_type;
    std::memcpy(&out[100], "Far", 3);
}

void test_a_send_to_an_unretained_key_asks_the_node_for_the_contact()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    const auto far = absent_key(0xB7);

    const auto result = service.send_private(far, "up here", WallTime{1000});
    CHECK(result.accepted());
    // Accepted, and `Queued` -- a fetch is a round trip to the node over BLE
    // with no radio in it, exactly like the room login's first phase.
    CHECK(service.status().delivery == MeshDelivery::Queued);

    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    // Opcode 30, then the WHOLE key: no prefix, no iteration, no ambiguity.
    CHECK(frame.size == 1 + core::kMeshPublicKeyBytes);
    CHECK(frame.bytes[0] == 30);
    for (std::size_t i = 0; i < core::kMeshPublicKeyBytes; ++i) {
        CHECK(frame.bytes[1 + i] == 0xB7);
    }
    CHECK(!client.next_tx(frame));

    // The node names the contact, and the text goes out behind it, narrowed to
    // six bytes in the frame and nowhere else.
    std::uint8_t reply[148];
    fetched_contact(reply, far, 1);
    CHECK(client.receive(reply, sizeof(reply), at(10)));
    CHECK(client.next_tx(frame));
    CHECK(frame.bytes[0] == 2);
    for (std::size_t i = 0; i < 6; ++i) CHECK(frame.bytes[7 + i] == 0xB7);
    CHECK(frame.size == 13 + 7);
    CHECK(std::memcmp(&frame.bytes[13], "up here", 7) == 0);
    CHECK(service.status().delivery == MeshDelivery::Queued);
}

// HAZARD 1. The reply is the iteration's frame, and the fetch exists precisely
// because this contact is not in the sixteen. Folding it in would grow the
// cache with a contact nobody asked to retain -- and on a full window it would
// be dropped in silence instead, which is the case that matters.
void test_a_fetched_contact_does_not_enter_the_retained_window()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    const std::uint16_t before = service.status().peers_retained;
    CHECK(before == 1);

    const auto far = absent_key(0xC4);
    CHECK(service.send_private(far, "hi", WallTime{1000}).accepted());
    std::uint8_t reply[148];
    fetched_contact(reply, far, 1);
    CHECK(client.receive(reply, sizeof(reply), at(10)));

    CHECK(service.status().peers_retained == before);
    CHECK(service.peer_count() == 1);
    MeshPeer kept{};
    CHECK(service.peer(0, kept));
    CHECK(kept.id.public_key[0] == 1);  // still the fixture's contact
}

// HAZARD 2. `accept_contact()` drops any advert type that is not chat, which is
// right for a list and wrong for a targeted fetch: the node HAS this key and it
// is a repeater. Absorbing that would leave the send with no terminal state at
// all, and the screen holding `Queued` for the session.
void test_a_fetched_contact_that_is_not_a_chat_contact_is_refused()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    const auto far = absent_key(0x5A);
    CHECK(service.send_private(far, "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 30);

    std::uint8_t reply[148];
    fetched_contact(reply, far, 3);  // a Room Server, not a chat contact
    CHECK(client.receive(reply, sizeof(reply), at(10)));

    // `Refused` and not a timeout: nothing reached the radio, so a resend
    // cannot duplicate anything -- what has to change is the recipient.
    CHECK(service.status().delivery == MeshDelivery::Refused);
    CHECK(!client.send_busy());
    CHECK(!client.next_tx(frame));  // and no text went out
}

// HAZARD 3, AND THE PLACEMENT IS THE SECOND LINE, NOT THE FIRST.
//
// A fetch reply refreshing `last_contact_at_` would move the boundary that
// decides a stream has gone quiet. Taking the frame above that stamp is why it
// cannot -- but nothing can currently reach that code, because the fetch is
// refused while any walk is running, which is the test below and the one after
// it. The placement is kept because the exclusion is a condition somebody can
// widen and the stamp is not obviously downstream of it.
//
// A RE-READ IS THE CASE "IS A WALK OPEN" DOES NOT COVER. `contacts_open_` is
// deliberately false throughout one, so `retry_*` is what has to be asked --
// and a re-read is exactly the walk whose end has no boundary frame to arrive,
// only the quiet sweep this stamp feeds.
void test_a_fetch_waits_for_a_re_read_too()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshService service(client);
    CHECK(service.status().snapshot == core::MeshSnapshot::Dirty);

    // The re-read is asked for on the delay, and is then outstanding.
    client.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(client) == 1);
    const auto during_retry =
        service.send_private(absent_key(0x88), "hi", WallTime{1000});
    CHECK(!during_retry.accepted());
    CHECK(during_retry.refusal == core::MeshSendRefusal::ContactsBusy);

    // And once the node starts streaming it, still refused -- this is the half
    // that runs with `contacts_open_` false.
    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(8 + 10002)));
    CHECK(service.status().snapshot == core::MeshSnapshot::RetryPending);
    const auto during_stream =
        service.send_private(absent_key(0x88), "hi", WallTime{1000});
    CHECK(!during_stream.accepted());
    CHECK(during_stream.refusal == core::MeshSendRefusal::ContactsBusy);
    CHECK(service.status().delivery == MeshDelivery::None);
}

// The node looked and does not have it. That is an answer to give the owner,
// not a reason to widen the search -- §8.3's third refusal.
void test_a_key_the_node_does_not_hold_is_refused()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    CHECK(service.send_private(absent_key(0x91), "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 30);

    // The handshake's own CMD_GET_CUSTOM_VARS is still outstanding and is the
    // *older* claimant for an untagged error, so a node that defines opcode 40
    // answers it first -- as every pre-existing test here does.
    const std::uint8_t vars[] = {21};
    CHECK(client.receive(vars, sizeof(vars), at(9)));
    // RESP_CODE_ERR carries no tag. The fetch is now the only command
    // outstanding, and it is the oldest, so it takes it.
    const std::uint8_t error[] = {1, 2};  // ERR_CODE_NOT_FOUND
    CHECK(client.receive(error, sizeof(error), at(10)));
    CHECK(service.status().delivery == MeshDelivery::Refused);
    CHECK(!client.send_busy());
}

// A fetch the node never answers ends on the budget, and ends as `Refused`.
//
// Three verdicts were arguable and two are wrong. `Unconfirmed` would invent
// an acceptance the node never gave. `Unknown` -- which this row asserted
// until #599 round 1 -- says *a frame may already be on the characteristic,
// weigh a duplicate before resending*, and no `CMD_SEND_TXT_MSG` was ever
// built: `take_fetched_contact()` clears `awaiting_contact_` before
// `enqueue_private()` runs, so the expiry can only fire with the fetch alone
// outstanding. Nothing reached the air, so `Refused` -- which is also what the
// same fetch answered `ERR_CODE_NOT_FOUND` yields, one row above. Identical
// ground truth must not reach the owner two ways depending on whether the node
// troubled itself to reply.
void test_a_fetch_the_node_never_answers_expires_refused()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    CHECK(service.send_private(absent_key(0x33), "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 30);

    client.tick(at(10));
    CHECK(service.status().delivery == MeshDelivery::Queued);
    client.tick(at(10 + 15000 + 1));
    CHECK(service.status().delivery == MeshDelivery::Refused);
    CHECK(!client.send_busy());
}

// AND THE LINK GOING IS THE THIRD ROUTE TO THE SAME GROUND TRUTH. The budget
// arm and the `ERR_CODE_NOT_FOUND` arm both answer `Refused`; a disconnect
// with the fetch outstanding used to answer `Unknown` through
// `reset_session()`, so one fact -- no `CMD_SEND_TXT_MSG` was ever built --
// reached the owner two ways depending on how the session ended. `fault()`
// shares the route, which is why one of these two tests is enough for it.
void test_a_fetch_the_link_drops_under_is_refused()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    CHECK(service.send_private(absent_key(0x33), "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 30);

    client.tick(at(10));
    CHECK(service.status().delivery == MeshDelivery::Queued);
    client.disconnected(at(20));
    CHECK(service.status().delivery == MeshDelivery::Refused);
    CHECK(!client.send_busy());
}

// AND A TEXT THE LINK DROPS UNDER IS STILL `Unknown`, which is what keeps the
// row above from being "a disconnect always refuses". That frame left the ring
// and may be on the characteristic already -- §11.1 row 14.
void test_a_text_the_link_drops_under_stays_unknown()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 2);

    client.tick(at(10));
    client.disconnected(at(20));
    CHECK(service.status().delivery == MeshDelivery::Unknown);
    CHECK(!client.send_busy());
}

// AND A TEXT THE NODE NEVER ANSWERS STILL EXPIRES `Unknown`, which is what
// keeps the row above from being a rename. Here the frame did leave the ring.
void test_a_text_the_node_never_answers_expires_unknown()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    MeshPeer peer{};
    CHECK(service.peer(0, peer));
    CHECK(service.send_private(peer.id, "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 2);

    client.tick(at(10));
    client.tick(at(10 + 15000 + 1));
    CHECK(service.status().delivery == MeshDelivery::Unknown);
    CHECK(!client.send_busy());
}

// THE WALK EXCLUSION IS TWO-SIDED, and this is the side `send_private()`
// cannot defend. Its refusal is checked once, at the call; the re-read leaves
// from `tick()`, so a walk can start *after* a fetch is outstanding. Then the
// walk's own `RESP_CODE_CONTACT` for that key is consumed as the fetch's
// reply, and the genuine reply folds into the retained window as a contact no
// walk listed.
//
// `test_a_fetch_waits_for_a_re_read_too()` is this row with the two steps in
// the other order, and it passes either way -- which is why the hazard needed
// its own row rather than an argument from that one.
// AND IF A WALK STARTS ANYWAY, IT OWNS THE FRAME. Both senders of
// `CMD_GET_CONTACTS` are excluded from overlapping a fetch, so this shape
// needs the node to volunteer a `RESP_CODE_CONTACTS_START` -- which the arm
// accepts, because refusing an unsolicited one would be a claim about upstream
// firmware this project has not traced. The point of the row is that the
// intercept holds its own precondition rather than resting on the two call
// sites: a third sender added later does not silently start feeding walk rows
// to a fetch.
void test_a_walk_that_starts_anyway_owns_the_contact_frame()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    const auto far = absent_key(0x44);
    CHECK(service.send_private(far, "hi", WallTime{1000}).accepted());
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame) && frame.bytes[0] == 30);

    const std::uint8_t start[] = {2, 1, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(10)));

    // The very key the fetch is waiting for, inside the walk.
    std::uint8_t reply[148];
    fetched_contact(reply, far, 1);
    CHECK(client.receive(reply, sizeof(reply), at(11)));

    // It went to the walk, and the fetch is still outstanding.
    CHECK(client.peer_count() == 1);
    CHECK(client.send_busy());
    CHECK(service.status().delivery == MeshDelivery::Queued);
    CHECK(client.malformed_frames() == 0);

    // And the fetch ends the way an unanswered fetch ends: nothing reached the
    // air, so a resend duplicates nothing. Two ticks, because the first is
    // what arms the budget.
    client.tick(at(12));
    client.tick(at(12 + 15000 + 1));
    CHECK(service.status().delivery == MeshDelivery::Refused);
}

void test_a_re_read_waits_for_a_fetch_too()
{
    MeshCoreCompanion client;
    open_a_dirty_walk(client, true);
    MeshService service(client);
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
    }

    // The fetch goes out first, while no walk is running.
    CHECK(service.send_private(absent_key(0x88), "hi", WallTime{1000}).accepted());
    CHECK(client.next_tx(frame) && frame.bytes[0] == 30);

    // The delay elapses. The re-read is due and must not go while the fetch is.
    client.tick(at(8 + 10001));
    CHECK(drain_counting_re_reads(client) == 0);

    // It is deferred, not cancelled: the budget is untouched, so the next tick
    // after the fetch is answered asks.
    std::uint8_t reply[148];
    fetched_contact(reply, absent_key(0x88), 1);
    CHECK(client.receive(reply, sizeof(reply), at(8 + 10002)));
    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(8 + 10003)));
    const std::uint8_t confirmed[] = {0x82, 1, 2, 3, 4};
    CHECK(client.receive(confirmed, sizeof(confirmed), at(8 + 10004)));
    CHECK(!client.send_busy());
    while (client.next_tx(frame)) {
    }
    client.tick(at(8 + 10005));
    CHECK(drain_counting_re_reads(client) == 1);
}

// ONE REQUEST, ONE ID, ACROSS BOTH PHASES. The caller was answered with an id
// before the fetch went out; a second id minted when the text is queued would
// publish every verdict against a number no caller holds.
void test_a_fetch_keeps_the_request_id_its_caller_was_given()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);
    const auto far = absent_key(0x77);
    const auto result = service.send_private(far, "hi", WallTime{1000});
    CHECK(result.accepted());
    CHECK(service.status().request_id == result.request_id);

    std::uint8_t reply[148];
    fetched_contact(reply, far, 1);
    CHECK(client.receive(reply, sizeof(reply), at(10)));
    CHECK(service.status().request_id == result.request_id);

    const std::uint8_t sent[] = {6, 0, 1, 2, 3, 4, 0x66, 0x09, 0, 0};
    CHECK(client.receive(sent, sizeof(sent), at(11)));
    CHECK(service.status().delivery == MeshDelivery::Accepted);
    CHECK(service.status().request_id == result.request_id);
}

// HAZARD 3, THE OTHER HALF: a walk owns the RESP_CODE_CONTACT arm while it is
// running, so a fetch is refused rather than issued into a frame two readers
// would both have a claim on. It is refused as a *call* -- no message exists --
// and with a reason of its own, because the remedy is to wait, not to shorten
// anything or fix a clock.
void test_a_fetch_waits_for_a_contacts_walk()
{
    MeshCoreCompanion client;
    connect_and_handshake(client);
    MeshService service(client);

    // Captured before the walk starts: RESP_CODE_CONTACTS_START empties the
    // retained set, which is the walk's own contract and not this test's
    // subject.
    MeshPeer kept{};
    CHECK(service.peer(0, kept));

    const std::uint8_t start[] = {2, 2, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(10)));
    const auto refused = service.send_private(absent_key(0x66), "hi", WallTime{1000});
    CHECK(!refused.accepted());
    CHECK(refused.refusal == core::MeshSendRefusal::ContactsBusy);
    // Nothing was published about it: it is a refusal, not a delivery state.
    CHECK(service.status().delivery == MeshDelivery::None);

    // And the walk hands that contact back, at which point the cached path is
    // open again: the refusal is about the fetch, not about sending.
    std::uint8_t contact[148];
    fetched_contact(contact, kept.id, 1);
    CHECK(client.receive(contact, sizeof(contact), at(11)));
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(12)));
    CHECK(service.send_private(kept.id, "hi", WallTime{1000}).accepted());
}

int main()
{
    test_typed_battery_failure_does_not_create_err_ambiguity();
    test_attached_node_battery_uses_the_live_queue_and_public_status();
    test_handshake_contacts_and_service_boundary();
    test_a_lost_contacts_end_still_asks_for_messages();
    test_a_refused_session_keeps_its_quiet_window();
    test_a_quiet_stream_that_cannot_send_tries_again();
    test_a_confirmation_mid_walk_confirms_without_dirtying();
    test_where_a_push_lands_decides_whether_it_is_dirt();
    test_a_disconnect_leaves_no_half_finished_snapshot();
    test_a_truncated_list_is_still_a_consistent_snapshot();
    test_a_message_waiting_mid_walk_neither_dirties_nor_is_swallowed();
    test_a_deletion_after_the_end_is_staleness_not_inconsistency();
    test_a_swept_stream_with_no_push_is_consistent();
    test_one_dirty_walk_costs_exactly_one_re_read();
    test_a_table_that_moves_under_every_re_read_ends_degraded();
    test_which_pushes_dirty_a_walk_and_which_only_look_it();
    test_an_error_owed_to_a_re_read_does_not_fail_a_send();
    test_an_error_older_than_the_re_read_still_fails_the_send();
    test_a_start_later_than_the_ack_budget_is_still_the_re_reads();
    test_an_expired_claim_frees_the_battery_poll_and_not_the_command();
    test_a_re_read_does_not_unname_a_sender_mid_walk();
    test_a_re_reads_end_spends_no_drain_on_a_full_ring();
    test_a_message_carries_a_coordinate_or_nothing();
    test_a_coordinate_that_is_not_one_is_refused();
    test_a_coordinate_from_nobody_is_dropped();
    test_a_truncated_message_yields_no_coordinate();
    test_a_second_peer_restarts_the_first_peers_arrival();
    test_an_unchanged_coordinate_is_not_re_stamped();
    test_a_reconnect_does_not_inherit_a_contact_coordinate();
    test_forgetting_the_node_withdraws_a_contact_coordinate();
    test_a_deleted_contact_takes_its_coordinate();
    test_a_short_contact_deleted_push_is_malformed();
    test_a_delete_inside_a_walk_dirties_it_whatever_its_length();
    test_a_misfired_sweep_publishes_a_partial_pair();
    test_a_contact_dropped_by_type_leaves_retained_below_reported();
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
    test_a_short_send_confirmed_is_refused_and_the_send_still_lives();
    test_a_room_send_owns_the_slot_through_its_login();
    test_a_room_login_that_is_never_answered_still_ends();
    test_a_send_that_is_never_confirmed_still_ends();
    test_a_custom_vars_error_does_not_fail_an_accepted_send();
    test_an_unanswered_custom_vars_request_stops_taking_the_blame();
    test_an_old_node_refusing_opcode_40_does_not_fail_a_room_login();
    test_an_old_node_refusing_opcode_40_does_not_fail_a_queued_send();
    test_an_answered_login_does_not_take_a_later_opcode_40s_error();
    test_signed_message_does_not_render_signature_as_text();
    test_cli_data_is_neither_a_message_nor_a_coordinate();
    test_channel_message_is_rendered_without_a_contact_prefix();
    test_a_queued_backlog_is_drained_to_the_end();
    test_a_reconnect_starts_a_new_drain();
    test_an_unreadable_message_does_not_spin_the_drain();
    test_a_drain_nobody_answers_expires();
    test_a_dropped_notification_ends_the_drain();
    test_a_push_swallowed_by_a_drain_is_paid_back();
    test_a_refused_node_is_not_asked_by_the_tick_sweep();
    test_self_info_carries_the_node_identity();
    test_the_pinned_node_is_the_one_the_handshake_continues_with();
    test_another_node_answers_and_the_handshake_stops_there();
    test_the_pin_outlives_the_session_and_the_identity_does_not();
    test_a_terminal_fault_outranks_a_refusal_the_session_kept();
    test_an_ordinary_disconnect_still_reports_the_refusal();
    test_unpin_clears_the_pin_and_the_refusal_it_caused();
    test_a_short_self_info_is_refused_before_anything_reads_it();
    test_a_re_read_the_sweep_closed_does_not_commit_what_it_swept();
    test_the_text_budget_is_counted_in_bytes_and_refused_not_repaired();
    test_the_utf8_boundary_stops_at_a_code_point();
    test_a_request_id_is_local_non_zero_and_distinct();
    test_an_ack_after_the_budget_upgrades_unconfirmed_to_confirmed();
    test_an_identical_ack_tag_does_not_confirm_the_earlier_request();
    test_an_error_code_is_not_shown_to_the_owner_as_a_reason();
    test_the_recipient_narrows_to_six_bytes_in_the_frame_and_nowhere_else();
    test_a_refused_send_does_not_erase_the_previous_verdict();
    test_a_zero_tag_does_not_confirm_a_settled_unconfirmed();
    test_a_send_to_an_unretained_key_asks_the_node_for_the_contact();
    test_a_fetched_contact_does_not_enter_the_retained_window();
    test_a_fetched_contact_that_is_not_a_chat_contact_is_refused();
    test_a_fetch_waits_for_a_re_read_too();
    test_a_key_the_node_does_not_hold_is_refused();
    test_a_fetch_the_node_never_answers_expires_refused();
    test_a_fetch_the_link_drops_under_is_refused();
    test_a_text_the_link_drops_under_stays_unknown();
    test_a_text_the_node_never_answers_expires_unknown();
    test_a_walk_that_starts_anyway_owns_the_contact_frame();
    test_a_re_read_waits_for_a_fetch_too();
    test_a_fetch_keeps_the_request_id_its_caller_was_given();
    test_a_room_login_keeps_the_request_id_its_caller_was_given();
    test_a_fetch_waits_for_a_contacts_walk();
    test_a_duplicate_end_does_not_delay_the_first_attempt();
    test_a_swept_walks_late_end_does_not_delay_the_next_attempt();
    test_a_swept_walks_late_end_does_not_settle_over_a_live_attempt();
    test_a_node_started_walk_after_the_budget_owns_its_frames();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("meshcore companion: all host checks passed (SIMULATED transport)\n");
    return 0;
}
