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
`.github/tests/gh-api-usage-test.sh`
([`.github/workflows/ci.yml:360`](../../.github/workflows/ci.yml) "bash .github/tests/gh-api-usage-test.sh") — `git apply --check` over
every `*.patch` in this directory. Until T-158 that sentence read *"check it
before trusting it"* and nothing did, which is the shape of every defect this
directory's own patches were written to fix.

**It is loud in two different ways, and which one depends on the half of the
patch that moved.** A patch has two, and they rot for different reasons.

| what moved | CI | why |
|---|---|---|
| a hunk under `.github/` | **fails** | nobody but the owner or another landing patch can move that context, and a stale workflow hunk means the parked change itself is now wrong |
| anything else the patch carries | **warns** — a job-summary line and a `::warning file=` annotation, naming the file that moved | those pins move under ordinary work, by people who did not choose to and cannot rebuild a workflow patch either |

The second row is not caution, it is arithmetic. `check_docs.py` enforces the
`ci.yml` citation fingerprints, so inserting a line into `ci.yml` *forces* a
citation edit that moves this patch's own pinned context. Fail on that and the
red lands on `main` and every open pull request over a file none of them
touched, and one stale patch stops the whole queue.

**Either way there are exactly two answers, and doing nothing is not one.** If
the patch has already been landed, it is a leftover copy: `git rm` it, which is
the same rule as *delete the patch in the same commit that applies it*, one
commit late. If it has not been landed, rebuild it against the current tree —
never hand-edit the hunk headers, and note that a patch written against a
`main` that has since moved may want `git apply -3` when it does land.

The same suite also reads the shell a parked patch would deploy — its
**post-image**, context plus added lines — for the `gh api --slurp` with `--jq`
pair that `gh` rejects before making a request: three of those shipped in
`pr-merge-sweep.yml`, and the scan that catches them globbed
`.github/workflows/` only, so it could not see this directory at all. It reads
the post-image and not the added lines alone because every `--slurp` call in
this repository puts `gh api` on one line and its flags on the next; a patch
touching only the flag line has no `gh api` among its added lines. Removing a
bad call is still not an offence — take `--jq` out and the post-image no longer
holds one — and only hunks targeting `.github/` are read, so a patch that
*documents* the rule in prose is left alone.

**What is parked here must be one flat directory of `*.patch` files**, and CI
now refuses anything else rather than skipping it. A patch in a subdirectory, a
file saved as `.diff`, a directory that has been renamed away, a file with no
diff target in it at all — each of those used to leave *both* halves of the
guard reading nothing while each printed its strongest green line. Group a
multi-file change into one patch, not into a folder. And a **missing**
directory is a failure where an **empty** one is a pass: empty is a state
somebody chose, missing is one nobody noticed.

**A patch `git` cannot parse is reported separately, and its remedy is never
`git rm`.** Rebuild it. Deleting a patch nobody can read is deleting work
nobody has read.

What CI still cannot judge: a patch that applies cleanly onto a job that has
been rewritten underneath it. `--check` says the context matches, not that the
change still makes sense.

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
