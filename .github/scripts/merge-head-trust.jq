# When did this pull request's head commit arrive, and was the reviewer's
# verdict reached on THAT commit?
#
# Both questions used to be answered by one expression in the sweep:
#
#     COMMITTED = (.pushedDate // .committedDate)
#
# and both answers were therefore the git committer clock, which is an input.
# `GIT_COMMITTER_DATE=2020-01-01 git commit` produces a head that is six hours
# old the instant it exists and that every earlier `ai-review:pass` labelling
# post-dates. The workflow's own comment said `committedDate` was defeatable and
# fell back to it anyway, on the grounds that `pushedDate` is null "for some
# commits". It is null for ALL of them: the field is deprecated in GitHub's
# GraphQL schema and answered `null` beside a live `committedDate` for the head
# of this repository's #193 on 2026-08-24. So the fallback was not the narrow
# case, it was the only branch ever taken. Issue #199.
#
# WHAT IS TRUSTED INSTEAD, and why it is the right primitive. When a commit
# becomes the head of a pull request, GitHub creates workflow runs for it, and
# it stamps them itself:
#
#     commits(last:1) { nodes { commit { oid checkSuites { nodes {
#       workflowRun { createdAt event } } } } } }
#
# `workflowRun.createdAt` is written by GitHub when it decides to start work, it
# is attached to THAT object id, and nothing a committer can put in a commit
# reaches it. Observed on #193 on 2026-08-24: `committedDate 18:29:57Z`,
# `workflowRun.createdAt 18:30:08Z`, eleven seconds later — GitHub noticing the
# push, not the author describing it.
#
# `event == "pull_request"` narrows it to the event that MEANS "this commit is
# now the head of a pull request". A `push` or `schedule` run on the same object
# says nothing about a pull request. The maximum over those stamps is taken, not
# the minimum, and that direction is the safe one in both cases it matters:
#
#   * a commit that already existed elsewhere in the repository carries older
#     runs, and its arrival HERE creates a newer one, so the maximum is the
#     arrival and the older objects cannot age it;
#   * a commit that is the head of two pull requests takes the later of the two
#     stamps, which shortens the settling window and lengthens what the verdict
#     has to post-date. Being wrong in that direction holds; being wrong in the
#     other merges.
#
# AND THE VERDICT BINDING FALLS OUT OF THE SAME NUMBER. `ai-review:pass` records
# that a verdict was reached, never which commit it was reached on, so the
# labelling is dated against the arrival above rather than against the commit's
# own claim about itself. A new head necessarily gets a new `pull_request` run,
# so it necessarily post-dates every label set before it, whatever date the
# commit carries. Both timestamps in that comparison are GitHub's.
#
# THERE IS NO FALLBACK, AND ADDING ONE IS THE DEFECT. Where the arrival cannot
# be established this prints HOLD. `committedDate`, `authoredDate` and
# `pushedDate` are not read here, and `merge-facts.graphql` no longer asks for
# them, so re-deriving one takes an edit to two files and a red test.
#
# Prints exactly one line, and there is no path through this that prints nothing:
#
#   HOLD <reason>                     the head could not be identified or timed
#   TRUSTED <oid> <verdict> <age>     oid is the head's object id; verdict is
#                                     true / false / unknown for whether the
#                                     latest `ai-review:pass` labelling is not
#                                     older than the arrival; age is that
#                                     arrival's age in whole seconds
#
# `unknown` rather than a refusal when no `ai-review:pass` labelling is on
# record, because that case already has a more precise answer waiting in
# merge-candidate.sh — `HOLD no ai-review:pass` — and it is checked first.
#
# In a file of its own for the reason merge-facts.jq is: a filter inside a shell
# string inside a YAML block cannot be executed by a test, and this one decides
# whether a robot may write to `main`. .github/tests/merge-candidate-test.sh
# runs it over documents shaped like GitHub's replies; CI runs that.

def hold($why): "HOLD \($why)";

# A timestamp is a string GitHub wrote, or it is not a timestamp. `null` here
# means unreadable, and every caller below turns unreadable into a HOLD rather
# than into "no such event".
def epoch($t):
  if ($t | type) != "string" then null
  else (try ($t | fromdateiso8601) catch null)
  end;

.data.repository.pullRequest as $pr
| if $pr == null or ($pr | type) != "object" then
    hold("the response carries no pull request, so nothing about its head is known")
  else
    ( try ($pr.commits.nodes[0].commit) catch null ) as $head
    | if $head == null or ($head | type) != "object" then
        hold("the head commit is missing from the response")
      elif ($head.oid | type) != "string"
           or ($head.oid | test("^[0-9a-f]{40}([0-9a-f]{24})?$") | not) then
        # No identity, no binding. A verdict can only be bound to a commit that
        # the response actually named.
        hold("the head commit has no usable object id, so no verdict can be bound to it")
      else
        $head.oid as $oid
        # The connection has to BE one. A missing or malformed `checkSuites` is
        # not a commit GitHub has never started work on -- that case is an empty
        # list, and it is answered separately below.
        | ( if ($head.checkSuites | type) != "object"
               or ($head.checkSuites | has("nodes") | not)
               or ($head.checkSuites.nodes | type) != "array" then null
            else [ $head.checkSuites.nodes[]
                   | select(type == "object")
                   | .workflowRun
                   | select(type == "object")
                   | select((.event? // "") == "pull_request")
                   | .createdAt ]
            end ) as $stamps
        | if $stamps == null then
            hold("the head commit's check suites came back unreadable, so when it arrived is unproven")
          elif ($stamps | length) == 0 then
            # Every open pull request on this repository raises `pull_request`
            # runs -- ci.yml and codeql.yml carry no path filter and the
            # reviewer fires on synchronize -- so an empty list is a state
            # nobody has explained, and an unexplained state does not merge.
            hold("GitHub has started no pull-request workflow run on \($oid[0:8]), so nothing says when this head arrived")
          else
            [ $stamps[] | epoch(.) ] as $epochs
            | if ($epochs | any(. == null)) then
                hold("a workflow run on \($oid[0:8]) carries a timestamp that cannot be read")
              else
                ($epochs | max) as $arrived
                # Every `ai-review:pass` labelling in the window, newest first.
                # An event outside a `last:` window is older than everything in
                # it and cannot raise this maximum, which is why a truncated
                # window is safe here rather than merely unlikely.
                | [ $pr.timelineItems.nodes[]?
                    | select(type == "object")
                    | select((.label?.name? // "") == "ai-review:pass")
                    | .createdAt ] as $passes
                | ( if ($passes | length) == 0 then "unknown"
                    else [ $passes[] | epoch(.) ] as $pe
                      | if ($pe | any(. == null)) then null
                        elif ($pe | max) >= $arrived then "true"
                        else "false"
                        end
                    end ) as $verdict
                | if $verdict == null then
                    hold("a labelling event on this pull request carries a timestamp that cannot be read")
                  else
                    # Clamped at zero. A stamp in the future is a clock
                    # disagreement, and a negative age would arrive at
                    # merge-candidate.sh as "unknown" -- a true sentence about
                    # the wrong thing. Zero is the honest reading: nothing here
                    # proves this head is old.
                    (($now - $arrived) | if . < 0 then 0 else . end | floor) as $age
                    | "TRUSTED \($oid) \($verdict) \($age)"
                  end
              end
          end
      end
  end

