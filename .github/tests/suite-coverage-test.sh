#!/usr/bin/env bash
# The guard that says every self-test suite is run by a workflow, tested by
# planting suites that are not.
#
# This is the half #385 could not have: the guard lived inline in a `run:`
# block, and an inline block cannot be pointed at a tree with a planted file in
# it. So the one step in ci.yml whose whole job is "no suite hides here" was
# itself unverified, which is the joke the issue was written around. The script
# takes a root for exactly this reason.
#
# Every case builds a throwaway root -- workflows, .github/tests, tools -- and
# runs THE SHIPPING SCRIPT against it. Nothing here re-implements the rule.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
GUARD="$REPO/.github/scripts/check-suite-coverage.sh"

pass=0
fail=0

ok() { echo "  ok    $1"; pass=$((pass + 1)); }
no() { echo "  FAIL  $1 ${2:-}"; fail=$((fail + 1)); }

check() {  # name, expected rc, actual rc
    if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "-- expected rc=$2, got rc=$3"; fi
}

contains() {  # name, haystack, needle
    case "$2" in
        *"$3"*) ok "$1" ;;
        *) no "$1" "-- output does not contain \"$3\"" ;;
    esac
}

# A root with the shape the guard expects and nothing in it yet.
new_root() {
    local d
    d="$(mktemp -d)"
    mkdir -p "$d/.github/workflows" "$d/.github/tests" "$d/tools/sub"
    printf 'jobs:\n  a:\n    steps:\n' > "$d/.github/workflows/ci.yml"
    printf '%s' "$d"
}

wire() {  # root, line to add to the workflow
    printf '      - run: %s\n' "$2" >> "$1/.github/workflows/ci.yml"
}

echo "check-suite-coverage selftest"

# --- 1. An empty tree is covered, because there is nothing to cover. ---------
R="$(new_root)"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a tree with no suites passes" 0 "$rc"
rm -rf "$R"

# --- 2. A shell suite nobody runs is named and turns it red. -----------------
R="$(new_root)"
: > "$R/.github/tests/planted-test.sh"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "an unwired shell suite fails the guard" 1 "$rc"
contains "  and the message names the file" "$out" ".github/tests/planted-test.sh"
contains "  and says what to add" "$out" "bash .github/tests/planted-test.sh"
rm -rf "$R"

# --- 3. Wiring it makes it pass. --------------------------------------------
R="$(new_root)"
: > "$R/.github/tests/planted-test.sh"
wire "$R" "bash .github/tests/planted-test.sh"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a wired shell suite passes" 0 "$rc"
rm -rf "$R"

# --- 4. The Python half, both naming conventions. ----------------------------
# `test_*.py` beside the checker, and `selftest*.py` inside the package. Both
# are in use in this repository and neither is being renamed, so the guard has
# to see both -- a rule that covered one convention would have left five of the
# nine suites #387 is about exactly as invisible as they were.
for name in tools/sub/test_planted.py tools/sub/selftest.py tools/sub/planted_selftest.py; do
    R="$(new_root)"
    : > "$R/$name"
    out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
    check "an unwired $name fails the guard" 1 "$rc"
    contains "  and the message names it" "$out" "$name"
    rm -rf "$R"

    R="$(new_root)"
    : > "$R/$name"
    wire "$R" "python3 $name"
    out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
    check "a wired $name passes" 0 "$rc"
    rm -rf "$R"
done

# --- 5. A mention is not a run. ---------------------------------------------
# The cheap version of this check greps the workflow for the path. ci.yml is
# full of prose that names files -- the comment above the watch-protocol step
# names `tools/watch/selftest.py` in a sentence about it NOT being run -- so a
# path-only match would have called that suite covered by the comment that
# says it is not.
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
printf '      # tools/sub/selftest.py is not run here, and that is the point\n' \
    >> "$R/.github/workflows/ci.yml"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a suite named only in a comment is still unwired" 1 "$rc"
rm -rf "$R"

# --- 6. Any workflow counts, not only ci.yml. --------------------------------
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
printf 'jobs:\n  b:\n    steps:\n      - run: python3 tools/sub/selftest.py\n' \
    > "$R/.github/workflows/nightly.yml"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a suite run by another workflow counts" 0 "$rc"
rm -rf "$R"

# --- 7. A root with no workflows is an error, not a pass. --------------------
# The dangerous answer to "nothing here can run a suite" is "then everything is
# covered". Running the guard from the wrong directory has to be loud.
R="$(mktemp -d)"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a root with no .github/workflows is rc=2" 2 "$rc"
rm -rf "$R"

out="$(bash "$GUARD" /nonexistent-root-for-the-selftest 2>&1)"; rc=$?
check "a root that does not exist is rc=2" 2 "$rc"

# --- 8. Several unwired suites are all named, not just the first. ------------
R="$(new_root)"
: > "$R/.github/tests/one-test.sh"
: > "$R/tools/sub/selftest.py"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "two unwired suites still fail once" 1 "$rc"
contains "  the shell one is named" "$out" "one-test.sh"
contains "  the python one is named too" "$out" "tools/sub/selftest.py"
rm -rf "$R"

# --- 9. The allow-list, if it is ever used, must carry a reason. -------------
# Not a hypothetical: the list is empty today, and an empty list is the one
# state in which nobody notices that entries are unchecked. The reason is
# printed on every run so an entry that has outlived it is visible.
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
python3 - "$R/../guard.sh" "$GUARD" <<'PY'
import sys, pathlib
src = pathlib.Path(sys.argv[2]).read_text()
src = src.replace("ALLOWED\n}", "tools/sub/selftest.py|it is proved by the case below it\nALLOWED\n}", 1)
pathlib.Path(sys.argv[1]).write_text(src)
PY
out="$(bash "$R/../guard.sh" "$R" 2>&1)"; rc=$?
check "an allow-listed suite passes" 0 "$rc"
contains "  and its reason is printed" "$out" "it is proved by the case below it"
rm -f "$R/../guard.sh"; rm -rf "$R"

# --- 10. And the shipping tree is green. -------------------------------------
# The nine cases above prove the rule. This one proves the repository obeys it,
# which is what the ci.yml step is for -- run here as well so a `git bisect`
# lands on the commit that broke it rather than on the workflow.
out="$(bash "$GUARD" "$REPO" 2>&1)"; rc=$?
check "this repository has no unwired suite" 0 "$rc"
if [ "$rc" != 0 ]; then echo "$out"; fi

echo
echo "suite-coverage selftest: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
