// PAYLOAD_TYPE_PATH length arithmetic — Attadipa issue #142, finding P3.
//
// THIS IS AN EXTRACTION, NOT AN EXECUTION OF THE UPSTREAM TRANSLATION UNIT.
// Reaching src/Mesh.cpp's PATH branch for real needs Utils::MACThenDecrypt to
// succeed, which needs the AES and SHA256 libraries MeshCore pulls from
// PlatformIO, and an Identity, which needs ed25519. None of that is built here.
// What is reproduced below is the index arithmetic, copied byte for byte from
//
//     meshcore-dev/MeshCore @ d92964352441e53b93e8667b802e04f6e072b39e
//     src/Mesh.cpp lines 161-172
//
// and run over its entire input domain, so the claim "extra_len can underflow"
// is a computed result rather than an assertion about code someone read.

#include <cstdio>
#include <cstdint>

#define MAX_PACKET_PAYLOAD  184
#define MAX_PATH_SIZE        64

// src/Packet.cpp:13-18, verbatim.
static bool isValidPathLen(uint8_t path_len) {
  uint8_t hash_count = path_len & 63;
  uint8_t hash_size = (path_len >> 6) + 1;
  if (hash_size == 4) return false;  // Reserved for future
  return hash_count*hash_size <= MAX_PATH_SIZE;
}

int main()
{
    // The decrypted plaintext length. MACThenDecrypt returns 0 for anything at
    // or under CIPHER_MAC_SIZE, and cannot exceed the payload buffer.
    // Utils::decrypt returns whole 16-byte blocks ("will always be multiple of
    // 16", src/Utils.cpp:82), so len is a multiple of 16. 176 is the largest
    // that still fits data[184]; 192 is the P4 case and is excluded here so the
    // two findings do not get mixed up.
    const int len_min = 16, len_max = 176, len_step = 16;

    long total = 0, underflow = 0, read_past_data = 0;
    int worst_extra_len = 0, worst_len = 0, worst_path_len = 0, worst_k = 0;
    int smallest_len = 1 << 30, smallest_path_len = 256;

    for (int len = len_min; len <= len_max; len += len_step) {
        for (int pl = 0; pl <= 255; pl++) {
            uint8_t path_len = (uint8_t)pl;
            if (!isValidPathLen(path_len)) continue;   // upstream rejects and breaks
            total++;

            // --- src/Mesh.cpp:161-172, indices only ---
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
                }
                // Does the consumer's window run off the end of data[184]?
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
    std::printf("  of those, &data[k] + extra_len > 184    : %ld\n", read_past_data);
    std::printf("smallest underflowing input              : len=%d path_len=0x%02X\n",
                smallest_len, smallest_path_len);
    std::printf("largest over-read window                 : len=%d path_len=0x%02X"
                " -> k=%d extra_len=%d, i.e. data[%d..%d] against data[0..183]"
                " (%d bytes past the end)\n",
                worst_len, worst_path_len, worst_k, worst_extra_len,
                worst_k, worst_k + worst_extra_len - 1,
                worst_k + worst_extra_len - MAX_PACKET_PAYLOAD);
    return 0;
}
