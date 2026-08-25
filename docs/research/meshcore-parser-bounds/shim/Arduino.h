// Host shim. Not upstream code: enough of the Arduino surface for the pinned
// MeshCore parser translation units to compile on a desktop.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
class Print {
public:
  virtual size_t write(uint8_t) { return 1; }
  size_t print(const char*) { return 0; }
  size_t print(char) { return 0; }
  size_t println(const char*) { return 0; }
  int printf(const char*, ...) { return 0; }
};
class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  virtual size_t readBytes(uint8_t*, size_t) { return 0; }
  virtual size_t write(const uint8_t*, size_t n) { return n; }
  using Print::write;
};
extern Stream Serial;
static inline unsigned long millis() { return 0; }
static inline void delay(unsigned long) {}
