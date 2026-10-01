// P3's guard, executed — Attadipa issue #142.
//
// path_arith answers "which (len, path_len) pairs underflow extra_len" on a
// revision that has no guard, using a hand-copy of src/Mesh.cpp's arithmetic.
// This answers the other half on a revision that HAS one: over the same domain,
// which pairs does the tree's own Packet::isValidPathPlaintext() let through,
// and does any of them still leave the 184-byte plaintext buffer?
//
// It links the tree's real src/Packet.cpp. Nothing is copied by hand here, so
// there is no fingerprint to keep in step — build-extras.sh builds it when the
// symbol exists and skips it when it does not, which is itself the answer for
// the pinned revision: there is no guard to execute.

#include <cstdio>
#include <Packet.h>

#ifndef PARSER_BOUNDS_REV
#define PARSER_BOUNDS_REV "unknown"
#endif

int main()
{
    std::printf("revision under test : %s\n", PARSER_BOUNDS_REV);
    std::printf("MAX_PACKET_PAYLOAD  : %d\n", MAX_PACKET_PAYLOAD);
    std::printf("len domain          : 16..%d step %d  (Utils::decrypt returns whole blocks)\n",
                MAX_PACKET_PAYLOAD - 8, CIPHER_BLOCK_SIZE);

    // The plaintext Mesh::onRecvPacket hands on: data[MAX_PACKET_PAYLOAD], of
    // which `len` bytes are defined. data[0] is path_len; the rest never has to
    // be read for the arithmetic to be decided.
    uint8_t data[MAX_PACKET_PAYLOAD] = {0};

    long accepted = 0, underflow = 0, past_end = 0, rejected = 0;
    int worst_k = 0, worst_len = 0, worst_path_len = 0;

    for (int len = CIPHER_BLOCK_SIZE; len <= MAX_PACKET_PAYLOAD - 8; len += CIPHER_BLOCK_SIZE) {
        for (int path_len = 0; path_len <= 255; path_len++) {
            data[0] = (uint8_t)path_len;
            if (!mesh::Packet::isValidPathPlaintext(data, (size_t)len)) { rejected++; continue; }
            accepted++;

            // What Mesh.cpp then does with it, in its own terms.
            const int hash_size  = (path_len >> 6) + 1;
            const int hash_count = path_len & 63;
            int k = 1 + hash_size * hash_count;   // path bytes
            k += 1;                               // extra_type
            if (k > len) {
                underflow++;
                if (k > worst_k) { worst_k = k; worst_len = len; worst_path_len = path_len; }
            }
            const int extra_len = (uint8_t)(len - k);   // the truncation under test
            if (k + extra_len > MAX_PACKET_PAYLOAD) past_end++;
        }
    }

    std::printf("\naccepted by isValidPathPlaintext         : %ld\n", accepted);
    std::printf("rejected                                 : %ld\n", rejected);
    std::printf("of the accepted, k > len (underflow)      : %ld\n", underflow);
    std::printf("of the accepted, window past data[%d]    : %ld\n", MAX_PACKET_PAYLOAD, past_end);
    if (underflow > 0) {
        std::printf("worst accepted underflow                 : len=%d path_len=0x%02X -> k=%d\n",
                    worst_len, worst_path_len, worst_k);
    }
    return (underflow == 0 && past_end == 0) ? 0 : 1;
}
