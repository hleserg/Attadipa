# Which decision-critical GraphQL connection, if any, the merge sweep could not
# prove it read all of.
#
# Prints one line: the empty string when every connection proved complete, or
# the reason the first incomplete one gives. `.github/scripts/merge-facts.sh`
# turns that into COMPLETE or HOLD; the argument for why any of this exists is
# in that file, and the query it reads is `.github/scripts/merge-facts.graphql`.
#
# In a file of its own for the reason `queue-scan.jq` and `failure-count.jq`
# are: a filter inside a shell string inside a YAML block is not executable by
# a test, and this one decides whether a robot may write to `main`. It also
# spent one draft as a quoted shell argument, where every apostrophe in a
# comment closed the quote — which is a small argument for the same thing.
#
# .github/tests/merge-candidate-test.sh runs it over real response shapes; CI
# runs that.

# A connection is complete when the SCHEMA says it has no next page.
#
# Never when it looks full. `nodes | length == 100` cannot tell an exactly-full
# page from a truncated one, so it either lets truncation through or holds every
# pull request that lands exactly on the boundary — and it is guessing in both
# directions while `pageInfo` answers exactly.
#
# `totalCount` is not the test either, and could not be: on a FILTERED
# connection GitHub does not count the filtered set. Against this repository's
# pull request #173 on 2026-08-24, `timelineItems(last:100,
# itemTypes:[LABELED_EVENT])` answered `totalCount: 15` beside a single node,
# while `pageInfo` on the same response respected the filter. So `totalCount`
# appears below only inside the sentence a human reads.
#
# EVERY UNREADABLE SHAPE HOLDS. Missing, null, not an object, no `nodes`, no
# `pageInfo`, a null `pageInfo`, a `pageInfo` without `hasNextPage`, a
# `hasNextPage` that is not a boolean. None of them may read as "nothing there",
# because "nothing there" is `0 unresolved threads`, and `0` is the value that
# merges.
def incomplete($what; $hides; $conn):
  if $conn == null then
    "\($what) could not be read at all, so \($hides) cannot be ruled out"
  elif ($conn | type) != "object" then
    "\($what) came back as \($conn | type) rather than a connection"
  elif ($conn | has("nodes") | not) or ($conn.nodes | type) != "array" then
    "\($what) came back without a list of its own contents"
  elif ($conn | has("pageInfo") | not) or ($conn.pageInfo == null) then
    "\($what) came back without pageInfo, so its completeness is unproven"
  elif ($conn.pageInfo | type) != "object" then
    "\($what) came back with a pageInfo that is not an object"
  elif ($conn.pageInfo | has("hasNextPage") | not)
       or ($conn.pageInfo.hasNextPage | type) != "boolean" then
    "\($what) came back with no usable hasNextPage, so its completeness is unproven"
  elif $conn.pageInfo.hasNextPage then
    # `totalCount` IS THE CONNECTION'S OWN COUNT AND DOES NOT RESPECT `itemTypes`
    # -- established by the live read against #173, and recorded in
    # REUSE_LEDGER.md. On a filtered connection the "of N" is therefore an upper
    # bound on the set, not its size, and a message could read "truncated at 1
    # of 15" about a page holding every labelling there is. Only `timelineItems`
    # is filtered, and its branch cannot fire (see below), so nothing prints
    # this today; it is written here because the next filtered connection added
    # would inherit the wording silently.
    ( ($conn.nodes | length) as $read
      | if ($conn.totalCount | type) == "number" and $conn.totalCount > $read then
          "\($what) is truncated at \($read) of \($conn.totalCount), so \($hides) cannot be ruled out"
        else
          "\($what) is truncated at \($read), so \($hides) cannot be ruled out"
        end )
  else
    empty
  end;

.data.repository.pullRequest as $pr
| if $pr == null or ($pr | type) != "object" then
    "the response carries no pull request"
  else
    ( try ($pr.commits.nodes[0].commit) catch null ) as $head
    | [
        # THE WORST OF THE FIVE, and it is the labels. It is the only one whose
        # truncation turns a refusal into a MERGE rather than into a hold:
        # `ai-review:blocking`, `agent:blocked` and `needs-owner` are read by
        # their presence, so a page that does not reach them says they are not
        # set. A truncated page that hides `ai-review:pass` only holds.
        incomplete("the label list"; "a blocking label"; $pr.labels),

        # `[ .nodes[] | select(.isResolved | not) ] | length` over a truncated
        # page is the finding this file was written for: 101 threads, the first
        # hundred resolved, answer zero.
        incomplete("the review-thread list"; "an unresolved one"; $pr.reviewThreads),

        # The path allowlist is applied to these nodes one at a time, so a path
        # off the list that sits past the end of the page is a change set that
        # reads as permitted.
        incomplete("the changed-file list"; "a path off the allowlist"; $pr.files),

        # THIS ONE CANNOT FIRE, AND IT IS HONEST TO SAY SO. `hasNextPage` is
        # true only under forward pagination or with `before:` set; a `last:`
        # page is the "otherwise" and answers false whatever it contains, so
        # this refusal is a tautology rather than a check. It stays for shape.
        # The real guarantee is that an event outside the window is older than
        # everything inside it and so cannot raise the maximum: a truncated
        # window produces no date, and the caller holds on `unknown`.
        #
        # `hasPreviousPage` is routinely true on a busy pull request and is
        # deliberately not a refusal. An older labelling cannot raise a maximum;
        # and if the latest pass event is older than this whole window then no
        # pass event is inside it at all, the date comes back empty, and
        # merge-candidate.sh holds under "could not tell whether ai-review:pass
        # covers the head commit". Refusing on it would hold every pull request
        # with a hundred label events and prove nothing extra.
        incomplete("the label timeline"; "a newer ai-review:pass event"; $pr.timelineItems),

        ( if $head == null or ($head | type) != "object" then
            "the head commit is missing from the response"
          elif $head.statusCheckRollup == null then
            # A NULL ROLLUP IS NOT A TRUNCATION AND MUST NOT BE READ AS ONE.
            # GitHub returns null when the head commit carries no check run and
            # no commit status at all — observed on this repository's #176 on
            # 2026-08-24 — and that state already has a precise answer waiting
            # for it downstream: `HOLD no check run on the head commit`. Calling
            # it unreadable here would replace that with a vaguer refusal and
            # teach the reader to distrust a message that is doing its job.
            empty
          else
            incomplete("the check list"; "a failing or pending check"; $head.statusCheckRollup.contexts)
          end )
      ]
    | first // ""
  end
