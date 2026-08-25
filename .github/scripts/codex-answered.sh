#!/usr/bin/env bash
# How many of the other reviewer's findings on this pull request are still
# unanswered -- where "answered" means somebody ENTITLED to answer them did,
# about THIS finding, on THIS head commit.
#
# WHY THIS FILE EXISTS. The rule it replaces was four lines of `jq` inside
# pr-merge-sweep.yml, and it read, in full:
#
#     [$all[] | select(.bot | not) | select(.at > $last)] | length
#
# -- the last Codex comment is answered if ANY later comment was written by a
# non-bot. `user.type != "Bot"` was doing the work of an authorisation check,
# and it is not one. This repository is public: anybody with a GitHub account
# can comment on a pull request, and that comment cleared the only condition
# that accounted for a Codex finding arriving outside a review thread. With
# `ai-review:pass` set and the other guards satisfied, a stranger typing "ok"
# was the last thing between an unreviewed finding and an unattended merge to
# `main`. Filed as issue #130.
#
# Human-versus-bot is a classification. Write access is an authorisation. Every
# other gate in this repository already knows the difference --
# intake-decision.sh rule 2 refuses on `permission`, not on `type` -- and this
# one now does too.
#
# THREE THINGS HAVE TO HOLD, and each was absent before:
#
#   1. WHO. The answerer holds `write`, `maintain` or `admin` on this
#      repository, looked up through the collaborators API by the caller. Not
#      `author_association`: `COLLABORATOR` is granted to anyone invited to the
#      repository, read-only included, and `CONTRIBUTOR` merely means a merged
#      commit. Not `bot == false`, which is what was there.
#   2. WHAT ABOUT. An answer is bound to the finding, not merely later than it.
#      GitHub gives a real reply relationship for review comments -- they carry
#      `in_reply_to_id`, so a thread is a fact rather than a guess -- and gives
#      NOTHING for issue comments and review bodies, which is exactly the shape
#      #130 is about. For those, the binding has to be written down: the
#      acknowledgement token below, in the body, outside code.
#   3. WHEN. At or after both the finding and the head commit. A verdict
#      reached on commit A says nothing about commit B, which is the same
#      argument `ai-review:pass` already carries in merge-candidate.sh, applied
#      to the same kind of stale evidence.
#
# It fails closed. Anything it could not establish -- a permission it could not
# read, a timestamp it could not parse, a head it was not given -- prints
# `unknown`, and merge-candidate.sh holds on `unknown` with a line of its own.
# "Could not tell" and "nothing outstanding" are different answers and must not
# print the same word.
#
# No network and no environment, for the reason merge-candidate.sh and
# intake-decision.sh give: a rule embedded in a workflow cannot be executed, so
# it cannot be tested, so every defect it has ships. The caller gathers the
# facts; .github/tests/codex-answered-test.sh runs the real file.

set -uo pipefail

# The other reviewer's own logins. NOT read from ATTADIPA_TRUSTED_PRODUCERS,
# though that variable currently names this same login: that list answers "who
# may file a task", and this answers "whose findings block a merge". Two
# boundaries that happen to share a value today are still two boundaries, and
# joining them means widening one silently widens the other.
ATTADIPA_CODEX_LOGINS='["chatgpt-codex-connector[bot]"]'

# Logins that can never answer anything, whatever a permission lookup says.
# This repository's own output must not authorise this repository's own writes
# -- the hand-over's outcome comment, the reviewer's sticky comment and this
# sweep's own notes are all written by one of these, and all three counted as
# an answer under the old rule.
ATTADIPA_CODEX_NEVER_ANSWERS='["github-actions[bot]","claude[bot]","github-actions","claude"]'

# The acknowledgement token. Visible in the rendered thread on purpose -- an
# HTML comment would be invisible to the person the acknowledgement is for --
# and long enough that nobody types it by accident.
#
# A LABEL WAS THE OTHER CANDIDATE and was rejected: adding a label that is
# already present raises no event at all, so re-acknowledging a second finding
# would need a remove-then-add nobody would guess at. AI_TASK_PROTOCOL.md
# documents that exact trap for `agent:ready`; one is enough. A comment always
# carries a fresh timestamp, and it can carry the reasoning as well.
ATTADIPA_CODEX_ACK="attadipa: codex-reviewed"

# The permissions that may answer. The same three intake-decision.sh accepts.
# `triage` is deliberately not among them: a triage collaborator may label and
# close, which is why the label route was not taken either.
ATTADIPA_CODEX_ANSWER_PERMISSIONS='["admin","maintain","write"]'

# attadipa_strip_code, reused rather than copied. See the note on it in
# intake-decision.sh: a token inside a code span is somebody writing about the
# token, and this repository has been bitten by that twice in one day.
# shellcheck source=.github/scripts/intake-decision.sh
. "$(dirname "${BASH_SOURCE[0]}")/intake-decision.sh"

# attadipa_codex_acknowledges BODY -- 0 when BODY is an acknowledgement.
attadipa_codex_acknowledges() {
  local stripped
  stripped="$(attadipa_strip_code "${1-}")"
  case "${stripped,,}" in
    *"$ATTADIPA_CODEX_ACK"*) return 0 ;;
  esac
  return 1
}

# attadipa_codex_answered HEAD_TIMESTAMP RECORDS_JSON
#
# HEAD_TIMESTAMP  the head commit's `pushedDate`, falling back to
#                 `committedDate` -- the same value merge-candidate.sh's
#                 settling window is measured from. `null`, empty or any shape
#                 that is not an ISO-8601 UTC instant means the head could not
#                 be established, which is `unknown` whenever there is a
#                 finding to date against it.
#
# RECORDS_JSON    every comment, review body and inline review comment on the
#                 pull request, as one JSON array. Each record:
#
#                   kind        "issue" | "review" | "review-comment"
#                   login       the author's GitHub login
#                   at          ISO-8601 UTC instant, e.g. 2026-08-23T05:57:52Z
#                   bot         true when `user.type == "Bot"`
#                   permission  "admin"|"maintain"|"write"|"read"|"triage"
#                               |"none"|"unknown" -- looked up by the caller
#                   thread      for "review-comment", `in_reply_to_id // id`,
#                               which is GitHub's own thread root; null
#                               otherwise
#                   body        the comment text, verbatim
#
# Prints exactly one line:
#   <n>        that many of the other reviewer's findings have no valid answer
#   unknown    at least one finding could not be decided either way
attadipa_codex_answered() {
  local head="${1-}" records="${2-}"

  [ -n "$records" ] || { echo "unknown"; return 0; }

  # A head that is not an instant is not a head. Left as the empty string, and
  # the rule below reports `unknown` for it -- but only when there is actually
  # a finding to date, so a pull request the other reviewer never touched is
  # not held hostage to a null `pushedDate`.
  case "$head" in
    [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]Z) : ;;
    *) head="" ;;
  esac

  # The acknowledgement test is markdown-aware, so it is bash rather than jq.
  # The flags are computed here, one per record, and handed to the rule below
  # as data -- which keeps the set logic in one jq program instead of a loop
  # that reimplements it.
  local count i body acks="" flag
  count="$(printf '%s' "$records" | jq 'length' 2>/dev/null)" || count=""
  case "$count" in
    ''|*[!0-9]*) echo "unknown"; return 0 ;;
  esac

  i=0
  while [ "$i" -lt "$count" ]; do
    body="$(printf '%s' "$records" | jq -r --argjson i "$i" '.[$i].body // ""' 2>/dev/null)"
    if attadipa_codex_acknowledges "$body"; then flag=true; else flag=false; fi
    acks="$acks$flag"$'\n'
    i=$((i + 1))
  done
  local acks_json
  acks_json="$(printf '%s' "$acks" | jq -Rn '[inputs | select(length > 0) | (. == "true")]' 2>/dev/null)" \
    || acks_json=""
  [ -n "$acks_json" ] || { echo "unknown"; return 0; }

  printf '%s' "$records" | jq -r \
    --argjson ack "$acks_json" \
    --argjson codex "$ATTADIPA_CODEX_LOGINS" \
    --argjson never "$ATTADIPA_CODEX_NEVER_ANSWERS" \
    --argjson may "$ATTADIPA_CODEX_ANSWER_PERMISSIONS" \
    --arg head "$head" '
      # An instant, or nothing. A timestamp this cannot parse is not compared
      # lexicographically and hoped for; it drops the record out of the answers
      # and makes a finding undecidable.
      def instant: ((.at // "") | type == "string")
        and ((.at // "") | test("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$"));
      # The login is captured before `index` runs, because inside `index(f)`
      # jq evaluates f against the ARRAY rather than against the record --
      # `$codex | index(.login)` reads `.login` of the list and errors out.
      def is_codex: (.login // "") as $l | ($codex | index($l)) != null;
      def named_never: (.login // "") as $l | ($never | index($l)) != null;
      def may_answer: (.permission // "unknown") as $p | ($may | index($p)) != null;

      [ to_entries[] | .value + {ack: ($ack[.key] // false)} ] as $all
      | [ $all[] | select(is_codex) ] as $findings
      | if ($findings | length) == 0 then "0"
        elif ([ $findings[] | select(instant | not) ] | length) > 0 then "unknown"
        elif $head == "" then "unknown"
        else
          [ $all[]
            | select(is_codex | not)
            # `bot == false` is not an authorisation, but it is still a
            # disqualification: a bot cannot answer for a person even when a
            # lookup would call it privileged. A record with no flag at all is
            # not given the benefit of the doubt either.
            | select(.bot == false)
            | select(named_never | not)
            | select(instant)
          ] as $answers
          | [ $findings[]
              | . as $f
              | [ $answers[]
                  | select(.at > $f.at)
                  # The head, as well as the finding. An acknowledgement
                  # written before the current head commit answered a different
                  # tree.
                  | select(.at >= $head)
                  # BOUND TO THIS FINDING. A reply in the same review thread,
                  # by way of the in_reply_to_id GitHub itself records rather
                  # than a guess from timing -- or an explicit acknowledgement.
                  # Nothing else, which is the whole of the change: a later
                  # comment is not an answer.
                  | select(
                      .ack
                      or ( $f.kind == "review-comment"
                           and .kind == "review-comment"
                           and ($f.thread != null)
                           and (.thread == $f.thread) ) )
                ] as $bound
              | if ([ $bound[] | select(may_answer) ] | length) > 0 then "answered"
                elif ([ $bound[] | select((.permission // "unknown") == "unknown") ] | length) > 0
                  then "indeterminate"
                else "unanswered" end ]
          | if (index("indeterminate") != null) then "unknown"
            else ([ .[] | select(. == "unanswered") ] | length | tostring) end
        end' 2>/dev/null || echo "unknown"
}

# Callable as a command -- the head as the one argument, the records on stdin,
# which is how pr-merge-sweep.yml calls it and therefore what the test calls.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_codex_answered "${1-}" "$(cat)"
fi
