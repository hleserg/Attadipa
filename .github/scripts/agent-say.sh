#!/usr/bin/env bash
# What the agent says out loud, and when.
#
# THE PROBLEM THIS SOLVES. Before 2026-08-22 a task could be accepted, worked on
# for forty minutes and finished, and the only trace on the issue was a label
# changing colour. On a phone that is invisible. The owner's experience of a
# working pipeline and a broken one was identical — silence — and they had to
# ask an agent to go and read run logs to tell the difference. Twice in one
# morning the answer was "it is working, look at the labels", and twice that was
# a bad answer.
#
# So the pipeline now answers every time, at three fixed points:
#
#   1. RECEIPT   — within seconds of the trigger. "Accepted, here is what I
#                  understood, here is the run, here is what happens next."
#   2. PROGRESS  — from the agent itself, when it has a plan and when the plan
#                  changes. Bounded, because a comment that says nothing new is
#                  worse than none.
#   3. OUTCOME   — always, whatever happened. A pull request and what is now
#                  being waited on; a BLOCKED with what it needs; or a failure
#                  with the log and what happens next.
#
# The rule underneath all three: NEVER LEAVE A REQUEST UNANSWERED. Silence reads
# as "thinking", and thinking is indistinguishable from dead.
#
# These are pure text renderers. No network, no `gh`, no environment — every
# input is an argument, which is what lets .github/tests/agent-say-test.sh
# assert on the exact text rather than on the workflow's intentions.

# attadipa_receipt RUN_URL TASK_TYPE PRIORITY RESEARCH_ONLY TRIGGER ACTOR STALENESS
#
# Posted by the `acknowledge` job the moment the gate accepts, in parallel with
# the agent starting. It is deliberately not posted by the agent: an agent that
# has to be running before it can say "I am running" cannot report the case
# where it never started.
attadipa_receipt() {
  local run_url="$1" task_type="$2" priority="$3" research_only="$4"
  local trigger="$5" actor="$6" staleness="$7"

  local mode next
  if [ "$research_only" = "true" ]; then
    mode="research only — no implementation code"
    next="verify sources, write to \`docs/research/\`, update the reuse ledger, open a documentation pull request"
  else
    mode="implementation"
    next="work on a branch, run the checks that can run here, open a **draft pull request**"
  fi

  local how
  case "$trigger" in
    issue_comment|pull_request_review_comment|pull_request_review)
      how="\`@claude\` from \`$actor\`" ;;
    issues)
      how="the \`agent:ready\` label, or an assignment" ;;
    workflow_dispatch)
      how="the hourly watchdog, which found this task waiting" ;;
    *)
      how="\`$trigger\`" ;;
  esac

  echo "<!-- attadipa-receipt -->"
  echo "### Accepted — an agent is working on this now"
  echo
  echo "| | |"
  echo "|---|---|"
  echo "| started by | $how |"
  echo "| kind | \`$task_type\` |"
  echo "| priority | \`$priority\` |"
  echo "| mode | $mode |"
  echo "| run | [live log]($run_url) |"
  echo
  echo "**Next:** $next."
  echo
  if [ -n "$staleness" ]; then
    echo "**Against which code:** $staleness"
    echo
  fi
  echo "You will get another comment here whichever way this ends — a pull"
  echo "request, a \`BLOCKED:\` saying what it needs, or a failure with the log."
  echo "If this issue is still \`agent:working\` in two hours with nothing new,"
  echo "the watchdog returns it to the queue and says so; nothing is left"
  echo "silently stuck."
}

# attadipa_outcome KIND RUN_URL DETAIL
#
# Posted by the `Hand over` step, always, on every exit path.
#
# KIND is one of:
#   done_pr    DETAIL is the pull request number, no `#`
#   done_nopr  DETAIL is unused
#   failed     DETAIL is the conclusion word from the action
attadipa_outcome() {
  local kind="$1" run_url="$2" detail="${3:-}"

  echo "<!-- attadipa-outcome -->"
  case "$kind" in
    done_pr)
      echo "### Done — pull request #$detail"
      echo
      echo "The work is in #$detail, and this issue closes when it merges."
      echo
      echo "**Now waiting on:** CI on that pull request, plus the"
      echo "independent review — a fresh context that did not write the code."
      echo "Both run automatically and neither needs you."
      echo
      echo "**When it needs you:** if the review labels it \`ai-review:blocking\`"
      echo "and the finding is a product decision rather than a defect, or if the"
      echo "pull request carries \`needs-owner\`. Otherwise it is merged without"
      echo "asking — owner decision, 2026-08-21."
      echo
      echo "[Run log]($run_url)"
      ;;
    done_nopr)
      echo "### Finished, and opened no pull request"
      echo
      echo "The run ended cleanly but produced no branch and no pull request."
      echo "That is a real outcome in two cases and a defect in every other:"
      echo
      echo "* the finding was **verified against current code and did not hold**"
      echo "  — the comment above should say so with a file and a line;"
      echo "* the work was **already done** by something merged since."
      echo
      echo "If neither is written above, this run did nothing and the honest"
      echo "reading is that it failed quietly. [Run log]($run_url) — and"
      echo "\`@claude\` here starts it again."
      ;;
    failed)
      echo "### The run did not finish"
      echo
      echo "It ended as \`$detail\` rather than reaching a conclusion. Nothing was"
      echo "left half-applied on this issue: the claim is released and the task is"
      echo "back in the queue."
      echo
      echo "**What happens without you:** the watchdog picks it up within the"
      echo "hour. **To start it now:** comment \`@claude\`."
      echo
      echo "[Run log]($run_url) — the cause is in there, and it is worth reading"
      echo "before retrying, because a retry of a deterministic failure is the"
      echo "same failure with a bill attached."
      ;;
    *)
      echo "### The run ended in an unrecognised state (\`$kind\`)"
      echo
      echo "This is a defect in the reporting itself rather than in the task."
      echo "[Run log]($run_url)"
      ;;
  esac
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  what="${1:-}"; shift || true
  case "$what" in
    receipt) attadipa_receipt "$@" ;;
    outcome) attadipa_outcome "$@" ;;
    *) echo "usage: agent-say.sh receipt|outcome ..." >&2; exit 2 ;;
  esac
fi
