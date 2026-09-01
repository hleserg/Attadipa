#!/usr/bin/env bash
# Every self-test suite in this repository is run by a workflow, or is named
# below with the reason it is not.
#
# The failure this exists to stop is not a broken suite. It is a suite nobody
# runs: `.github/tests/review-invalidate-workflow-test.sh` sat in the tree from
# #277 with 104 assertions and no `run:` line anywhere, and the directory read
# as coverage the whole time. `shellcheck -x .github/tests/*.sh` at
# `.github/workflows/ci.yml:233` -- "shellcheck -x .github/tests/*.sh" -- is
# what let it hide: a suite nobody runs still parses, and a green lint reads as
# a green test.
#
# #385 wired the shell half as an inline block in ci.yml. #387 found the same
# query returns ten under `tools/`, so the block moved here to be given a
# Python half and, more to the point, a test of its own -- an inline `run:`
# block cannot be run against a tree with a planted suite in it, so the guard
# was the one check in the file that nothing checked.
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

# $1 the suite, $2 the exact text some workflow must contain to be running it.
require_run_by_a_workflow() {
    local suite="$1" needle="$2" reason

    if reason="$(reason_for "$suite")" && [ -n "$reason" ]; then
        echo "note: $suite is deliberately not run -- $reason"
        return 0
    fi

    if grep -qFr -- "$needle" .github/workflows; then
        return 0
    fi

    echo "::error file=$suite,title=Test suite never runs::$suite is in the tree and no workflow runs it. Add a step whose \`run:\` contains \"$needle\", or delete the file, or put it on the allow-list in .github/scripts/check-suite-coverage.sh with a reason -- a suite nobody runs is worse than no suite, because the directory reads as coverage." >&2
    missing=1
}

# The shell half. `bash <path>` is how ci.yml invokes these, so it is what a
# workflow has to say for one to count as run.
for t in .github/tests/*.sh; do
    [ -e "$t" ] || continue
    require_run_by_a_workflow "$t" "bash $t"
done

# The Python half. Two naming conventions grew side by side -- `test_*.py`
# beside the checker it tests, and `selftest*.py` inside the package -- and
# both are in use, so both are matched rather than one being renamed to the
# other. `python3 <path>` is the invocation; a bare mention in a comment is
# deliberately not enough.
while IFS= read -r t; do
    [ -n "$t" ] || continue
    require_run_by_a_workflow "$t" "python3 $t"
done < <(find tools \( -name 'test_*.py' -o -name '*selftest*.py' \) -print | sort)

exit "$missing"
