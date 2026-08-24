// Host shim. Not upstream code. The parsers under test do not depend on the
// digest value; nothing here is a cryptographic implementation and it must
// never be used as one.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
class SHA256 {
public:
  void reset() {}
  void update(const void*, size_t) {}
  void finalize(void* out, size_t len) { memset(out, 0, len); }
  void resetHMAC(const void*, size_t) {}
  void finalizeHMAC(const void*, size_t, void* out, size_t len) { memset(out, 0, len); }
};
