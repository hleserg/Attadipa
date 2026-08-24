#!/usr/bin/env bash
#
# Run the ten-case corpus against every harness that has been built, and print
# the matrix in §4 of ../MESHCORE_PARSER_BOUNDS.md.
#
# One case per process, because the first sanitizer report ends the process and
# a second case in the same run would never be reached.

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"
cases=(A1 A2 A3 B1 B2 B3 C1 C2 C3 C4)

shopt -s nullglob
harnesses=("$out"/harness-*)
if [ ${#harnesses[@]} -eq 0 ]; then
    echo "nothing built yet — see build.sh" >&2
    exit 66
fi

for h in "${harnesses[@]}"; do
    tag="${h##*/harness-}"
    for c in "${cases[@]}"; do
        outp=$(ASAN_OPTIONS=detect_leaks=0 "$h" "$c" 2>&1); rc=$?
        if grep -q "AddressSanitizer" <<<"$outp"; then
            site=$(grep -oE '(Dispatcher|Packet|AdvertDataHelpers)\.cpp:[0-9]+:[0-9]+' <<<"$outp" | head -1)
            # Two wordings for one fact. A read just past a heap chunk is a
            # "heap-buffer-overflow" with a size; a read into the guard page is
            # a SEGV that ASan reports without one. Both are the parser reading
            # past the length it was given, so both print here — but they print
            # differently, because a row that hid which mechanism fired would
            # make a harness bug look like a finding.
            kind=$(grep -oE 'heap-buffer-overflow|SEGV|stack-buffer-overflow' <<<"$outp" | head -1)
            size=$(grep -oE 'READ of size [0-9]+' <<<"$outp" | head -1)
            printf '%-8s %-3s  OOB    %-22s at %s\n' "$tag" "$c" \
                "${kind}${size:+, $size}" "$site"
        elif [ $rc -ge 128 ]; then
            # The guard page without a sanitizer report: still an over-read, but
            # ASan did not name the line. Reported rather than swallowed.
            printf '%-8s %-3s  OOB    guard page, signal %d\n' "$tag" "$c" "$((rc - 128))"
        elif [ $rc -ne 0 ]; then
            printf '%-8s %-3s  ERROR  harness exited %d\n' "$tag" "$c" "$rc"
        else
            printf '%-8s %-3s  clean  %s\n' "$tag" "$c" \
                "$(grep -E 'returned|valid=' <<<"$outp" | head -1 | sed 's/^ *//')"
        fi
    done
    echo
done
