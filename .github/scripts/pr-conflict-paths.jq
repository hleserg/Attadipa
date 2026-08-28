# Which paths is a merge conflict in, said from two compare responses and
# without a checkout.
#
# WHY THIS SHAPE, and what it is honestly claiming. There is no GitHub API that
# returns the conflicted files of a pull request. `PUT .../update-branch` and
# `POST .../merges` both refuse on conflict without saying where, and neither
# GraphQL's `mergeable` nor `mergeStateStatus` carries a path. The exact set
# comes from `git merge-tree`, which needs both histories in a working copy --
# a full fetch of every branch on every push to `main`, in a job that otherwise
# makes API calls only and checks out nothing.
#
# So this computes the paths **both sides changed since their merge base**,
# which is a SUPERSET of the conflicted ones: two edits to the same file often
# merge cleanly. It is a superset that answers the question a person actually
# has -- *which of my branches has to move, and roughly where* -- and every
# caller must say which of the two it is naming. The comment written by
# pr-branch-update.sh says so in the sentence above the list, in those words.
# Calling a superset "the conflicting files" would be the kind of overclaim this
# repository writes UNKNOWN instead of.
#
# Input is one object:
#
#   { pr:   <GET /repos/{o}/{r}/compare/{base}...{head}>,
#     base: <GET /repos/{o}/{r}/compare/{merge_base}...{base}> }
#
# `.pr.files` is what the branch changed relative to the merge base -- NOT
# relative to the current base, which is why the second compare starts at
# `.pr.merge_base_commit.sha` rather than at the base branch. Comparing against
# the current base would count every file `main` has moved since as a file the
# branch changed, and the intersection would be everything.
#
# Output is a markdown fragment for a pull-request comment, and it is one string
# so a caller can interpolate it without knowing how many lines it has.
# .github/tests/pr-branch-update-test.sh runs this file over both compare
# shapes, the empty intersection, and the truncation case.

# Twelve is compare-summary.jq's threshold for "past this many, the names stop
# being the answer"; this list is what somebody has to go and fix by hand, so it
# runs to twenty before it starts counting instead. Both are capped, unlike the
# 800-character truncation that cut mid-path on the word "c" on issue #26.
def cap($n):
  if (. | length) > $n
  then (.[0:$n] | map("- `" + . + "`") | join("\n"))
       + "\n- … and \(. | length - $n) more"
  else (map("- `" + . + "`") | join("\n"))
  end;

# The compare endpoint returns at most 300 files and does not flag that it
# truncated -- there is no field to read, so a full page is the only signal
# there is. Saying "at least" is the honest reading of it: 300 exactly could be
# a repository with 300 changed files and no truncation at all.
def maybe_truncated: (. | length) >= 300;

([.pr.files[]?.filename] | unique) as $branch
| ([.base.files[]?.filename] | unique) as $moved
| ($branch - ($branch - $moved)) as $both
| (if ($branch | maybe_truncated) or ($moved | maybe_truncated)
   then "\n\n(One of the two comparisons returned a full page of 300 files, which is the most the compare endpoint reports. The list above may be short.)"
   else "" end) as $note
| if ($both | length) == 0 then
    # A real and not-rare case: a rename against an edit, a delete against an
    # edit, or a conflict inside a file only one side's compare lists. The
    # branch still has to move; there is just nothing this method can name, and
    # inventing a plausible path would be worse than saying so.
    "The two sides changed no path in common, so this is most likely a rename or a delete against an edit. The conflicting paths cannot be named from the compare API; `git merge-base` and a local merge will show them."
  else
    "These paths were changed both on this branch and on the base since the merge base, so the conflict is among them (not necessarily all of them — two edits to one file often merge cleanly):\n\n"
    + ($both | cap(20))
    + $note
  end
