#!/usr/bin/env bash
# Did the review the run paid for actually reach the pull request?
#
# THE GAP. Every existing guard in claude-pr-review.yml asks whether the model
# was *reached*. `steps.happened` keys off the existence of an execution log,
# which the action writes only once the model has been invoked; the notice it
# drives lists five ways a run can end before that point. None of them asks the
# next question: the model was reached, and then what did it publish?
#
# It has to be asked separately, because publication is not something the action
# does. The action leaves the review in the run log. The prompt asks the model
# to run `gh pr comment` and `gh pr edit --add-label`, and a request made of a
# model is not a guarantee. Two observed runs prove the shape, both recorded in
# that workflow's own comments:
#
#   2026-08-21, before `--allowedTools` existed: the reviewer ran for 41 s,
#   exited 0, and posted no comment and set no label. It had read the diff
#   perfectly well and had no way to say so.
#
#   Pull request #39: six and a half minutes of work, `is_error: false`, killed
#   at turn 50 by a 40-turn ceiling, "having posted nothing and set no label".
#
# Both went green. `continue-on-error: true` on the Review step is deliberate
# and correct -- a red check meaning "the quota is spent" is a check people
# learn to ignore -- but it means the job's colour carries no information about
# whether a verdict exists. `ran=yes` says the model was reached. Nothing said
# the pull request heard from it.
#
# AND THE HALF THAT IS WORSE THAN SILENCE. A verdict label records that a
# verdict was reached, never which commit it was reached on. So a run that
# publishes nothing does not leave "no verdict" behind; it leaves the PREVIOUS
# commit's verdict, wearing this commit's clothes. `.github/scripts/merge-
# candidate.sh` already refuses to merge on that (PASS_AFTER_HEAD, "the
# likeliest of these to recur") -- but the unattended sweep is not the only
# reader. A person looking at the pull request page sees a green tick and a
# label, and has nothing to tell them the label is four commits old.
#
# WHAT THIS IS NOT. It was written after run 32608091395 on #85 was read as
# exactly this failure -- 22 minutes, 83 turns, $8.47, 27 permission denials,
# no new comment on the pull request -- and that reading was WRONG. The
# reviewer had published, by editing comment 5382540003 in place at 00:53:32Z,
# inside that run's 00:31:47-00:54:10 window, which is precisely what the prompt
# tells it to do:
#
#   "check with `gh pr view` whether a comment carrying that marker already
#   exists. If one does, edit it rather than adding another"
#
# So a review published on the fourth push to a pull request can be a comment
# created before the first one. That mistake is a case below, and it is the
# reason this file reads `updated_at` and not only `created_at`. A guard that
# cries silence over a review sitting in plain sight is a guard somebody
# switches off, and then the real one goes unheard too.
#
# attadipa_review_published RAN OUTCOME COMMENTS_JSON LABELS RUN_STARTED_AT
#
#   RAN             yes | no      -- the existing `happened` step's answer
#   OUTCOME         the Review step's outcome (success, failure, ...)
#   COMMENTS_JSON   the pull request's issue comments, as the REST API returns
#                   them: an array of {user:{login}, body, created_at,
#                   updated_at}
#   LABELS          label names currently on the pull request, ONE PER LINE
#   RUN_STARTED_AT  RFC3339 Z, recorded in the job's first step
#
# Prints one line beginning with one of:
#
#   published   a verdict reached the pull request during this run
#   silent      the model ran and published nothing; the caller says so and
#               strips any verdict label, which is this commit's only in
#               appearance
#   not-run     the model was never reached; the existing notice owns that
#   unknown     a fact this needs could not be read, so it declines to judge
#
# `unknown` is a real outcome rather than a tidy-up. The two directions of
# error here are not symmetric: a missed silence leaves a stale label that
# merge-candidate.sh already refuses to merge on, while a false silence deletes
# a real reviewer's verdict from a pull request and posts a notice contradicting
# a review anybody can scroll to. So an unreadable fact holds, and says which
# one it was.
#
# No network, no `gh`, no environment. It is a file rather than a fragment of
# workflow for the reason handover-decision.sh and merge-candidate.sh are: a
# rule embedded in a workflow cannot be executed, so it cannot be tested, so
# every defect it has ships.
#
# OBSERVED ON THIS BRANCH'S OWN PULL REQUEST, which is the closest thing to a
# field test available. Run 32609977184 on #123 at 01:17 on 2026-08-23: the
# Independent review job finished in fifteen seconds, `conclusion: success`,
# and published nothing. The sibling guard -- the one for a model that was
# never reached -- fired correctly, posted
# github.com/hleserg/Attadipa/pull/123#issuecomment-5383545987 and stripped
# `ai-review:pass`. The cause is cause 4 in its own notice: this pull request
# edits claude-pr-review.yml, and the action refuses to run a version of itself
# that a pull request has modified.
#
# Two things follow, and the second is the one to act on:
#
#   1. This pull request cannot receive an AI review, ever, by design. It is
#      merged on ordinary CI and a person reading the diff, which is what its
#      own notice says happens to changes in these files.
#   2. Merging any change to claude-*.yml silently skips the review on every
#      open pull request whose merge ref predates it (cause 3), until each is
#      updated from `main`. So this one merges AFTER the reviews in flight have
#      published, not before, and the branches still open are updated from
#      `main` immediately afterwards. Ordering is the mitigation; there is no
#      code fix, because the action's refusal is correct.

set -uo pipefail

# The marker the prompt asks the reviewer to open its comment with.
ATTADIPA_REVIEW_MARKER='<!-- attadipa-ai-review -->'

# WHO THE REVIEWER IS. With ATTADIPA_AGENT_TOKEN unset -- the documented
# default -- the model's `gh` runs as the Claude GitHub App, so the review is
# authored by claude[bot]; comment 5382540003 on #85 is one. Should that token
# ever be set the author changes, and a guard pinned to one login would then
# report silence on every published review at once. Hence the marker as a
# second, independent signal below: the objection recorded above is to trusting
# the marker ALONE, because two real reviews (#92, #94) omitted it. Either
# signal is enough; neither is required.
ATTADIPA_REVIEW_AUTHOR='claude[bot]'

attadipa_review_published() {
  local ran="${1-}" outcome="${2-}" comments="${3-}" labels="${4-}" started="${5-}"

  # A session that never reached the model is a different failure with its own
  # notice, naming five causes this one cannot distinguish between. Competing
  # with it would put two contradictory comments on the same commit.
  if [ "$ran" != "yes" ] || [ "$outcome" = "failure" ]; then
    echo "not-run the model was not reached, so publication is not the question"
    return 0
  fi

  # THE TIMESTAMP IS A CONDITION, NOT A CONVENIENCE. Without it, a comment the
  # reviewer left on an earlier commit answers for this one -- the same mistake
  # the label makes, reproduced inside the thing meant to catch it. The caller
  # records it with `date -u` in the job's first step, so failing to read it
  # means something is wrong that this file cannot diagnose.
  case "$started" in
    [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z) ;;
    *)
      echo "unknown the run start time could not be read, so no comment can be attributed to this run"
      return 0
      ;;
  esac

  # created_at OR updated_at, and the `or` is the whole lesson of the header.
  # A reviewer told to edit its previous comment publishes by editing, so the
  # freshly written review can carry a created_at from three pushes ago.
  # Reading created_at alone reported `silent` for run 32608091395, which had
  # published a full blocking review.
  local fresh
  fresh="$(printf '%s' "$comments" | jq -r \
      --arg since "$started" \
      --arg who "$ATTADIPA_REVIEW_AUTHOR" \
      --arg marker "$ATTADIPA_REVIEW_MARKER" '
    (if type == "array" then . else error("not a comment array") end)
    | [ .[]
      | select(type == "object")
      | select(((.user.login // "") == $who)
               or (((.body // "") | tostring | contains($marker))))
      | select(((.created_at // "") >= $since) or ((.updated_at // "") >= $since))
    ] | length' 2>/dev/null || true)"

  case "$fresh" in
    ''|*[!0-9]*)
      echo "unknown the pull request comments could not be read, so publication cannot be established"
      return 0
      ;;
  esac

  if [ "$fresh" -gt 0 ]; then
    echo "published $fresh reviewer comment(s) written or edited since this run started"
    return 0
  fi

  # A LABEL ALONE IS NOT PUBLICATION, and this is deliberate rather than strict.
  # The label is what merge-candidate.sh and the orchestrator read, so a run
  # that sets one and explains nothing has put a verdict into the merge
  # machinery with no reasoning attached. `ai-review:blocking` with no comment
  # blocks a branch and names nothing to fix; `ai-review:pass` with no comment
  # is the claim this whole file exists to stop anybody making on the strength
  # of a green tick.
  if printf '%s\n' "$labels" | grep -q '^ai-review:'; then
    echo "silent a verdict label is set and no review was published this run, so the label is an older commit's"
    return 0
  fi

  echo "silent the model ran and published neither a comment nor a label"
}

# Runnable directly so the workflow can call it without sourcing.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_review_published "$@"
fi
