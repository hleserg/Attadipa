// WHAT THIS CLIENT DOES WITH AN ANSWER IT CANNOT READ — the ten rows of the
// matrix in ../MESHCORE_OFFLINE_QUEUE_FORWARD_COMPAT.md, measured rather than
// argued.
//
// It drives the shipping `attadipa::link::MeshCoreCompanion` through its public
// surface — `receive()`, `next_tx()`, `tick()` — and nothing else. There is no
// fixture standing in for the client and no copy of its logic here: a row of
// this table is what the production class did, at the revision recorded in the
// run log beside it. That is deliberate, and it is the difference between this
// and a state diagram somebody drew: `AGENTS.md` — "A test of a fixture, copied
// implementation, generated patch, or isolated decision helper does not prove
// the production caller works."
//
// Nothing here is part of an Attadipa build. See ./README.md for how to run it.
//
// THE TWO PRIVATE BITS ARE OBSERVED, NOT READ. `draining_` and `pending_push_`
// have no accessor and are not given one for a research tool, so each row is
// run twice against two fresh clients:
//
//   * pass 1 sends PUSH_CODE_MSG_WAITING straight after the frame under test.
//     A push that produces CMD_SYNC_NEXT_MESSAGE at once found `draining_`
//     down; a push swallowed with nothing on the wire found it up, because
//     coalescing is the only thing that swallows one
//     (`link/src/meshcore_companion.cpp:1736` — "    case kPushMessageWaiting:").
//
//   * pass 2 touches nothing and ticks a simulated minute, which is what the
//     firmware worker does anyway. The first CMD_SYNC_NEXT_MESSAGE to leave is
//     the unprompted recovery, and its timestamp is the column that says
//     whether the backlog waits fifteen seconds or waits for a person to send
//     another message.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "attadipa/link/meshcore_companion.h"

namespace {

namespace core = attadipa::core;
using attadipa::link::MeshCoreCompanion;
using attadipa::link::MeshCoreFrame;
using core::MonotonicTime;

int failures = 0;

void expect(bool value, const char* what)
{
    if (!value) {
        std::fprintf(stderr, "SETUP FAILED: %s\n", what);
        ++failures;
    }
}

MonotonicTime at(std::uint64_t ms) { return MonotonicTime{ms}; }

// Opcode 10 both ways: CMD_SYNC_NEXT_MESSAGE out, RESP_CODE_NO_MORE_MESSAGES
// back. Only the outbound sense is used below.
constexpr std::uint8_t kSync = 10;
constexpr std::uint8_t kBattery = 20;
constexpr std::uint8_t kMsgWaiting = 0x83;

// Everything the node put on the wire since the last look, as opcodes.
std::string wire(MeshCoreCompanion& client)
{
    std::string out;
    MeshCoreFrame frame{};
    while (client.next_tx(frame)) {
        if (!out.empty()) out += ',';
        out += std::to_string(static_cast<unsigned>(frame.bytes[0]));
    }
    return out.empty() ? "-" : out;
}

bool sync_left(MeshCoreCompanion& client)
{
    MeshCoreFrame frame{};
    bool seen = false;
    while (client.next_tx(frame)) {
        if (frame.size == 1 && frame.bytes[0] == kSync) seen = true;
    }
    return seen;
}

// The session every row starts from: connected, identified, contacts walked,
// and the session's own CMD_SYNC_NEXT_MESSAGE answered with an empty queue, so
// no drain is outstanding when a row begins.
void handshake(MeshCoreCompanion& client)
{
    client.begin(at(0));
    client.peer_arriving(at(1));
    client.connected(at(2));

    MeshCoreFrame frame{};
    expect(client.next_tx(frame) && frame.bytes[0] == 1, "CMD_APP_START");
    std::uint8_t self[62]{};
    self[0] = 5;
    std::memcpy(&self[58], "Node", 4);
    expect(client.receive(self, sizeof(self), at(3)), "RESP_CODE_SELF_INFO");
    expect(client.next_tx(frame) && frame.bytes[0] == 22, "CMD_DEVICE_QUERY");

    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    expect(client.receive(device, sizeof(device), at(4)), "RESP_CODE_DEVICE_INFO");
    expect(client.next_tx(frame) && frame.bytes[0] == 4, "CMD_GET_CONTACTS");

    const std::uint8_t start[] = {2, 1, 0, 0, 0};
    expect(client.receive(start, sizeof(start), at(5)), "RESP_CODE_CONTACTS_START");

    std::uint8_t contact[148]{};
    contact[0] = 3;
    for (std::size_t i = 0; i < 32; ++i)
        contact[1 + i] = static_cast<std::uint8_t>(i + 1);
    contact[33] = 1;  // ADV_TYPE_CHAT
    std::memcpy(&contact[100], "Peer", 4);
    expect(client.receive(contact, sizeof(contact), at(6)), "RESP_CODE_CONTACT");

    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    expect(client.receive(end, sizeof(end), at(7)), "RESP_CODE_END_OF_CONTACTS");
    expect(wire(client) == "10,40", "END_OF_CONTACTS asks for a message and opcode 40");

    const std::uint8_t drained[] = {10};
    expect(client.receive(drained, sizeof(drained), at(7)), "RESP_CODE_NO_MORE_MESSAGES");
    expect(client.status().availability == core::Availability::Ready, "Ready");
    expect(wire(client) == "-", "quiet");
}

// THE FRAMES. The two new ones are byte-for-byte the shapes upstream PR #3447
// documents; nothing in this file guesses a layout.

// `PACKET_CONTACT_MSG_SENT_V3` (0x1E): reserved 1-3, recipient prefix 4-9,
// path_len 10 (0xFF), text type 11, timestamp 12-15, text 16+.
std::vector<std::uint8_t> contact_sent_v3(const char* text)
{
    std::vector<std::uint8_t> f(16, 0);
    f[0] = 0x1E;
    for (std::size_t i = 0; i < 6; ++i) f[4 + i] = static_cast<std::uint8_t>(i + 1);
    f[10] = 0xFF;
    f[11] = 0;  // TXT_TYPE_PLAIN
    f.insert(f.end(), text, text + std::strlen(text));
    return f;
}

// `PACKET_CHANNEL_MSG_SENT_V3` (0x1F): reserved 1-3, channel 4, path_len 5
// (0xFF), text type 6, timestamp 7-10, text 11+.
std::vector<std::uint8_t> channel_sent_v3(const char* text)
{
    std::vector<std::uint8_t> f(11, 0);
    f[0] = 0x1F;
    f[4] = 3;
    f[5] = 0xFF;
    f[6] = 0;
    f.insert(f.end(), text, text + std::strlen(text));
    return f;
}

// `RESP_CODE_CONTACT_MSG_RECV` (7): key at 1, text type at 8, text at 13.
std::vector<std::uint8_t> contact_recv(const char* text)
{
    std::vector<std::uint8_t> f(13, 0);
    f[0] = 7;
    for (std::size_t i = 0; i < 6; ++i) f[1 + i] = static_cast<std::uint8_t>(i + 1);
    f.insert(f.end(), text, text + std::strlen(text));
    return f;
}

// A drain with one request outstanding: the node said something is waiting and
// the client asked for it.
void open_drain(MeshCoreCompanion& client, std::uint64_t t)
{
    const std::uint8_t waiting[] = {kMsgWaiting};
    expect(client.receive(waiting, sizeof(waiting), at(t)), "PUSH_CODE_MSG_WAITING");
    expect(wire(client) == "10", "one CMD_SYNC_NEXT_MESSAGE per push");
}

struct Row {
    std::string id;
    std::string what;
    std::string next_cmd;      // what left the wire on the frame under test
    std::string drain_held;    // was `draining_` still up after it
    std::string recovery;      // first unprompted CMD_SYNC_NEXT_MESSAGE
    std::uint32_t malformed = 0;
    std::uint32_t cli = 0;
    std::string availability;
};

using Setup = void (*)(MeshCoreCompanion&, std::uint64_t&);

const char* availability_name(core::Availability a)
{
    switch (a) {
    case core::Availability::Ready: return "Ready";
    case core::Availability::Unreachable: return "Unreachable";
    case core::Availability::Failed: return "Failed";
    default: return "Unknown";
    }
}

Row observe(const std::string& id, const std::string& what, Setup setup)
{
    Row row;
    row.id = id;
    row.what = what;

    // Pass 1 — the wire, the counters, and the probe that reads `draining_`.
    {
        MeshCoreCompanion client;
        std::uint64_t t = 10;
        handshake(client);
        setup(client, t);
        row.next_cmd = wire(client);
        row.malformed = client.malformed_frames();
        row.cli = client.cli_frames();
        row.availability = availability_name(client.status().availability);

        const std::uint8_t waiting[] = {kMsgWaiting};
        client.receive(waiting, sizeof(waiting), at(t + 1));
        row.drain_held = sync_left(client) ? "no (push asks at once)"
                                           : "YES (push swallowed)";
    }

    // Pass 2 — a minute of the worker's own ticks, and nothing else.
    {
        MeshCoreCompanion client;
        std::uint64_t t = 10;
        handshake(client);
        setup(client, t);
        (void)wire(client);
        row.recovery = "none in 60 s";
        for (std::uint64_t ms = t + 1; ms <= t + 60000; ms += 10) {
            client.tick(at(ms));
            if (sync_left(client)) {
                row.recovery = "+" + std::to_string(ms - t) + " ms";
                break;
            }
        }
    }
    return row;
}

// ---------------------------------------------------------------------------
// The rows.

// 1. The baseline the drain was built for: a known frame is accepted and the
//    next ask goes out on its own.
void row_known(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    const auto msg = contact_recv("oldest");
    expect(client.receive(msg.data(), msg.size(), at(++t)), "known message accepted");
}

// 2. An unknown but bounded 0x1E answering an outstanding sync, with a known
//    message still behind it in the node's queue.
void row_contact_sent_v3(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    const auto msg = contact_sent_v3("bot reply");
    client.receive(msg.data(), msg.size(), at(++t));
}

// 3. The channel counterpart, 0x1F.
void row_channel_sent_v3(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    const auto msg = channel_sent_v3("bot reply");
    client.receive(msg.data(), msg.size(), at(++t));
}

// 4. An unknown opcode that is neither: RESP_CODE_CLI_REPLY (29), which is a
//    real upstream response code this build does not define either.
void row_unknown_other(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    std::uint8_t frame[20]{};
    frame[0] = 29;
    client.receive(frame, sizeof(frame), at(++t));
}

// 5. A known code whose frame is too short to parse — the case #481 chose its
//    policy for.
void row_short_known(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    std::uint8_t truncated[12]{};
    truncated[0] = 16;  // RESP_CODE_CONTACT_MSG_RECV_V3 needs 16 before any text
    client.receive(truncated, sizeof(truncated), at(++t));
}

// 6. The same unknown frame with no sync outstanding at all. A node that
//    volunteers one is not answering anything.
void row_unsolicited(MeshCoreCompanion& client, std::uint64_t& t)
{
    const auto msg = contact_sent_v3("unasked");
    client.receive(msg.data(), msg.size(), at(++t));
}

// 7a. A frame longer than the client's buffer, handed to receive() as it is.
void row_oversize(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    std::vector<std::uint8_t> huge(attadipa::link::kMeshCoreFrameBytes + 1, 0);
    huge[0] = 0x1E;
    client.receive(huge.data(), huge.size(), at(++t));
}

// 7b. The transport dropping it before receive() ever sees it, which is what
//     actually happens to an oversize notification.
void row_dropped(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    client.drop_oversize_frame();
    ++t;
}

// 8. A node that answers every ask with an unknown frame. Each round needs its
//    own push, because today nothing else asks again — which is itself the
//    measurement: the budget is the node's pushes, not this client's.
void row_repeated(MeshCoreCompanion& client, std::uint64_t& t)
{
    for (int round = 0; round < 8; ++round) {
        const std::uint8_t waiting[] = {kMsgWaiting};
        client.receive(waiting, sizeof(waiting), at(++t));
        (void)wire(client);
        const auto msg = contact_sent_v3("again");
        client.receive(msg.data(), msg.size(), at(++t));
        // The 15 s deadline has to pass or the next push is swallowed.
        for (std::uint64_t ms = t + 10; ms <= t + 15100; ms += 100) client.tick(at(ms));
        t += 15100;
        (void)wire(client);
    }
    const std::uint8_t waiting[] = {kMsgWaiting};
    client.receive(waiting, sizeof(waiting), at(++t));
}

// 9. The unknown frame answered into a session that then drops. #481's
//    reconnect path has to survive it: a new session asks again.
void row_reconnect(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    const auto msg = contact_sent_v3("stranded");
    client.receive(msg.data(), msg.size(), at(++t));
    client.disconnected(at(++t));
    client.connected(at(++t));
    (void)wire(client);
}

// 10. A push that arrives while the sync is outstanding and is swallowed by
//     the coalescing, then an unknown answer. This is the row where the backlog
//     is known to be non-empty and the client has already been told so.
void row_swallowed_push(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    const std::uint8_t waiting[] = {kMsgWaiting};
    client.receive(waiting, sizeof(waiting), at(++t));
    expect(wire(client) == "-", "second push coalesced");
    const auto msg = contact_sent_v3("bot reply");
    client.receive(msg.data(), msg.size(), at(++t));
}

// 11. The counter-case for the whole file: TXT_TYPE_CLI_DATA is a frame the
//     client understands, shows nobody, and still advances the queue on (#627).
void row_cli_data(MeshCoreCompanion& client, std::uint64_t& t)
{
    open_drain(client, t);
    auto msg = contact_recv("cli output");
    msg[8] = 1;  // TXT_TYPE_CLI_DATA
    client.receive(msg.data(), msg.size(), at(++t));
}

}  // namespace

int main()
{
    const struct {
        const char* id;
        const char* what;
        Setup setup;
    } rows[] = {
        {"1", "known 0x07 answering a sync", row_known},
        {"2", "unknown bounded 0x1E answering a sync", row_contact_sent_v3},
        {"3", "unknown bounded 0x1F answering a sync", row_channel_sent_v3},
        {"4", "unknown 0x1D (29, CLI_REPLY) answering a sync", row_unknown_other},
        {"5", "structurally short known 0x10", row_short_known},
        {"6", "unsolicited 0x1E, no sync outstanding", row_unsolicited},
        {"7a", "oversize frame passed to receive()", row_oversize},
        {"7b", "oversize frame dropped by the transport", row_dropped},
        {"8", "eight unknown answers, one push each", row_repeated},
        {"9", "unknown answer, then disconnect and reconnect", row_reconnect},
        {"10", "push swallowed by the drain, then unknown answer", row_swallowed_push},
        {"11", "TXT_TYPE_CLI_DATA — understood, shown to nobody", row_cli_data},
    };

    std::printf("| # | frame under test | next command | drain still open | "
                "unprompted recovery | malformed | cli | availability |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (const auto& r : rows) {
        const Row row = observe(r.id, r.what, r.setup);
        std::printf("| %s | %s | `%s` | %s | %s | %u | %u | %s |\n",
                    row.id.c_str(), row.what.c_str(), row.next_cmd.c_str(),
                    row.drain_held.c_str(), row.recovery.c_str(), row.malformed,
                    row.cli, row.availability.c_str());
    }

    if (failures != 0) {
        std::fprintf(stderr, "\n%d setup expectation(s) failed — the table above "
                             "describes a session that did not start cleanly.\n",
                     failures);
        return 1;
    }
    return 0;
}
