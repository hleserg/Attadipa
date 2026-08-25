#!/usr/bin/env bash
# Which issues carry agent:failed with nothing that says what happens next?
#
# Offline and deterministic: fixtures in, issue numbers out. See the header of
# .github/scripts/stranded-failures.jq for what this is guarding against.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
filter="$here/../scripts/stranded-failures.jq"

pass=0; fail=0

# issue NUMBER LABELS_CSV [AUTHOR_ASSOCIATION] [LOGIN]
#
# The author fields default to an owner-filed issue, which is what every
# label-shape assertion below is about. The author filter itself is asserted
# separately, at the bottom, by passing them.
issue() {
  jq -nc --arg n "$1" --arg labels "$2" \
         --arg assoc "${3:-OWNER}" --arg login "${4:-hleserg}" '
    { number: ($n | tonumber),
      pull_request: null,
      author_association: $assoc,
      user: { login: $login },
      labels: ($labels | if . == "" then [] else split(",") | map({name:.}) end) }'
}

# pr NUMBER LABELS_CSV -- same shape, but a pull request rather than an issue.
pr() {
  jq -nc --arg n "$1" --arg labels "$2" '
    { number: ($n | tonumber),
      pull_request: {},
      author_association: "OWNER",
      user: { login: "hleserg" },
      labels: ($labels | if . == "" then [] else split(",") | map({name:.}) end) }'
}

# check WANT DESCRIPTION -- ISSUE_JSON...
check() {
  checkfull "$1" "" "$2" "$@"
}

# checkfull WANT TRUSTED DESCRIPTION -- ISSUE_JSON...
#
# TRUSTED is what ATTADIPA_TRUSTED_PRODUCERS holds for this assertion. It is a
# separate entry point rather than an extra argument on `check` so that the
# nine label-shape assertions read exactly as they did before the author filter
# existed.
checkfull() {
  local want="$1" trusted="$2" desc="$3"; shift 3
  while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do shift; done
  shift || true
  local got
  got=$(printf '%s\n' "$@" | jq -s . | jq -r --arg trusted "$trusted" -f "$filter" | paste -sd, -)
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
echo "Who filed it — the same author filter queue-scan.jq applies"

# The whole point of the filter: re-queueing an issue the scan will never pick
# leaves it stranded a second time with a comment on it saying it is not.
checkfull "" "" "an untrusted author's stranded issue is left alone, not re-queued" -- \
      "$(issue 27 agent:failed NONE some-app[bot])"
checkfull 27 "some-app[bot]" "the same issue IS re-queued once its author is trusted" -- \
      "$(issue 27 agent:failed NONE "some-app[bot]")"
checkfull 27 "" "MEMBER is trusted without being listed" -- \
      "$(issue 27 agent:failed MEMBER someone)"
checkfull 27 "" "COLLABORATOR is trusted without being listed" -- \
      "$(issue 27 agent:failed COLLABORATOR someone)"
checkfull "" "" "CONTRIBUTOR is not, and is not a near-miss for COLLABORATOR" -- \
      "$(issue 27 agent:failed CONTRIBUTOR someone)"

# Non-listable, exactly as in queue-scan.jq: this sweep re-queues, and the
# queue dispatches by workflow_dispatch, which the intake gate trusts by
# construction. Our own output must not be able to buy a billable writer.
checkfull "" "claude[bot]" "claude[bot] cannot be listed into trust here either" -- \
      "$(issue 27 agent:failed NONE "claude[bot]")"
checkfull "" "github-actions[bot]" "nor can github-actions[bot]" -- \
      "$(issue 27 agent:failed NONE "github-actions[bot]")"
checkfull "" "claude" "nor the bare form of either name" -- \
      "$(issue 27 agent:failed NONE claude)"
checkfull "" "some-app[bot]" "a trusted list matches whole logins, not substrings" -- \
      "$(issue 27 agent:failed NONE some-app)"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
