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
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
