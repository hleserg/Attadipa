#!/usr/bin/env bash
# Which issue does the watchdog hand to a write-capable agent?
#
# The watchdog hands over by workflow_dispatch, and the intake gate trusts
# workflow_dispatch by construction — it skips the actor check entirely. So this
# filter is not a convenience: it is the last place an untrusted author can be
# refused before a billable writer starts. It reached review in a state where
# naming `claude[bot]` in ATTADIPA_TRUSTED_PRODUCERS would have let the
# repository's own output drive its own agent, and no test existed to catch it.
# This is that test.
#
# Offline and deterministic: fixtures in, one issue number out.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
filter="$here/../scripts/queue-scan.jq"

pass=0; fail=0

# issue NUMBER LOGIN ASSOCIATION LABELS_CSV BODY
issue() {
  jq -nc --arg n "$1" --arg login "$2" --arg assoc "$3" --arg labels "$4" --arg body "$5" '
    { number: ($n | tonumber),
      pull_request: null,
      author_association: $assoc,
      user: { login: $login },
      labels: ($labels | if . == "" then [] else split(",") | map({name:.}) end),
      body: $body }'
}

MARKER='<!-- attadipa-agent-task
producer: chatgpt
-->

@claude please look at this.'

# check WANT DESCRIPTION TRUSTED -- ISSUE_JSON...
#
# WANT is the picked issue number, or "" for nothing waiting — the FAILED half
# of the filter's "NUMBER FAILED" output defaults to "0" and is compared by
# `check`, so an ordinary case need not spell it out. `checkfull` is the same
# thing without that default, for the cases where FAILED is the point.
check() {
  local want="$1" desc="$2" trusted="$3"; shift 4
  checkfull "$([ -n "$want" ] && echo "$want 0" || echo "")" "$desc" "$trusted" -- "$@"
}

checkfull() {
  local want="$1" desc="$2" trusted="$3"; shift 4
  local exclude="${WATCHDOG_TEST_EXCLUDE:-}"
  local got
  got=$(printf '%s\n' "$@" | jq -s . | jq -r --arg trusted "$trusted" --arg exclude "$exclude" -f "$filter")
  if [ "$got" = "$want" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s", got "%s"\n' "$desc" "$want" "$got"
  fi
}

CHATGPT='chatgpt-codex-connector[bot]'

echo "Watchdog queue scan — who may be handed to a write-capable agent"

check 7 "an owner's task with the agent:ready label" "" -- \
      "$(issue 7 hleserg OWNER agent:ready x)"
check 7 "an owner's task carrying only the marker" "" -- \
      "$(issue 7 hleserg OWNER "" "$MARKER")"
check "" "a stranger with the very same marker" "" -- \
      "$(issue 7 stranger NONE "" "$MARKER")"
check "" "a task somebody has already claimed" "" -- \
      "$(issue 7 hleserg OWNER agent:working "$MARKER")"
check "" "a task that is blocked" "" -- \
      "$(issue 7 hleserg OWNER agent:blocked "$MARKER")"

echo
echo "  Trusted producers"
check 7 "a listed producer app" "$CHATGPT" -- \
      "$(issue 7 "$CHATGPT" NONE "" "$MARKER")"
check "" "the same app with an empty list" "" -- \
      "$(issue 7 "$CHATGPT" NONE "" "$MARKER")"
check "" "an app nobody listed" "$CHATGPT" -- \
      "$(issue 7 "vendor-bot[bot]" NONE "" "$MARKER")"
check "" "an app whose login merely contains a listed one" "$CHATGPT" -- \
      "$(issue 7 "evil-$CHATGPT" NONE "" "$MARKER")"

echo
echo "  Our own output can never drive our own writer"
# The gate refuses to honour these in ATTADIPA_TRUSTED_PRODUCERS. It must be
# refused here too and not merely there, because the dispatch this produces
# enters the gate through the one door that does not check the actor.
check "" "claude[bot] named in the list anyway" "claude[bot],$CHATGPT" -- \
      "$(issue 7 "claude[bot]" NONE "" "$MARKER")"
check "" "github-actions[bot] named in the list anyway" "github-actions[bot]" -- \
      "$(issue 7 "github-actions[bot]" NONE "" "$MARKER")"
check "" "bare claude named in the list anyway" "claude" -- \
      "$(issue 7 claude NONE "" "$MARKER")"
check "" "bare github-actions named in the list anyway" "github-actions" -- \
      "$(issue 7 github-actions NONE "" "$MARKER")"
check 8 "a real producer is still picked while an internal one is listed" "claude[bot],$CHATGPT" -- \
      "$(issue 7 "claude[bot]" NONE "" "$MARKER")" \
      "$(issue 8 "$CHATGPT" NONE "" "$MARKER")"

echo
echo "  Priority order"
check 5 "P0 outranks a lower-numbered P2" "" -- \
      "$(issue 3 hleserg OWNER "agent:ready,priority:P2" x)" \
      "$(issue 5 hleserg OWNER "agent:ready,priority:P0" x)"
check 3 "equal priority falls back to issue number" "" -- \
      "$(issue 9 hleserg OWNER agent:ready x)" \
      "$(issue 3 hleserg OWNER agent:ready x)"
check 9 "an owner's P0 outranks a listed producer" "$CHATGPT" -- \
      "$(issue 2 "$CHATGPT" NONE "" "$MARKER")" \
      "$(issue 9 hleserg OWNER "agent:ready,priority:P0" x)"

echo
echo "  A failed task and the promise the hand-over makes about it — #82"
# The hand-over writes agent:failed AND agent:ready together on a generic
# failure, deliberately: agent:ready says "back in the queue" and
# agent:failed marks that this is not its first run. Dropping the pair here,
# as the filter used to, contradicted the outcome comment that promises the
# watchdog will pick it up.
checkfull "7 1" "the pair is picked, and flagged so the caller can bound it" "" -- \
      "$(issue 7 hleserg OWNER "agent:ready,agent:failed" x)"
check "" "agent:failed alone, with no agent:ready, is not waiting" "" -- \
      "$(issue 7 hleserg OWNER agent:failed x)"
check "" "agent:failed alone, even carrying the marker, is not waiting" "" -- \
      "$(issue 7 hleserg OWNER agent:failed "$MARKER")"
WATCHDOG_TEST_EXCLUDE=7 checkfull "9 0" \
      "\$exclude skips a candidate the caller already bounced this round" "" -- \
      "$(issue 7 hleserg OWNER "agent:ready,agent:failed,priority:P0" x)" \
      "$(issue 9 hleserg OWNER agent:ready x)"
check 7 "\$exclude defaults to empty and excludes nothing" "" -- \
      "$(issue 7 hleserg OWNER agent:ready x)"
WATCHDOG_TEST_EXCLUDE=7 checkfull "9 1" \
      "the loop's second candidate can itself be failed, and is flagged the same way" "" -- \
      "$(issue 7 hleserg OWNER "agent:ready,agent:failed,priority:P0" x)" \
      "$(issue 9 hleserg OWNER "agent:ready,agent:failed" x)"
WATCHDOG_TEST_EXCLUDE=7,9 checkfull "11 0" \
      "a multi-value \$exclude, the shape the loop actually builds after two bounces" "" -- \
      "$(issue 7 hleserg OWNER "agent:ready,agent:failed,priority:P0" x)" \
      "$(issue 9 hleserg OWNER "agent:ready,agent:failed,priority:P0" x)" \
      "$(issue 11 hleserg OWNER agent:ready x)"
WATCHDOG_TEST_EXCLUDE=7 checkfull "17 0" \
      "\$exclude matches whole issue numbers, not a numeric substring" "" -- \
      "$(issue 17 hleserg OWNER agent:ready x)"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
