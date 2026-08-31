#!/usr/bin/env bash
# A full queue must refuse to START a writer without losing the REQUEST (#293).
#
# The defect this guards was not inside any script: it was the order of two
# steps and one `if:` expression. `writer-admission.sh` ran before
# `intake-decision.sh`, and `Decide` was conditional on the admission verdict,
# so a `full` queue skipped the decision entirely -- no `run=true`, no
# `run=false`, no refusal output -- and every downstream job was skipped with
# it. Run 33048001621 finished green in ten seconds and lost the owner's
# verified NVS/RTC finding on #264.
#
# A unit test of either script would have passed throughout: both were correct.
# So this file asserts the two things a unit test cannot see -- the condition
# graph in the shipping workflow, and the shell of the step that answers --
# and then mutates the condition back to prove the assertions are load-bearing.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }
has() { if printf '%s' "$2" | grep -qF -- "$3"; then ok "$1"; else no "$1" "expected to find '$3'"; fi; }
hasnt() { if printf '%s' "$2" | grep -qF -- "$3"; then no "$1" "did not expect '$3'"; else ok "$1"; fi; }
say() { if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "wanted '$3', got '$2'"; fi; }

WF=.github/workflows/claude-agent.yml
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

# The `if:` that belongs to a named step, and nothing else. A step's condition
# is the line beginning `if:` between its `- name:` and the next one.
step_if() {
  awk -v want="$1" '
    index($0, "- name: " want) { instep = 1; next }
    instep && $0 ~ /^[[:space:]]*- name: / { exit }
    instep && $0 ~ /^[[:space:]]*if:[[:space:]]/ { sub(/^[[:space:]]*if:[[:space:]]*/, ""); print; exit }
  ' "$2"
}

# The same extractor review-invalidate-workflow-test.sh and
# orchestration-bundle-test.sh use: a `run: |` body that takes every value
# through `env:` runs outside the runner, and one that interpolates `${{ }}`
# does not.
extract_run_block() {
  awk -v want="$1" '
    index($0, "- name: " want) { instep = 1; next }
    instep && $0 ~ /^[[:space:]]*run: \|[[:space:]]*$/ {
      match($0, /^[[:space:]]*/); indent = RLENGTH; inrun = 1; next
    }
    inrun {
      if ($0 ~ /^[[:space:]]*$/) { print ""; next }
      match($0, /^[[:space:]]*/)
      if (RLENGTH <= indent) exit
      print substr($0, indent + 3)
    }
  ' "$2"
}

# A job's `if:`, which sits at the job's own indentation rather than a step's.
job_if() {
  awk -v want="$1" '
    $0 ~ "^  " want ":[[:space:]]*$" { injob = 1; next }
    injob && $0 ~ /^  [a-z_-]+:[[:space:]]*$/ { exit }
    injob && $0 ~ /^    if:[[:space:]]/ { sub(/^    if:[[:space:]]*/, ""); print; exit }
  ' "$2"
}

echo "  reading  $WF"

# ---------------------------------------------------------------- vacuous-pass
# Every assertion below is a grep against extracted text, and a grep that finds
# nothing because a step was RENAMED looks exactly like a grep that finds
# nothing because the defect is fixed. So the existence of each thing being
# asserted about is checked first, and a rename fails here loudly.
DECIDE_IF=$(step_if "Decide" "$WF")
UNDERSTOOD_IF=$(step_if "Say what was understood" "$WF")
DEFERRED_IF=$(step_if "Say it is deferred" "$WF")
AGENT_IF=$(job_if agent "$WF")
DEFERRED_RUN=$(extract_run_block "Say it is deferred" "$WF")
CLAIM_RUN=$(extract_run_block "Acquire exclusive claim" "$WF")

missing=
[ -n "$DECIDE_IF" ]     || missing="$missing 'Decide'"
[ -n "$UNDERSTOOD_IF" ] || missing="$missing 'Say what was understood'"
[ -n "$DEFERRED_IF" ]   || missing="$missing 'Say it is deferred'"
[ -n "$AGENT_IF" ]      || missing="$missing job 'agent'"
[ -n "$DEFERRED_RUN" ]  || missing="$missing 'Say it is deferred' run block"
[ -n "$CLAIM_RUN" ]     || missing="$missing 'Acquire exclusive claim' run block"
if [ -n "$missing" ]; then
  no "every step and job this test asserts about still exists" \
     "not found in $WF:$missing -- if something was renamed, re-point this test rather than deleting it"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
ok "every step and job this test asserts about still exists"

# ------------------------------------------------------------- condition graph
# THE DEFECT'S ACTUAL LOCATION. `Decide` must not depend on writer capacity:
# that dependency is what made a full queue skip the decision and answer
# nothing.
hasnt "Decide does not depend on writer admission" "$DECIDE_IF" "admission"
has   "Decide still respects the kill switch"      "$DECIDE_IF" "steps.switch.outputs.enabled == 'true'"

# ... and the capacity check has to still exist, on the job that writes code.
has "the agent job requires admission before it starts" "$AGENT_IF" "needs.gate.outputs.admission_allow == 'true'"
has "the agent job still requires acceptance"           "$AGENT_IF" "needs.gate.outputs.run == 'true'"

GATE_OUTPUTS=$(awk '/^    outputs:/ { on = 1; next } on && /^    [a-z]/ { exit } on' "$WF")
has "the gate publishes the admission verdict"  "$GATE_OUTPUTS" "admission_allow:"
has "the gate publishes the admission state"    "$GATE_OUTPUTS" "admission:"
has "the gate publishes the admission count"    "$GATE_OUTPUTS" "admission_count:"

# The two receipts are mutually exclusive: exactly one comment per request.
has "the accepted receipt is posted only when a writer may start" \
    "$UNDERSTOOD_IF" "needs.gate.outputs.admission_allow == 'true'"
has "the deferred receipt is posted only when one may not" \
    "$DEFERRED_IF" "needs.gate.outputs.admission_allow != 'true'"

# The race the gate cannot cover: capacity present at the gate, gone by the
# time the writer job reaches its recheck under the lease. That path released
# the lease and left a run-log notice, which is the same silent drop one step
# later.
has "the writer's own admission recheck leaves a durable signal" \
    "$CLAIM_RUN" "--add-label agent:ready"

# ------------------------------------------------------------------ executable
# The shell of the deferred step, run for real against a recording `gh`.
stub() {
  mkdir -p "$work/bin" "$work/state" || return 1
  : > "$work/state/comments"
  : > "$work/state/labels"
  # THE STUB DISTINGUISHES issue FROM pr ON PURPOSE. `gh issue edit` resolves
  # through GraphQL `repository.issue(number:)`, which does not resolve a pull
  # request -- so a stub that answers both the same way cannot see the defect
  # where a step edits a pull request's labels through the issue command. It is
  # recorded with its object kind, and `gh issue edit` against a pull request
  # exits non-zero the way the real one does. `blocked-restart-test.sh` models
  # the same split.
  cat > "$work/bin/gh" <<STUB
#!/usr/bin/env bash
case "\$1 \$2" in
  "api repos/"*)
    printf '%s' "\$ATTADIPA_STUB_LABELS" ;;
  "issue edit")
    if [ "\$ATTADIPA_STUB_KIND" = pr ]; then
      echo 'GraphQL: Could not resolve to an Issue with the number.' >&2
      exit 1
    fi
    shift 2
    printf 'issue %s\n' "\$*" >> "$work/state/labels" ;;
  "pr edit")
    shift 2
    printf 'pr %s\n' "\$*" >> "$work/state/labels" ;;
  "issue comment"|"pr comment")
    printf '%s\n' "\$1" >> "$work/state/commented-as"
    for a in "\$@"; do
      if [ "\$prev" = --body-file ]; then cat "\$a" >> "$work/state/comments"; fi
      prev=\$a
    done ;;
  *) echo "unexpected gh: \$*" >&2; exit 90 ;;
esac
STUB
  chmod +x "$work/bin/gh"
}
stub || exit 1

# run_deferred LABELS STATE COUNT [TARGET_KIND]
#
# TARGET_KIND defaults to `issue`; pass `pr` for the three triggers that fire on
# pull requests. RC is kept because the step runs under `set -euo pipefail` and
# the failure this test exists to catch is one that dies BEFORE the comment.
# shellcheck disable=SC2030,SC2031  # the subshell is the point: it scopes the
# step's environment to one case and leaves the next case a clean one.
run_deferred() {
  : > "$work/state/comments"; : > "$work/state/labels"
  : > "$work/state/commented-as"
  ( export PATH="$work/bin:$PATH"
    export ATTADIPA_STUB_LABELS="$1"
    export ATTADIPA_STUB_KIND="${4:-issue}"
    export GH_TOKEN=stub REPO=hleserg/Attadipa ISSUE=264
    export TARGET_KIND="${4:-issue}"
    export TASK_TYPE=continuous-review PRIORITY=P1
    export STATE="$2" COUNT="$3"
    export TRIGGER=issue_comment ACTOR=hleserg RUN_URL=http://run/1
    printf '%s\n' "$DEFERRED_RUN" > "$work/deferred.sh"
    bash "$work/deferred.sh" >/dev/null 2>&1 )
  RC=$?
  COMMENT=$(cat "$work/state/comments")
  LABELS_SET=$(cat "$work/state/labels")
  COMMENTED_AS=$(cat "$work/state/commented-as")
}

# 1. Trusted comment, queue `full`, ordinary unclaimed issue -- the #264 case.
run_deferred "" full 2
has   "a full queue answers on the issue"            "$COMMENT" "attadipa-deferred"
has   "the answer names writer capacity as the cause" "$COMMENT" "open pull request limit"
has   "the answer says the request survived"          "$COMMENT" "the request is not lost"
has   "the task is re-queued for the watchdog"        "$LABELS_SET" "--add-label agent:ready"
hasnt "nothing claims the task while it is deferred"  "$LABELS_SET" "agent:working"

# 2. `incident` and fail-closed `unknown` take the same visible path.
run_deferred "" incident 3
has "an incident answers too"       "$COMMENT" "attadipa-deferred"
has "an incident is named as such"  "$COMMENT" "incident"
has "an incident is still re-queued" "$LABELS_SET" "--add-label agent:ready"

run_deferred "" unknown unknown
has "a fail-closed unknown answers too"   "$COMMENT" "attadipa-deferred"
has "an unreadable count is said plainly" "$COMMENT" "could not be read"

# 3. An override comment on a task already under review. `queue-scan.jq` drops
#    agent:review, and intake-decision.sh rejects a dispatch carrying it, so
#    agent:ready would be an inert label promising a pickup that cannot happen.
run_deferred "agent:review,priority:P1" full 2
has   "a reviewing task still gets an answer"      "$COMMENT" "attadipa-deferred"
hasnt "and is not given an inert agent:ready"      "$LABELS_SET" "agent:ready"
has   "and is told which path actually restarts it" "$COMMENT" '@claude'

run_deferred "agent:blocked" full 2
hasnt "a blocked task is not given an inert label either" "$LABELS_SET" "agent:ready"

# 4. A PULL REQUEST target. Three of this workflow's five triggers fire on one,
#    so `needs.gate.outputs.issue` is a pull request number whenever an agent is
#    started from a review comment. `gh issue edit` does not resolve those, and
#    under `set -euo pipefail` the step would die at the label edit -- BEFORE
#    the comment it exists to post. That is the same silence #293 is about, on
#    the path this workflow uses most.
run_deferred "" full 2 pr
say   "a deferral on a pull request does not die at the label edit" "$RC" "0"
has   "a deferral on a pull request still answers"     "$COMMENT" "attadipa-deferred"
hasnt "and never reaches for gh issue edit"            "$LABELS_SET" "issue --add-label"
say   "and comments as a pull request, not by gh's accident" "$COMMENTED_AS" "pr"

#    ...and the label is not added there at all: queue-scan.jq selects
#    `.pull_request == null`, so the watchdog never picks a pull request up and
#    agent:ready on one is an inert label promising a pickup that cannot happen.
hasnt "a pull request is not given an inert agent:ready" "$LABELS_SET" "agent:ready"
has   "and the receipt says the watchdog is not coming"  "$COMMENT" "does not pick up pull requests"

# 5. `parked` is an owner hold, not a capacity answer. `wip-limit.sh` exempts a
#    parked pull request from the open count entirely, so nothing about the
#    queue draining lifts it -- and the receipt must not say capacity, because
#    the reader would then wait for the wrong event.
run_deferred "queue:parked" parked 1 pr
has   "a parked target still gets an answer"           "$COMMENT" "attadipa-deferred"
hasnt "a parked target is not given an inert label"    "$LABELS_SET" "agent:ready"
has   "and is told it is an owner hold"                "$COMMENT" "owner hold"
hasnt "and is NOT told to wait for capacity"           "$COMMENT" "about writer capacity and nothing else"
has   "and is told merging will not lift it"           "$COMMENT" "no amount of merging lifts it"

# 6. The second site the same blindness lives at: the writer's own admission
#    recheck under the lease. `|| true` there means a pull request does not fail
#    -- it silently does nothing, which is the outcome its own comment claims to
#    remove.
has "the lease recheck asks which object it is editing" "$CLAIM_RUN" "TARGET_KIND"
has "the gate publishes which object it is"             "$GATE_OUTPUTS" "target_kind:"

# ------------------------------------------------------------------- mutation
# Restore the defect on a copy: `Decide` conditional on admission again. Every
# assertion above is a grep, and a grep proves nothing unless putting the bug
# back makes it fail.
cp "$WF" "$work/mutant.yml" || exit 1
perl -0pi -e "s/(- name: Decide\n        id: decide\n        if: steps\.switch\.outputs\.enabled == 'true')/\$1 && steps.admission.outputs.allow == 'true'/" "$work/mutant.yml"
MUT_IF=$(step_if "Decide" "$work/mutant.yml")
if [ "$MUT_IF" = "$DECIDE_IF" ]; then
  no "the mutation actually changed Decide's condition" \
     "the perl substitution matched nothing -- this test can no longer prove anything"
elif printf '%s' "$MUT_IF" | grep -qF "admission"; then
  ok "restoring 'admit before decide' is detected by the assertion above"
else
  no "restoring 'admit before decide' is detected by the assertion above" \
     "mutant Decide if is '$MUT_IF'"
fi

# The pull-request half needs its own mutation, and it has to be run rather than
# grepped: the two defects it guards are a step that DIES and a label that lands
# where it is inert, neither of which is visible in the text.
# shellcheck disable=SC2030,SC2031  # same subshell isolation as run_deferred.
run_mutant_deferred() {
  : > "$work/state/comments"; : > "$work/state/labels"
  : > "$work/state/commented-as"
  ( export PATH="$work/bin:$PATH"
    export ATTADIPA_STUB_LABELS="" ATTADIPA_STUB_KIND=pr
    export GH_TOKEN=stub REPO=hleserg/Attadipa ISSUE=264 TARGET_KIND=pr
    export TASK_TYPE=continuous-review PRIORITY=P1 STATE=full COUNT=2
    export TRIGGER=issue_comment ACTOR=hleserg RUN_URL=http://run/1
    printf '%s\n' "$1" > "$work/mutant.sh"
    bash "$work/mutant.sh" >/dev/null 2>&1 )
  MLABELS=$(cat "$work/state/labels")
}

# The stub itself, first. Case 4's "never reaches for gh issue edit" is only
# worth anything if the stub would have noticed -- and the shipping step no
# longer has a path that calls it on a pull request, so nothing else proves the
# stub is still able to tell the two commands apart.
# shellcheck disable=SC2031  # scoped on purpose, like the helpers above.
if ( export PATH="$work/bin:$PATH" ATTADIPA_STUB_KIND=pr
     gh issue edit 264 --repo x --add-label agent:ready ) >/dev/null 2>&1; then
  no "the stub refuses gh issue edit on a pull request, as the real one does" \
     "it exited 0, so every assertion about the two commands above is vacuous"
else
  ok "the stub refuses gh issue edit on a pull request, as the real one does"
fi

# `gh issue comment` DOES resolve a pull request today, so this mutation cannot
# be caught by an exit code -- only by WHICH command was used. That is the
# point: the receipt must not rest on an accident of gh's implementation that
# `gh issue edit` does not share.
# shellcheck disable=SC2016  # the pattern matches the step's literal text.
M1=$(printf '%s\n' "$DEFERRED_RUN" | sed 's/attadipa_label_comment "$TARGET_KIND"/gh issue comment/')
if [ "$M1" = "$DEFERRED_RUN" ]; then
  no "the object-kind mutation changed something" "the sed matched nothing"
else
  run_mutant_deferred "$M1"
  MCOMMENTED=$(cat "$work/state/commented-as")
  if [ "$MCOMMENTED" = issue ]; then
    ok "commenting through the issue command again is detected"
  else
    no "commenting through the issue command again is detected" \
       "the mutant commented as '$MCOMMENTED', so the assertion in case 4 is not load-bearing"
  fi
fi

# shellcheck disable=SC2016  # the pattern matches the step's literal text.
M2=$(printf '%s\n' "$DEFERRED_RUN" | sed 's/\[ "\$TARGET_KIND" = pr \]/false/')
if [ "$M2" = "$DEFERRED_RUN" ]; then
  no "the inert-label mutation changed something" "the sed matched nothing"
else
  run_mutant_deferred "$M2"
  if printf '%s' "$MLABELS" | grep -qF "agent:ready"; then
    ok "dropping the pull-request guard puts the inert label back"
  else
    no "dropping the pull-request guard puts the inert label back" \
       "no agent:ready was recorded, so the guard's assertion is not load-bearing"
  fi
fi

# ------------------------------------------------------------------- renderer
D=$(bash .github/scripts/agent-say.sh deferred http://r/1 quality-audit P1 issue_comment hleserg full 2 true)
has   "the renderer marks its comment for deduplication" "$D" "<!-- attadipa-deferred -->"
hasnt "the renderer never claims an agent is working"    "$D" "an agent is working"
R=$(bash .github/scripts/agent-say.sh receipt http://r/1 quality-audit P1 false issue_comment hleserg '')
has   "the accepted receipt is still the one that says work started" "$R" "an agent is working on this now"

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
