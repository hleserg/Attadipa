#!/usr/bin/env bash
# Every branch of the merge-conflict guard, and the three states of `mergeable`.
#
# The defect this guard is for: pull request #65 sat with zero check runs across
# two pushes because it had a merge conflict, and GitHub does not run
# `pull_request` workflows on a conflicted head. Nothing anywhere said so, and
# "no failing check" reads as "not failing" to every consumer downstream.
#
# The defect this TEST is for is the opposite one, and it is the easier one to
# ship: a guard that mistakes GitHub's "still computing" for "conflicted" would
# accuse every push in flight, hourly, forever. The issue that filed this asked
# for `dirty`, `clean` and `null` to be covered for exactly that reason. They
# are, in both directions, plus the states nobody thinks about -- `behind`,
# `blocked`, `unstable`, `unknown`, `draft` -- and the shapes a failed API call
# leaves behind.
#
# Offline and deterministic: arguments in, two lines out, no network.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0

# decide MERGEABLE MERGEABLE_STATE ALREADY_SAID LABELS -> "ACTION|REASON"
decide() {
  local out
  out=$(bash .github/scripts/pr-conflict-decision.sh "$1" "$2" "$3" "$4")
  printf '%s|%s' \
    "$(printf '%s' "$out" | sed -n 1p)" \
    "$(printf '%s' "$out" | sed -n 2p)"
}

says() {
  local name="$1" got="$2" want="$3"
  if [ "$got" = "$want" ]; then
    printf '  ok    %s\n' "$name"; pass=$((pass + 1))
  else
    printf '  FAIL  %s\n     want: %s\n     got:  %s\n' "$name" "$want" "$got"
    fail=$((fail + 1))
  fi
}

echo "The three states of mergeable, which is the whole point"
says "dirty and false: the #65 shape, and the only one that speaks" \
     "$(decide false dirty no agent:review)" "say_and_requeue|issue-in-review"
says "clean and true: nothing to say" \
     "$(decide true clean no agent:review)" "quiet|mergeable"
says "null is not false -- GitHub is still computing, and every push passes through it" \
     "$(decide null unknown no agent:review)" "quiet|undetermined"
says "null with a stale-looking state is still not an accusation" \
     "$(decide null dirty no agent:review)" "quiet|undetermined"

echo
echo "  mergeable is the assertion; mergeable_state only corroborates"
# `mergeable_state` is not in the same REST reference as `mergeable` and its
# value set has already moved: it once returned `draft` for a draft pull
# request, masking everything else, and on 2026-08-23 this repository's four
# open drafts reported `clean` and `unstable` instead. Keying the assertion on
# `mergeable` survives that in either direction. Keying it on `dirty` would have
# gone silent for every draft on the day it masked -- which is every pull
# request an agent opens.
says "a conflicted DRAFT whose state says 'draft' is still caught" \
     "$(decide false draft no agent:review)" "say_and_requeue|issue-in-review"
says "a conflicted pull request whose state says 'unknown' is still caught" \
     "$(decide false unknown no agent:review)" "say_and_requeue|issue-in-review"
says "a conflicted pull request with no state at all is still caught" \
     "$(decide false "" no agent:review)" "say_and_requeue|issue-in-review"
says "true beside dirty is a contradiction, and a contradiction is not a finding" \
     "$(decide true dirty no agent:review)" "quiet|contradiction"

echo
echo "  The states that are not conflicts, and must never be reported as one"
says "behind: needs a rebase, gets CI, says nothing" \
     "$(decide true behind no agent:review)" "quiet|mergeable"
says "blocked: waiting on a review, gets CI, says nothing" \
     "$(decide true blocked no agent:review)" "quiet|mergeable"
says "unstable: a check is red, which is the case that ALREADY reports itself" \
     "$(decide true unstable no agent:review)" "quiet|mergeable"
says "draft: not a conflict" \
     "$(decide true draft no agent:review)" "quiet|mergeable"

echo
echo "  A lookup that failed must not be distinguishable from one that found nothing"
# The rule handover-decision.sh had to learn the expensive way: `gh` writes an
# error document to stdout before exiting non-zero, and a decision that pattern
# matches loosely will happily report it as a finding.
says "an empty field" \
     "$(decide "" "" no agent:review)" "quiet|undetermined"
says "an error document where a boolean was promised" \
     "$(decide '{"message":"Not Found"}' "" no agent:review)" "quiet|undetermined"
says "the word FALSE in the wrong case" \
     "$(decide FALSE dirty no agent:review)" "quiet|undetermined"
says "a string that merely contains false" \
     "$(decide "falsehood" dirty no agent:review)" "quiet|undetermined"
says "a string that merely contains true does not read as mergeable either" \
     "$(decide "construe" clean no agent:review)" "quiet|undetermined"

echo
echo "  Saying it once per head commit, not once per hour"
# The bound that makes this a guard rather than a nuisance. A pull request left
# conflicted for a week gets one comment; a new push that is STILL conflicted
# gets one more, because that push also got no CI. The caller keys the marker on
# the head SHA, so "already said" is a fact about a commit, never about a pull
# request.
says "already said for this head: silence" \
     "$(decide false dirty yes agent:review)" "quiet|already-said"
says "already said, and no linked issue either: still silence" \
     "$(decide false dirty yes "")" "quiet|already-said"
# An unreadable comment list is the one case where failing quiet is right. The
# alternative is a duplicate comment every hour, and the channel is worth more
# than the hour.
says "an unreadable comment list is treated as already said, never as not said" \
     "$(decide false dirty "" agent:review)" "quiet|already-said"
says "and so is an API error in that slot" \
     "$(decide false dirty '{"message":"Bad credentials"}' agent:review)" "quiet|already-said"

echo
echo "  Which half runs: the comment always, the relabel only when it is true"
says "no linked issue: comment, touch no labels" \
     "$(decide false dirty no "")" "say|no-issue"
says "an issue that is not in review: comment, touch no labels" \
     "$(decide false dirty no "agent:ready,priority:P1")" "say|issue-not-in-review"
says "an issue in review: comment and put it back in the queue" \
     "$(decide false dirty no "agent:review,agent:claude,priority:P2")" "say_and_requeue|issue-in-review"
# agent:working means somebody holds this right now. Moving it to agent:ready
# would dispatch a second writer onto a branch that already has one, which is a
# merge conflict caused by the merge-conflict guard.
says "an issue somebody is holding: comment, but do not queue a second writer" \
     "$(decide false dirty no "agent:working,agent:claude")" "say|issue-claimed"
# agent:blocked is a person's decision that this waits. Re-queueing it hourly is
# how a needs-owner question turns into a bill.
says "an issue deliberately parked: comment, do not re-queue the block" \
     "$(decide false dirty no "agent:blocked,needs-owner")" "say|issue-claimed"
says "both, and the claim still wins over the review" \
     "$(decide false dirty no "agent:review,agent:working")" "say|issue-claimed"

echo
echo "  Exact labels, never substrings"
# queue-scan.jq's rule, and it is here for the same reason: a near-miss that
# matches is a decision made about the wrong issue.
says "agent:reviewed does not read as agent:review" \
     "$(decide false dirty no "agent:reviewed")" "say|issue-not-in-review"
says "needs-agent:review does not read as agent:review" \
     "$(decide false dirty no "needs-agent:review")" "say|issue-not-in-review"
says "agent:review at the head of the list matches" \
     "$(decide false dirty no "agent:review,priority:P1")" "say_and_requeue|issue-in-review"
says "agent:review at the tail of the list matches" \
     "$(decide false dirty no "priority:P1,agent:review")" "say_and_requeue|issue-in-review"
says "agent:review alone matches" \
     "$(decide false dirty no "agent:review")" "say_and_requeue|issue-in-review"
says "agent:workings does not read as a claim" \
     "$(decide false dirty no "agent:workings,agent:review")" "say_and_requeue|issue-in-review"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]

