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
# THE AUTHOR FILTER IS THE SAME ONE queue-scan.jq APPLIES, and it is here for
# a reason found in review of #85. Without it this sweep re-queues issues the
# scan will never pick: an issue whose author is neither OWNER/MEMBER/
# COLLABORATOR nor named in ATTADIPA_TRUSTED_PRODUCERS — the realistic case is
# a producer dropped from that variable after its issue failed — would get
# `agent:ready` added and a comment saying the next tick decides. The scan then
# drops it silently on the author check, and this sweep never looks at it again
# because it now carries `agent:ready`. Stranded again, with a comment on the
# issue saying it is not: #82's own shape inside the file meant to prevent it.
# So an issue nothing will pick up is left alone rather than told otherwise,
# and the two filters agree about who counts.
#
# .github/tests/stranded-failures-test.sh runs it over fixtures; CI runs that.
#
# Input:  the GitHub issues API response, as an array.
# Arg:    $trusted — comma-separated producer app logins, from
#         ATTADIPA_TRUSTED_PRODUCERS. Read via $ARGS.named so a caller that has
#         not been taught to pass it still runs; absent means the empty string,
#         which trusts nobody extra. May be empty.
# Output: the stranded issue numbers, one per line, in no particular order.

($ARGS.named.trusted // "") as $trusted_producers
| [ .[]
  | select(.pull_request == null)
  | select(.author_association == "OWNER" or .author_association == "MEMBER" or .author_association == "COLLABORATOR"
           or (.user.login as $login
               # Repeated rather than shared, for the reason queue-scan.jq
               # gives at the same place: `claude` and `github-actions` must
               # never be listable, and a rule that lives in one file is a rule
               # the other path does not have.
               | ($login | test("^(claude|github-actions)(\\[bot\\])?$")) as $internal
               | if $internal then false
                 else ($trusted_producers | split(",") | index($login) != null) end))
  | {number, labels: [.labels[].name]}
  | select((.labels | index("agent:failed")) != null)
  | select((.labels | index("agent:ready")) == null)
  | select((.labels | index("agent:working")) == null)
  | select((.labels | index("agent:blocked")) == null)
  | select((.labels | index("agent:review")) == null)
  | .number
]
| .[]
