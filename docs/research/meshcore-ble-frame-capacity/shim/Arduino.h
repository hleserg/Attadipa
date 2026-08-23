#pragma once

// Desktop shim so that upstream's `BaseSerialInterface.h` and
// `MultiSerialInterface.h` compile natively. It is NOT an Arduino
// implementation and must never be used as one.
//
// Those two headers need only the integer types and `size_t`. Everything an
// Arduino core would also provide — `Serial`, `millis()`, `String` — is
// deliberately absent, so that a future upstream revision which starts
// depending on one fails to compile here loudly instead of linking against a
// stub that returns zero.

#include <stddef.h>
#include <stdint.h>
#include <string.h>
