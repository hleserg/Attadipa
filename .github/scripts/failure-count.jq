# How many times has `agent:failed` been labelled on this issue since a
# human last put it back in the queue?
#
# The hand-over labels `agent:failed` and `agent:ready` together,
# automatically, every time a run fails — so counting `agent:failed` events
# over the issue's whole lifetime overcounts as soon as somebody actually
# intervenes: an owner who reads why an issue failed, fixes the cause and adds
# `agent:ready` by hand would be charged for every failure that happened
# *before* their fix, and get bounced straight back to `agent:blocked` on the
# very next tick with zero retries of their own. Found in review on #82's own
# pull request, reproduced against #27, #28 and #69's real history — #69 had
# already failed twice before this file existed, so an unqualified count
# would keep it permanently one failure short of ever running again.
#
# The reset point is the most recent `labeled agent:ready` event whose actor
# is not the hand-over itself. `github-actions[bot]` and `claude[bot]` are the
# only identities that add `agent:ready` automatically — the same pair
# `.github/scripts/queue-scan.jq` refuses to honour in
# ATTADIPA_TRUSTED_PRODUCERS, for the same reason: they are the repository's
# own output, not a person. Anything else adding `agent:ready` is a person,
# and the count restarts there. No such event at all means nobody has
# intervened yet, so this counts from the beginning.
#
# .github/tests/failure-count-test.sh runs it over fixtures; CI runs that.
#
# Input:  an issue's /timeline API response, as a flat array of events (one
#         page, or every page already merged with `jq add`).
# Output: an integer.

def is_bot: test("^(claude|github-actions)(\\[bot\\])?$");

. as $events
| ($events
   | map(select(.event == "labeled"
                and .label.name == "agent:ready"
                and (((.actor.login // "") | is_bot) | not)))
   | max_by(.created_at)
   | .created_at
  ) as $reset
| [$events[]
   | select(.event == "labeled" and .label.name == "agent:failed")
   | select($reset == null or .created_at > $reset)
  ]
| length
