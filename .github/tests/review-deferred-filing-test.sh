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
#
# AND THE LEDGER IS WRITTEN AFTER THE ISSUE (#548). Between `gh issue create`
# returning and the ledger comment being posted, the issue exists and nothing
# records it; the step in between runs under `set -euo pipefail`, so one failed
# API call ends it there, and GitHub's supported re-run of the failed job reads
# the same receiptless ledger and files the follow-up a second time. That window
# cannot be tested by driving one successful round from another -- both of the
# rounds above published -- so the scenario further down makes the ledger write
# FAIL after a successful create, re-runs the same round from the same unchanged
# previous ledger, and counts the creates across both attempts. The recovery it
# exercises, `review-deferred-existing.sh`, is the shipping script, called by the
# extracted step, reading the marker out of the body the round itself rendered.

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
# The mutants below overwrite `step.sh`, so the unmutated one is kept here and
# restored after each. A mutant that leaked into the next scenario would be a
# test passing for the wrong reason, silently.
cp "$work/step.sh" "$work/pristine.sh"

# THE ISSUES COLLECTION THE RECOVERY READ WALKS, and every kind of thing that is
# in a real one:
#
#   #12  an issue opened with no body at all -- `null`, which `contains` refuses
#        outright, so a filter that forgets `// ""` fails the whole read here
#        rather than in production;
#   #99  a PULL REQUEST carrying the marker. `repos/:owner/:repo/issues` returns
#        pull requests as well as issues, and a pull request body is one more
#        place the marker text can be written;
#   #168 the same marker in an issue anybody could have opened. The body is
#        public input: an unbound match would put a stranger's number in the
#        ledger and file nothing;
#   #169 the follow-up for pull request SEVENTY. `:7` is a prefix of `:70` and
#        the trailing ` -->` is what stops it matching -- which is a property of
#        the marker worth a fixture rather than a reading.
#
# $1 is the body of #170, the follow-up for THIS pull request, or empty for a
# repository where it has not been filed yet. The caller passes the body the
# round actually rendered, never one written here: that the search key and the
# posted body are the same string is the thing being tested.
issues_fixture() {
  jq -n --arg filed "$1" '
    [ { number: 12,  user: { login: "github-actions[bot]" }, body: null },
      { number: 99,  user: { login: "github-actions[bot]" },
        pull_request: { url: "https://api.github.com/repos/owner/repo/pulls/99" },
        body: "<!-- attadipa-review-deferred:7 -->\nthe pull request the deferral came from" },
      { number: 168, user: { login: "a-stranger" },
        body: "<!-- attadipa-review-deferred:7 -->\nanybody may open an issue containing this" },
      { number: 169, user: { login: "github-actions[bot]" },
        body: "<!-- attadipa-review-deferred:70 -->\nthe follow-up for pull request seventy" } ]
    + (if $filed == "" then [] else
        [ { number: 170, user: { login: "github-actions[bot]" }, body: $filed } ] end)
  ' > "$work/issues.json"
}
issues_fixture ""

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
  # A round that dies before it publishes must not be readable as the previous
  # round's ledger. Leaving the file behind is how "it failed" would pass for
  # "it posted this".
  rm -f "$work/posted.md"
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
  "api repos/owner/repo/issues?state=all&per_page=100")
    # THE RECOVERY READ, answered by running the step's own \`--jq\` program over
    # the fixture collection rather than by echoing a number. The filter is the
    # part that has to drop a pull request, survive a \`null\` body, refuse a
    # marker anybody could have written and not mistake pull request 70 for 7;
    # a stub that answered '170' would assert none of it.
    [ ! -e "$work/collection-fails" ] || {
      printf 'stub: 502 from the issues collection\n' >&2; exit 1; }
    filter=""; prev=""
    for arg in "\$@"; do [ "\$prev" = --jq ] && filter="\$arg"; prev="\$arg"; done
    [ -n "\$filter" ] || {
      printf 'stub: the recovery read carried no --jq program\n' >&2; exit 4; }
    jq -r "\$filter" "$work/issues.json" ;;
  "api -X")
    # The ledger PATCH, and the half of #548 that can fail after the issue is
    # already filed. Keep the body so the next round can be driven by it.
    [ ! -e "$work/publish-fails" ] || {
      printf 'stub: 502 on the ledger comment\n' >&2; exit 1; }
    for arg in "\$@"; do case "\$arg" in body=@*) cp "\${arg#body=@}" "$work/posted.md" ;; esac; done ;;
  "issue create")
    printf '%s\n' "\$*" >> "$work/created.log"
    printf 'https://github.com/owner/repo/issues/170\n' ;;
  "pr comment")
    [ ! -e "$work/publish-fails" ] || {
      printf 'stub: 502 on the ledger comment\n' >&2; exit 1; }
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

# ---- round 2: the finding is first seen at the floor and is deferred ---------
: > "$work/created.log"
run_round '<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=1
floor=2
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

# ---- round 3: the same finding, still open, driven by round 2's own ledger ---
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

# ---- #548: the create lands, the ledger write does not, and the job re-runs --
#
# The two rounds above are two SUCCESSFUL rounds, and the property they prove is
# that a published ledger stops the second filing. This is the case where no
# ledger was ever published: the create returned, the comment write failed, the
# step died on `set -e`, and the issue exists with nothing recording it. The
# previous ledger handed to the re-run is therefore byte for byte the one the
# failed attempt read -- that is what "the write failed" MEANS -- and the count
# of creates is kept across both attempts, because one each is the defect.
ROUND_AT_FLOOR='<!-- attadipa-review-ledger -->
<!-- attadipa-review-ledger-state
round=1
floor=2
-->'

: > "$work/created.log"
issues_fixture ""
touch "$work/publish-fails"
run_round "$ROUND_AT_FLOOR"
crashed=$?
rm -f "$work/publish-fails"

if [ "$crashed" -ne 0 ]; then
  ok "the attempt whose ledger write fails fails the step"
else
  bad "the attempt whose ledger write fails fails the step -- it exited 0"
fi
say "the issue was created before that failure" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "1"
if [ -e "$work/posted.md" ]; then
  bad "and no ledger was published -- one was"
else
  ok "and no ledger was published"
fi

# The issue is on GitHub now, carrying the body this round rendered, and the
# fixture says so with that body rather than with a marker written by hand.
filed_body=$(cat "$work/tmp/deferred.md")
issues_fixture "$filed_body"
run_round "$ROUND_AT_FLOOR"
rerun=$?
recovered=$(cat "$work/posted.md" 2>/dev/null || printf '')

if [ "$rerun" -eq 0 ]; then
  ok "the re-run of the failed job succeeds"
else
  bad "the re-run of the failed job succeeds -- it exited $rerun: $(cat "$work/out.txt")"
fi
say "and files nothing further: one create across both attempts" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "1"
has "the number it publishes is the one the first attempt created" \
    "$recovered" "deferred_issue=170"
has "and the column says so on the round that recovered it" \
    "$recovered" "no — deferred, filed as #170"
has "the recovery says in the log that it reused rather than filed" \
    "$(cat "$work/out.txt")" "already filed as #170; reusing it"

# ---- a repository that already collected a duplicate converges on the older --
#
# #548 has been able to happen, so two follow-ups for one pull request may
# already be open. Answering with whichever the walk met first would make the
# ledger name a different one on each round, which is a worse ledger than the
# one that named none: the oldest is the one the attempt that crashed created.
: > "$work/created.log"
jq --arg filed "$filed_body" \
   '. + [{ number: 171, user: { login: "github-actions[bot]" }, body: $filed }]' \
   "$work/issues.json" > "$work/issues.dup.json"
mv "$work/issues.dup.json" "$work/issues.json"
run_round "$ROUND_AT_FLOOR"
say "two follow-ups already open file no third" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "0"
has "and the ledger names the older of them" \
    "$(cat "$work/posted.md")" "deferred_issue=170"

# ---- a collection that cannot be read is not an answer of "no issue" ---------
#
# Falling through to the create on a failed read would put the duplicate back on
# exactly the path where it is durable. The step has written nothing at this
# point, so dying is free: the re-run starts from the same state.
: > "$work/created.log"
issues_fixture ""
touch "$work/collection-fails"
run_round "$ROUND_AT_FLOOR"
unreadable=$?
rm -f "$work/collection-fails"

if [ "$unreadable" -ne 0 ]; then
  ok "an unreadable issues collection fails the step"
else
  bad "an unreadable issues collection fails the step -- it exited 0"
fi
say "and nothing is filed while the answer is unknown" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "0"

# ---- the mutant: the caller trusts the empty receipt, as it did before #548 --
: > "$work/created.log"
issues_fixture "$filed_body"
# shellcheck disable=SC2016  # `$(bash "$TRUSTED/...` is the literal text being
# matched in the extracted step, not a command substitution to run here.
sed 's|deferred_issue=$(bash "$TRUSTED/review-deferred-existing.sh".*|deferred_issue=""|' \
  "$work/pristine.sh" > "$work/mutant-recovery.sh"
if cmp -s "$work/pristine.sh" "$work/mutant-recovery.sh"; then
  bad "the recovery call can be removed from the step -- the line was not found"
else
  ok "the recovery call can be removed from the step"
fi
cp "$work/mutant-recovery.sh" "$work/step.sh"
run_round "$ROUND_AT_FLOOR"
say "without it, the re-run files the second issue -- the defect of #548" \
    "$(wc -l < "$work/created.log" | tr -d ' ')" "1"
cp "$work/pristine.sh" "$work/step.sh"

# ---- the mutant: the caller ignores /tmp/deferred.md, as it did before #503 --
: > "$work/created.log"
# Back to a repository where nothing is filed yet, so that what this mutant
# reaches is the create it has had removed and not the recovery above it.
issues_fixture ""
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

# ---- the guard the seam above cannot reach -----------------------------------
#
# `_attadipa_render_deferred` writes the marker as the body's first line, so the
# step can never hand the recovery a body without one -- which is why this is a
# direct call and not another round. The seam proves the caller; it cannot
# produce this input at all. An empty or malformed key would reach `contains`,
# match the first issue in the repository and put a number in the ledger that
# has nothing to do with the finding, so the refusal is worth an assertion of
# its own. The stub is still on PATH, so a script that dropped the guards and
# read the collection with an empty key answers #12 here instead of refusing.
refuses() {
  # $1 what the body is, for the assertion's name. $2 its first line, or the
  # empty string for a file with nothing in it at all.
  local out
  printf '%s' "$2" > "$work/bad-body.md"
  [ -z "$2" ] || printf '\n' >> "$work/bad-body.md"
  if out=$(PATH="$work/bin:$PATH" bash .github/scripts/review-deferred-existing.sh \
             owner/repo "$work/bad-body.md" 'github-actions[bot]' 2>/dev/null); then
    bad "$1 is refused -- it answered '$out'"
  else
    ok "$1 is refused"
  fi
}
refuses "a body whose first line is not a marker" 'this body carries no marker'
# `4242` strips to itself and is all digits: it is refused by the check that the
# marker was stripped at all, and by nothing else.
refuses "a first line that is a bare number" '4242'
refuses "a marker naming no pull request" '<!-- attadipa-review-deferred: -->'
refuses "a marker naming something that is not one" '<!-- attadipa-review-deferred:all -->'
refuses "an empty body file" ''

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
