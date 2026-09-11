#!/usr/bin/env bash
# The follow-up issue a deferred finding is promised, executed.
#
# `review-verdict-test.sh` asserts what `review-verdict.sh` answers. This file
# asserts what `claude-pr-review.yml` DOES with the answer, because the defect
# of #503 was entirely in the caller: `/tmp/deferred.md` was written from the
# first day of the rule and `grep -n deferred` over the workflow returned
# exactly the one line that wrote it. The script was blameless and the ledger
# told every reader the finding "is still recorded in the table below and still
# filed as the follow-up issue". No issue existed. PR #485 round 5 is the
# demonstration: `leave-back-reason-is-stale` was deferred, the ledger said
# `no — deferred`, and had that pull request merged on round 5 the finding would
# have merged with it.
#
# A promise whose failure mode is SILENCE cannot be verified by grep -- the
# absent issue looks exactly like a round with nothing to defer -- so the step's
# shell is extracted from the YAML and run against a stub `gh`, the same way
# review-cap-stale-block-test.sh and review-invalidate-workflow-test.sh run
# theirs. The stub exits non-zero on any call it does not recognise: a call that
# answered with an empty string would look like a clean read of an empty result,
# and the file-once assertions would pass on nothing.
#
# THE FILE-ONCE PROPERTY IS THE POINT, and it is a property of the LEDGER, not
# of a search. `review-verdict.sh` says why: GitHub's issue search is an index
# with lag, so two rounds minutes apart would file the same follow-up twice.
# So the assertions below drive the second round from the ledger the first
# round wrote, rather than from a fixture written by hand.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

pass=0; fail=0
ok()  { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }
say() {
  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 -- got '$2', want '$3'"; fi
}
has() {
  case "$2" in *"$3"*) ok "$1" ;; *) bad "$1 -- '$3' is not in: $2" ;; esac
}
lacks() {
  case "$2" in *"$3"*) bad "$1 -- '$3' is in: $2" ;; *) ok "$1" ;; esac
}

WF=.github/workflows/claude-pr-review.yml

# The same extractor the other two workflow tests use, and for the same reason:
# a `run: |` body that takes every value through `env:` is executable outside
# the runner, and one that interpolates `${{ }}` is not.
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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/tmp"

STEP=$(extract_run_block "Converge the published reviewer verdict" "$WF")
if [ -z "$STEP" ]; then
  bad "the verdict step's shell can be extracted and run"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
ok "the verdict step's shell can be extracted and run"

# The step writes to absolute `/tmp` paths. Redirecting them into the work
# directory keeps a test run from colliding with anything else on the machine
# and, more to the point, makes the ledger this round writes readable by the
# next assertion instead of being left where the real runner would leave it.
printf '%s\n' "$STEP" | sed "s#/tmp/#$work/tmp/#g" > "$work/step.sh"

# The stub. `gh issue create` is the call under test: it echoes the URL the real
# command echoes, and records that it was called.
cat > "$work/bin/gh" <<STUB
#!/usr/bin/env bash
case "\$1 \$2" in
  "api repos/owner/repo/issues/7/comments")
    # No ledger comment and no findings comment on the pull request: both
    # bodies are supplied by the fixtures below, which is what lets one
    # scenario be the previous round of the next.
    exit 0 ;;
  "issue create")
    printf '%s\n' "\$*" >> "$work/created.log"
    printf 'https://github.com/owner/repo/issues/170\n' ;;
  "pr comment"|"pr edit") : ;;
  *) printf 'stub: unexpected call: %s\n' "\$*" >&2; exit 3 ;;
esac
STUB
chmod +x "$work/bin/gh"

# A findings block whose one open finding is first seen at the floor, so it
# defers rather than blocks, and a previous ledger that puts the round there.
findings() {
  cat <<'BLOCK'
<!-- attadipa-review-findings
leave-back-reason-is-stale | open | normal | The leave-back reason quotes a sentence that moved
-->
BLOCK
}

run_round() {
  # $1 the previous ledger body (may be empty). Writes the new ledger to
  # $work/tmp/new-ledger.md and echoes nothing.
  printf '%s' "$1" > "$work/prev.md"
  findings > "$work/findings.md"
  rm -f "$work/tmp/previous-ledger.md" "$work/tmp/findings.md" \
        "$work/tmp/new-ledger.md" "$work/tmp/deferred.md" "$work/tmp/verdict.txt"
  cp "$work/prev.md" "$work/seed-prev.md"
  cp "$work/findings.md" "$work/seed-findings.md"
  # The step reads the two bodies from the API. The stub answers no comment at
  # all, so they are planted where the step would have written them -- and the
  # step's own `: >` truncation is what makes that impossible. Running the
  # step's code with the reads already satisfied is therefore done by handing it
  # a `gh` that writes them, which is the shape below.
  cat > "$work/bin/gh" <<STUB
#!/usr/bin/env bash
case "\$1 \$2" in
  "api repos/owner/repo/issues/7/comments")
    case "\$*" in
      *ATTADIPA_LEDGER_ACTOR*) [ -s "$work/seed-prev.md" ] && printf '111\n'; exit 0 ;;
      *) printf '222\n'; exit 0 ;;
    esac ;;
  "api repos/owner/repo/issues/comments/111")
    cat "$work/seed-prev.md" ;;
  "api repos/owner/repo/issues/comments/222")
    cat "$work/seed-findings.md" ;;
  "api -X")
    # The ledger PATCH. Keep the body so the next round can be driven by it.
    for arg in "\$@"; do case "\$arg" in body=@*) cp "\${arg#body=@}" "$work/posted.md" ;; esac; done ;;
  "issue create")
    printf '%s\n' "\$*" >> "$work/created.log"
    printf 'https://github.com/owner/repo/issues/170\n' ;;
  "pr comment")
    for arg in "\$@"; do case "\$arg" in --body-file) : ;; esac; done
    cp "$work/tmp/new-ledger.md" "$work/posted.md" ;;
  "pr edit") : ;;
  *) printf 'stub: unexpected call: %s\n' "\$*" >&2; exit 3 ;;
esac
STUB
  chmod +x "$work/bin/gh"
  PATH="$work/bin:$PATH" \
  GH_TOKEN=x REPO=owner/repo PR=7 TRUSTED=.github/scripts \
  ATTADIPA_REVIEW_ACTOR='claude[bot]' ATTADIPA_LEDGER_ACTOR='github-actions[bot]' \
  HEAD_SHA=abcdef1234567890abcdef1234567890abcdef12 \
    bash "$work/step.sh" > "$work/out.txt" 2>&1
}

# ---- round 4: the finding is first seen at the floor and is deferred ---------
: > "$work/created.log"
run_round '<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=3
floor=4
-->'
first=$(cat "$work/posted.md" 2>/dev/null || printf '')

say "one issue is filed for the deferred finding" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "1"
has "the issue is filed from the body the script wrote" \
    "$(cat "$work/created.log")" "--body-file $work/tmp/deferred.md"
has "the issue title names the pull request it came from" \
    "$(cat "$work/created.log")" "Deferred review findings from #7"
has "the deferred body links the pull request" \
    "$(cat "$work/tmp/deferred.md")" "The independent review of #7"
has "the ledger's column names the issue rather than only deferring" \
    "$first" "no — deferred, filed as #170"
has "the ledger records the issue in its state block" \
    "$first" "deferred_issue=170"

# ---- round 5: the same finding, still open, driven by round 4's own ledger ---
: > "$work/created.log"
run_round "$first"
second=$(cat "$work/posted.md" 2>/dev/null || printf '')

say "a second round files nothing further" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "0"
has "the second ledger still names the issue" \
    "$second" "no — deferred, filed as #170"

# ---- a round with nothing to defer files nothing -----------------------------
: > "$work/created.log"
findings() {
  cat <<'BLOCK'
<!-- attadipa-review-findings
leave-back-reason-is-stale | open | normal | The leave-back reason quotes a sentence that moved
-->
BLOCK
}
run_round '<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=1
floor=4
leave-back-reason-is-stale | 1 | normal | open | The leave-back reason quotes a sentence that moved
-->'
say "a finding raised before the floor blocks and files nothing" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "0"
lacks "and its ledger promises no issue" \
    "$(cat "$work/posted.md")" "filed as #"

# ---- the mutant: the caller ignores /tmp/deferred.md, as it did before #503 --
: > "$work/created.log"
# The line is replaced rather than deleted, so the mutant is the caller as it
# stood before #503 -- reaching the same branch and doing nothing with the body
# -- and not a caller that dies on an unbound variable one line later.
# shellcheck disable=SC2016  # `$(gh issue create` is the literal text being
# matched in the extracted step, not a command substitution to run here.
sed 's|deferred_url=$(gh issue create.*|deferred_url=""|' "$work/step.sh" > "$work/mutant.sh"
cp "$work/mutant.sh" "$work/step.sh"
run_round '<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=3
floor=4
-->'
say "with the filing removed, the deferred finding gets no issue" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "0"
lacks "and the ledger promises what it cannot keep -- the defect of #503" \
    "$(cat "$work/posted.md")" "filed as #"

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
