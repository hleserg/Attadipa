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

# attadipa_asks_for_agent TEXT
#
# Does this text actually ASK for an agent, as opposed to talking about one?
#
# THE TRAP, AND IT HAS NOW BEEN SPRUNG TWICE IN ONE DAY BY THE SAME MISTAKE IN
# TWO SPELLINGS. On 2026-08-21 a commit message quoting `Fixes #10` as an example
# closed issue #10, because GitHub does not distinguish a quoted example from a
# real instruction. On 2026-08-22 a pull request comment explaining why telling
# somebody to write "@claude" was dangerous started an agent on the pull request
# it was written on — every occurrence in that comment was inside a code span,
# and the gate read them all as requests.
#
# That is not a rare shape. This repository's own documentation is about the
# automation, so the people most likely to write `@claude` inside backticks are
# the ones maintaining it, and the cost is a billable writer started by a
# sentence explaining that a billable writer should not be started.
#
# So a mention is a request only when it is OUTSIDE code. Fenced blocks and
# inline spans are removed first, and what is left is what somebody actually
# said. Markdown-aware enough for the shapes that occur here and no more: this
# is a gate, not a parser, and it fails towards not starting an agent.
attadipa_asks_for_agent() {
  local text="$1" line out="" fenced=no

  # 1. Fenced blocks, ``` or ~~~. Everything between the markers goes.
  while IFS= read -r line; do
    case "$line" in
      '```'*|'~~~'*)
        if [ "$fenced" = yes ]; then fenced=no; else fenced=yes; fi
        continue ;;
    esac
    if [ "$fenced" = no ]; then
      out="$out$line"$'\n'
    fi
  done <<EOF
$text
EOF

  # 2. Inline code spans, one matched pair at a time. An unmatched trailing
  #    backtick ends the loop rather than eating the rest of the comment.
  local pre rest post
  while [ "${out#*\`}" != "$out" ]; do
    rest="${out#*\`}"
    if [ "${rest#*\`}" = "$rest" ]; then break; fi
    pre="${out%%\`*}"
    post="${rest#*\`}"
    out="$pre$post"
  done

  case "${out,,}" in
    *"@claude"*) return 0 ;;
  esac
  return 1
}

# attadipa_intake_decision ACTOR EVENT ACTION LABEL BODY LABELS STATE PERMISSION
#                         [TRUSTED_PRODUCERS] [COMMENT]
#
# TRUSTED_PRODUCERS is an optional comma-separated list of GitHub App logins,
# supplied by the workflow from the repository variable ATTADIPA_TRUSTED_PRODUCERS.
# It is empty by default, and empty behaves exactly as this gate did before it
# existed.
#
# BODY and COMMENT ARE NOT THE SAME TEXT, and collapsing them cost this queue a
# day. BODY is the issue or pull request body, read from the API, and it is what
# carries the task marker. COMMENT is the text of the comment or review that
# raised THIS event, and it is the only place an `@claude` mention can live on a
# comment event. Until 2026-08-22 the workflow passed the issue body in both
# roles, so rule 3 below looked for `@claude` in a text that could not contain
# it: every `@claude` comment ever written on this repository was refused with
# "nothing asks for an agent", including the owner's. The tests did not catch it
# because they passed the comment text in the BODY slot — which is to say they
# tested the function and not the call.
#
# Prints `accept`, or `reject: <reason>`. Never exits.
attadipa_intake_decision() {
  local actor="$1" event="$2" action="$3" label="$4" body="$5" labels="$6" state="$7" permission="$8"
  local trusted_producers="${9:-}" comment="${10:-}"

  # 0. Is this actor a producing app the owner has named?
  #
  # A producing agent may hold no GitHub account of its own. ChatGPT reaches this
  # repository through its GitHub App and arrives as
  # `chatgpt-codex-connector[bot]` with author_association NONE — observed, not
  # assumed: that is the login that reviewed pull request #11 on 2026-08-21. Rule
  # 1 below refuses it, which is right by default and total by accident, because
  # a producer that cannot file a task is a queue with no input.
  #
  # So an app login may be named in ATTADIPA_TRUSTED_PRODUCERS. Four things keep
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
  #
  # Matched case-insensitively, on both halves. A person typing `@Claude` at the
  # start of a sentence is asking for exactly the same thing as one typing
  # `@claude`, and a gate that silently disagrees teaches people the automation
  # is broken rather than that their capital letter was. `${x,,}` is bash's own
  # lowercasing and is multibyte-safe, which matters because these comments are
  # frequently in Russian.
  local wanted=no
  local lc_body="${body,,}"
  case "$event" in
    workflow_dispatch) wanted=yes ;;
    issue_comment|pull_request_review_comment|pull_request_review)
      # COMMENT, not BODY. See the header. And a mention inside code is somebody
      # writing ABOUT the agent, not to it — see attadipa_asks_for_agent.
      if attadipa_asks_for_agent "$comment"; then wanted=yes; fi ;;
    issues)
      case "$action" in
        labeled)
          # An `x && y` as the last statement of a case branch exits the whole
          # script under `set -e` when x is false. An if does not.
          if [ "$label" = "agent:ready" ]; then wanted=yes; fi ;;
        assigned) wanted=yes ;;
        *)
          # One marker string, and ADR-0012 is why there is no second one:
          # "no compatibility aliases for the previous identifier are retained".
          # Whether a legacy marker should be honoured is #25's question, and
          # answering it here would also need queue-scan.jq — the watchdog reads
          # its own copy of this test, and a marker only half the pipeline knows
          # is a task that cannot be recovered when a run is lost.
          case "$lc_body" in
            *"attadipa-agent-task"*)
              if attadipa_asks_for_agent "$body"; then wanted=yes; fi ;;
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
  # `--mentions TEXT` is the same question the gate asks in rule 3, exposed so
  # that claude-agent.yml can decide whether a refusal is worth explaining
  # without a second, drifting copy of the code-stripping logic.
  if [ "${1:-}" = "--mentions" ]; then
    attadipa_asks_for_agent "${2:-}"
  else
    attadipa_intake_decision "$@"
  fi
fi
