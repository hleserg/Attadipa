#!/usr/bin/env bash
# Should an agent be started for this event?
#
# This is the security boundary of the whole automation loop, so it lives in a
# file that can be executed and tested rather than inside a YAML block that
# cannot. .github/workflows/claude-agent.yml sources it; .github/tests/
# intake-gate-test.sh runs it over a table of cases including real external
# accounts. Neither is a transcription of the other, so the two cannot drift.
#
# It does no network access and reads no environment: everything it decides on
# is an argument. The one fact that does need the network — the actor's
# permission on this repository — is looked up by the caller and passed in,
# which is also what makes the table test possible.

# firefly_intake_decision ACTOR EVENT ACTION LABEL BODY LABELS STATE PERMISSION
#                         [TRUSTED_PRODUCERS]
#
# TRUSTED_PRODUCERS is an optional comma-separated list of GitHub App logins,
# supplied by the workflow from the repository variable FIREFLY_TRUSTED_PRODUCERS.
# It is empty by default, and empty behaves exactly as this gate did before it
# existed.
#
# Prints `accept`, or `reject: <reason>`. Never exits.
firefly_intake_decision() {
  local actor="$1" event="$2" action="$3" label="$4" body="$5" labels="$6" state="$7" permission="$8"
  local trusted_producers="${9:-}"

  # 0. Is this actor a producing app the owner has named?
  #
  # A producing agent may hold no GitHub account of its own. ChatGPT reaches this
  # repository through its GitHub App and arrives as
  # `chatgpt-codex-connector[bot]` with author_association NONE — observed, not
  # assumed: that is the login that reviewed pull request #11 on 2026-08-21. Rule
  # 1 below refuses it, which is right by default and total by accident, because
  # a producer that cannot file a task is a queue with no input.
  #
  # So an app login may be named in FIREFLY_TRUSTED_PRODUCERS. Four things keep
  # that from becoming a hole:
  #
  #   * EMPTY BY DEFAULT. No repository gains an exemption by taking this file.
  #   * `issues` EVENTS ONLY. Never comments. The loop rule 1 exists to prevent
  #     is an agent's own comment mentioning @claude, and no entry in this list
  #     can exempt a comment.
  #   * `claude` AND `github-actions` CAN NEVER BE LISTED. Checked here, after
  #     the list rather than before it, so naming them in the variable does
  #     nothing. Those two are this repository's own output, and letting our
  #     writes drive our writer is the bill that grows until somebody notices.
  #   * The owner editing a repository variable IS the human decision. An app is
  #     not a collaborator and has no permission to look up, so being on the list
  #     is the authorisation — which is why the list is the only way in.
  local exempt=no
  if [ "$event" = "issues" ] && [ -n "$trusted_producers" ]; then
    case "$actor" in
      claude|github-actions|"claude[bot]"|"github-actions[bot]") ;;
      *)
        case ",$trusted_producers," in
          *",$actor,"*) exempt=yes ;;
        esac ;;
    esac
  fi

  # 1. Never react to ourselves, or to any other bot.
  #
  # The action's own allowed_bots default is already "no bots", but the loop
  # this prevents is expensive enough to check twice: a Claude comment that
  # mentions @claude would otherwise start a Claude run that comments, and the
  # bill grows until somebody notices.
  #
  # workflow_dispatch is the one exception, and it is trusted by construction
  # rather than by exemption: GitHub only accepts a manual dispatch from an
  # actor with write access, and the only other way to produce one is a workflow
  # already in this repository. That is how the queue watchdog hands over a task
  # whose event was lost.
  if [ "$event" != "workflow_dispatch" ] && [ "$exempt" != "yes" ]; then
    case "$actor" in
      *"[bot]"|claude|github-actions)
        echo "reject: actor $actor is a bot"; return 0 ;;
    esac

    # 2. The actor must be trusted.
    #
    # `producer: chatgpt` in a task marker proves nothing — anybody with a
    # GitHub account can type it on a public repository. Write access is what
    # cannot be typed.
    case "$permission" in
      admin|maintain|write) ;;
      *) echo "reject: actor $actor has permission '$permission'"; return 0 ;;
    esac
  fi

  if [ "$state" != "open" ]; then
    echo "reject: issue is $state"; return 0
  fi

  # 3. Does anything in this event actually ask for an agent?
  local wanted=no
  case "$event" in
    workflow_dispatch) wanted=yes ;;
    issue_comment|pull_request_review_comment|pull_request_review)
      case "$body" in *"@claude"*) wanted=yes ;; esac ;;
    issues)
      case "$action" in
        labeled)
          # An `x && y` as the last statement of a case branch exits the whole
          # script under `set -e` when x is false. An if does not.
          if [ "$label" = "agent:ready" ]; then wanted=yes; fi ;;
        assigned) wanted=yes ;;
        *)
          case "$body" in
            *"firefly-agent-task"*) case "$body" in *"@claude"*) wanted=yes ;; esac ;;
          esac ;;
      esac ;;
  esac
  if [ "$wanted" != "yes" ]; then
    echo "reject: nothing asks for an agent"; return 0
  fi

  # 4. Deduplicate. An issue already claimed does not need a second writer;
  #    a fresh @claude comment overrides that, because a human asking again is
  #    a decision.
  if [ "$event" = "issues" ] || [ "$event" = "workflow_dispatch" ]; then
    case ",$labels," in
      *,agent:working,*|*,agent:review,*|*,agent:blocked,*|*,agent:done,*)
        echo "reject: already claimed ($labels)"; return 0 ;;
    esac
  fi

  echo "accept"
}

# Callable as a script as well as sourceable, so the workflow can run it
# without worrying about shell inheritance.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  firefly_intake_decision "$@"
fi
