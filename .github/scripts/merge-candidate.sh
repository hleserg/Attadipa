#!/usr/bin/env bash
# Whether one open pull request may be merged without a person looking at it.
#
# WHY THIS EXISTS. The queue's last link had no automation in it. An agent
# opened a pull request, CI went green, the independent reviewer set
# `ai-review:pass` -- and then nothing happened, because CLAUDE.md makes the
# merge an *orchestrator* act, meaning a live session. On 2026-08-22 six
# finished pull requests (#88, #92, #94, #95, #97, #103) sat green for hours,
# each carrying `Fixes #N`, and the six issues behind them stayed open. The
# owner's complaint was about the issues; the cause was that nobody merged.
#
# The unattended backstop routine does merge, but only `docs/`, never
# `docs/automation/`, three per run. So a green pull request touching `core/`
# was waiting for a human-shaped event that the design never scheduled.
#
# WHY NOT GITHUB'S OWN AUTO-MERGE. It cannot read a label, and
# `ai-review:blocking` is a label rather than a required check -- deliberately,
# because the reviewer's verdict is a judgement and a red tick is a fact.
# Arming native auto-merge would therefore merge exactly the pull requests the
# reviewer blocked. That is not a smaller version of this; it is the opposite.
#
# WHY A FILE. Same reason as handover-decision.sh and intake-decision.sh: a
# rule embedded in a workflow cannot be executed, so it cannot be tested, so
# every defect it has ships. No network, no `gh`, no environment -- the caller
# gathers the facts and passes them in, and .github/tests/merge-candidate-test.sh
# asserts on the exact output.
#
# THE CONDITIONS ARE NOT NEW. They are the six the owner already approved for
# the backstop routine (docs/automation/attadipa-backstop-routine.md, STEP 2),
# transcribed rather than reinvented, and this file must not add a seventh or
# drop one to make a candidate qualify.

set -uo pipefail

# attadipa_merge_candidate CHECKS LABELS UNRESOLVED CODEX_UNANSWERED \
#                          MERGEABLE_STATE IS_DRAFT HEAD_AGE_SECONDS
#
# CHECKS               one token per check run on the head commit, space
#                      separated, each the run's conclusion lowercased:
#                      `success`, `skipped`, `failure`, `cancelled`, ... An
#                      in-flight run has no conclusion and must be passed as
#                      `pending`. An EMPTY string means no check run exists.
# LABELS               space-separated label names currently on the pull request.
# UNRESOLVED           count of unresolved review threads.
# CODEX_UNANSWERED     count of comments from chatgpt-codex-connector[bot] with
#                      no reply after them, review thread or not.
# MERGEABLE_STATE      GitHub's mergeStateStatus, lowercased.
# IS_DRAFT             `true` or `false`.
# HEAD_AGE_SECONDS     now minus the head commit's committedDate, in seconds.
#
# Prints exactly one line:
#   MERGE                      every condition holds; merge it
#   READY                      every condition holds except that it is a draft;
#                              take it out of draft, then CALL THIS AGAIN
#   HOLD <reason>              do nothing, and <reason> says which condition
#
# READY is separate from MERGE on purpose. Taking a pull request out of draft
# raises `ready_for_review`, which claude-pr-review.yml fires on -- so undrafting
# starts a fresh review of the very pull request about to be merged. Merging
# straight afterwards would rest on an `ai-review:pass` read BEFORE the step
# that can replace it, and a second-pass finding would land as
# `ai-review:blocking` on a commit already in `main`. So the caller undrafts,
# and then re-gathers every fact and asks again. Not a summary of the facts --
# all of them: three rounds of review on the pull request that wrote the
# backstop found the same mistake three times, each fix re-checking whichever
# conditions were on somebody's mind and quietly dropping the rest.

MIN_HEAD_AGE_SECONDS=21600  # six hours; see the comment on the check below

attadipa_merge_candidate() {
  local checks="${1-}" labels="${2-}" unresolved="${3-}" codex="${4-}"
  local mergeable="${5-}" is_draft="${6-}" head_age="${7-}"

  # -- the reviewer's verdict, first, because it is the only judgement here ----
  # Absence of `ai-review:pass` is no verdict, never a silent yes. The reviewer
  # sets exactly one of pass/blocking and it is the only place this rule may
  # read a judgement from.
  case " $labels " in
    *" ai-review:blocking "*) echo "HOLD ai-review:blocking is set"; return 0 ;;
  esac
  case " $labels " in
    *" agent:blocked "*) echo "HOLD agent:blocked is set"; return 0 ;;
  esac
  case " $labels " in
    *" needs-owner "*) echo "HOLD needs-owner is set"; return 0 ;;
  esac
  case " $labels " in
    *" ai-review:pass "*) : ;;
    *) echo "HOLD no ai-review:pass"; return 0 ;;
  esac

  # -- the checks --------------------------------------------------------------
  # "All green" over an empty list is vacuously true, and a pull request no
  # workflow touched has proved nothing. So the count is a condition of its own.
  if [ -z "${checks// /}" ]; then
    echo "HOLD no check run on the head commit"
    return 0
  fi
  local c
  for c in $checks; do
    case "$c" in
      success|skipped) : ;;
      *) echo "HOLD check run is $c"; return 0 ;;
    esac
  done

  # -- the other reviewer ------------------------------------------------------
  # Codex is configured on ChatGPT's side, not in this repository, and it does
  # not set `ai-review:pass` -- so a Codex finding reaches us only as a comment.
  # That it always arrives as a *review thread* was observed once, on #19, and
  # is not a contract anybody here can read. Both shapes are therefore counted,
  # and when in doubt this does not merge. Codex found a real hole on #19 that
  # Claude's reviewer did not, which is why this condition is not optional.
  if [ "${unresolved:-0}" != "0" ]; then
    echo "HOLD $unresolved unresolved review thread(s)"
    return 0
  fi
  if [ "${codex:-0}" != "0" ]; then
    echo "HOLD $codex unanswered comment(s) from the other reviewer"
    return 0
  fi

  # -- how old is the code -----------------------------------------------------
  # `committedDate` on the head, never the pull request's `updatedAt`. This
  # establishes that no session is still pushing, and `updatedAt` cannot answer
  # that: a label, a bot comment or this workflow's own note bumps it. What
  # matters is when code last arrived.
  case "${head_age:-}" in
    ''|*[!0-9]*) echo "HOLD head commit age unknown"; return 0 ;;
  esac
  if [ "$head_age" -lt "$MIN_HEAD_AGE_SECONDS" ]; then
    echo "HOLD head commit is $head_age s old, under $MIN_HEAD_AGE_SECONDS"
    return 0
  fi

  # -- draft, and the mergeable state ------------------------------------------
  # A draft reads `draft` in mergeStateStatus, never `clean`, so the draft check
  # comes first or every candidate this rule was written for would be declined
  # on the state check instead. Undrafting is a visible act on somebody else's
  # work: it happens only once everything else already holds, and it is never a
  # way to make a candidate qualify.
  if [ "${is_draft:-}" = "true" ]; then
    echo "READY"
    return 0
  fi
  if [ "${mergeable:-}" != "clean" ]; then
    echo "HOLD mergeable state is ${mergeable:-unknown}"
    return 0
  fi

  echo "MERGE"
}

# Callable as a command as well as a sourced function, so the workflow does not
# have to source anything and the test can do either.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_merge_candidate "$@"
fi
