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

Check it before trusting it. `git apply --check` says whether it still applies;
a patch written against a `main` that has since moved may need `git apply -3`,
and one whose target job has been rewritten needs reading rather than applying.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

## Waiting now

| Patch | For | Written |
|---|---|---|
| `75-approval-stall.patch` | [#75](https://github.com/hleserg/Attadipa/issues/75) — the writer checkout's `token:`, the watchdog's `approvals` job, and the test's line in `ci.yml`. See [APPROVAL_STALLS.md](../APPROVAL_STALLS.md) | 2026-08-23 |
| `170-merge-sweep-completeness.patch` | [#170](https://github.com/hleserg/Attadipa/issues/170) — the caller half of the completeness rule, all in `pr-merge-sweep.yml`. **While this waits, the half-hourly merge sweep merges nothing at all**: the rule refuses the nine-argument caller by arity and holds every pull request, once per sweep, naming this file. See [CLAUDE_AUTOMATION.md](../CLAUDE_AUTOMATION.md) and T-144 | 2026-08-24 |

Verified before it was parked: `actionlint` clean over all seven workflows with
the patch applied, `shellcheck -x` clean, and the `approvals` job's body
dry-run against the live repository — the pagination, the jq, the marker
written and read back, and the rendered comment. What that does **not** prove
is the job running on a schedule under its own permissions, which no local run
can prove and which is therefore `NOT EXECUTED` until it is deployed.

For `170-merge-sweep-completeness.patch`: the rule, its filter, its query and
its 133 assertions are already on `main` and run in CI on every push — only the
four edits that make the sweep *call* them are in the patch. `git apply --check`
is asserted by `merge-candidate-test.sh` itself, so a patch that stops applying
turns CI red rather than rotting quietly. What that does **not** prove is the
sweep running on its schedule with the patch applied, which is `NOT EXECUTED`
until it lands.

**Two patches now edit `pr-merge-sweep.yml`.** Apply them in one commit, not in
either order — see T-144. Each `git rm`s only its own file; neither removes this
directory, which three links in `APPROVAL_STALLS.md` point at.
