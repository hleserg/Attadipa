#!/usr/bin/env bash
# Which issues carry agent:failed with nothing that says what happens next?
#
# Offline and deterministic: fixtures in, issue numbers out. See the header of
# .github/scripts/stranded-failures.jq for what this is guarding against.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
filter="$here/../scripts/stranded-failures.jq"

pass=0; fail=0

# issue NUMBER LABELS_CSV
issue() {
  jq -nc --arg n "$1" --arg labels "$2" '
    { number: ($n | tonumber),
      pull_request: null,
      labels: ($labels | if . == "" then [] else split(",") | map({name:.}) end) }'
}

# pr NUMBER LABELS_CSV -- same shape, but a pull request rather than an issue.
pr() {
  jq -nc --arg n "$1" --arg labels "$2" '
    { number: ($n | tonumber),
      pull_request: {},
      labels: ($labels | if . == "" then [] else split(",") | map({name:.}) end) }'
}

# check WANT DESCRIPTION -- ISSUE_JSON...
check() {
  local want="$1" desc="$2"; shift 3
  local got
  got=$(printf '%s\n' "$@" | jq -s . | jq -r -f "$filter" | paste -sd, -)
  if [ "$got" = "$want" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s", got "%s"\n' "$desc" "$want" "$got"
  fi
}

echo "Stranded failures — agent:failed with no state that explains it"

check 27 "agent:failed alone is stranded" -- \
      "$(issue 27 agent:failed)"
check "" "agent:failed with agent:ready is queued, not stranded" -- \
      "$(issue 27 "agent:failed,agent:ready")"
check "" "agent:failed with agent:working is a live run, not stranded" -- \
      "$(issue 27 "agent:failed,agent:working")"
check "" "agent:failed already relabelled agent:blocked is not stranded again" -- \
      "$(issue 27 "agent:failed,agent:blocked")"
check "" "agent:failed on an issue also marked agent:done is not stranded" -- \
      "$(issue 27 "agent:failed,agent:done")"
check "" "no agent:failed at all is never stranded" -- \
      "$(issue 27 agent:ready)"
check "" "agent:failed with agent:review has real work awaiting review, not nothing queuing it" -- \
      "$(issue 27 "agent:failed,agent:review")"
check "" "a pull request carrying agent:failed is never stranded, whatever its labels" -- \
      "$(pr 27 agent:failed)"
check "27,28" "two stranded issues are both reported" -- \
      "$(issue 27 agent:failed)" \
      "$(issue 28 agent:failed)"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
