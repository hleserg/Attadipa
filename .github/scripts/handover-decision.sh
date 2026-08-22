#!/usr/bin/env bash
# What the hand-over step decides, and why it is a file rather than a heredoc.
#
# THE HISTORY. On 2026-08-22 the hand-over step asked GraphQL for
# `repository.issue(number:)` to find the pull request an agent had opened.
# `issue()` does not resolve pull requests, so an agent started by `@claude` ON
# a pull request got NOT_FOUND -- and `gh` writes the error document to stdout
# before exiting non-zero, so `$(... || echo "")` captured the JSON. The outcome
# comment on #71 went out as `### Done — pull request #{"data":{"repository"...`.
#
# The first fix used `issueOrPullRequest` and reported the trigger as its own
# answer. Review caught the next defect immediately: being triggered from a pull
# request is not evidence that anything was done. The pull request is open
# before the agent starts and open after whatever it does, so a run that pushed
# nothing -- or died -- would have been reported as "Done — pushed to this pull
# request". That is worse than the JSON blob, because it is plausible.
#
# Both defects were in a shell block embedded in the workflow, where nothing
# could execute it. This repository's own rule, already applied to
# .github/scripts/intake-decision.sh and .github/scripts/queue-scan.jq, is that
# a filter which cannot be executed cannot be tested. So the decision lives
# here: no network, no `gh`, no environment, every input an argument, and
# .github/tests/handover-decision-test.sh asserts on the exact output.

# attadipa_handover_decision FOUND HEAD_BEFORE CONCLUSION
#
# FOUND is the raw output of the workflow's lookup, which is trusted for
# nothing. Two shapes mean something:
#
#   "here <number> <sha>"  the task IS an open pull request -- the trigger was
#                          a comment on it -- and <sha> is its head now
#   "pr <number>"          the task is an issue, and that open pull request
#                          closes or mentions it
#
# Anything else is treated as "found nothing", including an error document, a
# partial response, and whatever a future `gh` decides to print. A lookup that
# failed must never be distinguishable from a lookup that succeeded and found
# nothing, because the difference is exactly where a JSON blob got out.
#
# HEAD_BEFORE is the pull request's head SHA read before the agent ran, or
# empty when the task was an issue or the read failed.
#
# CONCLUSION is the action's own conclusion word, possibly empty.
#
# Prints two lines: the kind, then the detail.
attadipa_handover_decision() {
  local found="$1" head_before="$2" conclusion="$3"
  local where="" pr="" head_now="" rest=""

  case "$found" in
    "here "[0-9]*)
      where="here"; rest="${found#here }"
      pr="${rest%% *}"
      # No second field means no SHA, and "here 71" would otherwise set
      # head_now to "71" -- which can never equal a real SHA, but relying on
      # that is relying on an accident.
      case "$rest" in
        *" "*) head_now="${rest#* }" ;;
        *)     head_now="" ;;
      esac ;;
    "pr "[0-9]*)
      where="pr"; pr="${found#pr }" ;;
  esac

  case "$pr" in
    ""|*[!0-9]*) where=""; pr=""; head_now="" ;;
  esac
  case "$head_now" in
    ""|*[!0-9a-f]*) head_now="" ;;
  esac

  # Did anything actually happen on this branch? Only a moved head says so, and
  # an unknown starting point counts as "did not move": an unverified claim of
  # work is the thing that must not be printed.
  local moved=no
  if [ -n "$head_before" ] && [ -n "$head_now" ] && [ "$head_before" != "$head_now" ]; then
    moved=yes
  fi

  # A pull request number is the whole story, so it wins over every other
  # signal -- including a run whose conclusion never arrived. An agent that
  # opened a pull request and then timed out has still done the work, and
  # reporting that as a failure would send somebody looking for a branch that
  # is already there. That reasoning does NOT extend to `here`: there, the
  # pull request's existence is not the agent's doing.
  if [ "$where" = "here" ] && [ "$moved" = "yes" ]; then
    printf 'done_here\n%s\n' "$pr"
  elif [ "$where" = "here" ] && [ "$conclusion" = "success" ]; then
    printf 'done_here_nopush\n%s\n' "$pr"
  elif [ "$where" = "here" ]; then
    printf 'failed\n%s\n' "${conclusion:-no conclusion}"
  elif [ -n "$pr" ]; then
    printf 'done_pr\n%s\n' "$pr"
  elif [ "$conclusion" = "success" ]; then
    printf 'done_nopr\n\n'
  else
    printf 'failed\n%s\n' "${conclusion:-no conclusion}"
  fi
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_handover_decision "${1:-}" "${2:-}" "${3:-}"
fi
