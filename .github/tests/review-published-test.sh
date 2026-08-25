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
# The other half of that sentence, and the half that was missing. This
# repository is public: if the marker admits any author, a drive-by comment
# carrying it turns a silent review into a published one, and `published` is
# what skips the step that strips the previous head's `ai-review:pass`.
say 'the marker from an untrusted author is not publication' "$(run yes success "[$(comment 'passer-by' "$NEW" "$NEW" '<!-- attadipa-ai-review --> looks fine to me')]" ai-review:pass "$START")" silent
say 'a bot name that merely looks official is not on the list' "$(run yes success "[$(comment 'claude-review[bot]' "$NEW" "$NEW" '<!-- attadipa-ai-review --> review')]" '' "$START")" silent
say 'a stale comment and pass label are silence' "$(run yes success "[$(comment 'claude[bot]' "$OLD" "$OLD" review)]" ai-review:pass "$START")" silent
say 'a current human comment is not a review' "$(run yes success "[$(comment hleserg "$NEW" "$NEW" review)]" '' "$START")" silent
say 'an unreadable comments payload holds' "$(run yes success '{"message":"no"}' '' "$START")" unknown

# ---------------------------------------------------------------------------
# ...and what `silent` then makes happen. #240.
#
# Deciding correctly that a review published nothing is half of the guard. The
# other half is stripping the previous head's verdict labels, and on the shape
# this replaces that half came LAST, after a dedupe `gh api` and a `gh pr
# comment`, under `set -euo pipefail`. Either network call returning non-zero
# ended the step before the labels came off -- so a head nobody had reviewed
# kept its `ai-review:pass` in precisely the case the guard exists for.
#
# `review-invalidate.sh` is executed here rather than read, against a stub `gh`
# on PATH that can be told which subcommand to fail. The last case runs the
# pre-#240 shape through the same assertions and requires it to fail them: a
# test that does not discriminate between the bug and the fix is not evidence
# that the fix is there.
echo
echo "A silent review invalidates before it notifies (#240)"

sandbox=$(mktemp -d) || exit 1
trap 'rm -rf "$sandbox"' EXIT
mkdir -p "$sandbox/bin"

cat > "$sandbox/bin/gh" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$STUB_LOG"
prev=
case "$1 $2" in
  "pr edit")
    for a in "$@"; do
      if [ "$prev" = "--remove-label" ]; then
        case ":${STUB_FAIL_REMOVE:-}:" in
          *":$a:"*) echo "gh: HTTP 502 removing $a" >&2; exit 1 ;;
        esac
        printf '%s\n' "$a" >> "$STUB_REMOVED"
      fi
      prev="$a"
    done
    ;;
  "pr comment")
    [ -z "${STUB_FAIL_COMMENT:-}" ] || { echo "gh: HTTP 502 posting a comment" >&2; exit 1; }
    for a in "$@"; do
      if [ "$prev" = "--body-file" ]; then cat "$a" >> "$STUB_COMMENT"; fi
      prev="$a"
    done
    ;;
  "api "*)
    [ -z "${STUB_FAIL_API:-}" ] || { echo "gh: HTTP 502 reading comments" >&2; exit 1; }
    printf '%s\n' "${STUB_BODIES:-}"
    ;;
esac
exit 0
STUB
chmod +x "$sandbox/bin/gh"

# The shape #240 found, reconstructed from `claude-pr-review.yml` as it stood at
# 36e1ba9 -- lines 485 to 498, with the note's prose shortened and nothing else
# touched. It is here so the assertions below can be aimed at it: they are only
# worth running if they can tell it apart from the file beside it.
cat > "$sandbox/pre-240.sh" <<'PRE240'
#!/usr/bin/env bash
set -euo pipefail
marker="attadipa-review-not-published:${SHA}"
if ! gh api "repos/$REPO/issues/$PR/comments" --paginate --jq '.[].body' | grep -Fq "$marker"; then
  {
    echo "<!-- $marker -->"
    echo "**The independent review ran on \`${SHA:0:8}\` but published no verdict.**"
    echo "Run: $RUN_URL"
  } > "$TMPDIR/review-not-published.md"
  gh pr comment "$PR" --repo "$REPO" --body-file "$TMPDIR/review-not-published.md"
fi
gh pr edit "$PR" --repo "$REPO" --remove-label ai-review:pass || true
gh pr edit "$PR" --repo "$REPO" --remove-label ai-review:blocking || true
PRE240

# invalidate <script> -- every knob arrives through the environment of the
# caller, so a case reads as the failure it is describing.
invalidate() {
  : > "$sandbox/log"; : > "$sandbox/removed"; : > "$sandbox/comment"
  ( PATH="$sandbox/bin:$PATH" TMPDIR="$sandbox" \
    STUB_LOG="$sandbox/log" STUB_REMOVED="$sandbox/removed" \
    STUB_COMMENT="$sandbox/comment" \
    STUB_FAIL_API="${FAIL_API:-}" STUB_FAIL_COMMENT="${FAIL_COMMENT:-}" \
    STUB_FAIL_REMOVE="${FAIL_REMOVE:-}" STUB_BODIES="${BODIES:-}" \
    GH_TOKEN=stub REPO=owner/repo PR=42 SHA=deadbeefcafe1234 \
    RUN_URL=https://example.invalid/run/1 \
    bash "$1" ) > "$sandbox/out" 2>&1
  echo $?
}

HELPER=.github/scripts/review-invalidate.sh
removed() { sort -u "$sandbox/removed" | tr '\n' ' ' | sed 's/ $//'; }
BOTH='ai-review:blocking ai-review:pass'

# --- 1. The dedupe read fails. The note is dropped; the labels are not.
rc=$(FAIL_API=1 invalidate "$HELPER")
say 'a failed dedupe read still removes both verdict labels' "$(removed)" "$BOTH"
say '...and the step still succeeds, because the note is the courtesy' "$rc" 0
say '...and it says in the log that it could not read the comments' \
    "$(grep -c '::warning::the existing comments' "$sandbox/out")" 1

# --- 2. The notification itself fails. Same answer.
rc=$(FAIL_COMMENT=1 invalidate "$HELPER")
say 'a failed gh pr comment still removes both verdict labels' "$(removed)" "$BOTH"
say '...and the step still succeeds' "$rc" 0
say '...and it says the note did not go out' \
    "$(grep -c '::warning::the note for deadbeef could not be posted' "$sandbox/out")" 1

# --- 3. A removal fails. That is the one failure that must be loud, and the
#        second label must still be attempted rather than abandoned.
rc=$(FAIL_REMOVE=ai-review:pass invalidate "$HELPER")
say 'a failed removal is a failed step, not a green one' "$([ "$rc" -ne 0 ] && echo nonzero || echo zero)" nonzero
say '...and it names the label that is still on the pull request' \
    "$(grep -c '::error::could not remove the stale .ai-review:pass.' "$sandbox/out")" 1
say '...and the other label is removed anyway' "$(removed)" ai-review:blocking
say '...and the note still goes out, saying it has to come off by hand' \
    "$(grep -c 'take it off by hand' "$sandbox/comment")" 1

# --- 4. Nothing fails: one note, both labels, once.
rc=$(invalidate "$HELPER")
say 'the ordinary path removes both labels' "$(removed)" "$BOTH"
say '...and exits 0' "$rc" 0
say '...and posts exactly one note' "$(grep -c 'attadipa-review-not-published:deadbeefcafe1234' "$sandbox/comment")" 1
say '...carrying the head it is about' "$(grep -c 'ran on .deadbeef.' "$sandbox/comment")" 1

# --- 5. The dedupe marker still suppresses a second note for the same head, and
#        still does not suppress the invalidation.
rc=$(BODIES='<!-- attadipa-review-not-published:deadbeefcafe1234 -->' invalidate "$HELPER")
say 'a note already posted for this head is not posted twice' "$(wc -c < "$sandbox/comment" | tr -d ' ')" 0
say '...and the labels come off on the re-run as well' "$(removed)" "$BOTH"
say '...and the step succeeds' "$rc" 0

# --- 6. THE MUTATION. The pre-#240 order, through case 2's assertions. If this
#        passes them, the cases above are not testing what they claim to.
rc=$(FAIL_COMMENT=1 invalidate "$sandbox/pre-240.sh")
say 'the order this replaced leaves the stale verdict on the pull request' "$(removed)" ''
say '...which is what makes case 2 an assertion rather than a description' \
    "$([ "$rc" -ne 0 ] && echo nonzero || echo zero)" nonzero

# ---------------------------------------------------------------------------
# ...and the steps that call it, which run from here rather than from a line of
# their own in `ci.yml`.
#
# `review-invalidate-workflow-test.sh` extracts the two `claude-pr-review.yml`
# steps that reach the helper and executes them. It wanted its own step in
# `ci.yml`, and could not have one: that edit is a workflow edit, a workflow
# edit from a GitHub App has to be parked, and `gh-api-usage-test.sh` refuses
# two parked patches carrying one workflow file -- `75-approval-stall.patch`
# carries `ci.yml` already. Running it from here costs nothing and keeps it on a
# `ci.yml` line that exists.
echo
echo "...and the workflow steps that call it"
sub=$(bash .github/tests/review-invalidate-workflow-test.sh 2>&1); subrc=$?
# Its own tally is dropped and its cases are folded into this file's, so the
# line at the bottom is the number of assertions that actually ran.
printf '%s\n' "$sub" | grep -v '^[0-9]* passed, [0-9]* failed$'
subpass=$(printf '%s\n' "$sub" | sed -n 's/^\([0-9]*\) passed, \([0-9]*\) failed$/\1/p')
subfail=$(printf '%s\n' "$sub" | sed -n 's/^\([0-9]*\) passed, \([0-9]*\) failed$/\2/p')
if [ -n "$subpass" ] && [ -n "$subfail" ]; then
  pass=$((pass + subpass))
  fail=$((fail + subfail))
else
  printf '  FAIL  review-invalidate-workflow-test.sh reported no tally (exit %d)\n' "$subrc"
  fail=$((fail + 1))
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
