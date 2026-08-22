#pragma once

// Attadipa version. Pre-implementation: this exists so that the host build,
// the test target, and CI have something real to compile and check.
#define ATTADIPA_VERSION_MAJOR 0
#define ATTADIPA_VERSION_MINOR 0
#define ATTADIPA_VERSION_PATCH 1

#define ATTADIPA_STRINGIFY_(x) #x
#define ATTADIPA_STRINGIFY(x) ATTADIPA_STRINGIFY_(x)

#define ATTADIPA_VERSION_STRING          \
    ATTADIPA_STRINGIFY(ATTADIPA_VERSION_MAJOR) "." \
    ATTADIPA_STRINGIFY(ATTADIPA_VERSION_MINOR) "." \
    ATTADIPA_STRINGIFY(ATTADIPA_VERSION_PATCH)
