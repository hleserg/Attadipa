# Which open issue should the watchdog hand to the agent next?
#
# This lives in a file rather than inside the workflow's YAML for the same reason
# .github/scripts/intake-decision.sh does: a filter that cannot be executed cannot
# be tested, and this one is part of the security boundary. It reached `main` in a
# state where naming `claude[bot]` in ATTADIPA_TRUSTED_PRODUCERS would have let the
# repository's own output start a billable writer — caught by review, not by a
# test, because there was no test to catch it.
#
# .github/tests/watchdog-filter-test.sh runs it over fixtures; CI runs that.
#
# Input:  the GitHub issues API response, as an array.
# Arg:    $trusted — comma-separated producer app logins, from
#         ATTADIPA_TRUSTED_PRODUCERS. May be empty.
# Arg:    $exclude — comma-separated issue numbers to skip this round. Optional
#         (read via $ARGS.named so an older caller that has not been taught to
#         pass it yet still runs) and may be empty. The caller uses this to
#         move past a candidate it has already looked at and bounced this
#         round — see below.
# Output: "NUMBER FAILED", or an empty line when nothing is waiting. FAILED is
#         "1" when the pick carries `agent:failed` and "0" otherwise, because
#         picking it is not the end of the caller's decision: `agent:ready`
#         and `agent:failed` together mean "this failed once and was put back
#         in the queue" (see .github/workflows/claude-agent.yml's hand-over
#         step), and a repository that retries a deterministic failure every
#         hour forever is buying the same answer over and over — six runs on
#         2026-08-22 were exactly that, at a real bill. The bound on how many
#         times that is allowed lives in the caller (issue #82), because it
#         needs the issue's timeline, which is not in this file's input.

[ .[]
  | select(.pull_request == null)
  | select(.author_association == "OWNER" or .author_association == "MEMBER" or .author_association == "COLLABORATOR"
           or (.user.login as $login
               # The same non-listable rule as the intake gate, and it
               # has to be repeated HERE rather than trusted to live
               # there, because this path does not go through there.
               #
               # The watchdog hands over by workflow_dispatch, and the
               # gate trusts workflow_dispatch by construction — it
               # skips the actor check entirely. So a `claude[bot]`
               # entry in ATTADIPA_TRUSTED_PRODUCERS, which the gate
               # refuses to honour, would be honoured here and then
               # dispatched into a gate that no longer asks. Our own
               # output would start a billable writer: exactly the loop
               # the allowlist was built to avoid.
               | ($login | test("^(claude|github-actions)(\\[bot\\])?$")) as $internal
               | if $internal then false
                 else ($trusted | split(",") | index($login) != null) end))
  | {
      number,
      labels: [.labels[].name],
      body: (.body // "")
    }
  | select((.labels | index("agent:ready")) != null
           or ((.body | test("attadipa-agent-task"))
               and (.body | test("@claude"))))
  | select((.labels | index("agent:working")) == null)
  | select((.labels | index("agent:review")) == null)
  | select((.labels | index("agent:blocked")) == null)
  | select((.labels | index("agent:done")) == null)
  # `agent:failed` alone (no `agent:ready` beside it) is not "waiting" — it is
  # a task the hand-over never re-queued, and it needs a person rather than a
  # silent hourly retry. `agent:failed` WITH `agent:ready` is the pair the
  # hand-over deliberately writes on a generic failure, and dropping it here
  # was the bug #82 found: the hand-over comment promises a pick-up that this
  # filter refused to deliver.
  | select((.labels | index("agent:failed")) == null
           or (.labels | index("agent:ready")) != null)
  | . as $issue
  # $ARGS.named rather than a bare $exclude reference: it is always defined,
  # even against an older caller that has not been taught to pass --arg
  # exclude yet, so this file cannot become the thing that breaks the
  # watchdog the next time it changes shape.
  | select((($ARGS.named.exclude // "") | split(",") | index($issue.number | tostring)) == null)
  | {
      number,
      failed: ((.labels | index("agent:failed")) != null),
      rank: (if   (.labels | index("priority:P0")) then 0
             elif (.labels | index("priority:P1")) then 1
             elif (.labels | index("priority:P2")) then 2
             elif (.labels | index("priority:P3")) then 3
             else 2 end)
    }
]
| sort_by(.rank, .number)
| if length == 0 then "" else "\(.[0].number) \(.[0].failed | if . then "1" else "0" end)" end
