#!/usr/bin/env bash
# Every branch of the branch-update rule, the path list it names, and the
# transport that turns the two into writes against GitHub.
#
# THE DEFECT THIS IS FOR. A merge to `main` leaves every other open branch
# building against the `main` it forked from, and a semantic conflict between
# the two is green on both sides until it lands. Issue #171.
#
# THE DEFECT THIS TEST IS FOR is the one the fix can introduce, and it is worse
# than the one it fixes: a job with `contents: write` that walks every open pull
# request. The cases below are weighted accordingly -- most of them assert that
# a write does NOT happen. That a null `mergeable` is not an accusation, that a
# fork is not pushed into, that an unanswered comparison is not a zero, that the
# second run over an unchanged repository writes nothing at all, and that the
# update-branch call is made with the credential that starts workflow runs
# rather than the one that does not.
#
# Offline and deterministic. The decision and the path list are argument-and-
# stdin-in, text-out. The transport runs under a stub `gh` on PATH that records
# every call and answers from a fixture directory, so the assertions are about
# the requests the shipping script actually makes -- not about a copy of it.
#
# `sleep` is stubbed to a no-op alongside `gh`. The retry it paces is exercised
# by counting the second read, not by waiting for it.

set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
cd "$here/.." || exit 1

DECIDE=scripts/pr-branch-update-decision.sh
PATHS_JQ=scripts/pr-conflict-paths.jq
SWEEP=scripts/pr-branch-update.sh

pass=0; fail=0
says() {
  local name="$1" got="$2" want="$3"
  if [ "$got" = "$want" ]; then
    printf '  ok    %s\n' "$name"; pass=$((pass + 1))
  else
    printf '  FAIL  %s\n     want: %s\n     got:  %s\n' "$name" "$want" "$got"
    fail=$((fail + 1))
  fi
}
contains() {
  local name="$1" haystack="$2" needle="$3"
  case "$haystack" in
    *"$needle"*) printf '  ok    %s\n' "$name"; pass=$((pass + 1)) ;;
    *) printf '  FAIL  %s\n     wanted to find: %s\n     in: %s\n' "$name" "$needle" "$haystack"
       fail=$((fail + 1)) ;;
  esac
}
lacks() {
  local name="$1" haystack="$2" needle="$3"
  case "$haystack" in
    *"$needle"*) printf '  FAIL  %s\n     did not want to find: %s\n     in: %s\n' "$name" "$needle" "$haystack"
       fail=$((fail + 1)) ;;
    *) printf '  ok    %s\n' "$name"; pass=$((pass + 1)) ;;
  esac
}

# ==========================================================================
# 1. The decision
# ==========================================================================
# decide MERGEABLE BEHIND SAME_REPO LABELS -> "ACTION|REASON"
decide() {
  local out
  out=$(bash "$DECIDE" "$1" "$2" "$3" "$4")
  printf '%s|%s' \
    "$(printf '%s' "$out" | sed -n 1p)" \
    "$(printf '%s' "$out" | sed -n 2p)"
}

echo "The three states of mergeable, and why null does nothing in either direction"
says "behind and mergeable: the case the job exists for" \
     "$(decide true 4 yes "")" "update|behind"
says "conflicted and unlabelled: label it and name the paths" \
     "$(decide false 4 yes "")" "flag|conflicted"
says "null is not false -- every fresh push passes through it" \
     "$(decide null 4 yes "")" "quiet|undetermined"
says "null does not CLEAR a label either, which is the same mistake mirrored" \
     "$(decide null 0 yes needs-rebase)" "quiet|undetermined"
says "an empty mergeable is a failed lookup, not a clean branch" \
     "$(decide "" 4 yes "")" "quiet|undetermined"
says "an error document where mergeable should be is still undetermined" \
     "$(decide '{"message":"Not Found"}' 4 yes "")" "quiet|undetermined"

echo
echo "  behind_by is a count, and a count that did not arrive is not zero"
says "an unanswered comparison does not push" \
     "$(decide true "" yes "")" "quiet|behind-unknown"
says "an unanswered comparison does not clear a label either" \
     "$(decide true "" yes needs-rebase)" "quiet|behind-unknown"
says "the string null is not a number" \
     "$(decide true null yes needs-rebase)" "quiet|behind-unknown"
says "neither is an error document" \
     "$(decide true 'not a number' yes "")" "quiet|behind-unknown"
says "level with the base: nothing to push" \
     "$(decide true 0 yes "")" "quiet|up-to-date"
says "one commit behind is behind" \
     "$(decide true 1 yes "")" "update|behind"
# The idempotence claim in one case: the second run finds behind_by == 0 and
# issues no write, because the first run's merge commit is what makes it 0.
says "the run after an update finds nothing to do" \
     "$(decide true 0 yes "")" "quiet|up-to-date"

echo
echo "  Removing the label is a fact about the branch, not a favour"
says "clean and level again: the label comes off" \
     "$(decide true 0 yes needs-rebase)" "clear|up-to-date"
says "clean but behind: the label comes off after the update succeeds, not here" \
     "$(decide true 3 yes needs-rebase)" "update|behind"
says "still conflicted: the label is already on, so say nothing again" \
     "$(decide false 3 yes needs-rebase)" "quiet|still-conflicted"
says "conflicted beside other labels is still caught" \
     "$(decide false 3 yes "agent:review,needs-rebase,ci:failed")" "quiet|still-conflicted"

echo
echo "  Label matching is exact at both ends, never a substring"
says "needs-rebase-soon is not needs-rebase" \
     "$(decide false 3 yes needs-rebase-soon)" "flag|conflicted"
says "nor is pre-needs-rebase" \
     "$(decide true 0 yes pre-needs-rebase)" "quiet|up-to-date"
says "a label containing a space does not split the list" \
     "$(decide true 0 yes "needs owner,needs-rebase")" "clear|up-to-date"
says "an empty label list is not a match" \
     "$(decide true 0 yes "")" "quiet|up-to-date"

echo
echo "  An update costs the merge queue six hours, so a held branch does not pay it"
# `update-branch` gives the pull request a new head commit, and both of the
# unattended sweep's head-keyed gates reset on it: merge-candidate.sh's
# MIN_HEAD_AGE_SECONDS=21600, and merge-head-trust.jq's binding of
# `ai-review:pass` to a labelling no older than the head. That is accepted for a
# branch on its way in -- it really is a different branch now. It is not
# accepted for one nobody is merging.
says "a parked pull request is not pushed into" \
     "$(decide true 4 yes queue:parked)" "quiet|held"
says "nor is a blocked one" \
     "$(decide true 4 yes agent:blocked)" "quiet|held"
says "the hold suppresses the update and nothing else: a parked conflict is still named" \
     "$(decide false 4 yes queue:parked)" "flag|conflicted"
says "and a parked branch that merges cleanly again still loses its label" \
     "$(decide true 0 yes "queue:parked,needs-rebase")" "clear|up-to-date"
says "queue:parked-later is not queue:parked" \
     "$(decide true 4 yes queue:parked-later)" "update|behind"
says "an ordinary review label is not a hold" \
     "$(decide true 4 yes "agent:review,ai-review:pass")" "update|behind"

echo
echo "  A fork is nobody's branch to push"
says "a fork that is behind is left alone" \
     "$(decide true 9 no "")" "quiet|fork"
says "a fork that conflicts is not labelled either -- the label has no addressee" \
     "$(decide false 9 no "")" "quiet|fork"
says "a fork carrying the label is not cleared by this job" \
     "$(decide true 0 no needs-rebase)" "quiet|fork"
# `head.repo` is null when the fork was deleted, and fetch_facts answers "no".
# Anything that is not the exact word `yes` has to fall on the same side: a
# comparison that could not be made is not permission to push.
says "an unreadable head repository is treated as a fork" \
     "$(decide false 9 "" "")" "quiet|fork"
says "and so is any other answer" \
     "$(decide false 9 maybe "")" "quiet|fork"

# ==========================================================================
# 2. The paths named on a conflict
# ==========================================================================
echo
echo "The conflicting paths, from two compare responses and no checkout"

# files_json A,B,C -> a compare response body carrying those filenames
compare_of() {
  jq -nc --arg names "$1" '{files: ($names | split(",") | map({filename: .}))}'
}
paths_for() {
  jq -nc --argjson pr "$(compare_of "$1")" --argjson base "$(compare_of "$2")" \
    '{pr: $pr, base: $base}' | jq -r -f "$PATHS_JQ"
}

both=$(paths_for "core/a.c,ui/b.c,docs/z.md" "core/a.c,firmware/x.c,docs/z.md")
contains "a path both sides changed is named" "$both" "- \`core/a.c\`"
contains "and so is the second one" "$both" "- \`docs/z.md\`"
lacks "a path only the branch changed is not named" "$both" 'ui/b.c'
lacks "a path only the base changed is not named" "$both" 'firmware/x.c'
contains "the sentence says superset, because that is what it is" "$both" \
         "not necessarily all of them"

none=$(paths_for "ui/b.c" "firmware/x.c")
contains "no common path says so rather than naming nothing" "$none" \
         "changed no path in common"
contains "and points at the rename/delete case it usually is" "$none" \
         "rename or a delete"

# Twenty-one common paths: the list caps and counts the remainder rather than
# pasting a screenful into a pull request, the way compare-summary.jq caps.
many=$(seq -f 'core/f%02g.c' 1 21 | paste -sd, -)
capped=$(paths_for "$many" "$many")
contains "past twenty the list caps" "$capped" "- … and 1 more"
contains "and the first twenty are still named" "$capped" "- \`core/f01.c\`"
lacks "the twenty-first is not" "$capped" 'core/f21.c'

# The compare endpoint returns at most 300 files and does not say that it
# truncated, so a full page is the only signal there is.
full_page=$(seq -f 'core/g%03g.c' 1 300 | paste -sd, -)
truncated=$(paths_for "$full_page" "$full_page")
contains "a full 300-file page is called out as possibly short" "$truncated" \
         "full page of 300 files"
lacks "a 21-file page is not" "$capped" "full page of 300 files"

empty_sides=$(jq -nc '{pr: {}, base: {}}' | jq -r -f "$PATHS_JQ")
contains "a compare response with no files array at all does not crash" \
         "$empty_sides" "changed no path in common"

# ==========================================================================
# 3. The transport
# ==========================================================================
echo
echo "The transport: what the shipping script actually asks GitHub to do"

stub=$(mktemp -d) || exit 1
trap 'rm -rf "$stub"' EXIT

cat > "$stub/gh" <<'STUB'
#!/usr/bin/env bash
# A stub `gh` that answers from $FIX and records every call with the credential
# it was invoked under. The credential is recorded because which token makes the
# update-branch call is the difference between this job working and this job
# replacing "dirty, no CI" with "clean, no CI".
set -uo pipefail
printf '[%s] %s\n' "${GH_TOKEN:-none}" "$*" >> "$FIX/calls.log"

args=("$@"); n=${#args[@]}; i=0
jqexpr=""; method=GET; pos=()
while [ "$i" -lt "$n" ]; do
  case "${args[$i]}" in
    --jq)     i=$((i + 1)); jqexpr="${args[$i]:-}" ;;
    --method) i=$((i + 1)); method="${args[$i]:-}" ;;
    -f|--body-file|--repo|--add-label|--remove-label) i=$((i + 1)) ;;
    --paginate|--slurp) : ;;
    *) pos+=("${args[$i]}") ;;
  esac
  i=$((i + 1))
done
[ "${#pos[@]}" -gt 0 ] || exit 64

emit() {
  [ -f "$1" ] || { printf 'stub: no fixture %s\n' "$1" >&2; return 1; }
  if [ -n "$jqexpr" ]; then jq -r "$jqexpr" "$1"; else cat "$1"; fi
}

case "${pos[0]}" in
  pr) exit "$(cat "$FIX/pr-edit.rc" 2>/dev/null || echo 0)" ;;
  api) : ;;
  *) exit 64 ;;
esac

ep="${pos[1]:-}"
case "$ep" in
  */pulls\?*)
    emit "$FIX/pulls-list.json" ;;
  */pulls/*/update-branch)
    exit "$(cat "$FIX/update.rc" 2>/dev/null || echo 0)" ;;
  */pulls/*)
    num="${ep##*/pulls/}"
    seen=$(( $(cat "$FIX/seen-$num" 2>/dev/null || echo 0) + 1 ))
    printf '%s' "$seen" > "$FIX/seen-$num"
    if [ "$seen" -ge 2 ] && [ -f "$FIX/pull-$num.2.json" ]; then
      emit "$FIX/pull-$num.2.json"
    else
      emit "$FIX/pull-$num.json"
    fi ;;
  */compare/*)
    range="${ep##*/compare/}"
    left="${range%%...*}"; right="${range##*...}"
    if [ "$right" = main ]; then emit "$FIX/basemoved.json"
    else emit "$FIX/compare-$right.json"; fi ;;
  */issues/*/comments*)
    num="${ep#*/issues/}"; num="${num%%/*}"
    emit "$FIX/comments-$num.json" ;;
  */commits/*)
    emit "$FIX/base-commit.json" ;;
  repos/*/*)
    emit "$FIX/repo.json" ;;
  *) exit 64 ;;
esac
STUB
chmod +x "$stub/gh"
# The retry's three seconds are the thing being paced, not the thing being
# tested; the second read is asserted by counting it.
printf '#!/usr/bin/env bash\nexit 0\n' > "$stub/sleep"
chmod +x "$stub/sleep"
PATH="$stub:$PATH"

# scenario NAME -- creates $FIX with the fixtures every run needs
scenario() {
  FIX="$stub/$1"
  mkdir -p "$FIX"
  : > "$FIX/calls.log"
  printf '{"default_branch":"main"}\n' > "$FIX/repo.json"
  printf '{"sha":"basebasebase0000"}\n' > "$FIX/base-commit.json"
  printf '[[]]\n' > "$FIX/comments-1.json"
  printf '[[]]\n' > "$FIX/comments-2.json"
  jq -nc '{files: [{filename: "core/a.c"}]}' > "$FIX/basemoved.json"
  export FIX
}
# pull N MERGEABLE HEAD SAME_REPO LABELS [suffix]
pull() {
  jq -nc --arg m "$2" --arg h "$3" --arg same "$4" --arg labels "$5" \
    '{mergeable: (if $m == "null" then null elif $m == "true" then true else false end),
      head: {sha: $h, repo: {full_name: (if $same == "yes" then "o/r" else "fork/r" end)}},
      base: {repo: {full_name: "o/r"}},
      labels: ($labels | if . == "" then [] else split(",") end | map({name: .}))}' \
    > "$FIX/pull-$1${6:-}.json"
}
# compare HEAD BEHIND [FILES]
compare() {
  # `patch` is present because production always has it: a fixture shaped like
  # the JSON GitHub really returns is the only kind that can catch a size or a
  # field the script mishandles.
  jq -nc --argjson b "$2" --arg names "${3:-core/a.c,ui/b.c}" \
    '{behind_by: $b, merge_base_commit: {sha: "mergebase00000000"},
      files: ($names | split(",")
              | map({filename: ., patch: "@@ -1 +1 @@\n-old\n+new"}))}' \
    > "$FIX/compare-$1.json"
}
# base_moved FILES -- what the base changed since the merge base
base_moved() {
  jq -nc --arg names "$1" '{files: ($names | split(",") | map({filename: .}))}' \
    > "$FIX/basemoved.json"
}
run_sweep() {
  REPO=o/r BASE=main GH_TOKEN=read-cred RUNNER_TEMP="$FIX" \
    UPDATE_TOKEN="${1-update-cred}" bash "$SWEEP" 2>&1
}
calls() { cat "$FIX/calls.log"; }
body_of() { cat "$FIX/needs-rebase-$1.md" 2>/dev/null; }

echo
echo "  A branch behind the base is updated, once, bound to the head it was read at"
scenario behind
printf '[[{"number":1}]]\n' > "$FIX/pulls-list.json"
pull 1 true headaaaa0000 yes ""
compare headaaaa0000 3
out=$(run_sweep)
contains "the update-branch call is made" "$(calls)" \
         "api --method PUT repos/o/r/pulls/1/update-branch"
contains "and it is bound to the head every fact was read at" "$(calls)" \
         "expected_head_sha=headaaaa0000"
contains "with the credential that starts workflow runs" "$(calls)" \
         "[update-cred] api --method PUT"
lacks "and never with the read credential" "$(calls)" \
      "[read-cred] api --method PUT"
contains "the list is filtered to the base by the API, not by us" "$(calls)" \
         "base=main"
lacks "an up-to-date branch is not labelled" "$(calls)" "add-label"
contains "the run says what it did" "$out" "1 updated"

echo
echo "  ...and the run after it writes nothing at all, which is the idempotence claim"
scenario idempotent
printf '[[{"number":1}]]\n' > "$FIX/pulls-list.json"
pull 1 true headaaaa0000 yes ""
compare headaaaa0000 0
out=$(run_sweep)
lacks "no update-branch call" "$(calls)" "update-branch"
lacks "no label added" "$(calls)" "add-label"
lacks "no label removed" "$(calls)" "remove-label"
lacks "no comment" "$(calls)" "pr comment"
contains "and it says so" "$out" "0 updated, 0 flagged, 0 cleared"

echo
echo "  Without a credential that starts workflow runs, nothing is pushed"
# Replacing "dirty, no CI" with "clean, no CI" is the failure issue #171 named,
# and it is the one that looks fine. The label half still runs, because a label
# does not need a build.
scenario notoken
printf '[[{"number":1},{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 1 true headaaaa0000 yes ""
compare headaaaa0000 3
pull 2 false headbbbb0000 yes ""
compare headbbbb0000 2
out=$(run_sweep "")
lacks "the branch behind the base is NOT pushed" "$(calls)" "update-branch"
contains "and the log says why, rather than going quiet" "$out" \
         "does not start a workflow run"
contains "the conflicted one is still labelled" "$(calls)" \
         "pr edit 2 --repo o/r --add-label needs-rebase"

echo
echo "  A conflicted branch is labelled and told which paths to look at"
scenario conflicted
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 false headbbbb0000 yes ""
compare headbbbb0000 2
out=$(run_sweep)
contains "the label goes on" "$(calls)" "pr edit 2 --repo o/r --add-label needs-rebase"
contains "a comment is posted" "$(calls)" "pr comment 2 --repo o/r --body-file"
lacks "and update-branch is never called on a branch already known to conflict" \
      "$(calls)" "update-branch"
body=$(body_of 2)
contains "the comment names the path both sides changed" "$body" "- \`core/a.c\`"
lacks "and not the one only the branch changed" "$body" 'ui/b.c'
contains "it carries a marker keyed to the head commit" "$body" \
         "<!-- attadipa-needs-rebase:headbbbb0000 -->"
contains "and says how the label comes off" "$body" "comes off by itself"
contains "and does not promise a repeat it will not deliver" "$body" \
         "said once per conflict"

echo
echo "  ...and it is not said twice for the same head commit"
scenario already
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 false headbbbb0000 yes needs-rebase
compare headbbbb0000 2
jq -nc '[[{"body": "<!-- attadipa-needs-rebase:headbbbb0000 -->\nsaid already"}]]' \
  > "$FIX/comments-2.json"
out=$(run_sweep)
lacks "no second comment" "$(calls)" "pr comment"
lacks "no second label" "$(calls)" "add-label"
contains "the decision recognises its own label" "$out" "still-conflicted"

echo
echo "  ...and a label somebody removed by hand does not buy a second comment"
# The label and the comment are two guards, not one. Somebody clearing the
# label to see what happens must get the label back without the pull request
# being told the same thing twice for the same commit.
scenario relabelled
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 false headbbbb0000 yes ""
compare headbbbb0000 2
jq -nc '[[{"body": "<!-- attadipa-needs-rebase:headbbbb0000 -->\nsaid already"}]]' \
  > "$FIX/comments-2.json"
out=$(run_sweep)
contains "the label goes back on" "$(calls)" "add-label needs-rebase"
lacks "and nothing is said again for this head" "$(calls)" "pr comment"

echo
echo "  A conflict that has been fixed by hand loses its label"
scenario cleared
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 true headbbbb0000 yes needs-rebase
compare headbbbb0000 0
out=$(run_sweep)
contains "the label comes off" "$(calls)" \
         "pr edit 2 --repo o/r --remove-label needs-rebase"
lacks "and nothing is pushed to do it" "$(calls)" "update-branch"
contains "counted as cleared" "$out" "0 flagged, 1 cleared"

echo
echo "  A successful update on a labelled branch is itself the proof it merges"
scenario updated_and_cleared
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 true headbbbb0000 yes needs-rebase
compare headbbbb0000 5
out=$(run_sweep)
contains "the update goes out" "$(calls)" "update-branch"
contains "and the label comes off after it, not before" "$(calls)" \
         "remove-label needs-rebase"
contains "counted both ways" "$out" "1 updated, 0 flagged, 1 cleared"

echo
echo "  A refused update is re-read rather than parsed out of GitHub's prose"
# 422 covers both "merge conflict" and "expected head sha did not match" and the
# only thing separating them is wording that is not part of any contract.
scenario refused_conflict
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 true headbbbb0000 yes ""
pull 2 false headcccc0000 yes "" .2
compare headbbbb0000 5 "core/a.c,ui/b.c"
compare headcccc0000 5 "firmware/late.c"
base_moved "core/a.c,firmware/late.c"
printf '1\n' > "$FIX/update.rc"
out=$(run_sweep)
contains "a refusal that turns out to be a conflict is labelled" "$(calls)" \
         "add-label needs-rebase"
contains "and the comment is keyed to the head the RE-READ found" \
         "$(body_of 2)" "<!-- attadipa-needs-rebase:headcccc0000 -->"
contains "with paths read for that head, not for the one that moved" \
         "$(body_of 2)" "- \`firmware/late.c\`"
lacks "the superseded head's paths do not appear" "$(body_of 2)" 'core/a.c'

scenario refused_stale
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 true headbbbb0000 yes ""
pull 2 true headcccc0000 yes "" .2
compare headbbbb0000 5
printf '1\n' > "$FIX/update.rc"
out=$(run_sweep)
lacks "a refusal that is only a moved head does not become an accusation" \
      "$(calls)" "add-label"
lacks "and posts nothing" "$(calls)" "pr comment"
contains "it says it will come back" "$out" "leaving it for the next run"

echo
echo "  Undetermined mergeability is read again, and then let alone"
scenario undetermined
printf '[[{"number":1}]]\n' > "$FIX/pulls-list.json"
pull 1 null headaaaa0000 yes ""
compare headaaaa0000 3
out=$(run_sweep)
says "the pull request is read twice, not once" "$(cat "$FIX/seen-1")" "2"
lacks "and after the retry still says nothing" "$(calls)" "add-label"
lacks "and pushes nothing" "$(calls)" "update-branch"
contains "reported as undetermined" "$out" "undetermined"

echo
echo "  A fork is enumerated and then left alone"
scenario fork
printf '[[{"number":3}]]\n' > "$FIX/pulls-list.json"
pull 3 false headdddd0000 no ""
compare headdddd0000 7
printf '[[]]\n' > "$FIX/comments-3.json"
out=$(run_sweep)
lacks "not pushed into" "$(calls)" "update-branch"
lacks "not labelled" "$(calls)" "add-label"
contains "and said once, in the log" "$out" "fork"

echo
echo "  The write bound holds, and the run says when it bit"
scenario capped
jq -nc '[[range(1;13) | {number: .}]]' > "$FIX/pulls-list.json"
for i in $(seq 1 12); do
  pull "$i" true "head0000000$i" yes ""
  compare "head0000000$i" 2
  printf '[[]]\n' > "$FIX/comments-$i.json"
done
out=$(run_sweep)
says "ten updates, not twelve" "$(grep -c 'update-branch' "$FIX/calls.log")" "10"
contains "and the eleventh is told to wait rather than dropped silently" "$out" \
         "reached the cap of 10 updates"
contains "the summary counts what it did" "$out" "10 updated"

echo
echo "  A parked branch is enumerated, left unpushed, and still told it conflicts"
scenario parked
printf '[[{"number":1},{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 1 true headaaaa0000 yes queue:parked
compare headaaaa0000 4
pull 2 false headbbbb0000 yes queue:parked
compare headbbbb0000 4
out=$(run_sweep)
lacks "no push into a branch nobody is merging" "$(calls)" "update-branch"
contains "the conflict half still runs on it" "$(calls)" \
         "pr edit 2 --repo o/r --add-label needs-rebase"
contains "and the log says which of the two it was" "$out" "held"

echo
echo "  A real-sized compare body still names paths"
# A compare response carries a `patch` per file, so its body scales with the
# diff. Passing either body through argv fails at MAX_ARG_STRLEN -- PAGE_SIZE *
# 32, 128 KiB -- with E2BIG, and the paths would stop being named at exactly the
# size where a conflict gets interesting. Worse, the failure would arrive as
# "one of the comparisons did not answer", which would be false: both answered.
# So the fixtures here are production-shaped, and this one is over the limit.
scenario bigcompare
printf '[[{"number":2}]]\n' > "$FIX/pulls-list.json"
pull 2 false headbbbb0000 yes ""
jq -nc --arg blob "$(head -c 120000 /dev/zero | tr '\0' 'x')" \
  '{behind_by: 3, merge_base_commit: {sha: "mergebase00000000"},
    files: [{filename: "core/a.c", patch: $blob},
            {filename: "ui/b.c", patch: $blob}]}' > "$FIX/compare-headbbbb0000.json"
jq -nc --arg blob "$(head -c 120000 /dev/zero | tr '\0' 'x')" \
  '{files: [{filename: "core/a.c", patch: $blob},
            {filename: "firmware/x.c", patch: $blob}]}' > "$FIX/basemoved.json"
says "the fixture really is past the argv limit" \
     "$(if [ "$(wc -c < "$FIX/basemoved.json")" -gt 131072 ]; then echo yes; else echo no; fi)" \
     "yes"
out=$(run_sweep)
contains "the path is still named" "$(body_of 2)" "- \`core/a.c\`"
lacks "and the comment does not blame a comparison that answered" "$(body_of 2)" \
      "did not answer"

echo
echo "  An empty queue and an unreadable one are different answers"
scenario empty
printf '[[]]\n' > "$FIX/pulls-list.json"
out=$(run_sweep)
contains "nothing open is a notice, not a failure" "$out" "no open pull requests"
lacks "and no write" "$(calls)" "pr edit"

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
