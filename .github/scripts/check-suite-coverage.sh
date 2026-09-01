#!/usr/bin/env bash
# Every self-test suite in this repository is run by CI, or is named below with
# the reason it is not.
#
# The failure this exists to stop is not a broken suite. It is a suite nobody
# runs: `.github/tests/review-invalidate-workflow-test.sh` sat in the tree from
# #277 with 104 assertions and no `run:` line anywhere, and the directory read
# as coverage the whole time. `shellcheck -x` at
# `.github/workflows/ci.yml:233` -- "run: shellcheck -x .github/scripts/*.sh .github/tests/*.sh"
# -- is what let it hide: a suite nobody runs still parses, and a green lint
# reads as a green test.
#
# #385 wired the shell half as an inline block in ci.yml. #387 found the same
# query returns ten under `tools/`, so the block moved here to be given a
# Python half and, more to the point, a test of its own -- an inline `run:`
# block cannot be run against a tree with a planted suite in it, so the guard
# was the one check in the file that nothing checked.
#
# THERE ARE TWO WAYS TO RUN A SUITE HERE, and the first draft of this guard knew
# only one. A `run:` line in a workflow is one. Registration as a `ctest` test in
# `tests/CMakeLists.txt` is the other, and it is the one this repository
# prefers: `tests/CMakeLists.txt:95` -- "# They are ctest entries rather than
# CI-only steps so that a local run and CI" -- says why. It counts
# because `ctest` is not conditional on anything: `CMakeLists.txt:56` --
# "add_subdirectory(tests)" -- is outside every `if()`, and six jobs run the
# result, the first at `.github/workflows/ci.yml:43` -- "run: ctest --test-dir
# build --output-on-failure".
#
# Seeing only the first needle is not a cosmetic gap. It reports 52 `add_test`
# entries as unrun and tells the author to add a duplicate `run:` line for each,
# which is what #390 round 1 did for nine of them before the review caught it.
#
# Usage: check-suite-coverage.sh [ROOT]   (default: the current directory)
set -uo pipefail

root="${1:-.}"
cd "$root" || {
    echo "check-suite-coverage: no such directory: $root" >&2
    exit 2
}

if [ ! -d .github/workflows ]; then
    # Not a silent pass. A root with no workflows in it is a mistake in the
    # caller, and answering "everything is covered" to it is the exact shape of
    # failure this script is about.
    echo "check-suite-coverage: $root has no .github/workflows, so there is " \
         "nothing that could run a suite" >&2
    exit 2
fi

if [ ! -d .github/tests ]; then
    # And for the shell half. An unmatched glob writes nothing to stderr, so
    # this one failed more quietly than `tools/` ever did: thirty suites walked,
    # zero found, exit 0.
    echo "check-suite-coverage: $root has no .github/tests, so the shell half " \
         "would walk nothing and pass" >&2
    exit 2
fi

if [ ! -d tools ]; then
    # The same refusal for the Python half. It used to be missing, and the hole
    # was invisible because `find`'s status is discarded by a process
    # substitution and `pipefail` does not reach into one: a root with no
    # `tools/` walked zero suites and exited 0, which reads as "all covered".
    echo "check-suite-coverage: $root has no tools/, so the Python half would " \
         "walk nothing and pass" >&2
    exit 2
fi

# `#` to end of line, when the `#` opens a comment rather than sitting inside a
# word. That is YAML's rule and CMake's, and it is why a suite named in a
# sentence about NOT running it does not count as running it.
#
# A `#` inside a quoted string is stripped too, which can only ever hide a
# needle, never invent one -- so the mistake this makes is a loud false "never
# runs", not a silent false pass.
without_comments() {
    sed -e 's/[[:space:]]#.*$//' -e 's/^#.*$//' "$@"
}

# The workflow half of the corpus: only files GitHub would actually trigger. A
# workflow with no top-level `on:` key is a file, not a workflow, and counting
# it would let a suite be "run" by something that never starts. (`on` is YAML
# 1.1 truthy, so it is also written `"on"`, `'on'` and -- once parsed -- `true`.)
triggered_workflow_text() {
    local w
    for w in .github/workflows/*.yml .github/workflows/*.yaml; do
        [ -e "$w" ] || continue
        if grep -qE '^(on|"on"|'\''on'\''|true)[[:space:]]*:' "$w"; then
            without_comments "$w"
        fi
    done
}

# Both corpora are stripped ONCE, into variables, and every needle is then
# matched against a string rather than a pipeline. That is not an optimisation.
# `... | grep -q` under `set -o pipefail` is a coin toss: `grep -q` exits at the
# first match, `sed` takes SIGPIPE, and `pipefail` reports 141 for a pipeline
# that succeeded. Whether it does depends on how far into the file the match is,
# so the first draft of this guard passed every suite registered near the bottom
# of `tests/CMakeLists.txt` and failed the identical ones near the top.
# THE CTEST HALF IS A REGISTRATION, NOT A MENTION. The workflow half demands the
# literal invocation and then asks whether the file could ever run; asking less
# of `tests/CMakeLists.txt` would let a path in a `configure_file`, a
# `message()` or a `set()` count as a suite being run. So only the text inside
# `add_test(...)` reaches the needle -- the command whose argument list is the
# thing ctest executes.
#
# Nesting is counted rather than assumed, because the path and the `add_test(`
# are usually on different lines. A `(` inside a quoted argument would end the
# body early, which can only ever hide a registration -- a loud false "never
# runs", never a silent pass, which is the same direction `without_comments`
# fails in.
#
# What this still does not ask is whether the registration is *reachable*: an
# `add_test` inside an `if()` with no `else()` runs in some jobs and not others.
# Answering that needs `ctest --show-only=json-v1` against a configured build
# tree, which this script does not have and a lint job will not build. The
# convention that holds it up meanwhile is visible at `tests/CMakeLists.txt:247`
# -- "add_test(NAME l10n_checks_unavailable" -- a gate that cannot register the
# real test registers a failing one instead of registering nothing.
add_test_text() {
    awk '
        {
            s = $0
            while (1) {
                if (depth == 0) {
                    if (match(s, /add_test[ \t]*\(/) == 0) break
                    s = substr(s, RSTART + RLENGTH)
                    depth = 1
                }
                out = ""
                i = 1
                n = length(s)
                while (i <= n && depth > 0) {
                    c = substr(s, i, 1)
                    if (c == "(") depth++
                    else if (c == ")") depth--
                    if (depth > 0) out = out c
                    i++
                }
                if (out != "") print out
                if (depth > 0) break
                s = substr(s, i)
            }
        }
    '
}

workflow_text="$(triggered_workflow_text)"
ctest_text=""
[ -f tests/CMakeLists.txt ] &&
    ctest_text="$(without_comments tests/CMakeLists.txt | add_test_text)"

# A suite that is deliberately not run, and why. Keep this list short and keep
# the reason honest: "it is slow" is a reason to move it to another job, not to
# stop running it. Empty is the goal.
#
# Format: one `PATH|REASON` line. The reason is printed on every run, so an
# entry that has outlived its reason is visible rather than inert.
allow_list() {
    cat <<'ALLOWED'
ALLOWED
}

reason_for() {
    local path reason
    # Process substitution rather than a pipe: a `while` on the right of a pipe
    # runs in a subshell, so its `return 0` returns from the subshell and the
    # function's status is whatever the last `read` did -- 1, always, match or
    # not. The first draft of this file had that bug and the allow-list was
    # inert; the case for it in the test suite is what found it.
    while IFS='|' read -r path reason; do
        [ -n "$path" ] || continue
        if [ "$path" = "$1" ]; then
            printf '%s' "$reason"
            return 0
        fi
    done < <(allow_list)
    return 1
}

missing=0

# $1 the suite, $2 the exact text a triggered workflow must contain to be
# running it. Registration in `tests/CMakeLists.txt` counts instead.
require_run_by_ci() {
    local suite="$1" needle="$2" reason

    if reason="$(reason_for "$suite")" && [ -n "$reason" ]; then
        echo "note: $suite is deliberately not run -- $reason"
        return 0
    fi

    case "$workflow_text" in *"$needle"*) return 0 ;; esac
    case "$ctest_text" in *"$suite"*) return 0 ;; esac

    echo "::error file=$suite,title=Test suite never runs::$suite is in the tree and nothing in CI runs it. Register it in tests/CMakeLists.txt with add_test -- which is what this repository prefers, because a local ctest run then enforces the same rule -- or add a workflow step whose \`run:\` contains \"$needle\", or delete the file, or put it on the allow-list in .github/scripts/check-suite-coverage.sh with a reason. A suite nobody runs is worse than no suite, because the directory reads as coverage." >&2
    missing=1
}

# The shell half. `bash <path>` is how ci.yml invokes these, so it is what a
# workflow has to say for one to count as run.
for t in .github/tests/*.sh; do
    [ -e "$t" ] || continue
    require_run_by_ci "$t" "bash $t"
done

# The Python half. THREE naming conventions grew side by side -- `test_*.py`
# beside the checker it tests, `*_test.py`, and `selftest*.py` inside the
# package -- and all three are in use, so all three are matched rather than two
# being renamed to the third. The guard used to claim there were two and match
# two, so `tools/watch/e2e_test.py` was invisible to it.
#
# `python3 <path>` is the invocation; a bare mention in a comment is
# deliberately not enough, which is what `without_comments` is for.
while IFS= read -r t; do
    [ -n "$t" ] || continue
    require_run_by_ci "$t" "python3 $t"
done < <(find tools \( -name 'test_*.py' -o -name '*_test.py' \
                       -o -name '*selftest*.py' \) -print | sort)

exit "$missing"
