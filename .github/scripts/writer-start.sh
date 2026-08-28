#!/usr/bin/env bash
# Mandatory local writer entrypoint: global lease, admission, task claim.
set -uo pipefail

dir="$(cd "$(dirname "$0")" && pwd)" || exit 1
op="${1-}"
repo="${2-}"
number="${3-}"
holder="${4-}"
: "${repo:?repository required}"
: "${number:?issue or pull-request number required}"
: "${holder:?agent id required: an opaque label such as agent-<run>-<attempt>, never a credential -- it is published in a public tag}"

case "$op" in
  start)
    bash "$dir/claim.sh" acquire "$repo" writer "$holder" || exit $?
    output="$(mktemp)" || { bash "$dir/claim.sh" release "$repo" writer "$holder" || true; exit 2; }
    trap 'rm -f "$output"' EXIT
    GITHUB_OUTPUT="$output" bash "$dir/writer-admission.sh" "$repo" "$number"
    if [ "$(sed -n 's/^allow=//p' "$output")" != true ]; then
      bash "$dir/claim.sh" release "$repo" writer "$holder" || true
      echo "held: $(sed -n 's/^state=//p' "$output")" >&2
      exit 3
    fi
    set +e
    bash "$dir/claim.sh" acquire "$repo" "$number" "$holder"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
      bash "$dir/claim.sh" release "$repo" writer "$holder" || true
      exit "$rc"
    fi
    printf 'writer started: %s#%s (%s)\n' "$repo" "$number" "$holder"
    ;;
  finish)
    rc=0
    bash "$dir/claim.sh" release "$repo" "$number" "$holder" || rc=$?
    bash "$dir/claim.sh" release "$repo" writer "$holder" || rc=$?
    exit "$rc"
    ;;
  *) echo 'usage: writer-start.sh {start|finish} REPO NUMBER AGENT_ID' >&2; exit 64 ;;
esac
