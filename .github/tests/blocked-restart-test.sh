#!/usr/bin/env bash
# Restarting a blocked task must produce an outcome, not silence -- and a real
# blocker must not come back out of the pipeline as a success.
#
# THE PAIR, AGAIN. The watchdog's bounded retry escalates a task that has failed
# more than once since a person queued it: agent:blocked + needs-owner, plus a
# comment telling the reader how to start it again. The comment leads with
# `@claude`, because a comment event is the only start that does not need label
# surgery -- intake-decision.sh gates its claimed-state check on the `issues`
# and `workflow_dispatch` events only.
#
# It starts. It does not finish. `claude-agent.yml`'s Hand over step re-reads
# the labels at the end and, on finding agent:blocked, concludes the AGENT
# blocked itself during this run, writes no outcome comment, applies no
# agent:review and no agent:failed+agent:ready, and exits. It has no way to tell
# that apart from a label that was already there when the run started -- so the
# person who followed the instructions got a run that did the work and reported
# nothing. Found in review of #85; neither half's own tests could see it,
# because the escalation copy and the Hand over check live in different files
# and each is correct on its own.
#
# The first fix was one word in a list: the claim step stripped agent:blocked at
# the start, so agent:blocked at hand-over time could only mean this run applied
# it. THE SECOND REVIEW OF #85 SHOWED THAT ARGUMENT IS ISSUE-ONLY. Three of this
# workflow's five triggers fire on PULL REQUESTS, where claude-ci-repair.yml
# writes `ci:failed` + `agent:blocked` when repair gives up and asks for a
# human -- so the label CAN predate the run, and stripping it there would clear
# a human escalation and hand the branch to the unattended backstop, which
# requires agent:blocked absent and does not look at ci:failed.
#
# So the fact was recorded rather than inferred: the claim step read the labels
# BEFORE it changed any and wrote ATTADIPA_BLOCKED_BEFORE. AND THAT IS THE
# DEFECT #129 REPORTED, because the claim step then strips the very label it
# has just recorded. A pre-mutation snapshot cannot answer a post-mutation
# question, and on the exact route the escalation comment recommends it
# answered it backwards:
#
#   agent:blocked + needs-owner   the watchdog escalates
#   `@claude`                     a person restarts it, as instructed
#   BLOCKED_BEFORE=true           the claim step reads
#   agent:blocked removed         the claim step strips, snapshot now stale
#   agent:blocked + BLOCKED       the agent re-confirms the blocker and says why
#   "Done" + agent:review         the hand-over reads a `true` that describes a
#                                 label that no longer existed when it was read
#
# A first-class blocked outcome reported as a finished one. The claim step now
# re-reads AFTER its own edits and exports ATTADIPA_BLOCKED_AT_CLAIM, and the
# hand-over asks .github/scripts/blocked-outcome.sh.
#
# AND THIS FILE NOW EXECUTES THAT TRANSITION rather than asserting that its
# pieces are present. The previous version checked, separately, that something
# read the state, that something stripped the label and that the hand-over
# bailed on a variable -- three true statements about a sequence that was
# broken. #129 is right that a green structural test proved nothing here. The
# claim step's own shell is extracted out of the workflow and run against a stub
# `gh`, so the thing under test is the code that ships and not a transcription
# of it.

# The scripts this file sources are named relative to the repository root at
# run time, because of the `cd` below; shellcheck resolves them from this
# file's own directory, so it is told where that is before anything else runs.
# shellcheck source-path=SCRIPTDIR
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }

AGENT=.github/workflows/claude-agent.yml
REPAIR=.github/workflows/claude-ci-repair.yml
WATCHDOG=.github/workflows/agent-queue-watchdog.yml
INTAKE=.github/scripts/intake-decision.sh

echo "The claim step clears the label that would swallow this run's outcome"

# The stale-label list, read out of the file rather than assumed. It is now
# built as an array with one conditional member, so both the base list and the
# conditional addition are read.
STALE="$(sed -n 's/^[[:space:]]*stale_labels=(\(.*\))/\1/p' "$AGENT" | head -n 1)"
STALE_EXTRA="$(sed -n 's/^[[:space:]]*stale_labels+=(\(.*\))/\1/p' "$AGENT" | tr '\n' ' ')"
if [ -z "$STALE" ]; then
  no "the claim step still has a stale-label list" \
     "no 'stale_labels=(...)' found in $AGENT -- if the claim moved, point this test at it rather than deleting it"
else
  ok "the claim step still has a stale-label list ($STALE${STALE_EXTRA:+ + $STALE_EXTRA})"
  case " $STALE $STALE_EXTRA " in
    *" agent:blocked "*)
      ok "and it still clears agent:blocked somewhere, so the issue path keeps working" ;;
    *)
      no "and it still clears agent:blocked somewhere, so the issue path keeps working" \
         "agent:blocked is cleared nowhere; an issue the watchdog escalated and a person restarted with @claude will run to completion and report NOTHING -- no outcome comment, no label, no re-queue" ;;
  esac
  # ...but NOT unconditionally. On a pull request that label is
  # claude-ci-repair.yml's escalation to a human, and the unattended backstop
  # merges on its absence.
  case " $STALE " in
    *" agent:blocked "*)
      no "and it does not strip agent:blocked from a pull request unconditionally" \
         "agent:blocked is in the unconditional list; on a pull request that is claude-ci-repair.yml's escalation to a human, and removing it makes the branch backstop-eligible while ci:failed -- which the backstop does not read -- stays put" ;;
    *)
      case " $STALE_EXTRA " in
        *" agent:blocked "*)
          ok "and it does not strip agent:blocked from a pull request unconditionally" ;;
        *)
          no "and it does not strip agent:blocked from a pull request unconditionally" \
             "agent:blocked appears in neither list; see the assertion above" ;;
      esac ;;
  esac
  # The other direction: clearing too much is its own defect.
  case " $STALE " in
    *" needs-owner "*)
      no "and it does not clear needs-owner" \
         "needs-owner is a decision only the owner can make; starting a run does not make that decision" ;;
    *) ok "and it does not clear needs-owner" ;;
  esac
  case " $STALE " in
    *" agent:working "*)
      no "and it does not clear agent:working, which it is in the middle of applying" \
         "the claim adds agent:working; removing it in the same step is a race with itself" ;;
    *) ok "and it does not clear agent:working, which it is in the middle of applying" ;;
  esac
fi

echo
echo "...and it records the state it LEAVES, not the state it found"

if grep -q 'ATTADIPA_BLOCKED_AT_CLAIM=' "$AGENT"; then
  ok "the claim step exports ATTADIPA_BLOCKED_AT_CLAIM"
else
  no "the claim step exports ATTADIPA_BLOCKED_AT_CLAIM" \
     "nothing writes it; the hand-over is back to inferring whose label agent:blocked is, which is false on every pull request claude-ci-repair.yml has escalated and backwards on every issue the watchdog has"
fi

# THE ORDERING IS THE FIX. The end-to-end run below catches a read moved back
# above the strip, because it would report `true` again -- but the property is
# worth naming, so that a person reading the failure is told what the rule is
# rather than only that a scenario broke.
READ_LINE=$(grep -n 'ATTADIPA_BLOCKED_AT_CLAIM=' "$AGENT" | grep -v '::notice' | tail -n 1 | cut -d: -f1)
# shellcheck disable=SC2016  # `$stale` is the workflow's variable, matched literally
STRIP_LINE=$(grep -n 'label_edit --remove-label "\$stale"' "$AGENT" | tail -n 1 | cut -d: -f1)
if [ -n "$READ_LINE" ] && [ -n "$STRIP_LINE" ] && [ "$READ_LINE" -gt "$STRIP_LINE" ]; then
  ok "and it reads the labels after the strip (line $READ_LINE, strip at $STRIP_LINE), not before"
else
  no "and it reads the labels after the strip, not before" \
     "the read at line ${READ_LINE:-?} does not follow the removal loop at line ${STRIP_LINE:-?}; a snapshot taken before the claim mutates the labels cannot say whose label agent:blocked is afterwards -- that is #129"
fi

echo
echo "Hand over asks the decision file rather than reading the label itself"
HANDOVER_STEP="$(sed -n '/name: Hand over/,$p' "$AGENT")"
if printf '%s' "$HANDOVER_STEP" | grep -q 'blocked-outcome.sh'; then
  ok "the Hand over step calls .github/scripts/blocked-outcome.sh"
else
  no "the Hand over step calls .github/scripts/blocked-outcome.sh" \
     "the decision is back inside the workflow, where nothing can execute it; both of this rule's defects lived in exactly that YAML block"
fi
if [ -f .github/scripts/blocked-outcome.sh ]; then
  ok "and that file exists"
else
  no "and that file exists" ".github/scripts/blocked-outcome.sh is missing"
fi

# ...without being able to take the step down with it. The hand-over runs under
# `set -e` on whatever tree the action left behind, which on a run started from
# an old pull request is that branch (#133). A missing decision file must not
# turn into an aborted hand-over, because that is the silence being fixed.
# Structural, and it says so: nothing here can execute a step that has already
# died. The fallback must never be `silent`.
# shellcheck disable=SC2016  # `$(bash` is the workflow's text, matched literally
FALLBACK="$(printf '%s\n' "$HANDOVER_STEP" | sed -n '/BLOCKED_OUTCOME=\$(bash/,/^ *esac/p')"
if printf '%s\n' "$FALLBACK" | grep -q 'silent|report|normal' \
   && printf '%s\n' "$FALLBACK" | grep -q 'BLOCKED_OUTCOME=report' \
   && ! printf '%s\n' "$FALLBACK" | grep -q 'BLOCKED_OUTCOME=silent'; then
  ok "and an unrunnable decision file falls back to reporting, never to silence"
else
  no "and an unrunnable decision file falls back to reporting, never to silence" \
     "the call does not validate its answer against the three words and fall back from the labels alone; under set -e a missing script aborts the whole hand-over and the run reports nothing"
fi

# A BLOCKED OBJECT MUST NOT COME OUT LABELLED agent:review, AND `report` IS THE
# case that could. `silent` returns before the comment; `report` writes the
# comment -- because silence is the older defect -- and must then stop, since
# `agent:review` means "finished, somebody read this" and agent:failed +
# agent:ready means "back in the queue", neither of which is true of an object
# somebody has been asked to look at. The guard is a line-order property: its
# `exit 0` has to come before the first state label the step applies.
GUARD_LINE=$(printf '%s\n' "$HANDOVER_STEP" | grep -n 'BLOCKED_OUTCOME" = "report"' | head -n 1 | cut -d: -f1)
REVIEW_LINE=$(printf '%s\n' "$HANDOVER_STEP" | grep -n -- '--add-label agent:review' | head -n 1 | cut -d: -f1)
if [ -n "$GUARD_LINE" ] && [ -n "$REVIEW_LINE" ] && [ "$GUARD_LINE" -lt "$REVIEW_LINE" ]; then
  ok "and a reported blocker returns before any state label is applied"
else
  no "and a reported blocker returns before any state label is applied" \
     "the 'report' guard is at line ${GUARD_LINE:-absent} of the Hand over step and the first --add-label agent:review at line ${REVIEW_LINE:-absent}; a run that found agent:blocked already set would label the object as finished work"
fi

echo
echo "Every label edit knows which object it is editing"

# gh issue edit resolves repository.issue(number:), which does not resolve pull
# requests -- this repository has already had an error document from that field
# end up inside an outcome comment. Three of five triggers here fire on pull
# requests, and every call is `|| true`, so neither outcome would be reported.
if printf '%s' "$HANDOVER_STEP" | grep -qE '^\s*gh issue (edit|comment) '; then
  no "the Hand over step edits and comments through the object-aware helper" \
     "a bare 'gh issue edit/comment' survives in Hand over; on a pull request it either silently edits one while reasoning about issues, or silently does nothing -- see .github/scripts/gh-label.sh"
else
  ok "the Hand over step edits and comments through the object-aware helper"
fi

if [ -f .github/scripts/gh-label.sh ] && printf '%s' "$HANDOVER_STEP" | grep -q 'attadipa_label_edit'; then
  ok "and that helper exists and is the one it calls"
else
  no "and that helper exists and is the one it calls" \
     ".github/scripts/gh-label.sh is missing or unused; the split it encodes is the only thing keeping the pull request path honest"
fi

echo
echo "The escalation the check has to survive still exists"
if grep -q 'add-label agent:blocked' "$WATCHDOG"; then
  ok "the watchdog still escalates a repeatedly-failing task to agent:blocked"
else
  no "the watchdog still escalates a repeatedly-failing task to agent:blocked" \
     "the bounded retry no longer produces agent:blocked; if the escalation moved, re-derive whether the claim step still needs to clear it"
fi

ESCALATION="$(sed -n '/gh issue comment/,/|| true/p' "$WATCHDOG" | tr -d '\\`')"
if printf '%s' "$ESCALATION" | grep -q 'comment @claude'; then
  ok "and still offers the @claude route, which is the one this file exists for"
else
  no "and still offers the @claude route, which is the one this file exists for" \
     "if that route was withdrawn the defect is gone by another road -- delete this file deliberately rather than leaving it asserting a path nobody is told to take"
fi

echo
echo "And the @claude route really does reach the agent"
# The premise the escalation comment states in words: a comment event skips the
# claimed-state check. If the gate ever starts checking it for comments too, the
# comment is wrong and this test should say so before a person follows it.
if grep -qE 'issues|workflow_dispatch' "$INTAKE"; then
  ok "$INTAKE still scopes its claimed-state check by event"
else
  no "$INTAKE still scopes its claimed-state check by event" \
     "the gate no longer distinguishes events; re-read whether a comment on a blocked issue still starts a run at all"
fi

echo
echo "The other half of the same contract: a pull request can get back out"
# claude-ci-repair.yml raises `ci:failed` + `agent:blocked` when it gives up.
# Nothing removed them until #129, so a diagnosed and fixed pull request stayed
# permanently ineligible for an unattended merge. The exit exists, and it is a
# command rather than any `@claude`.
if grep -q 'ci-repair-reset.sh' "$REPAIR"; then
  ok "claude-ci-repair.yml has a reset path, and it is the executable decision"
else
  no "claude-ci-repair.yml has a reset path, and it is the executable decision" \
     "nothing calls .github/scripts/ci-repair-reset.sh; the escalation is a one-way door again -- merge-candidate.sh holds on agent:blocked by name"
fi
RESET_JOB="$(sed -n '/^  reset:/,$p' "$REPAIR")"
# shellcheck disable=SC2016  # `$label` is the workflow's variable, matched literally
if printf '%s' "$RESET_JOB" | grep -q 'remove-label "\$label"'; then
  ok "and it removes labels rather than only clearing a counter"
else
  no "and it removes labels rather than only clearing a counter" \
     "the reset job does not remove a label; the counter alone was the defect -- it is read on the NEXT CI failure and never touches agent:blocked"
fi
# WHICH labels, read out of the loop rather than assumed -- the same shape as
# the stale-label list above, and for the same reason: the list is the rule.
RESET_LABELS="$(printf '%s\n' "$RESET_JOB" | sed -n 's/^[[:space:]]*for label in \(.*\); do[[:space:]]*$/\1/p' | head -n 1)"
if [ -z "$RESET_LABELS" ]; then
  no "and the labels it clears can be read" \
     "no 'for label in ...; do' in the reset job; if the loop moved, point this test at it rather than deleting it"
else
  ok "and the labels it clears can be read ($RESET_LABELS)"
  case " $RESET_LABELS " in
    *" agent:blocked "*) ok "and agent:blocked is one of them, which is the hold the sweep reads" ;;
    *) no "and agent:blocked is one of them, which is the hold the sweep reads" \
          "the reset clears $RESET_LABELS; merge-candidate.sh holds on agent:blocked by name, so a green pull request stays ineligible" ;;
  esac
  case " $RESET_LABELS " in
    *" needs-owner "*) no "and needs-owner is not" \
                          "a decision only the owner can make is not undone by a repair command -- the claim step leaves it alone for the same reason" ;;
    *) ok "and needs-owner is not" ;;
  esac
fi

echo
echo "A finished run does not leave its pull request a draft"
# THE SECOND WAY WORK STOPS SHORT OF AN ANSWER. The prompt says to open the
# pull request as a draft while it is still moving and to mark it ready when it
# is not, and the second half was left to the model remembering. On 2026-08-22
# five finished pull requests -- #88, #92, #94, #95, #97 -- sat as drafts with
# green CI for hours. Nothing merges a draft, so six issues stayed open with
# their fix already written and their `Fixes #N` already in place, and the
# owner had to ask why. The step that decides the run is done marks it ready
# instead of asking the model to.
HANDOVER="$(sed -n '/read -r KIND/,/agent:failed --add-label agent:ready/p' "$AGENT")"
if printf '%s' "$HANDOVER" | grep -q 'gh pr ready'; then
  ok "the hand-over step marks the pull request ready for review"
else
  no "the hand-over step marks the pull request ready for review" \
     "no 'gh pr ready' in the hand-over; a finished pull request stays a draft until a person notices, and nothing merges a draft"
fi

# The other direction: promoting a half-finished pull request is worse than
# leaving it. A cut-off run is exactly what a draft is for.
#
# THIS ASSERTION USED TO BE UNABLE TO FAIL, which review of #85 caught and
# which is worth spelling out because the shape recurs. It was
# `grep -A3 'gh pr ready' | grep -q done_pr_cut`: `gh pr ready` occurs once, so
# -A3 saw the command and the two echoes AFTER it — while the `case "$KIND" in`
# and the `done_pr|done_here)` selector that decide which kinds reach it are
# ABOVE the match. Adding done_pr_cut to the selector left the test printing
# `ok`. So the guard is anchored on the case block instead: extract the whole
# `case "$KIND" in ... esac` that contains `gh pr ready`, and read its
# selectors. A test that cannot fail is worse than no test, in a file whose
# whole argument is that cross-file properties must be asserted.
READY_CASE="$(printf '%s\n' "$HANDOVER" | awk '
  /case "\$KIND" in/ { inblock = 1; buf = "" }
  inblock              { buf = buf $0 "\n" }
  /^[[:space:]]*esac/  { if (inblock && buf ~ /gh pr ready/) printf "%s", buf; inblock = 0 }
')"

if [ -n "$READY_CASE" ]; then
  ok "the 'gh pr ready' call sits in a case on \$KIND whose selectors can be read"
else
  no "the 'gh pr ready' call sits in a case on \$KIND whose selectors can be read" \
     "could not find a 'case \"\$KIND\" in ... esac' containing 'gh pr ready'; the guards below would pass vacuously, which is exactly the defect review of #85 found"
fi

# Now the selectors themselves, read from the lines that actually choose the
# kinds rather than from three lines of trailing echo.
READY_SELECTORS="$(printf '%s\n' "$READY_CASE" | grep -E '^[[:space:]]*[a-z_|]+\)[[:space:]]*$' || true)"

if printf '%s' "$READY_SELECTORS" | grep -q 'done_pr_cut\|done_here_cut'; then
  no "and it does not promote a cut-off run" \
     "a *_cut kind selects the 'gh pr ready' branch; half-finished work must stay a draft"
else
  ok "and it does not promote a cut-off run"
fi

if printf '%s' "$READY_SELECTORS" | grep -q 'done_here_nopush\|done_nopr'; then
  no "and it does not promote a run with nothing to promote" \
     "done_here_nopush pushed nothing and done_nopr has no pull request; neither can be marked ready"
else
  ok "and it does not promote a run with nothing to promote"
fi

# AND IT DOES NOT PROMOTE A PULL REQUEST THIS RUN DOES NOT OWN. For done_pr the
# number came from a closing keyword anybody can write, including an abandoned
# branch; undrafting is what makes a branch backstop-eligible, so it needs the
# ownership evidence handover-decision.sh already demands for done_here.
if printf '%s' "$HANDOVER" | grep -q 'promote-decision.sh'; then
  ok "and it asks promote-decision.sh whether this run owns the pull request"
else
  no "and it asks promote-decision.sh whether this run owns the pull request" \
     "nothing consults .github/scripts/promote-decision.sh; done_pr's number comes from whichever open pull request says 'Fixes #N', which an abandoned branch also says"
fi

if grep -q 'ATTADIPA_RUN_STARTED_AT=' "$AGENT"; then
  ok "and the run's start time it dates that against is recorded before the agent runs"
else
  no "and the run's start time it dates that against is recorded before the agent runs" \
     "nothing writes ATTADIPA_RUN_STARTED_AT; promote-decision.sh would hold every done_pr, leaving finished pull requests as drafts"
fi

if printf '%s' "$HANDOVER" | grep -q 'gh pr ready.*--undo'; then
  no "and it never puts one back to draft" \
     "--undo would overrule a person who deliberately marked the pull request draft again"
else
  ok "and it never puts one back to draft"
fi

# ---------------------------------------------------------------------------
# The transition itself, executed.
#
# Everything above reads the files. That is what #129 objected to and it was
# right: each of those statements was true on 7558728, where the sequence they
# describe was broken. So the claim step's own shell is lifted out of the
# workflow and RUN, against a stub `gh` that keeps a label set in a file. The
# code under test is therefore the code that ships -- extract a step that no
# longer exists and the harness says so rather than passing vacuously.
# ---------------------------------------------------------------------------
echo
echo "The transition, run end to end against a stub GitHub"

# step_script FILE "step name" -- the body of that step's `run: |` block,
# dedented. Nothing else in this repository needs it, so it lives here.
step_script() {
  awk -v want="$2" '
    $0 ~ "^[[:space:]]*- name: " want "[[:space:]]*$" { instep = 1; next }
    instep && /^[[:space:]]*- name: / { exit }
    instep && /^[[:space:]]*run: \|[[:space:]]*$/ {
      match($0, /^[[:space:]]*/); runindent = RLENGTH; inrun = 1; next
    }
    inrun {
      if ($0 ~ /^[[:space:]]*$/) { print ""; next }
      match($0, /^[[:space:]]*/)
      if (RLENGTH <= runindent) exit
      print substr($0, runindent + 3)
    }
  ' "$1"
}

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

CLAIM="$work/claim.sh"
step_script "$AGENT" "Normalise labels" > "$CLAIM"
if [ -s "$CLAIM" ] && grep -q 'stale_labels' "$CLAIM"; then
  ok "the claim step's shell can be extracted and run ($(wc -l < "$CLAIM") lines)"
else
  no "the claim step's shell can be extracted and run" \
     "nothing was extracted from the 'Normalise labels' step of $AGENT, or it does not look like the claim; every scenario below would pass vacuously -- point this at the step's new name rather than deleting it"
  printf '  %d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi

mkdir -p "$work/bin"
cat > "$work/bin/gh" <<'STUB'
#!/usr/bin/env bash
# A very small GitHub: one object, its labels in a file, and the calls the claim
# step actually makes. Every invocation is logged so a scenario can assert WHICH
# of `gh issue edit` and `gh pr edit` was used -- the split .github/scripts/
# gh-label.sh exists for, modelled here rather than assumed away.
set -u
state="${ATTADIPA_STUB_STATE:?stub state directory not set}"
printf '%s\n' "$*" >> "$state/calls"
kind=$(cat "$state/kind")

emit() {
  local names="" line
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    names="$names{\"name\":\"$line\"},"
  done < "$state/labels"
  names="${names%,}"
  if [ "$kind" = pr ]; then
    printf '{"number":7,"state":"open","pull_request":{"url":"u"},"labels":[%s]}\n' "$names"
  else
    printf '{"number":7,"state":"open","labels":[%s]}\n' "$names"
  fi
}

sub="${1-}"; shift || true
case "$sub" in
  api)
    if [ "${ATTADIPA_STUB_API_FAILS:-no}" = yes ]; then exit 1; fi
    case "${1-}" in
      */collaborators/*/permission)
        printf '%s\n' "${ATTADIPA_STUB_PERMISSION:-none}"; exit 0 ;;
    esac
    emit
    exit 0 ;;
  issue|pr)
    # `gh issue edit` resolves repository.issue(number:), which does not
    # resolve a pull request, and the reverse fails too.
    if [ "$sub" != "$kind" ]; then
      echo "could not resolve to a $sub" >&2
      exit 1
    fi
    case "${1-}" in
      view)
        # The only `--json labels --jq ...` this repository asks for. A stub,
        # so the projection is assumed rather than applied.
        paste -sd, "$state/labels"; exit 0 ;;
      comment)
        shift
        while [ $# -gt 0 ]; do
          case "$1" in
            --body-file) cp "$2" "$state/comment.md"; shift 2 ;;
            *) shift ;;
          esac
        done
        exit 0 ;;
    esac
    [ "${1-}" = edit ] || exit 0
    shift
    while [ $# -gt 0 ]; do
      case "$1" in
        --add-label)
          grep -Fxq -- "$2" "$state/labels" || printf '%s\n' "$2" >> "$state/labels"
          shift 2 ;;
        --remove-label)
          # Real `gh` errors when the label is not there, and the workflow's
          # `|| true` is what makes that survivable. Modelled rather than
          # smoothed over: the post-claim read exists because a removal can
          # fail, and a stub that always succeeds would hide that.
          if grep -Fxq -- "$2" "$state/labels"; then
            grep -Fxv -- "$2" "$state/labels" > "$state/labels.tmp" || true
            mv "$state/labels.tmp" "$state/labels"
          else
            echo "label $2 not found" >&2
            exit 1
          fi
          shift 2 ;;
        *) shift ;;
      esac
    done
    exit 0 ;;
esac
exit 0
STUB
chmod +x "$work/bin/gh"

state=""
scenario=0

# run_claim KIND [LABEL...] -- set the object up and execute the real claim step
run_claim() {
  local kind="$1"; shift
  scenario=$((scenario + 1))
  state="$work/s$scenario"
  mkdir -p "$state"
  printf '%s\n' "$kind" > "$state/kind"
  : > "$state/labels"
  : > "$state/calls"
  : > "$state/env"
  local l
  for l in "$@"; do printf '%s\n' "$l" >> "$state/labels"; done
  # The atomic Acquire exclusive claim step now runs before Normalise labels
  # and mirrors its winning ref as agent:working. This harness exercises the
  # normalisation/blocked transition, so begin at that real step boundary.
  printf 'agent:working\n' >> "$state/labels"
  # Each scenario runs in a subshell so the stub PATH and the fake environment
  # cannot leak into the next one. That is the point, not an accident.
  # shellcheck disable=SC2030,SC2031
  (
    PATH="$work/bin:$PATH"
    export PATH
    export ATTADIPA_STUB_STATE="$state"
    export GITHUB_ENV="$state/env"
    export GH_TOKEN=stub REPO=hleserg/Attadipa ISSUE=7
    export TASK_TYPE=continuous-review PRIORITY=P1 PRODUCER=chatgpt
    bash "$CLAIM"
  ) > "$state/log" 2>&1
}

# What the agent does next, and then what the hand-over decides.
agent_blocks()  { printf 'agent:blocked\n' >> "$state/labels"; }
labels_now()    { paste -sd, "$state/labels"; }
has()           { grep -Fxq -- "$1" "$state/labels"; }
claimed_state() { sed -n 's/^ATTADIPA_BLOCKED_AT_CLAIM=//p' "$state/env" | tail -n 1; }
decide()        { bash .github/scripts/blocked-outcome.sh "$(labels_now)" "$(claimed_state)"; }
is()            { if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "wanted '$3', got '$2'"; fi; }

echo
echo "  1. an issue the watchdog escalated, restarted with @claude, blocked again"
run_claim issue agent:blocked needs-owner agent:ready source:chatgpt
if has agent:blocked; then
  no "the claim clears agent:blocked from the issue" "it is still there after the claim step ran"
else
  ok "the claim clears agent:blocked from the issue"
fi
if has agent:working; then ok "and takes the claim"; else no "and takes the claim" "agent:working was not added"; fi
if has needs-owner; then ok "and leaves needs-owner alone"; else no "and leaves needs-owner alone" "needs-owner was removed; only the owner undoes that"; fi
if grep -q '^pr edit' "$state/calls"; then
  no "and edits it as an issue" "the claim used 'gh pr edit' on an issue"
else
  ok "and edits it as an issue"
fi
is "the recorded state is what the claim LEFT, not what it found" "$(claimed_state)" false
agent_blocks
is "so when the agent re-confirms the blocker, the hand-over stays quiet" "$(decide)" silent

echo
echo "  2. the same issue, but the agent finishes the work"
run_claim issue agent:blocked needs-owner agent:ready
is "the claim records the same cleared state" "$(claimed_state)" false
is "and an ordinary run gets an ordinary outcome" "$(decide)" normal

echo
echo "  3. a pull request carrying claude-ci-repair.yml's escalation, plus @claude"
run_claim pr ci:failed agent:blocked agent:claude
if has agent:blocked; then
  ok "the claim leaves the escalation on the pull request"
else
  no "the claim leaves the escalation on the pull request" \
     "agent:blocked was stripped; merge-candidate.sh holds on that label by name and the backstop requires it absent, so a comment would have made an escalated branch unattended-merge eligible"
fi
if has agent:working; then ok "and still takes the claim"; else no "and still takes the claim" "agent:working was not added"; fi
if grep -q '^issue edit' "$state/calls"; then
  no "and edits it as a pull request" "the claim used 'gh issue edit' on a pull request"
else
  ok "and edits it as a pull request"
fi
is "the recorded state says the label was already there" "$(claimed_state)" true
is "so the run reports what it did and changes no state label" "$(decide)" report

echo
echo "  4. the trusted exit: a person clears it, and the pull request is eligible again"
# shellcheck source=../scripts/merge-candidate.sh
. .github/scripts/merge-candidate.sh

# The pull request from scenario 3, now diagnosed and fixed: green checks, the
# reviewer's pass on the head commit, an old enough head, a path on the
# allowlist. Everything an unattended merge needs except the stale blocker --
# which is the whole complaint: nothing removed it, so this verdict was the
# permanent one.
green_verdict() {
  attadipa_merge_candidate "success success" "$1" 0 0 clean false 30000 \
    "docs/research/REUSE_LEDGER.md" true true 0123456789abcdef0123456789abcdef01234567
}
ESCALATED=$(printf 'agent:claude\nci:failed\nagent:blocked\nai-review:pass\n')
is "before the reset, the sweep holds on the stale blocker" \
   "$(green_verdict "$ESCALATED")" "HOLD agent:blocked is set"

RESET_STEP="$work/reset.sh"
step_script "$REPAIR" "Decide, and clear what the escalation put there" > "$RESET_STEP"
if [ -s "$RESET_STEP" ] && grep -q 'ci-repair-reset.sh' "$RESET_STEP"; then
  ok "the reset step's shell can be extracted and run too"
else
  no "the reset step's shell can be extracted and run too" \
     "nothing was extracted from claude-ci-repair.yml's reset step; the assertions below would pass vacuously"
fi

# run_reset ACTOR PERMISSION BODY -- an escalated pull request, and one comment
run_reset() {
  scenario=$((scenario + 1))
  state="$work/s$scenario"
  mkdir -p "$state"
  printf 'pr\n' > "$state/kind"
  printf '%s\n' "$ESCALATED" > "$state/labels"
  : > "$state/calls"
  # shellcheck disable=SC2030,SC2031
  (
    PATH="$work/bin:$PATH"
    export PATH
    export ATTADIPA_STUB_STATE="$state"
    export ATTADIPA_STUB_PERMISSION="$2"
    export GH_TOKEN=stub REPO=hleserg/Attadipa PR=7
    export ACTOR="$1" COMMENT_BODY="$3"
    bash "$RESET_STEP"
  ) > "$state/log" 2>&1
}

run_reset hleserg write '/ci-repair reset'
if has agent:blocked || has ci:failed; then
  no "the command from a person with write access clears both labels" \
     "still carrying $(labels_now)"
else
  ok "the command from a person with write access clears both labels"
fi
if has ai-review:pass && has agent:claude; then
  ok "and touches nothing else"
else
  no "and touches nothing else" "the reset removed a label it does not own: $(labels_now)"
fi
if [ -s "$state/comment.md" ]; then
  ok "and says on the pull request what it did"
else
  no "and says on the pull request what it did" \
     "no comment was written; a person who typed a documented command got a label change and silence"
fi
# The labels the step ACTUALLY left, handed to the rule that reads them. This
# is the sentence the Definition of Done asks for, executed rather than argued.
is "and the sweep now merges the very pull request it was holding" \
   "$(green_verdict "$(cat "$state/labels")")" MERGE

run_reset stranger read '/ci-repair reset'
if has agent:blocked; then
  ok "a stranger typing the same command changes nothing"
else
  no "a stranger typing the same command changes nothing" \
     "read access cleared a human escalation; the branch is now unattended-merge eligible"
fi

run_reset hleserg write '@claude have another go at this'
if has agent:blocked; then
  ok "and neither does a plain @claude from the owner"
else
  no "and neither does a plain @claude from the owner" \
     "any comment now dissolves the escalation -- that is the hole claude-agent.yml's claim step refuses to open, reopened one file over"
fi

echo
echo "  5. the pre-run read fails outright"
ATTADIPA_STUB_API_FAILS=yes run_claim issue
is "an unreadable object records unknown, not a guess" "$(claimed_state)" unknown
agent_blocks
is "and unknown reports rather than falling silent" "$(decide)" report

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
