#!/usr/bin/env bash
# The privileged tail of a run must not execute code from the branch it is
# reviewing -- and must still report when it cannot find any code to execute.
#
# THE DEFECT (#133). `claude-agent.yml`'s hand-over sourced `gh-label.sh` and
# ran `handover-decision.sh`, `agent-say.sh`, `promote-decision.sh` and
# `failure-reason.sh` out of $GITHUB_WORKSPACE. That directory is not the
# default branch:
#
#   - `pull_request_review` and `pull_request_review_comment` set GITHUB_REF to
#     `refs/pull/N/merge`, so the checkout is the reviewed branch merged into
#     the base -- and for any file the branch edits, the branch's version wins
#     that merge;
#   - the model session runs between the checkout and those steps with write
#     access to the whole tree, and agents are routinely given tasks whose
#     subject is these exact helpers (#106 rewrites failure-reason.sh, #129 adds
#     a hand-over script);
#   - a tree with no such file met `set -euo pipefail`, which killed the step
#     before the outcome comment and left the issue on `agent:working` with
#     nothing said until the watchdog swept it two hours later.
#
# `claude-pr-review.yml` had the same shape with a narrower blast radius: its
# only trigger is `pull_request`, so its checkout is ALWAYS the merge ref, and
# `failure-reason.sh` -- the redaction whitelist deciding what may be quoted out
# of an execution log -- came from it.
#
# This file asserts both halves, and it executes the second rather than reading
# it. A guard whose failure mode is silence cannot be verified by grep: the
# hand-over block is extracted from the YAML and run against a stub `gh`, once
# with a hostile workspace copy and a good bundle, once with no bundle at all.
# That is the same argument the intake gate and the watchdog filter make for
# living in files -- logic nothing has ever executed against a hostile input is
# a hypothesis.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }

AGENT=.github/workflows/claude-agent.yml
REVIEW=.github/workflows/claude-pr-review.yml

# Lines that INVOKE something under .github/scripts/, as opposed to the many
# that merely name one. Both shapes are everywhere in these files: the comments
# cite the helpers constantly, and one step echoes a path into a comment it
# posts. Naming a file is not running it, and a test that cannot tell the
# difference gets deleted the first time it cries wolf.
#
# So a reference counts only where it is preceded by something that EXECUTES
# it: `bash`, `sh`, `source`, `.`, or jq's `-f`. Teaching this repository a new
# way to run a helper means teaching this list about it -- which is the trade,
# and it is the right way round, because the failure mode of forgetting is a
# test that has to be updated rather than a hole that stays quiet.
#
# Comments are stripped first, the way gh-api-usage-test.sh strips them.
workspace_script_calls() {
  awk -v from="$2" '
    FNR <= from { next }
    { line = $0; sub(/[[:space:]]*#.*$/, "", line) }
    line ~ /(^|[;&|(]|[[:space:]])(bash|sh|source|\.)[[:space:]]+\.github\/scripts\// ||
    line ~ /-f[[:space:]]+\.github\/scripts\// { printf "%d: %s\n", FNR, line }
  ' "$1"
}

# The line number of the first match, or empty.
line_of() { grep -n -- "$2" "$1" | head -n 1 | cut -d: -f1; }

# ---------------------------------------------------------------------------
echo "The bundle is fetched from the default branch, and before the model runs"

for wf in "$AGENT" "$REVIEW"; do
  model=$(line_of "$wf" 'uses: anthropics/claude-code-action@')
  if [ -z "$model" ]; then
    no "$wf still invokes claude-code-action" \
       "no 'uses: anthropics/claude-code-action@' found -- if the step moved, point this test at it rather than deleting it"
    continue
  fi

  staged=$(line_of "$wf" 'path: .attadipa-bundle')
  moved=$(line_of "$wf" 'rm -rf .attadipa-bundle')
  # The pin has to be on THIS checkout, not merely somewhere in the file --
  # the gate and the acknowledge job both carry one, so a file-wide grep would
  # pass a bundle checkout that had lost its `ref:` entirely. Read the `with:`
  # block the `path:` line belongs to: six lines is the whole of it.
  pinned=""
  [ -n "$staged" ] && pinned=$(awk -v at="$staged" \
      'FNR >= at - 6 && FNR <= at + 6 && /ref: \$\{\{ github.event.repository.default_branch \}\}/ {print FNR}' "$wf")

  if [ -z "$staged" ]; then
    no "$wf stages a default-branch copy of .github/scripts" \
       "no checkout with 'path: .attadipa-bundle'; the privileged steps are back to reading the reviewed branch"
  elif [ "$staged" -ge "$model" ]; then
    no "$wf stages it before the model session starts" \
       "the bundle checkout is at line $staged, after claude-code-action at line $model -- the session would run with the trusted copy sitting in the workspace beside it"
  else
    ok "$wf stages a default-branch copy of .github/scripts before the session (line $staged)"
  fi

  if [ -z "$pinned" ]; then
    no "$wf pins that checkout to the default branch" \
       "no 'ref: \${{ github.event.repository.default_branch }}' anywhere; an unpinned checkout is the defect, not the fix"
  else
    ok "$wf pins a checkout to the default branch"
  fi

  if [ -z "$moved" ]; then
    no "$wf removes the staging copy from the workspace" \
       "nothing does 'rm -rf .attadipa-bundle'; actions/checkout cannot write outside \$GITHUB_WORKSPACE, so leaving it there hands the session the trusted copy"
  elif [ "$moved" -ge "$model" ]; then
    no "$wf removes the staging copy before the session starts" \
       "the removal is at line $moved, after claude-code-action at line $model -- the ordering IS the guarantee here"
  else
    ok "$wf removes the staging copy from the workspace before the session (line $moved)"
  fi

  if grep -q 'RUNNER_TEMP' "$wf"; then
    ok "$wf keeps the staged copy in RUNNER_TEMP, outside the workspace"
  else
    no "$wf keeps the staged copy in RUNNER_TEMP, outside the workspace" \
       "no RUNNER_TEMP; anywhere inside \$GITHUB_WORKSPACE is somewhere the session can edit"
  fi
done

# ---------------------------------------------------------------------------
echo
echo "...and nothing after the model session runs a script out of the workspace"

# THE REGRESSION ASSERTION. This is the one that is red on 7558728: every
# privileged step there referenced .github/scripts/ by a workspace-relative
# path.
for wf in "$AGENT" "$REVIEW"; do
  model=$(line_of "$wf" 'uses: anthropics/claude-code-action@')
  [ -n "$model" ] || continue
  offenders=$(workspace_script_calls "$wf" "$model")
  if [ -z "$offenders" ]; then
    ok "$wf: no workspace-relative .github/scripts/ reference survives the session"
  else
    no "$wf: no workspace-relative .github/scripts/ reference survives the session" \
       "these run after the model session and would execute the reviewed branch's code with a write token:
$(printf '%s\n' "$offenders" | sed 's/^/         /')"
  fi
done

# ---------------------------------------------------------------------------
echo
echo "The hand-over runs, and reports, when there is no bundle at all"

# Extract the `Hand over` step's shell body from the YAML and execute it. It
# carries no ${{ }} interpolation -- every value arrives through `env:` -- which
# is what makes this possible, and is itself worth asserting: the day somebody
# interpolates a context into it, this stops being executable and the guard
# stops guarding.
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

BLOCK=$(extract_run_block "Hand over" "$AGENT")
if [ -z "$BLOCK" ]; then
  no "the Hand over step's shell can be extracted and run" \
     "no 'run: |' body found under '- name: Hand over' in $AGENT -- if the step was renamed, re-point this test rather than deleting it"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
ok "the Hand over step's shell can be extracted and run"

# shellcheck disable=SC2016  # the literal characters ${{ are the thing sought.
if printf '%s' "$BLOCK" | grep -q '\${{'; then
  no "the Hand over shell takes every value through env:, not \${{ }}" \
     "a \${{ }} expression appeared in the body; besides being an injection surface for model-adjacent text, it makes this block unexecutable and this test blind"
else
  ok "the Hand over shell takes every value through env:, not \${{ }}"
fi

sandbox=$(mktemp -d) || exit 1
trap 'rm -rf "$sandbox"' EXIT

# A stub `gh` that records every call and answers the two reads the block makes.
mkdir -p "$sandbox/bin"
cat > "$sandbox/bin/gh" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$STUB_LOG"
case "$1 $2" in
  "api graphql") printf '%s\n' "${STUB_FOUND:-}" ;;
  "api repos"*|"api "*) printf '%s\n' "${STUB_LABELS:-}" ;;
esac
for a in "$@"; do
  if [ "$prev" = "--body-file" ]; then cat "$a" > "$STUB_COMMENT"; fi
  prev="$a"
done
exit 0
STUB
chmod +x "$sandbox/bin/gh"

# The workspace the session leaves behind: a HOSTILE copy of every helper. If
# any of these runs, it says so loudly and unmistakably.
mkdir -p "$sandbox/ws/.github/scripts"
for f in gh-label.sh handover-decision.sh agent-say.sh promote-decision.sh; do
  cat > "$sandbox/ws/.github/scripts/$f" <<STUB
#!/usr/bin/env bash
touch "\$PWNED_MARKER"
echo "PWNED"
STUB
  chmod +x "$sandbox/ws/.github/scripts/$f"
done

# The bundle: the real helpers, exactly as the workflow stages them from main.
mkdir -p "$sandbox/trusted"
cp .github/scripts/gh-label.sh .github/scripts/handover-decision.sh \
   .github/scripts/agent-say.sh .github/scripts/promote-decision.sh \
   "$sandbox/trusted/" || exit 1

# run_handover <trusted-dir-or-empty> <FOUND> <LABELS> <blocked-before>
run_handover() {
  : > "$sandbox/log"
  : > "$sandbox/comment"
  rm -f "$sandbox/pwned"
  ( cd "$sandbox/ws" || exit 1
    PATH="$sandbox/bin:$PATH" \
    STUB_LOG="$sandbox/log" STUB_COMMENT="$sandbox/comment" \
    STUB_FOUND="$2" STUB_LABELS="$3" PWNED_MARKER="$sandbox/pwned" \
    GH_TOKEN=x REPO=owner/repo ISSUE=133 HEAD_BEFORE="" CONCLUSION=success \
    REASON="" RUN_URL="http://run/1" \
    ATTADIPA_TARGET_KIND=issue ATTADIPA_BLOCKED_BEFORE="$4" \
    ATTADIPA_RUN_STARTED_AT="2026-08-23T00:00:00Z" \
    ATTADIPA_TRUSTED_SCRIPTS="$1" \
    bash -c "$BLOCK" ) > "$sandbox/stdout" 2> "$sandbox/stderr"
  echo $?
}

# --- 1. No bundle, and no pull request: it must still speak, and re-queue.
rc=$(run_handover "" "" "agent:working" false)
if [ "$rc" = "0" ]; then
  ok "with no bundle the step exits 0 rather than dying under set -e"
else
  no "with no bundle the step exits 0 rather than dying under set -e" \
     "exit $rc; stderr: $(head -c 400 "$sandbox/stderr")"
fi
if [ -s "$sandbox/comment" ]; then
  ok "...and it posts a comment instead of falling silent"
else
  no "...and it posts a comment instead of falling silent" \
     "nothing was written to a --body-file; this is the exact silence #133 is about"
fi
if [ -e "$sandbox/pwned" ] || grep -q PWNED "$sandbox/comment" 2>/dev/null; then
  no "...and it never falls back to the workspace copy" \
     "the hostile helper in the working tree ran; falling back to it is worse than falling silent"
else
  ok "...and it never falls back to the workspace copy"
fi
if grep -q -- '--add-label agent:failed' "$sandbox/log" \
   && grep -q -- '--add-label agent:ready' "$sandbox/log" \
   && grep -q -- '--remove-label agent:working' "$sandbox/log"; then
  ok "...and it leaves recoverable labels: agent:failed + agent:ready, agent:working released"
else
  no "...and it leaves recoverable labels: agent:failed + agent:ready, agent:working released" \
     "labels edited: $(grep -c edit "$sandbox/log") calls; log:
$(sed 's/^/         /' "$sandbox/log")"
fi

# --- 2. No bundle, but an open pull request closes the issue. Re-queueing that
#        would start a second billed agent against finished work, which is the
#        duplication the queue exists to prevent -- so it is agent:review, and
#        the draft is NOT promoted.
rc=$(run_handover "" "pr 156" "agent:working" false)
if grep -q -- '--add-label agent:review' "$sandbox/log" \
   && ! grep -q -- '--add-label agent:ready' "$sandbox/log"; then
  ok "with no bundle but an open pull request it says agent:review, not agent:ready"
else
  no "with no bundle but an open pull request it says agent:review, not agent:ready" \
     "re-queueing a finished task starts a second writer on it; log:
$(sed 's/^/         /' "$sandbox/log")"
fi
if grep -q '^pr ready' "$sandbox/log"; then
  no "...and it does not take the draft out of draft" \
     "'gh pr ready' was called by a hand-over that could not read promote-decision.sh; undrafting is what makes a branch eligible for the unattended backstop"
else
  ok "...and it does not take the draft out of draft"
fi
if grep -q 'could not read its own code' "$sandbox/comment"; then
  ok "...and the comment says the report is a reduced one rather than pretending"
else
  no "...and the comment says the report is a reduced one rather than pretending" \
     "a degraded outcome that reads like a normal one is a lie with a green tick on it"
fi

# --- 3. Bundle present, workspace hostile: the trusted copy is the one that runs.
rc=$(run_handover "$sandbox/trusted" "pr 156" "agent:working" false)
if [ "$rc" = "0" ]; then
  ok "with a bundle the step completes"
else
  no "with a bundle the step completes" \
     "exit $rc; stderr: $(head -c 400 "$sandbox/stderr")"
fi
if [ -e "$sandbox/pwned" ] || grep -q PWNED "$sandbox/comment" 2>/dev/null; then
  no "...and the hostile workspace copy never runs" \
     "the branch under review supplied the code that wrote this run's outcome, with issues:write in hand"
else
  ok "...and the hostile workspace copy never runs"
fi
if grep -q 'attadipa-outcome' "$sandbox/comment"; then
  ok "...and the comment is the real agent-say.sh rendering, from the bundle"
else
  no "...and the comment is the real agent-say.sh rendering, from the bundle" \
     "no <!-- attadipa-outcome --> marker; got: $(head -c 300 "$sandbox/comment")"
fi

# --- 4. The rule the trusted path already had must survive the new branch: a run
#        that blocked ITSELF is not talked over, and the claim is still released.
rc=$(run_handover "" "" "agent:working,agent:blocked" false)
if grep -q -- '--remove-label agent:working' "$sandbox/log"; then
  ok "a self-blocked run with no bundle still releases agent:working"
else
  no "a self-blocked run with no bundle still releases agent:working" \
     "agent:working beside agent:blocked is invisible to the watchdog and looks finished to a person -- the worst of both"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
