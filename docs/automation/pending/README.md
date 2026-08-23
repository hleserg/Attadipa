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

Check it before trusting it. `git apply --check` says whether it still applies;
a patch written against a `main` that has since moved may need `git apply -3`,
and one whose target job has been rewritten needs reading rather than applying.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

## Waiting now

| Patch | For | Written |
|---|---|---|
| `133-orchestration-bundle.patch` | [#133](https://github.com/hleserg/Attadipa/issues/133) — the default-branch bundle in `claude-agent.yml` and `claude-pr-review.yml`, plus the test's line in `ci.yml`. See [the security model](../CLAUDE_AUTOMATION.md#the-same-rule-at-the-other-end-of-the-run) | 2026-08-23 |

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
