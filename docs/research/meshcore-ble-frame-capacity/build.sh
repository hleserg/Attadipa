#!/usr/bin/env bash
#
# Build the BLE frame-capacity harness against one upstream revision.
#
#   ./build.sh <tag> <git-ref>
#   ./build.sh post 4f5e8b7aa63408370d95d44cdf60ba4125f07ea0
#
# <tag> names the output; <git-ref> is anything the upstream clone can resolve.
# Sources are exported with `git archive`, so the clone is never modified and
# two revisions can be built side by side — which is the point: the defect is
# visible only as a difference between them.
#
# Nothing here is part of an Attadipa build. See ../MESHCORE_BLE_FRAME_CAPACITY.md.

set -euo pipefail

OFFBAND_SRC="${OFFBAND_SRC:-/tmp/offband-src}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"

if [ $# -ne 2 ]; then
    echo "usage: $0 <tag> <git-ref>" >&2
    exit 64
fi
tag=$1
ref=$2

if [ ! -d "$OFFBAND_SRC/.git" ]; then
    cat >&2 <<EOF
No Offband clone at $OFFBAND_SRC.

    git clone --filter=blob:none https://github.com/OffbandMesh/meshcore-firmware $OFFBAND_SRC

A blobless clone is enough; set OFFBAND_SRC to use one elsewhere. A shallow
clone is not — this needs to resolve two revisions on either side of PR #939.
EOF
    exit 66
fi

if ! git -C "$OFFBAND_SRC" rev-parse --verify --quiet "$ref^{commit}" >/dev/null; then
    echo "$OFFBAND_SRC cannot resolve '$ref' — fetch it first:" >&2
    echo "    git -C $OFFBAND_SRC fetch origin $ref" >&2
    exit 66
fi

tree="$out/tree-$tag"
rm -rf "$tree"
mkdir -p "$tree"
git -C "$OFFBAND_SRC" archive "$ref" src/helpers | tar -x -C "$tree"

# Three upstream headers, unmodified: BleFrameSizing.h, BaseSerialInterface.h and
# MultiSerialInterface.h. They are header-only, so there is nothing else to link.
# -Wall -Wextra because a silent narrowing conversion is exactly the class of
# defect being measured.
${CXX:-g++} -std=c++17 -g -O0 -Wall -Wextra \
    -I"$tree/src" -I"$here/shim" \
    "$here/capacity.cpp" \
    -o "$out/capacity-$tag"

echo "built build/capacity-$tag from $(git -C "$OFFBAND_SRC" rev-parse "$ref")"
