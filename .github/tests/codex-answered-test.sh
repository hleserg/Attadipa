#!/usr/bin/env bash
# Who may close one of the other reviewer's findings, and by what act.
#
# The rule this tests replaced `select(.bot | not)` -- a human-versus-bot
# classification standing in for an authorisation. The bypass it allowed is the
# first fixture below and it is asserted as a bypass: a stranger's comment must
# NOT clear the guard. The other five come from issue #130 in the order it lists
# them, and the reply/acknowledgement cases are the other direction, because a
# rule with no passing case is a rule nobody has proved lets anything through.
#
# Every case runs the real .github/scripts/codex-answered.sh over a JSON
# document shaped exactly as pr-merge-sweep.yml assembles one. No network, no
# `gh`, no environment.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
SCRIPT_UNDER_TEST=.github/scripts/codex-answered.sh

pass=0; fail=0

HEAD_AT="2026-08-23T00:00:00Z"
CODEX="chatgpt-codex-connector[bot]"
ACK="attadipa: codex-reviewed"

# ok NAME EXPECTED HEAD RECORDS_JSON
ok() {
  local name="$1" expected="$2" head="$3" records="$4"
  local got
  got="$(printf '%s' "$records" | bash "$SCRIPT_UNDER_TEST" "$head")"
  if [ "$got" = "$expected" ]; then
    printf '  ok    %s\n' "$name"; pass=$((pass + 1))
  else
    printf '  FAIL  %s\n        expected: %s\n        got:      %s\n' "$name" "$expected" "$got"
    fail=$((fail + 1))
  fi
}

# finding KIND AT [THREAD] -- one Codex record
finding() {
  jq -nc --arg l "$CODEX" --arg k "$1" --arg at "$2" --argjson th "${3:-null}" \
    '{kind: $k, login: $l, at: $at, bot: true, permission: "unknown",
      thread: $th, body: "The parser trusts a length it never bounds."}'
}

# says KIND LOGIN AT PERMISSION BOT BODY [THREAD]
says() {
  jq -nc --arg k "$1" --arg l "$2" --arg at "$3" --arg p "$4" \
        --argjson bot "$5" --arg body "$6" --argjson th "${7:-null}" \
    '{kind: $k, login: $l, at: $at, bot: $bot, permission: $p,
      thread: $th, body: $body}'
}

# arr RECORD... -- the JSON array the workflow hands the script
arr() {
  printf '%s\n' "$@" | jq -sc '.'
}

AFTER="2026-08-23T02:00:00Z"       # after the finding and after the head
LATER="2026-08-23T03:00:00Z"
BEFORE_HEAD="2026-08-22T12:00:00Z" # before the head commit
FOUND_AT="2026-08-23T01:00:00Z"

echo "Nothing from the other reviewer"
ok "a pull request it never touched has nothing outstanding" 0 "$HEAD_AT" \
   "$(arr "$(says issue hleserg "$AFTER" admin false 'looks good')")"
ok "and an empty document is not a finding either" 0 "$HEAD_AT" '[]'
ok "nor is a head it could not read, when there is nothing to date against it" 0 "" \
   "$(arr "$(says issue hleserg "$AFTER" admin false 'looks good')")"

# THE SWEEP LEANS ON THE NEXT TWO. It runs this rule once with no permission
# resolved at all, and only spends an API call per commenter when that first
# answer is not `0` -- most pull requests never carry a finding, and this runs
# 48 times a day. That is safe exactly while `0` from an unverified document
# means "no finding" and can never mean "answered".
ok "no finding answers 0 even with nothing verified" 0 "$HEAD_AT" \
   "$(arr "$(says issue hleserg "$AFTER" unknown false "$ACK")" \
          "$(says review-comment someone "$LATER" unknown false 'fine' 4001)")"
ok "and a finding never answers 0 with nothing verified" unknown "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" unknown false "$ACK")")"

echo
echo "1. An outsider is not a reviewer — the bypass this rule was written for"
ok "a stranger's comment after the finding does NOT clear it" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue passer-by "$AFTER" none false 'ok')")"
ok "and neither does a stranger who writes the acknowledgement token" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue passer-by "$AFTER" none false "$ACK — handled")")"
ok "a read-only collaborator is still not entitled" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue reader "$AFTER" read false "$ACK — handled")")"
ok "and neither is triage, which may label and close but not merge" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue triager "$AFTER" triage false "$ACK — handled")")"

echo
echo "2. A collaborator's acknowledgement does clear it"
ok "write clears it"    0 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue dev "$AFTER" write false "$ACK — fixed in the last push")")"
ok "maintain clears it" 0 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue dev "$AFTER" maintain false "$ACK")")"
ok "admin clears it"    0 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "an acknowledgement in a review body counts too" 0 "$HEAD_AT" \
   "$(arr "$(finding review "$FOUND_AT")" \
          "$(says review hleserg "$AFTER" admin false "$ACK — not a defect, see ADR-0003")")"
ok "but a collaborator merely commenting is not an acknowledgement" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false 'thanks, merging')")"
ok "and the token inside a code span is writing about it, not doing it" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "you clear these by typing \`$ACK\`")")"
FENCE='```'
ok "nor does a fenced block containing it count" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'for example:\n%s\n%s\n%s\n' "$FENCE" "$ACK" "$FENCE")")")"
ok "the token is matched whatever case it is typed in" 0 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "Attadipa: Codex-Reviewed")")"

echo
echo "3. This repository's own output is not an answer"
ok "the hand-over's outcome comment does not clear a finding" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'github-actions[bot]' "$AFTER" unknown true "$ACK done")")"
ok "nor does the reviewer's sticky comment" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'claude[bot]' "$AFTER" unknown true "$ACK")")"
ok "a bot is refused even if a lookup calls it privileged" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'some-app[bot]' "$AFTER" admin true "$ACK")")"
ok "our own logins are refused even with the bot flag off and admin on" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'claude[bot]' "$AFTER" admin false "$ACK")")"
# Two records, both its own, and the later one carrying the token: it does not
# answer the earlier one, it is a second finding. Two outstanding, not none.
ok "and the other reviewer cannot answer itself" 2 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue "$CODEX" "$AFTER" admin true "$ACK")")"
ok "a record with no bot flag at all is not given the benefit of the doubt" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(jq -nc --arg at "$AFTER" --arg b "$ACK" \
             '{kind:"issue", login:"dev", at:$at, permission:"admin", thread:null, body:$b}')")"

echo
echo "4. A reply is bound to its thread, and only to its thread"
ok "a collaborator's reply in the finding's own thread clears it" 0 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001)" \
          "$(says review-comment hleserg "$AFTER" admin false 'bounded two lines up' 4001)")"
ok "a reply in a DIFFERENT thread does not" 1 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001)" \
          "$(says review-comment hleserg "$AFTER" admin false 'unrelated' 4002)")"
ok "an issue comment cannot be a reply to an inline finding without the token" 1 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001)" \
          "$(says issue hleserg "$AFTER" admin false 'fixed')")"
ok "but the token clears an inline finding as well" 0 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001)" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "a thread reply from an outsider clears nothing" 1 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001)" \
          "$(says review-comment passer-by "$AFTER" none false 'looks fine to me' 4001)")"
ok "a finding with a null thread is not answered by a null-threaded reply" 1 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT")" \
          "$(says review-comment hleserg "$AFTER" admin false 'fixed')")"

echo
echo "5. A permission that could not be read holds, and says so"
ok "an unverifiable answerer is unknown, not trusted" unknown "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue dev "$AFTER" unknown false "$ACK")")"
ok "and unknown is not silently none either — it does not print a count" unknown "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says review-comment dev "$AFTER" unknown false 'fixed' 4001)" \
          "$(finding review-comment "$FOUND_AT" 4001)")"
ok "a trusted answer beside an unverifiable one still clears the finding" 0 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue stranger "$AFTER" unknown false "$ACK")" \
          "$(says issue hleserg "$LATER" admin false "$ACK")")"
ok "an unverifiable commenter who says nothing binding changes nothing" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue stranger "$AFTER" unknown false 'hello')")"
ok "a missing permission field reads as unknown rather than as permission" unknown "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(jq -nc --arg at "$AFTER" --arg b "$ACK" \
             '{kind:"issue", login:"dev", at:$at, bot:false, thread:null, body:$b}')")"

echo
echo "6. An answer written before the current head answered a different tree"
ok "an acknowledgement older than the head does not clear the finding" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$BEFORE_HEAD")" \
          "$(says issue hleserg "2026-08-22T13:00:00Z" admin false "$ACK")")"
ok "and one older than the finding does not either" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$LATER")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "a reply that predates the head does not clear its thread" 1 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$BEFORE_HEAD" 4001)" \
          "$(says review-comment hleserg "2026-08-22T13:00:00Z" admin false 'done' 4001)")"
ok "an acknowledgement exactly at the head instant counts" 0 "$HEAD_AT" \
   "$(arr "$(finding issue "2026-08-22T23:00:00Z")" \
          "$(says issue hleserg "$HEAD_AT" admin false "$ACK")")"
ok "a head that could not be read holds every finding" unknown "" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "and so does a head that is not an instant" unknown "null" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "a head in some other shape is not compared lexicographically and hoped for" unknown "2026-08-23" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"

echo
echo "Several findings are counted, not collapsed"
ok "two findings and one acknowledgement leaves none — the token is not per-finding" 0 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(finding review "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "two findings, one answered in its thread, one not" 1 "$HEAD_AT" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001)" \
          "$(finding review-comment "$FOUND_AT" 4002)" \
          "$(says review-comment hleserg "$AFTER" admin false 'fixed' 4001)")"
ok "three unanswered findings count as three" 3 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(finding review "$FOUND_AT")" \
          "$(finding review-comment "$FOUND_AT" 4001)")"
ok "a later finding is not answered by the acknowledgement of an earlier one" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")" \
          "$(finding issue "$LATER")")"

echo
echo "Timestamps that are not instants"
ok "a finding with no timestamp cannot be dated, so it holds" unknown "$HEAD_AT" \
   "$(arr "$(jq -nc --arg l "$CODEX" '{kind:"issue", login:$l, at:null, bot:true,
                                        permission:"unknown", thread:null, body:"x"}')" \
          "$(says issue hleserg "$AFTER" admin false "$ACK")")"
ok "an answer with an unparseable timestamp is not an answer" 1 "$HEAD_AT" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "yesterday" admin false "$ACK")")"
ok "and malformed input as a whole is unknown, never zero" unknown "$HEAD_AT" 'not json at all'
ok "an empty document on stdin is unknown, not nothing outstanding" unknown "$HEAD_AT" ''

echo
echo "The rule does not lose a condition"
for condition in 'ATTADIPA_CODEX_LOGINS' 'ATTADIPA_CODEX_NEVER_ANSWERS' \
                 'ATTADIPA_CODEX_ACK' 'ATTADIPA_CODEX_ANSWER_PERMISSIONS' \
                 'attadipa_strip_code' 'in_reply_to_id'; do
  if grep -q -- "$condition" "$SCRIPT_UNDER_TEST"; then
    printf '  ok    %s is still there\n' "$condition"; pass=$((pass + 1))
  else
    printf '  FAIL  %s is gone\n' "$condition"; fail=$((fail + 1))
  fi
done

# The three permissions, and only those three. `triage` and `read` reaching the
# list would be a widening nobody would see in a diff of the rule's prose.
if grep -q '"admin","maintain","write"' "$SCRIPT_UNDER_TEST" \
   && ! grep -q 'ATTADIPA_CODEX_ANSWER_PERMISSIONS=.*triage' "$SCRIPT_UNDER_TEST"; then
  printf '  ok    only write, maintain and admin may answer\n'; pass=$((pass + 1))
else
  printf '  FAIL  the permissions that may answer have changed\n'; fail=$((fail + 1))
fi

# The workflow has to keep passing what the rule reads. A gathering step that
# stops collecting `in_reply_to_id`, or stops looking a permission up, turns
# every answer into `unknown` -- which holds, so nothing would go wrong loudly.
SWEEP=.github/workflows/pr-merge-sweep.yml
# shellcheck disable=SC2016  # $LOGIN and $PR are the workflow's own shell
# variables and are being searched for literally, not expanded here.
for gathered in 'in_reply_to_id' 'collaborators/\$LOGIN/permission' \
                'codex-answered.sh' 'pulls/\$PR/comments'; do
  if grep -q -- "$gathered" "$SWEEP"; then
    printf '  ok    the sweep still gathers %s\n' "$gathered"; pass=$((pass + 1))
  else
    printf '  FAIL  the sweep no longer gathers %s\n' "$gathered"; fail=$((fail + 1))
  fi
done

# And the shape that started this: a trust test made of `.bot` alone. Comment
# lines are dropped first -- the workflow quotes the old expression while
# explaining why it was wrong, and a test that cannot tell an explanation from
# an implementation would either fail on the explanation or force it to be
# deleted. The comment is the most valuable line in the block.
if grep -v '^[[:space:]]*#' "$SWEEP" | grep -q 'select(.bot | not)'; then
  printf '  FAIL  the sweep is back to treating non-bot as an authorisation\n'; fail=$((fail + 1))
else
  printf '  ok    the sweep no longer reads non-bot as an authorisation\n'; pass=$((pass + 1))
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
