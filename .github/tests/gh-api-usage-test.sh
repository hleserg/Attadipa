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
# EXITS NON-ZERO IF THE FILE HAS NO `+++` LINE AT ALL. Without that, a file
# that is not a patch -- truncated, half-written, saved in the wrong format --
# has no target to match, so `want` stays 0, the scan emits nothing, and the
# caller reads "no offenders found" from a file it never parsed. That is the
# `|| VAR=""` shape this whole file exists to refuse, and the caller already
# knows how to fail on a non-zero exit. Found in the second review round of
# #180.
patch_postimage() {
  awk '
    /^\+\+\+ /{
      seen_target = 1
      target = $2
      sub(/^b\//, "", target)
      want = (target ~ /^\.github\/(workflows|scripts|tests)\//)
      next
    }
    /^--- /  { next }
    /^@@/    { next }
    !want    { next }
    /^[+ ]/  { text = $0; sub(/^./, "", text); printf "%d\t%s\n", FNR, text }
    END      { if (!seen_target) exit 3 }
  ' "$1"
}

# The same rule as `offenders`, over a parked patch's post-image, reporting the
# line number in the PATCH -- not the nth added line, which is what this printed
# until review of #180 and which drifts further from the truth the longer the
# patch is.
patch_offenders() {
  local patch=$1 image
  # Captured rather than piped. `set -o pipefail` at the top of this file would
  # also carry `patch_postimage`'s refusal out of a pipeline, and mutating this
  # back to `a | b` leaves the suite green for exactly that reason -- so this is
  # belt and braces, not the load-bearing half, and saying otherwise would be
  # the kind of claim this file exists to catch. What it buys is that the
  # refusal survives somebody removing `pipefail`, which is one line at the top
  # of a 700-line file and no test would notice.
  image=$(patch_postimage "$patch") || return 3
  printf '%s\n' "$image" | awk -v patch="$patch" '
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

# Did `git` refuse because the PATCH ITSELF is malformed, rather than because
# the tree moved under it? The two are different problems with opposite
# remedies -- a moved context line is answered with `git rm` or a rebuild, a
# corrupt hunk header is answered by rebuilding and never by `git rm`, because
# `git rm`-ing a patch nobody can read throws away work nobody has read either.
# `:130` discarded git's stderr until the second review round of #180, so the
# two arrived as one message and the first remedy offered was the wrong one.
patch_is_corrupt() {
  # The wordings git actually produces, checked against git rather than
  # guessed: `corrupt patch at line N` for a hunk header whose counts do not
  # match its body, and `No valid patches in input` for a file with nothing
  # parseable in it at all. `unrecognized input` and `fatal:` are kept as a
  # widening for a git that words it differently; misclassifying a drift as
  # corrupt costs a wrong remedy in a message and never a wrong verdict, since
  # both branches fail the suite.
  case "$1" in
    *"corrupt patch"*|*"No valid patches in input"*|*"unrecognized input"*|*"fatal:"*) return 0 ;;
    *) return 1 ;;
  esac
}

# Which files `git apply` named in its refusal, deduplicated onto one line.
# This only enriches a message and never decides anything, so a `git` that
# words its errors differently costs a file name rather than a verdict.
refused_files() {
  sed -n 's/^error: \(.*\): patch does not apply$/\1/p;
          s/^error: \(.*\): No such file or directory$/\1/p' \
    | awk '!seen[$0]++' | tr '\n' ' ' | sed 's/ $//'
}

# THE ONE ENUMERATION both halves read, so neither can be silently switched off
# by a file the other can see. `find` and a `"$dir"/*.patch` glob disagreed until
# review of #180: `find` is recursive and the globs were not, so a patch parked
# in a subdirectory -- an ordinary way to group a multi-file change, and #128
# parked 516 lines across six files -- made the emptiness test say "not empty"
# while both scans opened nothing, and each then printed its strongest green
# line about a file neither had read.
#
# It refuses rather than ignores, in three cases that are all "somebody parked
# something this cannot judge": a subdirectory, a file that is not `*.patch`,
# and a missing directory. A missing directory is NOT an empty one -- empty is a
# state somebody chose, missing is one nobody noticed -- and reporting them
# alike is the same swallow this file exists to refuse.
# THE MARKER IS PRINTED FIRST, ahead of every valid patch, and that ordering is
# load-bearing. Both callers test the whole string with a `case` prefix pattern
# (`MISSING*|UNREADABLE*`), so a marker emitted after a real patch is a marker
# nobody sees -- and the only state where refusing matters is the one where
# something IS parked, which is exactly the state that used to hide it. This
# printed valid patches as it walked and the marker last until the third review
# round of #180, where the failing arrangement is a directory holding a good
# patch and a bad entry together; every fixture at the time held only the bad
# one, which is the single arrangement that put the marker first by accident.
parked_patches() {
  local dir=$1 f bad_entry="" good=""
  if [ ! -d "$dir" ]; then
    printf 'MISSING\n'
    return
  fi
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    case "$f" in
      "$dir"/*/*)  bad_entry="$bad_entry$f (in a subdirectory)"$'\n' ;;
      *.patch)     good="$good$f"$'\n' ;;
      *README.md)  ;;
      *)           bad_entry="$bad_entry$f (not a .patch)"$'\n' ;;
    esac
  done <<EOF
$(find "$dir" -type f 2>/dev/null | sort)
EOF
  [ -z "$bad_entry" ] || printf 'UNREADABLE\n%s' "$bad_entry"
  [ -z "$good" ] || printf '%s' "$good"
}

# The annotated entries out of a `parked_patches` string -- the ones that made it
# say UNREADABLE, and not the valid patches printed after them. A caller that
# indents the whole string under "this cannot read" would now name every good
# patch as unreadable, which is the mirror of the bug above.
parked_bad_entries() {
  printf '%s\n' "$1" | grep -e '(in a subdirectory)$' -e '(not a .patch)$'
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
  for patch in $(parked_patches "$dir" | grep -v '^MISSING$\|^UNREADABLE$\|(in a subdirectory)$\|(not a .patch)$'); do
    [ -e "$patch" ] || continue
    # `.github/workflows/*` and NOT `.github/*`. The whole rationale for making
    # this half fatal is at 1c: "no agent token can write those files." That is
    # true of `.github/workflows/`, which the `workflows` permission gates, and
    # false of `.github/scripts/` and `.github/tests/`, which any agent branch
    # edits -- 9991e79 created both while parking this patch, and this suite adds
    # 650 lines to `.github/tests/` itself. Under the wider glob, a patch
    # carrying a script-and-test hunk (the natural shape: a workflow change
    # arrives with its script and its test, as #128 did) goes fatal the moment an
    # unrelated pull request moves that context -- reddening a pull request that
    # did not cause it, which TASKS.md names as the criterion this must not
    # violate. Found in the third review round of #180.
    if ! err=$(git -C "$tree" apply --check --include='.github/workflows/*' -- "$patch" 2>&1); then
      if patch_is_corrupt "$err"; then
        printf 'corrupt\t%s\t%s\n' "$patch" "$(printf '%s\n' "$err" | head -1)"
      else
        printf 'stale-workflow\t%s\t%s\n' "$patch" "$(printf '%s\n' "$err" | refused_files)"
      fi
    elif ! err=$(git -C "$tree" apply --check -- "$patch" 2>&1); then
      if patch_is_corrupt "$err"; then
        printf 'corrupt\t%s\t%s\n' "$patch" "$(printf '%s\n' "$err" | head -1)"
      else
        printf 'drifted\t%s\t%s\n' "$patch" "$(printf '%s\n' "$err" | refused_files)"
      fi
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
  local dir=$1 patch found all="" entries
  entries=$(parked_patches "$dir")
  case "$entries" in
    MISSING*)
      bad "$dir does not exist -- a missing pending directory is not an empty one, \
and reporting them alike is how a guard turns itself off"
      return ;;
    UNREADABLE*)
      bad "something is parked here that this cannot read, so neither half of the \
guard covers it:"
      parked_bad_entries "$entries" | sed 's/^/       /'
      return ;;
  esac
  for patch in $entries; do
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
#       * the hunks under `.github/workflows/` can only be moved by an owner
#         edit or by another patch landing, since no agent token can write those
#         files. Small audience, and a stale workflow hunk means the parked
#         change itself is now wrong. FATAL.
#       * everything else a patch carries -- the docs edits its own landing
#         forces, per `pending/README.md`, and its `.github/scripts/` and
#         `.github/tests/` halves too -- moves under ordinary work, by people who
#         did not choose to and cannot rebuild a workflow patch. WARNING: a
#         `::warning::` annotation, a job-summary line, and a named remedy.
#
#     `.github/workflows/` and NOT `.github/`, which is what this used until the
#     third review round of #180. `scripts/` and `tests/` sit under `.github/`
#     and are written by agent branches constantly -- 32 of 262 commits since
#     2026-08-01 touch them -- so the wider glob put two agent-writable
#     directories on the fatal side, where an unrelated pull request moving a
#     test's context reds every open pull request at once.
#
#     `git apply --check --include='.github/workflows/*'` separates them, needs
#     no history and no `fetch-depth`, and works in the shallow checkout
#     `ci.yml` makes.
#
#     The SCAN's filter is deliberately wider than this SPLIT's: `patch_postimage`
#     reads `workflows|scripts|tests` because a forbidden `gh` call is a forbidden
#     call wherever it is parked, and breadth costs nothing there. Precision is
#     what the split needs, breadth is what the scan needs, and they are two
#     different globs on purpose.
check_parked_applies() {
  local dir=$1 tree=$2 states stale drifted patch refused entries
  entries=$(parked_patches "$dir")
  case "$entries" in
    MISSING*|UNREADABLE*) return ;;   # 1b already failed the suite for these
  esac
  states=$(patch_apply_states "$dir" "$tree")
  if [ -z "$entries" ]; then
    ok "no patches parked under ${dir#"$root/"}"
    return
  fi
  local corrupt
  corrupt=$(printf '%s\n' "$states" | grep '^corrupt')
  stale=$(printf '%s\n' "$states" | grep '^stale-workflow')
  drifted=$(printf '%s\n' "$states" | grep '^drifted')
  # Fatal, and reported FIRST and separately: a malformed patch is not a moved
  # context line, and the remedy for it is never `git rm`. Somebody parked work
  # nobody can read; deleting it is deleting the work.
  if [ -n "$corrupt" ]; then
    bad "a parked patch is malformed -- git cannot parse it, so neither half of this guard has read it:"
    printf '%s\n' "$corrupt" \
      | awk -F'\t' -v root="$root/" '{ p = $2; sub(root, "", p); printf "       %s\n         git said: %s\n", p, $3 }'
    printf '       rebuild it against the current tree. Do NOT git rm it -- a patch\n'
    printf '       nobody can read is work nobody has read either.\n'
  fi
  if [ -n "$stale" ]; then
    bad "a parked patch no longer applies under .github/workflows/ -- the workflow half it would land has moved:"
    printf '%s\n' "$stale" \
      | awk -F'\t' '{ printf "       %s\n         git refused: %s\n", $2, $3 }'
    printf '       already landed? git rm it -- pending/README.md says a patch is deleted in\n'
    printf '       the commit that applies it. Not landed? rebuild it against the current\n'
    printf '       tree; do not hand-edit the hunk headers.\n'
  elif [ -z "$drifted" ] && [ -z "$corrupt" ]; then
    # `-z "$corrupt"` because the corrupt branch above is its own `if`, outside
    # this chain: without it a malformed patch printed FAIL and then this printed
    # `ok every parked patch still applies` directly underneath, about a file
    # nothing had read. Third review round of #180.
    ok "every parked patch still applies to the tree"
    return
  elif [ -z "$drifted" ]; then
    return
  else
    ok "every parked patch still applies under .github/workflows/, the half nobody here can rebuild"
  fi
  [ -n "$drifted" ] || return
  ok "$(printf '%s\n' "$drifted" | wc -l | tr -d ' ') no longer apply in full (warning, not a failure)"
  printf '%s\n' "$drifted" | while IFS=$'\t' read -r _ patch refused; do
    [ -n "$patch" ] || continue
    # Workspace-relative, both here and in the summary. `file=` is matched
    # against workspace-relative paths, so an absolute runner path does not
    # anchor the annotation in Files changed -- and a summary line naming
    # /home/runner/work/... is meaningless the moment the container is gone.
    printf '::warning file=%s::this parked patch still applies under .github/workflows/ but not in full: %s moved under it. If it has already been landed, git rm it -- pending/README.md says a patch is deleted in the commit that applies it. If it has not, update that hunk, or land it with git apply -3 from a full clone.\n' \
      "${patch#"$root/"}" "${refused:-something it also edits}"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
      # shellcheck disable=SC2016  # the backticks are Markdown code spans in
      # the job summary, not command substitution; single quotes are what keeps
      # them literal.
      printf -- '- **drifted parked patch**: `%s` still applies under `.github/workflows/`, but `%s` moved under it. Landed already? `git rm` it. Not landed? Update that hunk, or `git apply -3`.\n' \
        "${patch#"$root/"}" "${refused:-something it also edits}" >> "$GITHUB_STEP_SUMMARY"
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
git -C "$tree" init -q . || exit 1
printf 'alpha\nbeta\ngamma\n' > "$tree/target.txt"
mkdir -p "$tree/.github/workflows"
printf 'alpha\nbeta\ngamma\n' > "$tree/.github/workflows/w.yml"
mkdir -p "$tree/.github/tests"
printf 'alpha\nbeta\ngamma\n' > "$tree/.github/tests/t.sh"
mkdir -p "$tree/empty" "$tree/fits" "$tree/drifted" "$tree/offends" \
         "$tree/both-halves" "$tree/workflow-drifted" "$tree/workflow-gone" \
         "$tree/tests-drifted"
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
# tests-drifted: the SHAPE OF THE REAL ONE. A workflow change arrives with its
# script and its test -- 9991e79 parked exactly that -- and `.github/tests/` is a
# directory agent branches edit constantly. So this half moving is ordinary work
# by somebody who did not choose it and cannot rebuild a workflow patch, and it
# must come out a WARNING. It was a FAILURE while the split's glob was
# `.github/*`, which is finding 2 of the third review round of #180: an
# unrelated pull request touching a test would have redded every open pull
# request at once. The workflow half here is untouched, which is what makes the
# verdict turn on the glob and nothing else.
cat > "$tree/tests-drifted/a.patch" <<'FIXTURE'
--- a/.github/workflows/w.yml
+++ b/.github/workflows/w.yml
@@ -1,3 +1,4 @@
 alpha
 beta
+delta
 gamma
--- a/.github/tests/t.sh
+++ b/.github/tests/t.sh
@@ -1,3 +1,4 @@
 alpha
 BETA-RENAMED
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

# Runs one of the shipping checks over a fixture and matches its output.
#
# GITHUB_STEP_SUMMARY IS REDIRECTED AT A SCRATCH FILE, and that is not tidiness.
# `out=$("$@" 2>&1)` captures stdout and stderr; a `>> "$GITHUB_STEP_SUMMARY"`
# redirection is neither. Actions sets that variable for every step and
# `ci.yml:360` runs this suite as an ordinary one, so without this every CI run
# -- every pull request, every push to main -- ended with four stale-parked-patch
# lines in its job summary, naming a `mktemp` directory the EXIT trap had already
# deleted. The job summary and the annotation are the ONLY channel 1c has,
# because it is deliberately not fatal; filling it with permanent false
# positives is how the hundred-and-first run, the one where the real patch
# drifted, goes unread. That is the swallowed-signal shape this file's header
# names as its own reason for existing, rebuilt inside its test section. Found
# in the second review round of #180.
expect() {  # $1 = what, $2 = expected substring, rest = command
  local what=$1 want=$2; shift 2
  local out; out=$(GITHUB_STEP_SUMMARY="$tree/summary" "$@" 2>&1)
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
  # Same scratch redirection as `expect`, and for the same reason: the fixtures
  # must never reach the real job summary.
  out=$(GITHUB_STEP_SUMMARY="$tree/summary" check_parked_applies "$tree/$3" "$tree" 2>&1)
  first=${out%%$'\n'*}
  case "$first" in
    "$2"*) ok "$1" ;;
    *) bad "$1 -- the verdict line was: $first" ;;
  esac
}
verdict "a docs hunk moving under a patch is a warning, not a failure" ok both-halves
verdict "a .github/workflows/ hunk moving under a patch IS a failure" FAIL workflow-drifted
verdict "a .github/tests/ hunk moving under a patch is a warning, not a failure" ok tests-drifted
verdict "a patch aimed at a workflow that no longer exists IS a failure" FAIL workflow-gone
expect "the warning names the file that actually moved, not just the patch" \
  "target.txt moved under it" check_parked_applies "$tree/both-halves" "$tree"
expect "the failure names the file that actually moved, not just the patch" \
  "git refused: .github/workflows/w.yml" check_parked_applies "$tree/workflow-drifted" "$tree"
expect "and a drifted test hunk is named in the warning, not in a failure" \
  ".github/tests/t.sh moved under it" check_parked_applies "$tree/tests-drifted" "$tree"

# 5. The job summary, which is the only operator-facing side effect in the file
#    and was both untested and leaking into the real one until review of #180.
#    `expect` cannot see it -- a redirection is not command substitution -- so
#    these read the file.
# Runs a shipping check for its SIDE EFFECT -- the job summary -- rather than its
# transcript, without throwing the transcript away. `>/dev/null` here meant a
# `bad` inside reached the counter with its message gone: `41 passed, 1 failed`
# and nothing naming which. Third review round of #180. It must stay in the
# CURRENT shell, or the counter moves in a subshell and the failure vanishes the
# other way round.
quietly() {
  local before=$fail
  GITHUB_STEP_SUMMARY="$tree/summary" "$@" > "$tree/quiet" 2>&1
  [ "$fail" -eq "$before" ] || sed 's/^/       /' "$tree/quiet"
}
: > "$tree/summary"
quietly check_parked_applies "$tree/both-halves" "$tree"
if grep -q 'drifted parked patch' "$tree/summary"; then
  ok "a drifted patch writes its line to the job summary"
else
  bad "nothing reached the job summary -- the warning has no channel left, and 1c is not fatal"
fi
# The path is printed as `${patch#"$root/"}`. The fixtures live outside `$root`,
# so nothing is stripped from them and asserting on their text proves nothing --
# run the check with `root` pointed at the fixture parent instead, which is the
# same relationship the real patch has to the real repository.
: > "$tree/summary"
( root=$tree; GITHUB_STEP_SUMMARY="$tree/summary" \
  check_parked_applies "$tree/both-halves" "$tree" >/dev/null 2>&1 )
# shellcheck disable=SC2016  # the backticks are the Markdown code span the
# summary line writes; single quotes are what keeps them literal.
if grep -q '`both-halves/a.patch`' "$tree/summary"; then
  ok "the summary line's path is relative to the tree, not the runner's"
else
  bad "the summary line carries an absolute path: $(head -1 "$tree/summary") \
-- file= will not anchor in Files changed, and the path is meaningless once the runner is gone"
fi
: > "$tree/summary"
quietly check_parked_applies "$tree/fits" "$tree"
if [ -s "$tree/summary" ]; then
  bad "a patch that fits wrote to the job summary -- a false warning there is how the true one goes unread"
else
  ok "a patch that fits writes nothing to the job summary"
fi

# 6. What is parked but cannot be judged. Each of these made BOTH halves print
#    their strongest green line about a file neither had opened.
sub=$(mktemp -d) || exit 1
trap 'rm -rf "$probe" "$tree" "$sub"' EXIT
# A CLEAN patch in each case, deliberately. Seeding with `adds-bad.patch` would
# make the case pass for the wrong reason -- the scan would flag its `--slurp`
# pair once the file became visible, so the assertion could not tell "refused
# because it is in a subdirectory" from "read it and found the bad call".
mkdir -p "$sub/nested/inner"
cp "$probe/adds-good.patch" "$sub/nested/inner/hidden.patch"
out=$(check_parked_shell "$sub/nested" 2>&1)
case "$out" in
  *FAIL*) ok "a patch parked in a subdirectory is a failure, not an unread pass" ;;
  *)      bad "a patch in a subdirectory is invisible to both halves while the emptiness test sees it" ;;
esac
mkdir -p "$sub/odd"
cp "$probe/adds-good.patch" "$sub/odd/change.diff"
out=$(check_parked_shell "$sub/odd" 2>&1)
case "$out" in
  *FAIL*) ok "a parked file that is not .patch is a failure, not an unread pass" ;;
  *)      bad "a .diff is invisible to both halves -- the glob is *.patch and nothing says so" ;;
esac
out=$(check_parked_shell "$sub/does-not-exist" 2>&1)
case "$out" in
  *FAIL*) ok "a MISSING pending directory is a failure, not the same pass as an empty one" ;;
  *)      bad "a missing directory reports as a pass -- empty is chosen, missing is unnoticed" ;;
esac
mkdir -p "$sub/empty-dir"
out=$(check_parked_shell "$sub/empty-dir" 2>&1)
case "$out" in
  *FAIL*) bad "an EMPTY pending directory failed -- empty is the normal state" ;;
  *)      ok "an empty pending directory is still a pass, as it should be" ;;
esac

# THE ARRANGEMENT THE THREE CASES ABOVE ALL MISS: a valid patch and a bad entry
# in the same directory. Each of `nested`, `odd` and `does-not-exist` holds only
# the bad thing, which is the one arrangement that put the UNREADABLE marker
# first by accident -- so all three passed while `parked_patches` printed valid
# patches as it walked and the marker last, and both callers tested the string
# with a `case` prefix pattern that a leading patch path defeats. This is the
# only state that matters, because a pending directory with nothing worth
# guarding in it is not the one that gets missed. Finding 1, third review round
# of #180.
mkdir -p "$sub/mixed/inner"
cp "$probe/adds-good.patch" "$sub/mixed/visible.patch"
cp "$probe/adds-good.patch" "$sub/mixed/inner/hidden.patch"
out=$(check_parked_shell "$sub/mixed" 2>&1)
case "$out" in
  *FAIL*) ok "a bad entry beside a valid patch is still a failure, not a pass about the good one" ;;
  *)      bad "a bad entry hides behind a valid patch -- the refusal fires only when there is nothing to guard" ;;
esac
# And it must name the bad entry ONLY. The marker now prints ahead of the valid
# patches, so a caller indenting the whole string would list every good patch as
# unreadable -- the same bug pointing the other way.
case "$out" in
  *"hidden.patch (in a subdirectory)"*)
    ok "and it names the entry it cannot read" ;;
  *) bad "the refusal does not name the unreadable entry: $(printf '%s' "$out" | tr '\n' ' ')" ;;
esac
case "$out" in
  *"visible.patch"*) bad "the refusal lists a valid patch as unreadable -- the marker's new position needs the caller to filter" ;;
  *)                 ok "and does not list the valid patch beside it as unreadable" ;;
esac
# 1c must refuse it too, and silently: 1b has already failed the suite, and two
# failures for one cause is noise.
out=$(GITHUB_STEP_SUMMARY="$tree/summary" check_parked_applies "$sub/mixed" "$tree" 2>&1)
if [ -z "$out" ]; then
  ok "and 1c says nothing about a directory 1b has already refused"
else
  bad "1c reported on a directory it cannot judge: $(printf '%s' "$out" | head -1)"
fi

# 7. A file that is not a patch at all, and one git cannot parse. Both used to
#    read as clean: the first has no `+++` so the scan matched nothing and the
#    caller saw an empty result, the second was reported in the same words as a
#    moved context line, whose first suggested remedy is `git rm` -- deleting
#    work nobody has read.
mkdir -p "$sub/notapatch"
printf 'this is prose, not a patch\nsomebody saved the wrong buffer\n' \
  > "$sub/notapatch/a.patch"
out=$(check_parked_shell "$sub/notapatch" 2>&1)
case "$out" in
  *FAIL*) ok "a file with no diff target in it is refused, not scanned clean" ;;
  *)      bad "a file with no +++ line scans as clean -- nothing was parsed and the result read as no offenders" ;;
esac
mkdir -p "$sub/corrupt"
# A hunk header whose counts do not match its body. `git apply` answers this
# with `corrupt patch at line N`, which is a different sentence from the
# `patch does not apply` a moved context line produces -- checked against git,
# not assumed.
cat > "$sub/corrupt/a.patch" <<'FIXTURE'
--- a/.github/workflows/w.yml
+++ b/.github/workflows/w.yml
@@ -1,99 +1,99 @@
 alpha
+beta
FIXTURE
out=$(GITHUB_STEP_SUMMARY="$tree/summary" check_parked_applies "$sub/corrupt" "$tree" 2>&1)
case "$out" in
  *"malformed"*) ok "a malformed patch is reported as malformed, not as drifted" ;;
  *)             bad "a corrupt patch reads as a moved context line -- got: $(printf '%s' "$out" | head -1)" ;;
esac
case "$out" in
  *"Do NOT git rm"*) ok "and its remedy is a rebuild, never git rm" ;;
  *)                 bad "a corrupt patch is offered git rm, which deletes work nobody has read" ;;
esac
# And it must not be contradicted two lines later. `corrupt` is its own `if`,
# outside the stale/drifted chain, so the chain's `ok every parked patch still
# applies` used to print directly under the FAIL -- about the very file nothing
# had been able to read. The suite still failed, so this never showed up as a
# red run; it showed up as a transcript that says both things. Third review
# round of #180.
case "$out" in
  *"still applies"*) bad "a malformed patch is reported and then contradicted by an ok line about the same file" ;;
  *)                 ok "and nothing prints ok still-applies about a file git could not parse" ;;
esac

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
