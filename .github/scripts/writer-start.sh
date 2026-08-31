#!/usr/bin/env bash
# Mandatory local writer entrypoint: global lease, admission, task claim.
#
# `held: full` means the pull-request queue is at its width, not that anything
# is wrong. The width defaults to two and comes from ATTADIPA_WIP_LIMIT. This
# script reads that repository variable itself (see `start` below), so a width
# the owner lifted in repository settings needs nothing done here.
#
# Do NOT export ATTADIPA_WIP_LIMIT to help it along. An explicit value in the
# environment suppresses the lookup, so exporting a number re-creates the exact
# defect the lookup removes -- and it outlives a later `gh variable delete`,
# leaving the shell at a width nobody has set any more. Export one only to pin a
# width deliberately, on a bench.
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
    # THE LOCAL WRITER RUNS AT THE SAME WIDTH AS ACTIONS, or the knob is only
    # half a knob. In a workflow the gate reads ATTADIPA_WIP_LIMIT from `vars`;
    # here there is no `vars` context, so a width the owner lifted in repository
    # settings would be invisible at exactly the entrypoint AGENTS.md makes
    # mandatory -- the gate refusing while the setting says admit, which is the
    # bug the knob exists to remove.
    #
    # An explicit value in the environment wins, so a bench run can still pin
    # one. A lookup that fails, or a variable nobody set, leaves it unset, which
    # wip-limit.sh reads as the designed 2: an unreachable API must not be a way
    # to widen the queue.
    if [ -z "${ATTADIPA_WIP_LIMIT-}" ]; then
      ATTADIPA_WIP_LIMIT="$(gh variable get ATTADIPA_WIP_LIMIT --repo "$repo" 2>/dev/null || true)"
      export ATTADIPA_WIP_LIMIT
    fi
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
