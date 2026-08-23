#!/usr/bin/env bash
# Whose label is `agent:blocked` at the end of a run, and what follows from it?
#
# WHY THIS IS A QUESTION AT ALL. The label is the state -- there is no database
# and no field in a comment -- so `agent:blocked` on an object at hand-over time
# has to answer two different things at once: *this cannot proceed*, which is
# state, and *the agent said so during this run*, which is provenance. The
# hand-over needs the second, because an agent that has written its own BLOCKED
# comment says more than a generated outcome could, and talking over it is the
# noise the reporting protocol exists to prevent.
#
# HOW THAT WENT WRONG, AND IT IS THE THIRD TIME ON THIS ONE LABEL. The
# provenance was first inferred from the label's presence (wrong on a pull
# request, where claude-ci-repair.yml's escalation predates the run), then from
# a snapshot the claim step took BEFORE it stripped the label from issues --
# which is a pre-mutation answer to a post-mutation question. The route that
# broke is the one the watchdog's own escalation comment recommends:
#
#   watchdog escalates    -> agent:blocked + needs-owner
#   a person writes @claude
#   the claim step reads  -> "blocked before: true"
#   the claim step strips -> agent:blocked gone, snapshot still says true
#   the agent re-confirms -> agent:blocked back on, and a BLOCKED comment
#   the hand-over asks    -> snapshot is not `false`, so this is somebody
#                            else's label -> outcome comment "Done", plus
#                            agent:review
#
# A first-class BLOCKED outcome became a success. Reported as #129 against
# 7558728, and right: `main` still had both halves.
#
# SO THE INPUT IS THE POST-CLAIM STATE. claude-agent.yml's claim step now
# re-reads the labels after it has finished editing them and exports
# ATTADIPA_BLOCKED_AT_CLAIM -- an observation of the baseline this run is
# measured against, rather than a snapshot of the moment before the baseline
# was set. `agent:blocked` present now and absent then is this run's doing, and
# nothing else is.
#
# WHY A FILE. The same reason as handover-decision.sh, intake-decision.sh,
# promote-decision.sh and merge-candidate.sh: a rule embedded in a workflow
# cannot be executed, so it cannot be tested, so every defect it has ships.
# Both of this rule's previous defects lived in a YAML block.

# attadipa_blocked_outcome LABELS BLOCKED_AT_CLAIM
#
# LABELS            the label names on the object now, comma separated, as
#                   `[.labels[].name] | join(",")` produces them.
# BLOCKED_AT_CLAIM  `true`, `false` or `unknown` -- whether `agent:blocked` was
#                   on the object AFTER the claim step finished normalising
#                   labels. `unknown` means the read failed.
#
# Prints exactly one word:
#
#   normal  `agent:blocked` is not set. The hand-over proceeds as it always has.
#   silent  it is set and this run set it. The agent's own BLOCKED comment is
#           the report; release the claim and add nothing.
#   report  it is set and this run did not set it, or nobody can tell. Say what
#           happened -- silence is the defect -- but change no state label.
#
# THE THIRD CASE USED TO BE THE FIRST CASE'S OPPOSITE and it is not. Reporting
# and relabelling are different acts, and only one of them is safe here: an
# `unknown` read must still produce a comment, because a finished run that says
# nothing is what all of this is about, and it must NOT produce `agent:review`,
# because an object that carries `agent:blocked` is by definition not finished
# work waiting for a reader. `report` writes words and leaves the state alone.
attadipa_blocked_outcome() {
  local labels="${1-}" at_claim="${2-}"

  # Matched between commas on a joined list, so a label whose name merely
  # contains this one cannot satisfy it. Same reasoning as
  # merge-candidate.sh's whole-line match, which was a substring match until
  # review found that `x ai-review:pass` passed the verdict check.
  case ",$labels," in
    *,agent:blocked,*) ;;
    *) printf 'normal\n'; return 0 ;;
  esac

  # Only an explicit `false` buys silence. `unknown` is a read that failed and
  # `true` is somebody else's escalation; both report.
  if [ "$at_claim" = "false" ]; then
    printf 'silent\n'
  else
    printf 'report\n'
  fi
}

# Callable as a script as well as sourceable, so the workflow can run it without
# worrying about shell inheritance and the test can do either.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_blocked_outcome "${1-}" "${2-}"
fi
