#!/usr/bin/env bash
#
# The two experiments that are not part of the ten-case corpus:
#
#   ./build/path_arith          finding P3 — every (len, path_len) pair that
#                               reaches src/Mesh.cpp:161-172, and which of them
#                               underflow extra_len. Self-contained: an
#                               extraction of the index arithmetic, not a build
#                               of the upstream translation unit, and the file
#                               says so at the top.
#
#   ./build/decrypt_bounds N    finding P4 — the real src/Utils.cpp compiled
#                               against a stub block cipher, writing into a
#                               184-byte destination backed by a guard page.
#                               N is src_len; 176 is clean, 177..180 are not.

set -euo pipefail

MESHCORE_SRC="${MESHCORE_SRC:-/tmp/meshcore-src}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"
tree="$out/tree-base"

mkdir -p "$out"

clang++ -std=c++17 -g -O0 "$here/path_arith.cpp" -o "$out/path_arith"
echo "built build/path_arith"

if [ ! -d "$tree/src" ]; then
    echo "build/tree-base is missing — run ./build.sh base <pinned-sha> first" >&2
    exit 66
fi

clang++ -std=c++17 -g -O0 -fsanitize=address -fno-omit-frame-pointer \
    -I"$tree/src" -I"$here/shim" \
    "$here/decrypt_bounds.cpp" "$here/shim/shim.cpp" "$tree/src/Utils.cpp" \
    -o "$out/decrypt_bounds"
echo "built build/decrypt_bounds"
