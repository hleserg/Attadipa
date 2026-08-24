#!/usr/bin/env bash
# Restarting a blocked task must produce an outcome, not silence.
#
# THE PAIR, AGAIN. The watchdog's bounded retry escalates a task that has failed
# more than once since a person queued it: agent:blocked + needs-owner, plus a
# comment telling the reader how to start it again. The comment leads with
# `@claude`, because a comment event is the only start that does not need label
# surgery -- intake-decision.sh gates its claimed-state check on the `issues`
# and `workflow_dispatch` events only.
#
# It starts. It does not finish. `claude-agent.yml`'s Hand over step re-reads
# the labels at the end and, on finding agent:blocked, concludes the AGENT
# blocked itself during this run, writes no outcome comment, applies no
# agent:review and no agent:failed+agent:ready, and exits. It has no way to tell
# that apart from a label that was already there when the run started -- so the
# person who followed the instructions got a run that did the work and reported
# nothing. Found in review of #85; neither half's own tests could see it,
# because the escalation copy and the Hand over check live in different files
# and each is correct on its own.
#
# The first fix was one word in a list: the claim step stripped agent:blocked at
# the start, so agent:blocked at hand-over time could only mean this run applied
# it. THE SECOND REVIEW OF #85 SHOWED THAT ARGUMENT IS ISSUE-ONLY. Three of this
# workflow's five triggers fire on PULL REQUESTS, where claude-ci-repair.yml
# writes `ci:failed` + `agent:blocked` when repair gives up and asks for a
# human -- so the label CAN predate the run, and stripping it there would clear
# a human escalation and hand the branch to the unattended backstop, which
# requires agent:blocked absent and does not look at ci:failed.
#
# So the fact is recorded rather than inferred: the claim step reads the labels
# BEFORE it changes any, writes ATTADIPA_BLOCKED_BEFORE, and strips
# agent:blocked from issues only. Hand over branches on that variable. This file
# asserts the whole chain, because any one link silently restores the silence.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }

AGENT=.github/workflows/claude-agent.yml
WATCHDOG=.github/workflows/agent-queue-watchdog.yml
INTAKE=.github/scripts/intake-decision.sh

echo "The claim step clears the label that would swallow this run's outcome"

# The stale-label list, read out of the file rather than assumed. It is now
# built as an array with one conditional member, so both the base list and the
# conditional addition are read.
STALE="$(sed -n 's/^[[:space:]]*stale_labels=(\(.*\))/\1/p' "$AGENT" | head -n 1)"
STALE_EXTRA="$(sed -n 's/^[[:space:]]*stale_labels+=(\(.*\))/\1/p' "$AGENT" | tr '\n' ' ')"
if [ -z "$STALE" ]; then
  no "the claim step still has a stale-label list" \
     "no 'stale_labels=(...)' found in $AGENT -- if the claim moved, point this test at it rather than deleting it"
else
  ok "the claim step still has a stale-label list ($STALE${STALE_EXTRA:+ + $STALE_EXTRA})"
  case " $STALE $STALE_EXTRA " in
    *" agent:blocked "*)
      ok "and it still clears agent:blocked somewhere, so the issue path keeps working" ;;
    *)
      no "and it still clears agent:blocked somewhere, so the issue path keeps working" \
         "agent:blocked is cleared nowhere; an issue the watchdog escalated and a person restarted with @claude will run to completion and report NOTHING -- no outcome comment, no label, no re-queue" ;;
  esac
  # ...but NOT unconditionally. On a pull request that label is
  # claude-ci-repair.yml's escalation to a human, and the unattended backstop
  # merges on its absence.
  case " $STALE " in
    *" agent:blocked "*)
      no "and it does not strip agent:blocked from a pull request unconditionally" \
         "agent:blocked is in the unconditional list; on a pull request that is claude-ci-repair.yml's escalation to a human, and removing it makes the branch backstop-eligible while ci:failed -- which the backstop does not read -- stays put" ;;
    *)
      case " $STALE_EXTRA " in
        *" agent:blocked "*)
          ok "and it does not strip agent:blocked from a pull request unconditionally" ;;
        *)
          no "and it does not strip agent:blocked from a pull request unconditionally" \
             "agent:blocked appears in neither list; see the assertion above" ;;
      esac ;;
  esac
  # The other direction: clearing too much is its own defect.
  case " $STALE " in
    *" needs-owner "*)
      no "and it does not clear needs-owner" \
         "needs-owner is a decision only the owner can make; starting a run does not make that decision" ;;
    *) ok "and it does not clear needs-owner" ;;
  esac
  case " $STALE " in
    *" agent:working "*)
      no "and it does not clear agent:working, which it is in the middle of applying" \
         "the claim adds agent:working; removing it in the same step is a race with itself" ;;
    *) ok "and it does not clear agent:working, which it is in the middle of applying" ;;
  esac
fi

echo
echo "Hand over still bails on agent:blocked -- that half is deliberate"
if grep -q '\*,agent:blocked,\*' "$AGENT"; then
  ok "the Hand over step still treats agent:blocked as the agent's own word"
else
  no "the Hand over step still treats agent:blocked as the agent's own word" \
     "the check is gone; an agent that blocks itself will now be talked over by a generated outcome comment"
fi

echo
echo "...and it asks a recorded fact rather than inferring one"

# THE HALF THE SECOND REVIEW FOUND. Bailing on agent:blocked is right only when
# THIS run applied it. Every other case -- a pull request carrying
# claude-ci-repair.yml's escalation, an issue whose read failed -- must produce
# an outcome, because silence is the defect this whole file is about.
if grep -q 'ATTADIPA_BLOCKED_BEFORE=' "$AGENT"; then
  ok "the claim step records whether agent:blocked was there before the run"
else
  no "the claim step records whether agent:blocked was there before the run" \
     "nothing writes ATTADIPA_BLOCKED_BEFORE; Hand over is back to inferring that the label is this run's, which is false on every pull request claude-ci-repair.yml has escalated"
fi

BAIL="$(sed -n '/\*,agent:blocked,\*/,/esac/p' "$AGENT")"
if printf '%s' "$BAIL" | grep -q 'ATTADIPA_BLOCKED_BEFORE'; then
  ok "and Hand over's bail is gated on it"
else
  no "and Hand over's bail is gated on it" \
     "the bail does not consult ATTADIPA_BLOCKED_BEFORE; a run started by @claude on a pull request that repair escalated will do the work and report nothing"
fi

# Fail toward reporting: an unreadable pre-run state must not buy silence.
if printf '%s' "$BAIL" | grep -q '= "false"'; then
  ok "and only an explicit false -- unknown reports rather than falls silent"
else
  no "and only an explicit false -- unknown reports rather than falls silent" \
     "the bail is not gated on the value being exactly \"false\"; an unreadable read would restore the silence it was added to remove"
fi

echo
echo "Every label edit knows which object it is editing"

# gh issue edit resolves repository.issue(number:), which does not resolve pull
# requests -- this repository has already had an error document from that field
# end up inside an outcome comment. Three of five triggers here fire on pull
# requests, and every call is `|| true`, so neither outcome would be reported.
HANDOVER_STEP="$(sed -n '/name: Hand over/,$p' "$AGENT")"
if printf '%s' "$HANDOVER_STEP" | grep -qE '^\s*gh issue (edit|comment) '; then
  no "the Hand over step edits and comments through the object-aware helper" \
     "a bare 'gh issue edit/comment' survives in Hand over; on a pull request it either silently edits one while reasoning about issues, or silently does nothing -- see .github/scripts/gh-label.sh"
else
  ok "the Hand over step edits and comments through the object-aware helper"
fi

if [ -f .github/scripts/gh-label.sh ] && printf '%s' "$HANDOVER_STEP" | grep -q 'attadipa_label_edit'; then
  ok "and that helper exists and is the one it calls"
else
  no "and that helper exists and is the one it calls" \
     ".github/scripts/gh-label.sh is missing or unused; the split it encodes is the only thing keeping the pull request path honest"
fi

echo
echo "The escalation the check has to survive still exists"
if grep -q 'add-label agent:blocked' "$WATCHDOG"; then
  ok "the watchdog still escalates a repeatedly-failing task to agent:blocked"
else
  no "the watchdog still escalates a repeatedly-failing task to agent:blocked" \
     "the bounded retry no longer produces agent:blocked; if the escalation moved, re-derive whether the claim step still needs to clear it"
fi

ESCALATION="$(sed -n '/gh issue comment/,/|| true/p' "$WATCHDOG" | tr -d '\\`')"
if printf '%s' "$ESCALATION" | grep -q 'comment @claude'; then
  ok "and still offers the @claude route, which is the one this file exists for"
else
  no "and still offers the @claude route, which is the one this file exists for" \
     "if that route was withdrawn the defect is gone by another road -- delete this file deliberately rather than leaving it asserting a path nobody is told to take"
fi

echo
echo "And the @claude route really does reach the agent"
# The premise the escalation comment states in words: a comment event skips the
# claimed-state check. If the gate ever starts checking it for comments too, the
# comment is wrong and this test should say so before a person follows it.
if grep -qE 'issues|workflow_dispatch' "$INTAKE"; then
  ok "$INTAKE still scopes its claimed-state check by event"
else
  no "$INTAKE still scopes its claimed-state check by event" \
     "the gate no longer distinguishes events; re-read whether a comment on a blocked issue still starts a run at all"
fi

echo
echo "A finished run does not leave its pull request a draft"
# THE SECOND WAY WORK STOPS SHORT OF AN ANSWER. The prompt says to open the
# pull request as a draft while it is still moving and to mark it ready when it
# is not, and the second half was left to the model remembering. On 2026-08-22
# five finished pull requests -- #88, #92, #94, #95, #97 -- sat as drafts with
# green CI for hours. Nothing merges a draft, so six issues stayed open with
# their fix already written and their `Fixes #N` already in place, and the
# owner had to ask why. The step that decides the run is done marks it ready
# instead of asking the model to.
HANDOVER="$(sed -n '/read -r KIND/,/agent:failed --add-label agent:ready/p' "$AGENT")"
if printf '%s' "$HANDOVER" | grep -q 'gh pr ready'; then
  ok "the hand-over step marks the pull request ready for review"
else
  no "the hand-over step marks the pull request ready for review" \
     "no 'gh pr ready' in the hand-over; a finished pull request stays a draft until a person notices, and nothing merges a draft"
fi

# The other direction: promoting a half-finished pull request is worse than
# leaving it. A cut-off run is exactly what a draft is for.
#
# THIS ASSERTION USED TO BE UNABLE TO FAIL, which review of #85 caught and
# which is worth spelling out because the shape recurs. It was
# `grep -A3 'gh pr ready' | grep -q done_pr_cut`: `gh pr ready` occurs once, so
# -A3 saw the command and the two echoes AFTER it — while the `case "$KIND" in`
# and the `done_pr|done_here)` selector that decide which kinds reach it are
# ABOVE the match. Adding done_pr_cut to the selector left the test printing
# `ok`. So the guard is anchored on the case block instead: extract the whole
# `case "$KIND" in ... esac` that contains `gh pr ready`, and read its
# selectors. A test that cannot fail is worse than no test, in a file whose
# whole argument is that cross-file properties must be asserted.
READY_CASE="$(printf '%s\n' "$HANDOVER" | awk '
  /case "\$KIND" in/ { inblock = 1; buf = "" }
  inblock              { buf = buf $0 "\n" }
  /^[[:space:]]*esac/  { if (inblock && buf ~ /gh pr ready/) printf "%s", buf; inblock = 0 }
')"

if [ -n "$READY_CASE" ]; then
  ok "the 'gh pr ready' call sits in a case on \$KIND whose selectors can be read"
else
  no "the 'gh pr ready' call sits in a case on \$KIND whose selectors can be read" \
     "could not find a 'case \"\$KIND\" in ... esac' containing 'gh pr ready'; the guards below would pass vacuously, which is exactly the defect review of #85 found"
fi

# Now the selectors themselves, read from the lines that actually choose the
# kinds rather than from three lines of trailing echo.
READY_SELECTORS="$(printf '%s\n' "$READY_CASE" | grep -E '^[[:space:]]*[a-z_|]+\)[[:space:]]*$' || true)"

if printf '%s' "$READY_SELECTORS" | grep -q 'done_pr_cut\|done_here_cut'; then
  no "and it does not promote a cut-off run" \
     "a *_cut kind selects the 'gh pr ready' branch; half-finished work must stay a draft"
else
  ok "and it does not promote a cut-off run"
fi

if printf '%s' "$READY_SELECTORS" | grep -q 'done_here_nopush\|done_nopr'; then
  no "and it does not promote a run with nothing to promote" \
     "done_here_nopush pushed nothing and done_nopr has no pull request; neither can be marked ready"
else
  ok "and it does not promote a run with nothing to promote"
fi

# AND IT DOES NOT PROMOTE A PULL REQUEST THIS RUN DOES NOT OWN. For done_pr the
# number came from a closing keyword anybody can write, including an abandoned
# branch; undrafting is what makes a branch backstop-eligible, so it needs the
# ownership evidence handover-decision.sh already demands for done_here.
if printf '%s' "$HANDOVER" | grep -q 'promote-decision.sh'; then
  ok "and it asks promote-decision.sh whether this run owns the pull request"
else
  no "and it asks promote-decision.sh whether this run owns the pull request" \
     "nothing consults .github/scripts/promote-decision.sh; done_pr's number comes from whichever open pull request says 'Fixes #N', which an abandoned branch also says"
fi

if grep -q 'ATTADIPA_RUN_STARTED_AT=' "$AGENT"; then
  ok "and the run's start time it dates that against is recorded before the agent runs"
else
  no "and the run's start time it dates that against is recorded before the agent runs" \
     "nothing writes ATTADIPA_RUN_STARTED_AT; promote-decision.sh would hold every done_pr, leaving finished pull requests as drafts"
fi

if printf '%s' "$HANDOVER" | grep -q 'gh pr ready.*--undo'; then
  no "and it never puts one back to draft" \
     "--undo would overrule a person who deliberately marked the pull request draft again"
else
  ok "and it never puts one back to draft"
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
