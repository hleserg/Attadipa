#!/usr/bin/env bash
# Who may drive a write-capable agent?
#
# The intake gate is the security boundary of the automation loop, and a
# security boundary that has never been executed against a hostile input is a
# hypothesis. This runs the real decision function — .github/scripts/
# intake-decision.sh, the same file claude-agent.yml sources — over the cases
# that matter, including the ones that must be refused.
#
# It is offline and deterministic. The permission answers below are the ones
# the GitHub API actually returned for these accounts on this repository when
# the test was written; the network lookup lives in the workflow so that this
# file can run anywhere, including on a laptop with no token.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source-path=SCRIPTDIR
# shellcheck source=../scripts/intake-decision.sh
. "$here/../scripts/intake-decision.sh"

pass=0; fail=0
MARKER='<!-- firefly-agent-task
producer: chatgpt
task_type: continuous-review
priority: P1
-->

@claude

Please look at the thing.'

# check WANT DESCRIPTION -- ARGS...
check() {
  local want="$1" desc="$2"; shift 3
  local got; got=$(firefly_intake_decision "$@")
  case "$want" in
    accept) [ "$got" = "accept" ] && ok=yes || ok=no ;;
    reject) case "$got" in reject:*) ok=yes ;; *) ok=no ;; esac ;;
  esac
  if [ "$ok" = yes ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted %s, got: %s\n' "$desc" "$want" "$got"
  fi
}

echo "Intake gate — who may drive a write-capable agent"

#     want    description                              actor  event  action  label  body  labels  state  permission
check accept "the owner, with a proper task marker" -- \
      hleserg issues opened "" "$MARKER" "" open admin

# The whole point. `producer: chatgpt` is in the body and proves nothing.
check reject "a stranger with the very same marker" -- \
      octocat issues opened "" "$MARKER" "" open none
check reject "a stranger commenting @claude" -- \
      octocat issue_comment created "" "@claude do the thing" "" open none
check reject "a read-only collaborator" -- \
      somebody issues opened "" "$MARKER" "" open read
check reject "a triage collaborator, who can label but not write" -- \
      somebody issues labeled "agent:ready" "" "" open triage

# The loop that costs money until somebody notices.
check reject "a bot replying to its own comment" -- \
      "claude[bot]" issue_comment created "" "@claude again" "" open write
check reject "github-actions[bot] opening an issue" -- \
      "github-actions[bot]" issues opened "" "$MARKER" "" open write

# Trusted by construction: GitHub only accepts a dispatch from write access.
check accept "the watchdog handing over a missed task" -- \
      github-actions workflow_dispatch "" "" "" "" open ""

check reject "an ordinary issue with no marker and no mention" -- \
      hleserg issues opened "" "Just a bug report." "" open admin
check reject "a marker with no @claude in it" -- \
      hleserg issues opened "" "<!-- firefly-agent-task -->" "" open admin
check reject "a closed issue" -- \
      hleserg issues opened "" "$MARKER" "" closed admin

# Deduplication, and the one thing that overrides it.
check reject "a task somebody has already claimed" -- \
      hleserg issues opened "" "$MARKER" "agent:working,type:review" open admin
check accept "an owner asking again in a comment" -- \
      hleserg issue_comment created "" "@claude have another go" "agent:working" open admin
check reject "a dispatch for an already-claimed task" -- \
      github-actions workflow_dispatch "" "" "" "agent:done" open ""

check accept "the agent:ready label on its own" -- \
      hleserg issues labeled "agent:ready" "" "" open write
check reject "some other label being added" -- \
      hleserg issues labeled "needs-owner" "$MARKER" "" open write

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
