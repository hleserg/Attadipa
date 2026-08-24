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
# silent. First the shell it adds, read the same way workflow shell is read --
# added lines only, since a patch that *removes* a bad call is the opposite of
# an offender. Second that it still applies: it is pinned to context in files
# this work edits constantly, `pending/README.md` tells a human to check, and
# nothing checked. `git apply --check` writes nothing and needs no repository.
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

# What a parked patch ADDS, with the leading `+` stripped, so the same scan can
# read it. Removed lines are dropped: a patch deleting a bad call is the fix,
# not the defect. `+++` is the file header and never shell.
added_lines() {
  sed -n '/^+++ /!s/^+//p' "$1"
}

# The same scan, over what each parked patch would add.
patch_offenders() {
  local patch=$1 tmp
  tmp=$(mktemp) || return 1
  added_lines "$patch" > "$tmp"
  offenders "$tmp" | sed "s|^$tmp:|$patch:|"
  rm -f "$tmp"
}

files=$(find "$root/.github/workflows" -name '*.yml' -o -name '*.yaml' | sort) || exit 1
[ -n "$files" ] || { printf 'FAIL no workflow files found\n'; exit 1; }

patches=$(find "$root/docs/automation/pending" -name '*.patch' 2>/dev/null | sort)

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
#     An empty pending directory is a pass and not a skip -- there is nothing
#     parked, which is the state this check wants.
patch_found=""
for patch in $patches; do
  one=$(patch_offenders "$patch")
  [ -n "$one" ] && patch_found="$patch_found$one"$'\n'
done
if [ -z "$patch_found" ]; then
  ok "no parked patch adds --slurp together with --jq or --template"
else
  bad "a parked patch would deploy an invocation gh rejects:"
  printf '%s\n' "$patch_found" | sed 's/^/       /'
fi

# 1c. And that each parked patch still applies. It is pinned to context in the
#     workflow files this work edits constantly, so it rots by being correct
#     while something else moves. `--check` writes nothing.
apply_bad=""
for patch in $patches; do
  if ! git -C "$root" apply --check "$patch" 2>/dev/null; then
    apply_bad="$apply_bad$patch"$'\n'
  fi
done
if [ -z "$patches" ]; then
  ok "no patches parked under docs/automation/pending"
elif [ -z "$apply_bad" ]; then
  ok "every parked patch still applies to the tree"
else
  bad "a parked patch no longer applies -- its context has moved under it:"
  printf '%s' "$apply_bad" | sed 's/^/       /'
  printf '       rebuild it against the current tree; do not hand-edit the hunk headers\n'
fi

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

# 3. The patch scan detects, and in both directions. The removal case is the
#    one worth having: a text scan that flagged a patch for deleting a bad call
#    would fire on the fix and teach the next person to route around it.
cat > "$probe/adds-bad.patch" <<'FIXTURE'
--- a/.github/workflows/x.yml
+++ b/.github/workflows/x.yml
@@ -1,2 +1,3 @@
 jobs:
+          gh api --paginate --slurp "repos/x/y/pulls" --jq '.[]'
 steps:
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

if [ -n "$(patch_offenders "$probe/adds-bad.patch")" ]; then
  ok "the patch scan catches a bad call a parked patch would add"
else
  bad "the patch scan misses an added --slurp/--jq pair, which is #179's whole point"
fi
for f in removes-bad adds-good; do
  if [ -z "$(patch_offenders "$probe/$f.patch")" ]; then
    ok "the patch scan leaves $f alone"
  else
    bad "the patch scan flags $f -- it must read added lines, not removed ones"
  fi
done

# 4. And the apply check fails on drift rather than reporting a clean tree.
#    Proven on a throwaway tree, because proving it on the real one would mean
#    breaking a parked patch to watch it break.
drift=$(mktemp -d) || exit 1
trap 'rm -rf "$probe" "$drift"' EXIT
printf 'alpha\nbeta\ngamma\n' > "$drift/target.txt"
cat > "$drift/fits.patch" <<'FIXTURE'
--- a/target.txt
+++ b/target.txt
@@ -1,3 +1,4 @@
 alpha
 beta
+delta
 gamma
FIXTURE
cat > "$drift/drifted.patch" <<'FIXTURE'
--- a/target.txt
+++ b/target.txt
@@ -1,3 +1,4 @@
 alpha
 BETA-RENAMED
+delta
 gamma
FIXTURE
if git -C "$drift" apply --check "$drift/fits.patch" 2>/dev/null; then
  ok "the apply check passes a patch that still fits"
else
  bad "the apply check rejects a patch that does fit, so it would fail on green"
fi
if git -C "$drift" apply --check "$drift/drifted.patch" 2>/dev/null; then
  bad "the apply check passes a patch whose context has moved, so it guards nothing"
else
  ok "the apply check catches a patch whose context has moved"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
