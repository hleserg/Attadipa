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
# Recognition is also scoped: the detectors run over the KNOWN FIELDS OF THE
# LAST `result` RECORD (`.result` and `.error`, the latter serialised), not over
# the file. The whole-file scan survives only for a log with no readable result
# record -- a truncated write, a shape jq cannot parse -- and a line produced
# that way says where it came from, because it may belong to some earlier
# record rather than to the failure.
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
  'oauth_refused OAuth (authentication|token)'
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
    # structural fields, and everything after it is the only text the detectors
    # are allowed to look at -- `.result` and `.error`, and nothing else in the
    # record, nothing at all from any other record.
    fields=$(jq -rs '
      def astext: if . == null then "" elif type == "string" then . else tojson end;
      flatten(1)
      | [.[] | select(type == "object" and .type == "result")] | last
      | if . == null then "||||no"
        else ([ (.subtype | astext),
                (if has("is_error") then (.is_error | tostring) else "" end),
                (.num_turns | astext),
                (if (.result // "") == "" then "no" else "yes" end) ]
              | join("|"))
             + "\n" + (.result | astext) + "\n" + (.error | astext)
        end' "$path" 2>/dev/null)
    head_line=${fields%%$'\n'*}
    if [ "$fields" != "$head_line" ]; then body=${fields#*$'\n'}; fi
    IFS='|' read -r subtype is_error turns had_result <<< "$head_line"
    subtype=$(attadipa__safe_name "$subtype")
    turns=$(attadipa__safe_count "$turns")
  fi

  if [ "$is_error" = "false" ]; then
    echo "the run reported no error${subtype:+ (subtype \`$subtype\`)}"
    return 0
  fi

  # WHERE THE DETECTORS MAY LOOK. A readable result record is the verdict, and
  # its two text fields are the whole search space -- so an error prefix in a
  # tool result is no longer able to become the run's stated cause. Only when no
  # result record could be read at all does the file itself become the source,
  # and a line found that way is labelled as such below.
  local mode="file" src="$path" provenance=""
  if [ "$head_line" = "||||no" ]; then
    provenance=" (not from the result record: the log has none)"
  elif [ -n "$head_line" ]; then
    mode="text"; src="$body"
  else
    provenance=" (not from the result record: the log could not be parsed)"
  fi

  # The transport half: a status code and a type name, both from closed
  # alphabets. `${hit##* }` takes the digits off `API Error: 500` -- the pattern
  # guarantees there is nothing else in the match.
  local hit="" code="" etype="" transport="" re=""
  hit=$(attadipa__scan "$mode" "$src" 'API Error: [0-9]{3}')
  [ -n "$hit" ] && code="${hit##* }"
  hit=$(attadipa__scan "$mode" "$src" "\"type\" *: *\"($ATTADIPA_ERROR_TYPES)\"")
  re="\"($ATTADIPA_ERROR_TYPES)\"\$"
  if [[ "$hit" =~ $re ]]; then etype="${BASH_REMATCH[1]}"; fi

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

  # The condition half: first entry that matches wins, and what is printed is
  # the renderer's sentence rather than the match. An entry whose renderer does
  # not exist is skipped rather than run -- a table typo then costs a
  # classification, which is what `unclassified` is for, instead of putting a
  # name from the table through the command line.
  local entry name pattern condition=""
  for entry in "${ATTADIPA_REASON_CONDITIONS[@]}"; do
    read -r name pattern <<< "$entry"
    declare -F "attadipa__render_$name" >/dev/null 2>&1 || continue
    hit=$(attadipa__scan "$mode" "$src" "$pattern")
    if [ -n "$hit" ]; then
      condition=$("attadipa__render_$name" "$hit")
      [ -n "$condition" ] && break
    fi
  done

  local line=""
  if [ -n "$transport" ] && [ -n "$condition" ]; then
    line="$transport — $condition"
  elif [ -n "$transport" ]; then
    line="$transport"
  elif [ -n "$condition" ]; then
    line="$condition"
  fi

  if [ -n "$line" ]; then
    # One line, and bounded, because an issue comment is not a log viewer. Both
    # are belt-and-braces now that nothing from the log is copied -- the
    # renderers above are the length bound -- and they stay, because the cost of
    # being wrong about that is a run log in a comment.
    line=$(printf '%s' "$line$provenance" | tr -d '\n\r' | cut -c1-300)
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
  echo "$detail"
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_failure_reason "${1:-}"
fi
