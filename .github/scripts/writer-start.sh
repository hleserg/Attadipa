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
: "${holder:?agent id required: an opaque label such as local-<who>-<n>, never a credential -- it is published in a public tag}"

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
      # NOT `2>/dev/null`, for the reason wip-limit.sh:143 gives about the same
      # CLI: the refusal is the only evidence of what went wrong. Two answers
      # arrive here as the same empty string. "Nobody set a width" is correct
      # and correctly silent; "this token may not read it" is not -- reading
      # Actions variables is its own permission, which a fine-grained token or
      # App installation must be granted explicitly and which none of the
      # contents, issues or pull-requests scopes carrying every other call on
      # this path imply. Swallowed, it leaves the writer refusing at the
      # designed width while Actions admits at the lifted one, with nothing to
      # tell that apart from a queue that is genuinely full.
      if ! lookup="$(gh variable get ATTADIPA_WIP_LIMIT --repo "$repo" 2>&1)"; then
        case "$lookup" in
          *'was not found'*) ;;
          *) printf 'writer-start: could not read ATTADIPA_WIP_LIMIT: %s\n' \
               "$(printf '%s' "$lookup" | head -1)" >&2 ;;
        esac
        lookup=
      fi
      ATTADIPA_WIP_LIMIT="$lookup"
      export ATTADIPA_WIP_LIMIT
    fi
    bash "$dir/claim.sh" acquire "$repo" writer "$holder" || exit $?
    output="$(mktemp)" || { bash "$dir/claim.sh" release "$repo" writer "$holder" || true; exit 2; }
    trap 'rm -f "$output"' EXIT
    GITHUB_OUTPUT="$output" bash "$dir/writer-admission.sh" "$repo" "$number"
    if [ "$(sed -n 's/^allow=//p' "$output")" != true ]; then
      bash "$dir/claim.sh" release "$repo" writer "$holder" || true
      # `held: full` alone cannot be compared with what Actions decided, which
      # is the comparison RECOVERY.md promises the operator. Name the width in
      # force, resolved by the one function that defines it rather than a
      # second copy of the rule.
      # shellcheck source=.github/scripts/wip-limit.sh
      ( . "$dir/wip-limit.sh"
        printf 'held: %s (width %s)\n' \
          "$(sed -n 's/^state=//p' "$output")" "$(attadipa_wip_limit)" ) >&2
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
