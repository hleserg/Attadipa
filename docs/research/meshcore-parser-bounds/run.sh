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
cases=(A1 A2 A3 B1 B2 B3 C1 C2 C3 C4 C5 C6
       D1 D2 D3 D4 D5 D6 D7 D8 D9 D10 D11 D12 D13 D14 D15 D16 D17 D18
       V1 V2 V3 V4 V5 V6 V7 V8 V9 V10 V11 V12 V13 V14 V15 V16)

# One case may be named on the command line, so that a single row can be
# re-run and read in full rather than through the matrix's one line.
if [ $# -gt 0 ]; then cases=("$@"); fi

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
            # -a, because an advert name is attacker-chosen bytes and grep
            # calls a NUL or a stray 0x80 "binary" and prints nothing — which
            # is a blank cell in the matrix for a case that ran perfectly.
            printf '%-8s %-3s  clean  %s\n' "$tag" "$c" \
                "$(grep -aE 'returned|valid=' <<<"$outp" | head -1 | sed 's/^ *//')"
        fi
    done
    echo
done
