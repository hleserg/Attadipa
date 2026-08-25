#!/usr/bin/env bash
# Decide whether a reviewer that reached the model published a verdict on this
# PR during this run.  A stale label is not evidence for the current head.
set -uo pipefail

ATTADIPA_REVIEW_MARKER='<!-- attadipa-ai-review -->'
ATTADIPA_REVIEW_AUTHOR='claude[bot]'

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
    --arg since "$started" --arg author "$ATTADIPA_REVIEW_AUTHOR" --arg marker "$ATTADIPA_REVIEW_MARKER" '
      if type != "array" then error("comments are not an array") else . end
      | [ .[] | select(type == "object")
          | select(((.user.login // "") == $author) or (((.body // "") | tostring | contains($marker))))
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
