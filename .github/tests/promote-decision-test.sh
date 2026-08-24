#!/usr/bin/env bash
# May the hand-over take this pull request out of draft?
#
# Offline and deterministic: four arguments in, one line out. See the header of
# .github/scripts/promote-decision.sh for what this is guarding against — in
# one sentence, that `gh pr ready` flips a bit which makes a branch eligible for
# an unattended merge, and the pull request number it is given for KIND=done_pr
# comes from a closing keyword anybody can write.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
script="$here/../scripts/promote-decision.sh"

pass=0; fail=0

RUN=2026-08-22T20:00:00Z   # when this hypothetical run started
BEFORE=2026-08-22T19:59:59Z
AFTER=2026-08-22T20:00:01Z
LONG_BEFORE=2026-08-19T11:00:00Z

# want DESCRIPTION WANTED_FIRST_WORD KIND CREATED HEAD_AT STARTED
want() {
  local desc="$1" wanted="$2"; shift 2
  local got first
  got=$(bash "$script" "$@")
  first=${got%% *}
  if [ "$first" = "$wanted" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s", got "%s"\n' "$desc" "$wanted" "$got"
  fi
}

# reason DESCRIPTION SUBSTRING KIND CREATED HEAD_AT STARTED
#
# The reason is not decoration: it is the only thing a person has when they ask
# why their finished pull request is still a draft.
reason() {
  local desc="$1" needle="$2"; shift 2
  local got
  got=$(bash "$script" "$@")
  case "$got" in
    *"$needle"*)
      pass=$((pass + 1)); printf '  ok    %s\n' "$desc" ;;
    *)
      fail=$((fail + 1)); printf '  FAIL  %s\n         wanted a reason containing "%s", got "%s"\n' "$desc" "$needle" "$got" ;;
  esac
}

echo "Promote decision — which kinds may be taken out of draft"

want "done_here is promoted on the moved head handover-decision.sh already proved" \
     promote done_here "" "" ""
want "done_here needs no timestamps at all" \
     promote done_here "$LONG_BEFORE" "$LONG_BEFORE" "$RUN"
want "done_here_nopush pushed nothing, so there is nothing to promote" \
     hold done_here_nopush "$AFTER" "$AFTER" "$RUN"
want "done_nopr has no pull request" \
     hold done_nopr "$AFTER" "$AFTER" "$RUN"
want "done_pr_cut is half-finished work, which is what a draft is for" \
     hold done_pr_cut "$AFTER" "$AFTER" "$RUN"
want "done_here_cut likewise" \
     hold done_here_cut "$AFTER" "$AFTER" "$RUN"
want "failed likewise" \
     hold failed "$AFTER" "$AFTER" "$RUN"
want "an unrecognised kind is held, not promoted" \
     hold something_new "$AFTER" "$AFTER" "$RUN"
want "an empty kind is held" \
     hold "" "$AFTER" "$AFTER" "$RUN"

echo
echo "done_pr — the heuristic that needs evidence"

want "a pull request created during this run is this run's work" \
     promote done_pr "$AFTER" "$AFTER" "$RUN"
want "so is one created earlier whose head commit landed during this run" \
     promote done_pr "$LONG_BEFORE" "$AFTER" "$RUN"
want "an abandoned pull request with a closing keyword is NOT — the whole point" \
     hold done_pr "$LONG_BEFORE" "$LONG_BEFORE" "$RUN"
reason "and the reason says so in words a person can act on" \
     "does not own it" done_pr "$LONG_BEFORE" "$LONG_BEFORE" "$RUN"
want "a second earlier is still earlier; the boundary is not fuzzy" \
     hold done_pr "$BEFORE" "$BEFORE" "$RUN"
want "exactly at the start counts as during, for creation" \
     promote done_pr "$RUN" "$LONG_BEFORE" "$RUN"
want "exactly at the start counts as during, for the head commit" \
     promote done_pr "$LONG_BEFORE" "$RUN" "$RUN"

echo
echo "When in doubt, hold"

want "no timestamps at all is held, never promoted" \
     hold done_pr "" "" "$RUN"
reason "and says which two facts were missing" \
     "neither the pull request" done_pr "" "" "$RUN"
want "an unreadable run start makes every comparison meaningless" \
     hold done_pr "$AFTER" "$AFTER" ""
reason "and says that rather than blaming the pull request" \
     "run's start time" done_pr "$AFTER" "$AFTER" ""
want "a gh error document in the created field is refused, not parsed" \
     hold done_pr '{"data":{"repository":null}}' "$LONG_BEFORE" "$RUN"
want "an offset instead of Z is refused — it is not comparable as a string" \
     hold done_pr "2026-08-22T23:00:00+03:00" "$LONG_BEFORE" "$RUN"
want "a date with no time is refused" \
     hold done_pr "2026-08-23" "$LONG_BEFORE" "$RUN"
want "a millisecond field is refused rather than silently mis-sorted" \
     hold done_pr "2026-08-22T20:00:01.500Z" "$LONG_BEFORE" "$RUN"
want "an unreadable creation time does not stop a readable head commit counting" \
     promote done_pr "not-a-date" "$AFTER" "$RUN"
want "an unreadable head commit does not stop a readable creation time counting" \
     promote done_pr "$AFTER" "not-a-date" "$RUN"
want "one unreadable and one too old is still held" \
     hold done_pr "not-a-date" "$LONG_BEFORE" "$RUN"

echo
echo "Output shape"

# The caller reads one line and branches on its first word. Two lines, or a
# word that is neither, is a contract break -- the same class of defect as the
# "NUMBER FAILED" skew this pull request's watchdog half is about.
lines=$(bash "$script" done_pr "$LONG_BEFORE" "$LONG_BEFORE" "$RUN" | wc -l)
if [ "$lines" -eq 1 ]; then
  pass=$((pass + 1)); printf '  ok    a hold is exactly one line\n'
else
  fail=$((fail + 1)); printf '  FAIL  a hold is exactly one line\n         got %s\n' "$lines"
fi
if [ "$(bash "$script" done_here "" "" "")" = "promote" ]; then
  pass=$((pass + 1)); printf '  ok    a promote is the bare word, with nothing after it\n'
else
  fail=$((fail + 1)); printf '  FAIL  a promote is the bare word, with nothing after it\n'
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
