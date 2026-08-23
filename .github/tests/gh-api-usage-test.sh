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

files=$(find "$root/.github/workflows" -name '*.yml' -o -name '*.yaml' | sort) || exit 1
[ -n "$files" ] || { printf 'FAIL no workflow files found\n'; exit 1; }

# 1. No workflow may combine them.
# shellcheck disable=SC2086  # the paths are ours and contain no spaces
found=$(offenders $files)
if [ -z "$found" ]; then
  ok "no workflow passes --slurp together with --jq or --template"
else
  bad "gh rejects --slurp with --jq/--template; these invocations would exit 1 before any request:"
  printf '%s\n' "$found" | sed 's/^/       /'
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

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
