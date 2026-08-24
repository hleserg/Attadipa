#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0
say() { if [ "$2" = "$3" ]; then printf '  ok    %s\n' "$1"; pass=$((pass + 1)); else printf '  FAIL  %s: wanted %s, got %s\n' "$1" "$3" "$2"; fail=$((fail + 1)); fi; }
run() { bash .github/scripts/review-published.sh "$@" | awk '{print $1}'; }
START=2026-08-25T00:00:00Z
OLD=2026-08-24T23:00:00Z
NEW=2026-08-25T00:10:00Z
comment() { printf '{"user":{"login":"%s"},"created_at":"%s","updated_at":"%s","body":"%s"}' "$1" "$2" "$3" "$4"; }

say 'a run that did not reach the model stays separate' "$(run no success '[]' '' "$START")" not-run
say 'a bad start time holds rather than guessing' "$(run yes success '[]' '' '')" unknown
say 'a reviewer comment created this run is publication' "$(run yes success "[$(comment 'claude[bot]' "$NEW" "$NEW" review)]" '' "$START")" published
say 'editing an old review comment is publication' "$(run yes success "[$(comment 'claude[bot]' "$OLD" "$NEW" review)]" ai-review:pass "$START")" published
say 'the marker permits the configured token author' "$(run yes success "[$(comment 'attadipa-agent[bot]' "$NEW" "$NEW" '<!-- attadipa-ai-review --> review')]" '' "$START")" published
say 'a stale comment and pass label are silence' "$(run yes success "[$(comment 'claude[bot]' "$OLD" "$OLD" review)]" ai-review:pass "$START")" silent
say 'a current human comment is not a review' "$(run yes success "[$(comment hleserg "$NEW" "$NEW" review)]" '' "$START")" silent
say 'an unreadable comments payload holds' "$(run yes success '{"message":"no"}' '' "$START")" unknown
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
