#!/usr/bin/env bash
# Does the staleness line say something a reader can act on?
#
# The line it produces is the only thing telling an agent that the finding it is
# about to implement was made against code that has since moved. Its previous
# version truncated at 800 characters and, on issue #26, ended mid-path on the
# word "c" — 86 commits of drift summarised into nothing.
#
# This runs the real filter, .github/scripts/compare-summary.jq, over compare
# responses of each shape. Same reason .github/tests/watchdog-filter-test.sh
# exists: a jq expression that has only been read is a hypothesis, and reading
# one is how a scoping bug survived in queue-scan.jq until a fixture caught it.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
filter="$here/../scripts/compare-summary.jq"

pass=0; fail=0

# check DESCRIPTION -- JSON -- NEEDLE...
check() {
  local desc="$1"; shift 2
  local json="$1"; shift 2
  local got missing=""
  if ! got=$(printf '%s' "$json" | jq -r -f "$filter" 2>&1); then
    fail=$((fail + 1))
    printf '  FAIL  %s\n         jq failed: %s\n' "$desc" "$got"
    return
  fi
  for needle in "$@"; do
    case "$got" in
      *"$needle"*) ;;
      *) missing="$missing\n         missing: $needle" ;;
    esac
  done
  if [ -z "$missing" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1))
    printf '  FAIL  %s%b\n         got: %s\n' "$desc" "$missing" "$got"
  fi
}

# absent DESCRIPTION -- JSON -- NEEDLE...
absent() {
  local desc="$1"; shift 2
  local json="$1"; shift 2
  local got found=""
  got=$(printf '%s' "$json" | jq -r -f "$filter" 2>&1)
  for needle in "$@"; do
    case "$got" in
      *"$needle"*) found="$found\n         should not contain: $needle" ;;
    esac
  done
  if [ -z "$found" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1))
    printf '  FAIL  %s%b\n         got: %s\n' "$desc" "$found" "$got"
  fi
}

echo "The staleness summary — how many, and where"

FEW='{"ahead_by":3,"files":[{"filename":"core/src/trust.cpp"},{"filename":"tests/test_trust.cpp"}]}'
check "a short list is named in full, because the names are the answer" -- "$FEW" -- \
      "Files changed since:" "core/src/trust.cpp" "tests/test_trust.cpp"
absent "and is not summarised into a count" -- "$FEW" -- "files changed since, across"

# Exactly at the boundary, and one past it. An off-by-one here silently swaps
# the two summaries, which is the kind of edit nothing else would catch.
twelve() {
  local n=$1 i out=""
  for i in $(seq 1 "$n"); do
    out="$out{\"filename\":\"dir$i/file$i.md\"},"
  done
  printf '{"ahead_by":9,"files":[%s]}' "${out%,}"
}
check "twelve files is still the naming branch" -- "$(twelve 12)" -- \
      "Files changed since:" "dir12/file12.md"
check "thirteen files switches to counting" -- "$(twelve 13)" -- \
      "13 files changed since, across:" "dir13/"
absent "and stops naming individual files" -- "$(twelve 13)" -- "file13.md"

MANY='{"ahead_by":86,"files":[
  {"filename":"CLAUDE.md"},{"filename":"STATUS.md"},{"filename":"core/src/trust.cpp"},
  {"filename":"core/include/attadipa/core/clock.h"},{"filename":".github/workflows/ci.yml"},
  {"filename":".github/scripts/agent-say.sh"},{"filename":"docs/research/VERIFIED_FACTS.md"},
  {"filename":"ui/src/color.cpp"},{"filename":"assets/fonts/README.md"},
  {"filename":"tests/test_trust.cpp"},{"filename":"apps/clock/main.cpp"},
  {"filename":"sim/main.cpp"},{"filename":"boards/waveshare/board.cpp"}]}'
check "a long list becomes a count and a set of directories" -- "$MANY" -- \
      "13 files changed since, across:" ".github/, CLAUDE.md, STATUS.md"
check "a file at the repository root keeps its own name" -- "$MANY" -- \
      "CLAUDE.md" "STATUS.md"
check "and a directory is named once, not per file" -- "$MANY" -- "core/"
absent "no path survives whole in the counting branch" -- "$MANY" -- \
       "core/src/trust.cpp" "ui/src/color.cpp"

# The shape that would divide by nothing at three in the morning.
check "a compare response with no files key at all is not an error" -- \
      '{"ahead_by":1}' -- "no changed files were reported"
check "an explicitly empty file list says the same" -- \
      '{"ahead_by":0,"files":[]}' -- "no changed files were reported"

# Both branches are capped. The first version of this filter capped only the
# long one, which the review on #70 called an inconsistency inside one diff.
long_names() {
  local n=$1 i out=""
  for i in $(seq 1 "$n"); do
    out="$out{\"filename\":\"a-very-long-directory-name-number-$i/and-a-long-file-name-$i.md\"},"
  done
  printf '{"ahead_by":4,"files":[%s]}' "${out%,}"
}
check "twelve very long paths are capped, not emitted whole" -- "$(long_names 12)" -- "…"
check "and so is a wide directory set" -- "$(long_names 40)" -- "…"

echo
echo "  $pass passed, $fail failed"
[ "$fail" -eq 0 ]
