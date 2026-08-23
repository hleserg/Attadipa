#!/usr/bin/env bash
# How many agent:failed labellings count against the bounded retry?
#
# Offline and deterministic: a timeline fixture in, an integer out. See the
# header of .github/scripts/failure-count.jq for the defect this exists to
# catch — a first version of this file counted an issue's whole history and
# never let a genuinely fixed task retry more than once, ever.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
filter="$here/../scripts/failure-count.jq"

pass=0; fail=0

# labeled EVENT ACTOR TIME
labeled() {
  jq -nc --arg label "$1" --arg actor "$2" --arg at "$3" \
    '{event: "labeled", label: {name: $label}, actor: {login: $actor}, created_at: $at}'
}

# other EVENT TIME -- an event with no label at all, e.g. a comment.
other() {
  jq -nc --arg event "$1" --arg at "$2" '{event: $event, created_at: $at}'
}

# check WANT DESCRIPTION -- EVENT_JSON...
check() {
  local want="$1" desc="$2"; shift 3
  local got
  got=$(printf '%s\n' "$@" | jq -s . | jq -r -f "$filter")
  if [ "$got" = "$want" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s", got "%s"\n' "$desc" "$want" "$got"
  fi
}

echo "Failure count — agent:failed labellings since a human last queued it"

check 0 "no history at all" -- \
      "$(other created "2026-08-22T07:00:00Z")"

check 1 "one failure, nobody has intervened" -- \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T08:00:00Z")"

check 2 "two failures, nobody has intervened" -- \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T09:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T09:00:00Z")"

# The defect this file exists to fix: two failures happened, a human fixed
# the cause and re-queued it by hand, and it has not failed since. The count
# must read as zero, not two, or the retry the human just earned never
# happens.
check 0 "a human re-add resets the count even after two prior failures" -- \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T09:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T09:00:00Z")" \
      "$(labeled agent:blocked github-actions[bot] "2026-08-22T09:00:01Z")" \
      "$(labeled agent:ready hleserg "2026-08-22T10:00:00Z")"

check 1 "one failure after a human reset counts only that one" -- \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:ready hleserg "2026-08-22T10:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T11:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T11:00:00Z")"

check 1 "claude[bot] re-adding agent:ready is not a human reset either" -- \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T08:00:00Z")" \
      "$(labeled agent:ready "claude[bot]" "2026-08-22T09:00:00Z")"

check 1 "the reset point is the MOST RECENT human re-add, not the first" -- \
      "$(labeled agent:ready hleserg "2026-08-22T06:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(labeled agent:ready hleserg "2026-08-22T08:00:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T09:00:00Z")" \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T09:00:00Z")"

check 1 "unrelated events between the failures do not confuse the count" -- \
      "$(labeled agent:ready github-actions[bot] "2026-08-22T07:00:00Z")" \
      "$(other commented "2026-08-22T07:30:00Z")" \
      "$(labeled priority:P1 hleserg "2026-08-22T07:45:00Z")" \
      "$(labeled agent:failed github-actions[bot] "2026-08-22T08:00:00Z")"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
