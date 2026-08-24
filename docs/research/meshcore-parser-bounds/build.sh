#!/usr/bin/env bash
#
# Build the parser-bounds harness against one MeshCore revision.
#
#   ./build.sh <tag> <git-ref>
#   ./build.sh base d92964352441e53b93e8667b802e04f6e072b39e
#
# <tag> names the output; <git-ref> is anything the upstream clone can resolve.
# The sources are exported with `git archive`, so the clone is never modified and
# two revisions can be built side by side.
#
# Nothing here is part of an Attadipa build. See ../MESHCORE_PARSER_BOUNDS.md.

set -euo pipefail

MESHCORE_SRC="${MESHCORE_SRC:-/tmp/meshcore-src}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"

if [ $# -ne 2 ]; then
    echo "usage: $0 <tag> <git-ref>" >&2
    exit 64
fi
tag=$1
ref=$2

if [ ! -d "$MESHCORE_SRC/.git" ]; then
    cat >&2 <<EOF
No MeshCore clone at $MESHCORE_SRC.

    git clone --filter=blob:none https://github.com/meshcore-dev/MeshCore $MESHCORE_SRC

A blobless clone is enough; set MESHCORE_SRC to use one elsewhere. A shallow
clone is not enough — this needs to resolve several revisions, and a pull
request head has to be fetched by SHA before it can be built:

    git -C $MESHCORE_SRC fetch origin <sha>
EOF
    exit 66
fi

if ! git -C "$MESHCORE_SRC" rev-parse --verify --quiet "$ref^{commit}" >/dev/null; then
    echo "$MESHCORE_SRC cannot resolve '$ref' — fetch it first:" >&2
    echo "    git -C $MESHCORE_SRC fetch origin $ref" >&2
    exit 66
fi

tree="$out/tree-$tag"
rm -rf "$tree"
mkdir -p "$tree"
git -C "$MESHCORE_SRC" archive "$ref" src | tar -x -C "$tree"

# Four upstream translation units, unmodified, plus the harness and the shim.
# -O0 so the sanitizer's line numbers name the statement rather than whatever a
# pass hoisted it into; the findings are about bounds, not about codegen.
clang++ -std=c++17 -g -O0 -fsanitize=address -fno-omit-frame-pointer \
    -I"$tree/src" -I"$here/shim" \
    "$here/harness.cpp" "$here/shim/shim.cpp" \
    "$tree/src/Packet.cpp" \
    "$tree/src/Dispatcher.cpp" \
    "$tree/src/helpers/AdvertDataHelpers.cpp" \
    -o "$out/harness-$tag"

echo "built build/harness-$tag from $(git -C "$MESHCORE_SRC" rev-parse "$ref")"
