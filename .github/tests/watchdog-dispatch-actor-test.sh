#!/usr/bin/env bash
# The watchdog dispatches the agent, and the agent must accept the dispatcher.
#
# THE FAILURE THIS EXISTS FOR. On 2026-08-22 the hourly watchdog had never once
# started an agent successfully, and nothing said so. It hands a task over with
#
#   GH_TOKEN: ${{ github.token }}
#   gh workflow run claude-agent.yml ...
#
# so the dispatching actor is `github-actions[bot]`. claude-agent.yml passed
# `allowed_bots: ""` to anthropics/claude-code-action, which refuses a non-User
# actor that is not on that list:
#
#   Actor type: Bot
#   Workflow initiated by non-human actor: github-actions (type: Bot).
#
# Five seconds, no execution log, and the hand-over could only report
# `no conclusion`. Four issues -- #27, #28, #67, #69 -- were written off as
# unexplained model deaths, and a whole task (T-107) was opened to investigate
# the reading list as the suspected cause. The real signal was that every death
# had been started by the watchdog and every success by a person commenting,
# which is invisible unless you line the two up.
#
# WHY A TEST RATHER THAN A COMMENT. Both halves are correct in isolation. The
# watchdog is right to dispatch with the built-in token, and an empty
# `allowed_bots` is a defensible default -- the comment it replaced correctly
# noted that '*' would let any GitHub App drive a write-capable agent. Only the
# PAIR is wrong, and neither file's own review would ever show it. Same
# reasoning as .github/scripts/queue-scan.jq and intake-decision.sh living in
# files: a rule nothing can execute is a rule nothing can check.
#
# The normalisation below mirrors isAllowedBot in
# src/github/validation/actor.ts at the action's v1 tag: split on commas, trim,
# lowercase, strip a trailing `[bot]`, on both the list and the actor. So
# `github-actions` and `github-actions[bot]` are the same entry, and this test
# accepts either rather than pinning a spelling the action does not care about.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0

ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }

WATCHDOG=.github/workflows/agent-queue-watchdog.yml
AGENT=.github/workflows/claude-agent.yml

# The actor of any workflow in this repository. Not configurable, and not a
# thing an outside GitHub App can present as -- which is the whole reason
# naming it is a narrower concession than '*'.
DISPATCHER=github-actions

# isAllowedBot, in shell. Returns 0 when $2 is covered by the list in $1.
covers() {
  local list="$1" who="$2" entry
  list="$(printf '%s' "$list" | tr -d '[:space:]')"
  [ "$list" = "*" ] && return 0
  [ -z "$list" ] && return 1
  who="$(printf '%s' "$who" | tr '[:upper:]' '[:lower:]')"
  who="${who%\[bot\]}"
  local IFS=,
  for entry in $list; do
    entry="$(printf '%s' "$entry" | tr '[:upper:]' '[:lower:]')"
    entry="${entry%\[bot\]}"
    [ -n "$entry" ] && [ "$entry" = "$who" ] && return 0
  done
  return 1
}

# says WANT LIST ACTOR NAME -- WANT is `yes` or `no`, spelled out rather than
# left to an exit status, because the whole subject of this file is a boolean
# that was wrong in one direction and nobody noticed.
says() {
  local want="$1" list="$2" actor="$3" name="$4" got=no
  if covers "$list" "$actor"; then got=yes; fi
  if [ "$got" = "$want" ]; then
    ok "$name"
  else
    no "$name" "wanted $want, got $got for list=\"$list\" actor=\"$actor\""
  fi
}

echo "The shell reimplementation of isAllowedBot agrees with the action"
says yes "github-actions"      "github-actions[bot]" "a bare name covers the [bot] suffix"
says yes "github-actions[bot]" "github-actions"      "and the suffixed spelling covers a bare actor"
says yes "GitHub-Actions"      "github-actions[bot]" "matching is case-insensitive"
says yes "dependabot, github-actions" "github-actions[bot]" "a comma-separated list is read as a list"
says yes "*"                   "anything[bot]"       "a star covers everything, as the action says"
says no  ""                    "github-actions[bot]" "an empty list covers nothing -- the bug, stated as a rule"
says no  "dependabot"          "github-actions[bot]" "a list without the actor does not match"

echo
echo "The watchdog still dispatches the workflow this test is about"
if grep -qE '^\s*gh workflow run\s+claude-agent\.yml\b' "$WATCHDOG"; then
  ok "$WATCHDOG dispatches claude-agent.yml"
else
  no "$WATCHDOG dispatches claude-agent.yml" \
     "no 'gh workflow run claude-agent.yml' found -- if the hand-over moved, point this test at its new target rather than deleting it"
fi

# If the watchdog ever dispatches with something other than the built-in token,
# the actor stops being a bot and this whole check becomes the wrong question.
# Better to fail loudly and be re-read than to keep asserting something that no
# longer describes the system.
if grep -qE 'GH_TOKEN:\s*\$\{\{\s*github\.token\s*\}\}' "$WATCHDOG"; then
  ok "and does it with the built-in token, so the actor is a bot"
else
  no "and does it with the built-in token, so the actor is a bot" \
     "GH_TOKEN is no longer github.token in $WATCHDOG -- re-derive the dispatching actor before trusting the assertion below"
fi

echo
echo "The agent accepts that dispatcher"
ALLOWED=$(sed -n 's/^[[:space:]]*allowed_bots:[[:space:]]*//p' "$AGENT" | head -n 1)
# Strip the surrounding quotes YAML keeps, in either style.
ALLOWED="${ALLOWED%\"}"; ALLOWED="${ALLOWED#\"}"
ALLOWED="${ALLOWED%\'}"; ALLOWED="${ALLOWED#\'}"

if [ -z "$(sed -n 's/^[[:space:]]*allowed_bots:.*/x/p' "$AGENT" | head -n 1)" ]; then
  no "$AGENT declares allowed_bots" "the input is absent entirely; the action defaults to refusing every bot"
elif covers "$ALLOWED" "$DISPATCHER"; then
  ok "allowed_bots (\"$ALLOWED\") covers $DISPATCHER"
else
  no "allowed_bots (\"$ALLOWED\") covers $DISPATCHER" \
     "the watchdog's dispatch will be refused in about five seconds, before the agent reads a file, and the issue will be told 'no conclusion'"
fi

# The other direction, because this file is the obvious place to over-correct.
# '*' passes the check above and is exactly what the input must never become on
# a public repository: it would let any GitHub App start a write-capable agent.
if [ "$(printf '%s' "$ALLOWED" | tr -d '[:space:]')" = "*" ]; then
  no "allowed_bots is not a blanket star" \
     "'*' lets ANY GitHub App drive a write-capable agent -- name the dispatcher instead"
else
  ok "allowed_bots is not a blanket star"
fi

echo
echo "The producer allowlist is untouched by any of this"
# Naming a bot as a *dispatcher* must never be confused with trusting it as a
# *producer*. queue-scan.jq refuses to honour `claude` or `github-actions` in
# ATTADIPA_TRUSTED_PRODUCERS precisely so this repository's own output cannot
# enqueue a billable writer. That is the rule that matters, and it is a
# different rule.
if grep -qE '\^\(claude\|github-actions\)' .github/scripts/queue-scan.jq; then
  ok "queue-scan.jq still refuses claude and github-actions as producers"
else
  no "queue-scan.jq still refuses claude and github-actions as producers" \
     "the non-listable internal-bot rule is gone from the scan filter"
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
