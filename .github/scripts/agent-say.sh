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
      # Not always the watchdog, and saying so when it was a person is the
      # small kind of wrong this whole protocol exists to stop. A manual
      # dispatch is a documented recovery path — the refusal comment tells
      # people to use it — and the gate trusts the event precisely because
      # GitHub only accepts one from an actor with write access.
      case "$actor" in
        github-actions|"github-actions[bot]"|"")
          how="the hourly watchdog, which found this task waiting" ;;
        *)
          how="a manual run of the \`Claude agent\` workflow by \`$actor\`" ;;
      esac ;;
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
#   done_pr_cut       same, but the run did not finish -- EXTRA is the conclusion
#   done_here  DETAIL is the pull request number this comment is being posted on
#   done_here_cut     same, but the run did not finish -- EXTRA is the conclusion
#   done_here_nopush  same, but the head did not move -- it ran and pushed nothing
#   done_nopr  DETAIL is unused
#   failed     DETAIL is the conclusion word from the action
#
# `done_pr` and `done_here` check that DETAIL is a number before saying it is
# one. That is not defensive habit: on 2026-08-22 the caller's lookup failed on a
# pull-request trigger and handed this function a GraphQL error document, which
# went out verbatim as "### Done — pull request #{"data":{"repository": ...".
# The caller is fixed; a renderer that prints whatever it is given as a pull
# request number would let the next such bug out too.
# The fifth argument, REASON, is one line from
# .github/scripts/failure-reason.sh, and it is the difference between a failure
# comment that helps and one that does not. Only the failure paths use it.
attadipa_outcome() {
  local kind="$1" run_url="$2" detail="${3:-}" extra="${4:-}" reason="${5:-}"

  case "$kind" in
    done_pr|done_pr_cut|done_here|done_here_cut|done_here_nopush)
      case "$detail" in
        ""|*[!0-9]*) kind=bad_detail ;;
      esac ;;
  esac

  echo "<!-- attadipa-outcome -->"
  case "$kind" in
    done_pr_cut)
      echo "### Pull request #$detail exists, and the run that made it did not finish"
      echo
      echo "#$detail says it closes this issue, so real work is on a branch — this"
      echo "is not a run that did nothing. But it ended as \`${extra:-no conclusion}\`"
      echo "rather than reaching a conclusion, so **what is in that pull request may"
      echo "be part of the task rather than all of it**, and nothing here can tell"
      echo "you which part."
      echo
      echo "**Read the pull request against this issue's acceptance criteria before"
      echo "the review does.** A partial change that compiles is the expensive kind:"
      echo "CI will go green on it and say nothing about the requirement that was"
      echo "never implemented. If it is incomplete, say what is missing on the pull"
      echo "request rather than reopening the work here."
      echo
      echo "**Now waiting on:** CI and the independent review. Neither of them knows"
      echo "the run was cut off."
      echo
      echo "[Run log]($run_url) — the reason it stopped is in there, and it decides"
      echo "whether the rest is worth restarting."
      ;;
    done_here)
      echo "### Done — pushed to this pull request"
      echo
      echo "The work is on this branch, in #$detail itself. There is no second"
      echo "pull request to look for: this run was started from a comment here,"
      echo "so it pushed to the branch under review rather than opening one."
      echo
      echo "**Now waiting on:** CI on the new head, plus a fresh independent"
      echo "review — the previous verdict was reached against the previous"
      echo "commit and says nothing about this one."
      echo
      echo "**When it needs you:** if the review labels this \`ai-review:blocking\`"
      echo "and the finding is a product decision rather than a defect, or if it"
      echo "carries \`needs-owner\`. Otherwise it is merged without asking —"
      echo "owner decision, 2026-08-21."
      echo
      echo "[Run log]($run_url)"
      ;;
    done_here_cut)
      echo "### A commit landed on this pull request, and the run did not finish"
      echo
      echo "The head of #$detail moved, so real work is on the branch — this is not"
      echo "a run that did nothing. But it ended as \`${extra:-no conclusion}\` rather"
      echo "than reaching a conclusion, so **what is on the branch may be half of"
      echo "what was asked for**, and nothing here can tell you which half."
      echo
      echo "**Read the diff before the review does.** A partial change that compiles"
      echo "is the expensive kind: CI will go green on it and say nothing about the"
      echo "part that never got written."
      echo
      echo "**Now waiting on:** CI on the new head and a fresh independent review."
      echo "Both run automatically. Neither of them knows the run was cut off."
      echo
      echo "**Nothing will come back for the unfinished part on its own.** The"
      echo "hourly watchdog scans issues, not pull requests, so no label on this"
      echo "page queues anything. Finishing it takes a person commenting"
      echo "\`@claude\` here, having read the diff and said what is still missing."
      echo
      echo "[Run log]($run_url) — the reason it stopped is in there, and it decides"
      echo "whether the rest is worth restarting or the branch is worth dropping."
      ;;
    done_here_nopush)
      echo "### Ran on this pull request, and pushed nothing"
      echo
      echo "The run finished cleanly and the head of #$detail is the commit it"
      echo "started on. Nothing was pushed, so **there is no new CI result and"
      echo "no new review** — the checks you can see are the ones that were"
      echo "already there."
      echo
      echo "That is a real outcome and not necessarily a wrong one. It reads three"
      echo "ways, and the comment above this one should say which:"
      echo
      echo "* the agent **checked and found nothing to change** — it should have"
      echo "  said so with a file and a line;"
      echo "* the change it wanted to make was **out of scope** for what it was"
      echo "  asked, and it said what it would take;"
      echo "* it **did not get to the work at all**, in which case nothing above"
      echo "  explains itself and the run log is the only place the reason is."
      echo
      echo "**Before commenting \`@claude\` again:** a comment is not deduplicated,"
      echo "by design, so a second one starts a second billed agent against the"
      echo "same branch. If the first said why it changed nothing, that reason"
      echo "will not change on its own."
      echo
      echo "[Run log]($run_url)"
      ;;
    bad_detail)
      echo "### The run finished, and the reporting could not name the result"
      echo
      echo "The agent ran and this step could not turn its result into a pull"
      echo "request number, so it is refusing to print one rather than printing"
      echo "something that is not a number. **This is a defect in the reporting,"
      echo "not necessarily in the work** — check for an open pull request on"
      echo "this branch before starting anything again."
      echo
      echo "[Run log]($run_url)"
      ;;
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
      echo "### Finished, and no pull request was found for this issue"
      echo
      echo "The run ended cleanly. Nothing here could find a pull request that"
      echo "references this issue, which is a real outcome in three cases:"
      echo
      echo "* the finding was **verified against current code and did not hold**"
      echo "  — the comment above should say so with a file and a line;"
      echo "* the work was **already done** by something merged since;"
      echo "* a pull request **was** opened and does not mention this issue"
      echo "  anywhere, in which case it is the pull request that needs fixing,"
      echo "  not the run — it will not close this issue on merge either."
      echo
      echo "**Check for an open pull request before doing anything else.** If"
      echo "one exists, this issue is with the reviewers and commenting"
      echo "\`@claude\` would start a second agent against work that is already"
      echo "done — a comment is not deduplicated, by design, so nothing would"
      echo "stop it."
      echo
      echo "If there is genuinely no pull request and none of the three cases"
      echo "above is written above this, the run did nothing and the honest"
      echo "reading is that it failed quietly. [Run log]($run_url)."
      ;;
    failed)
      echo "### The run did not finish"
      echo
      echo "It ended as \`$detail\` rather than reaching a conclusion. Nothing was"
      echo "left half-applied on this issue: the claim is released and the task is"
      echo "back in the queue."
      echo
      # WHY THIS IS NOT "[Run log] — the cause is in there". That is what this
      # branch used to say, and it was wrong about its own pipeline:
      # `show_full_output: false` empties that log of exactly the cause, on
      # purpose, because tool results carry file contents and token-shaped
      # strings. What a reader actually found was a result object with
      # `is_error: true` and nothing else -- run 32589375744 on #67 is the
      # example, and the afternoon spent guessing at it is why the reason is
      # now extracted on the runner and carried here instead.
      if [ -n "$reason" ]; then
        echo "**Why:** $reason"
        echo
        case "$reason" in
          unclassified*)
            echo "\`unclassified\` means the run failed in a shape"
            echo "\`.github/scripts/failure-reason.sh\` does not recognise yet, and the"
            echo "[run log]($run_url) will not tell you either — it is redacted by design."
            echo "**Widening that whitelist is the fix, and it is a task**, not something"
            echo "to work around by starting the run again." ;;
          *)
            echo "That line is extracted on the runner from the action's full execution"
            echo "log, which is not published: the [run log]($run_url) is redacted by"
            echo "design, because tool results carry file contents." ;;
        esac
      else
        echo "**Why: not established.** Nothing reported a reason, which is itself a"
        echo "defect — \`.github/scripts/failure-reason.sh\` is meant to leave one here"
        echo "whatever happened. The [run log]($run_url) is redacted by design and will"
        echo "not fill the gap."
      fi
      echo
      echo "**What happens without you:** the watchdog gives it one retry within"
      echo "the hour. If it fails a second time without anything changing in"
      echo "between, that is treated as the same failure with a bill attached,"
      echo "and the watchdog labels it \`agent:blocked\` and \`needs-owner\`"
      echo "instead of trying again."
      echo
      # The two ways of starting it again are NOT equivalent, and the
      # difference is invisible unless somebody writes it down.
      # .github/scripts/failure-count.jq resets its count at the most recent
      # `agent:ready` labelling by a person -- a comment is not a labelling, so
      # an owner who fixes the cause and replies `@claude` still carries every
      # failure that happened before their fix, and escalates on the next one.
      echo "**To start it now:** add \`agent:ready\` yourself. A human labelling is"
      echo "also what resets the retry budget, so a task whose cause you have"
      echo "actually fixed starts again from zero. Commenting \`@claude\` starts a"
      echo "run too, but it does **not** reset the count — read the line above"
      echo "first either way, because a retry of a deterministic failure is the"
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
