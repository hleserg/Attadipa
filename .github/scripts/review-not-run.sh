#!/usr/bin/env bash
# Invalidate a stale pass before reporting that no review ran on this head.

set -euo pipefail

: "${REPO:?REPO is required}"
: "${PR:?PR is required}"
: "${SHA:?SHA is required}"
DETAIL="${DETAIL:-}"
RUN_URL="${RUN_URL:-}"

# A review that did not run cannot release a blocking verdict, but a pass from
# an older head is never evidence about this one. This must precede every
# fallible notification call.
gh pr edit "$PR" --repo "$REPO" --remove-label ai-review:pass

marker="attadipa-review-did-not-run:${SHA}"
if ! bodies="$(gh api "repos/$REPO/issues/$PR/comments" --paginate --jq '.[].body' 2>/dev/null)"; then
  echo "::warning::the existing comments on #$PR could not be read; the stale pass was removed but no duplicate-prone note was posted"
  exit 0
fi
if printf '%s\n' "$bodies" | grep -Fq "$marker"; then
  echo "::notice::already said so for ${SHA:0:8} on #$PR"
  exit 0
fi

note="$(mktemp)"
trap 'rm -f "$note"' EXIT
{
  echo "<!-- $marker -->"
  echo "**The independent review did not run on \`${SHA:0:8}\`, so there is no verdict.**"
  echo
  echo "This says nothing about the diff."
  if [ -n "$DETAIL" ]; then
    echo
    echo "$DETAIL"
  fi
  echo
  echo "Troubleshooting: [CI and review pipeline](https://github.com/$REPO/blob/main/docs/automation/CI_AND_REVIEW_PIPELINE.md#when-the-review-publishes-no-verdict)."
  echo "Run: $RUN_URL"
  echo
  echo "The stale \`ai-review:pass\` was removed; \`ai-review:blocking\` was left unchanged."
} > "$note"

gh pr comment "$PR" --repo "$REPO" --body-file "$note"
echo "::warning::The independent review could not run on ${SHA:0:8}; no verdict was recorded."
