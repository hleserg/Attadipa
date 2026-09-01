#!/usr/bin/env bash
# Did the build resolve the dependency graph this repository holds?
#
# `firmware/dependencies.lock` is tracked because the licence audit in
# `docs/research/DEPENDENCIES.md` describes exact versions, and two of the seven
# entries are pulled in transitively at `^1.2.0` and `0.*` -- free to move
# between clean builds with nothing in the tree recording which ones were
# audited. Committing the lock records them. This is what stops the record from
# becoming decoration: CI copies the committed lock aside, builds, and compares.
#
# It is a script rather than twenty lines in a `run:` block for the reason
# `.github/scripts/check-suite-coverage.sh:15` gives about the guard that came
# before it -- "an inline `run:` block cannot be run against a tree with a
# planted suite in it, so the guard was the one check in the file that nothing
# checked". A drift check whose own drift nobody can plant is the same shape.
#
# Usage: lock-drift-check.sh COMMITTED RESOLVED
#
#   0  the two files are byte-identical: the build resolved what is committed
#   1  they differ -- the drift, printed as a diff and as a workflow ::error
#   2  a file is missing or unreadable, which is a broken invocation and not a
#      clean graph. Silence on a missing file is how a gate stops gating.
set -uo pipefail

committed="${1:-}"
resolved="${2:-}"

if [ -z "$committed" ] || [ -z "$resolved" ]; then
    echo "lock-drift-check: usage: lock-drift-check.sh COMMITTED RESOLVED" >&2
    exit 2
fi

for f in "$committed" "$resolved"; do
    if [ ! -r "$f" ]; then
        echo "lock-drift-check: cannot read $f" >&2
        exit 2
    fi
done

if diff -u "$committed" "$resolved"; then
    exit 0
fi

# `::error` is what turns a red step into an annotation on the file, and the
# text is the whole instruction: the fix is never "ignore the diff".
echo "::error title=Resolved dependency graph moved::firmware/dependencies.lock changed during the build. Commit the new lock and re-run the licence audit in docs/research/DEPENDENCIES.md before merging." >&2
exit 1
