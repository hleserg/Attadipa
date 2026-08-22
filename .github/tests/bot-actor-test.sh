#!/usr/bin/env bash
# A workflow that lets a bot actor start must also let the action accept it.
#
# ONE RULE, TWO PLACES IT WAS BROKEN, BOTH SILENT.
#
# `anthropics/claude-code-action` refuses any actor of type Bot that is not
# named in its `allowed_bots` input. Whether an actor reaches the action at all
# is decided somewhere else entirely -- by the workflow's own `if:`, or by how
# it is triggered. Those two decisions are made in different parts of different
# files by different people, and when they disagree the run dies in about five
# seconds, before the agent reads anything, with no execution log to say why.
#
# 1. THE WATCHDOG. `agent-queue-watchdog.yml` hands a task over with
#
#      GH_TOKEN: ${{ github.token }}
#      gh workflow run claude-agent.yml ...
#
#    so the dispatching actor is `github-actions[bot]`. `claude-agent.yml`
#    passed `allowed_bots: ""`, and the hourly watchdog had therefore **never
#    once started an agent**. Four issues -- #27, #28, #67, #69 -- were written
#    off as unexplained model deaths, and a whole task (T-107) was opened to
#    investigate the reading list as the suspected cause. Every death had been
#    started by the watchdog and every success by a person commenting, which is
#    invisible unless you line the two up.
#
# 2. THE REVIEWER. `claude-pr-review.yml`'s `if:` says, in as many words, that
#    claude[bot] is exempted because a blanket bot guard "skipped the review on
#    exactly the pull requests this workflow exists to review: the agent's
#    own". Then the step handed the action `allowed_bots: ""`. So the guard let
#    the job start and the action refused it:
#
#      Actor type: Bot
#      ##[error]Action failed with error: Workflow initiated by non-human
#      actor: claude (type: Bot). Add bot to allowed_bots list or use '*' to
#      allow all bots.
#
#    Byte-identical on runs 32597016812, 32596445164, 32595947792 and
#    32595273274 -- five, five, five and four seconds. **No agent-authored pull
#    request had ever been reviewed**, and every one of those jobs reported
#    SUCCESS, because the Review step carries `continue-on-error`.
#
# WHY A TEST RATHER THAN A COMMENT. In both cases each half is correct alone.
# A watchdog should dispatch with the built-in token. An `if:` should admit the
# agent's own pull requests. An empty `allowed_bots` is a defensible default --
# `'*'` would let any installed GitHub App drive a write-capable agent. Only
# the PAIR is wrong, and no single file's review can see the other half. Same
# reasoning as .github/scripts/queue-scan.jq and intake-decision.sh living in
# files: a rule nothing can execute is a rule nothing can check.
#
# So this asserts the rule, not the two instances: wherever a workflow's `if:`
# names a bot actor as admitted, that workflow's `allowed_bots` must cover it.
# A third workflow that grows the same exemption is caught the day it is added.
#
# The normalisation below mirrors isAllowedBot in
# src/github/validation/actor.ts at the action's v1 tag: split on commas, trim,
# lowercase, strip a trailing `[bot]`, on both the list and the actor. So
# `claude` and `claude[bot]` are the same entry, and this test accepts either
# rather than pinning a spelling the action does not care about.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0

ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }

WATCHDOG=.github/workflows/agent-queue-watchdog.yml
AGENT=.github/workflows/claude-agent.yml
REVIEW=.github/workflows/claude-pr-review.yml
REPAIR=.github/workflows/claude-ci-repair.yml

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
echo "Every workflow that admits a bot in its \`if:\` names it in allowed_bots"

# The rule, applied to whatever is in the tree rather than to a list written
# once. `allowed_bots_of FILE` reads the input; `admitted_bots_of FILE` reads
# every `github.actor == '...[bot]'` the file exempts. A third workflow that
# grows the same exemption is checked the day it is added, without this file
# being edited.
allowed_bots_of() {
  local v
  v=$(sed -n 's/^[[:space:]]*allowed_bots:[[:space:]]*//p' "$1" | head -n 1)
  v="${v%\"}"; v="${v#\"}"
  v="${v%\'}"; v="${v#\'}"
  printf '%s' "$v"
}

admitted_bots_of() {
  # Only equality against github.actor counts. A `!endsWith(actor, '[bot]')`
  # is the guard, not an exemption, and must not be read as one.
  grep -oE "github\.actor[[:space:]]*==[[:space:]]*'[^']*\[bot\]'" "$1" \
    | grep -oE "'[^']*'" | tr -d "'"
}

for wf in "$AGENT" "$REVIEW" "$REPAIR"; do
  if [ ! -f "$wf" ]; then
    no "$wf exists" "the file is gone; if a workflow was removed, remove it from this loop in the same commit"
    continue
  fi
  list=$(allowed_bots_of "$wf")
  admitted=$(admitted_bots_of "$wf")

  if [ -z "$admitted" ]; then
    ok "$(basename "$wf") exempts no bot actor, so allowed_bots has nothing to cover"
  else
    while IFS= read -r who; do
      [ -n "$who" ] || continue
      if covers "$list" "$who"; then
        ok "$(basename "$wf") exempts $who and allowed_bots (\"$list\") covers it"
      else
        no "$(basename "$wf") exempts $who and allowed_bots (\"$list\") covers it" \
           "the if: lets $who start the job and the action then refuses it in about five seconds, with no execution log -- and if the step carries continue-on-error the job still reports success"
      fi
    done <<< "$admitted"
  fi

  # The other direction, everywhere. '*' passes every check above and is
  # exactly what none of these inputs may become on a public repository.
  if [ "$(printf '%s' "$list" | tr -d '[:space:]')" = "*" ]; then
    no "$(basename "$wf")'s allowed_bots is not a blanket star" \
       "'*' lets ANY installed GitHub App drive this workflow -- name the actor instead"
  else
    ok "$(basename "$wf")'s allowed_bots is not a blanket star"
  fi
done

# The reviewer is the one whose exemption is load-bearing rather than
# incidental: without it the review is skipped on precisely the pull requests
# it exists to review. Asserted by name so that deleting the exemption is a
# deliberate act with a red test attached, not a tidy-up.
if grep -qE "github\.actor[[:space:]]*==[[:space:]]*'claude\[bot\]'" "$REVIEW"; then
  ok "the reviewer still admits claude[bot], which is what it exists to review"
else
  no "the reviewer still admits claude[bot], which is what it exists to review" \
     "the exemption is gone from $REVIEW; every agent-authored pull request is now unreviewed, and the job will still be green"
fi

echo
echo "Every agent workflow pins its model and its effort level"
# WHY THIS IS A TEST. The action has no `model:` input, so the model is a
# string inside claude_args and an absent string is not an error -- it is a
# silent fall back to whatever the CLI defaults to. That is how this loop ran
# from the day it was built until 2026-08-22 without anything recording which
# model was answering, which makes every past result unattributable and every
# comparison between two runs meaningless.
#
# The flags themselves were read off `claude --help`, not guessed:
#   --model <model>   an alias ('opus', 'sonnet', 'fable') or a full name
#   --effort <level>  one of low, medium, high, xhigh, max
# So a typo'd level is a real failure mode, and the set is closed and short
# enough to assert against rather than pattern-match loosely.
for wf in "$AGENT" "$REVIEW" "$REPAIR"; do
  name="$(basename "$wf")"
  args="$(sed -n '/claude_args:[[:space:]]*|/,/^[[:space:]]*prompt:/p' "$wf")"

  model="$(printf '%s' "$args" | sed -n 's/.*--model[[:space:]]\{1,\}\([^[:space:]]*\).*/\1/p' | head -n 1)"
  if [ -n "$model" ]; then
    ok "$name pins a model (--model $model)"
  else
    no "$name pins a model" \
       "no --model in claude_args; the run silently uses the CLI default and nothing records which model answered"
  fi

  effort="$(printf '%s' "$args" | sed -n 's/.*--effort[[:space:]]\{1,\}\([^[:space:]]*\).*/\1/p' | head -n 1)"
  case "$effort" in
    low|medium|high|xhigh|max)
      ok "$name pins an effort level the CLI accepts (--effort $effort)" ;;
    "")
      no "$name pins an effort level" \
         "no --effort in claude_args; the run uses the default and the setting is invisible to a reader" ;;
    *)
      no "$name pins an effort level the CLI accepts" \
         "--effort $effort is not one of low, medium, high, xhigh, max -- the CLI will reject it and the run dies before the model is reached" ;;
  esac
done

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
