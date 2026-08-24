# Changes a cloud session wrote and could not push

Everything here is a `.patch` against `.github/workflows/`, and it is here for
one reason: **GitHub refuses to let a GitHub App create or update a workflow
file** unless the installation holds the `workflows` permission, and the cloud
agent's `claude[bot]` token does not.

```
! [remote rejected] claude/... -> claude/...
  (refusing to allow a GitHub App to create or update workflow
   `.github/workflows/agent-queue-watchdog.yml` without `workflows` permission)
```

The alternative to a directory like this is an agent that either abandons the
half of its task it can do, or reports success over a change that was never
pushed. Neither is better.

## Applying one

Three commands from a local session with the owner's own `gh` login, from the
repository root:

```
git apply docs/automation/pending/<name>.patch
git rm docs/automation/pending/<name>.patch
git commit -m "Land the workflow half of <what>"
```

**Delete the patch in the same commit that applies it.** A patch that outlives
its application is a second copy of a workflow file, and the second copy is the
one that goes stale — silently, because nothing lints it and nothing runs it.

**A patch carries every edit its own landing forces**, so that those three
commands produce a commit CI accepts. Two kinds are easy to miss: its row in
*Waiting now* below, which otherwise advertises a patch that no longer exists;
and any fingerprinted citation that the insertion pushes down the file, which
`tools/docs/check_docs.py` reports on the landing commit — naming the document
that holds the citation rather than the workflow that moved it. `git apply`
then `python3 tools/docs/check_docs.py .` before parking a patch says which.

**CI checks that it still applies**, on every push, in
`.github/tests/gh-api-usage-test.sh` (`ci.yml:360`) — `git apply --check` over
every `*.patch` in this directory. Until T-158 that sentence read *"check it
before trusting it"* and nothing did, which is the shape of every defect this
directory's own patches were written to fix. The same suite also reads the
shell a parked patch would **add** for the `gh api --slurp` with `--jq` pair
that `gh` rejects before making a request: three of those shipped in
`pr-merge-sweep.yml`, and the scan that catches them globbed
`.github/workflows/` only, so it could not see this directory at all.

What CI still cannot judge: a patch that applies cleanly onto a job that has
been rewritten underneath it. `--check` says the context matches, not that the
change still makes sense; and a patch written against a `main` that has since
moved may need `git apply -3`. Read a red apply check as *rebuild it against
the current tree* rather than as *hand-edit the hunk headers*.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

## Waiting now

| Patch | For | Written |
|---|---|---|
| `75-approval-stall.patch` | [#75](https://github.com/hleserg/Attadipa/issues/75) — the writer checkout's `token:`, the watchdog's `approvals` job, and the test's line in `ci.yml`. See [APPROVAL_STALLS.md](../APPROVAL_STALLS.md) | 2026-08-23 |

Verified before it was parked: `actionlint` clean over all seven workflows with
the patch applied, `shellcheck -x` clean, and the `approvals` job's body
dry-run against the live repository — the pagination, the jq, the marker
written and read back, and the rendered comment. What that does **not** prove
is the job running on a schedule under its own permissions, which no local run
can prove and which is therefore `NOT EXECUTED` until it is deployed.
