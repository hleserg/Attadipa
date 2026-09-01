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
# runs THE SHIPPING SCRIPT against it. Nothing here re-implements the rule,
# with one stated exception: case 9 has to run a text-patched copy, because the
# allow-list is a function inside the script and there is no other way to reach
# it from outside. The patch adds one line to that list and changes nothing
# else, and the copy goes in its own `mktemp -d` rather than beside the root.
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
    # `on:` matters: the guard counts only workflows GitHub would trigger.
    printf 'on: [push]\njobs:\n  a:\n    steps:\n' > "$d/.github/workflows/ci.yml"
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
# `test_*.py` beside the checker, `*_test.py`, and `selftest*.py` inside the
# package. All three are in use in this repository and none is being renamed, so
# the guard has to see all three. It used to claim there were two and match two,
# which left `tools/watch/e2e_test.py` invisible to it.
for name in tools/sub/test_planted.py tools/sub/planted_test.py \
            tools/sub/selftest.py tools/sub/planted_selftest.py; do
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
printf 'on: [schedule]\njobs:\n  b:\n    steps:\n      - run: python3 tools/sub/selftest.py\n' \
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
# The patched copy goes in a second mktemp -d. It used to be written to
# "$R/../guard.sh" -- i.e. /tmp/guard.sh, a fixed name OUTSIDE the throwaway
# root -- which two concurrent runs race on, whose `rm -f` deletes whatever was
# already there, and which on a shared machine follows a symlink planted at it.
R="$(new_root)"
COPY="$(mktemp -d)"
: > "$R/tools/sub/selftest.py"
python3 - "$COPY/guard.sh" "$GUARD" <<'PY'
import sys, pathlib
src = pathlib.Path(sys.argv[2]).read_text()
src = src.replace("ALLOWED\n}", "tools/sub/selftest.py|it is proved by the case below it\nALLOWED\n}", 1)
pathlib.Path(sys.argv[1]).write_text(src)
PY
out="$(bash "$COPY/guard.sh" "$R" 2>&1)"; rc=$?
check "an allow-listed suite passes" 0 "$rc"
contains "  and its reason is printed" "$out" "it is proved by the case below it"
rm -rf "$COPY" "$R"

# --- 10. ctest registration counts as running a suite. -----------------------
# The needle this guard was missing. All 52 `add_test` entries in
# `tests/CMakeLists.txt` were invisible to it, so it reported every ctest-run
# suite as unrun and told the author to add a duplicate `run:` line -- which is
# what #390 round 1 did for nine of them before the review caught it.
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
mkdir -p "$R/tests"
# shellcheck disable=SC2016  # ${CMAKE_SOURCE_DIR} is CMake's, not the shell's
printf 'add_test(NAME planted\n         COMMAND ${Python3_EXECUTABLE}\n                 ${CMAKE_SOURCE_DIR}/tools/sub/selftest.py)\n' \
    > "$R/tests/CMakeLists.txt"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a suite registered with add_test passes without a run: line" 0 "$rc"
rm -rf "$R"

# --- 11. And it counts wherever in that file it sits. ------------------------
# Not a hypothetical: the first draft of this rework matched with
# `without_comments ... | grep -q`, and under `set -o pipefail` that is a coin
# toss -- `grep -q` exits at the first match, `sed` takes SIGPIPE, and the
# pipeline reports 141 for a match it found. So suites registered near the
# bottom of the file passed and identical ones near the top failed. This case
# puts the registration on line 1 of a long file, which is where it broke.
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
mkdir -p "$R/tests"
{
# shellcheck disable=SC2016  # ${CMAKE_SOURCE_DIR} is CMake's, not the shell's
    printf '${CMAKE_SOURCE_DIR}/tools/sub/selftest.py)\n'
    for _ in $(seq 1 4000); do printf 'set(padding "a line that is not a needle")\n'; done
} > "$R/tests/CMakeLists.txt"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a registration on the first line of a long file still counts" 0 "$rc"
rm -rf "$R"

# --- 12. A commented-out invocation does not count. --------------------------
# The ordinary way a WIRED suite stops running: somebody comments the step out.
# Matching the file's bytes rather than its steps called that covered. Both
# corpora have to strip comments, or the same hole reappears on the ctest side.
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
printf '      # - run: python3 tools/sub/selftest.py\n' >> "$R/.github/workflows/ci.yml"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a commented-out run: line does not count" 1 "$rc"
rm -rf "$R"

R="$(new_root)"
: > "$R/tools/sub/selftest.py"
mkdir -p "$R/tests"
printf '# add_test(NAME planted COMMAND python3 tools/sub/selftest.py)\n' \
    > "$R/tests/CMakeLists.txt"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a commented-out add_test does not count either" 1 "$rc"
rm -rf "$R"

# --- 13. A workflow GitHub would never trigger does not count. ---------------
# A file under .github/workflows with no top-level `on:` key is a file, not a
# workflow. Counting it lets a suite be "run" by something that never starts.
R="$(new_root)"
: > "$R/tools/sub/selftest.py"
printf 'jobs:\n  b:\n    steps:\n      - run: python3 tools/sub/selftest.py\n' \
    > "$R/.github/workflows/never.yml"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a workflow with no on: key does not count" 1 "$rc"
rm -rf "$R"

# `on` is YAML 1.1 truthy, so the same key is written four ways in the wild.
for key in 'on: [push]' '"on": [push]' "'on': [push]" 'true: [push]'; do
    R="$(new_root)"
    : > "$R/tools/sub/selftest.py"
    printf '%s\njobs:\n  b:\n    steps:\n      - run: python3 tools/sub/selftest.py\n' \
        "$key" > "$R/.github/workflows/other.yml"
    out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
    check "  a workflow keyed \"$key\" counts" 0 "$rc"
    rm -rf "$R"
done

# --- 14. A root with no tools/ is an error, not a pass. ----------------------
# The Python half's version of case 7. `find`'s status is discarded by a process
# substitution and `pipefail` does not reach into one, so a root with no
# `tools/` walked zero suites and exited 0 -- "all covered", from a tree the
# guard could not read. Every case above builds `tools/sub`, so none reached it.
R="$(mktemp -d)"
mkdir -p "$R/.github/workflows" "$R/.github/tests"
printf 'on: [push]\njobs:\n  a:\n    steps:\n' > "$R/.github/workflows/ci.yml"
out="$(bash "$GUARD" "$R" 2>&1)"; rc=$?
check "a root with no tools/ is rc=2" 2 "$rc"
contains "  and says why" "$out" "no tools/"
rm -rf "$R"

# --- 15. And the shipping tree is green. -------------------------------------
# The cases above prove the rule. This one proves the repository obeys it,
# which is what the ci.yml step is for -- run here as well so a `git bisect`
# lands on the commit that broke it rather than on the workflow.
out="$(bash "$GUARD" "$REPO" 2>&1)"; rc=$?
check "this repository has no unwired suite" 0 "$rc"
if [ "$rc" != 0 ]; then echo "$out"; fi

echo
echo "suite-coverage selftest: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
