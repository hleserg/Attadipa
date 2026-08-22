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
# what matches a known error grammar.
#
# THE WHITELIST IS THE SECURITY MODEL. Nothing is printed because it looked
# interesting. A string is printed because it matches a pattern below that can
# only be produced by an API or SDK error path, and each pattern is anchored and
# length-bounded. Anything unrecognised is reported as `unclassified` together
# with the facts that are structural rather than textual -- subtype, turn count,
# whether a result string existed at all. An unclassified failure is a gap in
# this list, and the honest thing is to say so rather than to widen the grammar
# until something matches.
#
# attadipa_failure_reason PATH
#
# Prints one line: a short reason, safe to paste into a public issue comment.
# Never fails: a missing file, an unparseable file and a clean run all produce a
# line, because the caller runs in `if: always()` and a diagnostic that can
# itself fail is one more thing to diagnose.

# Ordered most specific first. Each entry is a grep -oE pattern; the first that
# matches anything wins, and only the matched text is printed.
#
# `Prompt is too long` / `input length` — the API refusing a request larger than
#   the context window. The 2026-08-22 suspicion on #67, unconfirmed: the
#   reading list CLAUDE.md and the agent prompt mandate is over 500 KB before
#   the agent opens a file of its own.
# `exceed... max_tokens` — the same refusal from the other side.
# `Credit balance` / `rate_limit` / `overloaded` / `Request timed out` — the
#   account and the service rather than the task. These must be distinguishable
#   from a task failure, because re-queueing is right for one and wrong for the
#   other.
# `OAuth token has expired` — .github/workflows/claude-agent.yml authenticates
#   with a token refreshed by a scheduled workflow; an expiry looks exactly like
#   a broken agent from the issue page.
# shellcheck disable=SC2016  # These are grep -E patterns, not shell strings;
# the backticks below are literal characters the API's own message contains.
ATTADIPA_REASON_PATTERNS=(
  'Prompt is too long[^"]{0,200}'
  'input length and `?max_tokens`? exceed[^"]{0,200}'
  'input length exceeds[^"]{0,200}'
  'exceeds? the maximum (allowed )?(number of )?(input )?tokens[^"]{0,160}'
  "Claude's response exceeded the [0-9]+ output token maximum"
  'Credit balance is too low[^"]{0,160}'
  'OAuth (authentication|token)[^"]{0,160}'
  'API Error: [0-9]{3}[^"]{0,240}'
  '"type" *: *"(overloaded_error|rate_limit_error|api_error|authentication_error|permission_error|invalid_request_error|billing_error)"'
  'Request (timed out|was aborted)[^"]{0,160}'
  'Error: ENOSPC[^"]{0,120}'
  'JavaScript heap out of memory'
)

attadipa_failure_reason() {
  local path="${1:-}"

  if [ -z "$path" ] || [ ! -f "$path" ]; then
    echo "no execution log was written — the agent step did not get far enough to leave one"
    return 0
  fi

  local subtype="" is_error="" turns="" had_result="" fields=""
  if command -v jq >/dev/null 2>&1; then
    # `-s` plus `flatten(1)` because this action writes ONE JSON ARRAY on some
    # runs and ONE OBJECT PER LINE on others. Both shapes have been seen and
    # neither is documented -- .github/workflows/claude-pr-review.yml hit the
    # same thing and reads it the same way. Slurping handles both: an array
    # slurps to [[...]] and flattens back, objects slurp to [...] already.
    #
    # The last `result` record is the verdict; earlier ones belong to
    # sub-sessions. One jq invocation rather than four, so a partially readable
    # log cannot answer three questions and fail the fourth.
    fields=$(jq -rs '
      flatten(1)
      | [.[] | select(type == "object" and .type == "result")] | last
      | if . == null then "||||no"
        else [ (.subtype? // ""),
               (if has("is_error") then (.is_error | tostring) else "" end),
               ((.num_turns? // "") | tostring),
               (if (.result? // "") == "" then "no" else "yes" end) ]
             | join("|")
        end' "$path" 2>/dev/null)
    IFS='|' read -r subtype is_error turns had_result <<< "$fields"
  fi

  if [ "$is_error" = "false" ]; then
    echo "the run reported no error${subtype:+ (subtype \`$subtype\`)}"
    return 0
  fi

  local pattern hit
  for pattern in "${ATTADIPA_REASON_PATTERNS[@]}"; do
    hit=$(grep -oEm1 "$pattern" "$path" 2>/dev/null | head -1)
    if [ -n "$hit" ]; then
      # One line, bounded, and stripped of the quoting that surrounds it in JSON.
      hit=$(printf '%s' "$hit" | tr -d '\n\r' | cut -c1-300)
      echo "$hit"
      return 0
    fi
  done

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
