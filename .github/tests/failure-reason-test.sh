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

reason() { bash .github/scripts/failure-reason.sh "$1"; }

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
