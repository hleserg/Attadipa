#!/usr/bin/env bash
# Count active repository PRs for the owner WIP limit: two normal, three hard.
set -uo pipefail

attadipa_wip_decide() {
  local payload="${1-}" count exempt
  count="$(printf '%s' "$payload" | jq -r '
    if type != "array" then error("not an array") else . end
    | map(select(.head.repo.full_name == .base.repo.full_name)
          | select((.labels | map(.name) | index("queue:parked") or index("queue:emergency")) | not))
    | length' 2>/dev/null || true)"
  case "$count" in
    ''|*[!0-9]*) printf 'unknown unknown\n' ;;
    0|1|2) printf 'ok %s\n' "$count" ;;
    3) printf 'full %s\n' "$count" ;;
    *) printf 'incident %s\n' "$count" ;;
  esac
}

if [ "${1-}" = --say ]; then
  case "${2-}" in
    full) echo "WIP limit reached: ${3-unknown} active pull requests (normal limit: 2). Finish or explicitly park work before opening another." ;;
    incident) echo "QUEUE INCIDENT: ${3-unknown} active pull requests exceed the hard maximum of 3. Drain the queue; do not open more work." ;;
    *) echo 'Could not determine the active pull-request count; do not assume capacity.' ;;
  esac
  exit 0
fi

if [ "${BASH_SOURCE[0]}" != "$0" ]; then
  return 0
fi

payload="$(gh pr list --repo "${GITHUB_REPOSITORY:?}" --state open --limit 100 --json number,headRefName,headRepository,baseRepository,labels 2>/dev/null || true)"
# Normalize gh's headRepository/baseRepository fields into the shape used by the
# pure rule; API failure remains unknown rather than becoming a zero count.
payload="$(printf '%s' "$payload" | jq '[.[] | {head:{repo:{full_name:(.headRepository.nameWithOwner // "")}},base:{repo:{full_name:(.baseRepository.nameWithOwner // "")}},labels}]' 2>/dev/null || true)"
read -r state count < <(attadipa_wip_decide "$payload")
echo "state=$state" >> "$GITHUB_OUTPUT"
echo "count=$count" >> "$GITHUB_OUTPUT"
echo "Active pull requests: $count ($state)"
