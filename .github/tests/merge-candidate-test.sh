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
#
# AND SINCE #170 IT ALSO COVERS WHAT THE CALLER READ, not only what the rule did
# with it. Every argument to the rule is a summary, and a summary carries no
# trace of how much was read before summarising: `reviewThreads(first:100)` over
# a pull request with 101 threads returns a hundred, so an unresolved
# hundred-and-first arrives as `UNRESOLVED=0` -- the value that merges. No
# assertion phrased in the rule's own arguments could ever have caught that,
# which is why the sections below build documents shaped like GitHub's replies
# and run the filter that ships (.github/scripts/merge-facts.jq, through
# .github/scripts/merge-facts.sh) rather than passing in numbers somebody
# normalised by hand.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
SCRIPT_UNDER_TEST=.github/scripts/merge-candidate.sh

pass=0; fail=0

# run_rule ARGS... -- calls the rule, filling in the arguments the older
# assertions predate.
#
# CHANGED_PATHS defaults to STATUS.md, PASS_AFTER_HEAD and FACTS_COMPLETE to
# `true`, so every assertion written before those conditions existed still tests
# exactly the condition it names rather than being refused by a new one first.
# Each new condition has a section of its own below.
run_rule() {
  local checks="$1" labels="$2" unresolved="$3" codex="$4"
  local mergeable="$5" is_draft="$6" head_age="$7"
  local paths="${8-STATUS.md}" pass_after="${9-true}" complete="${10-true}"
  bash "$SCRIPT_UNDER_TEST" "$checks" "$labels" "$unresolved" "$codex" \
       "$mergeable" "$is_draft" "$head_age" "$paths" "$pass_after" "$complete"
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

# A COMMIT STATUS IS NOT A CHECK RUN. Both arrive in one `statusCheckRollup`,
# and flattening them let a third-party app stand in for CI having run: the
# only context on some head commits was "Devin Review / success / Full review
# skipped: trial expired and no credits remaining", so the combined state read
# `success` over a pull request whose workflows were all still waiting for
# approval. A green status is evidence about that app. A red one is still
# information and still refuses.
ok "a green commit status alone is not a check having run" \
                                "HOLD no check run on the head commit" \
                                 "status:success" "ai-review:pass" 0 0 clean false "$OLD"
ok "several green statuses are still not a check having run" \
                                "HOLD no check run on the head commit" \
                                 "status:success status:success" "ai-review:pass" 0 0 clean false "$OLD"
ok "a red commit status refuses, and says it was a status" \
                                "HOLD commit status is failure" \
                                 "success status:failure" "ai-review:pass" 0 0 clean false "$OLD"
ok "a pending commit status refuses too" \
                                "HOLD commit status is pending" \
                                 "success status:pending" "ai-review:pass" 0 0 clean false "$OLD"
ok "and a green status beside a real green check run does not block the merge" \
                                "MERGE" \
                                 "success status:success" "ai-review:pass" 0 0 clean false "$OLD"

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
echo "Whether the caller read all of it"
# A GraphQL connection is a page. `reviewThreads(first:100)` over a pull request
# with 101 threads returns a hundred, and if the unresolved one is the
# hundred-and-first then UNRESOLVED arrives here as `0` -- the value that
# merges. No condition on the arguments above can see that, because every one of
# them is a summary carrying no trace of how much was read. Issue #170.
ok "a proven-complete snapshot merges"  MERGE \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true true
ok "a truncated one refuses"     "HOLD the facts read about this pull request were truncated" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true false
ok "an unreadable answer refuses rather than being assumed complete" \
                                "HOLD could not tell whether the facts read about this pull request are complete" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true unknown
ok "an empty answer is unknown, not true" \
                                "HOLD could not tell whether the facts read about this pull request are complete" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true ""
ok "and neither is a stray word" \
                                "HOLD could not tell whether the facts read about this pull request are complete" \
                                 "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true yes
ok "a draft whose facts are truncated is not undrafted either" \
                                "HOLD the facts read about this pull request were truncated" \
                                 "success" "ai-review:pass" 0 0 draft true "$OLD" "STATUS.md" true false

# THE OLD CALLER IS THE DEFECT, so it is refused by arity rather than reinstated
# by a default. Nine arguments is a caller that read bounded pages and never
# asked whether there were more; there is no reading of those nine under which
# this may merge. The message names the fix, because this line is what a reader
# sees in the sweep log until somebody applies it.
got="$(bash "$SCRIPT_UNDER_TEST" "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true 2>/dev/null)"
case "$got" in
  "HOLD this caller cannot prove it read all of the pull request"*)
    printf '  ok    a nine-argument caller cannot merge, and is told what to apply\n'; pass=$((pass + 1)) ;;
  *)
    printf '  FAIL  a nine-argument caller is the pre-#170 sweep and must not merge\n        got: %s\n' "$got"
    fail=$((fail + 1)) ;;
esac

# The redirect above is deliberate, and so is this. `ci.yml` runs this suite as
# a plain `run:` step, and the runner scans a step's stderr for workflow
# commands -- so an unredirected fixture prints `::warning::the merge sweep is
# holding every pull request` on every CI run of every pull request, and keeps
# printing it after the patch lands and the state is no longer true. The one
# signal that separates a disabled sweep from an idle one would be buried in
# its own noise long before the day it matters. Discarding it there means
# asserting it here, or the remedy is inert and nothing would say so.
warned="$(bash "$SCRIPT_UNDER_TEST" "success" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true 2>&1 >/dev/null)"
case "$warned" in
  *"::warning::"*"merge sweep is holding every pull request"*)
    printf '  ok    and it warns on stderr, which is where the sweep log shows it\n'; pass=$((pass + 1)) ;;
  *)
    printf '  FAIL  the nine-argument refusal is silent on stderr, so a disabled sweep reads as an idle one\n        got: %s\n' "$warned"
    fail=$((fail + 1)) ;;
esac

# The order is asserted, as it is for draft-before-mergeable above: an
# unreadable snapshot is reported as unreadable, not as whichever condition
# happened to be evaluated first over facts nobody could trust.
ok "completeness is answered before the verdict, because the verdict was read out of the same snapshot" \
                                "HOLD the facts read about this pull request were truncated" \
                                 "success" "$(printf 'ai-review:pass\nai-review:blocking')" 0 0 clean false "$OLD" "STATUS.md" true false

echo
echo "What the sweep actually reads — the GraphQL contract, over response shapes"
# These do not hand the rule a normalised value. They build a document shaped
# like GitHub's own reply -- the shapes were taken from live responses for this
# repository's #173 and #176 on 2026-08-24 -- and run the filter that ships,
# .github/scripts/merge-facts.jq, through the wrapper that ships.
FACTS_RULE=.github/scripts/merge-facts.sh
FACTS_QUERY=.github/scripts/merge-facts.graphql

if ! command -v jq >/dev/null 2>&1; then
  printf '  FAIL  jq is not installed, so the GraphQL contract was not tested at all\n'
  fail=$((fail + 1))
fi

# facts JQ-MUTATION... -- a healthy, complete, mergeable response, then the
# mutations that make it the case under test.
facts() {
  local doc mutation
  doc="$(jq -nc '{data:{repository:{pullRequest:{
    isDraft: false,
    mergeStateStatus: "CLEAN",
    labels: {totalCount:1, pageInfo:{hasNextPage:false}, nodes:[{name:"ai-review:pass"}]},
    reviewThreads: {totalCount:0, pageInfo:{hasNextPage:false}, nodes:[]},
    files: {totalCount:1, pageInfo:{hasNextPage:false}, nodes:[{path:"STATUS.md"}]},
    timelineItems: {totalCount:15, pageInfo:{hasNextPage:false, hasPreviousPage:false},
                    nodes:[{createdAt:"2026-08-24T00:00:00Z", label:{name:"ai-review:pass"}}]},
    commits: {totalCount:1, pageInfo:{hasNextPage:false}, nodes:[{commit:{
      committedDate:"2026-08-23T00:00:00Z", pushedDate:"2026-08-23T00:00:00Z",
      statusCheckRollup:{contexts:{totalCount:1, pageInfo:{hasNextPage:false},
        nodes:[{__typename:"CheckRun", conclusion:"SUCCESS", status:"COMPLETED"}]}}}}]}
  }}}}')" || return 1
  for mutation in "$@"; do
    doc="$(printf '%s' "$doc" | jq -c "$mutation")" || return 1
  done
  printf '%s' "$doc"
}

# complete_says NAME EXPECTED-PREFIX DOCUMENT
complete_says() {
  local name="$1" expected="$2" doc="$3"
  local got; got="$(printf '%s' "$doc" | bash "$FACTS_RULE")"
  case "$got" in
    "$expected"*) printf '  ok    %s\n' "$name"; pass=$((pass + 1)) ;;
    *) printf '  FAIL  %s\n        expected prefix: %s\n        got:             %s\n' \
         "$name" "$expected" "$got"; fail=$((fail + 1)) ;;
  esac
}

# THE FINDING ITSELF. 101 review threads, the first hundred resolved, the
# hundred-and-first not -- so the page that comes back is a hundred resolved
# threads and nothing else.
TRUNCATED_THREADS="$(facts '.data.repository.pullRequest.reviewThreads =
  {totalCount:101, pageInfo:{hasNextPage:true}, nodes:[range(0;100)|{isResolved:true}]}')"

# And here is what the sweep used to compute over it, quoted verbatim from
# pr-merge-sweep.yml at 6965191. It is a historical constant on purpose: it
# records the defect rather than tracking the file, so it cannot quietly start
# agreeing with a fixed workflow.
OLD_UNRESOLVED="$(printf '%s' "$TRUNCATED_THREADS" | jq -r \
  '[.data.repository.pullRequest.reviewThreads.nodes[] | select(.isResolved | not)] | length')"
if [ "$OLD_UNRESOLVED" = "0" ]; then
  printf '  ok    the old extraction really did answer 0 unresolved over 101 threads\n'
  pass=$((pass + 1))
else
  printf '  FAIL  the fixture does not reproduce the finding: old extraction said %s, not 0\n' "$OLD_UNRESOLVED"
  fail=$((fail + 1))
fi
# Same fixture, same numbers, through the rule that ships now. This pair is the
# regression: the left-hand side is what merged, the right-hand side is what
# refuses.
complete_says "and the same document is refused as truncated" \
  "HOLD the review-thread list is truncated at 100 of 101" "$TRUNCATED_THREADS"
ok "so the verdict over it is a HOLD and not a MERGE" \
                                "HOLD the facts read about this pull request were truncated" \
                                 "success" "ai-review:pass" "$OLD_UNRESOLVED" 0 clean false "$OLD" "STATUS.md" true false

complete_says "truncation alone refuses, even when every thread that was read is resolved" \
  "HOLD the review-thread list is truncated" \
  "$(facts '.data.repository.pullRequest.reviewThreads =
      {totalCount:101, pageInfo:{hasNextPage:true}, nodes:[range(0;100)|{isResolved:true}]}')"

complete_says "a non-green check on the second page cannot be reached, so it refuses" \
  "HOLD the check list is truncated at 100 of 101" \
  "$(facts '.data.repository.pullRequest.commits.nodes[0].commit.statusCheckRollup.contexts =
      {totalCount:101, pageInfo:{hasNextPage:true},
       nodes:[range(0;100)|{__typename:"CheckRun", conclusion:"SUCCESS", status:"COMPLETED"}]}')"

# 101 GREEN check runs. Nothing is hiding on the second page and it still
# refuses, because "nothing is hiding there" is precisely what a truncated page
# cannot say. Explicit HOLD, never silent acceptance of the first hundred.
complete_says "101 green check runs are refused too, because the second page was never read" \
  "HOLD the check list is truncated at 100 of 101" \
  "$(facts '.data.repository.pullRequest.commits.nodes[0].commit.statusCheckRollup.contexts =
      {totalCount:101, pageInfo:{hasNextPage:true},
       nodes:[range(0;100)|{__typename:"CheckRun", conclusion:"SUCCESS", status:"COMPLETED"}]}')"

# THE LABELS ARE THE WORST OF THE FIVE: the only truncation that turns a refusal
# into a merge rather than into a hold. Asserted at both page sizes -- 51 over
# the `first:50` the query used to ask for, and 101 over the 100 it asks for now
# -- so raising the page size did not quietly become the fix.
complete_says "a blocking label past the old fiftieth refuses" \
  "HOLD the label list is truncated at 50 of 51" \
  "$(facts '.data.repository.pullRequest.labels =
      {totalCount:51, pageInfo:{hasNextPage:true},
       nodes:([{name:"ai-review:pass"}] + [range(0;49)|{name:"filler"}])}')"
complete_says "and one past the hundredth refuses the same way" \
  "HOLD the label list is truncated at 100 of 101" \
  "$(facts '.data.repository.pullRequest.labels =
      {totalCount:101, pageInfo:{hasNextPage:true},
       nodes:([{name:"ai-review:pass"}] + [range(0;99)|{name:"filler"}])}')"

complete_says "a truncated changed-file list refuses, because the path off the allowlist is on the page nobody read" \
  "HOLD the changed-file list is truncated at 100 of 140" \
  "$(facts '.data.repository.pullRequest.files =
      {totalCount:140, pageInfo:{hasNextPage:true}, nodes:[range(0;100)|{path:"docs/research/a.md"}]}')"

# EXACTLY AT THE LIMIT, SAYING SO. This is the assertion that stops anybody
# reintroducing `nodes | length == 100` as the test: a hundred nodes with
# `hasNextPage: false` is a complete set, and holding it would be guessing
# conservatively rather than reading the answer.
complete_says "exactly 100 threads with hasNextPage false is complete, not assumed truncated" COMPLETE \
  "$(facts '.data.repository.pullRequest.reviewThreads =
      {totalCount:100, pageInfo:{hasNextPage:false}, nodes:[range(0;100)|{isResolved:true}]}')"
complete_says "exactly 100 contexts with hasNextPage false is complete too" COMPLETE \
  "$(facts '.data.repository.pullRequest.commits.nodes[0].commit.statusCheckRollup.contexts =
      {totalCount:100, pageInfo:{hasNextPage:false},
       nodes:[range(0;100)|{__typename:"CheckRun", conclusion:"SUCCESS", status:"COMPLETED"}]}')"
complete_says "and exactly 50 labels with hasNextPage false is complete" COMPLETE \
  "$(facts '.data.repository.pullRequest.labels =
      {totalCount:50, pageInfo:{hasNextPage:false},
       nodes:([{name:"ai-review:pass"}] + [range(0;49)|{name:"filler"}])}')"

# A MISSING OR MALFORMED pageInfo IS NOT AN EMPTY SET. Every one of these must
# hold: "nothing there" is `0 unresolved threads`, and `0` merges.
complete_says "a connection with no pageInfo at all refuses" \
  "HOLD the label list came back without pageInfo" \
  "$(facts 'del(.data.repository.pullRequest.labels.pageInfo)')"
complete_says "a null pageInfo refuses" \
  "HOLD the review-thread list came back without pageInfo" \
  "$(facts '.data.repository.pullRequest.reviewThreads.pageInfo = null')"
complete_says "a pageInfo that is not an object refuses" \
  "HOLD the changed-file list came back with a pageInfo that is not an object" \
  "$(facts '.data.repository.pullRequest.files.pageInfo = "false"')"
complete_says "a pageInfo with no hasNextPage refuses" \
  "HOLD the label list came back with no usable hasNextPage" \
  "$(facts '.data.repository.pullRequest.labels.pageInfo = {hasPreviousPage:false}')"
complete_says "a hasNextPage that is a string rather than a boolean refuses" \
  "HOLD the label list came back with no usable hasNextPage" \
  "$(facts '.data.repository.pullRequest.labels.pageInfo.hasNextPage = "false"')"
complete_says "a null connection refuses" \
  "HOLD the review-thread list could not be read at all" \
  "$(facts '.data.repository.pullRequest.reviewThreads = null')"
complete_says "a connection with no nodes refuses, rather than counting as none" \
  "HOLD the review-thread list came back without a list of its own contents" \
  "$(facts 'del(.data.repository.pullRequest.reviewThreads.nodes)')"
complete_says "and a missing head commit refuses" \
  "HOLD the head commit is missing from the response" \
  "$(facts '.data.repository.pullRequest.commits.nodes = []')"

# THE TIMELINE. Both documents below are hypothetical: a `last:`-only connection
# answers `hasNextPage: false` under the Relay contract whatever it holds, so
# GitHub will not return the first one. These assert that the *extraction* treats
# the flag uniformly across every connection -- not that the case can arise. The
# timeline's real safety is downstream: an event outside the window is older than
# everything in it, so a truncated window yields no date and the caller holds.
complete_says "a label timeline flagged truncated refuses, uniformly with every other connection" \
  "HOLD the label timeline is truncated" \
  "$(facts '.data.repository.pullRequest.timelineItems.pageInfo.hasNextPage = true')"
complete_says "but older label events beyond the window are not a refusal, and must not become one" COMPLETE \
  "$(facts '.data.repository.pullRequest.timelineItems.pageInfo.hasPreviousPage = true')"

# A NULL ROLLUP IS A REAL STATE, NOT AN UNREADABLE ONE -- GitHub returns it when
# the head commit carries no check run and no commit status at all, observed on
# #176. It already has a precise answer waiting downstream, and this must not
# replace that with a vaguer one.
complete_says "a head commit with no checks at all is complete, not unreadable" COMPLETE \
  "$(facts '.data.repository.pullRequest.commits.nodes[0].commit.statusCheckRollup = null')"
ok "and the rule still refuses it for the reason that is actually true" \
                                "HOLD no check run on the head commit" \
                                 "" "ai-review:pass" 0 0 clean false "$OLD" "STATUS.md" true true

# The read that did not happen.
complete_says "an empty document refuses" "HOLD the pull request's facts came back empty" ""
complete_says "whitespace refuses"        "HOLD the pull request's facts came back empty" "   "
complete_says "unparseable output refuses" "HOLD the pull request's facts could not be parsed" "not json at all"
complete_says "a GraphQL error document with no pull request refuses" \
  "HOLD the response carries no pull request" \
  '{"data":{"repository":{"pullRequest":null}},"errors":[{"message":"Something went wrong"}]}'
complete_says "and a healthy document is not refused for sport" COMPLETE "$(facts)"

# The filter is a separate file, so its absence is a separate failure and must
# not be reported as GitHub having answered badly.
got="$(ATTADIPA_MERGE_FACTS_JQ=/nonexistent/merge-facts.jq bash "$FACTS_RULE" "$(facts)")"
case "$got" in
  "HOLD the completeness filter /nonexistent/merge-facts.jq is missing"*)
    printf '  ok    a missing filter refuses, and says it was the filter\n'; pass=$((pass + 1)) ;;
  *) printf '  FAIL  a missing filter must not read as a pull request with nothing wrong\n        got: %s\n' "$got"
     fail=$((fail + 1)) ;;
esac

echo
echo "The query asks for what the rule reads"
# The rule can only refuse on a `pageInfo` the query asked for. These two files
# are one mechanism split across two languages, and the join between them is the
# thing nothing else would notice going missing.
if [ -f "$FACTS_QUERY" ]; then
  printf '  ok    %s exists\n' "$FACTS_QUERY"; pass=$((pass + 1))
else
  printf '  FAIL  %s is missing, so the rule has nothing to read\n' "$FACTS_QUERY"; fail=$((fail + 1))
fi
# COMMENT LINES ARE DROPPED FIRST, and that is not tidiness. The first version
# of this scan read the whole file, and the query's own prose quotes the defect
# it fixed -- "it asked for `labels(first:50)` ... and for no `pageInfo` at all"
# -- so every connection was "found" inside a paragraph describing the bug.
# Deleting `pageInfo` from the real `labels(first: 100)` block left the scan
# green. Caught by mutating the query and watching nothing happen.
QUERY_BODY="$(grep -vE '^[[:space:]]*#' "$FACTS_QUERY")"
for connection in labels reviewThreads files timelineItems contexts; do
  # From the connection to its own `nodes`, which is the field it is being read
  # for, `pageInfo` has to appear in between -- and the connection has to appear
  # at all, so deleting one outright is a failure and not a vacuous pass.
  if printf '%s\n' "$QUERY_BODY" | awk -v c="$connection" '
      $0 ~ "(^|[^A-Za-z])" c "[[:space:]]*\\(" { inside = 1; found = 0; seen = 1; next }
      inside && /pageInfo/ { found = 1 }
      inside && /nodes/ { inside = 0; if (!found) bad = 1 }
      END { exit((seen && !bad) ? 0 : 1) }'; then
    printf '  ok    %s asks for pageInfo\n' "$connection"; pass=$((pass + 1))
  else
    printf '  FAIL  %s does not ask for pageInfo, so its truncation is invisible again\n' "$connection"
    fail=$((fail + 1))
  fi
done
# A page over a hundred is not a bigger page, it is a GraphQL error -- and an
# error here is a fail-closed HOLD on every pull request, 48 times a day.
if [ -z "$(printf '%s\n' "$QUERY_BODY" | grep -oE 'first: *[0-9]+' | grep -oE '[0-9]+' | awk '$1 > 100')" ]; then
  printf '  ok    no connection asks for more than the 100 GitHub allows\n'; pass=$((pass + 1))
else
  printf '  FAIL  a connection asks for more than 100, which GitHub rejects outright\n'; fail=$((fail + 1))
fi
# a192a89 separated the two things the rollup holds. Keep it separated.
if grep -q 'on CheckRun' "$FACTS_QUERY" && grep -q 'on StatusContext' "$FACTS_QUERY" \
   && grep -q '__typename' "$FACTS_QUERY"; then
  printf '  ok    a commit status is still distinguishable from a check run\n'; pass=$((pass + 1))
else
  printf '  FAIL  the query no longer separates CheckRun from StatusContext (a192a89)\n'; fail=$((fail + 1))
fi

echo
echo "The rule does not grow a seventh condition or lose one of the six"
for condition in 'ai-review:pass' 'ai-review:blocking' 'agent:blocked' 'needs-owner' \
                 'MIN_HEAD_AGE_SECONDS' 'mergeable' 'is_draft' 'facts_complete'; do
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
echo "A rule nothing calls, and nothing tracks, is the state this must never be in"
# THIS IS THE ASSERTION THAT KEEPS THE FIX HONEST WHILE HALF OF IT IS PARKED.
#
# Agents here run as `claude[bot]`, and that installation token holds no
# `workflows` permission -- a push touching `.github/workflows/` is refused by
# the remote, verified on 2026-08-24:
#
#   ! [remote rejected] (refusing to allow a GitHub App to create or update
#     workflow `.github/workflows/pr-merge-sweep.yml` without `workflows`
#     permission)
#
# So the caller half of #170 travels as a patch. There are exactly two states
# this repository may be in, and this asserts it is in one of them: either the
# sweep already calls the rule, or the patch that makes it do so is still here
# waiting. What it refuses is the third state -- a tested rule, an unchanged
# caller, and nothing on disk that remembers the two are meant to meet.
SWEEP=.github/workflows/pr-merge-sweep.yml
PENDING=docs/automation/pending/170-merge-sweep-completeness.patch
if grep -q 'merge-facts.sh' "$SWEEP" 2>/dev/null; then
  printf '  ok    the sweep calls the rule that has a test\n'; pass=$((pass + 1))
  if [ -e "$PENDING" ]; then
    printf '  FAIL  the sweep calls the rule, so %s has landed and should be deleted\n' "$PENDING"
    fail=$((fail + 1))
  else
    printf '  ok    and the pending patch has been cleared away\n'; pass=$((pass + 1))
  fi
elif [ -f "$PENDING" ]; then
  printf '  ok    the sweep does not call the rule yet, and %s says so\n' "$PENDING"
  pass=$((pass + 1))
  if git --no-pager apply --check "$PENDING" >/dev/null 2>&1; then
    printf '  ok    and that patch still applies cleanly to this tree\n'; pass=$((pass + 1))
  else
    printf '  FAIL  %s no longer applies; the parked half has rotted\n' "$PENDING"
    fail=$((fail + 1))
  fi
else
  printf '  FAIL  merge-facts.sh is tested but nothing calls it and no patch is pending\n'
  fail=$((fail + 1))
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
