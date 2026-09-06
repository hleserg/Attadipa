#!/usr/bin/env bash
# The cap gate's decision to clear or hold `ai-review:blocking`, executed.
#
# `review-verdict-test.sh` asserts what `review-verdict.sh gate` answers. This
# file asserts what `claude-pr-review.yml` does with that answer once the five
# rounds are gone -- specifically the question the invalidation step is not
# entitled to ask and this one is: has the head moved since the block was
# applied? #445 merged carrying a block no push could clear, because nothing
# asked it. A guard whose failure mode is a label quietly staying put cannot be
# verified by grep, so the step's shell is extracted from the YAML and run
# against a stub `gh`, the same way review-invalidate-workflow-test.sh runs the
# invalidation.
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
mkdir -p "$work/bin"

CAP=$(extract_run_block "Has this review already had its five rounds" "$WF")
if [ -z "$CAP" ]; then
  bad "the cap step's shell can be extracted and run"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
printf '%s\n' "$CAP" > "$work/cap.sh"
ok "the cap step's shell can be extracted and run"

# A ledger already at the ceiling, with one finding still open, so the gate
# answers `run=no` and the code under test is reached at all.
cat > "$work/ledger.md" <<'LEDGER'
<!-- attadipa-review-ledger -->
| finding | round | kind | status |
<!-- attadipa-review-ledger-state
round=5
floor=1
some-finding | 5 | floor | open | The trust state is claimed with no source
-->
LEDGER

cat > "$work/bin/gh" <<'STUB'
#!/usr/bin/env bash
args="$*"
case "$args" in
  *"/timeline"*)                  [ -n "${T_BLOCKED_AT:-}" ] && echo "$T_BLOCKED_AT"; exit 0 ;;
  *"--json headRefOid"*)          echo deadbeef; exit 0 ;;
  *"commits/deadbeef"*)           [ -n "${T_HEAD_AT:-}" ] && echo "$T_HEAD_AT"; exit 0 ;;
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
verdict() {
  T_BLOCKED_AT="$1" T_HEAD_AT="$2" T_BLOCKING="$3" \
  T_LEDGER="$work/ledger.md" T_OUT="$work/out.txt" \
  PATH="$work/bin:$PATH" \
  GH_TOKEN=stub REPO=owner/repo PR=1 TRUSTED="$PWD/.github/scripts" \
  ATTADIPA_LEDGER_ACTOR='github-actions[bot]' ATTADIPA_REVIEW_ACTOR='claude[bot]' \
  GITHUB_OUTPUT="$work/gho.txt" \
    bash "$work/cap.sh" > "$work/log.txt" 2>&1
  if grep -q 'unrecognised gh call' "$work/log.txt"; then echo stub-gap; return; fi
  if grep -q 'add-label ai-review:pass' "$work/out.txt"; then echo cleared; else echo held; fi
}

reset() { : > "$work/out.txt"; : > "$work/gho.txt"; }

BEFORE=2026-09-05T22:30:26Z
AFTER=2026-09-05T22:42:37Z

reset
say 'a head pushed after the block clears it' \
    "$(verdict "$BEFORE" "$AFTER" 1)" cleared
say '...and the gate still refused to buy a sixth round' \
    "$(sed -n 's/^run=//p' "$work/gho.txt" | tail -1)" no

reset
say 'a bare re-run against the blocked head holds it' \
    "$(verdict "$AFTER" "$BEFORE" 1)" held

reset
say 'a head with the same timestamp as the block holds it' \
    "$(verdict "$AFTER" "$AFTER" 1)" held

reset
say 'a timeline that answers nothing holds it' \
    "$(verdict "" "$AFTER" 1)" held

reset
say 'a head whose commit date is unreadable holds it' \
    "$(verdict "$BEFORE" "" 1)" held

reset
say 'no block on the pull request grants the pass, as before' \
    "$(verdict "" "" 0)" cleared

# The note is what a human reads, and a held note left standing on a pull
# request that has since been cleared is the same lie in slower form.
reset
verdict "$BEFORE" "$AFTER" 1 > /dev/null
say 'the cleared note is flavoured so a held note does not suppress it' \
    "$(grep -c 'attadipa-review-cap:cleared' /tmp/cap-note.md)" 1
reset
verdict "$AFTER" "$BEFORE" 1 > /dev/null
say 'the held note carries the other flavour' \
    "$(grep -c 'attadipa-review-cap:held' /tmp/cap-note.md)" 1
say '...and does not promise a re-run will clear it' \
    "$(grep -c 'Re-running this workflow against the same head will not clear it' /tmp/cap-note.md)" 1

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
