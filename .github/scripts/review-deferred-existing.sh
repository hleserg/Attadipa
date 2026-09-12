#!/usr/bin/env bash
# The follow-up issue this pull request's deferred findings were ALREADY filed
# as, found from the issue itself rather than from the receipt for it.
#
# Usage: review-deferred-existing.sh REPO DEFERRED_BODY_FILE AUTHOR
#        prints the issue number, or nothing, on stdout.
#
# THE WINDOW THIS CLOSES (#548). `claude-pr-review.yml` files the follow-up with
# `gh issue create` and records the number in the ledger comment afterwards, by
# a separate API call, under `set -euo pipefail`. Between those two lines the
# issue is already a durable object on GitHub and nothing anywhere says so. A
# 502 on the ledger write, a rate limit, a cancelled runner -- any of them ends
# the step there, and GitHub's supported "re-run failed jobs" then replays the
# same round against the same receiptless ledger, concludes again that no issue
# exists, and files a second one. The ledger names only the newest; the first is
# orphaned from the review that made it, and two copies of the same task are
# open for triage and for Claude to pick up.
#
# So the ledger stays the fast path and stops being the ONLY path. The issue
# body already carries a deterministic per-pull-request marker -- the first line
# `_attadipa_render_deferred` writes in `.github/scripts/review-verdict.sh` --
# and that marker is on a durable object that survived the failure. This script
# is the recovery read over it.
#
# NOT THE SEARCH INDEX, and that is not a preference. `review-verdict.sh` has
# said from the day the rule was written that GitHub's issue search is an index
# with lag, so a round minutes after the create can be told the issue does not
# exist -- which would file it twice and is the reason the ledger was the record
# in the first place. This reads `repos/:owner/:repo/issues`, the collection
# itself: paginated and served from the data rather than ranked from an index.
# It costs a walk of every issue in the repository, open and closed, and it is
# paid only on the round that would otherwise create one.
#
# THE KEY IS READ OUT OF THE BODY ABOUT TO BE POSTED, never restated here. A
# marker spelled in two files is a marker that drifts in one of them, and the
# failure that drift produces is silent in the direction that hurts: a key that
# no longer matches finds nothing, files again, and looks exactly like a first
# filing. Passing the body file makes the search key and the posted body the
# same string by construction.
#
# THE MATCH IS BOUND TO AN AUTHOR, because an issue body is public input. Anyone
# may open an issue containing `<!-- attadipa-review-deferred:7 -->`, and an
# unbound match would hand that number to the ledger and file nothing -- the
# deferred finding recorded as living in a stranger's issue. The follow-up is
# created by the workflow's own token, so the caller passes the account that
# token authenticates as, the one it already names for the ledger comment. If
# that account ever changes, the filter stops matching and the behaviour falls
# back to what this repository did before #548 -- a possible duplicate on a
# re-run, never a wrong issue adopted.
#
# A READ THAT FAILED IS NOT AN ANSWER. Reporting "nothing is filed" when the API
# could not be reached would re-create the duplicate this exists to prevent, on
# the one path where a duplicate is durable, so an unreadable collection exits
# non-zero and the caller's `set -e` stops before the create. Nothing has been
# written at that point, and the re-run tries again from the same state.
#
# Everything diagnostic goes to stderr: stdout is the answer.
#
# `.github/tests/review-deferred-filing-test.sh` runs this file through the
# workflow step that calls it, with a fail-once create and a re-run.

set -uo pipefail

repo="${1-}"
body="${2-}"
author="${3-}"
: "${repo:?repository required}"
: "${body:?the deferred issue body file is required}"
: "${author:?the account that files the follow-up is required, so that a body anybody can write cannot claim the number}"

if [ ! -s "$body" ]; then
  printf 'review-deferred-existing: %s is missing or empty, so there is no marker to recover by\n' "$body" >&2
  exit 2
fi

marker="$(head -n 1 "$body")"
# The marker, and nothing looser. An empty or malformed key would be passed to
# `contains` and match the first issue in the repository -- the one failure mode
# worse than filing twice, since it puts a number in the ledger that has nothing
# to do with the finding.
number="${marker#<!-- attadipa-review-deferred:}"
number="${number% -->}"
if [ "$number" = "$marker" ] || [ -z "$number" ]; then
  printf 'review-deferred-existing: the first line of %s is not a deferred marker: %s\n' "$body" "$marker" >&2
  exit 2
fi
case "$number" in
  ''|*[!0-9]*)
    printf 'review-deferred-existing: the deferred marker names no pull request: %s\n' "$marker" >&2
    exit 2 ;;
esac

# `env.` rather than interpolation, so that a marker is data to `jq` and not
# part of the program, and `.body // ""` because an issue opened with no body at
# all answers `null`, which `contains` refuses. Pull requests are in this
# collection too and are dropped: the thing being recovered is an issue.
export ATTADIPA_DEFERRED_MARKER="$marker"
export ATTADIPA_DEFERRED_AUTHOR="$author"
if ! found="$(gh api "repos/$repo/issues?state=all&per_page=100" --paginate --jq '
      .[]
      | select(has("pull_request") | not)
      | select(.user.login == env.ATTADIPA_DEFERRED_AUTHOR)
      | select((.body // "") | contains(env.ATTADIPA_DEFERRED_MARKER))
      | .number')"; then
  printf 'review-deferred-existing: could not read the issues of %s, so whether the follow-up for %s already exists is unknown\n' \
    "$repo" "$marker" >&2
  exit 3
fi

# The oldest match, so that a repository which already collected duplicates from
# this defect converges on one of them instead of alternating. `sed -n 1p` reads
# its input to the end: `head -1` would close the pipe under `pipefail` and turn
# an answer into a failure.
lowest="$(printf '%s\n' "$found" | grep -E '^[0-9]+$' | sort -n | sed -n '1p')"

if [ -n "$lowest" ]; then
  printf 'review-deferred-existing: %s is already filed as #%s; reusing it\n' "$marker" "$lowest" >&2
  printf '%s\n' "$lowest"
fi
exit 0
