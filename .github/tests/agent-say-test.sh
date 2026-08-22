#!/usr/bin/env bash
# Does the pipeline actually say the thing?
#
# The receipt and the outcome comment exist because silence and success looked
# identical from the outside. A renderer that quietly drops the pull request
# number, or reports an implementation task as research, puts the pipeline back
# where it was while looking like it did not. So the text is asserted, not the
# intention — same reason .github/tests/intake-gate-test.sh executes the gate.
#
# The needles below are literal Markdown carrying backticks and @ signs, so they
# are single-quoted throughout: nothing in them is meant to expand, and inside
# double quotes a backtick is command substitution rather than a code span.
# shellcheck disable=SC2016
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source-path=SCRIPTDIR
# shellcheck source=../scripts/agent-say.sh
. "$here/../scripts/agent-say.sh"

pass=0; fail=0
RUN="https://github.com/hleserg/Attadipa/actions/runs/1"

# says DESCRIPTION -- TEXT -- NEEDLE...
says() {
  local desc="$1"; shift 2
  local text="$1"; shift 2
  local missing=""
  for needle in "$@"; do
    case "$text" in
      *"$needle"*) ;;
      *) missing="$missing\n         missing: $needle" ;;
    esac
  done
  if [ -z "$missing" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s%b\n' "$desc" "$missing"
  fi
}

# lacks DESCRIPTION -- TEXT -- NEEDLE...
lacks() {
  local desc="$1"; shift 2
  local text="$1"; shift 2
  local found=""
  for needle in "$@"; do
    case "$text" in
      *"$needle"*) found="$found\n         should not contain: $needle" ;;
    esac
  done
  if [ -z "$found" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s%b\n' "$desc" "$found"
  fi
}

echo "The receipt — what an owner sees within seconds of asking"

IMPL=$(attadipa_receipt "$RUN" continuous-review P1 false issue_comment hleserg \
       "main is 3 commit(s) ahead of the reviewed commit abc123.")
says "carries the marker so it can be found and deduplicated" -- "$IMPL" -- \
     "<!-- attadipa-receipt -->"
says "says it was accepted, in the first line, not the fourth" -- "$IMPL" -- \
     "### Accepted"
says "names who asked and how" -- "$IMPL" -- '`@claude` from `hleserg`'
says "carries the run link, because 'it is working' is not evidence" -- "$IMPL" -- "$RUN"
says "repeats back what it understood" -- "$IMPL" -- \
     '`continuous-review`' '`P1`'
says "an implementation task promises a draft pull request" -- "$IMPL" -- \
     "draft pull request" "implementation"
says "passes on the staleness the gate computed" -- "$IMPL" -- \
     "3 commit(s) ahead"
says "promises a second comment either way — the whole point" -- "$IMPL" -- \
     "whichever way this ends"
says "says what happens if it goes quiet" -- "$IMPL" -- "watchdog"

RESEARCH=$(attadipa_receipt "$RUN" next-task-research P2 true issues "" "")
says "a research task promises documentation" -- "$RESEARCH" -- \
     "research only" "docs/research/" "documentation pull request"
lacks "and never promises implementation" -- "$RESEARCH" -- \
     "implementation code — no" "work on a branch"
says "a label-triggered run says so rather than inventing an author" -- "$RESEARCH" -- \
     '`agent:ready` label'
lacks "no staleness line when the gate had nothing to say" -- "$RESEARCH" -- \
     "Against which code:"

DISPATCH=$(attadipa_receipt "$RUN" unspecified P2 false workflow_dispatch github-actions "")
says "a watchdog handover is named as one" -- "$DISPATCH" -- "hourly watchdog"

# workflow_dispatch is not only the watchdog — the refusal comment tells people
# to use it as a recovery path, and the gate trusts the event because GitHub
# only accepts a manual one from an actor with write access.
MANUAL=$(attadipa_receipt "$RUN" quality-audit P1 false workflow_dispatch hleserg "")
says "a person who dispatched it by hand is not told a watchdog found it" -- \
     "$MANUAL" -- "manual run" '`hleserg`'
lacks "and the watchdog is not credited with their work" -- "$MANUAL" -- \
     "hourly watchdog"

echo
echo "The outcome — always, on every exit path"

DONE=$(attadipa_outcome done_pr "$RUN" 51)
says "leads with the pull request number" -- "$DONE" -- "pull request #51"
says "says the issue closes on merge" -- "$DONE" -- "closes when it merges"
says "answers 'what is it waiting for now?' explicitly" -- "$DONE" -- \
     "Now waiting on:" "independent review"
says "and says when it will actually need the owner" -- "$DONE" -- \
     "When it needs you:" 'ai-review:blocking' "needs-owner"
says "carries the marker" -- "$DONE" -- "<!-- attadipa-outcome -->"

NOPR=$(attadipa_outcome done_nopr "$RUN")
says "a clean run with no pull request is not reported as success" -- "$NOPR" -- \
     "no pull request was found" "failed quietly"
says "names the three cases where that is legitimate" -- "$NOPR" -- \
     "did not hold" "already done" "does not mention this issue"
# The review on #58 found this one: research-only tasks are not required to put
# `Fixes #N` in the pull request body, so a perfectly good documentation pull
# request can go undetected — and the old wording then called a successful run a
# silent failure AND told the reader to comment `@claude`, which the gate does
# not deduplicate for comment events. That is a second billed run and a second
# competing pull request, produced by the message meant to prevent exactly that.
lacks "never tells the reader to re-run without checking first" -- "$NOPR" -- \
     "starts it again"
says "tells them to look for a pull request before doing anything" -- "$NOPR" -- \
     "Check for an open pull request before doing anything else"
says "and says plainly why a second @claude is dangerous here" -- "$NOPR" -- \
     "second agent" "not deduplicated"

FAILED=$(attadipa_outcome failed "$RUN" cancelled)
says "reports the actual conclusion word" -- "$FAILED" -- '`cancelled`'
says "says the claim was released, so nobody has to check" -- "$FAILED" -- \
     "claim is released"
says "says what happens without the owner, and what starts it now" -- "$FAILED" -- \
     "one retry" "hour" '`@claude`'
says "says the retry is bounded rather than promising an unconditional pick-up" -- "$FAILED" -- \
     "fails a second time" "needs-owner"
says "warns against retrying a deterministic failure" -- "$FAILED" -- \
     "same failure" "with a bill attached"
# The old text sent the reader to the run log for the cause. That log is
# emptied by `show_full_output: false` on purpose, so the advice was sound and
# the address was wrong; sending somebody to a redacted log to find out why a
# run died is how #67 cost an afternoon.
says "does not send anybody to the run log for a cause it does not contain" -- \
     "$FAILED" -- "redacted by" "design"
says "and a reason that never arrived is called a defect rather than glossed" -- \
     "$FAILED" -- "Why: not established"

# The fifth argument is the whole point of .github/scripts/failure-reason.sh
# reaching the issue at all.
FAILED_WHY=$(attadipa_outcome failed "$RUN" cancelled "" "API Error: 400 prompt is too long: 214233 tokens > 200000 maximum")
says "carries the reason the extractor found, in the reader's first paragraph" -- \
     "$FAILED_WHY" -- "**Why:**" "prompt is too long: 214233 tokens"
says "and still says what happens next, because a cause is not a plan" -- \
     "$FAILED_WHY" -- "watchdog gives it one retry" '`@claude`'
# An unclassified reason must not read as a dead end: it is a gap in a
# whitelist, and saying which file to widen is what makes it actionable.
FAILED_UNCLASS=$(attadipa_outcome failed "$RUN" cancelled "" "unclassified — SDK subtype \`success\`, ended at turn 20")
says "an unclassified reason names the file that has to be widened" -- \
     "$FAILED_UNCLASS" -- "unclassified" "failure-reason.sh"

# No reason at all is the pre-existing shape and must still render.
says "and a caller that passes no reason gets no empty Why heading" -- \
     "$FAILED" -- "did not finish"
case "$FAILED" in
  *"**Why:**"*) echo "  FAIL  an absent reason must not print a Why heading"; fail=$((fail + 1)) ;;
  *) echo "  ok    an absent reason prints no Why heading"; pass=$((pass + 1)) ;;
esac

UNKNOWN=$(attadipa_outcome something-else "$RUN")
says "an unrecognised state is reported as a reporting defect, not swallowed" -- \
     "$UNKNOWN" -- "unrecognised state"

# A pull request that started its own agent. Reporting it as "the work is in
# #71" when the comment is being posted ON #71 sends the reader looking for a
# pull request that is the one they are reading.
HERE=$(attadipa_outcome done_here "$RUN" 71)
says "says the work landed on this pull request, not a second one" -- "$HERE" -- \
     "pushed to this pull request" "#71" "no second"
says "says the old review verdict does not carry over to the new head" -- \
     "$HERE" -- "previous verdict" "says nothing about this one"

# The #71 defect: the caller's lookup failed and handed this a GraphQL error
# document, which went out as "### Done — pull request #{"data":...".
BAD=$(attadipa_outcome done_pr "$RUN" '{"data":{"repository":{"issue":null}}}')
says "a pull request number that is not a number is refused, not printed" -- \
     "$BAD" -- "could not name the result"
case "$BAD" in
  *'{"data"'*) says "FORCED FAIL: the error document was printed" -- "" -- "x" ;;
  *) says "and the error document itself does not reach the comment" -- "$BAD" -- \
          "Run log" ;;
esac
# The review finding on the first version of done_here: a run that pushed
# nothing must not claim a push. The wording has to be usable on its own,
# because it is the only thing a person reading the pull request will see.
NOPUSH=$(attadipa_outcome done_here_nopush "$RUN" 71)
says "says plainly that nothing was pushed" -- "$NOPUSH" -- "pushed nothing" "#71"
says "and that there is therefore no new CI and no new review" -- "$NOPUSH" -- \
     "no new CI result" "no new review"
says "offers the three readings rather than asserting one" -- "$NOPUSH" -- \
     "nothing to change" "out of scope" "did not get to the work"
says "and warns that a second @claude is a second billed agent" -- "$NOPUSH" -- \
     "second billed agent"

# The second review's finding: a moved head on a run that never finished is
# neither "done" nor "nothing happened". Real work is on the branch AND it may
# be half of it.
# #76: a closing reference must not launder a dead run into a success. The
# outcome says both true things instead of picking one.
PRCUT=$(attadipa_outcome done_pr_cut "$RUN" 71 failure)
says "names the pull request and says the run did not finish" -- "$PRCUT" -- \
     "#71" "did not finish" '`failure`'
says "warns it may be part of the task rather than all of it" -- "$PRCUT" -- \
     "part of the task rather than all of it" "which part"
says "and sends the reader to the acceptance criteria, not back to the issue" -- \
     "$PRCUT" -- "acceptance criteria" "rather than reopening the work here"

CUT=$(attadipa_outcome done_here_cut "$RUN" 71 cancelled)
says "says a commit landed and the run did not finish, both" -- "$CUT" -- \
     "commit landed" "did not finish" "#71"
says "names the conclusion word rather than gesturing at the log" -- "$CUT" -- \
     '`cancelled`'
says "warns that it may be half the work and nothing here knows which half" -- \
     "$CUT" -- "may be half" "which half"
says "and that green CI proves nothing about the part never written" -- "$CUT" -- \
     "never got written"
# Review's third-round finding: the first version of this case added agent:ready
# alongside agent:review, and agent:ready is inert on a pull request -- the
# watchdog's queue scan drops pull requests before it reads a label. The label
# is gone; saying so is what replaces it, because words are the only thing that
# reaches a person here.
says "says plainly that nothing automated will come back for the rest" -- "$CUT" -- \
     "on its own" "watchdog scans issues, not pull requests" '`@claude`'
CUT_NO=$(attadipa_outcome done_here_cut "$RUN" 71 "")
says "a missing conclusion is named rather than left as an empty quote" -- \
     "$CUT_NO" -- "no conclusion"

EMPTY=$(attadipa_outcome done_here "$RUN" "")
says "an empty detail is refused the same way" -- "$EMPTY" -- \
     "could not name the result"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
