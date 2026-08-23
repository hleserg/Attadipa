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

# The finding is the DIFFERENCE between the two revisions, so half of it is not a
# smaller version of it — it is a run that proves nothing while reading as a pass.
# If `build.sh post` ever fails (Offband force-pushes `firmware-base`, a SHA is
# GC'd — it is not our repository), a pre-only run would otherwise print
# "expected: pre fails" and exit 0. Named revisions, not a count, so that a
# stale binary from an older build cannot stand in for a missing one.
missing=()
for want in pre post; do
    [ -x "$out/capacity-$want" ] || missing+=("$want")
done
if [ ${#missing[@]} -ne 0 ]; then
    echo "missing build(s): ${missing[*]}" >&2
    echo "Both revisions are required — the defect is only visible as the" >&2
    echo "difference between them. See the README for the two SHAs." >&2
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
