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
static uint8_t* tight(const std::initializer_list<uint8_t>& bytes, size_t& len)
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

static void case_tryParsePacket(const char* name, std::initializer_list<uint8_t> bytes,
                                const char* what)
{
    banner(name, what);
    size_t len = 0;
    uint8_t* raw = tight(bytes, len);

    StubRadio radio; StubClock clock; StubMgr mgr;
    TestDispatcher d(radio, clock, mgr);
    mesh::Packet pkt;

    bool ok = d.tryParsePacket(&pkt, raw, static_cast<int>(len));
    std::printf("    returned %s (path_len=%u payload_len=%u)\n",
                ok ? "true" : "false", (unsigned)pkt.path_len, (unsigned)pkt.payload_len);
    std::fflush(stdout);
    release(raw);
}

static void case_readFrom(const char* name, std::initializer_list<uint8_t> bytes,
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

static void case_advert(const char* name, std::initializer_list<uint8_t> bytes,
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

    std::printf("\nall requested cases ran to completion\n");
    return 0;
}
