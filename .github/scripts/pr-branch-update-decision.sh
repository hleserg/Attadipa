#!/usr/bin/env bash
# What should be done about one open pull request whose base branch has moved?
#
# THE DEFECT. A merge to `main` does not touch any other open branch, and
# nothing here noticed. The owner's account of it on issue #171, 2026-08-28:
# several branches were in flight at once, each adding a step to `ci.yml`, and
# `docs/research/WAVESHARE_ARRIVAL.md` cites that file BY LINE NUMBER. The
# citation is correct on the branch and wrong on `main` a minute later --
# **and CI is green on both sides of the merge**, because each branch was only
# ever built against the `main` it forked from. So the breakage lands on `main`
# with nothing red anywhere to have caught it. That is not a merge conflict;
# git merges both cleanly. It is a semantic conflict, and the only thing that
# finds one is building the branch against the base as it is now.
#
# This file decides, per pull request, which of four things to do. It is a file
# rather than shell inside a workflow for the reason pr-conflict-decision.sh,
# intake-decision.sh and queue-scan.jq are files: a rule embedded in YAML cannot
# be executed, so it cannot be tested, so every defect it has ships.
# .github/tests/pr-branch-update-test.sh runs THIS file; CI runs that.
#
# WHAT THIS IS NOT. #119 already comments on a conflicted pull request, once per
# head commit, to say that a conflict suppresses every `pull_request` check
# there is (.github/scripts/pr-conflict-decision.sh, and the `conflicts` job in
# agent-queue-watchdog.yml). That is a different sentence to a different reader
# and it is not re-implemented here. What is added is the part a person can
# filter on and act from: a `needs-rebase` label, and the paths both sides
# touched. The `flag` action below fires on the TRANSITION into conflict, not
# once per tick and not once per head, precisely so the two do not become two
# voices saying the same thing.

# attadipa_branch_update_decision MERGEABLE BEHIND SAME_REPO LABELS
#
# MERGEABLE is the `mergeable` field of GET /repos/{o}/{r}/pulls/{n}, verbatim:
# the string `true`, `false` or `null`. It is a THREE-state field and the third
# state is not a soft `false`:
#
#   "If the value is null, then GitHub has started a background job to compute
#    the mergeability. After giving the job time to complete, resubmit the
#    request."
#   -- Pulls REST reference, docs.github.com, read 2026-08-23
#
# Every fresh push passes through null on its way to an answer. Treating null as
# false would label every push in flight `needs-rebase`; treating it as true
# would push an update at a branch nobody knows the state of. Both are worse
# than waiting for the next run, so null does nothing in EITHER direction --
# including not removing a label that is already there.
#
# BEHIND is `behind_by` from GET /repos/{o}/{r}/compare/{base}...{head}: the
# number of commits on the base that the head does not have. Anything that is
# not a run of digits -- an empty string, an error document, `null` -- means the
# comparison did not answer, and an unanswered comparison is not a zero.
#
# It is `behind_by` and not `mergeable_state == "behind"` for the reason
# pr-conflict-decision.sh gives at length about the same field: `mergeable_state`
# is not in the same REST reference as `mergeable`, its value set has already
# changed under this repository, and it collapses. A pull request that is BOTH
# behind and unstable reports one of them, so a rule keyed on the string goes
# silent for every branch that also has a red check -- which is most of the ones
# that need this. `behind_by` is a count of commits and cannot be masked by a
# second condition.
#
# Gating the update on BEHIND > 0 is also the whole of the idempotency claim.
# `PUT .../update-branch` is not a no-op when there is nothing to do, so "run it
# and see" would put a merge commit on every open branch on every run. Asking
# first means a second run over an already-updated pull request issues no write
# at all.
#
# SAME_REPO is `yes` when the head repository is this repository and `no` when
# the pull request comes from a fork. Updating a fork's branch means pushing
# into somebody else's repository, which GitHub permits only through
# `maintainer_can_modify` -- a flag its author can withdraw between the read and
# the write. pr-wip-limit.yml already draws this line at the same place
# (`head.repo.full_name == github.repository`), and the cost of drawing it is
# that a fork contributor rebases by hand, which is what they do today.
#
# LABELS is the comma-separated label list of the PULL REQUEST -- not of the
# issue it closes, which is what pr-conflict-decision.sh reads. `needs-rebase`
# is a property of the branch, so it lives where the branch does. Two other
# labels are read here, and the reason is in the next paragraph.
#
# WHAT AN UPDATE COSTS THE MERGE QUEUE, because it is not free and the number is
# six hours. `PUT .../update-branch` gives the pull request a NEW HEAD COMMIT,
# and two gates of the unattended merge sweep are keyed on that object:
# merge-candidate.sh's `MIN_HEAD_AGE_SECONDS=21600` measures the head's own age
# from its check suites (#199), and merge-head-trust.jq binds `ai-review:pass`
# to a labelling no older than the head's arrival. A new head sets the first to
# zero and makes the second `false`. So every merge to `main` puts each other
# open pull request back behind a six-hour settling window and a fresh review.
#
# THAT IS ACCEPTED, NOT OVERLOOKED. A branch that has just had `main` merged
# into it really is a different branch, and re-settling is those gates working
# rather than misfiring -- the alternative is merging content that was never
# built against the base it is going into, which is the whole of #171. What is
# NOT accepted is paying it for a pull request that was never going to merge:
# `queue:parked` and `agent:blocked` are deliberate holds, so they suppress the
# update and nothing else. The conflict half still runs on them, because knowing
# a parked branch has gone dirty is information and costs no push.
#
# The cost is bounded by the queue's own width -- two active pull requests
# (.github/scripts/wip-limit.sh) -- so an ordinary merge re-settles about one
# other branch. If the queue is ever wide and this serialises it, the number to
# revisit is `MIN_HEAD_AGE_SECONDS`, and that is an owner decision, not this
# file's to take.
#
# Prints two lines: the action, then a one-word reason for the run log. The
# reason is always printed, so a caller can read a fixed number of lines.
#
#   quiet   nothing to do, or nothing that can be honestly done
#   update  call the update-branch API on this pull request
#   flag    add `needs-rebase` and name the paths; it has just gone conflicted
#   clear   remove `needs-rebase`; it merges cleanly again
attadipa_branch_update_decision() {
  local mergeable="$1" behind="$2" same_repo="$3" labels="$4"

  # Exact token match against the comma-separated list, never a substring, for
  # the reason pr-conflict-decision.sh gives: wrapping the list in its own
  # separators is what makes the glob exact at both ends, so a future
  # `needs-rebase-soon` cannot read as `needs-rebase`.
  local padded=",$labels," flagged=no held=no
  case "$padded" in
    *",needs-rebase,"*) flagged=yes ;;
  esac
  case "$padded" in
    *",queue:parked,"*|*",agent:blocked,"*) held=yes ;;
  esac

  # Before anything else, and before the `mergeable` triage, because none of the
  # four actions is available on a fork: we will not push into one, and a
  # `needs-rebase` label on a pull request whose author cannot be helped by this
  # job is an instruction with no addressee.
  if [ "$same_repo" != yes ]; then
    printf 'quiet\nfork\n'
    return
  fi

  case "$mergeable" in
    false)
      # A conflict. update-branch would return 422 and change nothing, so it is
      # not attempted: the answer is already known and the API call would only
      # spend a request to be told so. Say it once, on the way in.
      if [ "$flagged" = yes ]; then
        printf 'quiet\nstill-conflicted\n'
      else
        printf 'flag\nconflicted\n'
      fi
      return ;;
    true)
      : ;;
    *)
      # null, an empty string, an error document, or whatever a future `gh`
      # prints. A lookup that FAILED is deliberately not distinguishable from a
      # lookup that answered "still computing": both mean we do not know, and
      # the honest response to not knowing is to do nothing and come back.
      printf 'quiet\nundetermined\n'
      return ;;
  esac

  case "$behind" in
    ''|*[!0-9]*)
      # The comparison did not answer. Not a zero: a zero would look exactly
      # like "up to date" and would strip a `needs-rebase` label on the strength
      # of a failed request.
      printf 'quiet\nbehind-unknown\n'
      return ;;
  esac

  if [ "$behind" -gt 0 ]; then
    # A deliberate hold. Pushing into it would reset a settling window and a
    # review verdict for a branch nobody is merging, which is spend without a
    # question it answers. It stays behind until the hold comes off, and the
    # run after that updates it like any other.
    if [ "$held" = yes ]; then
      printf 'quiet\nheld\n'
      return
    fi
    # Behind and mergeable. This is the case the job exists for. Whether the
    # label comes off is decided by the update SUCCEEDING, not here -- the
    # caller removes it after a 202, because that 202 is the evidence that the
    # branch merges cleanly again. Deciding it here would strip the label on the
    # strength of an intention.
    printf 'update\nbehind\n'
    return
  fi

  # Level with the base and mergeable. Nothing to push; the only thing that can
  # be wrong is a label left over from a conflict that has since been resolved
  # by hand, which is the second half of "remove the label when it merges
  # cleanly again" -- the half that does not go through this job's own write.
  if [ "$flagged" = yes ]; then
    printf 'clear\nup-to-date\n'
  else
    printf 'quiet\nup-to-date\n'
  fi
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_branch_update_decision "${1:-}" "${2:-}" "${3:-}" "${4:-}"
fi
