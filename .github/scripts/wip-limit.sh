#!/usr/bin/env bash
# How wide is the queue allowed to be, and how wide is it right now?
#
# CLAUDE.md, "The queue has a width, and it is two" — owner decision OD-23,
# 2026-08-24. Two open working pull requests is normal, three is a ceiling that
# blocks starting anything new, four or more is a queue incident that stops
# feature work until the queue comes back down.
#
# WHY THIS EXISTS AS A CHECK RATHER THAN A PARAGRAPH. The paragraph was not the
# missing part. On 2026-08-24 this repository had thirty-five pull requests open
# at once, and no agent that opened one of them was doing anything the rules
# forbade at the moment it opened it — each was a reasonable next step, and the
# count was nobody's step. A limit that is only ever checked by whoever
# remembers to check it is checked at the end, which is where thirty-five comes
# from. This runs when a pull request is opened, which is the one moment the
# number can still be acted on cheaply.
#
# WHAT IT DOES NOT DO. It does not close, label-block or refuse anything, and it
# has no write access to the repository's contents. It counts, and it says the
# number out loud on the pull request that crossed the line. Refusing the push
# would land the work nowhere — the branch would still exist, the agent would
# still have done it, and the only thing lost would be the record. The queue is
# reduced by finishing pull requests, and no automation can do that part.
#
# The counting itself is deliberately dumb: every open pull request, minus the
# two exemptions, and that is the whole rule. `queue:parked` is work the owner
# agreed to hold; `queue:emergency` is one of the four cases CLAUDE.md names as
# allowed over the limit. A draft is NOT exempt. Draft was the loophole that
# produced the thirty-five: a branch that is being worked on is work in the
# queue whatever bit is set on it, and exempting drafts would make the guard
# agree with the queue instead of measuring it.

set -euo pipefail

# attadipa_wip_verdict COUNT
#
# COUNT  open working pull requests, exemptions already removed. The pull
#        request that triggered the run is included in it: it is the one that
#        made the number what it is, and a limit that excludes the newest entry
#        is a limit one higher.
#
# Prints exactly one line, `<state> <count>`:
#
#   ok N        at or under the normal limit of two
#   full N      three: the ceiling. Nothing new may be started.
#   incident N  four or more: new feature, research and meta work stop.
#
# A count that is not a number prints `unknown` rather than guessing, for the
# same reason promote-decision.sh holds on an unreadable timestamp: this runs
# unattended, and the failure mode of a wrong number here is an agent told the
# queue is fine when it is not.
attadipa_wip_verdict() {
  local count="${1-}"

  case "$count" in
    ''|*[!0-9]*) echo "unknown ${count}"; return 0 ;;
  esac

  # Written as three explicit bands rather than two comparisons, because the
  # middle one is not "over the limit" — three is allowed to exist and is not
  # allowed to grow, and those are different sentences to the agent reading it.
  if [ "$count" -le 2 ]; then
    echo "ok ${count}"
  elif [ "$count" -eq 3 ]; then
    echo "full ${count}"
  else
    echo "incident ${count}"
  fi
}

# attadipa_wip_say STATE COUNT
#
# The sentence that goes on the pull request, or into a log. Kept beside the
# verdict so the two cannot drift, and so the test can assert on both.
attadipa_wip_say() {
  local state="$1" count="$2"
  case "$state" in
    ok)
      echo "**${count} open** — inside the normal limit of two."
      ;;
    full)
      echo "**${count} open — the queue is at its ceiling.** Three is the hard temporary maximum. Finish, merge or close one of these before opening another; do not start new work while it stands at three."
      ;;
    incident)
      echo "**${count} open — QUEUE INCIDENT.** The limit is two, with three as a short-lived maximum. New feature, research and meta work stop until the queue is back to two or three. Triage what is open into: merge now or after a mechanical update · fix once · close as stale or superseded · external blocker returned to an issue. See CLAUDE.md, \"The queue has a width, and it is two\"."
      ;;
    *)
      echo "The number of open pull requests could not be read, so the WIP limit was not checked. This says nothing about this pull request."
      ;;
  esac
}

# Everything below needs the network. Nothing above does, which is what lets
# .github/tests/wip-limit-test.sh execute the decision itself rather than a
# re-implementation of it.
attadipa_wip_count() {
  local repo="$1"
  gh pr list --repo "$repo" --state open --limit 200 \
    --json number,labels \
    --jq '[ .[] | select([.labels[].name] | any(. == "queue:parked" or . == "queue:emergency") | not) ] | length'
}

# Sourced by the test, run by the workflow.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  case "${1-}" in
    --count)
      attadipa_wip_count "${2:-${GITHUB_REPOSITORY:?repository not given}}"
      ;;
    --verdict)
      attadipa_wip_verdict "${2-}"
      ;;
    --say)
      attadipa_wip_say "${2-}" "${3-}"
      ;;
    *)
      repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"
      count="$(attadipa_wip_count "$repo" || echo '')"
      read -r state n <<<"$(attadipa_wip_verdict "$count")"
      printf '%s\n' "$(attadipa_wip_say "$state" "$n")"
      printf 'state=%s\n' "$state" >> "${GITHUB_OUTPUT:-/dev/null}"
      printf 'count=%s\n' "$n" >> "${GITHUB_OUTPUT:-/dev/null}"
      ;;
  esac
fi
