#!/usr/bin/env bash
# A review that reached the model and published no verdict invalidates the
# previous head's verdict labels, and SAYS SO. Two jobs, and the order between
# them is the whole of this file.
#
# THE DEFECT (#240). The step that does this lived inside
# `claude-pr-review.yml` and read, in order, under `set -euo pipefail`:
#
#   1. `gh api .../comments --paginate --jq '.[].body' | grep -Fq "$marker"`
#   2. `gh pr comment` when that marker was absent
#   3. `gh pr edit --remove-label ai-review:pass  || true`
#   4. `gh pr edit --remove-label ai-review:blocking || true`
#
# Steps 1 and 2 are network calls to an API that rate-limits, times out and
# returns 502s. Under `set -e` either one returning non-zero ends the step at
# that line -- so the invalidation, the only part of this that protects a
# merge, was skipped in exactly the circumstance the guard was added for: a new
# head that nothing reviewed, carrying the previous head's `ai-review:pass`.
# The `|| true` on lines 3 and 4 made it worse in the other direction: a
# removal that genuinely failed reported success.
#
# So the order is inverted here and the failure handling with it:
#
#   - INVALIDATION FIRST, and its failure is this script's failure. Telling
#     somebody a verdict is void while leaving the label that asserts it is the
#     one outcome that must never be reported green.
#   - Both labels are attempted even when the first fails. Stopping at the
#     first leaves the second stale for no gain.
#   - NOTIFICATION SECOND, and its failure is a `::warning::`. A pull request
#     that lost its stale `ai-review:pass` and did not get a note is a pull
#     request nothing can wrongly merge. The reverse is not true.
#
# `gh pr edit --remove-label` on a label the pull request does not carry is a
# no-op that exits 0 -- `claude-pr-review.yml`'s convergence step has depended
# on that since it was written, where `--remove-label "$other"` runs under
# `set -e` with the other label absent nearly every time. So a non-zero exit
# here is an API failure rather than an absent label, which is what makes it
# safe to treat as one.
#
# Every value arrives through the environment. .github/tests/review-published-test.sh
# runs this against a stub `gh` on PATH, including the mutant with the two
# halves swapped back.

set -uo pipefail

: "${REPO:?REPO is required}"
: "${PR:?PR is required}"
: "${SHA:?SHA is required}"
RUN_URL="${RUN_URL:-}"

# --- 1. Invalidate. ---------------------------------------------------------
#
# The rest of this file may fail. This may not.
invalidated=0
for label in ai-review:pass ai-review:blocking; do
  if ! gh pr edit "$PR" --repo "$REPO" --remove-label "$label"; then
    invalidated=1
    echo "::error::could not remove the stale \`$label\` from #$PR; a verdict from an older head is still on this pull request and has to come off by hand"
  fi
done

# --- 2. Say so, once per head commit. ---------------------------------------
#
# The marker carries the SHA so that a re-run on the same head does not post
# twice and a push does not inherit the previous head's note.
marker="attadipa-review-not-published:${SHA}"
seen=unknown
if bodies="$(gh api "repos/$REPO/issues/$PR/comments" --paginate --jq '.[].body' 2>/dev/null)"; then
  if printf '%s\n' "$bodies" | grep -Fq "$marker"; then seen=yes; else seen=no; fi
fi

if [ "$seen" = yes ]; then
  echo "::notice::already said so for ${SHA:0:8} on #$PR"
elif [ "$seen" = unknown ]; then
  # The dedupe read failed. Posting anyway risks a duplicate note; not posting
  # risks silence. Silence is the cheaper mistake, because the labels are
  # already off and the run log carries this line.
  echo "::warning::the existing comments on #$PR could not be read, so the note for ${SHA:0:8} was not posted; the stale verdict labels were removed regardless"
else
  note="$(mktemp)"
  {
    echo "<!-- $marker -->"
    echo "**The independent review ran on \`${SHA:0:8}\` but published no verdict.**"
    echo
    echo "No \`ai-review:pass\` from an older head is valid for this commit."
    [ "$invalidated" -eq 0 ] || echo "It could not be removed automatically — take it off by hand before trusting anything on this pull request."
    echo "Run: $RUN_URL"
  } > "$note"
  if ! gh pr comment "$PR" --repo "$REPO" --body-file "$note"; then
    echo "::warning::the note for ${SHA:0:8} could not be posted on #$PR; the stale verdict labels were removed regardless"
  fi
  rm -f "$note"
fi

# --- 3. The step's result is the invalidation's, and nothing else's. ---------
exit "$invalidated"
