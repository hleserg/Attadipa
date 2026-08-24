// Host shim. Not upstream code, and NOT a cipher: decryptBlock/encryptBlock
// copy their 16 bytes through. The experiment this serves measures how many
// bytes Utils::decrypt writes for a given src_len, which is a property of its
// loop and not of the block function it calls.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
class AES128 {
public:
  void setKey(const uint8_t*, size_t) {}
  void decryptBlock(uint8_t* out, const uint8_t* in) { memcpy(out, in, 16); }
  void encryptBlock(uint8_t* out, const uint8_t* in) { memcpy(out, in, 16); }
};
