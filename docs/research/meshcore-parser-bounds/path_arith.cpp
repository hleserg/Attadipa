// PAYLOAD_TYPE_PATH length arithmetic — Attadipa issue #142, finding P3.
//
// THIS IS AN EXTRACTION, NOT AN EXECUTION OF THE UPSTREAM TRANSLATION UNIT.
// Reaching src/Mesh.cpp's PATH branch for real needs Utils::MACThenDecrypt to
// succeed, which needs the AES and SHA256 libraries MeshCore pulls from
// PlatformIO, and an Identity, which needs ed25519. None of that is built here.
// What is reproduced below is the index arithmetic, copied by hand from
//
//     src/Mesh.cpp lines 160-172
//
// and run over its entire input domain, so the claim "extra_len can underflow"
// is a computed result rather than an assertion about code someone read.
//
// WHAT REVISION IT SPEAKS FOR. Everything a hand-copy cannot notice is taken
// from the built tree instead of retyped here: MAX_PACKET_PAYLOAD and
// MAX_PATH_SIZE come from the revision's own MeshCore.h, and isValidPathLen is
// the revision's own Packet.cpp, linked in and called. What is left hand-copied
// is the eight lines of index arithmetic, and build-extras.sh fingerprints those
// against the tree before it will build this at all. So a revision where
// upstream changed the constants, the validator or the arithmetic cannot be
// silently measured with the pinned revision's answer — which is exactly the
// failure a "re-run the corpus against the candidate" entry condition is for.

#include <cstdio>
#include <cstdint>

#include <Packet.h>     // mesh::Packet::isValidPathLen, at the built revision
#include <MeshCore.h>   // MAX_PACKET_PAYLOAD, MAX_PATH_SIZE, at the built revision

#ifndef PARSER_BOUNDS_REV
#define PARSER_BOUNDS_REV "unknown — built outside build-extras.sh"
#endif

int main()
{
    // The decrypted plaintext length. MACThenDecrypt returns 0 for anything at
    // or under CIPHER_MAC_SIZE, and cannot exceed the payload buffer.
    // Utils::decrypt returns whole 16-byte blocks ("will always be multiple of
    // 16", src/Utils.cpp:81), so len is a multiple of 16. MAX_PACKET_PAYLOAD
    // rounded down to a whole block is the largest that still fits data[]; the
    // next block up is the P4 case and is excluded here so the two findings do
    // not get mixed up.
    const int len_step = 16;
    const int len_min  = len_step;
    const int len_max  = (MAX_PACKET_PAYLOAD / len_step) * len_step;

    std::printf("revision under test : %s\n", PARSER_BOUNDS_REV);
    std::printf("MAX_PACKET_PAYLOAD  : %d   (from the tree's MeshCore.h)\n",
                (int)MAX_PACKET_PAYLOAD);
    std::printf("MAX_PATH_SIZE       : %d   (from the tree's MeshCore.h)\n",
                (int)MAX_PATH_SIZE);
    std::printf("len domain          : %d..%d step %d\n\n",
                len_min, len_max, len_step);

    long total = 0, underflow = 0, read_past_data = 0;
    int worst_extra_len = 0, worst_len = 0, worst_path_len = 0, worst_k = 0;
    int smallest_len = 1 << 30, smallest_path_len = 256;
    int smallest_k = 0, smallest_extra_len = 0;

    for (int len = len_min; len <= len_max; len += len_step) {
        for (int pl = 0; pl <= 255; pl++) {
            uint8_t path_len = (uint8_t)pl;
            // The revision's own validator, linked from its Packet.cpp.
            if (!mesh::Packet::isValidPathLen(path_len)) continue;  // upstream rejects and breaks
            total++;

            // --- src/Mesh.cpp:160-172, indices only ---
            int k = 0;
            k++;                                        // uint8_t path_len = data[k++]
            uint8_t hash_size  = (path_len >> 6) + 1;
            uint8_t hash_count = path_len & 63;
            k += hash_size * hash_count;                // uint8_t* path = &data[k]; k += ...
            k++;                                        // extra_type = data[k++] & 0x0F
            uint8_t extra_len = (uint8_t)(len - k);     // <-- the subtraction under test
            // --- end extraction ---

            if (len - k < 0) {
                underflow++;
                if (len < smallest_len || (len == smallest_len && path_len < smallest_path_len)) {
                    smallest_len = len; smallest_path_len = path_len;
                    smallest_k = k; smallest_extra_len = extra_len;
                }
                // Does the consumer's window run off the end of data[]?
                if (k + extra_len > MAX_PACKET_PAYLOAD) {
                    read_past_data++;
                    if (extra_len > worst_extra_len) {
                        worst_extra_len = extra_len; worst_len = len;
                        worst_path_len = path_len; worst_k = k;
                    }
                }
            }
        }
    }

    std::printf("accepted (len, path_len) pairs           : %ld\n", total);
    std::printf("pairs where k > len (extra_len underflows): %ld\n", underflow);
    std::printf("  of those, &data[k] + extra_len > %-3d   : %ld\n",
                (int)MAX_PACKET_PAYLOAD, read_past_data);
    if (underflow > 0) {
        std::printf("smallest underflowing input              : len=%d path_len=0x%02X"
                    " -> k=%d extra_len=%d, i.e. data[%d..%d] against data[0..%d]"
                    " (%d bytes past the end)\n",
                    smallest_len, smallest_path_len, smallest_k, smallest_extra_len,
                    smallest_k, smallest_k + smallest_extra_len - 1,
                    (int)MAX_PACKET_PAYLOAD - 1,
                    smallest_k + smallest_extra_len - MAX_PACKET_PAYLOAD);
        std::printf("largest over-read window                 : len=%d path_len=0x%02X"
                    " -> k=%d extra_len=%d, i.e. data[%d..%d] against data[0..%d]"
                    " (%d bytes past the end)\n",
                    worst_len, worst_path_len, worst_k, worst_extra_len,
                    worst_k, worst_k + worst_extra_len - 1,
                    (int)MAX_PACKET_PAYLOAD - 1,
                    worst_k + worst_extra_len - MAX_PACKET_PAYLOAD);
    }
    return 0;
}
