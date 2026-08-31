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

# A workflow's CONTENT, with full-line comments removed. Not cosmetic: the
# comment above claude-pr-review.yml's guard quotes the condition it replaced,
# and a scanner that cannot tell a comment from a condition reads the removed
# defect as still present -- and would go on passing after the condition was
# deleted for real. Every scan below reads this rather than the raw file.
yaml_body() { sed 's/^[[:space:]]*#.*//' "$1"; }

admitted_bots_of() {
  # Only equality against github.actor counts. A `!endsWith(actor, '[bot]')`
  # is the guard, not an exemption, and must not be read as one.
  yaml_body "$1" \
    | grep -oE "github\.actor[[:space:]]*==[[:space:]]*'[^']*\[bot\]'" \
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

echo
echo "A guard that excludes bots by NAME admits every other bot, ours included"

# THE OTHER HALF OF THE SAME RULE, AND THE ONE #332 NEEDED.
#
# `admitted_bots_of` reads `==` exemptions, which is the right question for a
# guard shaped `!endsWith(actor, '[bot]') || actor == 'x[bot]'`. A guard built
# the other way round -- exclude one name, admit the rest -- has no `==` to
# read, so the loop above would go vacuous on it while every bot the deny-list
# does not name walks into an action that refuses them in five seconds.
#
# That is not hypothetical. `github.actor` on a `pull_request` is the pusher,
# not the author, so an ordinary agent branch updated by our own sweep arrives
# as `github-actions[bot]`; run 33156056991 on #330 is the skip, and
# `automation/171-branch-update-sweep` is the branch where the same head passed
# under `claude[bot]` and was skipped under `github-actions[bot]`.
#
# The rule: where a workflow's guard excludes bots by name, every one of this
# repository's own bot actors that the deny-list does not name is admitted, and
# `allowed_bots` must cover it. A workflow with no bot condition at all is not
# this rule's business -- nothing has shown that shape coming apart, and
# inventing a reachability analysis for it is how a test starts asserting a
# model rather than the system.

# This repository's own bot actors: the two logins our automation can present
# as. The same pair queue-scan.jq refuses as producers, which is not a
# coincidence and is still a different rule.
OWN_BOTS="claude github-actions"

denied_bots_of() {
  yaml_body "$1" \
    | grep -oE "github\.actor[[:space:]]*!=[[:space:]]*'[^']*\[bot\]'" \
    | grep -oE "'[^']*'" | tr -d "'"
}

# `grep -E ... >/dev/null` rather than `grep -qE`, and the difference is not
# style. `set -o pipefail` is on at the top of this file; `-q` exits at the
# first match, the upstream `sed` takes SIGPIPE, and pipefail then reports the
# whole pipeline as FAILED -- so the predicate answers "no suffix guard" at
# exactly the moment there is one. That silently un-failed the mutation that
# restores the #332 defect while this file was being written.
suffix_guarded() { yaml_body "$1" | grep -E "endsWith\([[:space:]]*github\.actor" >/dev/null; }

for wf in "$AGENT" "$REVIEW" "$REPAIR"; do
  [ -f "$wf" ] || continue
  name="$(basename "$wf")"
  denied="$(denied_bots_of "$wf" | tr '\n' ',')"

  if [ -z "$denied" ]; then
    ok "$name has no by-name bot exclusion, so this rule has nothing to say about it"
    continue
  fi

  if suffix_guarded "$wf"; then
    no "$name names what it excludes instead of testing the [bot] suffix" \
       "it does both: a deny-list AND endsWith(github.actor, ...). Together they are the condition #332 was about, and which one wins is not obvious to a reader"
  else
    ok "$name names what it excludes instead of testing the [bot] suffix"
  fi

  list=$(allowed_bots_of "$wf")
  for who in $OWN_BOTS; do
    if covers "$denied" "$who"; then
      ok "$name deliberately excludes $who, so allowed_bots need not cover it"
    elif covers "$list" "$who"; then
      ok "$name admits $who and allowed_bots (\"$list\") covers it"
    else
      no "$name admits $who and allowed_bots (\"$list\") covers it" \
         "the deny-list does not name $who, so $who starts the job and the action then refuses it in about five seconds -- and with continue-on-error the job still reports success. That is #332"
    fi
  done
done

echo
# The reviewer's admission of claude[bot] is load-bearing rather than
# incidental: without it the review is skipped on precisely the pull requests it
# exists to review. It used to be an `==` exemption and is now carried by the
# guard not naming claude, so assert the property rather than the spelling --
# the loop above would otherwise report an excluded claude as a deliberate
# choice and pass.
if denied_bots_of "$REVIEW" | grep -iE '^claude(\[bot\])?$' >/dev/null; then
  no "the reviewer still admits claude[bot], which is what it exists to review" \
     "$REVIEW's guard now excludes claude by name; every agent-authored pull request is unreviewed, and the job will still be green"
elif suffix_guarded "$REVIEW"; then
  no "the reviewer still admits claude[bot], which is what it exists to review" \
     "$REVIEW is back to a [bot] suffix test, which excludes claude[bot] unless something else re-admits it -- the defect #332 removed"
else
  ok "the reviewer still admits claude[bot], which is what it exists to review"
fi

# And the direction #332's Definition of Done names second: the guard has to go
# on excluding what it was actually for. Dependabot opens four dependency bumps
# at a time and none of them wants an architecture review. Tied to
# dependabot.yml, so removing the source of those pull requests relaxes this
# rather than leaving a rule about a bot that no longer arrives.
if [ -f .github/dependabot.yml ]; then
  if denied_bots_of "$REVIEW" | grep -iE '^dependabot(\[bot\])?$' >/dev/null; then
    ok "the reviewer still excludes dependabot[bot], and by name"
  else
    no "the reviewer still excludes dependabot[bot], and by name" \
       ".github/dependabot.yml is configured, so the bumps arrive, and nothing in $REVIEW's guard keeps them out of an architecture review"
  fi
else
  ok "no .github/dependabot.yml, so the reviewer has nothing to exclude"
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
