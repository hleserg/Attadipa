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
# THE CONDITIONS ARE NOT NEW. They are the ones the owner already approved for
# the backstop routine (docs/automation/attadipa-backstop-routine.md, STEP 2),
# transcribed rather than reinvented, and this file must not add one or drop one
# to make a candidate qualify.
#
# AND THE FIRST VERSION DROPPED ONE, in the widening direction, which review
# caught. The `Limits:` block those six conditions live in also carries a PATH
# ALLOWLIST -- CLAUDE_AUTOMATION.md's "May be merged unattended" table, an owner
# decision of 2026-08-21 -- and this file had no path condition at all. That is
# not a smaller gate, it is a different one: an agent could open a pull request
# replacing this very rule, get it green (the test lives on that branch and
# asserts whatever the branch says), collect `ai-review:pass`, and have the
# sweep read the rule from `main`, correctly, and merge the branch that replaces
# it. A gate that can widen itself is not a gate -- the table says so in those
# words about `docs/automation/`, and it applies to `.github/` for the same
# reason. The allowlist is transcribed below and asserted in both directions.
#
# Widening it is the owner's to grant, not a reviewer's and not this file's.
# CLAUDE.md keeps "over every path" for the ORCHESTRATOR, which is a live
# session; `schedule:` is not one.

set -uo pipefail

# attadipa_merge_candidate CHECKS LABELS UNRESOLVED CODEX_UNANSWERED \
#                          MERGEABLE_STATE IS_DRAFT HEAD_AGE_SECONDS \
#                          CHANGED_PATHS PASS_AFTER_HEAD FACTS_COMPLETE \
#                          HEAD_OID
#
# CHECKS               one token per check run on the head commit, space
#                      separated, each the run's conclusion lowercased:
#                      `success`, `skipped`, `failure`, `cancelled`, ... An
#                      in-flight run has no conclusion and must be passed as
#                      `pending`. An EMPTY string means no check run exists.
#
#                      A COMMIT STATUS IS NOT A CHECK RUN, and the two arrive
#                      in one rollup. A third-party app posting a green commit
#                      status is evidence about that app, not evidence that
#                      this repository's CI ran: "Devin Review / success /
#                      Full review skipped: trial expired and no credits
#                      remaining" was, for a while, the ONLY context on a head
#                      commit whose workflows were all still waiting for
#                      approval -- so the combined state read `success` over a
#                      pull request nothing had looked at. Commit statuses are
#                      therefore passed with a `status:` prefix
#                      (`status:success`, `status:failure`). They still HOLD the
#                      merge when they are not green -- a red third party is
#                      information -- but they cannot satisfy the "some check
#                      ran" condition below, because they are not one.
# LABELS               label names currently on the pull request, ONE PER LINE.
#                      Newline separated rather than space separated because
#                      GitHub permits a space inside a label name, and a label
#                      called `x ai-review:pass` satisfied the space-separated
#                      test. Found in review.
# UNRESOLVED           count of unresolved review threads.
# CODEX_UNANSWERED     count of comments from chatgpt-codex-connector[bot] with
#                      no reply after them, review thread or not.
# MERGEABLE_STATE      GitHub's mergeStateStatus, lowercased.
# IS_DRAFT             `true` or `false`.
# HEAD_AGE_SECONDS     how long ago GITHUB saw this head arrive, in seconds.
#
#                      NOT `committedDate`, AND THE DIFFERENCE IS THE WHOLE
#                      POINT. Until #199 this was `(.pushedDate //
#                      .committedDate)` -- and `pushedDate` is deprecated and
#                      answers `null` for every commit this repository has, so
#                      the fallback was the only branch taken and the settling
#                      window was measured on the git committer clock. A commit
#                      made with `GIT_COMMITTER_DATE` six hours in the past
#                      cleared the window at the moment it was created. The
#                      caller now derives this from
#                      `workflowRun.createdAt` on the head commit's own check
#                      suites -- see .github/scripts/merge-head-trust.jq -- and
#                      holds outright where GitHub has stamped nothing.
# CHANGED_PATHS        every path the pull request touches, ONE PER LINE,
#                      repository-relative. EMPTY means the caller could not
#                      read them, which holds -- an unknown change set is not a
#                      permitted one.
# PASS_AFTER_HEAD      `true` when the most recent `labeled ai-review:pass`
#                      event is not older than GITHUB'S OWN RECORD of this head
#                      arriving; `false` when it is older; `unknown` when the
#                      caller could not tell. Only `true` merges.
#
#                      "Not older than the head commit" was the first wording
#                      and it hid the defect in #199: the head commit's date is
#                      whatever its committer put there, so a backdated commit
#                      B made the previous head's label look like a verdict
#                      about B. Both sides of the comparison are now GitHub's
#                      own -- a `LabeledEvent.createdAt` against a
#                      `workflowRun.createdAt` on that object id -- so a change
#                      of head invalidates the previous verdict whatever date
#                      the new commit carries.
#
#                      WHY THIS IS A CONDITION AND NOT AN ASSUMPTION. The label
#                      records that a verdict was reached, not WHICH COMMIT it
#                      was reached on. The reviewer passes commit A, the agent
#                      pushes B, and the review of B reaches no verdict at all
#                      -- a spent quota, a `cancel-in-progress` cancellation, an
#                      actor refusal, or the workflow-validation skip, which
#                      that workflow reports as SUCCESS because its Review step
#                      carries `continue-on-error`. Nothing removes the label,
#                      so six hours later B merges reviewed by nothing. The
#                      backstop routine guards this and calls it "the likeliest
#                      of these to recur"; the first version of this file did
#                      not transcribe it. Found in review.
#
# FACTS_COMPLETE       `true` when the caller PROVED it read all of every set
#                      the arguments above were computed from. `false` when it
#                      proved it had not; anything else, including the argument
#                      not being passed at all, is a caller that cannot say.
#                      Only `true` merges.
#
#                      AND ITS REACH IS THE GRAPHQL DOCUMENT, WHICH IS NARROWER
#                      THAN THAT SENTENCE SOUNDS. `merge-facts.sh` sees one
#                      reply and answers about the six connections in it.
#                      CODEX_UNANSWERED is computed in the caller from two REST
#                      reads the filter never sees (pr-merge-sweep.yml around
#                      :268-291), so a `true` beside it is a statement about the
#                      GraphQL half only. That is safe TODAY -- both are
#                      `--paginate --slurp`, and an error on either holds -- and
#                      it is written here because this repository has already
#                      shipped exactly one silent `first: N` ceiling, and the
#                      next `?per_page=100` added there would arrive underneath
#                      a `true` that reads as having ruled it out. If a REST
#                      read here ever stops paginating, this argument stops
#                      covering it and something else has to.
#
#                      WHY THE CALLER HAS TO PROVE THIS, AND WHY NO CONDITION
#                      ON THE OTHER ARGUMENTS COULD HAVE. Every one of them is
#                      a summary: `UNRESOLVED` is a count, `LABELS` is a list,
#                      `CHECKS` is a string of tokens -- and not one of them
#                      carries any trace of how much the caller managed to read
#                      before summarising. A GraphQL connection is a PAGE.
#                      `reviewThreads(first:100)` over a pull request with 101
#                      threads returns a hundred, and if the unresolved one is
#                      the hundred-and-first then `UNRESOLVED` arrives as `0` --
#                      which is the value that merges. This rule cannot see that
#                      in the `0`, and never could. The fail-open was not in the
#                      decision, it was at the boundary, which is why the fix is
#                      a new argument rather than another test on the old ones.
#                      Issue #170. `.github/scripts/merge-facts.sh` is what
#                      establishes it, out of `pageInfo` rather than out of the
#                      length of whatever came back.
#
# HEAD_OID             the object id of the commit this decision is about, as
#                      GitHub named it in the same snapshot the arguments above
#                      came from. Forty (or sixty-four) lowercase hex digits;
#                      anything else, including an empty string, holds.
#
#                      WHY A RULE WITH NO REPOSITORY ACCESS IS GIVEN A SHA. It
#                      cannot verify one, and it is not asked to. What it can do
#                      is refuse a caller that reached this point without ever
#                      establishing WHICH COMMIT it was about -- and that is a
#                      caller whose `PASS_AFTER_HEAD` was computed against
#                      nothing. #199 is the shape: two security properties were
#                      derived from a date the commit carried rather than from
#                      the commit's identity, and no argument in the list above
#                      made that visible, because a boolean carries no trace of
#                      what it was a boolean ABOUT. The log line naming the
#                      merged commit is the smaller half of the reason.
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

# THE PATH ALLOWLIST, transcribed from CLAUDE_AUTOMATION.md's "May be merged
# unattended" table. It is an allowlist rather than `docs/` minus exclusions
# because a list of what is permitted fails closed when somebody adds a
# directory, and a list of what is forbidden fails open -- the table says so,
# and a rule holding unattended write access to `main` takes the one that fails
# closed. Keep the two in step; .github/tests/merge-candidate-test.sh asserts
# every row of the table in both directions.
ATTADIPA_MERGE_ALLOWED_PREFIXES="docs/architecture/ docs/community/ docs/hardware/ docs/mobile/ docs/node/ docs/research/ docs/testing/ docs/ui/ docs/upstream/"
# STATUS.md and TASKS.md are on the list because CLAUDE.md *requires* them in
# the same commit as the change they describe -- excluding them would disqualify
# every compliant pull request.
ATTADIPA_MERGE_ALLOWED_FILES="STATUS.md TASKS.md"
# Inside an allowed prefix and still refused: the one file in docs/research/
# that records authority rather than findings. "Not ours to overturn", in its
# own words.
ATTADIPA_MERGE_DENIED_FILES="docs/research/OWNER_DECISIONS.md"

# attadipa_merge_has_label LABELS NAME -- 0 when NAME is one of LABELS.
#
# LABELS arrives one per line and the match is on a WHOLE line, so a label whose
# name contains another label's name cannot satisfy it. The first version joined
# labels with spaces and matched a substring, so a label literally called
# `x ai-review:pass` passed the verdict check. Found in review.
attadipa_merge_has_label() {
  printf '%s\n' "$1" | grep -Fxq -- "$2"
}

# attadipa_merge_path_allowed PATH -- 0 when the table permits it unattended.
attadipa_merge_path_allowed() {
  local path="$1" entry
  [ -n "$path" ] || return 1
  for entry in $ATTADIPA_MERGE_DENIED_FILES; do
    [ "$path" = "$entry" ] && return 1
  done
  for entry in $ATTADIPA_MERGE_ALLOWED_FILES; do
    [ "$path" = "$entry" ] && return 0
  done
  for entry in $ATTADIPA_MERGE_ALLOWED_PREFIXES; do
    case "$path" in
      "$entry"*)
        # A prefix match must be a DIRECTORY match: `docs/uix/y` must not be
        # admitted by the `docs/ui/` row. The trailing slash in the entry does
        # that, and this case is asserted.
        return 0 ;;
    esac
  done
  return 1
}

attadipa_merge_candidate() {
  local argc="$#"
  local checks="${1-}" labels="${2-}" unresolved="${3-}" codex="${4-}"
  local mergeable="${5-}" is_draft="${6-}" head_age="${7-}"
  local paths="${8-}" pass_after_head="${9-}" facts_complete="${10-}"
  local head_oid="${11-}"

  # -- did the caller read all of it? ------------------------------------------
  # BEFORE EVERYTHING ELSE, because every condition below is computed from the
  # snapshot this one is about. A count of unresolved threads taken over the
  # first page of them is not a smaller fact than the real one, it is a
  # different fact wearing its clothes -- and it is wrong in the merging
  # direction. See FACTS_COMPLETE above and issue #170.
  #
  # NINE ARGUMENTS IS THE OLD CALLER, AND THE OLD CALLER IS THE DEFECT. It read
  # bounded pages and never asked whether there were more, so there is no
  # reading of its nine arguments under which this rule may merge. Refusing it
  # by arity rather than by an empty tenth argument is deliberate: an empty
  # string is something a caller can pass by accident, while nine arguments is
  # a caller that predates the condition entirely, and the two deserve
  # different sentences in the log. The message names the fix because this line
  # is what a reader will see 48 times a day until somebody applies it.
  #
  # ELEVEN SINCE #199, AND STILL ONE PATCH AND ONE TRANSITION. That fix parked
  # its caller edits in the same file rather than beside it, precisely so that
  # this number moves once. The live sweep passes nine today and eleven the
  # moment the patch lands; there is no state in between, and no second "apply
  # this one first" to reconcile. T-144.
  if [ "$argc" -lt 11 ]; then
    # Also to **stderr**, as a workflow warning. The caller turns every HOLD
    # into a `::notice::` and carries on, so the job stays green and reads
    # "sweep finished, 0 merged" -- the same line a sweep with nothing to do
    # prints, 48 times a day, while the sweep is in fact disabled. This file
    # already carries that lesson for the empty-repository case; it was not
    # applied to the one state in which the gate refuses *everything*. Only
    # stdout is captured into the caller's verdict and compared by the tests,
    # so this reaches the run log without changing either.
    echo "::warning::the merge sweep is holding every pull request: its caller predates the completeness condition. Apply docs/automation/pending/170-merge-sweep-completeness.patch (T-144)." >&2
    echo "HOLD this caller cannot prove it read all of the pull request; apply docs/automation/pending/170-merge-sweep-completeness.patch"
    return 0
  fi
  case "${facts_complete:-}" in
    true) : ;;
    false) echo "HOLD the facts read about this pull request were truncated"; return 0 ;;
    *)     echo "HOLD could not tell whether the facts read about this pull request are complete"; return 0 ;;
  esac

  # -- and WHICH COMMIT is this a decision about? -------------------------------
  # Beside the completeness question and for the same reason: a rule that cannot
  # name the commit it is deciding on cannot have bound anything to it. See
  # HEAD_OID above and issue #199. Shape only -- nothing here can verify a SHA,
  # and the caller that establishes it is
  # .github/scripts/merge-head-trust.sh.
  case "${head_oid:-}" in
    *[!0-9a-f]*|"") echo "HOLD the head commit could not be identified"; return 0 ;;
  esac
  case "${#head_oid}" in
    40|64) : ;;
    *) echo "HOLD the head commit could not be identified"; return 0 ;;
  esac

  # -- the reviewer's verdict, first, because it is the only judgement here ----
  # Absence of `ai-review:pass` is no verdict, never a silent yes. The reviewer
  # sets exactly one of pass/blocking and it is the only place this rule may
  # read a judgement from.
  if attadipa_merge_has_label "$labels" "ai-review:blocking"; then
    echo "HOLD ai-review:blocking is set"; return 0
  fi
  if attadipa_merge_has_label "$labels" "agent:blocked"; then
    echo "HOLD agent:blocked is set"; return 0
  fi
  if attadipa_merge_has_label "$labels" "needs-owner"; then
    echo "HOLD needs-owner is set"; return 0
  fi
  if ! attadipa_merge_has_label "$labels" "ai-review:pass"; then
    echo "HOLD no ai-review:pass"; return 0
  fi

  # -- and WHICH COMMIT that verdict was reached on ----------------------------
  # See PASS_AFTER_HEAD above. Only an explicit `true` passes: `unknown` is a
  # read that failed, and a rule with unattended write access to `main` does not
  # merge on a fact it could not establish.
  case "${pass_after_head:-}" in
    true) : ;;
    false) echo "HOLD ai-review:pass predates the head commit"; return 0 ;;
    *)     echo "HOLD could not tell whether ai-review:pass covers the head commit"; return 0 ;;
  esac

  # -- what it touches ---------------------------------------------------------
  # The allowlist above, which is the owner's decision of 2026-08-21 rather than
  # this rule's opinion. An empty path list is a read that failed, and holds.
  if [ -z "${paths//[$'\n'[:space:]]/}" ]; then
    echo "HOLD could not read which paths this changes"
    return 0
  fi
  local path
  while IFS= read -r path; do
    [ -n "$path" ] || continue
    if ! attadipa_merge_path_allowed "$path"; then
      echo "HOLD $path is not on the unattended-merge allowlist"
      return 0
    fi
  done <<EOF
$paths
EOF

  # -- the checks --------------------------------------------------------------
  # "All green" over an empty list is vacuously true, and a pull request no
  # workflow touched has proved nothing. So the count is a condition of its own
  # -- and it counts CHECK RUNS, never commit statuses. See the CHECKS contract
  # above: a third-party app's green status is evidence about that app.
  local c runs=0
  for c in $checks; do
    case "$c" in
      status:success|status:skipped) : ;;
      status:*)   echo "HOLD commit status is ${c#status:}"; return 0 ;;
      success|skipped) runs=$((runs + 1)) ;;
      *)          echo "HOLD check run is $c"; return 0 ;;
    esac
  done
  if [ "$runs" -eq 0 ]; then
    echo "HOLD no check run on the head commit"
    return 0
  fi

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
  # WHEN GITHUB SAW THE HEAD ARRIVE, which is neither of the two things this
  # comment used to offer. Not the pull request's `updatedAt`, which a label, a
  # bot comment or this workflow's own undrafting bumps -- that answers "has
  # anything happened here", not "has code arrived". And no longer
  # `committedDate` either: that is the git committer clock, an input, and
  # `GIT_COMMITTER_DATE=2020-01-01` cleared this window at the moment the commit
  # was made. Both properties that depended on it are now derived from
  # `workflowRun.createdAt` on the head's own check suites, which GitHub writes.
  # Issue #199.
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
