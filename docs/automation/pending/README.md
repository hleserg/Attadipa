# Changes a cloud session wrote and could not push

Everything here is a `.patch` against `.github/workflows/`, and it is here for
one reason: **GitHub refuses to let a GitHub App create or update a workflow
file** unless the installation holds the `workflows` permission, and the cloud
agent's `claude[bot]` token does not.

```
! [remote rejected] claude/... -> claude/...
  (refusing to allow a GitHub App to create or update workflow
   `.github/workflows/claude-agent.yml` without `workflows` permission)
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

**And delete its row from the table below, in that same commit.** Each row names
the documents carrying a "not landed yet" note for that patch; those notes become
false the moment it is applied, and prose that describes a fix as pending after
it has shipped is how a reader concludes the fix is not there. Nothing links
*to* a patch file from outside this directory, deliberately: a link into a file
the instructions above tell you to delete is a broken link in
`tools/docs/check_docs.py` on the very commit that lands the work, which turns
doing the right thing into a red build.

Check it before trusting it. `git apply --check` says whether it still applies;
a patch written against a `main` that has since moved may need `git apply -3`,
and one whose target job has been rewritten needs reading rather than applying.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

## Waiting now

| Patch | For | Notes to clear when applying | Written |
|---|---|---|---|
| `133-orchestration-bundle.patch` | [#133](https://github.com/hleserg/Attadipa/issues/133) — the default-branch bundle in `claude-agent.yml` and `claude-pr-review.yml`, plus the test's line in `ci.yml`. See [the security model](../CLAUDE_AUTOMATION.md#the-same-rule-at-the-other-end-of-the-run) | the block quote in [CLAUDE_AUTOMATION.md](../CLAUDE_AUTOMATION.md#the-same-rule-at-the-other-end-of-the-run); the "or it will, once" clause in [AI_TASK_PROTOCOL.md](../AI_TASK_PROTOCOL.md#what-the-pipeline-says-and-when); the "written, verified and NOT LANDED" sentence in `STATUS.md`; the owner-action bullet under T-100 in `TASKS.md`; and the header note in [`orchestration-bundle-test.sh`](../../../.github/tests/orchestration-bundle-test.sh) saying it is inert | 2026-08-23 |

Verified before it was parked: `git apply --check` clean against this branch,
`actionlint` clean over all seven workflows with the patch applied,
`shellcheck -x` clean over every script and test, and
[`orchestration-bundle-test.sh`](../../../.github/tests/orchestration-bundle-test.sh)
23/23 with the patch applied against 3/23 without it — the test extracts the
`Hand over` step's shell out of the YAML and executes it, so it is measuring the
patched workflow rather than describing it.

What that does **not** prove is a real run: the sparse default-branch checkout
into a second path, on a runner that already holds a full checkout, has never
been observed in production. `NOT EXECUTED` until this patch is applied and an
agent run starts from a review comment on a pull request.

**This directory did not exist on `main` when this patch was written.** Two
other open branches create it as well — [#128](https://github.com/hleserg/Attadipa/pull/128)
and [#154](https://github.com/hleserg/Attadipa/pull/154) — so whichever of the
three lands first creates this file and the other two conflict on one table row.
Take the existing file and keep the rows; the prose above is the same text in
all three because it is the same convention, not three descriptions of it.
