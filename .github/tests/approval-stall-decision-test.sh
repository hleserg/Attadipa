#!/usr/bin/env bash
# Every branch of the approval-stall decision, plus the input the API actually
# sent on the day this was found.
#
# The two runs below are real. `32581052659` (CI) and `32581052664` (the
# independent review) were created at 15:13:36Z on 2026-08-22 for head
# `488be1e`, and attempt 1 of each carries `status: completed`, `conclusion:
# action_required`, `run_started_at == updated_at == created_at`, and a jobs
# endpoint returning `total_count: 0`. Those exact values are asserted here, so
# a future edit that stops recognising them fails rather than going quiet --
# which is the only failure mode that matters for a guard whose whole job is to
# break a silence.
#
# The cases that are NOT the stall are as important as the ones that are. A
# guard that comments on every run is read once and ignored afterwards.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0

# decide STATUS CONCLUSION JOB_COUNT HEAD_SHA SAID_FOR -> "DECISION|REASON"
decide() {
  local out
  out=$(bash .github/scripts/approval-stall-decision.sh "$1" "$2" "$3" "$4" "$5")
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

# The head of #71 that stalled, and the head before it, which did not.
SHA_STALLED=488be1e91dbf08412c811eef46ecb3bdff2bc288
SHA_RAN=31c2c39a9d4f4a8b7c6e5d4c3b2a1908f7e6d5c4

echo "The runs that were actually on #71, exactly as the API returned them"
says "CI 32581052659 attempt 1: completed + action_required + 0 jobs" \
     "$(decide completed action_required 0 "$SHA_STALLED" "")" "say|stalled"
says "the review 32581052664 attempt 1, which is the same shape" \
     "$(decide completed action_required 0 "$SHA_STALLED" "")" "say|stalled"
says "attempt 2, re-run by a person, is a normal green run and says nothing" \
     "$(decide completed success 8 "$SHA_STALLED" "")" "quiet|ran"
says "the review's attempt 2 concluded skipped, which is also not this" \
     "$(decide completed skipped 0 "$SHA_STALLED" "")" "quiet|ran"

echo
echo "action_required arrives on either field, so both are read"
says "as a status, which is how the REST reference lists it" \
     "$(decide action_required "" 0 "$SHA_STALLED" "")" "say|stalled"
says "as a status with the conclusion already filled in" \
     "$(decide action_required action_required 0 "$SHA_STALLED" "")" "say|stalled"
says "as a conclusion on a completed run, which is what happened here" \
     "$(decide completed action_required 0 "$SHA_STALLED" "")" "say|stalled"

echo
echo "Zero jobs and some jobs are different answers, not a detail"
says "no jobs means no check ever appeared on the pull request" \
     "$(decide completed action_required 0 "$SHA_STALLED" "")" "say|stalled"
says "jobs beside action_required is an environment gate, and says so" \
     "$(decide completed action_required 3 "$SHA_STALLED" "")" "say|gated"
says "one job is still the gated shape, not the stalled one" \
     "$(decide completed action_required 1 "$SHA_STALLED" "")" "say|gated"

echo
echo "Everything that is not this stall stays quiet"
says "a green run" "$(decide completed success 8 "$SHA_RAN" "")" "quiet|ran"
says "a red run -- that one is visible and has an owner" \
     "$(decide completed failure 8 "$SHA_RAN" "")" "quiet|ran"
says "a cancelled run" "$(decide completed cancelled 8 "$SHA_RAN" "")" "quiet|ran"
says "a skipped run" "$(decide completed skipped 0 "$SHA_RAN" "")" "quiet|ran"
says "a run still queued" "$(decide queued "" 0 "$SHA_RAN" "")" "quiet|running"
says "a run in progress" "$(decide in_progress "" 4 "$SHA_RAN" "")" "quiet|running"
says "a run held by a deployment protection rule is visible, so not ours" \
     "$(decide waiting "" 2 "$SHA_RAN" "")" "quiet|running"
says "a completed run with no conclusion word at all" \
     "$(decide completed "" 0 "$SHA_RAN" "")" "quiet|ran"

echo
echo "The repeat bound is per HEAD COMMIT, because the second push is the common one"
says "already said for this head, so nothing more is said" \
     "$(decide completed action_required 0 "$SHA_STALLED" "$SHA_STALLED")" "quiet|repeat"
says "said for the PREVIOUS head does not silence this one" \
     "$(decide completed action_required 0 "$SHA_STALLED" "$SHA_RAN")" "say|stalled"
says "the gated shape is bounded by the same rule" \
     "$(decide completed action_required 5 "$SHA_STALLED" "$SHA_STALLED")" "quiet|repeat"
says "the same commit in a different case is still the same commit" \
     "$(decide completed action_required 0 "$SHA_STALLED" "${SHA_STALLED^^}")" "quiet|repeat"
says "and in the other direction, which is how a marker could have been stored" \
     "$(decide completed action_required 0 "${SHA_STALLED^^}" "$SHA_STALLED")" "quiet|repeat"

echo
echo "An unreadable input is never resolved in either direction"
says "no job count: not a number, so not an answer" \
     "$(decide completed action_required "" "$SHA_STALLED" "")" "quiet|unreadable"
says "a job count that is an error document" \
     "$(decide completed action_required '{"message":"Not Found"}' "$SHA_STALLED" "")" "quiet|unreadable"
says "a negative job count, which the API cannot send and jq can" \
     "$(decide completed action_required -1 "$SHA_STALLED" "")" "quiet|unreadable"
says "a job count with a space in it" \
     "$(decide completed action_required "0 " "$SHA_STALLED" "")" "quiet|unreadable"
says "no head SHA at all -- nothing to bound the repeat by" \
     "$(decide completed action_required 0 "" "")" "quiet|unreadable"
says "a head that is not hexadecimal" \
     "$(decide completed action_required 0 "not-a-sha" "")" "quiet|unreadable"
says "a lone space is not a head" \
     "$(decide completed action_required 0 " " "")" "quiet|unreadable"
says "an unreadable job count does not consume this head's one comment" \
     "$(decide completed action_required "" "$SHA_STALLED" "")" "quiet|unreadable"

echo
echo "The API contradicting itself is answered as we do not know"
says "a conclusion on a run that is still queued" \
     "$(decide queued success 0 "$SHA_RAN" "")" "quiet|contradiction"
says "action_required as a conclusion on a run still in progress" \
     "$(decide in_progress action_required 0 "$SHA_STALLED" "")" "quiet|contradiction"
says "an empty status with a conclusion beside it" \
     "$(decide "" action_required 0 "$SHA_STALLED" "")" "quiet|contradiction"
says "an empty status with nothing beside it is merely not finished" \
     "$(decide "" "" 0 "$SHA_STALLED" "")" "quiet|running"

echo
echo "Substrings and near-misses, which is what a changed API word looks like"
says "action_requires is not action_required" \
     "$(decide completed action_requires 0 "$SHA_STALLED" "")" "quiet|ran"
says "a leading space defeats the match, and that is the safe direction" \
     "$(decide completed " action_required" 0 "$SHA_STALLED" "")" "quiet|ran"
says "the word in the status field of a completed run is still not this" \
     "$(decide completed completed 0 "$SHA_STALLED" "")" "quiet|ran"

echo
echo "The field split in the pending patch, lifted out of the patch itself"
# Every defect this guard has shipped lived in a `run:` block, and the split
# below is one. It cannot be reached from here -- the loop is inside an
# unapplied patch in docs/automation/pending/, which neither this suite nor
# ci.yml's actionlint/shellcheck globs can see -- so the line is EXTRACTED from
# the patch file rather than copied. A copy would drift; an extraction fails
# loudly the moment the patch stops containing a recognisable `while IFS=`.
#
# What it is guarding: tab is IFS *whitespace*, so `IFS=$'\t' read` collapses
# adjacent tabs and a null `conclusion` shifts every later field left, putting
# the head SHA in CONCLUSION and the workflow name in RUN_SHA. The decision
# script then answers `quiet|unreadable` for a real stall. `|` is not IFS
# whitespace and survives the empty field.
PATCH=docs/automation/pending/75-approval-stall.patch
SPLIT=$(sed -n 's/^+ *\(while IFS=.*read -r RUN_ID .*; do\)$/\1/p' "$PATCH" | head -1)

if [ -z "$SPLIT" ]; then
  printf '  FAIL  the patch no longer contains a recognisable field split\n'
  fail=$((fail + 1))
else
  # `action_required` as a STATUS, which is the shape that arrives with an
  # empty conclusion -- the exact record the collapse makes unreachable.
  got=$(printf '32581052659|action_required||%s|CI\n' "$SHA_STALLED" \
          | eval "$SPLIT"' printf "%s/%s/%s/%s/%s" "$RUN_ID" "$STATUS" "$CONCLUSION" "$RUN_SHA" "$RUN_NAME"; done')
  says "a null conclusion keeps its own field, so the SHA stays in RUN_SHA" \
       "$got" "32581052659/action_required//$SHA_STALLED/CI"

  # And the decision the loop would then reach, with the fields in the right
  # places. This is the assertion that goes red if the delimiter regresses.
  # shellcheck disable=SC2016
  fields=$(printf '32581052659|action_required||%s|CI\n' "$SHA_STALLED" \
             | eval "$SPLIT"' printf "%s\n%s\n%s" "$STATUS" "$CONCLUSION" "$RUN_SHA"; done')
  says "and the run the fields describe is still recognised as the stall" \
       "$(decide "$(printf '%s' "$fields" | sed -n 1p)" \
                 "$(printf '%s' "$fields" | sed -n 2p)" \
                 0 "$(printf '%s' "$fields" | sed -n 3p)" "")" "say|stalled"
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
