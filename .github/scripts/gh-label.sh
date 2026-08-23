#!/usr/bin/env bash
# Edit the labels on an issue OR a pull request, saying which it is.
#
# WHY THIS EXISTS. `gh issue edit` resolves its argument through GraphQL
# `repository.issue(number:)`, and that field does not resolve pull requests --
# this repository has already been bitten by exactly that, in the hand-over's
# own lookup, where the NOT_FOUND error document ended up inside an outcome
# comment on #71 (see .github/scripts/handover-decision.sh's header).
#
# claude-agent.yml triggers on `issue_comment`, `pull_request_review_comment`
# and `pull_request_review`. All three fire on pull requests, and the gate's
# `issue` output is then the PULL REQUEST's number. So every `gh issue edit` in
# that workflow runs against a pull request roughly whenever an agent is started
# from one -- and every call is `|| true`, so nothing reports which of the two
# outcomes you got:
#
#   - it resolves, and a step that reasoned about issues has quietly edited a
#     pull request's labels. claude-ci-repair.yml puts `ci:failed` +
#     `agent:blocked` on a pull request when repair gives up and asks for a
#     human; stripping `agent:blocked` there clears that escalation, and
#     `agent:blocked` is one of the labels the unattended backstop requires
#     ABSENT while `ci:failed` is not on that list.
#   - it does not, and the whole normalisation silently does not happen on the
#     pull request path -- including the `agent:working` claim.
#
# Review of #85 found the pair and put it plainly: exactly one of those is
# happening and nobody knows which. This file removes the question rather than
# answering it. `gh pr edit` is used for pull requests and `gh issue edit` for
# issues, so the call is correct either way and a failure is a real failure.
#
# .github/tests/gh-label-test.sh runs it against a stub `gh` on PATH, which is
# why the command is built and then invoked rather than written twice.

# attadipa_label_edit TARGET_KIND NUMBER REPO ARGS...
#
# TARGET_KIND is "pr" or "issue". Anything else is treated as "issue", because
# that is what an unreadable lookup most likely is and because the issue path is
# the one every scheduled trigger uses.
# The third parameter is `slug` rather than `repo` on purpose: a local named
# `repo` in a sourced file makes shellcheck read every `$REPO` in the sourcing
# script as a misspelling of it (SC2153), which failed CI on the commit that
# introduced this file. Renaming the parameter is the fix; a disable comment
# would only move the warning.
attadipa_label_edit() {
  local kind="$1" number="$2" slug="$3"
  shift 3
  case "$kind" in
    pr) gh pr edit "$number" --repo "$slug" "$@" ;;
    *)  gh issue edit "$number" --repo "$slug" "$@" ;;
  esac
}

# attadipa_label_comment TARGET_KIND NUMBER REPO ARGS...
#
# Same split for comments. `gh issue comment` does resolve pull requests today,
# but relying on that is relying on an accident of gh's implementation -- the
# same accident `gh issue edit` does NOT share, which is the whole reason this
# file exists.
attadipa_label_comment() {
  local kind="$1" number="$2" slug="$3"
  shift 3
  case "$kind" in
    pr) gh pr comment "$number" --repo "$slug" "$@" ;;
    *)  gh issue comment "$number" --repo "$slug" "$@" ;;
  esac
}
