#!/usr/bin/env bash
# Do the `gh api` calls in the workflows use combinations `gh` actually accepts?
#
# This exists because of one line that shipped, went green in every check the
# repository has, and then failed on its first real run:
#
#   gh api --paginate --slurp "repos/$REPO/pulls?..." --jq 'map(.number)|.[]'
#   -> the `--slurp` option is not supported with `--jq` or `--template`
#
# `--slurp` collects the pages into one outer array and hands the result to the
# caller; `--jq` and `--template` are output formatters `gh` applies itself.
# `gh` refuses to do both and exits 1 before making a request. Nothing in the
# repository could catch that: shellcheck sees a well-formed command, actionlint
# sees valid YAML, and the workflow only runs on a schedule. In pr-merge-sweep
# it appeared three times -- once fatal, twice swallowed by `|| VAR=""`, which
# turned every candidate into "could not read its comments, leaving it alone".
# A guard that reads the workflow text is the only thing that would have.
#
# The rule this enforces: pipe into a separate `jq` instead. That is what the
# watchdog already did, and it is why the watchdog was unaffected.
#
# IT ALSO READS THE PARKED PATCHES, and that is the second reason it exists.
# An agent token cannot write under `.github/workflows/`, so workflow changes
# land as reviewed patches under `docs/automation/pending/` and are applied by
# hand later. #128 parked 516 lines of workflow shell that way. Three guards
# existed and none of them reached that directory: actionlint globs
# `.github/workflows/`, shellcheck globs `.github/scripts` and `.github/tests`,
# and this file's own `find` did the same as actionlint. So a parked patch
# could reintroduce the exact `--slurp`/`--jq` pair above and every check in
# the repository would stay green until somebody applied it. Found in review of
# #128 and filed as #179.
#
# Two things are checked about a parked patch, because both failure modes are
# silent. First the shell it would deploy, read as its POST-IMAGE -- context
# plus added lines -- because every `--slurp` call in this repository puts
# `gh api` and its flags on different lines, and a patch that edits only the
# flag line has no `gh api` among its added lines at all. Removing a bad call
# is still the opposite of an offence: take `--jq` out and the post-image no
# longer holds one. Second that the patch still applies: it is pinned to
# context in files this work edits constantly, `pending/README.md` tells a
# human to check, and nothing checked. `git apply --check` writes nothing and
# needs no repository -- so that half is a WARNING, not a failure, for the
# reason set out at 1c below.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
root=$(cd "$here/../.." && pwd) || exit 1

pass=0; fail=0

ok()   { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad()  { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }

# Join shell line continuations so one `gh api` invocation is one record,
# strip comments, then look for the forbidden pair inside a single invocation.
# `awk` rather than `grep` because the flags are routinely on different lines.
offenders() {
  awk '
    { sub(/[[:space:]]*#.*$/, "") }
    { line = line $0 }
    /\\[[:space:]]*$/ { sub(/\\[[:space:]]*$/, " ", line); next }
    {
      if (line ~ /gh api/ && line ~ /--slurp/ && (line ~ /--jq/ || line ~ /--template/))
        printf "%s:%d\n", FILENAME, FNR
      line = ""
    }
  ' "$@"
}

# The POST-IMAGE of a parked patch -- context lines plus added lines, with the
# leading marker stripped -- restricted to hunks whose target is workflow shell,
# and labelled with each line's position IN THE PATCH FILE.
#
# Post-image and not added-lines-only, which is what this read until review of
# #180. Added-only looked right ("a patch that REMOVES a bad call is the fix,
# not the defect") and was blind to the shape every `--slurp` call in this
# repository actually uses: `gh api` on one line and `--slurp` on the next,
# joined by a backslash. Edit only the flag line and the added-lines record
# holds no `gh api` at all, so the scan says nothing while the patch deploys
# exactly the pair `gh` rejects. The post-image keeps the removal property
# anyway -- take out `--jq` and the post-image no longer has one.
#
# Restricted by `+++` target because a patch is not all shell. This repository
# documents the `--slurp`/`--jq` rule constantly, in prose with no `#` to strip,
# and a parked patch that edits a README explaining the rule would otherwise be
# reported for breaking it.
patch_postimage() {
  awk '
    /^\+\+\+ /{
      target = $2
      sub(/^b\//, "", target)
      want = (target ~ /^\.github\/(workflows|scripts|tests)\//)
      next
    }
    /^--- /  { next }
    /^@@/    { next }
    !want    { next }
    /^[+ ]/  { text = $0; sub(/^./, "", text); printf "%d\t%s\n", FNR, text }
  ' "$1"
}

# The same rule as `offenders`, over a parked patch's post-image, reporting the
# line number in the PATCH -- not the nth added line, which is what this printed
# until review of #180 and which drifts further from the truth the longer the
# patch is.
patch_offenders() {
  local patch=$1
  patch_postimage "$patch" | awk -v patch="$patch" '
    {
      n = $0; sub(/\t.*$/, "", n)
      t = $0; sub(/^[0-9]+\t/, "", t)
      sub(/[[:space:]]*#.*$/, "", t)
      if (line == "") first = n
      line = line t
      if (t ~ /\\[[:space:]]*$/) { sub(/\\[[:space:]]*$/, " ", line); next }
      if (line ~ /gh api/ && line ~ /--slurp/ && (line ~ /--jq/ || line ~ /--template/))
        printf "%s:%d\n", patch, first
      line = ""
    }
  '
}

# Which files `git apply` named in its refusal, deduplicated onto one line.
# This only enriches a message and never decides anything, so a `git` that
# words its errors differently costs a file name rather than a verdict.
refused_files() {
  sed -n 's/^error: \(.*\): patch does not apply$/\1/p;
          s/^error: \(.*\): No such file or directory$/\1/p' \
    | awk '!seen[$0]++' | tr '\n' ' ' | sed 's/ $//'
}

# Every parked patch in DIR that no longer applies to TREE, one per line as
#
#   <state><TAB><patch><TAB><the files git refused>
#
# where <state> is `stale-workflow` or `drifted`. Silent when they all apply.
#
# The two states exist because a parked patch has two halves that rot for
# different reasons, and only one of them belongs to whoever is standing there
# when it breaks -- see 1c.
patch_apply_states() {
  local dir=$1 tree=$2 patch err
  [ -d "$dir" ] || return 0
  for patch in "$dir"/*.patch; do
    [ -e "$patch" ] || continue
    if ! err=$(git -C "$tree" apply --check --include='.github/*' -- "$patch" 2>&1); then
      printf 'stale-workflow\t%s\t%s\n' "$patch" "$(printf '%s\n' "$err" | refused_files)"
    elif ! err=$(git -C "$tree" apply --check -- "$patch" 2>&1); then
      printf 'drifted\t%s\t%s\n' "$patch" "$(printf '%s\n' "$err" | refused_files)"
    fi
  done
}

files=$(find "$root/.github/workflows" -name '*.yml' -o -name '*.yaml' | sort) || exit 1
[ -n "$files" ] || { printf 'FAIL no workflow files found\n'; exit 1; }

# Overridable so the fixtures below exercise the shipping code rather than
# re-testing git. Review of #180 found every branch of the real loops untested:
# inverting a condition in them left all cases green.
PENDING_DIR=${PENDING_DIR:-$root/docs/automation/pending}

# 1. No workflow may combine them.
# shellcheck disable=SC2086  # the paths are ours and contain no spaces
found=$(offenders $files)
if [ -z "$found" ]; then
  ok "no workflow passes --slurp together with --jq or --template"
else
  bad "gh rejects --slurp with --jq/--template; these invocations would exit 1 before any request:"
  printf '%s\n' "$found" | sed 's/^/       /'
fi

# 1b. The same rule, over the workflow shell that has not been applied yet.
#     FATAL, unlike 1c below, because this only fires when somebody wrote the
#     bad call into a patch. It does not drift on its own.
check_parked_shell() {
  local dir=$1 patch found all=""
  for patch in "$dir"/*.patch; do
    [ -e "$patch" ] || continue
    if ! found=$(patch_offenders "$patch"); then
      bad "could not scan $patch -- treating an unreadable patch as clean is the \
|| VAR=\"\" shape this file exists to refuse"
      return
    fi
    [ -n "$found" ] && all="$all$found"$'\n'
  done
  if [ -z "$all" ]; then
    ok "no parked patch would deploy --slurp together with --jq or --template"
  else
    bad "a parked patch would deploy an invocation gh rejects:"
    printf '%s' "$all" | sed 's/^/       /'
  fi
}

# 1c. And whether each parked patch still applies. THE SEVERITY IS SPLIT BY
#     HALF, and this is the interesting decision in the file.
#
#     Failing on any drift stops the queue. A parked patch goes stale because of
#     work somewhere else -- often work CI itself demands: `check_docs.py`
#     enforces the fingerprint on `WAVESHARE_ARRIVAL.md`'s citation of
#     `ci.yml:499`, so inserting a line in `ci.yml` above it forces an edit that
#     moves the very context the parked patch pins. Fail the build on that and
#     the red lands on `main` and, on their next run, on every open pull request
#     at once, none of which touched the patch -- and CLAUDE.md has both the
#     orchestrator and the merge sweep gated on green. One stale patch would
#     stop the whole queue, and the only way out would be rebuilding a 516-line
#     patch against files an agent token cannot write. Found in review of #180,
#     which shipped it fatal, and reproduced end to end before this was written.
#
#     Warning on any drift removes the teeth #179 asked for. But the two are not
#     the only options, because the blast radius is not a property of the check
#     -- it is a property of WHICH HALF of the patch moved:
#
#       * the hunks under `.github/` can only be moved by an owner edit or by
#         another patch landing, since no agent token can write those files.
#         Small audience, and a stale workflow hunk means the parked change
#         itself is now wrong. FATAL.
#       * everything else a patch carries -- the docs edits its own landing
#         forces, per `pending/README.md` -- moves under ordinary work, by
#         people who did not choose to and cannot rebuild a workflow patch.
#         WARNING: a `::warning::` annotation, a job-summary line, and a named
#         remedy.
#
#     `git apply --check --include='.github/*'` separates them, needs no history
#     and no `fetch-depth`, and works in the shallow checkout `ci.yml` makes.
check_parked_applies() {
  local dir=$1 tree=$2 states stale drifted patch refused
  states=$(patch_apply_states "$dir" "$tree")
  if [ ! -d "$dir" ] || [ -z "$(find "$dir" -name '*.patch' 2>/dev/null)" ]; then
    ok "no patches parked under ${dir#"$root/"}"
    return
  fi
  stale=$(printf '%s\n' "$states" | grep '^stale-workflow')
  drifted=$(printf '%s\n' "$states" | grep '^drifted')
  if [ -n "$stale" ]; then
    bad "a parked patch no longer applies under .github/ -- the workflow half it would land has moved:"
    printf '%s\n' "$stale" \
      | awk -F'\t' '{ printf "       %s\n         git refused: %s\n", $2, $3 }'
    printf '       already landed? git rm it -- pending/README.md says a patch is deleted in\n'
    printf '       the commit that applies it. Not landed? rebuild it against the current\n'
    printf '       tree; do not hand-edit the hunk headers.\n'
  elif [ -z "$drifted" ]; then
    ok "every parked patch still applies to the tree"
    return
  else
    ok "every parked patch still applies under .github/, the half nobody here can rebuild"
  fi
  [ -n "$drifted" ] || return
  ok "$(printf '%s\n' "$drifted" | wc -l | tr -d ' ') no longer apply in full (warning, not a failure)"
  printf '%s\n' "$drifted" | while IFS=$'\t' read -r _ patch refused; do
    [ -n "$patch" ] || continue
    printf '::warning file=%s::this parked patch still applies under .github/ but not in full: %s moved under it. If it has already been landed, git rm it -- pending/README.md says a patch is deleted in the commit that applies it. If it has not, update that hunk, or land it with git apply -3 from a full clone.\n' \
      "$patch" "${refused:-something it also edits}"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
      # shellcheck disable=SC2016  # the backticks are Markdown code spans in
      # the job summary, not command substitution; single quotes are what keeps
      # them literal.
      printf -- '- **drifted parked patch**: `%s` still applies under `.github/`, but `%s` moved under it. Landed already? `git rm` it. Not landed? Update that hunk, or `git apply -3`.\n' \
        "$patch" "${refused:-something it also edits}" >> "$GITHUB_STEP_SUMMARY"
    fi
  done
}

check_parked_shell "$PENDING_DIR"
check_parked_applies "$PENDING_DIR" "$root"

# 2. The detector itself detects. A guard that cannot fail guards nothing, and
#    this one is a text scan over files it does not control, so it is worth
#    proving on a fixture rather than trusting.
probe=$(mktemp -d) || exit 1
trap 'rm -rf "$probe"' EXIT

cat > "$probe/bad-oneline.yml" <<'FIXTURE'
run: gh api --paginate --slurp "repos/x/y/pulls" --jq '.[]'
FIXTURE
cat > "$probe/bad-continued.yml" <<'FIXTURE'
run: |
  gh api --paginate --slurp \
    "repos/x/y/pulls" \
    --jq 'map(.number) | .[]'
FIXTURE
cat > "$probe/bad-template.yml" <<'FIXTURE'
run: gh api --slurp "repos/x/y/pulls" --template '{{.}}'
FIXTURE
cat > "$probe/good-piped.yml" <<'FIXTURE'
run: |
  gh api "repos/x/y/pulls" --paginate --slurp \
    | jq 'if (length > 0 and (.[0] | type) == "array") then add else . end'
FIXTURE
cat > "$probe/good-jq-alone.yml" <<'FIXTURE'
run: gh api "repos/x/y/pulls" --paginate --jq '.[].number'
FIXTURE
cat > "$probe/good-two-calls.yml" <<'FIXTURE'
run: |
  gh api "repos/x/y/a" --paginate --slurp | jq '.'
  gh api "repos/x/y/b" --jq '.number'
FIXTURE
cat > "$probe/good-commented.yml" <<'FIXTURE'
run: |
  # never write: gh api --slurp --jq '.'
  gh api "repos/x/y/pulls" --slurp | jq '.'
FIXTURE

for f in bad-oneline bad-continued bad-template; do
  if [ -n "$(offenders "$probe/$f.yml")" ]; then
    ok "the scan catches $f"
  else
    bad "the scan misses $f, so it would not have caught the real one"
  fi
done
for f in good-piped good-jq-alone good-two-calls good-commented; do
  if [ -z "$(offenders "$probe/$f.yml")" ]; then
    ok "the scan leaves $f alone"
  else
    bad "the scan flags $f, which is the shape it is telling people to use"
  fi
done

# 3. The patch scan detects, in both directions, and over the shape this
#    repository actually writes. The removal case is the one worth having: a
#    scan that flagged a patch for DELETING a bad call would fire on the fix and
#    teach the next person to route around the guard. The multi-line case is the
#    one that was missing -- `agent-queue-watchdog.yml` has `gh api` on one line
#    and `--slurp` on the next, and so does every other `--slurp` call here.
cat > "$probe/adds-bad.patch" <<'FIXTURE'
--- a/.github/workflows/x.yml
+++ b/.github/workflows/x.yml
@@ -1,2 +1,3 @@
 jobs:
+          gh api --paginate --slurp "repos/x/y/pulls" --jq '.[]'
 steps:
FIXTURE
cat > "$probe/adds-bad-continued.patch" <<'FIXTURE'
--- a/.github/workflows/x.yml
+++ b/.github/workflows/x.yml
@@ -1,3 +1,3 @@
             if TIMELINE=$(gh api "repos/$REPO/issues/1/timeline" \
-                            --paginate --slurp 2>/tmp/e); then
+                            --paginate --slurp --jq '.[]' 2>/tmp/e); then
             fi
FIXTURE
cat > "$probe/removes-bad.patch" <<'FIXTURE'
--- a/.github/workflows/x.yml
+++ b/.github/workflows/x.yml
@@ -1,3 +1,3 @@
 jobs:
-          gh api --paginate --slurp "repos/x/y/pulls" --jq '.[]'
+          gh api --paginate --slurp "repos/x/y/pulls" | jq '.[]'
 steps:
FIXTURE
cat > "$probe/adds-good.patch" <<'FIXTURE'
--- a/.github/workflows/x.yml
+++ b/.github/workflows/x.yml
@@ -1,2 +1,3 @@
 jobs:
+          gh api "repos/x/y/pulls" --paginate --slurp | jq '.'
 steps:
FIXTURE
cat > "$probe/documents-the-rule.patch" <<'FIXTURE'
--- a/docs/automation/pending/README.md
+++ b/docs/automation/pending/README.md
@@ -1,2 +1,3 @@
 # Parked
+Never write `gh api --paginate --slurp "repos/x/y/pulls" --jq '.[]'`: gh refuses it.
 ## Waiting
FIXTURE

for f in adds-bad adds-bad-continued; do
  if [ -n "$(patch_offenders "$probe/$f.patch")" ]; then
    ok "the patch scan catches $f"
  else
    bad "the patch scan misses $f -- a parked patch would deploy a call gh rejects"
  fi
done
for f in removes-bad adds-good documents-the-rule; do
  if [ -z "$(patch_offenders "$probe/$f.patch")" ]; then
    ok "the patch scan leaves $f alone"
  else
    bad "the patch scan flags $f, which is not an offence"
  fi
done

# 3b. The reported line number is a line in the PATCH FILE, and specifically the
#     line the offending invocation STARTS on. That is the number an operator
#     can act on: `sed -n '4p' the-patch` shows them `gh api ...`, the head of
#     the call the rule is about. Reporting the flag's own line instead would
#     open at a bare continuation -- `--paginate --slurp --jq ...` with no verb
#     -- and reporting the nth added line, which is what this printed until
#     review of #180, opens at whatever text happens to sit at that offset. For
#     a single-line call all three coincide, which is why the continued fixture
#     is the one that can tell them apart.
want=$(grep -n -- "gh api" "$probe/adds-bad-continued.patch" | cut -d: -f1)
got=$(patch_offenders "$probe/adds-bad-continued.patch" | sed 's/.*://')
if [ "$got" = "$want" ]; then
  ok "the report opens the continued patch at the call ($got)"
else
  bad "the report says line $got; the call starts at $want -- an operator opening it finds a bare continuation"
fi
# The number is a real line of the patch and not a count of added lines: this
# fixture has exactly one added line, so the old nth-added report would say 1.
if [ "$got" != "1" ]; then
  ok "the report is a patch line, not an added-line ordinal"
else
  bad "the report says 1, which is this patch's added-line ordinal and not a line an operator can open"
fi
# And on the single-line shape the call, the flag and the added line are one
# line, so the report lands there -- the property the continued case splits.
want=$(grep -n -- "--jq" "$probe/adds-bad.patch" | grep '^[0-9]*:+' | cut -d: -f1)
got=$(patch_offenders "$probe/adds-bad.patch" | sed 's/.*://')
if [ "$got" = "$want" ]; then
  ok "the report names the single-line patch's own line ($got)"
else
  bad "the report says line $got; the offending line is $want -- an operator opening it finds unrelated text"
fi

# 4. And the SHIPPING code, over four fixture trees. Calling `git apply --check`
#    directly proves git behaves as documented and exercises no branch of the
#    loops that will actually run. Found in review of #180: inverting a
#    condition in them left every case green.
tree=$(mktemp -d) || exit 1
trap 'rm -rf "$probe" "$tree"' EXIT
git -C "$tree" init -q .
printf 'alpha\nbeta\ngamma\n' > "$tree/target.txt"
mkdir -p "$tree/.github/workflows"
printf 'alpha\nbeta\ngamma\n' > "$tree/.github/workflows/w.yml"
mkdir -p "$tree/empty" "$tree/fits" "$tree/drifted" "$tree/offends" \
         "$tree/both-halves" "$tree/workflow-drifted" "$tree/workflow-gone"
cat > "$tree/fits/a.patch" <<'FIXTURE'
--- a/target.txt
+++ b/target.txt
@@ -1,3 +1,4 @@
 alpha
 beta
+delta
 gamma
FIXTURE
sed 's/^ beta$/ BETA-RENAMED/' "$tree/fits/a.patch" > "$tree/drifted/a.patch"
cp "$probe/adds-bad.patch" "$tree/offends/a.patch"

# The two halves, in one patch, as every real parked patch has them: a workflow
# hunk, and a docs hunk it carries because its own landing forces it. Written
# out rather than derived from each other -- which half moved is the whole
# point of these three, so it should be readable at a glance.
#
# both-halves: the docs half has moved, the workflow half has not. This is the
# reviewer's scenario and the case that must NOT fail the build.
cat > "$tree/both-halves/a.patch" <<'FIXTURE'
--- a/.github/workflows/w.yml
+++ b/.github/workflows/w.yml
@@ -1,3 +1,4 @@
 alpha
 beta
+delta
 gamma
--- a/target.txt
+++ b/target.txt
@@ -1,3 +1,4 @@
 alpha
 BETA-RENAMED
+delta
 gamma
FIXTURE
# workflow-drifted: the other way round. Only an owner edit or another landing
# patch can do this, and it means the parked change itself is now wrong.
cat > "$tree/workflow-drifted/a.patch" <<'FIXTURE'
--- a/.github/workflows/w.yml
+++ b/.github/workflows/w.yml
@@ -1,3 +1,4 @@
 alpha
 BETA-RENAMED
+delta
 gamma
--- a/target.txt
+++ b/target.txt
@@ -1,3 +1,4 @@
 alpha
 beta
+delta
 gamma
FIXTURE
# workflow-gone: the workflow it targets no longer exists at all.
cat > "$tree/workflow-gone/a.patch" <<'FIXTURE'
--- a/.github/workflows/deleted.yml
+++ b/.github/workflows/deleted.yml
@@ -1,3 +1,4 @@
 alpha
 beta
+delta
 gamma
FIXTURE

expect() {  # $1 = what, $2 = expected substring, rest = command
  local what=$1 want=$2; shift 2
  local out; out=$("$@" 2>&1)
  case "$out" in
    *"$want"*) ok "$what" ;;
    *) bad "$what -- got: $(printf '%s' "$out" | head -2 | tr '\n' ' ')" ;;
  esac
}

expect "an empty pending directory is a pass with a line of its own, not a skip" \
  "no patches parked" check_parked_applies "$tree/empty" "$tree"
expect "a parked patch that fits reports as applying" \
  "still applies" check_parked_applies "$tree/fits" "$tree"
expect "a parked patch whose context moved is reported" \
  "no longer apply" check_parked_applies "$tree/drifted" "$tree"
expect "and reported as a warning rather than a failure" \
  "::warning file=" check_parked_applies "$tree/drifted" "$tree"
expect "a parked patch adding a forbidden call fails the suite" \
  "would deploy an invocation gh rejects" check_parked_shell "$tree/offends"
expect "a clean pending directory passes the shell scan" \
  "no parked patch would deploy" check_parked_shell "$tree/fits"

# And the split itself, which is the reason 1c is not simply one verdict. Each
# of these asserts on the FIRST line of the output, because "warning" and
# "failure" are told apart by which of `ok`/`bad` was called and nothing else.
verdict() {  # $1 = what, $2 = ok|FAIL, $3 = fixture dir
  local out first
  out=$(check_parked_applies "$tree/$3" "$tree" 2>&1)
  first=${out%%$'\n'*}
  case "$first" in
    "$2"*) ok "$1" ;;
    *) bad "$1 -- the verdict line was: $first" ;;
  esac
}
verdict "a docs hunk moving under a patch is a warning, not a failure" ok both-halves
verdict "a .github/ hunk moving under a patch IS a failure" FAIL workflow-drifted
verdict "a patch aimed at a workflow that no longer exists IS a failure" FAIL workflow-gone
expect "the warning names the file that actually moved, not just the patch" \
  "target.txt moved under it" check_parked_applies "$tree/both-halves" "$tree"
expect "the failure names the file that actually moved, not just the patch" \
  "git refused: .github/workflows/w.yml" check_parked_applies "$tree/workflow-drifted" "$tree"

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
