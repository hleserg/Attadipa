#!/usr/bin/env bash
# Which commit the unattended merge sweep is about to merge, when GitHub saw it
# arrive, and whether the reviewer's verdict covers it.
#
# WHY THIS EXISTS. `pr-merge-sweep.yml` derived both of its head-related
# security properties -- the six-hour settling window and "was `ai-review:pass`
# reached on this commit" -- from
#
#     (.pushedDate // .committedDate)
#
# `pushedDate` is deprecated and answers `null` for every commit this repository
# has (verified against the head of #193 on 2026-08-24), so the fallback was the
# only branch ever taken and both properties rested on the git committer clock.
# A commit made with `GIT_COMMITTER_DATE` six hours in the past cleared the
# settling window the instant it existed and post-dated nothing, so a stale
# `ai-review:pass` from the previous head read as a verdict about it. Issue
# #199.
#
# The trusted replacement is `workflowRun.createdAt` on the head commit's own
# check suites, restricted to `pull_request` events -- a timestamp GitHub writes
# against that object id when it starts work on it. The argument for it, and for
# taking the maximum rather than the minimum, is in merge-head-trust.jq beside
# the code.
#
# WHY A FILE, AND NOT FIVE MORE LINES OF `jq` IN THE YAML. The same reason
# merge-candidate.sh, merge-facts.sh and intake-decision.sh are files: a rule
# embedded in a workflow cannot be executed, so it cannot be tested, so every
# defect it has ships and stays -- and the defect this replaces was in exactly
# that position for its whole life, in a workflow whose own comment described
# the attack it was vulnerable to. This one takes a GraphQL response document on
# stdin or as `$1` and needs no network, no `gh` and no environment, so
# .github/tests/merge-candidate-test.sh drives it with real response shapes.
#
# Prints exactly one line, and the vocabulary is merge-candidate.sh's on purpose
# so the caller can pass either straight through to its own log:
#
#   TRUSTED <oid> <true|false|unknown> <seconds>
#                   the head's object id; whether the latest `ai-review:pass`
#                   labelling is not older than GitHub's own record of that
#                   head arriving; and how old that arrival is, in seconds
#   HOLD <reason>   one of those could not be established -- <reason> says which
#
# There is no third answer, no path through this file that prints nothing, and
# no path that falls back to a date the author of the commit chose.

set -uo pipefail

# Where the filter lives, resolved against this file rather than the working
# directory, for the reason merge-facts.sh gives: the sweep runs this from the
# repository root today, and a caller that does not is a bug in the caller
# rather than a reason to answer wrongly.
ATTADIPA_MERGE_HEAD_TRUST_JQ="${ATTADIPA_MERGE_HEAD_TRUST_JQ:-$(
  cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd
)/merge-head-trust.jq}"

# attadipa_merge_head_trust [DOCUMENT]
#
# DOCUMENT is the raw GraphQL response, as `$1` or on stdin.
#
# ATTADIPA_MERGE_NOW overrides the clock, and exists so the test can assert an
# exact age rather than a range. It is read here and nowhere else; the sweep
# never sets it.
attadipa_merge_head_trust() {
  local doc="${1-}"
  if [ "$#" -eq 0 ]; then
    doc="$(cat)"
  fi

  if [ -z "${doc//[$'\n'[:space:]]/}" ]; then
    echo "HOLD the pull request's facts came back empty, so nothing about its head is known"
    return 0
  fi

  # A filter that is not there is not a head commit with nothing wrong with it.
  # Named separately from a parse failure so the log sends whoever reads it at a
  # sparse checkout rather than at GitHub's response.
  if [ ! -f "$ATTADIPA_MERGE_HEAD_TRUST_JQ" ]; then
    echo "HOLD the head-trust filter $ATTADIPA_MERGE_HEAD_TRUST_JQ is missing, so nothing was checked"
    return 0
  fi

  local now="${ATTADIPA_MERGE_NOW:-}"
  if [ -z "$now" ]; then
    now="$(date -u +%s 2>/dev/null)" || now=""
  fi
  case "$now" in
    ''|*[!0-9]*)
      # No clock, no age. This is not a case that can arise on a GitHub runner,
      # and it still may not be guessed at.
      echo "HOLD the current time could not be read, so no age could be computed"
      return 0 ;;
  esac

  local answer
  if ! answer="$(printf '%s' "$doc" | jq -r --argjson now "$now" \
       -f "$ATTADIPA_MERGE_HEAD_TRUST_JQ" 2>/dev/null)"; then
    echo "HOLD the pull request's facts could not be parsed, so nothing about its head is known"
    return 0
  fi

  # THE SHAPE IS VALIDATED HERE, not trusted from the filter. The caller reads
  # three fields out of this line and hands them to a rule with write access to
  # `main`; a filter that one day prints two lines, or an empty one, or a
  # `null`, must not reach that as three empty strings.
  case "$answer" in
    "") echo "HOLD the head-trust check answered nothing, which it must not"; return 0 ;;
    null) echo "HOLD the head of this pull request could not be established"; return 0 ;;
    *[$'\n']*) echo "HOLD the head-trust check answered more than once, which it must not"; return 0 ;;
    HOLD*) echo "$answer"; return 0 ;;
  esac

  local verb oid verdict age rest
  read -r verb oid verdict age rest <<<"$answer"
  if [ "$verb" != "TRUSTED" ] || [ -n "$rest" ]; then
    echo "HOLD the head-trust check answered '$answer', which is not one of its two answers"
    return 0
  fi
  case "$oid" in
    *[!0-9a-f]*|"") echo "HOLD the head-trust check named no usable head commit"; return 0 ;;
  esac
  case "${#oid}" in
    40|64) : ;;
    *) echo "HOLD the head-trust check named no usable head commit"; return 0 ;;
  esac
  case "$verdict" in
    true|false|unknown) : ;;
    *) echo "HOLD the head-trust check gave no usable verdict binding"; return 0 ;;
  esac
  case "$age" in
    ''|*[!0-9]*) echo "HOLD the head-trust check gave no usable head age"; return 0 ;;
  esac

  echo "$answer"
}

# Callable as a command as well as a sourced function, the same as
# merge-candidate.sh and merge-facts.sh, so the workflow sources nothing and the
# test can do either.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_merge_head_trust "$@"
fi

