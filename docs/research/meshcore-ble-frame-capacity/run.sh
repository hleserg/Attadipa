#!/usr/bin/env bash
#
# Run every harness build.sh has produced, and say plainly which revision is
# expected to fail. Both outcomes are the evidence.
#
# Nothing here is part of an Attadipa build. See ../MESHCORE_BLE_FRAME_CAPACITY.md.

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"

shopt -s nullglob
binaries=("$out"/capacity-*)
if [ ${#binaries[@]} -eq 0 ]; then
    echo "nothing built — run ./build.sh first" >&2
    exit 66
fi

status=0
for bin in "${binaries[@]}"; do
    tag="${bin##*/capacity-}"
    echo "======================================================================"
    echo "  $tag"
    echo "======================================================================"
    "$bin" "$tag"
    rc=$?
    echo
    case "$tag" in
    pre)
        if [ $rc -eq 0 ]; then
            echo ">>> UNEXPECTED: 'pre' passed. It is the tree before PR #939, where"
            echo "    MultiSerialInterface does not override maxFrameSize(). If it"
            echo "    passes, the revision built is not the one intended."
            status=1
        else
            echo ">>> expected: 'pre' fails section 3. That failure IS the defect —"
            echo "    the wrapper reports the frame buffer, so the BLE interface's"
            echo "    MTU-aware answer is never consulted."
        fi
        ;;
    *)
        if [ $rc -ne 0 ]; then
            echo ">>> '$tag' failed and was not expected to."
            status=1
        fi
        ;;
    esac
    echo
done

exit $status
