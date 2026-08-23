#!/usr/bin/env bash
# Every branch of the publication guard, and the mistake that produced it.
#
# The case this file exists for is "a review published by EDITING". The first
# version of review-published.sh counted `created_at` only, and would have
# reported `silent` for run 32608091395 on #85 -- a 22-minute, 83-turn, $8.47
# review that published a full blocking verdict by editing comment 5382540003
# in place, exactly as the prompt instructs. A guard that fires on success is a
# guard somebody switches off. That is `an edited comment is published` below,
# and it fails against the version this replaced.
#
# The other rule being argued for: the two directions of error are not
# symmetric. Missing a silence leaves a stale label that merge-candidate.sh
# already refuses to merge on; inventing one deletes a real verdict and posts a
# comment contradicting a review anybody can scroll to. So every unreadable
# fact must come out `unknown`, and those cases are here too.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0

# verdict RAN OUTCOME COMMENTS LABELS STARTED -> the first word only
verdict() {
  bash .github/scripts/review-published.sh "$1" "$2" "$3" "$4" "$5" | awk '{print $1}'
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

# The run window used throughout. RUN is inside it; BEFORE is three pushes ago.
RUN=2026-08-23T00:31:47Z
DURING=2026-08-23T00:53:32Z
BEFORE=2026-08-22T20:52:09Z

# comment LOGIN CREATED UPDATED [BODY]
comment() {
  printf '{"user":{"login":"%s"},"created_at":"%s","updated_at":"%s","body":%s}' \
    "$1" "$2" "$3" "$(printf '%s' "${4-a review}" | jq -Rs .)"
}
# Joins the objects above into one array. Written out rather than `printf
# '%s,'`-and-trim because a trailing comma is invalid JSON, which jq would
# report as a failed read -- and a helper that turns every case into `unknown`
# would print `ok` for the three cases that expect it and nothing else.
arr() {
  local out="" one
  for one in "$@"; do
    if [ -z "$out" ]; then out="$one"; else out="$out,$one"; fi
  done
  printf '[%s]' "$out"
}

MARKER='<!-- attadipa-ai-review -->'
PASS_LABEL=$'ai-review:pass'
BLOCK_LABEL=$'ai-review:blocking'

echo "The model was never reached — a different failure, with its own notice"
says "no execution log is not this file's business" \
     "$(verdict no success "$(arr "$(comment 'claude[bot]' "$BEFORE" "$BEFORE")")" "" "$RUN")" \
     "not-run"
says "a failed Review step likewise" \
     "$(verdict yes failure "[]" "" "$RUN")" "not-run"
says "and not-run wins even with a stale verdict label sitting there" \
     "$(verdict no success "[]" "$BLOCK_LABEL" "$RUN")" "not-run"

echo
echo "A review that reached the pull request"
says "a comment created during the run is published" \
     "$(verdict yes success "$(arr "$(comment 'claude[bot]' "$DURING" "$DURING")")" "" "$RUN")" \
     "published"
says "an edited comment is published — run 32608091395, the case this file exists for" \
     "$(verdict yes success "$(arr "$(comment 'claude[bot]' "$BEFORE" "$DURING")")" "$BLOCK_LABEL" "$RUN")" \
     "published"
says "a comment written at the exact instant the run started counts" \
     "$(verdict yes success "$(arr "$(comment 'claude[bot]' "$RUN" "$RUN")")" "" "$RUN")" \
     "published"
says "a review under a different login is published if it carries the marker" \
     "$(verdict yes success "$(arr "$(comment 'attadipa-agent[bot]' "$DURING" "$DURING" "$MARKER
findings")")" "" "$RUN")" \
     "published"
says "one fresh review among older ones is enough" \
     "$(verdict yes success "$(arr "$(comment 'claude[bot]' "$BEFORE" "$BEFORE")" \
                                   "$(comment 'hleserg' "$BEFORE" "$BEFORE")" \
                                   "$(comment 'claude[bot]' "$DURING" "$DURING")")" "" "$RUN")" \
     "published"

echo
echo "A review that did not"
says "the model ran, published nothing, and no label is set" \
     "$(verdict yes success "[]" "" "$RUN")" "silent"
says "the 41-second run of 2026-08-21: read the diff, no way to say so" \
     "$(verdict yes success "$(arr "$(comment 'hleserg' "$DURING" "$DURING")")" "" "$RUN")" \
     "silent"
says "#39's turn-50 kill: a stale pass is the previous commit's, not this one's" \
     "$(verdict yes success "$(arr "$(comment 'claude[bot]' "$BEFORE" "$BEFORE")")" "$PASS_LABEL" "$RUN")" \
     "silent"
says "a stale blocking label is no better — it names nothing to fix" \
     "$(verdict yes success "$(arr "$(comment 'claude[bot]' "$BEFORE" "$BEFORE")")" "$BLOCK_LABEL" "$RUN")" \
     "silent"
says "a label with no comment at all is a verdict with no reasoning attached" \
     "$(verdict yes success "[]" "$PASS_LABEL" "$RUN")" "silent"
says "somebody else's fresh comment is not the reviewer's" \
     "$(verdict yes success "$(arr "$(comment 'github-actions[bot]' "$DURING" "$DURING")")" "" "$RUN")" \
     "silent"
says "the marker in a HUMAN's comment does not stand in for a review either" \
     "$(verdict yes success "$(arr "$(comment 'hleserg' "$BEFORE" "$BEFORE" "$MARKER
quoting the reviewer")")" "" "$RUN")" \
     "silent"

echo
echo "Facts that could not be read hold rather than accuse"
says "an empty run start time" \
     "$(verdict yes success "[]" "" "")" "unknown"
says "a date with no time" \
     "$(verdict yes success "[]" "" "2026-08-23")" "unknown"
says "an offset instead of Z — string comparison would silently misorder it" \
     "$(verdict yes success "[]" "" "2026-08-23T00:31:47+03:00")" "unknown"
says "a millisecond field" \
     "$(verdict yes success "[]" "" "2026-08-23T00:31:47.000Z")" "unknown"
says "a gh error document where the timestamp should be" \
     "$(verdict yes success "[]" "" '{"message":"Not Found"}')" "unknown"
says "comments that are not JSON at all" \
     "$(verdict yes success "not json" "" "$RUN")" "unknown"
says "a gh error document where the comment array should be" \
     "$(verdict yes success '{"message":"Not Found","status":"404"}' "" "$RUN")" "unknown"
says "an empty comments payload — a fetch that produced nothing is not a fetch that found nothing" \
     "$(verdict yes success "" "" "$RUN")" "unknown"

echo
echo "Shapes review has caught elsewhere in this repository"
says "a label called 'x ai-review:pass' is not a verdict label" \
     "$(verdict yes success "[]" "x ai-review:pass" "$RUN")" "silent"
says "and it is not counted as one in the reason either" \
     "$(bash .github/scripts/review-published.sh yes success "[]" "x ai-review:pass" "$RUN" \
        | grep -c 'neither a comment nor a label')" "1"
says "a null user object does not crash the count" \
     "$(verdict yes success '[{"user":null,"created_at":"'"$DURING"'","updated_at":"'"$DURING"'"}]' "" "$RUN")" \
     "silent"
says "a comment missing its timestamps does not count as fresh" \
     "$(verdict yes success '[{"user":{"login":"claude[bot]"},"body":"x"}]' "" "$RUN")" "silent"
says "a JSON object rather than an array is a failed read, not an empty one" \
     "$(verdict yes success '{"user":{"login":"claude[bot]"}}' "" "$RUN")" "unknown"

echo
printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
