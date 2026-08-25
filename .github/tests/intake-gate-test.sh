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
#
# Some case texts below are single-quoted Markdown carrying backticks and @
# signs — nothing in them is meant to expand, and inside double quotes a
# backtick is command substitution rather than a code span.
# shellcheck disable=SC2016
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source-path=SCRIPTDIR
# shellcheck source=../scripts/intake-decision.sh
. "$here/../scripts/intake-decision.sh"

pass=0; fail=0
MARKER='<!-- attadipa-agent-task
producer: chatgpt
task_type: continuous-review
priority: P1
-->

@claude

Please look at the thing.'

# check WANT DESCRIPTION -- ARGS...
check() {
  local want="$1" desc="$2"; shift 3
  local got; got=$(attadipa_intake_decision "$@")
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

# want  description                actor event action label body labels state permission [trusted] [comment]
#
# The last two are optional and the last one is the one that matters here: BODY
# is the issue, COMMENT is the text of the comment that fired the event. They
# are different arguments because they are different texts, and the version of
# this file that passed comment text in the BODY slot passed while production
# refused every @claude ever written. A test that does not call the function the
# way the workflow calls it is a test of something else.
check accept "the owner, with a proper task marker" -- \
      hleserg issues opened "" "$MARKER" "" open admin

# The whole point. `producer: chatgpt` is in the body and proves nothing.
check reject "a stranger with the very same marker" -- \
      octocat issues opened "" "$MARKER" "" open none
check reject "a stranger commenting @claude" -- \
      octocat issue_comment created "" "" "" open none "" "@claude do the thing"
check reject "a read-only collaborator" -- \
      somebody issues opened "" "$MARKER" "" open read
check reject "a triage collaborator, who can label but not write" -- \
      somebody issues labeled "agent:ready" "" "" open triage

# The loop that costs money until somebody notices.
check reject "a bot replying to its own comment" -- \
      "claude[bot]" issue_comment created "" "" "" open write "" "@claude again"
check reject "github-actions[bot] opening an issue" -- \
      "github-actions[bot]" issues opened "" "$MARKER" "" open write

# Trusted by construction: GitHub only accepts a dispatch from write access.
check accept "the watchdog handing over a missed task" -- \
      github-actions workflow_dispatch "" "" "" "" open ""

check reject "an ordinary issue with no marker and no mention" -- \
      hleserg issues opened "" "Just a bug report." "" open admin
check reject "a marker with no @claude in it" -- \
      hleserg issues opened "" "<!-- attadipa-agent-task -->" "" open admin
check reject "a closed issue" -- \
      hleserg issues opened "" "$MARKER" "" closed admin

# Deduplication, and the one thing that overrides it.
check reject "a task somebody has already claimed" -- \
      hleserg issues opened "" "$MARKER" "agent:working,type:review" open admin
check accept "an owner asking again in a comment" -- \
      hleserg issue_comment created "" "" "agent:working" open admin "" "@claude have another go"
check reject "a dispatch for an already-claimed task" -- \
      github-actions workflow_dispatch "" "" "" "agent:review" open ""

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
# may name an app in ATTADIPA_TRUSTED_PRODUCERS. These are the cases that decide
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
      "$CHATGPT" issue_comment created "" "" "" open none "$TRUSTED" "@claude do it"
check reject "a listed producer on a pull request review comment" -- \
      "$CHATGPT" pull_request_review_comment created "" "" "" open none "$TRUSTED" "@claude do it"

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

# THE BUG THIS SECTION EXISTS FOR.
#
# On 2026-08-22 the owner commented "@claude принято, вариант 4 ..." on issue #41
# and the gate answered "#41 nothing asks for an agent". The workflow was passing
# the ISSUE body in the BODY slot for a comment event, so the mention — which is
# only ever in the comment — was invisible. Two things were wrong at once, and
# the second one hid the first: the call, and a test suite that made the same
# mistake in the same direction. Every case below fails against the old code.
echo
echo "A comment is not the issue it is on"

ORDINARY='A bug report with no marker and no mention in it at all.'

check accept "the owner mentioning @claude in a comment on an ordinary issue" -- \
      hleserg issue_comment created "" "$ORDINARY" "" open admin "" \
      "@claude принято, вариант 4 — Meshtastic не поддерживаем."
check reject "the same comment with no mention in it" -- \
      hleserg issue_comment created "" "$ORDINARY" "" open admin "" \
      "Принято, вариант 4."
# The issue body may not stand in for the comment. This is the exact shape of the
# defect: a marker-bearing issue, an unrelated comment, and no request in it.
check reject "a comment with no mention on an issue whose BODY says @claude" -- \
      hleserg issue_comment created "" "$MARKER" "" open admin "" \
      "Thanks, looks right."
check accept "a review comment asking for a change" -- \
      hleserg pull_request_review_comment created "" "PR body, no mention." "" open write "" \
      "@claude this needs a bounds check."
check accept "a submitted review whose body mentions @claude" -- \
      hleserg pull_request_review submitted "" "PR body, no mention." "" open write "" \
      "@claude please fix the two blocking findings."

# Capital letters. The owner typed "@Claude" on #41 on 2026-08-22, which under a
# case-sensitive match is a second silent refusal for a different reason.
check accept "@Claude, capitalised" -- \
      hleserg issue_comment created "" "$ORDINARY" "" open admin "" "@Claude do the thing"
check accept "@CLAUDE, shouted" -- \
      hleserg issue_comment created "" "$ORDINARY" "" open admin "" "@CLAUDE DO THE THING"
check accept "a marker issue written in mixed case" -- \
      hleserg issues opened "" "<!-- Attadipa-Agent-Task -->
@Claude please" "" open admin

# A closed issue stays closed however loudly it is asked. The owner hit this one
# too: commenting on an issue they had just closed themselves.
check reject "@claude on a closed issue" -- \
      hleserg issue_comment created "" "$ORDINARY" "" closed admin "" "@claude do the thing"

# The comment slot never overrides the actor rules.
check reject "an outsider whose comment mentions @claude" -- \
      octocat pull_request_review_comment created "" "" "" open none "" "@claude ship it"
check reject "claude[bot] quoting itself" -- \
      "claude[bot]" issue_comment created "" "" "" open write "chatgpt-codex-connector[bot]" \
      "I have asked @claude to look again."

# TALKING ABOUT THE AGENT IS NOT ASKING FOR ONE.
#
# Sprung for real on 2026-08-22: a pull request comment explaining why telling
# somebody to write "@claude" was dangerous started an agent on the pull request
# it was written on. Every occurrence in that comment was inside a code span,
# and the gate read all of them as requests — the same shape as the commit
# message that closed issue #10 by quoting `Fixes #10` as an example.
#
# The people most likely to write the mention inside backticks are the ones
# maintaining this pipeline, so this is not a rare case, and the cost is a
# billable writer started by a sentence saying a billable writer should not be.
echo
echo "A mention inside code is somebody writing about the agent, not to it"

check reject "the exact comment that sprang this, inside a code span" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      'The old text ended "`@claude` here starts it again", and the gate does not deduplicate comments.'
check reject "a fenced block containing the mention" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      'Post it like this:

```
@claude do the thing
```

and it will be picked up.'
check reject "a tilde fence" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      '~~~
@claude
~~~'
check reject "documentation prose that only quotes the trigger" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      'To queue it deliberately, somebody with write access can comment `@claude`.'

# And the other direction, which matters more: a real request must survive.
check accept "a real ask that also happens to quote some code" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      '@claude the bounds check in `geo.cpp` is wrong, please fix it.'
check accept "a real ask after a fenced block" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      'Here is the failing output:

```
error: signed integer overflow
```

@claude please fix this.'
check accept "a real ask with an unmatched backtick somewhere in it" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      '@claude the quoting in that shell line is wrong: it opens a ` and never closes it.'
check accept "capitalised, outside code, still a request" -- \
      hleserg issue_comment created "" "" "" open admin "" \
      'Ok @Claude, go ahead — the `agent:ready` label is on it.'

# The marker path reads the issue body through the same filter.
check reject "a marker issue whose only mention is inside a fence" -- \
      hleserg issues opened "" '<!-- attadipa-agent-task -->

```
@claude
```
' "" open admin

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
