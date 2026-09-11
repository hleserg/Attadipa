#include <array>
#include <cstdio>
#include <cstring>

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
// made (docs/research/MESHCORE_T114_FIRST_CONTACT.md:54).
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

    // Cancellation must erase an unsent login too. A shorter request then
    // reuses that same ring slot; inspect its full storage, not just its size.
    MeshCoreCompanion cancelled;
    connect_and_handshake(cancelled);
    const std::uint8_t receiver_hint[] = {21};
    CHECK(cancelled.receive(receiver_hint, sizeof(receiver_hint), at(8)));
    CHECK(cancelled.send_room(room, canary, "never sent", WallTime{1000}));
    cancelled.tick(at(100));
    cancelled.tick(at(15100));
    CHECK(cancelled.status().delivery == MeshDelivery::Failed);
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
// MESHCORE_COMPANION_PROTOCOL.md:175-177 is the reason this is one test rather
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
    // (MESHCORE_COMPANION_PROTOCOL.md:177). One this build does not know is
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
        CHECK(service.send_private(peer.id, "text", WallTime{1000}));
        const std::uint8_t error[] = {1, 4};
        CHECK(client.receive(error, sizeof(error), at(8)));
        CHECK(service.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(service.send_private(peer.id, "again", WallTime{1001}));
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
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}));

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

    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}));
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
    CHECK(service.status().delivery == MeshDelivery::Failed);
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
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
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
    CHECK(client.send_room(room, "password", "Hello", WallTime{1000}));
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
    CHECK(client.status().delivery != MeshDelivery::Failed);
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
    CHECK(service.send_private(peer.id, "Hello", WallTime{1000}));

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
        CHECK(client.send_private(peer.id, "first", WallTime{1}));
        client.tick(at(10));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(sent, sizeof(sent), at(11)));
        CHECK(!client.next_tx(frame));
        CHECK(client.receive(confirmed, sizeof(confirmed), at(12)));
        client.tick(at(13));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.send_private(peer.id, "second", WallTime{2}));
        client.tick(at(14));
        CHECK(!client.next_tx(frame));
        CHECK(client.receive(error, sizeof(error), at(15)));
        CHECK(client.status().delivery == MeshDelivery::Queued);
        CHECK(client.send_busy());
        client.tick(at(16));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(17)));
        CHECK(client.status().delivery == MeshDelivery::Failed);
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
        CHECK(client.send_private(peer.id, "after poll", WallTime{1}));
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
        CHECK(client.send_private(peer.id, "no reply", WallTime{2}));
        client.tick(at(60012));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(60013)));
        CHECK(client.send_busy());
        client.tick(at(75012));
        CHECK(client.status().delivery == MeshDelivery::Failed);
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
        CHECK(client.send_private(peer.id, "A", WallTime{1}));
        client.tick(at(5011));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
        CHECK(client.receive(error, sizeof(error), at(5012))); // A's refusal
        CHECK(client.send_busy());
        client.tick(at(20011));
        CHECK(client.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(client.send_private(peer.id, "B", WallTime{2}));
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
            CHECK(client.send_room(peer.id.public_key, "password", "stalled pump", WallTime{1}));
        } else {
            CHECK(client.send_private(peer.id, "stalled pump", WallTime{1}));
        }
        // Retained drain work after the operation tests FIFO compaction too.
        const std::uint8_t drained[] = {10};
        CHECK(client.receive(drained, sizeof(drained), at(11)));
        CHECK(client.receive(waiting, sizeof(waiting), at(12)));
        client.tick(at(13));
        CHECK(client.send_busy());
        client.tick(at(15013));
        CHECK(client.status().delivery == MeshDelivery::Failed);
        CHECK(!client.send_busy());
        CHECK(client.next_tx(frame) && frame.bytes[0] == 20);
        CHECK(client.receive(voltage, sizeof(voltage), at(15014)));
        CHECK(client.next_tx(frame) && frame.bytes[0] == 10);
        CHECK(!client.next_tx(frame)); // no expired text or login remains
        CHECK(client.receive(drained, sizeof(drained), at(15015)));
        CHECK(client.send_private(peer.id, "new", WallTime{2}));
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
            CHECK(client.send_private(peer.id, "after typed reply", WallTime{1}));
            client.tick(at(reply_at + 1));
            CHECK(client.next_tx(frame) && frame.bytes[0] == 2);
            CHECK(client.receive(error, sizeof(error), at(reply_at + 2)));
            CHECK(client.send_busy() == previous_timeout);
            CHECK(client.status().delivery == (previous_timeout ? MeshDelivery::Queued
                                                               : MeshDelivery::Failed));
            if (previous_timeout) {
                client.tick(at(reply_at + 15001));
                CHECK(client.status().delivery == MeshDelivery::Failed);
                CHECK(!client.send_busy());
            }
        }
    }
}

int main()
{
    test_typed_battery_failure_does_not_create_err_ambiguity();
    test_attached_node_battery_uses_the_live_queue_and_public_status();
    test_handshake_contacts_and_service_boundary();
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
    test_a_room_send_owns_the_slot_through_its_login();
    test_a_room_login_that_is_never_answered_still_ends();
    test_a_send_that_is_never_confirmed_still_ends();
    test_a_custom_vars_error_does_not_fail_an_accepted_send();
    test_an_unanswered_custom_vars_request_stops_taking_the_blame();
    test_an_old_node_refusing_opcode_40_does_not_fail_a_room_login();
    test_an_old_node_refusing_opcode_40_does_not_fail_a_queued_send();
    test_an_answered_login_does_not_take_a_later_opcode_40s_error();
    test_signed_message_does_not_render_signature_as_text();
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
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("meshcore companion: all host checks passed (SIMULATED transport)\n");
    return 0;
}
