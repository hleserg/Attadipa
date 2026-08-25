#!/usr/bin/env bash
# Who may close one of the other reviewer's findings, by what act, and about
# which tree.
#
# The rule this tests replaced `select(.bot | not)` -- a human-versus-bot
# classification standing in for an authorisation. The bypass it allowed is the
# first fixture below and it is asserted as a bypass: a stranger's comment must
# NOT clear the guard.
#
# TWO MORE BYPASSES SURVIVED THAT ROUND and #130 was reopened for them, so they
# are asserted here in the same way -- as the thing that must not happen:
#
#   * an acknowledgement written about commit A cleared a finding on commit B,
#     because the binding to the head was a TIMESTAMP. Move a pull request onto
#     a commit that already existed and the head's stamp goes backwards, and an
#     older acknowledgement clears it. Section 6;
#   * the acknowledgement was recognised by "does this string appear once code
#     has been stripped", over a stripper that knows two forms out of markdown's
#     many. A ``double-backtick`` span, four spaces of indent, an HTML comment,
#     a `> ` quotation and the token mid-sentence all cleared a finding on
#     `main@36e1ba9`. Section 7.
#
# Every case runs the real .github/scripts/codex-answered.sh over a JSON
# document shaped exactly as pr-merge-sweep.yml assembles one. No network, no
# `gh`, no environment.
#
# AND THE LAST SECTION RUNS THE RULE WITH ITS PROPERTIES BROKEN ONE AT A TIME.
# A suite of fixtures proves the rule answers correctly today; it does not prove
# any single line of the rule is load-bearing. Eight mutants, each reverting one
# property to the shape that shipped the defect, and each must make an assertion
# above go red.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
SCRIPT_UNDER_TEST=.github/scripts/codex-answered.sh

pass=0; fail=0

# Two object ids that are not each other. A is the head everything below is
# decided against; B is the commit the pull request used to be on, or moves to.
HEAD_A="4038850c2a4b1f0e9d7c6b5a49382716f0e5d4c3"
HEAD_B="945c16aefd0b3c27185ae9f4d6b0c81723ae5f90"
CODEX="chatgpt-codex-connector[bot]"

# ack [OID] -- the acknowledgement as a collaborator would type it.
ack() { printf 'attadipa: codex-reviewed %s' "${1-$HEAD_A}"; }

# ok NAME EXPECTED HEAD_OID RECORDS_JSON
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

# finding KIND AT [THREAD] [COMMIT_OID] -- one Codex record
finding() {
  jq -nc --arg l "$CODEX" --arg k "$1" --arg at "$2" --argjson th "${3:-null}" \
        --arg oid "${4-}" \
    '{kind: $k, login: $l, at: $at, bot: true, permission: "unknown",
      thread: $th, commit_oid: (if $oid == "" then null else $oid end),
      body: "The parser trusts a length it never bounds."}'
}

# says KIND LOGIN AT PERMISSION BOT BODY [THREAD] [COMMIT_OID]
says() {
  jq -nc --arg k "$1" --arg l "$2" --arg at "$3" --arg p "$4" \
        --argjson bot "$5" --arg body "$6" --argjson th "${7:-null}" \
        --arg oid "${8-}" \
    '{kind: $k, login: $l, at: $at, bot: $bot, permission: $p,
      thread: $th, commit_oid: (if $oid == "" then null else $oid end),
      body: $body}'
}

# arr RECORD... -- the JSON array the workflow hands the script
arr() {
  printf '%s\n' "$@" | jq -sc '.'
}

AFTER="2026-08-23T02:00:00Z"       # after the finding
LATER="2026-08-23T03:00:00Z"
EARLIER="2026-08-22T12:00:00Z"
FOUND_AT="2026-08-23T01:00:00Z"

echo "Nothing from the other reviewer"
ok "a pull request it never touched has nothing outstanding" 0 "$HEAD_A" \
   "$(arr "$(says issue hleserg "$AFTER" admin false 'looks good')")"
ok "and an empty document is not a finding either" 0 "$HEAD_A" '[]'
ok "nor is a head it could not read, when there is nothing to bind to it" 0 "" \
   "$(arr "$(says issue hleserg "$AFTER" admin false 'looks good')")"

# THE SWEEP LEANS ON THE NEXT TWO. It runs this rule once with no permission
# resolved at all, and only spends an API call per commenter when that first
# answer is not `0` -- most pull requests never carry a finding, and this runs
# 48 times a day. That is safe exactly while `0` from an unverified document
# means "no finding" and can never mean "answered".
ok "no finding answers 0 even with nothing verified" 0 "$HEAD_A" \
   "$(arr "$(says issue hleserg "$AFTER" unknown false "$(ack)")" \
          "$(says review-comment someone "$LATER" unknown false 'fine' 4001)")"
ok "and a finding never answers 0 with nothing verified" unknown "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" unknown false "$(ack)")")"

echo
echo "1. An outsider is not a reviewer — the bypass this rule was written for"
ok "a stranger's comment after the finding does NOT clear it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue passer-by "$AFTER" none false 'ok')")"
ok "and neither does a stranger who writes the acknowledgement" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue passer-by "$AFTER" none false "$(ack)")")"
ok "a read-only collaborator is still not entitled" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue reader "$AFTER" read false "$(ack)")")"
ok "and neither is triage, which may label and close but not merge" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue triager "$AFTER" triage false "$(ack)")")"

echo
echo "2. A collaborator's acknowledgement of this head does clear it"
ok "write clears it"    0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue dev "$AFTER" write false \
             "$(printf 'Fixed in the last push.\n\n%s\n' "$(ack)")")")"
ok "maintain clears it" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue dev "$AFTER" maintain false "$(ack)")")"
ok "admin clears it"    0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "an acknowledgement in a review body counts too" 0 "$HEAD_A" \
   "$(arr "$(finding review "$FOUND_AT")" \
          "$(says review hleserg "$AFTER" admin false \
             "$(printf 'Not a defect, see ADR-0003.\n\n%s\n' "$(ack)")")")"
ok "but a collaborator merely commenting is not an acknowledgement" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false 'thanks, merging')")"
ok "the acknowledgement is matched whatever case it is typed in" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "ATTADIPA: Codex-Reviewed ${HEAD_A^^}")")"

echo
echo "3. This repository's own output is not an answer"
ok "the hand-over's outcome comment does not clear a finding" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'github-actions[bot]' "$AFTER" unknown true "$(ack)")")"
ok "nor does the reviewer's sticky comment" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'claude[bot]' "$AFTER" unknown true "$(ack)")")"
ok "a bot is refused even if a lookup calls it privileged" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'some-app[bot]' "$AFTER" admin true "$(ack)")")"
ok "our own logins are refused even with the bot flag off and admin on" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue 'claude[bot]' "$AFTER" admin false "$(ack)")")"
# Two records, both its own, and the later one carrying the acknowledgement: it
# does not answer the earlier one, it is a second finding. Two outstanding.
ok "and the other reviewer cannot answer itself" 2 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue "$CODEX" "$AFTER" admin true "$(ack)")")"
ok "a record with no bot flag at all is not given the benefit of the doubt" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(jq -nc --arg at "$AFTER" --arg b "$(ack)" \
             '{kind:"issue", login:"dev", at:$at, permission:"admin", thread:null,
               commit_oid:null, body:$b}')")"

echo
echo "4. A reply is bound to its thread, and to the tree that thread is about"
ok "a collaborator's reply in the finding's own thread clears it" 0 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says review-comment hleserg "$AFTER" admin false 'bounded two lines up' 4001 "$HEAD_A")")"
ok "a reply in a DIFFERENT thread does not" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says review-comment hleserg "$AFTER" admin false 'unrelated' 4002 "$HEAD_A")")"
ok "an issue comment cannot be a reply to an inline finding without the acknowledgement" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says issue hleserg "$AFTER" admin false 'fixed')")"
ok "but the acknowledgement clears an inline finding as well" 0 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "a thread reply from an outsider clears nothing" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says review-comment passer-by "$AFTER" none false 'looks fine to me' 4001 "$HEAD_A")")"
ok "a finding with a null thread is not answered by a null-threaded reply" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" '' "$HEAD_A")" \
          "$(says review-comment hleserg "$AFTER" admin false 'fixed' '' "$HEAD_A")")"

echo
echo "5. A permission that could not be read holds, and says so"
ok "an unverifiable answerer is unknown, not trusted" unknown "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue dev "$AFTER" unknown false "$(ack)")")"
ok "and unknown is not silently none either — it does not print a count" unknown "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says review-comment dev "$AFTER" unknown false 'fixed' 4001 "$HEAD_A")")"
ok "a trusted answer beside an unverifiable one still clears the finding" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue stranger "$AFTER" unknown false "$(ack)")" \
          "$(says issue hleserg "$LATER" admin false "$(ack)")")"
ok "an unverifiable commenter who says nothing binding changes nothing" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue stranger "$AFTER" unknown false 'hello')")"
ok "a missing permission field reads as unknown rather than as permission" unknown "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(jq -nc --arg at "$AFTER" --arg b "$(ack)" \
             '{kind:"issue", login:"dev", at:$at, bot:false, thread:null,
               commit_oid:null, body:$b}')")"

echo
echo "6. An answer is about one tree, and the tree is named by its object id"
# THE REOPENED HALF OF #130, FIRST PATH. Every fixture in this block passed
# under the timestamp rule, including the two that are bypasses.
ok "an acknowledgement naming the previous head does not clear a finding on this one" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack "$HEAD_B")")")"
ok "and moving the pull request to a commit that already existed does not carry it over" 1 "$HEAD_B" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack "$HEAD_A")")")"
# The reproduction from the issue, in its own shape: the acknowledgement is
# LATER than everything, and it still does not answer for a tree it never named.
ok "a backdated head does not make the newest acknowledgement apply to it" 1 "$HEAD_B" \
   "$(arr "$(finding issue "$EARLIER")" \
          "$(says issue hleserg "$LATER" admin false "$(ack "$HEAD_A")")")"
ok "an acknowledgement with no object id at all clears nothing" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false 'attadipa: codex-reviewed')")"
ok "nor does an abbreviated one — a prefix is a question, an object id is not" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "attadipa: codex-reviewed ${HEAD_A:0:12}")")"
ok "nor one whose object id is malformed" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false 'attadipa: codex-reviewed not-an-object-id')")"
ok "a head that could not be read holds every finding" unknown "" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "and so does a head that is a timestamp — the argument the old caller passed" unknown "2026-08-23T00:00:00Z" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "and so does an abbreviated head" unknown "${HEAD_A:0:12}" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "and so does a head that is not hexadecimal" unknown "zzzz850c2a4b1f0e9d7c6b5a49382716f0e5d4c3" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "an acknowledgement older than the finding is not an answer to it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$LATER")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
# The reply route carries no text, so its tree comes from GitHub's own record of
# which commit the comment was written against.
ok "a reply written against the previous head does not clear its thread" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says review-comment hleserg "$AFTER" admin false 'done' 4001 "$HEAD_B")")"
ok "and neither does a reply to a finding raised against a tree that is no longer head" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_B")" \
          "$(says review-comment hleserg "$AFTER" admin false 'done' 4001 "$HEAD_B")")"
ok "a reply with no recorded commit is not an answer" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(says review-comment hleserg "$AFTER" admin false 'done' 4001)")"
ok "an acknowledgement of the current head still clears a stale thread's finding" 0 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_B")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"

echo
echo "7. The acknowledgement is a line somebody wrote, not a string that occurs"
# THE REOPENED HALF OF #130, SECOND PATH. Five of these six returned ACCEPT on
# `main@36e1ba9` -- run, not read -- so a collaborator quoting the
# acknowledgement while explaining it gave it. Codex reported this on #219 and
# the merged implementation did not close it.
FENCE='```'
ok "a single-backtick span is writing about it, not doing it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "you clear these by typing \`$(ack)\`")")"
ok "a double-backtick span does not do it either" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "the form is \`\`$(ack)\`\`")")"
ok "nor a span of three backticks on one line" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "\`\`\`$(ack)\`\`\`")")"
ok "nor a fenced block containing it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'for example:\n%s\n%s\n%s\n' "$FENCE" "$(ack)" "$FENCE")")")"
ok "nor a fenced block indented up to three spaces, which is still a fence" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'for example:\n   %s\n   %s\n   %s\n' "$FENCE" "$(ack)" "$FENCE")")")"
ok "nor a tilde run inside a backtick fence, which is content and not a closer" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf '%s\n~~~\n%s\n~~~\n%s\n' "$FENCE" "$(ack)" "$FENCE")")")"
ok "nor an indented code block" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'like this:\n\n    %s\n' "$(ack)")")")"
ok "nor a tab-indented one" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'like this:\n\n\t%s\n' "$(ack)")")")"
ok "nor an HTML comment, which the person acknowledging cannot even see" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "<!-- $(ack) -->")")"
ok "nor one spread over several lines" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf '<!--\n%s\n-->\n' "$(ack)")")")"
ok "an unterminated HTML comment takes the rest of the body with it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'see below\n<!--\n%s\n' "$(ack)")")")"
ok "nor a quotation of somebody else saying it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "> $(ack)")")"
# LAZY CONTINUATION. The second line here carries no `>` and still renders
# inside the quotation, which is CommonMark's rule for a paragraph continuing a
# block. Refusing only the lines that begin with a marker refuses the marker and
# not the quotation.
ok "nor the line after a quotation, which markdown renders inside it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf '> Somebody wrote:\n%s\n' "$(ack)")")")"
ok "but a blank line ends the quotation, and what follows is the writer's own" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf '> Somebody wrote:\n> the finding\n\n%s\n' "$(ack)")")")"
ok "nor the same in the middle of a sentence" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "they keep typing $(ack) at me")")"
ok "nor a line with anything else on it" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack) — done")")"
ok "nor a list item" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "- $(ack)")")"
# And the other direction, because a recogniser with no accepting case is a
# recogniser nobody has proved lets anything through.
ok "a line of its own after a closed fence is an acknowledgement" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false \
             "$(printf 'the finding quoted:\n%s\nx\n%s\n\n%s\n' "$FENCE" "$FENCE" "$(ack)")")")"
ok "three spaces of indent is still prose" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "   $(ack)")")"
ok "and trailing whitespace does not spoil one" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)   ")")"
ok "nor do carriage returns" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(printf 'fixed\r\n\r\n%s\r\n' "$(ack)")")")"

echo
echo "Several findings are counted, not collapsed"
ok "two findings and one acknowledgement leaves none — it is not per-finding" 0 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(finding review "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "two findings, one answered in its thread, one not" 1 "$HEAD_A" \
   "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")" \
          "$(finding review-comment "$FOUND_AT" 4002 "$HEAD_A")" \
          "$(says review-comment hleserg "$AFTER" admin false 'fixed' 4001 "$HEAD_A")")"
ok "three unanswered findings count as three" 3 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(finding review "$FOUND_AT")" \
          "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_A")")"
ok "a later finding is not answered by the acknowledgement of an earlier one" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")" \
          "$(finding issue "$LATER")")"

echo
echo "Timestamps that are not instants"
ok "a finding with no timestamp cannot be dated, so it holds" unknown "$HEAD_A" \
   "$(arr "$(jq -nc --arg l "$CODEX" '{kind:"issue", login:$l, at:null, bot:true,
                                        permission:"unknown", thread:null,
                                        commit_oid:null, body:"x"}')" \
          "$(says issue hleserg "$AFTER" admin false "$(ack)")")"
ok "an answer with an unparseable timestamp is not an answer" 1 "$HEAD_A" \
   "$(arr "$(finding issue "$FOUND_AT")" \
          "$(says issue hleserg "yesterday" admin false "$(ack)")")"
ok "and malformed input as a whole is unknown, never zero" unknown "$HEAD_A" 'not json at all'
ok "an empty document on stdin is unknown, not nothing outstanding" unknown "$HEAD_A" ''

echo
echo "The caller's shape is part of the rule"
# A rule that guesses at its own inputs is the shape merge-candidate.sh refuses
# by arity, for the reason it gives: the caller and the rule are edited by
# different hands at different times, and the sweep's half cannot be pushed by
# an agent at all.
for argv in "" "$HEAD_A extra"; do
  # shellcheck disable=SC2086  # deliberately unquoted: this is an argv shape
  got="$(printf '[]' | bash "$SCRIPT_UNDER_TEST" $argv)"
  if [ "$got" = "unknown" ]; then
    printf '  ok    a caller passing %d arguments is refused, not guessed at\n' \
      "$(printf '%s' "$argv" | wc -w)"
    pass=$((pass + 1))
  else
    printf '  FAIL  a caller passing %d arguments got %s rather than unknown\n' \
      "$(printf '%s' "$argv" | wc -w)" "$got"
    fail=$((fail + 1))
  fi
done

echo
echo "The rule does not lose a condition"
for condition in 'ATTADIPA_CODEX_LOGINS' 'ATTADIPA_CODEX_NEVER_ANSWERS' \
                 'ATTADIPA_CODEX_ACK' 'ATTADIPA_CODEX_ANSWER_PERMISSIONS' \
                 'attadipa_codex_oid' 'attadipa_codex_uncomment' \
                 'attadipa_codex_acknowledges' 'in_reply_to_id'; do
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

# AND IT DOES NOT BORROW THE INTAKE GATE'S STRIPPER AGAIN. `attadipa_strip_code`
# answers "is there an @claude in this comment", where the string is a mention
# inside a sentence; it removes fences that start in column one and
# single-backtick pairs, and nothing else. Reusing it here is what let five
# markdown forms give an acknowledgement. Sourcing intake-decision.sh at all is
# the first step back to it.
# Comment lines dropped first: the rule explains at length WHY it no longer
# borrows that function, and a scan that cannot tell an explanation from a call
# would force the explanation to be deleted.
if grep -vE '^[[:space:]]*#' "$SCRIPT_UNDER_TEST" \
   | grep -q 'attadipa_strip_code\|intake-decision.sh'; then
  printf '  FAIL  the rule is borrowing the intake gate stripper again\n'; fail=$((fail + 1))
else
  printf '  ok    the acknowledgement has a recogniser of its own\n'; pass=$((pass + 1))
fi

echo
echo "The caller gathers what the rule reads"
# A gathering step that stops collecting `in_reply_to_id`, stops looking a
# permission up, or stops passing the head's object id turns every answer into
# `unknown` -- which holds, so nothing would go wrong loudly.
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

# THE TWO FIELDS THIS ROUND ADDED ARE IN THE HALF AN AGENT CANNOT PUSH.
# `claude[bot]` holds no `workflows` permission -- verified on 2026-08-24, and
# the refusal is quoted at the head of the parked patch -- so the sweep's edits
# travel in docs/automation/pending/170-merge-sweep-completeness.patch, folded
# into the one patch that already edits this workflow rather than beside it.
# There are exactly two states this repository may be in and this asserts it is
# in one of them: the sweep already passes the object id and gathers
# `original_commit_id`, or the patch that makes it do so is here and applies.
# The third state -- a rule that reads fields nothing supplies, and nothing on
# disk that remembers they are meant to meet -- is what this refuses.
PENDING=docs/automation/pending/170-merge-sweep-completeness.patch
# Comment lines dropped first: the patch's own prose quotes the call it adds,
# and a scan of the whole file would be satisfied by an explanation.
SWEEP_BODY="$(grep -vE '^[[:space:]]*#' "$SWEEP" 2>/dev/null)"
# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
CODEX_CALL="$(printf '%s\n' "$SWEEP_BODY" | grep -c 'codex-answered.sh "\$HEAD_OID"' || true)"
# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
if [ "$CODEX_CALL" -gt 0 ]; then
  printf '  ok    the sweep passes the head object id to the rule\n'; pass=$((pass + 1))
  if printf '%s\n' "$SWEEP_BODY" | grep -q 'original_commit_id'; then
    printf '  ok    and gathers the commit each review comment was written against\n'
    pass=$((pass + 1))
  else
    printf '  FAIL  the sweep passes the head but gathers no original_commit_id, so no reply can ever answer\n'
    fail=$((fail + 1))
  fi
elif [ -f "$PENDING" ]; then
  if grep -q 'codex-answered.sh "\$HEAD_OID"' "$PENDING" \
     && grep -q 'original_commit_id' "$PENDING"; then
    printf '  ok    the sweep does not pass it yet, and %s carries both edits\n' "$PENDING"
    pass=$((pass + 1))
  else
    printf '  FAIL  %s does not carry the codex caller edits, so they are parked nowhere\n' "$PENDING"
    fail=$((fail + 1))
  fi
  if git --no-pager apply --check "$PENDING" >/dev/null 2>&1; then
    printf '  ok    and that patch still applies cleanly to this tree\n'; pass=$((pass + 1))
  else
    printf '  FAIL  %s no longer applies; the parked half has rotted\n' "$PENDING"
    fail=$((fail + 1))
  fi
  # The live caller still passes a timestamp, which this rule refuses as a head
  # -- so the sweep holds every pull request carrying a finding until the patch
  # lands. That is the intended state and it must be a stated one.
  if printf '%s\n' "$SWEEP_BODY" | grep -q 'codex-answered.sh "\$COMMITTED"'; then
    printf '  ok    and the live caller is still the one the patch replaces\n'; pass=$((pass + 1))
  else
    printf '  FAIL  the patch is parked but the live caller is neither shape\n'; fail=$((fail + 1))
  fi
else
  printf '  FAIL  the rule reads a head object id that nothing passes and no patch is pending\n'
  fail=$((fail + 1))
fi

# And the shape that started this: a trust test made of `.bot` alone. Comment
# lines are dropped first -- the workflow quotes the old expression while
# explaining why it was wrong, and a test that cannot tell an explanation from
# an implementation would either fail on the explanation or force it to be
# deleted. The comment is the most valuable line in the block.
if printf '%s\n' "$SWEEP_BODY" | grep -q 'select(.bot | not)'; then
  printf '  FAIL  the sweep is back to treating non-bot as an authorisation\n'; fail=$((fail + 1))
else
  printf '  ok    the sweep no longer reads non-bot as an authorisation\n'; pass=$((pass + 1))
fi

echo
echo "Break one property at a time — every mutant must be caught"
# WHAT THIS ADDS OVER THE FIXTURES ABOVE. They prove the rule answers correctly
# as it stands. They do not prove that any particular line of it is why. Each
# mutant below reverts exactly one property to the shape that shipped a defect,
# and must change an answer that the fixtures assert -- if it does not, that
# property has no test and the next edit to it is unguarded.
MUT="$(mktemp -d)"
trap 'rm -rf "$MUT"' EXIT

# mutant NAME SED_EXPR SAFE_ANSWER HEAD RECORDS
mutant() {
  local name="$1" expr="$2" safe="$3" head="$4" records="$5"
  local file="$MUT/mutant.sh" real got
  sed "$expr" "$SCRIPT_UNDER_TEST" >"$file"
  if cmp -s "$file" "$SCRIPT_UNDER_TEST"; then
    printf '  FAIL  %s: the mutation changed nothing, so it tested nothing\n' "$name"
    fail=$((fail + 1)); return
  fi
  real="$(printf '%s' "$records" | bash "$SCRIPT_UNDER_TEST" "$head")"
  got="$(printf '%s' "$records" | bash "$file" "$head")"
  if [ "$real" != "$safe" ]; then
    printf '  FAIL  %s: the unmutated rule answered %s, not %s\n' "$name" "$real" "$safe"
    fail=$((fail + 1)); return
  fi
  if [ "$got" = "$safe" ]; then
    printf '  FAIL  %s: the mutant still answers %s, so nothing here tests it\n' "$name" "$safe"
    fail=$((fail + 1))
  else
    printf '  ok    %s (mutant answers %s)\n' "$name" "$got"; pass=$((pass + 1))
  fi
}

# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "the acknowledgement stops naming the head" \
  's|want="$ATTADIPA_CODEX_ACK $head"|want="$ATTADIPA_CODEX_ACK"|' \
  1 "$HEAD_A" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false 'attadipa: codex-reviewed')")"

# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "an unreadable head stops holding" \
  's|elif $head == "" then "unknown"|elif false then "unknown"|' \
  unknown "" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false "$(ack)")")"

# A line that merely CONTAINS the acknowledgement, as the rule this replaced
# asked. No backticks in the fixture on purpose: the backtick refusal above it
# would discard a code span before the comparison ever ran, so a span cannot
# show what the comparison itself is for.
# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "the line has to be the acknowledgement, not contain it" \
  's|if \[ "${stripped,,}" = "$want" \]; then|case "${stripped,,}" in *"$want"*) return 0 ;; esac; if false; then|' \
  1 "$HEAD_A" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false "they keep typing $(ack) at me")")"

# The multi-line form, for the same reason: `<!-- ack -->` on one line is
# already refused by the exact-line comparison, so it cannot show what removing
# the comment is for. Spread over three lines, the middle one IS the
# acknowledgement, and only the removal keeps it invisible.
# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "HTML comments become visible again" \
  's|$(attadipa_codex_uncomment "${1-}")|${1-}|' \
  1 "$HEAD_A" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false "$(printf '<!--\n%s\n-->\n' "$(ack)")")")"

# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "indented code becomes prose" \
  's@^    \[ "$indent" -le 3 \] || continue$@    :@' \
  1 "$HEAD_A" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false "$(printf 'like this:\n\n    %s\n' "$(ack)")")")"

# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "a fence closes on any run of three, whatever opened it" \
  's|if \[ "$c" = "$fence_char" \] && \[ "$run" -ge "$fence_len" \] && \[ -z "$tail" \]; then|if true; then|' \
  1 "$HEAD_A" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false \
            "$(printf '%s\n~~~\n%s\n~~~\n%s\n' "$FENCE" "$(ack)" "$FENCE")")")"

# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "a quotation ends at the marker rather than at a blank line" \
  's@quoted=yes; continue ;;@continue ;;@' \
  1 "$HEAD_A" \
  "$(arr "$(finding issue "$FOUND_AT")" \
         "$(says issue hleserg "$AFTER" admin false \
            "$(printf '> Somebody wrote:\n%s\n' "$(ack)")")")"

# The finding was raised against a tree that is no longer head and the reply was
# written against the head, so only the FINDING's binding refuses this one --
# which is the condition the mutant removes.
# shellcheck disable=SC2016  # a literal $ in a pattern, not an expansion
mutant "a reply stops being bound to the tree its thread is about" \
  's@and (($f | oid) == $head)@and true@' \
  1 "$HEAD_A" \
  "$(arr "$(finding review-comment "$FOUND_AT" 4001 "$HEAD_B")" \
         "$(says review-comment hleserg "$AFTER" admin false 'done' 4001 "$HEAD_A")")"

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
