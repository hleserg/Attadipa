# Which open issues carry `agent:failed` with nothing that says what happens
# next?
#
# The hand-over pairs `agent:failed` with `agent:ready` deliberately, on every
# generic failure (see .github/workflows/claude-agent.yml's "Hand over" step) —
# but that pairing started only on 2026-08-22, and before it a failure left
# only `agent:failed` behind, with `agent:working` removed and nothing
# re-added. #27 and #28 were found by #82 in exactly that state: two failures
# each, no `agent:ready`, no `agent:blocked`, invisible to
# .github/scripts/queue-scan.jq either way and unexplained to anybody reading
# the issue — both were relabelled by hand ahead of this fix, so this filter
# matches nothing in this repository today. It stays as a guard against the
# same shape recurring — an interrupted run, a `gh issue edit` that errors
# before it finishes and leaves `agent:working` removed without anything put
# back — not because it is repairing a live condition.
#
# `agent:review` is excluded for the same reason `queue-scan.jq` excludes it:
# a run that pushed a commit and then failed to finish is `done_*_cut` in
# .github/scripts/handover-decision.sh, which labels the issue `agent:review`
# and leaves `agent:failed` in place if an earlier `gh issue edit` failed to
# remove it (`.github/workflows/claude-agent.yml`'s claim step removes it with
# `|| true`). That issue has real work awaiting review, not a task nobody
# queued — this file must not tell its reader otherwise.
#
# .github/tests/stranded-failures-test.sh runs it over fixtures; CI runs that.
#
# Input:  the GitHub issues API response, as an array.
# Output: the stranded issue numbers, one per line, in no particular order.

[ .[]
  | select(.pull_request == null)
  | {number, labels: [.labels[].name]}
  | select((.labels | index("agent:failed")) != null)
  | select((.labels | index("agent:ready")) == null)
  | select((.labels | index("agent:working")) == null)
  | select((.labels | index("agent:blocked")) == null)
  | select((.labels | index("agent:done")) == null)
  | select((.labels | index("agent:review")) == null)
  | .number
]
| .[]
