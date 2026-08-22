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
     "within the" "hour" '`@claude`'
says "warns against retrying a deterministic failure" -- "$FAILED" -- \
     "same failure with a bill"

UNKNOWN=$(attadipa_outcome something-else "$RUN")
says "an unrecognised state is reported as a reporting defect, not swallowed" -- \
     "$UNKNOWN" -- "unrecognised state"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
