#!/usr/bin/env bash
# Catch two gh CLI errors that otherwise fail before making an API request.
# Shipping workflow and script files are the source under test; there is no
# generated post-image or parked-patch surrogate.

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1

pass=0; fail=0
ok()  { pass=$((pass + 1)); printf 'ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }

# Join shell continuations, strip comments, and reject the gh combination that
# the CLI itself refuses: --slurp with its own formatter.
api_offenders() {
  awk '
    { sub(/[[:space:]]*#.*$/, "") }
    { line = line $0 }
    /\\[[:space:]]*$/ { sub(/\\[[:space:]]*$/, " ", line); next }
    {
      if (line ~ /gh[[:space:]]+api/ && line ~ /--slurp/ &&
          line ~ /--(jq|template)([ =]|$)/)
        printf "%s:%d\n", FILENAME, FNR
      line = ""
    }
  ' "$@"
}

# Ask this runner's gh for its accepted fields. This is a local flag-parse
# operation: no request or repository state is involved, and the list stays in
# step with the CLI version installed on the runner.
gh_fields() {
  local noun=$1
  gh "$noun" list --json 2>&1 >/dev/null \
    | awk '/^  [A-Za-z]/ { out = out sep $1; sep = "," } END { print out }'
}

# Find literal --json field lists (including a one-line shell variable) and
# compare them with the noun-specific schema above. Runtime-built lists are
# left to the caller's executable test rather than guessed at here.
json_offenders() {
  local pr_fields=$1 issue_fields=$2
  shift 2
  awk -v PR="$pr_fields" -v ISSUE="$issue_fields" '
    function check(rec, where, allowed,   raw,n,i,a,name) {
      if (rec ~ /gh[[:space:]]+pr[[:space:]]+(list|view)/) allowed = PR
      else if (rec ~ /gh[[:space:]]+issue[[:space:]]+(list|view)/) allowed = ISSUE
      else return
      if (!match(rec, /--json[ =]+[^[:space:]]+/)) return
      raw = substr(rec, RSTART, RLENGTH)
      sub(/^--json[ =]+/, "", raw); gsub(/^["\047]|["\047]$/, "", raw)
      if (raw ~ /^\$\{?[A-Za-z_][A-Za-z0-9_]*\}?$/) {
        name = raw; gsub(/[$}{]/, "", name)
        if (name in vars) raw = vars[name]; else return
      } else if (raw ~ /[$]/) return
      n = split(raw, a, ",")
      for (i = 1; i <= n; i++)
        if (a[i] != "" && index("," allowed ",", "," a[i] ",") == 0)
          printf "%s\t%s\n", where, a[i]
    }
    FNR == 1 { line = ""; split("", vars) }
    {
      text = $0; sub(/[[:space:]]*#.*$/, "", text)
      if (text ~ /^[A-Za-z_][A-Za-z0-9_]*=(["\047])[^"\047]*["\047][[:space:]]*$/) {
        name = text; sub(/=.*/, "", name)
        value = text; sub(/^[^=]*=/, "", value)
        gsub(/^["\047]|["\047][[:space:]]*$/, "", value); vars[name] = value
      }
      if (line == "") first = FILENAME ":" FNR
      line = line text
      if (text ~ /\\[[:space:]]*$/) { sub(/\\[[:space:]]*$/, " ", line); next }
      check(line, first, ""); line = ""
    }
  ' "$@"
}

mapfile -t shipping < <(
  find .github/workflows .github/scripts -type f \
    \( -name '*.yml' -o -name '*.yaml' -o -name '*.sh' \) | sort
)
[ "${#shipping[@]}" -gt 0 ] || { printf 'FAIL no shipping automation found\n'; exit 1; }

found=$(api_offenders "${shipping[@]}")
if [ -z "$found" ]; then
  ok 'shipping automation never combines --slurp with --jq/--template'
else
  bad 'gh rejects --slurp with --jq/--template at:'
  printf '       %s\n' "$found"
fi

pr_fields=$(gh_fields pr || true)
issue_fields=$(gh_fields issue || true)
if [ -z "$pr_fields" ] || [ -z "$issue_fields" ]; then
  bad 'could not read gh pr/issue --json schemas'
else
  found=$(json_offenders "$pr_fields" "$issue_fields" "${shipping[@]}")
  if [ -z "$found" ]; then
    ok 'every literal gh pr/issue --json field is accepted by this gh'
  else
    bad 'gh rejects these --json fields before making a request:'
    printf '%s\n' "$found" | awk -F '\t' '{ printf "       %s  %s\n", $1, $2 }'
  fi
fi

# Minimal mutations prove both guards can fail and leave the supported forms
# alone. Process substitution avoids a fixture implementation that can drift.
if [ -n "$(api_offenders <(printf '%s\n' 'gh api x --paginate --slurp --jq .'))" ]; then
  ok 'the API flag guard catches the rejected combination'
else
  bad 'the API flag guard cannot catch its target'
fi
if [ -z "$(api_offenders <(printf '%s\n' 'gh api x --paginate --slurp | jq .'))" ]; then
  ok 'the API flag guard accepts an external jq pipe'
else
  bad 'the API flag guard rejects the supported pipe form'
fi
if [ -n "$pr_fields" ] && json_offenders "$pr_fields" "$issue_fields" \
     <(printf '%s\n' 'gh pr list --json number,baseRepository') | grep -q baseRepository; then
  ok 'the field guard catches a REST-only pull-request name'
else
  bad 'the field guard cannot catch an unsupported pull-request field'
fi
if [ -n "$issue_fields" ] && json_offenders "$pr_fields" "$issue_fields" \
     <(printf '%s\n' 'gh issue list --json number,isCrossRepository') | grep -q isCrossRepository; then
  ok 'the field guard keeps pull-request and issue schemas separate'
else
  bad 'the field guard accepts a pull-request field on an issue command'
fi
# shellcheck disable=SC2016  # $FIELDS is fixture text for the scanner.
if [ -z "$(json_offenders "$pr_fields" "$issue_fields" \
     <(printf '%s\n' "FIELDS='number,isCrossRepository,labels'" 'gh pr list --json "$FIELDS"'))" ]; then
  ok 'the field guard resolves a literal field variable'
else
  bad 'the field guard rejects a supported literal field variable'
fi

# A third shape that fails before it reaches an API, and for the same reason the
# two above do: the shell, not the service, decides the answer. `grep -q` exits
# at its first match and closes the pipe; a `printf` still writing gets EPIPE,
# and `set -o pipefail` makes that non-zero status the pipeline's. The condition
# then reads false while being true. It is a scheduler race, so it lands on a
# runner and almost never on a workstation -- #500 has the log line and the two
# attempts at one commit that disagree. The fix is a here-string, which makes
# the shell itself the writer. This guard is a tripwire for the shape coming
# back, not a proof: a `printf` whose argument contains its own `)` is missed.
# `q` is looked for anywhere in the short-flag cluster: `-Fxq`, `-Eq` and `-qF`
# exit early for the same reason `-q` does, and #500's own grep, which anchored
# on `grep -q`, missed fourteen of them -- four on the writer admission path.
printf_pipe_offenders() {
  awk '
    {
      bare = $0
      gsub(/'"'"'[^'"'"']*'"'"'/, "", bare)
      if (bare ~ /printf.*[^) ][[:space:]]*\|[[:space:]]*grep[[:space:]]+-[A-Za-z]*q/)
        printf "%s:%d: %s\n", FILENAME, FNR, $0
    }
  ' "$@"
}

found="$(printf_pipe_offenders .github/scripts/*.sh .github/tests/*.sh)"
if [ -z "$found" ]; then
  # shellcheck disable=SC2016  # `$VAR` here is prose about shell text, not an expansion.
  ok 'no `printf ... | grep -q` can lose its race with pipefail'
else
  # shellcheck disable=SC2016  # same: the message quotes the fixed form verbatim.
  bad 'a printf writing into `grep -q` under pipefail (use `grep -q ... <<<"$VAR"`):'
  awk '{ print "       " $0 }' <<<"$found"
fi

# shellcheck disable=SC2016  # fixture text for the scanner, not a command to run.
if [ -n "$(printf_pipe_offenders <(printf '%s\n' 'printf "%s" "$BODY" | grep -q needle'))" ]; then
  ok 'the pipefail guard catches the racing pipeline'
else
  bad 'the pipefail guard cannot catch its target'
fi
# shellcheck disable=SC2016  # fixture text for the scanner, not a command to run.
if [ -z "$(printf_pipe_offenders <(printf '%s\n' 'grep -q needle <<<"$BODY"'))" ]; then
  ok 'the pipefail guard accepts the here-string form'
else
  bad 'the pipefail guard rejects the fixed form'
fi
# shellcheck disable=SC2016  # fixture text for the scanner, not a command to run.
if [ -n "$(printf_pipe_offenders <(printf '%s\n' 'printf "%s" "$L" | grep -Fxq name'))" ]; then
  ok 'the pipefail guard catches a q bundled with other short flags'
else
  bad 'the pipefail guard only looks for a bare -q'
fi
if [ -z "$(printf_pipe_offenders <(printf '%s\n' 'if json_offenders <(printf "%s" x) | grep -q y; then'))" ]; then
  ok 'the pipefail guard leaves a process substitution alone'
else
  bad 'the pipefail guard flags a pipeline printf does not write'
fi

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
