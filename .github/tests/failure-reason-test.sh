#!/usr/bin/env bash
# The whitelist is the security model, so the tests that matter are the ones
# where something confidential is in the log and must not come out.
#
# .github/scripts/failure-reason.sh reads the action's full execution log, which
# contains every tool result: file contents, `gh` output, environment echoes,
# and whatever a Bash step printed. It exists so that a failure names itself on
# the issue instead of pointing at a redacted run log. That is only worth having
# if the thing it prints is bounded by shape rather than by hope, and the second
# half of this file is entirely about that.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Overridable so that "this case is red before the fix" can be *run* rather than
# asserted in a commit message:
#   ATTADIPA_REASON_SH=/tmp/old.sh bash .github/tests/failure-reason-test.sh
# The #106 cases below were checked that way against 5fd2738's copy of the
# script, which prints six of them verbatim.
REASON_SH="${ATTADIPA_REASON_SH:-.github/scripts/failure-reason.sh}"
reason() { bash "$REASON_SH" "$1"; }

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
  local name="$1" got="$2" want="$3"
  case "$got" in
    *"$want"*) printf '  ok    %s\n' "$name"; pass=$((pass + 1)) ;;
    *) printf '  FAIL  %s\n     want to contain: %s\n     got:  %s\n' "$name" "$want" "$got"
       fail=$((fail + 1)) ;;
  esac
}

lacks() {
  local name="$1" got="$2" forbidden="$3"
  case "$got" in
    *"$forbidden"*) printf '  FAIL  %s\n     must not contain: %s\n     got:  %s\n' \
                      "$name" "$forbidden" "$got"; fail=$((fail + 1)) ;;
    *) printf '  ok    %s\n' "$name"; pass=$((pass + 1)) ;;
  esac
}

log() { printf '%s' "$1" > "$TMP/log.json"; echo "$TMP/log.json"; }

echo "The log does not exist, which is itself an outcome"
says "a missing path is reported rather than crashed on" \
     "$(reason "$TMP/nothing-here.json")" \
     "no execution log was written — the agent step did not get far enough to leave one"
says "and so is an empty argument, which is what an unset output looks like" \
     "$(reason "")" \
     "no execution log was written — the agent step did not get far enough to leave one"

echo
echo "A clean run"
says "no error is said plainly, with the subtype the SDK chose" \
     "$(reason "$(log '[{"type":"result","subtype":"success","is_error":false,"num_turns":31,"result":"done"}]')")" \
     "the run reported no error (subtype \`success\`)"

echo
echo "The failures this pipeline has actually had"
# Run 32589375744 on #67, 2026-08-22: a real session, twenty turns, sixty-nine
# cents, and a result object that named nothing. This is the case the whole file
# was written for, and the honest answer is that it is unclassified -- but
# unclassified WITH the structural facts, which is strictly more than the issue
# comment used to carry.
contains "#67's signature is classified as unclassified, not guessed at" \
     "$(reason "$(log '[{"type":"result","subtype":"success","is_error":true,"duration_ms":84607,"num_turns":20,"total_cost_usd":0.689,"permission_denials_count":0}]')")" \
     "unclassified — SDK subtype \`success\`, ended at turn 20, with no final message at all"
contains "the six earlier deaths that day named themselves, and still do" \
     "$(reason "$(log '[{"type":"result","subtype":"error_max_turns","is_error":true,"num_turns":61}]')")" \
     "SDK subtype \`error_max_turns\`, ended at turn 61"

echo
echo "Error shapes worth naming"
contains "a context refusal is the suspicion on #67 and must be legible if it happens" \
     "$(reason "$(log '[{"type":"result","subtype":"success","is_error":true,"result":"API Error: 400 {\"type\":\"invalid_request_error\",\"message\":\"prompt is too long: 214233 tokens > 200000 maximum\"}"}]')")" \
     "API Error: 400"
contains "so is the same refusal phrased as prose" \
     "$(reason "$(log '[{"type":"result","is_error":true,"result":"Prompt is too long: 214233 tokens > 200000 maximum"}]')")" \
     "Prompt is too long: 214233 tokens"
contains "a spend failure is not a task failure and must not read as one" \
     "$(reason "$(log '[{"type":"result","is_error":true,"result":"Credit balance is too low to run this request"}]')")" \
     "Credit balance is too low"
contains "nor is an expired token, which looks identical from the issue page" \
     "$(reason "$(log '[{"type":"result","is_error":true,"result":"OAuth token has expired"}]')")" \
     "OAuth token has expired"
contains "a service failure is named by its own type field" \
     "$(reason "$(log '[{"type":"assistant","message":{"content":[{"type":"text","text":"x"}]}},{"type":"result","is_error":true,"error":{"type":"overloaded_error"}}]')")" \
     "overloaded_error"

echo
echo "WHAT MUST NOT COME OUT — the log is full of tool results"
# Each of these puts something confidential in the log AND an error next to it.
# The reason line must carry the error and nothing else. A test that only
# asserted the error was present would pass while printing the secret too, so
# every case here asserts both halves.
secretlog=$(log '[
  {"type":"user","message":{"content":[{"type":"tool_result","content":"GH_TOKEN=ghp_EXAMPLENOTAREALTOKEN0123456789 and ANTHROPIC_API_KEY=sk-ant-EXAMPLE-NOT-REAL"}]}},
  {"type":"user","message":{"content":[{"type":"tool_result","content":"-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAA\n-----END OPENSSH PRIVATE KEY-----"}]}},
  {"type":"result","subtype":"success","is_error":true,"result":"API Error: 500 internal server error"}
]')
got=$(reason "$secretlog")
contains "the error itself is reported" "$got" "API Error: 500"
lacks "a token-shaped string in a tool result stays on the runner" "$got" "ghp_"
lacks "so does an API key" "$got" "sk-ant-"
lacks "and so does a private key" "$got" "PRIVATE KEY"

# The nastier case: no recognised error at all, so the fallback runs. The
# fallback is the one path that could be tempted to print "whatever was last",
# and it must print structure instead.
quietlog=$(log '[
  {"type":"user","message":{"content":[{"type":"tool_result","content":"AWS_SECRET_ACCESS_KEY=EXAMPLEKEYMATERIAL"}]}},
  {"type":"result","subtype":"success","is_error":true,"num_turns":9,"result":"I read the file and found the credentials at line 40: EXAMPLEKEYMATERIAL"}
]')
got=$(reason "$quietlog")
contains "an unrecognised failure reports its shape" "$got" "unclassified"
lacks "and never the final message, however tempting" "$got" "EXAMPLEKEYMATERIAL"
lacks "nor anything from a tool result" "$got" "AWS_SECRET"
contains "but it does say a final message existed, which is a fact about the shape" \
     "$got" "matched no known error shape"

echo
echo "AND NOT THE TAIL OF A RECOGNISED ERROR EITHER — #106"
# The cases above put the secret somewhere the reason line never looked. #106 is
# the case where it is somewhere the reason line looked and copied: the patterns
# used to end in `[^"]{0,240}` and the WHOLE MATCH was printed, so a secret
# sitting directly after a status code went out with it. Recognising an error
# and disclosing the text around it were the same act, which is why widening the
# whitelist for T-108 was a security review rather than an afternoon's typing.
#
# Every entry in the vocabulary is checked, not just the one #106 named. The
# secret goes immediately after the prefix, where the old suffix would have
# picked it up, and the assertion is in two halves: the secret does not come out
# AND the error is still recognised. A file that printed nothing would pass the
# first half on its own.
# Each fixture is `<rule name> <the API's own words>`; the name is what the
# coverage check below compares against the script's own table, so the list
# cannot quietly fall behind it.
SECRET='ghp_EXAMPLENOTAREALTOKEN0123456789'
# shellcheck disable=SC2016  # These are the API's own words, backticks and all.
fixtures=(
  'transport_status API Error: 500'
  'transport_type \"type\":\"overloaded_error\"'
  'prompt_too_long Prompt is too long'
  'input_and_max_tokens input length and `max_tokens` exceed'
  'input_length input length exceeds'
  'max_input_tokens exceeds the maximum allowed number of input tokens'
  "output_maximum Claude's response exceeded the 32000 output token maximum"
  'credit_balance Credit balance is too low'
  'oauth_expired OAuth token has expired'
  'oauth_refused OAuth authentication failed'
  'request_timeout Request timed out'
  'request_aborted Request was aborted'
  'disk_full Error: ENOSPC'
  'heap_oom JavaScript heap out of memory'
)
covered=""
for fixture in "${fixtures[@]}"; do
  read -r rule prefix <<< "$fixture"
  covered="$covered $rule"
  got=$(reason "$(log "$(printf '[{"type":"result","is_error":true,"num_turns":4,"result":"%s %s"}]' \
                          "$prefix" "$SECRET")")")
  lacks "a secret behind \`${prefix}\` stays on the runner" "$got" "$SECRET"
  case "$got" in
    unclassified*) printf '  FAIL  %s\n     got:  %s\n' \
                     "and \`${prefix}\` is still recognised" "$got"; fail=$((fail + 1)) ;;
    *) printf '  ok    %s\n' "and \`${prefix}\` is still recognised"; pass=$((pass + 1)) ;;
  esac
done

# A fixture per condition is a guarantee only while the list is complete, and a
# hand-written list beside a hand-written table drifts on the first busy
# afternoon. T-108 exists to ADD conditions, so this is the assertion that makes
# the loop above a rule rather than a snapshot: the script's own table is read
# here, and a condition with no case turns the suite red on the commit that adds
# it rather than on the run that leaks.
# shellcheck source=/dev/null  # The path is a variable on purpose — see REASON_SH.
. "$REASON_SH"
if declare -p ATTADIPA_REASON_CONDITIONS >/dev/null 2>&1; then
  for fixture in "${ATTADIPA_REASON_CONDITIONS[@]}"; do
    read -r rule _ <<< "$fixture"
    case " $covered " in
      *" $rule "*) printf '  ok    %s\n' "the #106 loop has a case for \`$rule\`"
                   pass=$((pass + 1)) ;;
      *) printf '  FAIL  %s\n' "the #106 loop has no case for \`$rule\` — add one"
         fail=$((fail + 1)) ;;
    esac
  done
else
  printf '  FAIL  %s\n' "the script has no ATTADIPA_REASON_CONDITIONS table to check against"
  fail=$((fail + 1))
fi

# The second shape #106 gives: the secret is not in `result` but in a nested
# error message, which the old patterns reached just as easily because they were
# run over the raw bytes of the file.
got=$(reason "$(log '[{"type":"result","subtype":"success","is_error":true,"num_turns":11,
  "error":{"type":"api_error","message":"Prompt is too long AWS_SECRET_ACCESS_KEY=EXAMPLEKEYMATERIAL"}}]')")
contains "a nested error message is classified by its type" "$got" "api_error"
contains "and by its condition" "$got" "the prompt is too long"
lacks "but the message itself does not come out" "$got" "EXAMPLEKEYMATERIAL"

# The third shape, and the one that is not only a disclosure: an error prefix in
# UNRELATED tool or model text used to become the run's stated verdict, because
# the patterns were grepped over the whole file rather than over the record that
# holds the verdict. The result record here says the run merely gave up.
got=$(reason "$(log '[
  {"type":"user","message":{"content":[{"type":"tool_result",
    "content":"API Error: 500 GH_TOKEN=ghp_EXAMPLENOTAREALTOKEN0123456789"}]}},
  {"type":"assistant","message":{"content":[{"type":"text",
    "text":"I will retry: API Error: 500 sk-ant-EXAMPLE-NOT-REAL"}]}},
  {"type":"result","subtype":"success","is_error":true,"num_turns":7,"result":"I gave up"}]')")
lacks "a token quoted in a tool result is not disclosed" "$got" "ghp_"
lacks "nor one the model repeated back in its own text" "$got" "sk-ant-"
contains "and neither becomes the verdict — the result record is what failed" \
     "$got" "unclassified — SDK subtype \`success\`, ended at turn 7"

# The one path that still reads the whole file, because there is no record to
# read instead. It may name an error that belongs to some earlier line, so it
# says so — but it still cannot print anything but the classification.
got=$(reason "$(log '[{"type":"result","is_error":true,"result":"API Error: 429 GH_TOKEN=ghp_EXAMPLENOTAREALTOKEN')")
contains "a truncated log is still classified" "$got" "API Error: 429"
contains "and admits it did not read that from the result record" "$got" "not from the result record"
lacks "and still discloses nothing" "$got" "ghp_"

# `subtype` and `num_turns` are the two fields printed without being recognised
# first. They come from a record the SDK wrote, so this is not a live threat —
# it is the reason the unclassified line is safe at all, and it should hold by
# construction rather than by where the bytes happened to come from.
got=$(reason "$(log '[{"type":"result","subtype":"ghp_EXAMPLENOTAREALTOKEN0123456789",
  "is_error":true,"num_turns":"7 sk-ant-EXAMPLE-NOT-REAL","result":"x"}]')")
lacks "a subtype that is not a subtype is not printed" "$got" "ghp_"
lacks "and neither is a turn count that is not a number" "$got" "sk-ant-"
contains "the line is still produced" "$got" "unclassified"
says "a clean run's subtype survives the same check" \
     "$(reason "$(log '[{"type":"result","subtype":"success","is_error":false}]')")" \
     "the run reported no error (subtype \`success\`)"

echo
echo "Malformed input, which is what a changed action version looks like"
says "a file that is not JSON is not a crash" \
     "$(reason "$(log 'this is not json at all')")" "unclassified"
says "neither is an empty file" "$(reason "$(log '')")" "unclassified"
contains "nor is a JSON document of an entirely different shape" \
     "$(reason "$(log '{"messages":[]}')")" "unclassified"
# A truncated log is the realistic failure: the runner died mid-write.
contains "a truncated log still yields an error if the error was already written" \
     "$(reason "$(log '[{"type":"result","is_error":true,"result":"API Error: 429 rate')")" \
     "API Error: 429"

echo
echo "BOTH LOG SHAPES — this action writes one array on some runs, one object"
echo "per line on others, and neither is documented"
# .github/workflows/claude-pr-review.yml hit exactly this and reads it the same
# way. A reader that handles only the array shape reports every JSONL failure as
# unclassified, which is the quiet kind of broken: it never errors, it just
# stops knowing anything.
jsonl='{"type":"system","subtype":"init"}
{"type":"result","subtype":"error_during_execution","is_error":true,"num_turns":14,"result":"x"}'
contains "one object per line is read, not shrugged at" \
     "$(reason "$(log "$jsonl")")" \
     "SDK subtype \`error_during_execution\`, ended at turn 14"
contains "and the same content as one array gives the same answer" \
     "$(reason "$(log '[{"type":"system","subtype":"init"},{"type":"result","subtype":"error_during_execution","is_error":true,"num_turns":14,"result":"x"}]')")" \
     "SDK subtype \`error_during_execution\`, ended at turn 14"
# The LAST result record is the verdict. An earlier one belongs to a sub-session
# spawned by the Task tool, and reading it instead would report a subagent's
# clean finish as the run's outcome.
says "a sub-session's own clean result does not become the run's verdict" \
     "$(reason "$(log '[{"type":"result","subtype":"success","is_error":false,"num_turns":3},{"type":"result","subtype":"error_max_turns","is_error":true,"num_turns":200}]')")" \
     "unclassified — SDK subtype \`error_max_turns\`, ended at turn 200, with no final message at all"

echo
echo "Length, because an issue comment is not a log viewer"
long=$(printf 'API Error: 400 %s' "$(head -c 4000 /dev/zero | tr '\0' 'x')")
got=$(reason "$(log "[{\"type\":\"result\",\"is_error\":true,\"result\":\"$long\"}]")")
if [ "${#got}" -le 300 ]; then
  printf '  ok    %s\n' "a very long error is cut to something a person will read"; pass=$((pass + 1))
else
  printf '  FAIL  %s (%d chars)\n' "a very long error is cut" "${#got}"; fail=$((fail + 1))
fi
if [ "$(printf '%s' "$got" | wc -l)" -eq 0 ]; then
  printf '  ok    %s\n' "and it is one line, so a caller can read exactly one"; pass=$((pass + 1))
else
  printf '  FAIL  %s\n' "the reason must be a single line"; fail=$((fail + 1))
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
