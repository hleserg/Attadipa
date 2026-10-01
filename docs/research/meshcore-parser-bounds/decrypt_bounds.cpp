// Utils::decrypt output bound — Attadipa issue #142, finding P4.
//
// Real upstream translation unit (src/Utils.cpp, unmodified) against a stub
// block cipher. The cipher is irrelevant: what is under test is how far the
// loop at src/Utils.cpp:76-79 walks 'dest' for a src_len that is not a multiple
// of the block size, which is exactly the shape Mesh::onRecvPacket hands it.
//
// dest is 184 bytes — sizeof(uint8_t data[MAX_PACKET_PAYLOAD]) in
// Mesh::onRecvPacket — placed flush against a PROT_NONE page.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <Utils.h>

// Two entry points, because the gate moved twice and "is this revision safe"
// has a different answer at each.
//
//   decrypt_bounds <n>        calls Utils::decrypt directly with src_len = n
//   decrypt_bounds <n> --mac  calls Utils::MACThenDecrypt with src_len = n,
//                             which is what Mesh::onRecvPacket calls
//
// The --mac mode passes the shim's HMAC, which is all zeroes, by starting the
// buffer with two zero bytes. That is NOT a claim that a real 2-byte MAC can be
// passed at will; it is how the harness gets past a stub cipher to the length
// arithmetic on the other side, which is the only thing under test. The real
// gate costs an attacker a MAC, and the report says so in its own row.
int main(int argc, char** argv)
{
    const int src_len = argc > 1 ? atoi(argv[1]) : 180;
    const bool via_mac = argc > 2 && std::strcmp(argv[2], "--mac") == 0;

    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* m = (uint8_t*)mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    // Checked, because the failure mode is indistinguishable from the finding:
    // an unchecked mmap leaves dest a wild pointer and the first block write
    // faults, which looks exactly like the over-write under test and is not it.
    if (m == MAP_FAILED) { std::perror("mmap"); return 70; }
    if (mprotect(m + page, page, PROT_NONE) != 0) { std::perror("mprotect"); return 70; }
    uint8_t* dest = m + page - MAX_PACKET_PAYLOAD;   // 184 bytes, then the wall

    static uint8_t src[512];
    static uint8_t key[CIPHER_KEY_SIZE] = {0};
    memset(src, 0xAA, sizeof(src));
    if (via_mac) { src[0] = 0; src[1] = 0; }   // matches the shim's zero HMAC

    std::printf("dest = 184 bytes (uint8_t data[MAX_PACKET_PAYLOAD]), src_len = %d, via %s\n",
                src_len, via_mac ? "MACThenDecrypt (Mesh::onRecvPacket's call)" : "decrypt");
    std::fflush(stdout);

    int n = via_mac ? mesh::Utils::MACThenDecrypt(key, dest, src, src_len)
                    : mesh::Utils::decrypt(key, dest, src, src_len);

    std::printf("%s returned %d — wrote dest[0..%d]\n",
                via_mac ? "MACThenDecrypt()" : "decrypt()", n, n - 1);
    return 0;
}
