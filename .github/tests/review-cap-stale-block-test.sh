#!/usr/bin/env bash
# The cap gate's decision to clear or hold `ai-review:blocking`, executed.
#
# `review-verdict-test.sh` asserts what `review-verdict.sh gate` and
# `review-verdict.sh cap` answer. This file asserts what `claude-pr-review.yml`
# does with the second of those once the five rounds are gone -- specifically
# the question the invalidation step is not entitled to ask and this one is: is
# the standing block still about the commit being merged? #445 merged carrying
# one no push could clear, because nothing asked it. A guard whose failure mode
# is a label quietly staying put cannot be verified by grep, so the step's shell
# is extracted from the YAML and run against a stub `gh`, the same way
# review-invalidate-workflow-test.sh runs the invalidation.
#
# WHAT THIS FILE GOT WRONG THE FIRST TIME, because that is the defect it now
# exists to keep out. Every scenario used to answer `deadbeef` to
# `--json headRefOid`, and the two opposite outcomes -- "a head pushed after the
# block" and "a bare re-run against the blocked head" -- were selected by
# nothing but the committer timestamps the stub returned beside it. Eleven
# assertions passed over one exact SHA, so the test could not fail on a rule
# that read only the clock, and the rule under it read only the clock: a future
# `GIT_COMMITTER_DATE` on the unchanged blocked head cleared a blocking verdict
# with no commit in between. Issue #199. So every scenario below names two
# object ids, the timestamps are held constant across the pairs that must differ
# and varied across the pairs that must not, and the last block re-installs the
# old rule under the real caller and asserts the whole set goes red.
#
# The stub exits non-zero on any call it does not recognise. An unrecognised
# call that answered with an empty string would look exactly like a clean API
# read of an empty result, and half these assertions would pass on nothing.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

pass=0; fail=0
ok()  { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }
say() {
  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 -- got '$2', want '$3'"; fi
}
# The mutant's assertions are the mirror image: a scenario that still agrees
# with the correct answer is a scenario that was not testing the rule.
differs() {
  if [ "$2" != "$3" ]; then ok "$1"; else bad "$1 -- got '$2', which is what the real rule gives"; fi
}

WF=.github/workflows/claude-pr-review.yml

# The same extractor review-invalidate-workflow-test.sh uses, and for the same
# reason: a `run: |` body that takes every value through `env:` is executable
# outside the runner, and one that interpolates `${{ }}` is not.
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
mkdir -p "$work/bin" "$work/mutant"

CAP=$(extract_run_block "Has this review already had its five rounds" "$WF")
if [ -z "$CAP" ]; then
  bad "the cap step's shell can be extracted and run"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
printf '%s\n' "$CAP" > "$work/cap.sh"
ok "the cap step's shell can be extracted and run"

# THE WORKFLOW HALF OF #199 CANNOT BE PUSHED BY THE AGENT THAT WROTE IT.
# `claude[bot]` holds no `workflows` permission, so `.github/workflows/*.yml`
# reaches a branch through an owner-capable credential and through nothing else.
# `review-verdict.sh cap` and every assertion in review-verdict-test.sh land
# without it; the step that CALLS them does not. Until that call lands, this
# file is red on purpose: the cap still decides by `.commit.committer.date`, so
# a bare re-run against the unchanged blocked head still turns
# `ai-review:blocking` into `ai-review:pass` whenever that head carries a future
# committer date, and the defect is live in the repository. A skip here would
# read as a pass in the summary, which is the failure mode this file was
# rewritten to remove.
#
# The prose is stripped before the check, and that is not fastidiousness: the
# replacement step's own comment quotes the rule it replaced, so a match against
# the whole block would report the fix as the defect.
CODE=$(printf '%s\n' "$CAP" | sed 's/^[[:space:]]*#.*$//')
case "$CODE" in
  *'review-verdict.sh" cap '*) : ;;
  *)
    bad "the cap step still decides by committer date -- the workflow half of #199 is not applied"
    cat <<'HANDOFF'

     .github/workflows/claude-pr-review.yml still carries the rule this file
     exists to refuse. The replacement step is in the pull request that added
     `review-verdict.sh cap`, as a diff in its body; apply it with a credential
     that holds `workflows`. Nothing else in that pull request is waiting on it.
HANDOFF
    printf '\n%d passed, %d failed\n' "$pass" "$fail"
    exit 1 ;;
esac

# Two commits that are not each other, and nothing else about them differs. The
# whole rule is an equality between these.
HEAD_A=abcdef1234567890abcdef1234567890abcdef12
HEAD_B=0fedcba9876543210fedcba9876543210fedcba9

# A ledger already at the ceiling, with one finding still open, so the gate
# answers `run=no` and the code under test is reached at all. `$1` is the
# `head_sha=` line's value: empty writes no such line, which is what every
# ledger written before the field existed looks like.
ledger() {
  {
    printf '%s\n' '<!-- attadipa-review-ledger -->'
    printf '%s\n' '| finding | round | kind | status |'
    printf '%s\n' '<!-- attadipa-review-ledger-state'
    printf 'round=5\n'
    printf 'floor=1\n'
    [ -z "${1:-}" ] || printf 'head_sha=%s\n' "$1"
    printf 'some-finding | 5 | floor | open | The trust state is claimed with no source\n'
    printf -- '-->\n'
  } > "$work/ledger.md"
}

cat > "$work/bin/gh" <<'STUB'
#!/usr/bin/env bash
args="$*"
case "$args" in
  *"/timeline"*)                  [ -n "${T_BLOCKED_AT:-}" ] && echo "$T_BLOCKED_AT"; exit 0 ;;
  *"--json headRefOid"*)          [ -n "${T_HEAD:-}" ] && echo "$T_HEAD"; exit 0 ;;
  # THE READ THAT MUST NOT HAPPEN. `.commit.committer.date` is typed by whoever
  # makes the commit, so the production path is asserted below to never come
  # here at all; the answer is kept only so the mutant at the end of this file
  # can be the old rule rather than a broken one.
  *"/commits/"*)                  echo commit-date >> "$T_CALLS"
                                  [ -n "${T_HEAD_AT:-}" ] && echo "$T_HEAD_AT"; exit 0 ;;
  *"--json labels"*)              [ "${T_BLOCKING:-1}" = 1 ] && echo 'ai-review:blocking'; exit 0 ;;
  *"attadipa-review-cap"*)        exit 0 ;;
  *"issues/comments/9001"*)       cat "$T_LEDGER"; exit 0 ;;
  *"attadipa-review-ledger -->"*) echo 9001; exit 0 ;;
  *"attadipa-review-findings"*)   seq 1 5; exit 0 ;;
  "pr edit"*)                     echo "$args" >> "$T_OUT"; exit 0 ;;
  "pr comment"*)                  echo "commented" >> "$T_OUT"; exit 0 ;;
esac
echo "unrecognised gh call: $args" >&2
exit 9
STUB
chmod +x "$work/bin/gh"

# Runs the real step and reports what it did to the label: `cleared` or `held`.
#
#   verdict LEDGER_HEAD CURRENT_HEAD BLOCKED_AT HEAD_AT BLOCKING
#
# The first two are the identities the rule is about. The next two are the
# timestamps it must ignore -- they are still handed to the stub so that a rule
# reading them can be caught reading them.
verdict() {
  ledger "$1"
  T_HEAD="$2" T_BLOCKED_AT="$3" T_HEAD_AT="$4" T_BLOCKING="$5" \
  T_LEDGER="$work/ledger.md" T_OUT="$work/out.txt" T_CALLS="$work/calls.txt" \
  PATH="$work/bin:$PATH" \
  GH_TOKEN=stub REPO=owner/repo PR=1 TRUSTED="${T_TRUSTED:-$PWD/.github/scripts}" \
  ATTADIPA_LEDGER_ACTOR='github-actions[bot]' ATTADIPA_REVIEW_ACTOR='claude[bot]' \
  GITHUB_OUTPUT="$work/gho.txt" \
    bash "$work/cap.sh" > "$work/log.txt" 2>&1
  if grep -q 'unrecognised gh call' "$work/log.txt"; then echo stub-gap; return; fi
  if grep -q 'add-label ai-review:pass' "$work/out.txt"; then echo cleared; else echo held; fi
}

reset() { : > "$work/out.txt"; : > "$work/gho.txt"; : > "$work/calls.txt"; }

# Timestamps chosen so that each pair on its own would decide the old way, and
# in the opposite direction to the identity beside it.
ANCIENT=2020-01-01T00:00:00Z
BEFORE=2026-09-05T22:30:26Z
AFTER=2026-09-05T22:42:37Z
FUTURE=2099-01-01T00:00:00Z

printf '\n-- the head moved, or it did not --\n'

# The attack, and it is the one the old rule let through: the exact head the
# block was reached on, unchanged, carrying a committer date past the label.
reset
say 'an unchanged head with a future-dated commit holds the block' \
    "$(verdict "$HEAD_A" "$HEAD_A" "$BEFORE" "$FUTURE" 1)" held
say '...and the gate still refused to buy a sixth round' \
    "$(sed -n 's/^run=//p' "$work/gho.txt" | tail -1)" no
say '...and no commit date was read to decide it' \
    "$(wc -l < "$work/calls.txt" | tr -d ' ')" 0

# The mirror, and it is the one that held a real fix for ever.
reset
say 'a new head with a backdated commit clears the block' \
    "$(verdict "$HEAD_A" "$HEAD_B" "$AFTER" "$ANCIENT" 1)" cleared
say '...and no commit date was read to decide that either' \
    "$(wc -l < "$work/calls.txt" | tr -d ' ')" 0

# The two that pin the decision to identity by removing the clock's vote
# entirely: with the timestamps equal the old rule could only ever hold, and
# with them ordered for a clearance it could only ever clear.
reset
say 'equal timestamps and a different head: identity clears it' \
    "$(verdict "$HEAD_A" "$HEAD_B" "$AFTER" "$AFTER" 1)" cleared
reset
say 'a later timestamp and the same head: identity holds it' \
    "$(verdict "$HEAD_A" "$HEAD_A" "$BEFORE" "$AFTER" 1)" held

printf '\n-- an identity that cannot be established --\n'

reset
say 'a ledger written before the head was recorded holds the block' \
    "$(verdict "" "$HEAD_A" "$BEFORE" "$AFTER" 1)" held
reset
say 'a malformed head in the ledger holds the block' \
    "$(verdict "not-a-commit" "$HEAD_A" "$BEFORE" "$AFTER" 1)" held
reset
say 'a truncated head in the ledger holds the block' \
    "$(verdict "abcdef12" "$HEAD_A" "$BEFORE" "$AFTER" 1)" held
reset
say 'a head GitHub would not name holds the block' \
    "$(verdict "$HEAD_A" "" "$BEFORE" "$AFTER" 1)" held
reset
say 'a malformed current head holds the block' \
    "$(verdict "$HEAD_A" "../../etc/passwd" "$BEFORE" "$AFTER" 1)" held

# Case is a spelling of an object id, not a different object id. Reading it as
# one would clear a block on the head it was reached on.
reset
say 'the same head in upper case is the same head, and holds' \
    "$(verdict "$HEAD_A" "ABCDEF1234567890ABCDEF1234567890ABCDEF12" \
               "$BEFORE" "$AFTER" 1)" held

reset
say 'no block on the pull request grants the pass, as before' \
    "$(verdict "$HEAD_A" "$HEAD_A" "" "" 0)" cleared

printf '\n-- what the note tells a person --\n'

# The note is what a human reads, and a held note left standing on a pull
# request that has since been cleared is the same lie in slower form.
reset
verdict "$HEAD_A" "$HEAD_B" "$AFTER" "$AFTER" 1 > /dev/null
say 'the cleared note is flavoured so a held note does not suppress it' \
    "$(grep -c 'attadipa-review-cap:cleared' /tmp/cap-note.md)" 1
reset
verdict "$HEAD_A" "$HEAD_A" "$BEFORE" "$AFTER" 1 > /dev/null
say 'the held note carries the other flavour' \
    "$(grep -c 'attadipa-review-cap:held' /tmp/cap-note.md)" 1
say '...and does not promise a re-run will clear it' \
    "$(grep -c 'Re-running this workflow against the same head will not clear it' /tmp/cap-note.md)" 1
say '...and quotes the reason, which names the head it was reached on' \
    "$(grep -c 'which is still the head being merged' /tmp/cap-note.md)" 1
# The one case a push does NOT fix, said out loud rather than left to a
# maintainer pushing empty commits at a label that will never move.
reset
verdict "" "$HEAD_A" "$BEFORE" "$AFTER" 1 > /dev/null
say 'a ledger with no head says so, so nobody pushes at it in vain' \
    "$(grep -c 'the review ledger records no head' /tmp/cap-note.md)" 1

printf '\n-- the old rule, put back under the real caller --\n'

# THE MUTATION, and it is the decision helper rather than a copy of the step:
# `cap` answers the way `claude-pr-review.yml` used to, from the committer date
# and the label time, and everything above it -- the extraction, the gate, the
# label edit, the note -- is the shipping code unchanged. If the scenarios above
# still agree with this, they are not testing the rule.
cat > "$work/mutant/review-verdict.sh" <<'MUTANT'
#!/usr/bin/env bash
set -uo pipefail
if [ "${1:-}" = cap ]; then
  blocked_at=$(gh api "repos/$REPO/issues/$PR/timeline" --paginate --jq '.[] | select(.event == "labeled") | select(.label.name == "ai-review:blocking") | .created_at' | tail -1) || blocked_at=""
  head_sha=$(gh pr view "$PR" --repo "$REPO" --json headRefOid --jq '.headRefOid') || head_sha=""
  head_at=""
  [ -z "$head_sha" ] || head_at=$(gh api "repos/$REPO/commits/$head_sha" --jq '.commit.committer.date') || head_at=""
  if [ -n "$blocked_at" ] && [ -n "$head_at" ] && [[ "$head_at" > "$blocked_at" ]]; then
    echo "STALE $head_sha $head_sha"
  else
    echo "HOLD the head is no newer than the block"
  fi
  exit 0
fi
exec bash "$ATTADIPA_REAL_VERDICT" "$@"
MUTANT
chmod +x "$work/mutant/review-verdict.sh"
export ATTADIPA_REAL_VERDICT="$PWD/.github/scripts/review-verdict.sh"

T_TRUSTED="$work/mutant"
reset
differs 'the old rule launders an unchanged head with a future-dated commit' \
    "$(verdict "$HEAD_A" "$HEAD_A" "$BEFORE" "$FUTURE" 1)" held
reset
differs 'the old rule holds a new head with a backdated commit' \
    "$(verdict "$HEAD_A" "$HEAD_B" "$AFTER" "$ANCIENT" 1)" cleared
reset
differs 'the old rule cannot see a different head behind equal timestamps' \
    "$(verdict "$HEAD_A" "$HEAD_B" "$AFTER" "$AFTER" 1)" cleared
reset
differs 'the old rule cannot see the same head behind a later timestamp' \
    "$(verdict "$HEAD_A" "$HEAD_A" "$BEFORE" "$AFTER" 1)" held
unset T_TRUSTED

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
