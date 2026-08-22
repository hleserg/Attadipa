# What changed between the commit a producer reviewed and the branch as it is
# now, said in a way somebody can act on.
#
# Input is a GitHub compare response. Output is one line for the staleness
# sentence in the intake gate's receipt.
#
# THIS LIVES IN A FILE BECAUSE THE PROJECT ALREADY LEARNED THIS ONCE. Logic
# inside a YAML `run:` block cannot be executed by a test, so the fixtures it
# was "checked against" exist only in somebody's terminal history. The same
# reasoning put the gate in intake-decision.sh and the watchdog filter in
# queue-scan.jq — and a scoping bug in the latter was caught by running it over
# a fixture rather than by reading it. The first version of this summary was
# inline YAML, and the review on #70 pointed out that its own description cited
# that precedent while not following it.
#
# THE SHAPE, and why it is not just a list. Past a dozen files the useful
# summary is HOW MANY and WHERE: "400 files, across core/ and docs/" and "400
# files, all under .github/" are different warnings to an agent about to
# re-verify a finding, and the 800-character truncation this replaces
# distinguished neither — it cut mid-path, on the word "c", on issue #26.
#
# Twelve or fewer are named, because at that size the names are the answer.

# Both branches are capped. The previous code capped only the list it built;
# the first version of this file capped only the long branch, which the same
# review called out as an inconsistency within one diff. Same data, same guard.
def cap($n): if (. | length) > $n then .[0:$n] + " …" else . end;

[.files[]?.filename] as $files
| ($files | length) as $n
| if $n == 0 then
    # A compare response with no files array at all. Not an error: an empty
    # range reports this, and so does a response the API truncated.
    "no changed files were reported"
  elif $n <= 12 then
    "Files changed since: " + (($files | join(", ")) | cap(400))
  else
    # Top-level directory per path, deduplicated. A file at the repository root
    # keeps its own name, which is what keeps CLAUDE.md and STATUS.md visible
    # rather than folded away into nothing.
    ($files
     | map(split("/") | if length > 1 then .[0] + "/" else .[0] end)
     | unique | join(", ")) as $where
    | "\($n) files changed since, across: " + ($where | cap(400))
  end
