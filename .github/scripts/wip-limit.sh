#!/usr/bin/env bash
# Count active repository PRs for the owner WIP limit: N normal, N+1 hard,
# where N is `ATTADIPA_WIP_LIMIT` and defaults to two.
#
# TWO VOCABULARIES MEET IN THIS FILE, and confusing them is what broke it. The
# decision rule below reads the REST shape -- `.head.repo.full_name` against
# `.base.repo.full_name` -- because that is what `repos/{owner}/{repo}/pulls`
# returns and what the tests were written against. `gh pr list --json` does NOT
# speak that vocabulary: it has `headRepository`, `headRepositoryOwner` and
# `isCrossRepository`, and it has no `baseRepository` at all. Asking for one is
# not an empty column, it is `Unknown JSON field: "baseRepository"` and exit 1
# before any request is made -- the same class of refusal as the `--slurp`/`--jq`
# pair `.github/tests/gh-api-usage-test.sh` exists for.
#
# That shipped. From #216 until #239 every live run of this script asked for
# `baseRepository`, `gh` refused, `2>/dev/null || true` swallowed the refusal,
# the empty payload normalised to nothing, and the workflow reported
# `Could not determine the active pull-request count` -- on #219, #236 and #237,
# reproducibly. `full` and `incident` were unreachable states, so the WIP limit
# existed only on paper while its own test suite ran 6/6 green: the test called
# the pure rule with hand-built JSON and never executed the transport. Codex had
# already named the exact field in review of #216 and the merged implementation
# did not carry the correction.
#
# So the transport is now covered by an executable caller test with a stub `gh`
# that refuses an unknown `--json` field exactly as `gh` does, and the field list
# is checked statically across every workflow, script and parked patch. The
# normaliser below is the ONE seam where a CLI name becomes a REST name; keep the
# translation here rather than teaching the rule a second shape.
set -uo pipefail

# THE ONE DEFINITION of "a pull request that spends repository capacity": raised
# to a variable because the decision and the operator-facing log line below both
# need it, and two copies of a filter is two places for the exemption labels to
# drift apart. A log naming pull requests the count did not count is worse than
# no log at all.
attadipa_wip_active_jq='
  map(select(.head.repo.full_name == .base.repo.full_name)
      | select((.labels | map(.name) | index("queue:parked") or index("queue:emergency")) | not))'

# THE WIDTH OF THE QUEUE, and the one place it is decided. It used to be the
# literals `0|1`, `2` and `*` in the case below, which meant the owner could not
# lift the limit without editing this file and merging a pull request -- through
# the gate being lifted, while it was refusing. The width is now a repository
# variable, so raising it and putting it back are both settings, not commits.
#
# ANYTHING THAT IS NOT A ONE- OR TWO-DIGIT NUMBER FALLS BACK TO 2. Unset, empty,
# misspelt, whitespace, `unlimited`, or twenty digits that would overflow the
# arithmetic below: all of them are 2. This file's entire history is about
# failing closed, and a variable nobody set must not be how the queue becomes
# unbounded. `10#` is not decoration -- without it `08` is an octal literal and
# `$((08))` is a fatal error, not a limit.
#
# Zero is honoured rather than rejected: it means "admit nothing", which is a
# deliberate freeze and is closed, not open.
attadipa_wip_limit() {
  case "${ATTADIPA_WIP_LIMIT-}" in
    ''|*[!0-9]*) printf '2\n' ;;
    ?|??) printf '%s\n' "$((10#${ATTADIPA_WIP_LIMIT}))" ;;
    *) printf '2\n' ;;
  esac
}

attadipa_wip_decide() {
  local payload="${1-}" count limit
  count="$(printf '%s' "$payload" | jq -r '
    if type != "array" then error("not an array") else . end
    | '"$attadipa_wip_active_jq"'
    | length' 2>/dev/null || true)"
  case "$count" in
    ''|*[!0-9]*) printf 'unknown unknown\n' ; return 0 ;;
  esac
  limit="$(attadipa_wip_limit)"
  if [ "$count" -lt "$limit" ]; then
    printf 'ok %s\n' "$count"
  elif [ "$count" -eq "$limit" ]; then
    printf 'full %s\n' "$count"
  else
    printf 'incident %s\n' "$count"
  fi
}

# The exact `--json` field list this script asks `gh pr list` for, in one place
# so the caller test can assert on the list itself rather than on a substring of
# a command line. Every name here is one the decision needs: `isCrossRepository`
# decides whether a pull request spends repository capacity, `labels` carries the
# two exemptions, and `number` is for the operator reading the log. `headRefName`
# and `headRepository` were requested before and read by nothing.
ATTADIPA_WIP_JSON_FIELDS='number,isCrossRepository,labels'

# Did `gh` refuse the command itself, rather than fail to reach GitHub? The two
# are different problems: a network or auth failure is transient and the right
# answer is to fail closed and try again on the next pull request, while an
# unsupported field is a defect in this file that will refuse every run until
# somebody edits it. Reporting them alike is what hid this for three of them.
attadipa_wip_is_schema_error() {
  case "${1-}" in
    *"Unknown JSON field"*|*"Specify one or more comma-separated fields"*) return 0 ;;
    *) return 1 ;;
  esac
}

if [ "${1-}" = --say ]; then
  # The limit IN FORCE, not the designed one. An operator reading "normal limit:
  # 2" under a lifted limit would go looking for a bug in the count.
  say_limit="$(attadipa_wip_limit)"
  # A FROZEN QUEUE IS NOT AN OVERFLOWING ONE, and they arrive here as the same
  # state. Width 0 means "admit nothing", so `full` holds only while the queue is
  # empty and one ordinary pull request is already `incident` -- the word this
  # repository reserves for the queue having overflowed, and the word RECOVERY.md
  # answers with drain/recovery mode. Sending an operator to drain a queue the
  # owner closed on purpose is the same wrong diagnosis as "normal limit: 2"
  # under a lifted limit, in the other direction.
  case "${2-}" in
    full|incident)
      if [ "$say_limit" -eq 0 ]; then
        echo "QUEUE FROZEN: ATTADIPA_WIP_LIMIT is 0, so no new writer is admitted and the ${3-unknown} open pull requests are not an overflow. This is a deliberate freeze; it is lifted by setting or deleting the variable, not by draining."
        exit 0
      fi ;;
  esac
  case "${2-}" in
    full) echo "WIP limit reached: ${3-unknown} active pull requests (normal limit: $say_limit). Finish or explicitly park work before opening another." ;;
    incident) echo "QUEUE INCIDENT: ${3-unknown} active pull requests reached the hard threshold of $((say_limit + 1)). Drain the queue; do not open more work." ;;
    *) echo 'Could not determine the active pull-request count; do not assume capacity.' ;;
  esac
  exit 0
fi

if [ "${BASH_SOURCE[0]}" != "$0" ]; then
  return 0
fi

: "${GITHUB_REPOSITORY:?}"
mode="${1-}"
GITHUB_OUTPUT="${GITHUB_OUTPUT:-/dev/null}"

stderr_file="$(mktemp)" || exit 1
trap 'rm -f "$stderr_file"' EXIT

payload=""
if raw="$(gh pr list --repo "$GITHUB_REPOSITORY" --state open --limit 100 \
            --json "$ATTADIPA_WIP_JSON_FIELDS" 2>"$stderr_file")"; then
  # gh's shape into the REST shape the rule reads. The base repository is not
  # asked for at all: it is `GITHUB_REPOSITORY`, which Actions sets from the
  # event and which no pull request can influence. A cross-repository head gets
  # `null`, which cannot equal a non-empty `$base` -- so a fork never consumes
  # repository capacity, while a `null` or absent `isCrossRepository` counts,
  # which is the conservative direction: over-counting narrows the queue, and
  # under-counting is the failure this whole file is about.
  payload="$(printf '%s' "$raw" | jq --arg base "$GITHUB_REPOSITORY" '
    [.[] | {
      number,
      head: {repo: {full_name: (if .isCrossRepository then null else $base end)}},
      base: {repo: {full_name: $base}},
      labels
    }]' 2>/dev/null || true)"
else
  gh_error="$(cat "$stderr_file")"
  # NOT `2>/dev/null`. The refusal is the only evidence of what went wrong, and
  # it was discarded for three pull requests while the workflow printed a
  # diagnostic that named none of it. All of it goes to the log.
  printf '%s\n' "$gh_error" >&2
  # THE ANNOTATION GETS THE FIRST LINE ONLY, and that is not brevity. An
  # `::error::` is terminated by a newline, so a multi-line message leaves
  # everything after the first line sitting in the log as loose text and the
  # annotation truncated mid-sentence. `gh`'s refusal here is about fifty lines:
  # `Unknown JSON field: "x"` and then every field it does have. The first line
  # is the one that names the problem, and the rest is two lines above.
  gh_first="$(printf '%s\n' "$gh_error" | head -1)"
  if attadipa_wip_is_schema_error "$gh_error"; then
    # A hard diagnostic, and deliberately NOT a non-zero exit. Failing the step
    # reds an unrelated pull request's checks AND skips the reporting step that
    # follows it, so the queue would lose its comment as well as its count --
    # the same silence, one layer up. `::error::` is loud in the run and in the
    # annotations; the executable caller test is what actually stops a bad field
    # list reaching main.
    printf '::error title=The WIP limit is broken, not merely unreadable::gh refused the command: %s. This is a defect in .github/scripts/wip-limit.sh and not a transient API failure -- every run will fail the same way until the field list is fixed. Fields requested: %s\n' \
      "$gh_first" "$ATTADIPA_WIP_JSON_FIELDS"
  else
    printf '::warning title=Could not read the pull-request queue::gh could not list pull requests: %s. Failing closed to unknown.\n' \
      "$gh_first"
  fi
fi

read -r state count < <(attadipa_wip_decide "$payload")
echo "state=$state" >> "$GITHUB_OUTPUT"
echo "count=$count" >> "$GITHUB_OUTPUT"
# The numbers, not just the total. This line is how an operator confirms from a
# live run that the count is a real one -- the single thing the previous version
# could never show, and the reason its silence read as a working guard.
counted="$(printf '%s' "$payload" | jq -r "$attadipa_wip_active_jq"'
  | map("#" + (.number | tostring)) | join(" ")' 2>/dev/null || true)"
if [ -n "$counted" ]; then
  echo "Active pull requests: $count ($state) — $counted"
else
  echo "Active pull requests: $count ($state)"
fi
if [ "$mode" = --admit ]; then
  printf '%s %s\n' "$state" "$count"
fi
