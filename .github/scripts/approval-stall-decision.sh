#!/usr/bin/env bash
# Whether a workflow run is stalled behind an approval nobody is watching for.
#
# THE FAILURE THIS EXISTS FOR. On 2026-08-22 an agent pushed `488be1e` to the
# branch of #71. Both runs for that head -- CI (`32581052659`) and the
# independent review (`32581052664`) -- were created and, in the same second,
# completed with conclusion `action_required` and **zero jobs**. No check ever
# appeared on the pull request. The agent's own comment then said it was waiting
# on CI, which was true and would have stayed true forever. Nothing was red, so
# nothing was investigated.
#
# That is the quietest failure this pipeline has: the orchestrator merges once
# CI is green, the merge sweep's conditions are checks *on* a verdict, and
# `agent:review` says the work is with the reviewers. All three read "no failing
# check" as "not failing yet". See docs/automation/APPROVAL_STALLS.md for the
# documented rule that causes it and what the fix costs.
#
# It is a file rather than shell inside `agent-queue-watchdog.yml` for the
# reason this repository has already learnt four times on the hand-over step:
# a decision that cannot be executed cannot be tested, and every defect that
# step shipped lived in a `run:` block where nothing could run it.

# attadipa_approval_stall_decision STATUS CONCLUSION JOB_COUNT HEAD_SHA SAID_FOR
#
# STATUS      the run's `status` word, straight from the API.
# CONCLUSION  the run's `conclusion` word, possibly empty.
# JOB_COUNT   `total_count` from the run's jobs endpoint, as text.
# HEAD_SHA    the run's head SHA.
# SAID_FOR    the head SHA this pull request has already been told about, empty
#             when it has not been.
#
# Prints two lines: the decision, then a one-word reason.
#
# THREE DECISIONS IN HERE THAT ARE NOT OBVIOUS.
#
# 1. `action_required` is read off BOTH fields. GitHub's REST reference lists
#    `action_required` as a value of `status` and of `conclusion`, and the two
#    real runs above carried `status: completed` with `conclusion:
#    action_required`. Keying on one field alone is a coin toss about which
#    shape a future run arrives in, and the failure mode of guessing wrong is
#    silence -- the same silence this whole file is about.
#
# 2. Zero jobs and some jobs are DIFFERENT ANSWERS, not a detail. A run with no
#    jobs put no check on the pull request at all, which is this issue. A run
#    with jobs that is nonetheless `action_required` is a deployment protection
#    rule or an environment gate: also waiting on a person, but visible on the
#    pull request, with a different cause and a different fix. One message for
#    both would be wrong for one of them, so they get different reasons and the
#    caller writes different words.
#
# 3. An unreadable input is never resolved in either direction. A job count
#    that is not a number and a head that is not a SHA both mean "we do not
#    know", and both stay quiet for this tick. Guessing "stalled" accuses every
#    pull request the moment the API has a bad minute -- hourly, forever, which
#    is the shape of #82 -- and guessing "fine" restores exactly the silence
#    being fixed. The watchdog's queue scan already answers an unreadable
#    timeline this way and says why; this is the same rule.
attadipa_approval_stall_decision() {
  local status="$1" conclusion="$2" job_count="$3" head_sha="$4" said_for="$5"

  # The head is the repeat bound, so it is checked before anything that could
  # produce a comment. Per HEAD COMMIT rather than per pull request: bounding
  # by pull request means the second stalled push is silent, and the second
  # push is the common one -- an agent fixing its own review findings pushes
  # more than once.
  case "$head_sha" in
    "" | *[!0-9a-fA-F]*)
      printf 'quiet\nunreadable\n'
      return
      ;;
  esac

  # A run that has not completed cannot have concluded anything, so a
  # conclusion beside a non-terminal status is the API contradicting itself.
  # Answered as "we do not know" and left for the next tick rather than picked
  # in the direction that happens to suit this file.
  if [ -n "$conclusion" ] && [ "$status" != "completed" ] && [ "$status" != "action_required" ]; then
    printf 'quiet\ncontradiction\n'
    return
  fi

  # Is this the approval shape at all? Everything else -- queued, in_progress,
  # waiting, and every completed run that concluded something real -- is not
  # this file's business. `waiting` in particular is a run held by a deployment
  # protection rule, which is visible on the pull request and has an owner.
  if [ "$status" != "action_required" ] &&
     ! { [ "$status" = "completed" ] && [ "$conclusion" = "action_required" ]; }; then
    if [ "$status" = "completed" ]; then
      printf 'quiet\nran\n'
    else
      printf 'quiet\nrunning\n'
    fi
    return
  fi

  # Only now is the job count load-bearing, so only now does an unreadable one
  # matter. Checked after the shape and before the repeat bound, because a
  # count we cannot read must not consume this head's one comment.
  case "$job_count" in
    "" | *[!0-9]*)
      printf 'quiet\nunreadable\n'
      return
      ;;
  esac

  # Compared case-insensitively. Both sides come from the same API today, so
  # both are lower case and this changes nothing -- which is the point. The
  # marker is written once and read back hours later by a different job, and
  # the day one side is upper case the bound silently disappears and the guard
  # comments hourly on a pull request nobody has touched. That is #82's shape,
  # and it costs one line to make impossible rather than unlikely.
  if [ "${said_for,,}" = "${head_sha,,}" ]; then
    printf 'quiet\nrepeat\n'
    return
  fi

  if [ "$job_count" -eq 0 ]; then
    printf 'say\nstalled\n'
  else
    printf 'say\ngated\n'
  fi
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_approval_stall_decision "${1:-}" "${2:-}" "${3:-}" "${4:-}" "${5:-}"
fi
