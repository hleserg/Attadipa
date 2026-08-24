#!/usr/bin/env bash
# How wide is the queue allowed to be?
#
# Offline and deterministic: a number in, one line out. The banding is the whole
# rule (CLAUDE.md, "The queue has a width, and it is two"; owner decision
# OD-23), and the boundaries are where a rule like this is got wrong — three is
# allowed to exist and is not allowed to grow, which is neither "ok" nor
# "incident" and is the case a two-way comparison loses.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
script="$here/../scripts/wip-limit.sh"

pass=0; fail=0

# want DESCRIPTION WANTED_LINE COUNT
want() {
  local desc="$1" wanted="$2" count="${3-}"
  local got
  got=$(bash "$script" --verdict "$count")
  if [ "$got" = "$wanted" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s", got "%s"\n' "$desc" "$wanted" "$got"
  fi
}

# says DESCRIPTION SUBSTRING STATE COUNT
says() {
  local desc="$1" needle="$2" state="$3" count="$4"
  local got
  got=$(bash "$script" --say "$state" "$count")
  case "$got" in
    *"$needle"*)
      pass=$((pass + 1)); printf '  ok    %s\n' "$desc" ;;
    *)
      fail=$((fail + 1)); printf '  FAIL  %s\n         wanted a sentence containing "%s", got "%s"\n' "$desc" "$needle" "$got" ;;
  esac
}

echo "WIP limit — the three bands and their boundaries"

want 'an empty queue is ok'                'ok 0'        0
want 'one open pull request is ok'         'ok 1'        1
want 'two is the normal limit, still ok'   'ok 2'        2
want 'three is the ceiling, not ok'        'full 3'      3
want 'four is a queue incident'            'incident 4'  4
want 'the thirty-five that caused OD-23'   'incident 35' 35

echo
echo "A number that is not a number is refused, not guessed at"

want 'an empty count is unknown'           'unknown '    ''
want 'a non-numeric count is unknown'      'unknown n/a' 'n/a'
want 'a negative count is unknown'         'unknown -1'  '-1'

echo
echo "The sentence says what to do, not only what the number is"

says 'the ceiling tells the reader to finish one first' 'before opening another'         full     3
says 'the ceiling does not call itself an incident'     'ceiling'                        full     3
says 'an incident stops new work'                       'New feature, research and meta' incident 9
says 'an incident names where the rule is written'      'CLAUDE.md'                      incident 9
says 'an unreadable count says it checked nothing'      'says nothing about this pull request' unknown ''

echo
echo "Shape"

lines=$(bash "$script" --verdict 7 | wc -l | tr -d ' ')
if [ "$lines" = "1" ]; then
  pass=$((pass + 1)); printf '  ok    a verdict is exactly one line\n'
else
  fail=$((fail + 1)); printf '  FAIL  a verdict is exactly one line\n         got %s\n' "$lines"
fi

# The count and the state travel together: a workflow that printed "ok" beside
# a count of nine would be worse than printing nothing.
if [ "$(bash "$script" --verdict 9)" = "incident 9" ]; then
  pass=$((pass + 1)); printf '  ok    the verdict carries the count it was reached on\n'
else
  fail=$((fail + 1)); printf '  FAIL  the verdict carries the count it was reached on\n'
fi


echo
echo "Counting: the query, and the shell it has to run in"

# A stub `gh` on PATH, in the shape of gh-label-test.sh: it records the command
# line it was given and answers with a fixed "<counted> <exempted>" pair. The
# real filter runs inside `gh --jq` and cannot be executed here, so what is
# asserted is that the right question is asked and that the answer is read from
# the right field. Both of those have been wrong.
stub=$(mktemp -d) || exit 1
trap 'rm -rf "$stub"' EXIT
cat > "$stub/gh" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$ATTADIPA_GH_LOG"
printf '%s\n' "${ATTADIPA_GH_ANSWER-5 2}"
STUB
chmod +x "$stub/gh"
export ATTADIPA_GH_LOG="$stub/log"
export ATTADIPA_GH_ANSWER="5 2"

# count_says DESCRIPTION WANTED_STDOUT ARGS...
# Runs the script with the stub on PATH and GITHUB_REPOSITORY removed, which is
# the environment every document tells a person to type this in.
count_says() {
  local desc="$1" wanted="$2"; shift 2
  local got rc
  : > "$ATTADIPA_GH_LOG"
  got=$(PATH="$stub:$PATH" env -u GITHUB_REPOSITORY bash "$script" "$@" 2>/dev/null); rc=$?
  if [ "$rc" = "0" ] && [ "$got" = "$wanted" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s" (exit 0), got "%s" (exit %s)\n' "$desc" "$wanted" "$got" "$rc"
  fi
}

# asked DESCRIPTION SUBSTRING -- of the gh command line the script issued.
asked() {
  local desc="$1" needle="$2"
  if grep -qF -- "$needle" "$ATTADIPA_GH_LOG"; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         no "%s" in: %s\n' "$desc" "$needle" "$(cat "$ATTADIPA_GH_LOG")"
  fi
}

# The invocation the documents actually give. GITHUB_REPOSITORY exists only
# inside GitHub Actions, and `${VAR:?}` under `set -euo pipefail` aborts a
# non-interactive shell -- so this exact command printed an error to stderr,
# nothing to stdout, and exited 1, while CLAUDE.md, AI_TASK_PROTOCOL.md step 0
# and T-171's acceptance criterion all offered it as a working command.
count_says "--count runs in a shell that has no GITHUB_REPOSITORY" "5" --count
asked      "and asks gh for the open pull requests" "pr list --state open"

if grep -qF -- "--repo " "$ATTADIPA_GH_LOG"; then
  fail=$((fail + 1)); printf '  FAIL  no --repo is passed when none is known\n         got: %s\n' "$(cat "$ATTADIPA_GH_LOG")"
else
  pass=$((pass + 1)); printf '  ok    no --repo is passed when none is known, so gh infers it\n'
fi

count_says "--count takes an explicit owner/repo" "5" --count o/r
asked      "and passes it through"                "--repo o/r"

count_says "--exempt reports the subtraction, not the total" "2" --exempt

# Finding 7 of the review of #193: the fork exclusion was stated in a YAML
# comment and absent from the query, so three outside contributions would read
# as a queue incident and stop this repository's own work.
count_says "the query is issued" "5" --count o/r
# The needle is the FILTER, not the field name: `isCrossRepository` also appears
# in --json, so grepping for it alone passed with the exclusion deleted. A test
# that cannot fail is not evidence.
asked      "and excludes pull requests opened from a fork" "select(.isCrossRepository | not)"
asked      "and names the parked exemption"                "queue:parked"
asked      "and the emergency one"                         "queue:emergency"

# A gh that answers nothing must reach the `unknown` band, not a wrong number.
: > "$ATTADIPA_GH_LOG"
empty=$(ATTADIPA_GH_ANSWER="" PATH="$stub:$PATH" env -u GITHUB_REPOSITORY bash "$script" --count 2>/dev/null)
if [ -z "$empty" ] && [ "$(bash "$script" --verdict "$empty")" = "unknown " ]; then
  pass=$((pass + 1)); printf '  ok    a count that came back empty becomes unknown, not a band\n'
else
  fail=$((fail + 1)); printf '  FAIL  a count that came back empty becomes unknown, not a band\n         got "%s"\n' "$empty"
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
