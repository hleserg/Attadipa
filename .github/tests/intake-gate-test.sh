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


# The trusted-producer allowlist.
#
# ChatGPT reaches this repository through its GitHub App and arrives as
# chatgpt-codex-connector[bot] with author_association NONE — observed on
# 2026-08-21, when that login reviewed pull request #11. The bot rule refuses it,
# which is correct by default and leaves the queue with no input, so the owner
# may name an app in FIREFLY_TRUSTED_PRODUCERS. These are the cases that decide
# whether that list is a door or a hole.
CHATGPT='chatgpt-codex-connector[bot]'
TRUSTED="$CHATGPT,some-other-producer[bot]"

echo
echo "Trusted producers — the allowlist is a door, not a hole"

check accept "a listed producer app opening a task" -- \
      "$CHATGPT" issues opened "" "$MARKER" "" open none "$TRUSTED"
check reject "the same app when the list is empty" -- \
      "$CHATGPT" issues opened "" "$MARKER" "" open none ""
check reject "the same app when the list is not passed at all" -- \
      "$CHATGPT" issues opened "" "$MARKER" "" open none
check reject "an app nobody listed" -- \
      "vendor-bot[bot]" issues opened "" "$MARKER" "" open none "$TRUSTED"

# The loop lives in comments, so nothing in the list may exempt one.
check reject "a listed producer COMMENTING @claude" -- \
      "$CHATGPT" issue_comment created "" "@claude do it" "" open none "$TRUSTED"
check reject "a listed producer on a pull request review comment" -- \
      "$CHATGPT" pull_request_review_comment created "" "@claude do it" "" open none "$TRUSTED"

# Our own output can never be listed, however hard somebody tries.
check reject "claude[bot] named in the list anyway" -- \
      "claude[bot]" issues opened "" "$MARKER" "" open none "claude[bot],$CHATGPT"
check reject "github-actions[bot] named in the list anyway" -- \
      "github-actions[bot]" issues opened "" "$MARKER" "" open none "github-actions[bot]"
check reject "bare claude named in the list anyway" -- \
      claude issues opened "" "$MARKER" "" open none "claude"

# The allowlist replaces the permission check, not the rest of the gate.
check reject "a listed producer on a closed issue" -- \
      "$CHATGPT" issues opened "" "$MARKER" "" closed none "$TRUSTED"
check reject "a listed producer on an already-claimed task" -- \
      "$CHATGPT" issues opened "" "$MARKER" "agent:working" open none "$TRUSTED"
check reject "a listed producer with no marker and no mention" -- \
      "$CHATGPT" issues opened "" "Just a bug report." "" open none "$TRUSTED"

# A substring of a listed login is not that login.
check reject "an app whose name merely contains a listed one" -- \
      "evil-chatgpt-codex-connector[bot]" issues opened "" "$MARKER" "" open none "$TRUSTED"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
