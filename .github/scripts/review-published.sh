#!/usr/bin/env bash
# Decide whether a reviewer that reached the model published a verdict on this
# PR during this run.  A stale label is not evidence for the current head.
set -uo pipefail

ATTADIPA_REVIEW_MARKER='<!-- attadipa-ai-review -->'
ATTADIPA_REVIEW_AUTHOR='claude[bot]'
# The marker admits a SECOND account, and only a second account.
#
# It exists so a repository that sets ATTADIPA_AGENT_TOKEN — publishing under
# its own app rather than as `claude[bot]` — can still be recognised. It used to
# admit ANY author: the clause was `login == $author OR body contains $marker`,
# with nothing on the second half. This repository is public, so any account
# could post a comment containing the marker and turn a silent review into a
# published one — and `published` is what skips the step that strips the
# previous head's `ai-review:pass`. A stale verdict then survived a head change
# that nothing had reviewed. The marker still identifies the alternate
# publisher; it no longer identifies everybody.
ATTADIPA_REVIEW_MARKER_AUTHORS='["attadipa-agent[bot]"]'

attadipa_review_published() {
  local ran="${1-}" comments="${3-}" labels="${4-}" started="${5-}"
  if [ "$ran" != yes ]; then
    echo 'not-run the model was not reached'
    return 0
  fi
  case "$started" in
    [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z) ;;
    *) echo 'unknown the run start time could not be read'; return 0 ;;
  esac

  local fresh
  fresh="$(printf '%s' "$comments" | jq -r \
    --arg since "$started" --arg author "$ATTADIPA_REVIEW_AUTHOR" --arg marker "$ATTADIPA_REVIEW_MARKER" \
    --argjson marker_authors "$ATTADIPA_REVIEW_MARKER_AUTHORS" '
      if type != "array" then error("comments are not an array") else . end
      | [ .[] | select(type == "object")
          | select((.user.login // "") as $login
                   | ($login == $author)
                     or ((($marker_authors | index($login)) != null)
                         and (((.body // "") | tostring | contains($marker)))))
          | select(((.created_at // "") >= $since) or ((.updated_at // "") >= $since)) ]
      | length' 2>/dev/null || true)"
  case "$fresh" in
    ''|*[!0-9]*) echo 'unknown pull-request comments could not be read'; return 0 ;;
  esac
  if [ "$fresh" -gt 0 ]; then
    echo "published $fresh reviewer comment(s) written or edited during this run"
    return 0
  fi
  if printf '%s\n' "$labels" | grep -qxE 'ai-review:(pass|blocking)'; then
    echo 'silent a stale review label exists without a current review comment'
  else
    echo 'silent the model published neither a review comment nor a verdict label'
  fi
}

# The publication step of claude-pr-review.yml: read this PR's comments and
# labels, decide, write `state=` to $GITHUB_OUTPUT, and be red on `unknown`.
# Reads REPO, PR, STARTED, OUTCOME and GITHUB_OUTPUT from the environment.
attadipa_review_publication_step() {
  local comments="" labels answer state attempt
  # The read is retried, and its failure stays distinguishable from an empty
  # list (#391). It used to sit behind two `2>/dev/null` and a `|| true` in the
  # workflow, so a rate limit or a 502 arrived here as "", the helper answered
  # `unknown`, and no step matches `unknown`: the job ended green having
  # converged nothing (#382, four rounds).
  for attempt in 1 2 3; do
    comments="$(gh api "repos/$REPO/issues/$PR/comments?per_page=100" --paginate --slurp | jq -c 'flatten(1)')" && break
    comments=""
    echo "::warning::reading the comments of #$PR failed (attempt $attempt of 3)"
    [ "$attempt" -lt 3 ] && sleep 5
  done
  labels="$(gh pr view "$PR" --repo "$REPO" --json labels --jq '.labels[].name' 2>/dev/null || true)"
  answer="$(attadipa_review_published yes "$OUTCOME" "$comments" "$labels" "$STARTED")"
  case "$answer" in
    published\ *) state=published ;;
    silent\ *) state=silent ;;
    *) state=unknown ;;
  esac
  echo "state=$state" >> "$GITHUB_OUTPUT"
  if [ "$state" = unknown ]; then
    # Neither `published` nor `silent`, and no step of the workflow runs for
    # it, so this step carries the failure itself. Widening the two
    # did-not-happen conditions instead would also catch their mirror case, a
    # run that declined to review on purpose (#391).
    echo "::error::the review reached the model but its verdict could not be read back (${answer#unknown }); nothing was converged, and the label on #$PR is the reviewer's own rather than the convergence rule's. Re-run this check (#391)"
    return 1
  fi
  if [ "$state" = published ] && [ "$OUTCOME" = failure ]; then
    echo "::warning::The review action reported failure after publishing its verdict; the published verdict remains authoritative."
  fi
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  case "${1-}" in
    step) attadipa_review_publication_step ;;
    *) attadipa_review_published "$@" ;;
  esac
fi
