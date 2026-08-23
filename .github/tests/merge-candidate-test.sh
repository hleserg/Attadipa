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

# run_rule ARGS... -- calls the rule, filling in the two arguments the older
# assertions predate.
#
# CHANGED_PATHS defaults to STATUS.md and PASS_AFTER_HEAD to `true`, so every
# assertion written before those conditions existed still tests exactly the
# condition it names rather than being refused by a new one first. Both new
# conditions have sections of their own below.
run_rule() {
  local checks="$1" labels="$2" unresolved="$3" codex="$4"
  local mergeable="$5" is_draft="$6" head_age="$7"
  local paths="${8-STATUS.md}" pass_after="${9-true}"
  bash "$SCRIPT_UNDER_TEST" "$checks" "$labels" "$unresolved" "$codex" \
       "$mergeable" "$is_draft" "$head_age" "$paths" "$pass_after"
}

# ok NAME EXPECTED ARGS...
ok() {
  local name="$1" expected="$2"; shift 2
  local got; got="$(run_rule "$@")"
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
  local got; got="$(run_rule "$@")"
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
                                 "success" "$(printf 'ai-review:pass\nai-review:blocking')" 0 0 clean false "$OLD"
ok "and it refuses even alongside a pass" "HOLD ai-review:blocking is set" \
                                 "success" "$(printf 'ai-review:blocking\nai-review:pass')" 0 0 clean false "$OLD"
ok "agent:blocked refuses"       "HOLD agent:blocked is set" \
                                 "success" "$(printf 'ai-review:pass\nagent:blocked')" 0 0 clean false "$OLD"
ok "needs-owner refuses"         "HOLD needs-owner is set" \
                                 "success" "$(printf 'ai-review:pass\nneeds-owner')" 0 0 clean false "$OLD"
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
                                 "success" "$(printf 'ai-review:pass\nai-review:blocking')" 0 0 draft true "$OLD"
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
echo "Which commit the verdict was reached on"
# The label records that a verdict happened, not which commit it happened on.
# Reviewer passes A, agent pushes B, the review of B reaches no verdict (a spent
# quota, a cancellation, an actor refusal, the workflow-validation skip that
# reports SUCCESS) -- nothing removes the label, and B merges reviewed by
# nothing. The backstop routine calls this the likeliest of its guards to recur.
ok "a pass older than the head commit refuses" \
                                "HOLD ai-review:pass predates the head commit" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" false
ok "and an unreadable answer refuses too, rather than being assumed covered" \
                                "HOLD could not tell whether ai-review:pass covers the head commit" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" unknown
ok "an empty answer is unknown, not true" \
                                "HOLD could not tell whether ai-review:pass covers the head commit" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" ""
ok "and neither is a stray word" \
                                "HOLD could not tell whether ai-review:pass covers the head commit" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" yes
ok "a draft whose pass predates its head is not undrafted either" \
                                "HOLD ai-review:pass predates the head commit" \
                                 "success" "ai-review:pass" 0 0 draft true "$OLD" "STATUS.md" false

echo
echo "What it touches — CLAUDE_AUTOMATION.md's table, row by row"
# Row 1: the nine documentation directories, plus STATUS.md and TASKS.md,
# which are on the list because CLAUDE.md requires them in the same commit.
for allowed in docs/architecture/a.md docs/community/a.md docs/hardware/a.md \
               docs/mobile/a.md docs/node/a.md docs/research/a.md \
               docs/testing/a.md docs/ui/a.md docs/upstream/a.md \
               STATUS.md TASKS.md; do
  ok "$allowed may be merged unattended" MERGE \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "$allowed"
done
ok "and a pull request touching several allowed paths still merges" MERGE \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" \
                                 "$(printf 'docs/research/a.md\nSTATUS.md\nTASKS.md')"

# Every "no" row. Each is a decision of the owner's, 2026-08-21.
ok "docs/master-prompt-final.md may not — a process that can edit the requirements it is judged against is not a process" \
                                "HOLD docs/master-prompt-final.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/master-prompt-final.md"
ok "docs/research/OWNER_DECISIONS.md may not, though its directory may" \
                                "HOLD docs/research/OWNER_DECISIONS.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/research/OWNER_DECISIONS.md"
ok "docs/adr/ may not — ADR-0003 is what stands between this project and assuming a T-Watch has LoRa" \
                                "HOLD docs/adr/0003-radio-not-lora.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/adr/0003-radio-not-lora.md"
ok "docs/automation/ may not — a gate that can widen itself is not a gate" \
                                "HOLD docs/automation/CLAUDE_AUTOMATION.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/automation/CLAUDE_AUTOMATION.md"
ok ".github/ may not, for the same reason — including this very rule" \
                                "HOLD .github/scripts/merge-candidate.sh is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" ".github/scripts/merge-candidate.sh"
ok "and neither may the workflow that runs it" \
                                "HOLD .github/workflows/pr-merge-sweep.yml is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" ".github/workflows/pr-merge-sweep.yml"
for denied in core/x.c platform/x.c link/x.c apps/x.c sim/x.c boards/x.c; do
  ok "$denied may not — green CI proves nothing about a board" \
                                "HOLD $denied is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "$denied"
done
ok "the live Pages document may not — a merge there is a publication" \
                                "HOLD docs/index.html is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/index.html"
ok "nor its assets" \
                                "HOLD docs/assets/site.js is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/assets/site.js"
ok "nor the brand, which is an identity decision" \
                                "HOLD docs/brand/logo.svg is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/brand/logo.svg"
ok "a file added to docs/ after the table was written may not — that is what an allowlist is for" \
                                "HOLD docs/newthing/a.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/newthing/a.md"
ok "a bare file at the docs/ root may not either" \
                                "HOLD docs/README.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/README.md"
ok "nor a new file at the repository root, however harmless it looks" \
                                "HOLD NOTES.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "NOTES.md"

# A prefix match must be a directory match.
ok "docs/uix/ is not admitted by the docs/ui/ row" \
                                "HOLD docs/uix/a.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "docs/uix/a.md"
ok "and a path merely containing an allowed directory name is not admitted" \
                                "HOLD vendor/docs/research/a.md is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "vendor/docs/research/a.md"

# One disallowed path is enough, wherever it sits in the list.
ok "one refused path refuses the whole pull request, even last" \
                                "HOLD core/x.c is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" \
                                 "$(printf 'docs/research/a.md\nSTATUS.md\ncore/x.c')"
ok "and even first" \
                                "HOLD core/x.c is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" \
                                 "$(printf 'core/x.c\ndocs/research/a.md')"

# An unknown change set is not a permitted one.
ok "an empty path list refuses" \
                                "HOLD could not read which paths this changes" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" ""
ok "and so does whitespace" \
                                "HOLD could not read which paths this changes" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "  "
ok "a draft touching a refused path is not undrafted either" \
                                "HOLD core/x.c is not on the unattended-merge allowlist" \
                                 "success" "ai-review:pass" 0 0 draft true "$OLD" "core/x.c"

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
