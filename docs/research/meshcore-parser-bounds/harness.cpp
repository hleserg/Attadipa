// Host research harness for MeshCore parser bounds — Attadipa issue #142.
//
// This file is the harness only. Every parser it exercises is compiled from
// upstream MeshCore sources unmodified; nothing here reimplements one.
//
// Design note on the input buffer. Each case gets EXACTLY the declared length,
// ending flush against a PROT_NONE guard page, and the build is also under
// AddressSanitizer. So a read at src[len] is caught whichever way. That is the
// point: it isolates "the parser reads past the length it was given" from "the
// caller happened to hand it a bigger array", which are different claims and
// only the first is a property of the parser. The second is answered by reading
// the call sites, and the report says what that reading found.

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include <Mesh.h>
#include <Packet.h>
#include <Dispatcher.h>
#include <helpers/AdvertDataHelpers.h>

// ---------------------------------------------------------------- stubs ----

class StubClock : public mesh::MillisecondClock {
public:
    unsigned long getMillis() override { return 0; }
};

class StubRadio : public mesh::Radio {
public:
    int recvRaw(uint8_t*, int) override { return 0; }
    uint32_t getEstAirtimeFor(int) override { return 0; }
    float packetScore(float, int) override { return 0; }
    bool startSendRaw(const uint8_t*, int) override { return true; }
    bool isSendComplete() override { return true; }
    void onSendFinished() override {}
    bool isInRecvMode() const override { return true; }
};

class StubMgr : public mesh::PacketManager {
public:
    mesh::Packet* allocNew() override { return new mesh::Packet(); }
    void free(mesh::Packet* p) override { delete p; }
    void queueOutbound(mesh::Packet*, uint8_t, uint32_t) override {}
    mesh::Packet* getNextOutbound(uint32_t) override { return nullptr; }
    int getOutboundCount(uint32_t) const override { return 0; }
    int getOutboundTotal() const override { return 0; }
    int getFreeCount() const override { return 0; }
    mesh::Packet* getOutboundByIdx(int) override { return nullptr; }
    mesh::Packet* removeOutboundByIdx(int) override { return nullptr; }
    void queueInbound(mesh::Packet*, uint32_t) override {}
    mesh::Packet* getNextInbound(uint32_t) override { return nullptr; }
};

class TestDispatcher : public mesh::Dispatcher {
public:
    TestDispatcher(mesh::Radio& r, mesh::MillisecondClock& c, mesh::PacketManager& m)
        : mesh::Dispatcher(r, c, m) {}
    mesh::DispatcherAction onRecvPacket(mesh::Packet*) override { return 0; }
};

// ----------------------------------------------------------------- rig -----

// Two mechanisms, because neither alone covers every case.
//
// ASan's redzone catches a read past a heap chunk and names the source line,
// which is what makes the evidence quotable. But an ASan malloc(0) does NOT
// report a read at offset 0 — measured, and it silently turned two len=0 cases
// green on a build that had no guard at all. So the buffer is instead placed
// against a PROT_NONE guard page and ends exactly at the boundary: a read at
// src[len] faults for every len including zero, with no sanitizer in the story.
static uint8_t* tight(const std::vector<uint8_t>& bytes, size_t& len)
{
    len = bytes.size();
    const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    uint8_t* base = static_cast<uint8_t*>(
        ::mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (base == MAP_FAILED) { std::perror("mmap"); std::exit(2); }
    if (::mprotect(base + page, page, PROT_NONE) != 0) { std::perror("mprotect"); std::exit(2); }

    uint8_t* p = base + page - len;   // last declared byte abuts the guard page
    size_t i = 0;
    for (uint8_t b : bytes) p[i++] = b;
    return p;
}

static void release(uint8_t*) { /* deliberately leaked: the guard page must
                                  outlive the case, and the process is about
                                  to exit either way. */ }

static void banner(const char* name, const char* what)
{
    std::printf("\n--- %s : %s\n", name, what);
    std::fflush(stdout);   // the sanitizer report may be the next thing written
}

// --------------------------------------------------- one per parser under test --

static void case_tryParsePacket(const char* name, const std::vector<uint8_t>& bytes,
                                const char* what)
{
    banner(name, what);
    size_t len = 0;
    uint8_t* raw = tight(bytes, len);

    StubRadio radio; StubClock clock; StubMgr mgr;
    TestDispatcher d(radio, clock, mgr);
    mesh::Packet pkt;

    bool ok = d.tryParsePacket(&pkt, raw, static_cast<int>(len));
    std::printf("    returned %s (type=%u path_len=%u payload_len=%u)\n",
                ok ? "true" : "false", (unsigned)pkt.getPayloadType(),
                (unsigned)pkt.path_len, (unsigned)pkt.payload_len);
    std::fflush(stdout);
    release(raw);
}

static void case_readFrom(const char* name, const std::vector<uint8_t>& bytes,
                          const char* what)
{
    banner(name, what);
    size_t len = 0;
    uint8_t* src = tight(bytes, len);

    mesh::Packet pkt;
    bool ok = pkt.readFrom(src, static_cast<uint8_t>(len));
    std::printf("    returned %s (path_len=%u payload_len=%u)\n",
                ok ? "true" : "false", (unsigned)pkt.path_len, (unsigned)pkt.payload_len);
    std::fflush(stdout);
    release(src);
}

static void case_advert(const char* name, const std::vector<uint8_t>& bytes,
                        const char* what)
{
    banner(name, what);
    size_t len = 0;
    uint8_t* app = tight(bytes, len);

    AdvertDataParser parser(app, static_cast<uint8_t>(len));
    std::printf("    valid=%s type=%u hasLatLon=%s lat=%d lon=%d name=\"%s\"\n",
                parser.isValid() ? "true" : "false", (unsigned)parser.getType(),
                parser.hasLatLon() ? "true" : "false",
                (int)parser.getIntLat(), (int)parser.getIntLon(), parser.getName());
    std::fflush(stdout);
    release(app);
}

// --------------------------------------------------- typed frame builder ---
//
// The D and V series are whole radio frames rather than fragments, because the
// shape checks #3521 adds are per payload type and a fragment has no type. Each
// is built the way Dispatcher::checkRecv would hand it over: header, the four
// transport-code bytes when the route calls for them, path_len, the path, then
// the payload as the remainder. Nothing here encrypts anything — every typed
// check under test is a length check, and a length is not a ciphertext.

static std::vector<uint8_t> frame(uint8_t route, uint8_t type, uint8_t path_len,
                                  const std::vector<uint8_t>& path,
                                  const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> f;
    f.push_back(static_cast<uint8_t>(route | (type << PH_TYPE_SHIFT)));
    if (route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT) {
        f.insert(f.end(), {0x11, 0x22, 0x33, 0x44});
    }
    f.push_back(path_len);
    f.insert(f.end(), path.begin(), path.end());
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

// Filler. 0xAA rather than 0x00 so that a byte the parser copied out of the
// frame can be told apart from one it never wrote.
static std::vector<uint8_t> fill(size_t n, uint8_t v = 0xAA)
{
    return std::vector<uint8_t>(n, v);
}

// A payload of `n` bytes whose first byte is `first` — MULTIPART carries its
// inner type there, CONTROL its flags.
static std::vector<uint8_t> lead(uint8_t first, size_t n)
{
    std::vector<uint8_t> p = fill(n);
    if (n > 0) p[0] = first;
    return p;
}

// ------------------------------------------------------------------ main ---

int main(int argc, char** argv)
{
    const std::string only = argc > 1 ? argv[1] : "";
    auto want = [&](const char* n) { return only.empty() || only == n; };

    std::printf("MeshCore parser bounds harness — one case per process is the\n"
                "usable mode under ASan, because the first report aborts.\n");

    // A. Dispatcher::tryParsePacket
    //    header 0x01 = ROUTE_TYPE_FLOOD -> no transport codes.
    //    header 0x00 = ROUTE_TYPE_TRANSPORT_FLOOD -> four transport-code bytes.
    if (want("A1"))
        case_tryParsePacket("A1", {0x01},
            "len=1, flood route: path_len byte is read at raw[1]");
    if (want("A2"))
        case_tryParsePacket("A2", {0x00},
            "len=1, transport route: four transport bytes read at raw[1..4]");
    if (want("A3"))
        case_tryParsePacket("A3", {},
            "len=0: header read at raw[0] (not reachable from checkRecv, which gates on len>0)");

    // B. Packet::readFrom
    //    path_len 0x3F = 63 hashes of 1 byte = 63 path bytes claimed.
    if (want("B1"))
        case_readFrom("B1", {0x01, 0x3F},
            "len=2, 63 path bytes claimed: 63-byte read at src[2..64]");
    if (want("B2"))
        case_readFrom("B2", {0x01},
            "len=1, flood route: path_len byte read at src[1]");
    if (want("B3"))
        case_readFrom("B3", {0x00},
            "len=1, transport route: transport bytes read at src[1..4]");

    // C. AdvertDataParser
    //    flags 0x10 = LATLON, 0x20 = FEAT1, 0x40 = FEAT2, 0x80 = NAME.
    if (want("C1"))
        case_advert("C1", {0x91},
            "len=1, LATLON|NAME: eight lat/lon bytes read at app_data[1..8]");
    if (want("C2"))
        case_advert("C2", {0xF1},
            "len=1, all flags: twelve bytes read at app_data[1..12]");
    if (want("C3"))
        case_advert("C3", {},
            "len=0: flags byte read at app_data[0]");
    if (want("C4"))
        case_advert("C4", {0x21},
            "len=1, FEAT1 only: two bytes read at app_data[1..2]");

    // C5 is a WRITE, and the only one in this file. _name is char[32]
    // (MAX_ADVERT_DATA_SIZE) and nlen is app_data_len - i with nothing
    // comparing it to that. Reachability is a property of the callers, not of
    // the parser: src/Mesh.cpp:269 clamps app_data_len to 32 before either
    // call site, so at the pin this is a broken contract, not a radio-reachable
    // overflow. See §9 of ../MESHCORE_PARSER_BOUNDS.md.
    if (want("C5")) {
        std::vector<uint8_t> big = fill(255, 'A');   // printable: the name is echoed
        big[0] = 0x80;   // NAME only: nlen = 255 - 1 = 254 into char[32]
        case_advert("C5", big,
            "len=255, NAME only: 254-byte memcpy into _name[32] — a write");
    }
    if (want("C6")) {
        std::vector<uint8_t> ok = fill(32, 'A');
        ok[0] = 0x80;    // the largest app_data AdvertDataBuilder can produce
        case_advert("C6", ok,
            "len=32, NAME only: the builder's maximum — must stay valid");
    }

    // ---------------------------------------------------------------------
    // D. Typed payload shapes, whole frames, through the radio entry point.
    //    At the pin nothing below the frame header is checked at all, so each
    //    of these is accepted and handed to Mesh::onRecvPacket; on #3521's head
    //    Dispatcher::tryParsePacket is Packet::readFrom and isValidPayload
    //    decides. The row of interest is therefore "accepted" vs "rejected",
    //    not a sanitizer report: the fault these shapes cause is downstream, in
    //    translation units this harness cannot link.
    // ---------------------------------------------------------------------

    if (want("D1"))
        case_tryParsePacket("D1", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, {}),
            "ACK with no payload: the 4-byte hash is read from nothing");
    if (want("D2"))
        case_tryParsePacket("D2", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, fill(3)),
            "ACK with 3 payload bytes: one byte short of the hash");
    if (want("D3"))
        case_tryParsePacket("D3", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, fill(7)),
            "ACK with 7 payload bytes: one past MAX_ACK_PAYLOAD_SIZE");
    if (want("D4"))
        case_tryParsePacket("D4", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, fill(184)),
            "ACK with a full 184-byte payload: createMultiAck writes 1+184");
    if (want("D5"))
        case_tryParsePacket("D5", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TRACE, 0, {}, fill(8)),
            "TRACE with 8 payload bytes: the 9-byte prefix underflows");
    if (want("D6"))
        case_tryParsePacket("D6", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TRACE, 0, {},
                                        [] { auto p = fill(12); p[8] = 0x01; return p; }()),
            "TRACE, hash size 2, 3 trailing bytes: one incomplete hash");
    if (want("D7"))
        case_tryParsePacket("D7", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TRACE, 0x41, fill(2),
                                        [] { auto p = fill(9); p[8] = 0x00; return p; }()),
            "TRACE carrying an outer path: path_len upper bits set");
    if (want("D8"))
        case_tryParsePacket("D8", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_CONTROL, 0, {}, {}),
            "CONTROL with no payload: payload[0] is read from nothing");
    if (want("D9"))
        case_tryParsePacket("D9", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, {}, fill(19)),
            "TXT_MSG, 15 ciphertext bytes: not a whole cipher block");
    if (want("D10"))
        case_tryParsePacket("D10", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, {}, fill(184)),
            "TXT_MSG, 180 ciphertext bytes: P4's 192-into-184 decrypt");
    if (want("D11"))
        case_tryParsePacket("D11", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, {}, fill(4)),
            "TXT_MSG with prefix and MAC and no ciphertext at all");
    if (want("D12"))
        case_tryParsePacket("D12", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, {}, fill(18)),
            "GRP_TXT, 15 ciphertext bytes after the 1-byte channel hash");
    if (want("D13"))
        case_tryParsePacket("D13", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ANON_REQ, 0, {}, fill(50)),
            "ANON_REQ, 15 ciphertext bytes after hash and ephemeral key");
    if (want("D14"))
        case_tryParsePacket("D14", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 0, {}, fill(99)),
            "ADVERT one byte short of key+timestamp+signature");
    if (want("D15"))
        case_tryParsePacket("D15", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 0, {}, fill(133)),
            "ADVERT with 33 app_data bytes: one past MAX_ADVERT_DATA_SIZE");
    if (want("D16"))
        case_tryParsePacket("D16", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_MULTIPART, 0, {}, {}),
            "MULTIPART with no payload: the inner type is read from nothing");
    if (want("D17"))
        case_tryParsePacket("D17", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_MULTIPART, 0, {},
                                         lead(0x13, 8)),
            "MULTIPART ACK whose inner ACK is 7 bytes");
    if (want("D18"))
        case_tryParsePacket("D18", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_PATH, 0, {}, fill(19)),
            "PATH, 15 ciphertext bytes: not a whole cipher block");

    // ---------------------------------------------------------------------
    // V. Shapes the pinned firmware itself produces. If any of these stops
    //    being accepted on the head, the patch costs compatibility and that is
    //    a different conversation from memory safety. Each one names the line
    //    in the pinned tree that builds it.
    // ---------------------------------------------------------------------

    if (want("V1"))
        case_tryParsePacket("V1", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, fill(4)),
            "ACK of 4 — Mesh.h:190, createAck(uint32_t)");
    if (want("V2"))
        case_tryParsePacket("V2", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, fill(6)),
            "ACK of 6 — BaseChatMesh.cpp:254, sendAckTo(from, hash, 6)");
    if (want("V3"))
        case_tryParsePacket("V3", frame(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_MULTIPART, 0, {},
                                        lead(0x13, 5)),
            "MULTIPART ACK, remaining=1, 4-byte ACK — Mesh.cpp:574");
    if (want("V4"))
        case_tryParsePacket("V4", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 0, {}, fill(100)),
            "ADVERT with no app_data — key, timestamp, signature only");
    if (want("V5"))
        case_tryParsePacket("V5", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 0, {}, fill(132)),
            "ADVERT with 32 app_data bytes — AdvertDataBuilder's maximum");
    if (want("V6"))
        case_tryParsePacket("V6", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_TXT_MSG, 0, {}, fill(20)),
            "TXT_MSG with one whole cipher block");
    if (want("V7"))
        case_tryParsePacket("V7", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_GRP_TXT, 0, {}, fill(19)),
            "GRP_TXT with one whole cipher block");
    if (want("V8"))
        case_tryParsePacket("V8", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ANON_REQ, 0, {}, fill(51)),
            "ANON_REQ with one whole cipher block");
    if (want("V9"))
        case_tryParsePacket("V9", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_PATH, 0, {}, fill(20)),
            "PATH with one whole cipher block");
    if (want("V10"))
        case_tryParsePacket("V10", frame(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_TRACE, 0, {},
                                         [] { auto p = fill(9); p[8] = 0x00; return p; }()),
            "TRACE as Mesh::createTrace builds it — Mesh.cpp:605, flags=0");
    if (want("V11"))
        case_tryParsePacket("V11", frame(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_TRACE, 0, {},
                                         [] { auto p = fill(17); p[8] = 0x00; return p; }()),
            "TRACE with eight SNR bytes appended — Mesh.cpp:700");
    if (want("V12"))
        case_tryParsePacket("V12", frame(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ACK, 0x03, fill(3), fill(4)),
            "flood ACK that has collected three 1-byte hops");
    if (want("V13"))
        case_tryParsePacket("V13", frame(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ACK, 0, {}, fill(4)),
            "transport-flood ACK with its four transport-code bytes");
    if (want("V14"))
        case_tryParsePacket("V14", frame(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_ACK, 0x42, fill(4), fill(4)),
            "direct ACK over a two-hop path of 2-byte hashes");
    if (want("V15"))
        case_tryParsePacket("V15", frame(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_CONTROL, 0, {},
                                         lead(0x01, 4)),
            "CONTROL with the high bit of payload[0] clear");
    if (want("V16"))
        case_tryParsePacket("V16", frame(ROUTE_TYPE_DIRECT, PAYLOAD_TYPE_RAW_CUSTOM, 0, {}, fill(10)),
            "RAW_CUSTOM — application-defined, no shape to check");

    std::printf("\nall requested cases ran to completion\n");
    return 0;
}
