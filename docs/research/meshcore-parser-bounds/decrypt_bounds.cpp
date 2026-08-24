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

int main(int argc, char** argv)
{
    const int src_len = argc > 1 ? atoi(argv[1]) : 180;

    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uint8_t* m = (uint8_t*)mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mprotect(m + page, page, PROT_NONE);
    uint8_t* dest = m + page - MAX_PACKET_PAYLOAD;   // 184 bytes, then the wall

    static uint8_t src[512];
    static uint8_t key[CIPHER_KEY_SIZE] = {0};
    memset(src, 0xAA, sizeof(src));

    std::printf("dest = 184 bytes (uint8_t data[MAX_PACKET_PAYLOAD]), src_len = %d\n", src_len);
    std::fflush(stdout);

    int n = mesh::Utils::decrypt(key, dest, src, src_len);

    std::printf("decrypt() returned %d — wrote dest[0..%d]\n", n, n - 1);
    return 0;
}
