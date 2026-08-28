#!/usr/bin/env bash
# Does the unattended merge actually merge the commit the sweep inspected?
#
# WHY THIS EXISTS. `pr-merge-sweep.sh` gathers every fact about a pull request
# -- checks, labels, review threads, changed paths, the reviewer's verdict --
# against one head commit, `HEAD_OID`, and then merged with
#
#     gh pr merge "$PR" --repo "$REPO" --squash --delete-branch
#
# which names no commit. A push landing between the GraphQL round trip and that
# command merges the NEW head under a verdict earned by the old one. The window
# is the whole body of the loop, including up to twenty-five collaborator
# permission lookups, so it is not theoretical. Issue #256; the finding is
# https://github.com/hleserg/Attadipa/pull/255#discussion_r3854552742
#
# WHY IT DRIVES THE SCRIPT RATHER THAN A COPY. merge-candidate-test.sh already
# covers the three decision rules richly, but it reaches them through
# `sweep_verdict()`, which re-implements the caller's own extraction. A copied
# caller cannot show what the real caller passes to `gh`, and what it passes to
# `gh` is the entire finding here. So this test puts a recording stub on PATH
# and runs the shipping script end to end.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
SWEEP="$here/../scripts/pr-merge-sweep.sh"

pass=0; fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

# The head the sweep inspects, and a different one to stand for a push that
# lands while it is deciding. If the merge names the second, or names nothing,
# the pull request that gets merged is not the one that was reviewed.
INSPECTED=0123456789abcdef0123456789abcdef01234567
ARRIVED_LATER=fedcba9876543210fedcba9876543210fedcba98

# The same document shape merge-candidate-test.sh builds: every connection
# complete, checks green, `ai-review:pass` labelled after GitHub started work on
# this head, one documentation path. It merges.
jq -nc --arg oid "$INSPECTED" '{data:{repository:{pullRequest:{
  isDraft: false,
  mergeStateStatus: "CLEAN",
  labels: {totalCount:1, pageInfo:{hasNextPage:false}, nodes:[{name:"ai-review:pass"}]},
  reviewThreads: {totalCount:0, pageInfo:{hasNextPage:false}, nodes:[]},
  files: {totalCount:1, pageInfo:{hasNextPage:false}, nodes:[{path:"docs/research/status-example.md"}]},
  timelineItems: {totalCount:1, pageInfo:{hasNextPage:false, hasPreviousPage:false},
                  nodes:[{createdAt:"2026-08-24T00:00:00Z", label:{name:"ai-review:pass"}}]},
  commits: {totalCount:1, pageInfo:{hasNextPage:false}, nodes:[{commit:{
    oid: $oid,
    committedDate:"2020-01-01T00:00:00Z",
    checkSuites:{totalCount:1, pageInfo:{hasNextPage:false}, nodes:[
      {app:{slug:"github-actions"},
       workflowRun:{createdAt:"2026-08-23T00:00:00Z", event:"pull_request"}}]},
    statusCheckRollup:{contexts:{totalCount:1, pageInfo:{hasNextPage:false},
      nodes:[{__typename:"CheckRun", conclusion:"SUCCESS", status:"COMPLETED"}]}}}}]}
}}}}' > "$work/facts.json" || exit 1

# 2026-08-24T20:00:00Z: past the settling window for a head GitHub started work
# on at 2026-08-23T00:00:00Z. merge-head-trust.sh is the only reader.
export ATTADIPA_MERGE_NOW=1787616000

cat > "$work/gh" <<'STUB'
#!/usr/bin/env bash
# Records `gh pr merge`; answers everything else the sweep asks with the
# smallest well-formed document that lets it keep going.
case "$*" in
  "pr merge "*) printf '%s\n' "$*" >> "$ATTADIPA_MERGE_LOG"; exit 0 ;;
  *graphql*)    cat "$ATTADIPA_FACTS"; exit 0 ;;
  *"/pulls?state=open"*) printf '[{"number":7}]\n'; exit 0 ;;
  *comments*|*reviews*) printf '[]\n'; exit 0 ;;
esac
printf '{}\n'
STUB
chmod +x "$work/gh"
export PATH="$work:$PATH"
export ATTADIPA_FACTS="$work/facts.json"

# run_sweep SCRIPT -- prints the recorded `gh pr merge` argument line, or
# nothing if the sweep never got that far.
run_sweep() {
  : > "$work/merge.argv"
  ATTADIPA_MERGE_LOG="$work/merge.argv" REPO=o/r bash "$1" >/dev/null 2>&1
  cat "$work/merge.argv"
}

echo "The unattended merge names the commit it inspected"

MERGED_WITH="$(run_sweep "$SWEEP")"

# First: the fixture has to reach the merge at all. Without this, every
# assertion below passes vacuously on an empty string -- which is exactly how a
# gate test stops guarding anything.
case "$MERGED_WITH" in
  "pr merge 7 --repo o/r"*) ok "the eligible pull request reaches gh pr merge" ;;
  "") no "the sweep never reached gh pr merge; the rest of this test proves nothing"; ;;
  *) no "the sweep called gh pr merge unexpectedly: $MERGED_WITH" ;;
esac

case "$MERGED_WITH" in
  *"--match-head-commit "*) ok "the merge carries a head precondition" ;;
  *) no "the merge carries no --match-head-commit; a push mid-sweep merges unreviewed code" ;;
esac

case "$MERGED_WITH" in
  *"--match-head-commit $INSPECTED"*)
    ok "the precondition is the commit the facts were gathered for" ;;
  *)
    no "the precondition is not $INSPECTED: $MERGED_WITH" ;;
esac

case "$MERGED_WITH" in
  *"$ARRIVED_LATER"*) no "the merge names a commit the sweep never inspected" ;;
  *) ok "no other commit is named" ;;
esac

# THE MUTATION. An assertion that cannot fail is decoration. Strip the flag from
# a copy of the shipping script and the three checks above must stop holding --
# if they still hold, they are reading something other than what ships.
# The sweep resolves its four helper scripts and its GraphQL query against its
# own directory, so the mutated copy needs them beside it -- a copy that runs
# from somewhere else fails before the merge and would "pass" this check for the
# wrong reason.
mkdir -p "$work/mut" || exit 1
cp "$here/../scripts/"* "$work/mut/" || exit 1
# shellcheck disable=SC2016  # `$HEAD_OID` is the literal being removed, not a value.
sed -i 's/ --match-head-commit "\$HEAD_OID"//' "$work/mut/pr-merge-sweep.sh" || exit 1
if cmp -s "$SWEEP" "$work/mut/pr-merge-sweep.sh"; then
  no "the mutation changed nothing; this test is no longer reading the merge command"
else
  UNBOUND="$(run_sweep "$work/mut/pr-merge-sweep.sh")"
  case "$UNBOUND" in
    *--match-head-commit*) no "the mutation did not disarm the binding" ;;
    "") no "the mutated sweep never merged, so the mutation proved nothing" ;;
    *) ok "removing the flag is caught here rather than on main" ;;
  esac
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
