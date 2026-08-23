#!/usr/bin/env bash
# Why the agent died, in one line, extracted from a log nobody is allowed to read.
#
# THE PROBLEM. Every failure comment this pipeline posts ends with the same
# sentence: "the cause is in there, and it is worth reading before retrying,
# because a retry of a deterministic failure is the same failure with a bill
# attached." That sentence points at a run log which `show_full_output: false`
# has deliberately emptied. The agent's step prints its SDK options, an init
# line, and then this and nothing else:
#
#   { "type": "result", "subtype": "success", "is_error": true,
#     "duration_ms": 84607, "num_turns": 20, "total_cost_usd": 0.689,
#     "permission_denials_count": 0, "modelUsage": { ... } }
#
# That is run 32589375744 on #67, 2026-08-22, and it is the whole of what a
# person gets. `is_error: true` under `subtype: success` says a real session ran
# and ended badly, and names nothing. Six earlier deaths that day were
# `error_max_turns`, which at least said its own name; this one did not, and the
# hours spent guessing at it are the reason this file exists.
#
# Turning `show_full_output` on is not the fix, and .github/workflows/
# claude-pr-review.yml already says why: tool results are where the cause would
# appear, and tool results carry file contents and token-shaped strings into a
# world-readable log. So this reads the full execution log -- which the action
# writes to $RUNNER_TEMP and which never leaves the runner -- and emits ONLY
# what a closed vocabulary below can say.
#
# NOTHING FROM THE LOG IS COPIED OUT. That is the security model, and the first
# version of this file did not have it: its patterns were written as
# `API Error: [0-9]{3}[^"]{0,240}` and the WHOLE MATCH was printed, so 240
# characters of whatever followed a status code went into a public comment. A
# tool result reading `API Error: 500 GH_TOKEN=ghp_...` published the token; so
# did a nested `error.message`; and because the patterns were run over the raw
# file rather than over a parsed record, an error prefix sitting in unrelated
# tool output became the run's stated verdict. Reported as #106.
#
# What replaces it: the log is READ to decide WHICH SENTENCE OF THIS FILE to
# print. Every line this emits is assembled from
#
#   * literal text written here, plus
#   * captures drawn from a bounded alphabet -- a three-digit HTTP status, a
#     group of at most twelve digits, or a name from the closed list in
#     ATTADIPA_ERROR_TYPES.
#
# There is no path by which a byte of message text reaches stdout. A secret
# cannot be spelled in three digits, and it cannot be spelled as
# `overloaded_error`, so widening what is *recognised* no longer widens what is
# *disclosed* -- which is what made the old grammar-by-grammar review so
# delicate.
#
# Recognition is ORDERED rather than scoped, and the difference is a review
# finding on #106's own pull request. The detectors read the known fields of the
# LAST `result` RECORD first -- `.result` and `.error`, the latter serialised --
# because that record is the run's verdict and an error prefix quoted in some
# tool result is not. But if nothing there matches, the file itself is read, and
# the line says so: `show_full_output: false` publishes the result record and
# withholds everything else, so a reader who cannot be told about the rest is
# being sent to a log that has already been emptied. Run 32589375744 -- the run
# this file was written for -- has neither `.result` nor `.error`, which is
# exactly the shape a record-only reader would go quiet on.
#
# A line is therefore stamped with what it stands on:
#
#   (found outside the result record)     -- an earlier line may have said it
#   (no status code or error type beside it)  -- the words matched, and nothing
#                                                structural corroborated them
#
# The second matters because `.result` is model output. An agent that dies while
# writing about this very file leaves `Credit balance is too low` in its final
# message, and a spend failure that never happened is worse than an
# `unclassified` -- the retry decision turns on that distinction. The detectors
# are not the place to solve it; saying what the evidence was is.
#
# Anything unrecognised is reported as `unclassified` together with facts that
# are structural rather than textual -- subtype, turn count, whether a result
# string existed at all. An unclassified failure is a gap in the vocabulary
# (T-108), and the honest thing is to say so rather than to widen a pattern
# until something matches.
#
# attadipa_failure_reason PATH
#
# Prints one line: a short reason, safe to paste into a public issue comment.
# Never fails: a missing file, an unparseable file and a clean run all produce a
# line, because the caller runs in `if: always()` and a diagnostic that can
# itself fail is one more thing to diagnose.

# The closed list of error type names. A `type` field is printed only when it is
# spelled exactly like one of these -- so `"type":"result"`, `"type":"text"` and
# `"type":"tool_result"`, which occur all over the log, are not classifications,
# and neither is anything an attacker would rather put there.
ATTADIPA_ERROR_TYPES='overloaded_error|rate_limit_error|api_error|authentication_error|permission_error|invalid_request_error|billing_error|not_found_error|request_too_large|timeout_error'

# The same field, spelled for both places it is read from. Out of a parsed
# `.result` it arrives as `"type":"api_error"`; out of the raw file it is still
# inside a JSON string, so every quote wears a backslash. A reader that knew only
# the first spelling was silent on every whole-file match, which is the quiet
# kind of broken -- it never errors, it just stops recognising.
ATTADIPA_TYPE_FIELD='\\?"type\\?" *: *\\?"('"$ATTADIPA_ERROR_TYPES"')\\?"'

# The conditions worth naming, most specific first. Each entry is
# `name<space>ERE`; the renderer is `attadipa__render_<name>`, which receives
# the matched fragment and returns a sentence of ITS OWN, optionally carrying
# digits it re-extracted from that fragment. The fragment itself is never
# printed, so the trailing parts of these patterns exist to make digits
# available to the renderer and for no other reason.
#
# `Prompt is too long` / `input length` — the API refusing a request larger than
#   the context window. The 2026-08-22 suspicion on #67, unconfirmed: the
#   reading list CLAUDE.md and the agent prompt mandate is over 500 KB before
#   the agent opens a file of its own.
# `exceed... max_tokens` — the same refusal from the other side.
# `Credit balance` / `Request timed out` — the account and the service rather
#   than the task. These must be distinguishable from a task failure, because
#   re-queueing is right for one and wrong for the other.
# `OAuth token has expired` — .github/workflows/claude-agent.yml authenticates
#   with a token refreshed by a scheduled workflow; an expiry looks exactly like
#   a broken agent from the issue page.
# shellcheck disable=SC2016  # These are grep -E patterns, not shell strings;
# the backticks below are literal characters the API's own message contains.
ATTADIPA_REASON_CONDITIONS=(
  'prompt_too_long [Pp]rompt is too long(: [0-9]{1,12} tokens > [0-9]{1,12} maximum)?'
  'input_and_max_tokens input length and `?max_tokens`? exceed( context limit: [0-9]{1,12} \+ [0-9]{1,12} > [0-9]{1,12})?'
  'input_length input length exceeds'
  'max_input_tokens exceeds? the maximum (allowed )?(number of )?(input )?tokens'
  "output_maximum Claude's response exceeded the [0-9]{1,12} output token maximum"
  'credit_balance Credit balance is too low'
  'oauth_expired OAuth token has expired'
  'oauth_refused OAuth (authentication|token) (failed|error|has expired|is expired|is invalid|is required|was refused|is (currently )?not supported)'
  'request_timeout Request timed out'
  'request_aborted Request was aborted'
  'disk_full Error: ENOSPC'
  'heap_oom JavaScript heap out of memory'
)

# --- renderers -------------------------------------------------------------
# Each takes the matched fragment and prints a sentence. Every character of that
# sentence is either written here or a digit group re-extracted under an
# anchored pattern. `[[ =~ ]]` with the pattern in a variable, because a bare
# `>` inside `[[ ]]` is an operator rather than part of a regex.

attadipa__render_prompt_too_long() {
  local re='([0-9]{1,12}) tokens > ([0-9]{1,12}) maximum'
  if [[ "$1" =~ $re ]]; then
    printf 'Prompt is too long: %s tokens > %s maximum' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"
  else
    printf 'the prompt is too long for the context window'
  fi
}

# shellcheck disable=SC2016  # The backticks below quote `max_tokens` for the
# Markdown of an issue comment; nothing is meant to expand.
attadipa__render_input_and_max_tokens() {
  local re='([0-9]{1,12}) \+ ([0-9]{1,12}) > ([0-9]{1,12})'
  if [[ "$1" =~ $re ]]; then
    printf 'input length and `max_tokens` exceed the context limit: %s + %s > %s' \
      "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"
  else
    printf 'input length and `max_tokens` exceed the context limit'
  fi
}

attadipa__render_input_length() {
  printf 'the input length exceeds what the model accepts'
}

attadipa__render_max_input_tokens() {
  printf 'the request exceeds the maximum number of input tokens'
}

attadipa__render_output_maximum() {
  local re='the ([0-9]{1,12}) output token maximum'
  if [[ "$1" =~ $re ]]; then
    printf "Claude's response exceeded the %s output token maximum" "${BASH_REMATCH[1]}"
  else
    printf "Claude's response exceeded the output token maximum"
  fi
}

attadipa__render_credit_balance() {
  printf 'Credit balance is too low to pay for this request'
}

attadipa__render_oauth_expired() {
  printf 'OAuth token has expired and needs refreshing'
}

attadipa__render_oauth_refused() {
  printf 'the OAuth credential was not accepted'
}

attadipa__render_request_timeout() {
  printf 'Request timed out'
}

attadipa__render_request_aborted() {
  printf 'Request was aborted'
}

attadipa__render_disk_full() {
  printf 'the runner ran out of disk space (ENOSPC)'
}

attadipa__render_heap_oom() {
  printf 'JavaScript heap out of memory'
}

# The status codes this pipeline can actually meet, so that a bare number is not
# the whole of what a reader gets. Anything else prints as the number alone --
# a status is three digits either way, and inventing a word for one we have
# never seen would be a guess dressed as a fact.
attadipa__status_word() {
  case "$1" in
    400) printf 'bad request' ;;
    401) printf 'unauthenticated' ;;
    403) printf 'forbidden' ;;
    404) printf 'not found' ;;
    413) printf 'request too large' ;;
    429) printf 'rate limited' ;;
    500) printf 'server error' ;;
    502|503) printf 'service unavailable' ;;
    529) printf 'overloaded' ;;
  esac
}

# Structural fields are printed only when they are spelled the way the SDK
# spells them. `subtype` and `num_turns` come from a record the SDK wrote, not
# from the session -- but they are the two things this file prints without
# recognising them first, so they get an alphabet rather than trust. A token has
# digits in it and a key has punctuation; neither survives `[a-z_]`.
attadipa__safe_name() {
  local re='^[a-z][a-z_]{0,39}$'
  [[ "$1" =~ $re ]] && printf '%s' "$1"
}

attadipa__safe_count() {
  local re='^[0-9]{1,9}$'
  [[ "$1" =~ $re ]] && printf '%s' "$1"
}

# One matcher for both sources, so the file scan and the record scan cannot
# drift apart in what they recognise. `-a` because a log with a stray NUL byte
# would otherwise be reported as "Binary file matches" and match nothing.
attadipa__scan() {
  local mode="$1" src="$2" pattern="$3"
  case "$mode" in
    file) grep -aoEm1 "$pattern" "$src" 2>/dev/null | head -1 ;;
    text) printf '%s\n' "$src" | grep -aoEm1 "$pattern" 2>/dev/null | head -1 ;;
  esac
}

attadipa_failure_reason() {
  local path="${1:-}"

  if [ -z "$path" ] || [ ! -f "$path" ]; then
    echo "no execution log was written — the agent step did not get far enough to leave one"
    return 0
  fi

  local subtype="" is_error="" turns="" had_result="" fields="" head_line="" body=""
  local state="unreadable"
  if command -v jq >/dev/null 2>&1; then
    # `-s` plus `flatten(1)` because this action writes ONE JSON ARRAY on some
    # runs and ONE OBJECT PER LINE on others. Both shapes have been seen and
    # neither is documented -- .github/workflows/claude-pr-review.yml hit the
    # same thing and reads it the same way. Slurping handles both: an array
    # slurps to [[...]] and flattens back, objects slurp to [...] already.
    #
    # The last `result` record is the verdict; earlier ones belong to
    # sub-sessions. One jq invocation rather than five, so a partially readable
    # log cannot answer four questions and fail the fifth: line one is the
    # structural fields, and everything after it is the record's own text.
    #
    # THE ALPHABET CHECKS ARE IN HERE, not only at the print site, because line
    # one is `|`-joined and `read` splits on `|`. A `subtype` of `x|false|1|no`
    # would otherwise shift every field right and make a failed run report
    # itself as `is_error=false` -- sanitised output over a decision taken on
    # unsanitised input. `name` and `count` cannot emit a `|`, so the frame
    # holds by construction.
    fields=$(jq -rs '
      def astext: if . == null then "" elif type == "string" then . else tojson end;
      def name:  if (type == "string") and test("^[a-z][a-z_]{0,39}$") then . else "" end;
      def count: (if type == "number" then tostring elif type == "string" then . else "" end)
                 | if test("^[0-9]{1,9}$") then . else "" end;
      flatten(1)
      | [.[] | select(type == "object" and .type == "result")] | last
      | if . == null then "NORESULT"
        else ([ (.subtype | name),
                (if .is_error == true then "true"
                 elif .is_error == false then "false" else "" end),
                (.num_turns | count),
                (if (.result // "") == "" then "no" else "yes" end) ]
              | join("|"))
             + "\n" + (.result | astext) + "\n" + (.error | astext)
        end' "$path" 2>/dev/null)
    head_line=${fields%%$'\n'*}
    if [ -z "$fields" ]; then
      state="unreadable"
    elif [ "$head_line" = "NORESULT" ]; then
      state="none"
    else
      state="ok"
      if [ "$fields" != "$head_line" ]; then body=${fields#*$'\n'}; fi
      IFS='|' read -r subtype is_error turns had_result <<< "$head_line"
      # Belt and braces: the same rule at the print site, so a change to the jq
      # above cannot quietly widen what a comment may contain.
      subtype=$(attadipa__safe_name "$subtype")
      turns=$(attadipa__safe_count "$turns")
    fi
  fi

  if [ "$is_error" = "false" ]; then
    echo "the run reported no error${subtype:+ (subtype \`$subtype\`)}"
    return 0
  fi

  # WHERE THE DETECTORS LOOK, AND IN WHAT ORDER. The result record first,
  # because it is the run's verdict and a tool result is not; the whole file
  # second, because `show_full_output: false` publishes the record and withholds
  # the rest, so a reader told nothing about the rest has been sent to an empty
  # log. Run 32589375744 has neither `.result` nor `.error`, and a record-only
  # reader is silent on exactly the run this file was written for.
  local mode src outside="no" hit="" re="" window=""
  local code="" etype="" transport="" condition="" entry name pattern pass
  for pass in 1 2; do
    if [ "$pass" = 1 ] && [ "$state" = "ok" ]; then
      mode="text"; src="$body"
    else
      mode="file"; src="$path"; outside="yes"
    fi

    # The transport half. Both the status and the type come out of ONE bounded
    # window, so two unrelated errors in one body cannot be spliced into
    # `API Error: 500 (rate_limit_error)`. The window is internal; only the
    # three digits and the closed-list name are ever printed.
    window=$(attadipa__scan "$mode" "$src" 'API Error: [0-9]{3}.{0,160}')
    re='API Error: ([0-9]{3})'
    if [[ "$window" =~ $re ]]; then
      code="${BASH_REMATCH[1]}"
      re="$ATTADIPA_TYPE_FIELD"
      if [[ "$window" =~ $re ]]; then etype="${BASH_REMATCH[1]}"; fi
    else
      hit=$(attadipa__scan "$mode" "$src" "$ATTADIPA_TYPE_FIELD")
      re="$ATTADIPA_TYPE_FIELD"
      if [[ "$hit" =~ $re ]]; then etype="${BASH_REMATCH[1]}"; fi
    fi

    # The condition half: first entry that matches wins, and what is printed is
    # the renderer's sentence rather than the match. An entry whose renderer
    # does not exist is skipped rather than run -- a table typo then costs a
    # classification, which is what `unclassified` is for, instead of putting a
    # name from the table through the command line.
    for entry in "${ATTADIPA_REASON_CONDITIONS[@]}"; do
      read -r name pattern <<< "$entry"
      declare -F "attadipa__render_$name" >/dev/null 2>&1 || continue
      hit=$(attadipa__scan "$mode" "$src" "$pattern")
      if [ -n "$hit" ]; then
        condition=$("attadipa__render_$name" "$hit")
        [ -n "$condition" ] && break
      fi
    done

    # Something was recognised, or there is nothing left to read.
    if [ -n "$code" ] || [ -n "$etype" ] || [ -n "$condition" ] || [ "$outside" = "yes" ]
    then break; fi
  done

  if [ -n "$code" ]; then
    transport="API Error: $code"
    if [ -n "$etype" ]; then
      transport="$transport (\`$etype\`)"
    else
      hit=$(attadipa__status_word "$code")
      [ -n "$hit" ] && transport="$transport ($hit)"
    fi
  elif [ -n "$etype" ]; then
    transport="the API reported \`$etype\`"
  fi

  local line=""
  if [ -n "$transport" ] && [ -n "$condition" ]; then
    line="$transport — $condition"
  elif [ -n "$transport" ]; then
    line="$transport"
  elif [ -n "$condition" ]; then
    line="$condition"
  fi

  if [ -n "$line" ]; then
    # WHAT THE LINE STANDS ON, said rather than implied. `agent-say.sh` follows
    # this with "extracted on the runner", so a line matched in an agent's own
    # prose would be an inference wearing an extraction's clothes.
    if [ "$outside" = "yes" ]; then
      line="$line (found outside the result record)"
    elif [ -z "$transport" ]; then
      line="$line (no status code or error type beside it)"
    fi
    # One line, and bounded, because an issue comment is not a log viewer. Both
    # are belt-and-braces now that nothing from the log is copied -- the
    # renderers above are the length bound -- and they stay, because the cost of
    # being wrong about that is a run log in a comment.
    line=$(printf '%s' "$line" | tr -d '\n\r' | cut -c1-300)
    echo "$line"
    return 0
  fi

  # NOT A GUESS. Everything here is structural: a name the SDK chose, a count it
  # kept, and whether a final message existed. None of it is text the session
  # produced, which is the whole point -- an unclassified failure must not be
  # the one that starts printing arbitrary strings.
  local detail="unclassified"
  [ -n "$subtype" ] && detail="$detail — SDK subtype \`$subtype\`"
  [ -n "$turns" ] && detail="$detail, ended at turn $turns"
  case "$had_result" in
    no)  detail="$detail, with no final message at all" ;;
    yes) detail="$detail, and its final message matched no known error shape" ;;
  esac
  # `agent-say.sh` answers `unclassified` with "widening that whitelist is the
  # fix, and it is a task". That is the right advice for a gap in the vocabulary
  # and the wrong advice for a log nothing could read, so the two say which.
  case "$state" in
    none)       detail="$detail — the log has no result record" ;;
    unreadable) detail="$detail — the log could not be parsed" ;;
  esac
  echo "$detail"
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_failure_reason "${1:-}"
fi
