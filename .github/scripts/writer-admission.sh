#!/usr/bin/env bash
# Fail-closed admission check that runs before any writer claims a task.
set -uo pipefail

repo="${1-}"
number="${2-}"
: "${repo:?repository required}"
: "${number:?issue or pull-request number required}"
: "${GITHUB_OUTPUT:?}"

dir="$(cd "$(dirname "$0")" && pwd)" || exit 1

emit() {
  printf 'allow=%s\nstate=%s\ncount=%s\nreason=%s\n' "$1" "$2" "$3" "$4" >> "$GITHUB_OUTPUT"
}

target=""
if ! target="$(gh api "repos/$repo/issues/$number" 2>&1)"; then
  printf '::error title=Writer admission unavailable::Could not read %s#%s: %s. Failing closed.\n' \
    "$repo" "$number" "$(printf '%s' "$target" | head -1)" >&2
  emit false unknown unknown target-unreadable
  exit 0
fi

if ! target_state="$(printf '%s' "$target" | jq -er '
  if (.state | type) != "string" or (.labels | type) != "array" then error("bad target schema") else .state end' 2>/dev/null)"; then
  printf '::error title=Writer admission schema error::Target %s#%s returned malformed JSON. Failing closed.\n' "$repo" "$number" >&2
  emit false unknown unknown target-malformed
  exit 0
fi

if [ "$target_state" != open ]; then
  emit false closed unknown target-closed
  exit 0
fi

labels="$(printf '%s' "$target" | jq -r '[.labels[].name] | join("\n")')"
if printf '%s\n' "$labels" | grep -Fxq queue:parked; then
  emit false parked unknown target-parked
  exit 0
fi

if printf '%s\n' "$labels" | grep -Fxq queue:emergency; then
  emit true emergency unknown emergency-recovery
  exit 0
fi

if printf '%s' "$target" | jq -e '.pull_request != null' >/dev/null 2>&1; then
  emit true recovery unknown existing-pr
  exit 0
fi

wip_output="$(mktemp)" || {
  emit false unknown unknown temporary-file-failed
  exit 0
}
trap 'rm -f "$wip_output"' EXIT
GITHUB_REPOSITORY="$repo" GITHUB_OUTPUT="$wip_output" bash "$dir/wip-limit.sh" --admit
state="$(sed -n 's/^state=//p' "$wip_output")"
count="$(sed -n 's/^count=//p' "$wip_output")"
case "$state" in
  ok) emit true ok "$count" capacity-available ;;
  full|incident) emit false "$state" "$count" queue-closed ;;
  *) emit false unknown "${count:-unknown}" queue-unreadable ;;
esac
