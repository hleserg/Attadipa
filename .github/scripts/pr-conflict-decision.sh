#!/usr/bin/env bash
# Does this pull request need to be told it has a merge conflict, and does its
# issue need to go back in the queue?
#
# THE HISTORY. Pull request #65 sat with **zero check runs** for over an hour
# across two pushes -- not a red check, no check. `get_check_runs` answered
# `total_count: 0`, the Actions list held nothing for the branch, and the pull
# request page showed no CI section to be alarmed by. The cause was a merge
# conflict against `main`, and GitHub documents the consequence plainly:
#
#   "Workflows will not run on `pull_request` activity if the pull request has
#    a merge conflict. The merge conflict must be resolved first."
#   -- Events that trigger workflows, docs.github.com, read 2026-08-23
#
# Every check this repository produces on a pull request is `pull_request`-
# triggered: `ci.yml` (`push` only for `main`), `codeql.yml` (same) and
# `claude-pr-review.yml`. There is no `pull_request_target` workflow here, which
# is the one exception the same page names. So the blast radius of a conflict is
# not partial -- it is every check there is, and nothing goes red.
#
# Everything downstream reads "no failing check" as "not failing": the
# orchestrator merges once CI is green, the backstop's conditions are checks ON
# a verdict there is no verdict for, `agent:review` says the work is with the
# reviewers indefinitely, and an agent pushing a fix to its own pull request
# gets no signal that its fix was never built. Worse than "not red": on #110's
# head `244c4c0` the combined-status endpoint answered **`state: success`**,
# because a third-party app had left one passing status on a commit no workflow
# had ever built.
#
# This is a file rather than shell inside the workflow for the reason
# intake-decision.sh, queue-scan.jq and handover-decision.sh are files: a rule
# embedded in YAML cannot be executed, so it cannot be tested, so every defect
# it has ships. .github/tests/pr-conflict-decision-test.sh runs it; CI runs that.

# attadipa_pr_conflict_decision MERGEABLE MERGEABLE_STATE ALREADY_SAID LABELS
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
# Every fresh push passes through null on its way to an answer, so treating null
# as false would accuse every push in flight of a conflict it does not have.
# That is not a smaller version of this guard, it is the failure the guard is
# supposed to prevent, in the opposite direction, and it is the one that teaches
# people to stop reading the comments.
#
# MERGEABLE_STATE is `mergeable_state` from the same response, and it is
# CORROBORATION, never the assertion. `mergeable` is the documented three-state
# field with documented null semantics; `mergeable_state` is not in the same
# reference and its value set has already changed under this repository. It once
# returned `draft` for a draft pull request, masking every other state -- and
# today it does not: all thirteen open pull requests on 2026-08-23 reported
# `clean` or `unstable`, four of them drafts. A rule built on `mergeable`
# survives that change in either direction; a rule built on `mergeable_state ==
# "dirty"` would have gone silent for every draft on the day it masked, which
# is every pull request an agent opens.
#
# ALREADY_SAID is `yes` when a conflict comment already exists on this pull
# request FOR THIS HEAD COMMIT, `no` when it does not. Anything else -- an
# unreadable comment list, an API error -- is treated as `yes`. A guard that
# cannot tell whether it has already spoken must not speak: the cost of a missed
# hour is an hour, and the cost of a duplicate is the whole channel.
#
# LABELS is the comma-separated label list of the issue the pull request body
# says it closes, or empty when there is no such issue.
#
# Prints two lines: the action, then a one-word reason for the run log. The
# reason is always printed, so a caller can read a fixed number of lines.
#
#   quiet            nothing to do or nothing to say
#   say              comment on the pull request; leave the labels alone
#   say_and_requeue  comment, and move the issue from agent:review to
#                    agent:ready
attadipa_pr_conflict_decision() {
  local mergeable="$1" state="$2" already_said="$3" labels="$4"

  # `mergeable` is matched exactly against the three documented values. An error
  # document, a truncated response, an empty string or whatever a future `gh`
  # decides to print all land in the same bucket as null -- we do not know -- so
  # a lookup that FAILED is never distinguishable here from a lookup that
  # succeeded and found no conflict. That is deliberate: the distinguishable
  # version is how a JSON blob got into an outcome comment on #71.
  case "$mergeable" in
    true)
      # A positive "this merges" beside a positive "this is dirty" is a
      # contradiction, and the honest answer to a contradiction is that we do
      # not know. Quiet, and the caller logs it loudly, because if it ever
      # happens the rule above is the thing that needs rereading.
      if [ "$state" = "dirty" ]; then
        printf 'quiet\ncontradiction\n'
      else
        printf 'quiet\nmergeable\n'
      fi
      return ;;
    false)
      : ;;
    *)
      printf 'quiet\nundetermined\n'
      return ;;
  esac

  if [ "$already_said" != "no" ]; then
    printf 'quiet\nalready-said\n'
    return
  fi

  if [ -z "$labels" ]; then
    printf 'say\nno-issue\n'
    return
  fi

  # Exact token match against the comma-separated list, never a substring:
  # `agent:reviewed` must not read as `agent:review`, for the same reason
  # queue-scan.jq refuses `evil-chatgpt-codex-connector[bot]`. Wrapping the list
  # in its own separators is what makes the glob exact at both ends; splitting on
  # IFS would do the same and would also split a label that contains a space.
  local padded=",$labels,"
  local has_review=no has_claim=no

  case "$padded" in
    *",agent:review,"*) has_review=yes ;;
  esac
  # A claim, and a deliberate park. agent:working means somebody is holding this
  # right now, and moving it to agent:ready would put two writers on one branch
  # -- the merge conflict this guard exists to report, caused by the guard.
  # agent:blocked means a person decided it waits; re-queueing that hourly is
  # how a needs-owner question becomes a bill.
  case "$padded" in
    *",agent:working,"*|*",agent:blocked,"*) has_claim=yes ;;
  esac

  if [ "$has_claim" = "yes" ]; then
    printf 'say\nissue-claimed\n'
  elif [ "$has_review" = "yes" ]; then
    printf 'say_and_requeue\nissue-in-review\n'
  else
    printf 'say\nissue-not-in-review\n'
  fi
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_pr_conflict_decision "${1:-}" "${2:-}" "${3:-}" "${4:-}"
fi

