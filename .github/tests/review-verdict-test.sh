#!/usr/bin/env bash
# Does the convergence rule converge, and does it still block what must block?
#
# Offline and deterministic: two files and three values in, `key=value` lines
# out. See the header of .github/scripts/review-verdict.sh for what this is
# guarding against — in one sentence, that `claude-pr-review.yml` reviews the
# head commit, so answering every finding produces a new head and therefore new
# findings, and four branches sat at rounds five, eight, ten and two with the
# label never coming off.
#
# Two properties are asserted harder than the rest, because getting either wrong
# is worse than having no rule at all:
#
#   1. a deferred finding NEVER ages into a blocker. #169's own phrasing —
#      "block on a finding it already raised and the push did not fix" — does not
#      converge, because each round's new prose defect is next round's carry-over.
#   2. a floor finding blocks at ANY round, however late. That is the acceptance
#      in #169 and it is not negotiable by a round number.
# shellcheck disable=SC2016  # The expected strings carry markdown backticks,
# which are exactly what is being asserted; double quotes would run them.

set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
script="$here/../scripts/review-verdict.sh"
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

pass=0; fail=0

# run PREV FINDINGS FLOOR -> stdout, with the ledger left in $work/ledger.md and
# the follow-up body in $work/deferred.md (removed first, so its absence means
# the script chose not to write it).
run() {
  rm -f "$work/ledger.md" "$work/deferred.md"
  bash "$script" "${1:-}" "${2:-}" "${3:-4}" "$work/ledger.md" "$work/deferred.md" 139
}

# key KEY -> the value from the last run's stdout, held in $out
key() { printf '%s\n' "$out" | sed -n "s/^$1=//p"; }

check() {
  local desc="$1" wanted="$2" got="$3"
  if [ "$wanted" = "$got" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s", got "%s"\n' "$desc" "$wanted" "$got"
  fi
}

contains() {
  local desc="$1" needle="$2" hay="$3"
  case "$hay" in
    *"$needle"*) pass=$((pass + 1)); printf '  ok    %s\n' "$desc" ;;
    *) fail=$((fail + 1)); printf '  FAIL  %s\n         wanted a text containing "%s"\n' "$desc" "$needle" ;;
  esac
}

absent() {
  local desc="$1" needle="$2" hay="$3"
  case "$hay" in
    *"$needle"*) fail=$((fail + 1)); printf '  FAIL  %s\n         did not want "%s"\n' "$desc" "$needle" ;;
    *) pass=$((pass + 1)); printf '  ok    %s\n' "$desc" ;;
  esac
}

# findings FILE < heredoc — wraps lines in the block the reviewer writes.
findings() {
  local f="$work/$1"; shift
  { printf '<!-- attadipa-ai-review -->\nSome prose about the diff.\n\n'
    printf '<!-- attadipa-review-findings\n'
    cat
    printf -- '-->\n'
  } > "$f"
  printf '%s' "$f"
}

echo "Round 1 — the open regime, which is the same formula and not a second one"

f=$(findings f1 <<'EOF'
gnss-trust-source | open | floor | The trust state is claimed with no source
adr-section | open | normal | ADR-0007 sec 4.1 should read 2.4
EOF
)
out=$(run "" "$f" 4)
check "round 1 with no previous ledger" 1 "$(key round)"
check "a floor finding and a prose finding both hold, below the floor" ai-review:blocking "$(key label)"
check "and the reason names both clauses" floor-and-pre-floor "$(key reason)"
check "two open" 2 "$(key open)"
check "two holding" 2 "$(key blocking)"
check "nothing deferred below the floor" 0 "$(key deferred)"
check "no follow-up issue is written when nothing is deferred" absent \
      "$([ -f "$work/deferred.md" ] && echo present || echo absent)"
absent "and no follow-up title is printed either" "deferred_title=" "$out"
ledger1=$(cat "$work/ledger.md")
contains "the ledger says which round it is, so a reader can see round nine is round nine" \
         "round 1" "$ledger1"
contains "and prints the floor it judged against" "floor is **round 4**" "$ledger1"

echo
echo "The rounds advance from the ledger, not from a run counter"

cp "$work/ledger.md" "$work/L1.md"
f=$(findings f2 <<'EOF'
gnss-trust-source | fixed | floor | The trust state is claimed with no source
adr-section | open | normal | ADR-0007 sec 4.1 should read 2.4
EOF
)
out=$(run "$work/L1.md" "$f" 4)
check "round 2 follows round 1" 2 "$(key round)"
check "a fixed floor finding stops holding" ai-review:blocking "$(key label)"
check "and the survivor is the pre-floor one alone" pre-floor "$(key reason)"
check "one open" 1 "$(key open)"
cp "$work/ledger.md" "$work/L2.md"

echo
echo "Property 1 — a deferred finding never ages into a blocker"

f=$(findings f4 <<'EOF'
adr-section | fixed | normal | ADR-0007 sec 4.1 should read 2.4
late-typo | open | normal | A stray backtick in the paragraph this push added
EOF
)
out=$(run "$work/L2.md" "$f" 4)
check "round 3" 3 "$(key round)"
check "a NEW prose finding at round 3 still holds — round 3 is below the floor" \
      ai-review:blocking "$(key label)"
cp "$work/ledger.md" "$work/L3.md"

# Same finding, first raised AT the floor instead of below it.
out=$(run "$work/L2.md" "$f" 3)
check "the identical finding at a floor of 3 does not hold" ai-review:pass "$(key label)"
check "it is deferred rather than dropped" 1 "$(key deferred)"
check "and a follow-up body is written for it" present \
      "$([ -f "$work/deferred.md" ] && echo present || echo absent)"
contains "the follow-up names the pull request" "#139" "$(cat "$work/deferred.md")"
contains "and prints a title for the issue" "deferred_title=Deferred review findings from #139" "$out"
cp "$work/ledger.md" "$work/D3.md"

# ...and one more round with it still open. This is the assertion the issue's
# own phrasing would fail: under "carry-over only", a finding raised last round
# and not fixed is a carry-over and blocks.
f=$(findings f5 <<'EOF'
late-typo | open | normal | A stray backtick in the paragraph this push added
EOF
)
out=$(run "$work/D3.md" "$f" 3)
check "a round later it is STILL not holding — this is the whole convergence claim" \
      ai-review:pass "$(key label)"
check "and it is still on the record as open" 1 "$(key open)"
check "still deferred, not quietly dropped" 1 "$(key deferred)"
cp "$work/ledger.md" "$work/D4.md"

out=$(run "$work/D4.md" "$f" 3)
check "and a round after that, unchanged" ai-review:pass "$(key label)"
check "the round still advances while it waits" 5 "$(key round)"

echo
echo "Property 2 — the floor holds at any round, however late"

f=$(findings f6 <<'EOF'
psram-120mhz | open | floor | A 120 MHz PSRAM figure with no datasheet behind it
EOF
)
out=$(run "$work/D4.md" "$f" 3)
check "a floor finding first raised at round 5 holds" ai-review:blocking "$(key label)"
check "and the reason is the floor, not the round" floor "$(key reason)"
cp "$work/ledger.md" "$work/F5.md"

f=$(findings f7 <<'EOF'
psram-120mhz | fixed | floor | A 120 MHz PSRAM figure with no datasheet behind it
EOF
)
out=$(run "$work/F5.md" "$f" 3)
check "fixing it releases the pull request" ai-review:pass "$(key label)"
check "with the deferred one still open and still not holding" 1 "$(key deferred)"

echo
echo "The safe direction, every time there is a choice"

f=$(findings f8 <<'EOF'
mystery | open | banana | A category nobody recognises
EOF
)
out=$(run "" "$f" 1)
check "an unrecognised category is read as floor, not as normal" ai-review:blocking "$(key label)"
check "with floor as the reason" floor "$(key reason)"

f=$(findings f9 <<'EOF'
mystery | resolved | normal | A status nobody recognises
EOF
)
out=$(run "" "$f" 1)
check "an unrecognised status is read as open, not as fixed" 1 "$(key open)"

# A finding in the ledger that this round does not mention at all.
f=$(findings f10 <<'EOF'
adr-section | open | normal | ADR-0007 sec 4.1 should read 2.4
EOF
)
out=$(run "$work/L1.md" "$f" 4)
check "a ledger finding this round did not mention stays open" 2 "$(key open)"
check "and it still holds — silence is not evidence that a push fixed it" \
      ai-review:blocking "$(key label)"

# Category may be upgraded but never downgraded.
f=$(findings f11 <<'EOF'
gnss-trust-source | open | normal | Reclassified downwards by a later round
EOF
)
out=$(run "$work/L1.md" "$f" 4)
check "a floor finding cannot be downgraded to normal by a later round" \
      ai-review:blocking "$(key label)"
contains "and the ledger still records it as floor" \
         '| `gnss-trust-source` | round 1 | floor |' "$(cat "$work/ledger.md")"

f=$(findings f12 <<'EOF'
adr-section | open | floor | Reclassified upwards, which is allowed
EOF
)
out=$(run "$work/L1.md" "$f" 1)
check "but it CAN be upgraded to floor, which is the direction that blocks more" \
      ai-review:blocking "$(key label)"

echo
echo "Dating is the ledger's, not the model's"

# The reviewer cannot re-date a finding into blocking: the ledger says round 5,
# so a floor of 4 leaves it deferred whatever this round's block claims.
cat > "$work/redate.md" <<'EOF'
<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=5
floor=4
late-typo | 5 | normal | open | A stray backtick
-->
EOF
f=$(findings f13 <<'EOF'
late-typo | open | normal | A stray backtick
EOF
)
out=$(run "$work/redate.md" "$f" 4)
check "a finding dated round 5 in the ledger stays deferred at round 6" ai-review:pass "$(key label)"
contains "and the ledger keeps its original round" '| round 5 |' "$(cat "$work/ledger.md")"

# ...and the reverse: a ledger date below the floor keeps blocking even though
# the reviewer is raising it as if for the first time.
cat > "$work/old.md" <<'EOF'
<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=8
floor=4
old-thing | 2 | normal | open | Raised at round 2 and never fixed
-->
EOF
f=$(findings f14 <<'EOF'
old-thing | open | normal | Raised at round 2 and never fixed
EOF
)
out=$(run "$work/old.md" "$f" 4)
check "a round-2 finding still unfixed at round 9 holds" ai-review:blocking "$(key label)"
check "and it is the pre-floor clause that does it" pre-floor "$(key reason)"
check "round 9 counts as round 9" 9 "$(key round)"

echo
echo "A corrupt or absent ledger fails towards blocking, never towards passing"

printf '<!-- attadipa-review-ledger -->\nno state block here at all\n' > "$work/bad1.md"
out=$(run "$work/bad1.md" "$f" 4)
check "a ledger comment with no state block restarts at round 1" 1 "$(key round)"

cat > "$work/bad2.md" <<'EOF'
<!-- attadipa-review-ledger-state
round=not-a-number
floor=4
old-thing | later | normal | open | An unreadable first round
-->
EOF
out=$(run "$work/bad2.md" "$f" 4)
check "an unreadable round restarts at 1 — the open regime, not the converged one" \
      1 "$(key round)"
check "an unreadable first round is read as 1, which is below every floor" \
      ai-review:blocking "$(key label)"

out=$(run "$work/does-not-exist.md" "$f" 4)
check "a missing ledger file is round 1, not an error" 1 "$(key round)"

echo
echo "No findings block is not a verdict"

printf '<!-- attadipa-ai-review -->\nI read the diff and wrote prose and no block.\n' > "$work/noblock.md"
out=$(run "$work/L1.md" "$work/noblock.md" 4)
check "no block prints label=unknown" unknown "$(key label)"
check "and says which of the five reasons that is" no-findings-block "$(key reason)"
check "the round still advances, so the ledger does not stall" 2 "$(key round)"
contains "the ledger says the reviewer's own label stands" \
         "no findings block" "$(cat "$work/ledger.md")"
contains "and carries the earlier findings forward unchanged" \
         '`gnss-trust-source`' "$(cat "$work/ledger.md")"

out=$(run "$work/L1.md" "$work/does-not-exist.md" 4)
check "a missing findings file is also unknown, not a pass" unknown "$(key label)"

# An EMPTY block is a different statement from a MISSING one: it says the
# reviewer looked and had nothing to add this round.
printf '<!-- attadipa-review-findings\n-->\n' > "$work/empty.md"
out=$(run "" "$work/empty.md" 4)
check "an empty block with no history is a pass" ai-review:pass "$(key label)"
check "with nothing holding" nothing-holding "$(key reason)"
out=$(run "$work/L1.md" "$work/empty.md" 4)
check "an empty block does NOT clear findings already on the ledger" \
      ai-review:blocking "$(key label)"

echo
echo "Parsing what a model actually writes"

# CRLF, which is what a comment fetched back from the API can carry.
printf '<!-- attadipa-review-findings\r\nthing | open | normal | With carriage returns\r\n-->\r\n' \
  > "$work/crlf.md"
out=$(run "" "$work/crlf.md" 1)
check "CRLF does not turn 'normal' into an unknown category" ai-review:pass "$(key label)"
check "and the finding is still counted" 1 "$(key open)"

# Whitespace around every field, and an aligned table.
cat > "$work/spaced.md" <<'EOF'
<!-- attadipa-review-findings
  thing      |  open   |  normal  |   Padded out to line up
-->
EOF
out=$(run "" "$work/spaced.md" 1)
check "padding around the fields is trimmed" ai-review:pass "$(key label)"

# A self-closing marker is an EMPTY block, not the start of one that swallows
# the rest of the comment.
cat > "$work/selfclose.md" <<'EOF'
<!-- attadipa-review-findings -->
thing | open | floor | This is prose, outside any block
EOF
out=$(run "" "$work/selfclose.md" 1)
check "a self-closing marker reads as an empty block" 0 "$(key open)"

# Only the first block counts, so a second one pasted below cannot add to it.
cat > "$work/two.md" <<'EOF'
<!-- attadipa-review-findings
first | open | normal | In the first block
-->
<!-- attadipa-review-findings
second | open | floor | In a second block that must be ignored
-->
EOF
out=$(run "" "$work/two.md" 1)
check "a second block is ignored" 1 "$(key open)"
check "and its floor finding does not leak in" ai-review:pass "$(key label)"

# An unterminated block keeps its last line rather than eating it.
printf '<!-- attadipa-review-findings\na | open | normal | one\nb | open | normal | two\n' \
  > "$work/unterminated.md"
out=$(run "" "$work/unterminated.md" 1)
check "an unterminated block does not lose its last finding" 2 "$(key open)"

echo
echo "Ids are slugs, and a title is data"

cat > "$work/ids.md" <<'EOF'
<!-- attadipa-review-findings
# a comment line the reviewer left for itself
Good-Id | open | normal | Capitals are not a slug
has space | open | normal | Nor are spaces
-empty | open | normal | Nor a leading dash
| open | normal | Nor nothing at all
no-pipes-here
fine-id | open | normal | This one is fine
-->
EOF
out=$(run "" "$work/ids.md" 1)
check "only the well-formed id survives" 1 "$(key open)"
check "and the rest are counted as dropped rather than repaired" 5 "$(key dropped)"
contains "the ledger says how many lines it could not read" "unreadable and dropped" \
         "$(cat "$work/ledger.md")"

cat > "$work/title.md" <<'EOF'
<!-- attadipa-review-findings
piped | open | normal | A title with | a pipe | in it
ticked | open | normal | A title with `backticks` in it
EOF
printf -- '-->\n' >> "$work/title.md"
out=$(run "" "$work/title.md" 1)
led=$(cat "$work/ledger.md")
check "a title containing pipes does not add table columns" 2 "$(key open)"
absent "the pipes are gone from the ledger's state block" \
       "A title with | a pipe" "$led"
absent "and backticks are not left to swallow the rest of the row" \
       'A title with `backticks`' "$led"

echo
echo "Output shape"

f=$(findings f20 <<'EOF'
thing | open | normal | Something
EOF
)
out=$(run "" "$f" 4)
bad=$(printf '%s\n' "$out" | grep -cv '^[a-z_]*=' || true)
check "stdout is key=value lines and nothing else" 0 "$bad"
for k in round floor label reason open blocking deferred dropped; do
  n=$(printf '%s\n' "$out" | grep -c "^$k=" || true)
  check "there is exactly one $k= line" 1 "$n"
done
out=$(bash "$script" "" "$f" 4 "" "" 139)
check "the ledger output may be skipped without changing the verdict" \
      ai-review:blocking "$(printf '%s\n' "$out" | sed -n 's/^label=//p')"

echo
echo "The follow-up issue does not enqueue anything"

f=$(findings f21 <<'EOF'
late-one | open | normal | A late prose defect
EOF
)
out=$(run "" "$f" 1)
body=$(cat "$work/deferred.md")
absent "the follow-up body carries no agent:ready" "agent:ready" "$body"
contains "and says so, rather than leaving it to be noticed" \
         "no \`agent:*\` label on purpose" "$body"
contains "it names the floor list so a reader can see what is NOT in it" \
         "did not run on a board" "$body"

echo
echo "The follow-up issue is found from the ledger, not from a search index"

cat > "$work/filed.md" <<'EOF'
<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=6
floor=4
deferred_issue=170
late-typo | 5 | normal | open | A stray backtick
-->
EOF
f=$(findings f22 <<'EOF'
late-typo | open | normal | A stray backtick
EOF
)
out=$(run "$work/filed.md" "$f" 4)
check "the issue number is read back out of the ledger" 170 "$(key deferred_issue)"
contains "and written into the next round's state block" \
         "deferred_issue=170" "$(cat "$work/ledger.md")"
contains "so a reader of the ledger can follow it" "filed as #170" "$(cat "$work/ledger.md")"
out=$(run "" "$f" 4)
absent "with no ledger there is no number to invent" "deferred_issue=" "$out"

cat > "$work/badissue.md" <<'EOF'
<!-- attadipa-review-ledger-state
round=6
floor=4
deferred_issue=../../etc/passwd
late-typo | 5 | normal | open | A stray backtick
-->
EOF
out=$(run "$work/badissue.md" "$f" 4)
absent "a non-numeric issue number is refused rather than carried" "deferred_issue=" "$out"

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
