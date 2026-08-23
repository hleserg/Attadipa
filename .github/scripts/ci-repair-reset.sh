#!/usr/bin/env bash
# May this comment clear a pull request's CI escalation?
#
# THE HALF OF THE STATE CONTRACT THAT HAD NO EXIT. claude-ci-repair.yml gives up
# after two attempts on one problem chain and writes `ci:failed` +
# `agent:blocked` on the pull request, which is right: a third automatic attempt
# would be guessing. What it did not have was a way back. `/ci-repair reset`
# clears the attempt COUNTER -- and only the counter, and only the next time CI
# fails, because the counter is read by the `workflow_run` gate and nothing
# reads it at the moment somebody types the command. So a person could diagnose
# the failure, push the fix, watch CI go green, and the pull request would still
# carry `agent:blocked`. merge-candidate.sh holds on that label by name, and the
# backstop routine requires it absent, so a green, reviewed, finished pull
# request was permanently ineligible for an unattended merge with nothing on it
# saying why. Reported as #129 against 7558728.
#
# WHY NOT JUST LET `@claude` CLEAR IT. Because that is the same mistake in the
# other direction, and this repository has already made it once: the claim step
# in claude-agent.yml deliberately does NOT strip `agent:blocked` from a pull
# request, precisely so that a comment cannot dissolve a human escalation and
# hand the branch to the unattended backstop -- see .github/scripts/gh-label.sh
# and .github/tests/blocked-restart-test.sh, which assert that in both
# directions. An escalation to a person is undone by a person, deliberately,
# with a command that says what it is doing and nothing else.
#
# WHY A FILE, AND WHY IT TAKES THE PERMISSION AS AN ARGUMENT. Exactly as
# .github/scripts/intake-decision.sh: this is a trust boundary, it does no
# network access and reads no environment, and the one fact that needs the
# network -- what permission the actor holds -- is looked up by the caller and
# passed in. That is also what makes the table test possible.

# attadipa_ci_reset_decision ACTOR PERMISSION IS_PULL_REQUEST BODY
#
# ACTOR            the login that wrote the comment.
# PERMISSION       that login's permission on this repository, as
#                  `repos/{owner}/{repo}/collaborators/{actor}/permission`
#                  reports it, or `none` when the lookup failed.
# IS_PULL_REQUEST  `true` when the comment is on a pull request.
# BODY             the comment text.
#
# Prints `reset`, or `reject: <reason>`. Never exits.
attadipa_ci_reset_decision() {
  local actor="${1-}" permission="${2-}" is_pr="${3-}" body="${4-}"

  # 1. Only a pull request has a CI escalation to clear. An issue never carries
  #    `ci:failed`, and clearing `agent:blocked` from one is the queue's
  #    business -- AI_TASK_PROTOCOL.md's "restarting an escalated task", which
  #    is two labels or a `@claude` comment and is deliberately not this.
  if [ "$is_pr" != "true" ]; then
    printf 'reject: not a pull request\n'; return 0
  fi

  # 2. The command, and it has to be the whole of a line.
  #
  #    NOT A SUBSTRING, AND THE REASON IS SITTING IN THIS REPOSITORY. The
  #    give-up comment claude-ci-repair.yml writes ends with the sentence that
  #    tells the reader to use this command, so the command's own text appears
  #    in a comment written by `github-actions[bot]` on every escalated pull
  #    request. A substring match would fire on it. The bot check below would
  #    also refuse that particular comment, but relying on a second guard to
  #    cover a first one that is wrong is how a guard stops being read at all --
  #    and the same collision reaches human comments the moment somebody quotes
  #    the command while explaining it, which on a repository whose subject IS
  #    the automation is not a rare shape (intake-decision.sh's header describes
  #    the identical trap being sprung twice in one day on `@claude`).
  #
  #    So: some line of the comment, with surrounding whitespace removed, must
  #    be exactly the command. That accepts a comment that is only the command,
  #    and a comment that puts the command on its own line above an
  #    explanation, and refuses every mention of it inside a sentence, a code
  #    span or a quotation.
  local line found=no
  while IFS= read -r line; do
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    if [ "$line" = "/ci-repair reset" ]; then found=yes; break; fi
  done <<EOF
$body
EOF
  if [ "$found" != yes ]; then
    printf 'reject: no /ci-repair reset on a line of its own\n'; return 0
  fi

  # 3. Never our own output. The give-up comment is written by
  #    `github-actions[bot]`; a repair attempt's marker comment is written by
  #    whichever token claude-ci-repair.yml holds. Neither is a person deciding
  #    that the cause is understood, and this whole file exists to require that
  #    somebody decided.
  case "$actor" in
    *"[bot]"|claude|github-actions)
      printf 'reject: actor %s is a bot\n' "$actor"; return 0 ;;
  esac

  # 4. And the person has to be one of ours. Same boundary as the intake gate:
  #    write access is the thing that cannot be typed into a comment.
  case "$permission" in
    admin|maintain|write) ;;
    *) printf "reject: actor %s has permission '%s'\n" "$actor" "$permission"; return 0 ;;
  esac

  printf 'reset\n'
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_ci_reset_decision "${1-}" "${2-}" "${3-}" "${4-}"
fi
