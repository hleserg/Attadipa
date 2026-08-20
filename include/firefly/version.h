#pragma once

// Firefly OS version. Pre-implementation: this exists so that the host build,
// the test target, and CI have something real to compile and check.
#define FIREFLY_VERSION_MAJOR 0
#define FIREFLY_VERSION_MINOR 0
#define FIREFLY_VERSION_PATCH 1

#define FIREFLY_STRINGIFY_(x) #x
#define FIREFLY_STRINGIFY(x) FIREFLY_STRINGIFY_(x)

#define FIREFLY_VERSION_STRING          \
    FIREFLY_STRINGIFY(FIREFLY_VERSION_MAJOR) "." \
    FIREFLY_STRINGIFY(FIREFLY_VERSION_MINOR) "." \
    FIREFLY_STRINGIFY(FIREFLY_VERSION_PATCH)
