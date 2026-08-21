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
# Output: one issue number, or an empty line when nothing is waiting.

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
  | select((.labels | index("agent:failed")) == null)
  | {
      number,
      rank: (if   (.labels | index("priority:P0")) then 0
             elif (.labels | index("priority:P1")) then 1
             elif (.labels | index("priority:P2")) then 2
             elif (.labels | index("priority:P3")) then 3
             else 2 end)
    }
]
| sort_by(.rank, .number)
| .[0].number // ""
