#!/usr/bin/env bash
# Mandatory local writer entrypoint: global lease, admission, task claim.
set -uo pipefail

dir="$(cd "$(dirname "$0")" && pwd)" || exit 1
op="${1-}"
repo="${2-}"
number="${3-}"
token="${4-}"
: "${repo:?repository required}"
: "${number:?issue or pull-request number required}"
: "${token:?unique run token required}"

case "$op" in
  start)
    bash "$dir/claim.sh" acquire "$repo" writer "$token" || exit $?
    output="$(mktemp)" || { bash "$dir/claim.sh" release "$repo" writer "$token" || true; exit 2; }
    trap 'rm -f "$output"' EXIT
    GITHUB_OUTPUT="$output" bash "$dir/writer-admission.sh" "$repo" "$number"
    if [ "$(sed -n 's/^allow=//p' "$output")" != true ]; then
      bash "$dir/claim.sh" release "$repo" writer "$token" || true
      echo "held: $(sed -n 's/^state=//p' "$output")" >&2
      exit 3
    fi
    set +e
    bash "$dir/claim.sh" acquire "$repo" "$number" "$token"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
      bash "$dir/claim.sh" release "$repo" writer "$token" || true
      exit "$rc"
    fi
    printf 'writer started: %s#%s (%s)\n' "$repo" "$number" "$token"
    ;;
  finish)
    rc=0
    bash "$dir/claim.sh" release "$repo" "$number" "$token" || rc=$?
    bash "$dir/claim.sh" release "$repo" writer "$token" || rc=$?
    exit "$rc"
    ;;
  *) echo 'usage: writer-start.sh {start|finish} REPO NUMBER TOKEN' >&2; exit 64 ;;
esac
