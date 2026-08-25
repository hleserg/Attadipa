#!/usr/bin/env bash
# How many of the other reviewer's findings on this pull request are still
# unanswered -- where "answered" means somebody ENTITLED to answer them did,
# about THIS finding, about THIS object id.
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
#      acknowledgement below, in the body, in visible prose.
#   3. WHICH TREE. The answer names THE HEAD COMMIT'S OBJECT ID. A verdict
#      reached on commit A says nothing about commit B, which is the same
#      argument `ai-review:pass` already carries in merge-candidate.sh.
#
# CONDITION 3 USED TO BE A DATE, AND A DATE COULD NOT CARRY IT. Until issue
# #130 was reopened this file took the head's timestamp and asked for an answer
# `.at >= $head`. Two things were wrong with that and they compound:
#
#   * the value the sweep passed was `(.pushedDate // .committedDate)`, and #199
#     established that `pushedDate` is deprecated and answers `null` for every
#     commit this repository has -- so it was always `committedDate`, which is
#     the git committer clock and which `GIT_COMMITTER_DATE` sets. The condition
#     meant to bind a verdict to a tree rested on a field the author of that
#     tree writes;
#   * even with an honest clock, ORDER IS NOT IDENTITY. Move a pull request onto
#     a commit that already existed -- a revert, a rebase onto an older base, a
#     force-push back to a commit somebody pushed this morning -- and the head's
#     timestamp goes BACKWARDS. An acknowledgement written at 10:00 about the
#     tree that was head then still post-dates a head whose stamp reads 08:00,
#     so it answers a different tree without anybody typing anything. Filed on
#     #130 with the reproduction; it printed `0` before and after the move.
#
# So the head arrives here as its OBJECT ID, which is immutable, which nothing
# but the tree itself can produce, and which an acknowledgement has to name.
# There is no timestamp condition against the head any more and reintroducing
# one is the defect, not the belt to the braces: the ordering that remains --
# an answer is later than the finding it answers -- is GitHub's `created_at` on
# two comments, a clock no contributor writes.
#
# It fails closed. Anything it could not establish -- a permission it could not
# read, a timestamp it could not parse, a head object id it was not given --
# prints `unknown`, and merge-candidate.sh holds on `unknown` with a line of its
# own. "Could not tell" and "nothing outstanding" are different answers and must
# not print the same word.
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

# The acknowledgement. Visible in the rendered thread on purpose -- an HTML
# comment would be invisible to the person the acknowledgement is for -- and it
# carries the head commit's object id, so that the sentence it makes is "I have
# read the other reviewer's findings against THIS tree" and not "I have read
# something at some point".
#
#     attadipa: codex-reviewed 4038850c2a4b1f0e9d7c6b5a49382716f0e5d4c3
#
# THE WHOLE LINE, AND NOTHING ELSE ON IT. `gh pr view N --json headRefOid -q
# .headRefOid` prints the argument; forty hex characters, not the abbreviation
# GitHub shows in a page, because a prefix is a question about how many
# characters are enough and a full object id is not a question at all.
#
# A LABEL WAS THE OTHER CANDIDATE and was rejected: adding a label that is
# already present raises no event at all, so re-acknowledging a second finding
# would need a remove-then-add nobody would guess at. AI_TASK_PROTOCOL.md
# documents that exact trap for `agent:ready`; one is enough. A comment always
# carries a fresh timestamp, it can carry the object id, and it can carry the
# reasoning as well.
ATTADIPA_CODEX_ACK="attadipa: codex-reviewed"

# The permissions that may answer. The same three intake-decision.sh accepts.
# `triage` is deliberately not among them: a triage collaborator may label and
# close, which is why the label route was not taken either.
ATTADIPA_CODEX_ANSWER_PERMISSIONS='["admin","maintain","write"]'

# attadipa_codex_oid TEXT -- prints TEXT lowercased when it is a git object id,
# prints nothing and returns 1 otherwise. Forty hexadecimal characters: GitHub's
# `oid`, `commit_id` and `original_commit_id` are all this shape.
attadipa_codex_oid() {
  local oid="${1-}"
  oid="${oid,,}"
  [ "${#oid}" -eq 40 ] || return 1
  case "$oid" in
    *[!0-9a-f]*) return 1 ;;
  esac
  printf '%s' "$oid"
}

# attadipa_codex_uncomment TEXT -- TEXT with every HTML comment removed.
#
# `<!-- attadipa: codex-reviewed <oid> -->` renders as nothing at all, and it
# cleared a finding: the stripper this file used to borrow from the intake gate
# removed fenced blocks and single-backtick spans, and an HTML comment is
# neither. So the acknowledgement could be given by a comment whose author
# could not see they had given it, and taken back by nobody. Reported by Codex
# on #219 and merged past.
#
# An UNTERMINATED `<!--` drops everything after it. That is what a browser does
# with the rest of the document, and it is the fail-closed direction here: the
# text nobody can read cannot acknowledge anything.
attadipa_codex_uncomment() {
  local rest="${1-}" out="" after
  while : ; do
    case "$rest" in
      *'<!--'*) : ;;
      *) out="$out$rest"; break ;;
    esac
    out="$out${rest%%<!--*}"
    after="${rest#*<!--}"
    case "$after" in
      *'-->'*) rest="${after#*-->}" ;;
      *) break ;;
    esac
  done
  printf '%s' "$out"
}

# attadipa_codex_acknowledges BODY HEAD_OID -- 0 when BODY carries a visible,
# standalone acknowledgement naming HEAD_OID.
#
# WHY THIS IS A RECOGNISER AND NOT A STRIPPER. The old test was "does the token
# appear anywhere in the body once code has been removed", over
# `attadipa_strip_code` from intake-decision.sh -- and that function removes
# exactly two things, fences that begin in column one and single-backtick pairs.
# Every other way markdown has of showing a reader a string without saying it
# went through: a ``double-backtick`` span, four spaces of indent, an HTML
# comment, a `> ` quotation, and the token in the middle of an ordinary
# sentence. All five cleared a finding on `main@36e1ba9`, run rather than read.
#
# Removing more forms one at a time is a losing game against a markdown parser,
# so the question is inverted. Nothing is stripped and then searched. A line
# must SURVIVE to be considered -- outside every fence, indented at most three
# spaces, not a quotation, containing no backtick anywhere at all -- and then it
# must be, once trimmed, EXACTLY the acknowledgement and its object id. A form
# this recogniser does not understand is a form that does not acknowledge
# anything.
#
# `attadipa_strip_code` is deliberately left alone rather than fixed here. It
# guards a different boundary -- whether a comment asks for an agent -- where
# the string is a mention inside a sentence and an exact-line rule would refuse
# every real `@claude`. Two boundaries, two recognisers -- issue #174 tracks
# that gate's remaining blind spots, because its answer cannot be an exact-line
# rule.
attadipa_codex_acknowledges() {
  local head line stripped indent want
  head="$(attadipa_codex_oid "${2-}")" || return 1
  want="$ATTADIPA_CODEX_ACK $head"

  local fence_char="" fence_len=0 run=0 rest c tail quoted=no
  while IFS= read -r line; do
    line="${line%$'\r'}"

    # A tab is code wherever CommonMark's tab stops land it, and it is never
    # part of an acknowledgement. Out before anything else looks at the line,
    # and without touching the fence state: a tab-indented line can neither
    # open a fence nor close one.
    case "$line" in
      $'\t'*) continue ;;
    esac

    stripped="$line"
    indent=0
    while [ "${stripped# }" != "$stripped" ]; do
      stripped="${stripped# }"
      indent=$((indent + 1))
    done

    # A BLANK LINE ENDS A QUOTATION, and nothing else does. Markdown's LAZY
    # CONTINUATION means the second line of
    #
    #     > Somebody wrote:
    #     attadipa: codex-reviewed <oid>
    #
    # renders INSIDE the quote even though it carries no `>` of its own -- so
    # refusing only the lines that begin with one refuses the marker and not the
    # quotation. The flag below survives until a blank line, which is where
    # CommonMark ends the paragraph and therefore the block.
    if [ -z "$stripped" ]; then
      quoted=no
      continue
    fi

    # A FENCE, tracked by its character and its run length rather than by "the
    # line starts with three of something". Inside a ``` block a `~~~` line is
    # content, and a toggle that does not know which character opened the block
    # closes it early -- which puts the next line, the one carrying the token,
    # back in visible prose.
    if [ "$indent" -le 3 ]; then
      case "$stripped" in
        '```'*|'~~~'*)
          c="${stripped:0:1}"
          run=0
          rest="$stripped"
          while [ "${rest#"$c"}" != "$rest" ]; do
            rest="${rest#"$c"}"
            run=$((run + 1))
          done
          if [ -z "$fence_char" ]; then
            # An opening backtick fence may not carry a backtick in its info
            # string; if it does it is not a fence, and the line falls through
            # to the backtick refusal below, which discards it anyway.
            if [ "$c" = '`' ] && [ "${rest//[^\`]/}" != "" ]; then
              :
            else
              fence_char="$c"
              fence_len="$run"
              continue
            fi
          else
            # A closing fence: the same character, at least as long as the one
            # that opened the block, and nothing but whitespace after it.
            tail="${rest//[[:space:]]/}"
            if [ "$c" = "$fence_char" ] && [ "$run" -ge "$fence_len" ] && [ -z "$tail" ]; then
              fence_char=""
              fence_len=0
            fi
            continue
          fi
          ;;
      esac
    fi

    # Inside a fenced block, or indented far enough to be a code block.
    [ -z "$fence_char" ] || continue
    [ "$indent" -le 3 ] || continue

    case "$stripped" in
      '>'*) quoted=yes; continue ;;  # a quotation is somebody else's words
      # ANY BACKTICK AT ALL, so no code span of any width survives -- and this
      # is deliberately redundant. The exact comparison below already refuses a
      # line carrying one, because the acknowledgement has no backtick in it, so
      # no fixture can distinguish this rule from that one and no mutant can
      # kill it. It is a second lock on the same door, there for the day the
      # comparison is loosened, and it is written down as redundant so that
      # nobody deletes it believing they have found dead code -- or keeps it
      # believing it is tested.
      *'`'*) continue ;;
    esac

    # Still inside a quotation this line does not have to carry a marker for.
    [ "$quoted" = no ] || continue

    # Trailing whitespace only; a line with anything else on it is prose about
    # the acknowledgement rather than the acknowledgement.
    while [ "${stripped% }" != "$stripped" ]; do stripped="${stripped% }"; done
    if [ "${stripped,,}" = "$want" ]; then
      return 0
    fi
  done <<EOF
$(attadipa_codex_uncomment "${1-}")
EOF
  return 1
}

# attadipa_codex_answered HEAD_OID RECORDS_JSON
#
# HEAD_OID        the head commit's object id, `oid` in the GraphQL the sweep
#                 already asks for and the same value merge-candidate.sh binds
#                 the reviewer's verdict to. Anything that is not forty
#                 hexadecimal characters means the head could not be
#                 established, which is `unknown` whenever there is a finding to
#                 bind to it.
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
#                   commit_oid  for "review-comment", `original_commit_id` --
#                               the commit the comment was WRITTEN against,
#                               which GitHub records once and does not revise;
#                               null otherwise
#                   body        the comment text, verbatim
#
# Prints exactly one line:
#   <n>        that many of the other reviewer's findings have no valid answer
#   unknown    at least one finding could not be decided either way
attadipa_codex_answered() {
  local head="${1-}" records="${2-}"

  [ -n "$records" ] || { echo "unknown"; return 0; }

  # A head that is not an object id is not a head. Left as the empty string,
  # and the rule below reports `unknown` for it -- but only when there is
  # actually a finding to bind, so a pull request the other reviewer never
  # touched is not held hostage to a head the caller could not read.
  head="$(attadipa_codex_oid "$head")" || head=""

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
    if [ -n "$head" ] && attadipa_codex_acknowledges "$body" "$head"; then
      flag=true
    else
      flag=false
    fi
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
      # An object id, or null. Anything that is not one can never equal $head,
      # which is validated before it gets here.
      def oid: (.commit_oid // "")
        | if (type == "string") and test("^[0-9a-fA-F]{40}$") then ascii_downcase else null end;
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
                  # BOUND TO THIS FINDING, AND TO THIS TREE.
                  #
                  # An acknowledgement names the head object id in its own
                  # text, which is what `.ack` already means -- the recogniser
                  # was given $head and matched it.
                  #
                  # A REPLY carries no text anybody has to write, so the tree
                  # it is about comes from GitHub: both the finding and the
                  # reply must have been written against the commit that is
                  # head now. `original_commit_id` is the commit a review
                  # comment was made on, recorded once. So a thread that
                  # discussed commit A stops answering the moment the pull
                  # request moves to B -- the finding is still outstanding, it
                  # is simply outstanding about a tree nobody has replied to
                  # yet, and an acknowledgement naming B is the way to say
                  # otherwise. Held, never dropped: a finding must not become
                  # answerable by pushing past it.
                  | select(
                      .ack
                      or ( $f.kind == "review-comment"
                           and .kind == "review-comment"
                           and ($f.thread != null)
                           and (.thread == $f.thread)
                           and (($f | oid) == $head)
                           and ((. | oid) == $head) ) )
                ] as $bound
              | if ([ $bound[] | select(may_answer) ] | length) > 0 then "answered"
                elif ([ $bound[] | select((.permission // "unknown") == "unknown") ] | length) > 0
                  then "indeterminate"
                else "unanswered" end ]
          | if (index("indeterminate") != null) then "unknown"
            else ([ .[] | select(. == "unanswered") ] | length | tostring) end
        end' 2>/dev/null || echo "unknown"
}

# Callable as a command -- the head's object id as the one argument, the records
# on stdin, which is how pr-merge-sweep.yml calls it and therefore what the test
# calls. A caller that passes anything other than one argument is not a caller
# this rule understands, and a rule that guesses at its own inputs is the shape
# merge-candidate.sh refuses by arity for the same reason.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  if [ "$#" -ne 1 ]; then
    echo "unknown"
    exit 0
  fi
  attadipa_codex_answered "$1" "$(cat)"
fi
