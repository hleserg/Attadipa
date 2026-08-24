#!/usr/bin/env bash
# Is the snapshot the unattended merge sweep is about to decide on COMPLETE?
#
# WHY THIS EXISTS. `pr-merge-sweep.yml` asks GitHub for the facts about a pull
# request in one GraphQL round trip, and a GraphQL connection is a PAGE. Until
# 2026-08-24 the sweep asked for `labels(first:50)`, `reviewThreads(first:100)`,
# `files(first:100)` and `statusCheckRollup.contexts(first:100)`, never asked
# whether there was another page, and then computed on what came back as though
# it were the whole set:
#
#   UNRESOLVED="$(... [ .reviewThreads.nodes[] | select(.isResolved | not) ] | length)"
#
# A pull request with 101 review threads, the first hundred resolved and the
# hundred-and-first not, reported ZERO unresolved threads -- and zero is the
# value that merges. The same shape hid a failing check run past the hundredth
# context and an `ai-review:blocking` label past the fiftieth. `first: N` had
# become a silent PERMITTING condition: the more there was to read, the less the
# gate could refuse on. Issue #170.
#
# WHY A FILE, AND NOT THREE MORE LINES OF `jq` IN THE YAML. The same reason
# merge-candidate.sh and intake-decision.sh are files: a rule embedded in a
# workflow cannot be executed, so it cannot be tested, so every defect it has
# ships and stays. This one takes a GraphQL response document on stdin or as
# `$1` and needs no network, no `gh` and no environment, so
# .github/tests/merge-candidate-test.sh drives it with real response shapes --
# 101 threads, a truncated label page, a `pageInfo` that is missing altogether
# -- rather than with values somebody has already normalised by hand.
#
# WHY REFUSING RATHER THAN PAGINATING. For an unattended gate the bounded
# fail-closed answer is the one worth having: paginating five connections adds
# request loops, partial-failure states and a second way to be wrong, to raise a
# ceiling that the three-per-run cap and a documentation-only allowlist make
# almost unreachable. A hundred labels or a hundred and one review threads on a
# `docs/` pull request is not a case to optimise; it is a case for an
# orchestrator session to look at, which is where everything off the allowlist
# already goes. If that ever stops being true, the change is to paginate here --
# not to widen what counts as complete.
#
# THE CONNECTIONS THIS GUARDS, and what each one hides when it is truncated, are
# in .github/scripts/merge-facts.jq beside the checks themselves, so the list and
# the reasons cannot drift apart.
#
# Prints exactly one line, and the vocabulary is merge-candidate.sh's on purpose
# so the caller can pass either straight through to its own log:
#
#   COMPLETE        every decision-critical connection proved it has no next page
#   HOLD <reason>   one did not, or could not say -- <reason> names which
#
# There is no third answer, and no path through this file that prints nothing.

set -uo pipefail

# Where the filter lives, resolved against this file rather than the working
# directory: the sweep runs this from the repository root today, and a caller
# that does not is a bug in the caller rather than a reason to answer wrongly.
ATTADIPA_MERGE_FACTS_JQ="${ATTADIPA_MERGE_FACTS_JQ:-$(
  cd "$(dirname "${BASH_SOURCE[0]}")" 2>/dev/null && pwd
)/merge-facts.jq}"

# attadipa_merge_facts_complete [DOCUMENT]
#
# DOCUMENT is the raw GraphQL response, as `$1` or on stdin.
attadipa_merge_facts_complete() {
  local doc="${1-}"
  if [ "$#" -eq 0 ]; then
    doc="$(cat)"
  fi

  if [ -z "${doc//[$'\n'[:space:]]/}" ]; then
    echo "HOLD the pull request's facts came back empty, so nothing about them is known"
    return 0
  fi

  # A filter that is not there is not a pull request with nothing wrong with it.
  # Unguarded, `jq -f` on a missing file prints its own error and exits non-zero,
  # which the `||` below would catch -- but it would catch it as "could not be
  # parsed", which sends whoever reads the log looking at GitHub's response
  # instead of at a sparse checkout that did not include this directory.
  if [ ! -f "$ATTADIPA_MERGE_FACTS_JQ" ]; then
    echo "HOLD the completeness filter $ATTADIPA_MERGE_FACTS_JQ is missing, so nothing was checked"
    return 0
  fi

  local reason
  # A `jq` failure -- malformed JSON, a document that is not an object, a type
  # error inside the filter -- lands here and holds. That is the direction the
  # caller already takes on an API error, and it is why the filter itself does
  # not have to be defensive about every shape it might be handed.
  if ! reason="$(printf '%s' "$doc" | jq -r -f "$ATTADIPA_MERGE_FACTS_JQ" 2>/dev/null)"; then
    echo "HOLD the pull request's facts could not be parsed, so nothing about them is known"
    return 0
  fi

  # `jq -r` printing `null` is not a clean bill of health, and neither is a
  # filter that somehow produced several lines. Only one thing passes here, and
  # it is the literal empty string.
  case "$reason" in
    "") echo "COMPLETE" ;;
    null) echo "HOLD the completeness of the pull request's facts could not be established" ;;
    *[$'\n']*) echo "HOLD the completeness check answered more than once, which it must not" ;;
    *) echo "HOLD $reason" ;;
  esac
}

# Callable as a command as well as a sourced function, the same as
# merge-candidate.sh, so the workflow sources nothing and the test can do either.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_merge_facts_complete "$@"
fi
