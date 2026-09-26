#!/usr/bin/env bash
#
# Build and run the offline-queue drain trace.
#
#   ./run.sh                 # print the table
#   ./run.sh > trace.md      # capture it
#
# It compiles the shipping `attadipa_link` translation units directly rather
# than through the project's CMake, for one reason: this is not part of an
# Attadipa build and must not become one. `cmake --build build` does not know
# this file exists and nothing here is added to `tests/CMakeLists.txt`.
#
# Three translation units, and all three are production code. `trace.cpp` is
# the only file here that is not.
#
# See ../MESHCORE_OFFLINE_QUEUE_FORWARD_COMPAT.md for what the table means.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out="$here/build"
mkdir -p "$out"

revision="$(git -C "$root" rev-parse HEAD 2>/dev/null || echo UNKNOWN)"
dirty=""
if ! git -C "$root" diff --quiet 2>/dev/null; then
    dirty=" (working tree modified)"
fi

# -Wall and no sanitizer: the question here is control flow through a state
# machine, not memory safety, and the harness allocates nothing the class does
# not. -O0 keeps a crash landing on the statement that caused it.
g++ -std=c++17 -g -O0 -Wall -Wextra \
    -I"$root/core/include" -I"$root/link/include" \
    "$here/trace.cpp" \
    "$root/link/src/meshcore_companion.cpp" \
    "$root/link/src/link_state.cpp" \
    "$root/core/src/mesh_service.cpp" \
    -o "$out/trace"

echo "Attadipa $revision$dirty"
echo
"$out/trace"
