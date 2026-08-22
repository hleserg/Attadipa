#!/usr/bin/env bash
# Every condition in .github/scripts/merge-candidate.sh, asserted both ways.
#
# "Both ways" is the point. A merge rule that only has passing cases is a rule
# nobody has proved refuses anything, and this one can put a commit in `main`
# with no person in the loop. So each condition gets a case that merges and a
# case that does not, and the negative case names the condition it is testing.
#
# The order of the conditions is also asserted, because the backstop routine's
# own history is three rounds of review finding the same ordering mistake: a
# draft reads `draft` in mergeStateStatus, never `clean`, so a rule that checked
# the state before the draft flag would decline every candidate it was written
# for -- with a message about the wrong condition.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
SCRIPT_UNDER_TEST=.github/scripts/merge-candidate.sh

pass=0; fail=0

# ok NAME EXPECTED ARGS...
ok() {
  local name="$1" expected="$2"; shift 2
  local got; got="$(bash "$SCRIPT_UNDER_TEST" "$@")"
  if [ "$got" = "$expected" ]; then
    printf '  ok    %s\n' "$name"; pass=$((pass + 1))
  else
    printf '  FAIL  %s\n        expected: %s\n        got:      %s\n' "$name" "$expected" "$got"
    fail=$((fail + 1))
  fi
}

# starts NAME PREFIX ARGS... -- for HOLD lines whose tail carries a number
starts() {
  local name="$1" prefix="$2"; shift 2
  local got; got="$(bash "$SCRIPT_UNDER_TEST" "$@")"
  case "$got" in
    "$prefix"*) printf '  ok    %s\n' "$name"; pass=$((pass + 1)) ;;
    *) printf '  FAIL  %s\n        expected prefix: %s\n        got:             %s\n' \
         "$name" "$prefix" "$got"; fail=$((fail + 1)); ;;
  esac
}

OLD=30000   # over six hours
NEW=900     # well under

echo "The pull request that has everything"
ok "merges"                     MERGE   "success success skipped" "ai-review:pass" 0 0 clean false "$OLD"
ok "a lone skipped check counts as a check" MERGE "skipped" "ai-review:pass" 0 0 clean false "$OLD"

echo
echo "The reviewer's verdict"
ok "no ai-review:pass is no verdict, not a silent yes" \
                                "HOLD no ai-review:pass" "success" "" 0 0 clean false "$OLD"
ok "ai-review:blocking refuses"  "HOLD ai-review:blocking is set" \
                                 "success" "ai-review:pass ai-review:blocking" 0 0 clean false "$OLD"
ok "and it refuses even alongside a pass" "HOLD ai-review:blocking is set" \
                                 "success" "ai-review:blocking ai-review:pass" 0 0 clean false "$OLD"
ok "agent:blocked refuses"       "HOLD agent:blocked is set" \
                                 "success" "ai-review:pass agent:blocked" 0 0 clean false "$OLD"
ok "needs-owner refuses"         "HOLD needs-owner is set" \
                                 "success" "ai-review:pass needs-owner" 0 0 clean false "$OLD"
ok "a label that merely contains the word does not count as it" \
                                "HOLD no ai-review:pass" \
                                 "success" "ai-review:passing-later" 0 0 clean false "$OLD"

echo
echo "The checks"
ok "an empty check list is not vacuously green" \
                                "HOLD no check run on the head commit" \
                                 "" "ai-review:pass" 0 0 clean false "$OLD"
ok "whitespace is still an empty check list" \
                                "HOLD no check run on the head commit" \
                                 "   " "ai-review:pass" 0 0 clean false "$OLD"
ok "one failure refuses"         "HOLD check run is failure" \
                                 "success success failure" "ai-review:pass" 0 0 clean false "$OLD"
ok "a cancelled run refuses"     "HOLD check run is cancelled" \
                                 "success cancelled" "ai-review:pass" 0 0 clean false "$OLD"
ok "an in-flight run refuses"    "HOLD check run is pending" \
                                 "success pending" "ai-review:pass" 0 0 clean false "$OLD"
ok "and an unknown conclusion refuses rather than being assumed benign" \
                                "HOLD check run is action_required" \
                                 "action_required" "ai-review:pass" 0 0 clean false "$OLD"

echo
echo "The other reviewer"
starts "an unresolved review thread refuses" "HOLD 1 unresolved review thread" \
                                 "success" "ai-review:pass" 1 0 clean false "$OLD"
starts "an unanswered Codex comment refuses, review thread or not" \
                                "HOLD 2 unanswered comment" \
                                 "success" "ai-review:pass" 0 2 clean false "$OLD"

echo
echo "How old the code is"
starts "a head commit under six hours refuses" "HOLD head commit is 900 s old" \
                                 "success" "ai-review:pass" 0 0 clean false "$NEW"
ok "exactly six hours is still too new" \
                                "HOLD head commit is 21599 s old, under 21600" \
                                 "success" "ai-review:pass" 0 0 clean false 21599
ok "one second past six hours merges" MERGE \
                                 "success" "ai-review:pass" 0 0 clean false 21600
ok "an unknown age refuses rather than counting as old" \
                                "HOLD head commit age unknown" \
                                 "success" "ai-review:pass" 0 0 clean false ""
ok "and a non-numeric age is not silently zero" \
                                "HOLD head commit age unknown" \
                                 "success" "ai-review:pass" 0 0 clean false "later"

echo
echo "Draft, and the order the conditions are checked in"
ok "a qualifying draft says READY, not MERGE" READY \
                                 "success" "ai-review:pass" 0 0 draft true "$OLD"
ok "a draft that fails a condition says HOLD, not READY" \
                                "HOLD ai-review:blocking is set" \
                                 "success" "ai-review:pass ai-review:blocking" 0 0 draft true "$OLD"
ok "a draft too new to merge is not undrafted either" \
                                "HOLD head commit is 900 s old, under 21600" \
                                 "success" "ai-review:pass" 0 0 draft true "$NEW"
ok "a draft with a failing check is not undrafted" \
                                "HOLD check run is failure" \
                                 "failure" "ai-review:pass" 0 0 draft true "$OLD"
ok "the draft flag is read before mergeStateStatus, or every draft would be refused for the wrong reason" \
                                READY "success" "ai-review:pass" 0 0 draft true "$OLD"

echo
echo "The mergeable state, once it is not a draft"
ok "a conflict refuses -- never resolve one to merge it" \
                                "HOLD mergeable state is dirty" \
                                 "success" "ai-review:pass" 0 0 dirty false "$OLD"
ok "behind refuses"              "HOLD mergeable state is behind" \
                                 "success" "ai-review:pass" 0 0 behind false "$OLD"
ok "blocked refuses"             "HOLD mergeable state is blocked" \
                                 "success" "ai-review:pass" 0 0 blocked false "$OLD"
ok "unknown refuses"             "HOLD mergeable state is unknown" \
                                 "success" "ai-review:pass" 0 0 "" false "$OLD"

echo
echo "The rule does not grow a seventh condition or lose one of the six"
for condition in 'ai-review:pass' 'ai-review:blocking' 'agent:blocked' 'needs-owner' \
                 'MIN_HEAD_AGE_SECONDS' 'mergeable' 'is_draft'; do
  if grep -q -- "$condition" "$SCRIPT_UNDER_TEST"; then
    printf '  ok    %s is still checked\n' "$condition"; pass=$((pass + 1))
  else
    printf '  FAIL  %s is no longer checked\n' "$condition"; fail=$((fail + 1))
  fi
done

if grep -qE 'MIN_HEAD_AGE_SECONDS=[0-9]+' "$SCRIPT_UNDER_TEST" \
   && [ "$(grep -oE 'MIN_HEAD_AGE_SECONDS=[0-9]+' "$SCRIPT_UNDER_TEST" | head -1 | cut -d= -f2)" = "21600" ]; then
  printf '  ok    the settling window is still six hours\n'; pass=$((pass + 1))
else
  printf '  FAIL  the settling window is no longer six hours\n'; fail=$((fail + 1))
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
