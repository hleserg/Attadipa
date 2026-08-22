#!/usr/bin/env bash
# Hostile input and every branch of the hand-over decision.
#
# This step has shipped two defects, both in shell embedded in a workflow where
# nothing could run it:
#
#   1. It asked GraphQL for `issue(number:)`, which does not resolve pull
#      requests. `gh` wrote the NOT_FOUND document to stdout before failing, the
#      `|| echo ""` did not undo that, and #71 got
#      `### Done — pull request #{"data":{"repository":{"issue":null}}…`.
#   2. The fix reported the trigger as its own answer, so a run that pushed
#      nothing -- or died -- would have said "Done — pushed to this pull
#      request". Caught by review rather than in production, and it is the worse
#      of the two, because it is plausible.
#
# Both are cases below. The rule the whole file is arguing for: a lookup that
# failed must be indistinguishable from one that found nothing, and a claim of
# work must have evidence of work behind it.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0

# decide FOUND HEAD_BEFORE CONCLUSION -> "KIND|DETAIL|EXTRA"
decide() {
  local out
  out=$(bash .github/scripts/handover-decision.sh "$1" "$2" "$3")
  printf '%s|%s|%s' \
    "$(printf '%s' "$out" | sed -n 1p)" \
    "$(printf '%s' "$out" | sed -n 2p)" \
    "$(printf '%s' "$out" | sed -n 3p)"
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

SHA_A=488be1e91dbf08412c811eef46ecb3bdff2bc288
SHA_B=4cd0ff514e9feb3ee5335b6ec007495ac2475929

echo "An issue whose agent opened a pull request"
says "a closing or cross-referenced pull request is the answer" \
     "$(decide "pr 71" "" success)" "done_pr|71|"
says "and it outranks a conclusion that never arrived — the work is there" \
     "$(decide "pr 71" "" "")" "done_pr|71|"
says "a clean run that opened nothing says so" \
     "$(decide "" "" success)" "done_nopr||"
says "a run that died reports the conclusion word" \
     "$(decide "" "" cancelled)" "failed|cancelled|"
says "and a missing conclusion is named rather than left blank" \
     "$(decide "" "" "")" "failed|no conclusion|"

echo
echo "A pull request that started its own agent"
says "a moved head is the evidence, and only then is it done" \
     "$(decide "here 71 $SHA_B" "$SHA_A" success)" "done_here|71|"
says "THE REVIEW FINDING: an unmoved head is not a push, however clean the run" \
     "$(decide "here 71 $SHA_A" "$SHA_A" success)" "done_here_nopush|71|"
says "and an unmoved head on a run that died is a failure, not a no-op" \
     "$(decide "here 71 $SHA_A" "$SHA_A" cancelled)" "failed|cancelled|"
says "SECOND REVIEW FINDING: a moved head on a run that died is neither" \
     "$(decide "here 71 $SHA_B" "$SHA_A" cancelled)" "done_here_cut|71|cancelled"
says "and a moved head with no conclusion at all says so in words" \
     "$(decide "here 71 $SHA_B" "$SHA_A" "")" "done_here_cut|71|no conclusion"
says "only a clean run over a moved head is plain done" \
     "$(decide "here 71 $SHA_B" "$SHA_A" success)" "done_here|71|"
says "a head this step could not read before the run counts as unmoved" \
     "$(decide "here 71 $SHA_B" "" success)" "done_here_nopush|71|"
says "as does a head it cannot read now" \
     "$(decide "here 71" "$SHA_A" success)" "done_here_nopush|71|"
says "an unreadable head with no conclusion is a failure, never a claim" \
     "$(decide "here 71" "" "")" "failed|no conclusion|"

echo
echo "The lookup failed, and must look like it found nothing"
says "THE #71 DEFECT: a GraphQL error document is not a pull request number" \
     "$(decide '{"data":{"repository":{"issue":null}},"errors":[{"type":"NOT_FOUND"}]}' "" success)" \
     "done_nopr||"
says "nor is it one when the run also died" \
     "$(decide '{"data":{"repository":{"issue":null}}}' "" failure)" "failed|failure|"
says "a bare number with no prefix is not a shape this accepts" \
     "$(decide "71" "" success)" "done_nopr||"
says "neither is the right word with the wrong payload" \
     "$(decide "pr NaN" "" success)" "done_nopr||"
says "or a payload that only starts numeric" \
     "$(decide "pr 71x" "" success)" "done_nopr||"
says "a head that is not hexadecimal is discarded, not compared" \
     "$(decide "here 71 ../../etc/passwd" "$SHA_A" success)" "done_here_nopush|71|"
says "and neither field is taken from a longer line than the shape allows" \
     "$(decide "here 71 $SHA_B extra" "$SHA_A" success)" "done_here_nopush|71|"

echo
echo "The head moved for a reason that was not this run"
# The gate used to take the "before" snapshot at event time, while the agent job
# waits in a repo-wide writer queue -- so a second @claude queued ahead of this
# one, or a person pushing a fixup, moved the head and this run got the credit.
# The decision cannot detect that on its own; the snapshot moved into the agent
# job, behind the queue. What this asserts is the shape the fix relies on: an
# unreadable "before" is never evidence of a push.
says "an empty before-head is never evidence, whatever the head is now" \
     "$(decide "here 71 $SHA_B" "" success)" "done_here_nopush|71|"
says "and an empty before-head on a dead run is a failure, not a claim" \
     "$(decide "here 71 $SHA_B" "" cancelled)" "failed|cancelled|"
says "identical before and after is not a push even on a clean run" \
     "$(decide "here 71 $SHA_A" "$SHA_A" success)" "done_here_nopush|71|"

echo
echo "Whitespace and empty input, which is what an unset variable looks like"
says "nothing at all, with a clean run" "$(decide "" "" success)" "done_nopr||"
says "nothing at all, with nothing else" "$(decide "" "" "")" "failed|no conclusion|"
says "a lone space is not a shape" "$(decide " " "" success)" "done_nopr||"
says "a leading space defeats the prefix, and that is the safe direction" \
     "$(decide " pr 71" "" success)" "done_nopr||"

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
