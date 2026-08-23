#!/usr/bin/env bash
# May the hand-over take this pull request out of draft?
#
# WHY THIS IS A SEPARATE QUESTION FROM "did the run finish". Marking a pull
# request ready is not a sentence in a comment; it flips a real bit. A draft can
# never read `mergeable_state: clean`, so being a draft is what keeps a pull
# request out of the unattended backstop's candidate set
# (docs/automation/attadipa-backstop-routine.md STEP 2). Undrafting is therefore
# the step that makes a pull request mergeable without a person, and it must
# rest on evidence that this run owns the branch — not on a heuristic.
#
# THE HEURISTIC IT REPLACES. For KIND=done_pr the pull request number comes from
# `closedByPullRequestsReferences`: whichever OPEN pull request declares it
# closes the issue, first of up to five. Anybody can write `Fixes #N`, including
# an abandoned branch from a run three days ago and somebody else's competing
# attempt. That heuristic has already misfired in this repository — #75 was
# reported as #71 — and while it only put a wrong number in a sentence, being
# wrong was cheap. Undrafting an abandoned pull request with a closing keyword
# on it is not: it hands that branch to the backstop. Found in review of #85.
#
# WHAT COUNTS AS EVIDENCE. One of two facts, both timestamps, both from GitHub
# rather than from the model:
#
#   - the pull request was CREATED during this run, or
#   - its head commit was COMMITTED during this run.
#
# Either means the run put work on that branch. An abandoned pull request
# satisfies neither, because both of its timestamps predate the run. For
# KIND=done_here the evidence is already established upstream:
# handover-decision.sh prints done_here only when the head SHA read before the
# run differs from the head SHA after it, which is the same fact measured
# directly.
#
# WHEN IN DOUBT, HOLD. An unreadable or missing timestamp prints `hold`, never
# `promote`. The cost of holding is that a finished pull request stays a draft
# until a person presses the button, which is the state this repository was in
# before #85 and is merely slow. The cost of promoting wrongly is a branch
# nobody stands behind becoming eligible for an unattended merge, which is not.
#
# No network, no `gh`, no environment: every input is an argument, so
# .github/tests/promote-decision-test.sh can execute it. Same reason as
# handover-decision.sh, intake-decision.sh and merge-candidate.sh.

# attadipa_promote_decision KIND PR_CREATED_AT PR_HEAD_AT RUN_STARTED_AT
#
# KIND            the hand-over kind, from handover-decision.sh.
# PR_CREATED_AT   the pull request's createdAt, ISO-8601 with a trailing Z. May
#                 be empty when the lookup failed or was not made.
# PR_HEAD_AT      its head commit's committedDate, same format, same caveat.
# RUN_STARTED_AT  when this run's writer job started, same format. Recorded by
#                 the workflow's first step, before the agent is reached, so a
#                 commit the agent pushes cannot predate it.
#
# Prints exactly one line: `promote`, or `hold <reason>` where the reason is
# written to be read in a workflow log by somebody wondering why their pull
# request is still a draft.
attadipa_promote_decision() {
  local kind="$1" created="$2" head_at="$3" started="$4"

  # An ISO-8601 UTC instant compares correctly as a string, and only in that
  # one shape: fixed-width fields, most significant first, a literal Z. Anything
  # else -- an offset, a missing Z, an error document -- is not compared, it is
  # refused. failure-count.jq relies on the same property and says so.
  _iso_z() {
    case "$1" in
      [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z) return 0 ;;
      *) return 1 ;;
    esac
  }

  case "$kind" in
    done_here)
      # handover-decision.sh prints done_here only when the head SHA moved
      # between the read before the run and the read after it. That IS the
      # ownership fact this file is otherwise reconstructing from timestamps,
      # already established, so asking for it twice would only add a way to get
      # it wrong.
      echo "promote"
      return 0
      ;;
    done_pr) ;;
    *)
      # Every other kind either has no pull request (done_nopr), pushed nothing
      # to one (done_here_nopush), or did not finish (*_cut, failed). A draft is
      # the correct state for all of them.
      echo "hold $kind is not a kind that owns a pull request this run advanced"
      return 0
      ;;
  esac

  if ! _iso_z "$started"; then
    echo "hold the run's start time was not readable, so nothing can be dated against it"
    return 0
  fi

  # A >= B over two instants already known to be in the one comparable shape.
  _at_or_after() {
    [ "$1" = "$2" ] && return 0
    [[ "$1" > "$2" ]]
  }

  local have=no
  if _iso_z "$created" && _at_or_after "$created" "$started"; then
    have=created
  elif _iso_z "$head_at" && _at_or_after "$head_at" "$started"; then
    have=pushed
  fi

  case "$have" in
    created) echo "promote" ;;
    pushed)  echo "promote" ;;
    *)
      if ! _iso_z "$created" && ! _iso_z "$head_at"; then
        echo "hold neither the pull request's creation time nor its head commit's time could be read"
      else
        echo "hold the pull request predates this run and its head did not move during it, so this run does not own it"
      fi
      ;;
  esac
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_promote_decision "${1:-}" "${2:-}" "${3:-}" "${4:-}"
fi
