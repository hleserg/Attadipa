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
  local ran="${1-}" outcome="${2-}" comments="${3-}" labels="${4-}" started="${5-}"
  if [ "$ran" != yes ] || [ "$outcome" = failure ]; then
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

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  attadipa_review_published "$@"
fi
