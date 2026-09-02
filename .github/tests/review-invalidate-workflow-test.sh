#!/usr/bin/env bash
# The two steps that CALL the invalidation, executed rather than read.
#
# `.github/tests/review-published-test.sh` asserts what
# `.github/scripts/review-invalidate.sh` and `review-not-run.sh` do. This file
# asserts that `claude-pr-review.yml` reaches them with the right environment
# and takes their exit status -- the half a unit test of the
# helper cannot see. The shell bodies are extracted from the YAML and run
# against a stub `gh`, the same way orchestration-bundle-test.sh runs the
# hand-over. A guard whose failure mode is a label quietly staying put cannot be
# verified by grep.
#
# `review-published-test.sh` runs this file from the CI line it already owns.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }
say() { if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "wanted '$3', got '$2'"; fi; }

LIVE=.github/workflows/claude-pr-review.yml
sandbox=$(mktemp -d) || exit 1
trap 'rm -rf "$sandbox"' EXIT

WF=$LIVE
if ! grep -q 'review-invalidate.sh' "$WF" || ! grep -q 'review-not-run.sh' "$WF"; then
  no "the live workflow calls both shipping helpers" \
     "$WF does not call review-invalidate.sh and review-not-run.sh"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
echo "  reading  $WF"

# The same extractor orchestration-bundle-test.sh uses, and for the same reason:
# a `run: |` body that takes every value through `env:` is executable outside
# the runner, and one that interpolates `${{ }}` is not. That property is
# asserted below rather than assumed, because the day somebody interpolates a
# context into either step, this file stops being able to see anything.
extract_run_block() {
  awk -v want="$1" '
    index($0, "- name: " want) { instep = 1; next }
    instep && $0 ~ /^[[:space:]]*run: \|[[:space:]]*$/ {
      match($0, /^[[:space:]]*/); indent = RLENGTH; inrun = 1; next
    }
    inrun {
      if ($0 ~ /^[[:space:]]*$/) { print ""; next }
      match($0, /^[[:space:]]*/)
      if (RLENGTH <= indent) exit
      print substr($0, indent + 3)
    }
  ' "$2"
}

extract_step() {
  awk -v want="$1" '
    index($0, "- name: " want) { instep = 1 }
    instep && $0 ~ /^[[:space:]]*- name: / && index($0, "- name: " want) == 0 { exit }
    instep { print }
  ' "$2"
}

SILENT=$(extract_run_block "Say that the review published nothing" "$WF")
NORUN=$(extract_run_block "Say that the review did not happen" "$WF")
EARLY=$(extract_run_block "Drop the previous head's pass before reviewing this one" "$WF")
PUBLISHED=$(extract_run_block "Establish whether the review was published" "$WF")
if [ -z "$SILENT" ] || [ -z "$NORUN" ] || [ -z "$EARLY" ] || [ -z "$PUBLISHED" ]; then
  no "both steps' shell can be extracted and run" \
     "no 'run: |' body found under one of the two step names in $WF -- if a step was renamed, re-point this test rather than deleting it"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
ok "both steps' shell can be extracted and run"

# The routing around those bodies is part of the production guard. A completed
# comment can coexist with `steps.review.outcome == failure` because
# `--max-turns` is checked after the action has published. The publication step
# must therefore run for that outcome, while the no-review path must explicitly
# exclude a proven publication.
PUBLISHED_STEP=$(extract_step "Establish whether the review was published" "$WF")
WHY_STEP=$(extract_step "Work out why the review did not happen" "$WF")
NORUN_STEP=$(extract_step "Say that the review did not happen" "$WF")
if printf '%s\n' "$PUBLISHED_STEP" | grep -Fq "steps.review.outcome != 'failure'"; then
  no "a failed action still checks whether its verdict was published" \
     "the publication step is skipped on failure"
else
  ok "a failed action still checks whether its verdict was published"
fi
check_published_guard() {
  local block="$1" what="$2"
  if printf '%s\n' "$block" | grep -Fq "steps.published.outputs.state != 'published'"; then
    ok "$what excludes an already-published verdict"
  else
    no "$what excludes an already-published verdict" \
       "its live workflow condition does not test publication"
  fi
}
check_published_guard "$WHY_STEP" "the diagnosis step"
check_published_guard "$NORUN_STEP" "the did-not-run step"

for pair in "SILENT:the silent step" "NORUN:the did-not-run step" "EARLY:the early-strip step" "PUBLISHED:the publication step"; do
  var=${pair%%:*}
  what=${pair#*:}
  # shellcheck disable=SC2016  # the literal characters ${{ are the thing sought.
  if printf '%s' "${!var}" | grep -q '\${{'; then
    no "$what takes every value through env:, not \${{ }}" \
       "a \${{ }} expression appeared in the body; besides being an injection surface, it makes the block unexecutable and this test blind"
  else
    ok "$what takes every value through env:, not \${{ }}"
  fi
done

mkdir -p "$sandbox/bin" "$sandbox/ws/.github/scripts" "$sandbox/trusted"

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

# The bundle the workflow stages from the default branch...
cp .github/scripts/review-invalidate.sh .github/scripts/review-not-run.sh \
   "$sandbox/trusted/" || exit 1
# ...and the workspace the reviewed branch leaves behind. The step must never
# read this one. `contents: read` bounds the damage; it does not make reading
# the branch's own copy of a privileged helper correct.
cat > "$sandbox/ws/.github/scripts/review-invalidate.sh" <<'HOSTILE'
#!/usr/bin/env bash
touch "$PWNED_MARKER"
echo "PWNED"
HOSTILE
chmod +x "$sandbox/ws/.github/scripts/review-invalidate.sh"
cat > "$sandbox/ws/.github/scripts/review-not-run.sh" <<'HOSTILE'
#!/usr/bin/env bash
touch "$PWNED_MARKER"
echo "PWNED"
HOSTILE
chmod +x "$sandbox/ws/.github/scripts/review-not-run.sh"

# step <block> <trusted-dir-or-empty>; knobs through the caller's environment.
step() {
  : > "$sandbox/log"; : > "$sandbox/removed"; : > "$sandbox/comment"
  rm -f "$sandbox/pwned"
  ( cd "$sandbox/ws" || exit 1
    PATH="$sandbox/bin:$PATH" TMPDIR="$sandbox" \
    STUB_LOG="$sandbox/log" STUB_REMOVED="$sandbox/removed" \
    STUB_COMMENT="$sandbox/comment" PWNED_MARKER="$sandbox/pwned" \
    STUB_FAIL_API="${FAIL_API:-}" STUB_FAIL_COMMENT="${FAIL_COMMENT:-}" \
    STUB_FAIL_REMOVE="${FAIL_REMOVE:-}" STUB_BODIES="${BODIES:-}" \
    GH_TOKEN=stub REPO=owner/repo PR=42 SHA=deadbeefcafe1234 \
    RUN_URL=https://example.invalid/run/1 DETAIL="Reason: none" \
    TRUSTED="$2" \
    bash -c "$1" ) > "$sandbox/out" 2>&1
  echo $?
}

removed() { sort -u "$sandbox/removed" | tr '\n' ' ' | sed 's/ $//'; }
BOTH='ai-review:blocking ai-review:pass'
sign() { [ "$1" -eq 0 ] && echo zero || echo nonzero; }

# ---------------------------------------------------------------------------
echo
echo "The silent step delegates, and survives a failed notification"

# Every case in this section ends non-zero, and that is the point of #339: the
# silence is the defect, so the exit status carries it. What the cases still
# discriminate is the ANNOTATION -- "nothing was published" alone, or that plus
# "the stale label is still there" -- and whether the labels actually came off.
# A version that exits 1 without invalidating would pass the exit assertions
# and fail `removed()`, which is why both are asserted every time.
rc=$(FAIL_COMMENT=1 step "$SILENT" "$sandbox/trusted")
say 'a failed gh pr comment does not keep the stale verdict' "$(removed)" "$BOTH"
say '...and the step is red, because the review published nothing' "$(sign "$rc")" nonzero
if [ -e "$sandbox/pwned" ]; then
  no "...and the workspace copy of the helper never runs" \
     "the reviewed branch supplied the code that decides whether its own verdict label survives"
else
  ok "...and the workspace copy of the helper never runs"
fi

rc=$(FAIL_API=1 step "$SILENT" "$sandbox/trusted")
say 'a failed dedupe read does not keep the stale verdict' "$(removed)" "$BOTH"
say '...and the step is red, because the review published nothing' "$(sign "$rc")" nonzero
# The order this replaced did not lose the labels on THIS path -- the read sits
# in an `if` condition, which `set -e` exempts -- but it did fall through to the
# `else` and post a note it could not prove was absent. A dedupe that cannot
# read is a dedupe that does not know, and a second copy of the same note is how
# the 2026-08-22 turn-limit note was misread on the step below.
say '...and does not post a note it could not prove was absent' \
    "$(wc -c < "$sandbox/comment" | tr -d ' ')" 0

rc=$(FAIL_REMOVE=ai-review:pass step "$SILENT" "$sandbox/trusted")
say 'a failed removal fails the step' "$(sign "$rc")" nonzero
say '...and the helper still says which label is stuck' \
    "$(grep -c '::error::could not remove the stale' "$sandbox/out")" 1

rc=$(step "$SILENT" "$sandbox/trusted")
say 'the ordinary path removes both labels' "$(removed)" "$BOTH"
say '...and posts exactly one note' \
    "$(grep -c 'attadipa-review-not-published:deadbeefcafe1234' "$sandbox/comment")" 1
# The regression this file exists to catch from now on. Before #339 the whole
# section above asserted `zero` here, and a silent review reported a green
# `Independent review` check that every cheap signal read as "reviewed".
say '...and is red even though the invalidation worked' "$(sign "$rc")" nonzero
say '...and says why, once' \
    "$(grep -c '::error::the independent review ran on deadbeef and published no verdict' "$sandbox/out")" 1
say '...and does not also claim a label is stuck' \
    "$(grep -c '::error::could not remove' "$sandbox/out")" 0

# ---------------------------------------------------------------------------
echo
echo "...and without the bundle it drops the note rather than the invalidation"

rc=$(step "$SILENT" "")
say 'an unstaged helper still removes both labels' "$(removed)" "$BOTH"
say '...and says so in the log' \
    "$(grep -c '::warning::the review invalidation helper was not staged' "$sandbox/out")" 1
say '...and is red for the same reason as the staged path' "$(sign "$rc")" nonzero
rc=$(FAIL_REMOVE=ai-review:blocking step "$SILENT" "")
say '...and a removal that fails there is named as well as red' \
    "$(grep -c '::error::could not remove the stale' "$sandbox/out")" 1

# ---------------------------------------------------------------------------
echo
echo "The did-not-run step invalidates before it says anything"

rc=$(FAIL_COMMENT=1 step "$NORUN" "$sandbox/trusted")
say 'a failed note does not keep the stale pass' "$(removed)" ai-review:pass
say '...and the step is red rather than quietly green' "$(sign "$rc")" nonzero

rc=$(FAIL_REMOVE=ai-review:pass step "$NORUN" "$sandbox/trusted")
say 'a failed removal there is a failed step, not a swallowed one' "$(sign "$rc")" nonzero

rc=$(step "$NORUN" "$sandbox/trusted")
say 'the ordinary path removes the stale pass' "$(removed)" ai-review:pass
# `ai-review:blocking` is deliberately left alone: a review that did not run has
# said nothing that justifies releasing a hold somebody else put on.
say '...and leaves ai-review:blocking alone' \
    "$(grep -c 'remove-label ai-review:blocking' "$sandbox/log")" 0
say '...and posts one note' \
    "$(grep -c 'attadipa-review-did-not-run:deadbeefcafe1234' "$sandbox/comment")" 1
say '...and exits 0' "$(sign "$rc")" zero
if [ -e "$sandbox/pwned" ]; then
  no "...and the reviewed branch cannot replace the no-review helper" \
     "the workspace copy ran instead of the staged default-branch helper"
else
  ok "...and the reviewed branch cannot replace the no-review helper"
fi

# ---------------------------------------------------------------------------
echo
echo "The early strip takes the previous head's pass off when the head arrives"

rc=$(step "$EARLY" "$sandbox/trusted")
say 'it removes the previous head'"'"'s pass' "$(removed)" ai-review:pass
# A stale block holds a merge; a stale pass releases one. Only the second is
# worth acting on before anything has reviewed the new head, and the
# did-not-run path leaves `ai-review:blocking` alone for the same reason.
say '...and leaves ai-review:blocking alone' \
    "$(grep -c 'remove-label ai-review:blocking' "$sandbox/log")" 0
say '...and says nothing on the pull request' \
    "$(wc -c < "$sandbox/comment" | tr -d ' ')" 0
say '...and exits 0' "$(sign "$rc")" zero

rc=$(FAIL_REMOVE=ai-review:pass step "$EARLY" "$sandbox/trusted")
# This step is an earlier attempt at an invalidation the publish path repeats
# and fails on. Red here would report the API's bad minute as a failed review.
say 'an API failure here is a warning, not a red check' "$(sign "$rc")" zero
say '...and names the label it could not remove' \
    "$(grep -c "::warning::could not remove a previous head's" "$sandbox/out")" 1

# The `if:` is the half that decides WHEN, and it cannot be executed here.
EARLY_STEP=$(extract_step "Drop the previous head's pass before reviewing this one" "$WF")
if printf '%s\n' "$EARLY_STEP" | grep -Fq "github.event.action == 'synchronize'"; then
  ok "and it fires on a new head rather than on every event"
else
  no "and it fires on a new head rather than on every event" \
     "its condition does not name 'synchronize'; on reopened or ready_for_review the head is unchanged and the label is still this head's"
fi

# ---------------------------------------------------------------------------
echo
echo "The publication read is retried, and an answer it cannot get is red (#391)"

# `review-published.sh` answers `unknown` when the comments it was handed are
# not a list, and no step in the job matches `unknown`: on #382 the ledger
# froze at round 5 through four further publications with every run green.
# The step must therefore try again, and then fail rather than fall through.
cp .github/scripts/review-published.sh "$sandbox/trusted/" || exit 1
state() { sed -n 's/^state=//p' "$sandbox/output" | tail -1; }
reads() { grep -c '^api ' "$sandbox/log"; }
pub() {
  : > "$sandbox/output"
  GITHUB_OUTPUT="$sandbox/output" STARTED=2026-08-25T00:00:00Z OUTCOME=success \
    ATTADIPA_TRUSTED_SCRIPTS="$sandbox/trusted" step "$PUBLISHED" "$sandbox/trusted"
}

rc=$(FAIL_API=1 pub)
say 'a comment read that keeps failing is tried three times' "$(reads)" 3
say '...and records unknown, not silent: nothing proved the review said nothing' "$(state)" unknown
say '...and the step is red, so the run cannot end green having converged nothing' "$(sign "$rc")" nonzero
say '...and says why' "$(grep -c '::error::.*could not be read back' "$sandbox/out")" 1

rc=$(BODIES='[[]]' pub)
say 'an empty comment list is silent, not unknown' "$(state)" silent
say '...read once' "$(reads)" 1
say '...and green here; the silent step carries that verdict' "$(sign "$rc")" zero

rc=$(BODIES='[[{"user":{"login":"claude[bot]"},"created_at":"2026-08-25T00:10:00Z","updated_at":"2026-08-25T00:10:00Z","body":"review"}]]' pub)
say 'a reviewer comment written during the run is published' "$(state)" published
say '...and green' "$(sign "$rc")" zero

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
