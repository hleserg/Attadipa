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
# The fix is one word in a list: the claim step strips agent:blocked at the
# start, so agent:blocked at hand-over time can only mean this run applied it.
# This file asserts the whole chain, because any one link silently restores the
# silence.

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

# The stale-label loop, read out of the file rather than assumed.
STALE="$(sed -n 's/^[[:space:]]*for stale in \(.*\); do/\1/p' "$AGENT" | head -n 1)"
if [ -z "$STALE" ]; then
  no "the claim step still has a stale-label loop" \
     "no 'for stale in ... ; do' found in $AGENT -- if the claim moved, point this test at it rather than deleting it"
else
  ok "the claim step still has a stale-label loop ($STALE)"
  case " $STALE " in
    *" agent:blocked "*)
      ok "and it clears agent:blocked, so Hand over's check means what it says" ;;
    *)
      no "and it clears agent:blocked, so Hand over's check means what it says" \
         "agent:blocked is not in the stale list; an issue the watchdog escalated and a person restarted with @claude will run to completion and report NOTHING -- no outcome comment, no label, no re-queue" ;;
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
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
