#!/usr/bin/env bash
#
# The two experiments that are not part of the ten-case corpus:
#
#   ./build/path_guard          finding P3, on a revision that has a guard —
#                               the same domain through the tree's own
#                               Packet::isValidPathPlaintext. Built only where
#                               that symbol exists.
#
#   ./build/path_arith          finding P3 — every (len, path_len) pair that
#                               reaches src/Mesh.cpp:160-172, and which of them
#                               underflow extra_len. The constants and the
#                               validator come from the built tree; the eight
#                               lines of index arithmetic are a hand-copy, and
#                               are fingerprinted against the tree below.
#
#   ./build/decrypt_bounds N    finding P4 — the real src/Utils.cpp compiled
#                               against a stub block cipher, writing into a
#                               184-byte destination backed by a guard page.
#                               N is src_len; 176 is clean, 177..180 are not.
#                               Add --mac to enter through MACThenDecrypt,
#                               which is the call Mesh::onRecvPacket makes and
#                               the place upstream put the gate.
#
# Usage:
#
#   ./build-extras.sh [tag]     tag defaults to "base"; it names a tree already
#                               written by ./build.sh <tag> <ref>.
#
# WHY THE TAG IS AN ARGUMENT. Both of these are enrolled as revision checks by
# the entry condition on a future MeshCore pin. An earlier version hardcoded
# tree-base, so `./build.sh cand <sha>` followed by ./build-extras.sh measured
# whatever tree-base happened to hold — reporting the pin's answer under the
# candidate's name, silently, in the tool written to prevent exactly that.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"

if [ $# -gt 1 ]; then
    echo "usage: $0 [tag]" >&2
    exit 64
fi
tag="${1:-base}"
tree="$out/tree-$tag"

if [ ! -d "$tree/src" ]; then
    echo "build/tree-$tag is missing — run ./build.sh $tag <git-ref> first" >&2
    exit 66
fi

if [ ! -f "$tree/.revision" ]; then
    echo "build/tree-$tag has no .revision — it predates this script; rebuild it" >&2
    echo "    ./build.sh $tag <git-ref>" >&2
    exit 66
fi
revision=$(cat "$tree/.revision")

mkdir -p "$out"

# --- P4, first, because P3's gate must not take it down with it ------------
#
# decrypt_bounds compiles the tree's own src/Utils.cpp and copies nothing, so
# it is correct on any revision. The P3 fingerprint below is a statement about
# path_arith alone. Building them in the other order — which is how this script
# started — meant that a revision which had MOVED the PATH branch could not be
# measured for P4 either, and the revisions that move it are exactly the ones
# worth measuring. #3521's head is one.

clang++ -std=c++17 -g -O0 -fsanitize=address -fno-omit-frame-pointer \
    -I"$tree/src" -I"$here/shim" \
    "$here/decrypt_bounds.cpp" "$here/shim/shim.cpp" "$tree/src/Utils.cpp" \
    -o "$out/decrypt_bounds"
echo "built build/decrypt_bounds from $revision"

# --- P3's guard, where there is one ----------------------------------------
#
# Built only when the tree declares Packet::isValidPathPlaintext. Its absence
# is not a build failure: on every revision up to and including the pin there
# is no guard, and "there is nothing to execute" is the finding rather than an
# error. path_arith below is what measures those.

if grep -q "isValidPathPlaintext" "$tree/src/Packet.h"; then
    clang++ -std=c++17 -g -O0 -fsanitize=address -fno-omit-frame-pointer \
        -DPARSER_BOUNDS_REV="\"$revision\"" \
        -I"$tree/src" -I"$here/shim" \
        "$here/path_guard.cpp" "$here/shim/shim.cpp" "$tree/src/Packet.cpp" \
        -o "$out/path_guard"
    echo "built build/path_guard from $revision"
else
    rm -f "$out/path_guard"
    echo "no Packet::isValidPathPlaintext at $revision — nothing to execute for P3's guard"
fi

# --- P3's hand-copy, fingerprinted against the tree ------------------------
#
# path_arith takes its constants and its validator from the built revision, but
# the index arithmetic in src/Mesh.cpp's PATH branch cannot be executed on a
# host (it sits behind MACThenDecrypt, so behind AES, SHA-256 and ed25519) and
# is therefore copied by hand. A hand-copy cannot notice upstream changing what
# it copied — including upstream *fixing* it, which is the case that matters.
#
# So the branch is extracted between two anchors, whitespace-normalised and
# hashed. A revision that has touched those lines fails here, loudly, instead of
# being measured with the pinned revision's arithmetic. Re-reading P3 by hand at
# that revision is then a decision somebody makes rather than one they skip
# without knowing. #3269's head is such a revision: it adds a line inside this
# region, so it is expected to fail this check.
PATH_BRANCH_SHA256_PIN=5eee273c0079c2e6332f42b0d3d285f7b8eaf55ecdf81602941b4eb48a09fbc2

branch_text=$(
    awk '/if \(pkt->getPayloadType\(\) == PAYLOAD_TYPE_PATH\) \{/,/uint8_t extra_len = len - k;/' \
        "$tree/src/Mesh.cpp" \
        | sed -e 's/[[:space:]]\+/ /g' -e 's/^ //' -e 's/ $//'
)
if [ -z "$branch_text" ]; then
    echo "could not find the PAYLOAD_TYPE_PATH branch in $tree/src/Mesh.cpp." >&2
    echo "Upstream has moved or renamed it. Re-read finding P3 by hand at $revision." >&2
    echo "build/decrypt_bounds IS built and is valid for this revision." >&2
    exit 65
fi
digest=$(printf '%s\n' "$branch_text" | sha256sum | cut -d' ' -f1)
if [ "$digest" != "$PATH_BRANCH_SHA256_PIN" ]; then
    cat >&2 <<EOF
The PAYLOAD_TYPE_PATH branch in src/Mesh.cpp has changed at $revision.

    expected $PATH_BRANCH_SHA256_PIN   (the pinned revision, d929643)
    found    $digest

path_arith's index arithmetic is a hand-copy of those lines, so its answer is
about the pinned revision and NOT about this one. Re-read finding P3 in
../MESHCORE_PARSER_BOUNDS.md against $revision, update the extraction and this
digest together, and only then trust the number.

build/decrypt_bounds IS built from $revision and is valid for it: it compiles
that tree's own src/Utils.cpp and copies nothing, so P4 can still be measured
here. Only path_arith is refused.
EOF
    exit 65
fi

clang++ -std=c++17 -g -O0 \
    -DPARSER_BOUNDS_REV="\"$revision\"" \
    -I"$tree/src" -I"$here/shim" \
    "$here/path_arith.cpp" "$here/shim/shim.cpp" "$tree/src/Packet.cpp" \
    -o "$out/path_arith"
echo "built build/path_arith from $revision (PATH branch digest matches)"

