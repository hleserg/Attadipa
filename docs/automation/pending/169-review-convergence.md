# Pending: the convergence rule's workflow half

**Status: written, tested, and NOT DEPLOYED.** The rule in
[`.github/scripts/review-verdict.sh`](../../../.github/scripts/review-verdict.sh)
is on `main` and CI runs its test suite. Nothing calls it yet. Until the patch
beside this file is applied, `claude-pr-review.yml` behaves exactly as it did
before: the reviewer sets its own label, every round is a first round, and a
branch can stay `ai-review:blocking` indefinitely.

Do not read a green CI run as evidence that the convergence rule is in force.

## Why it is a patch and not a workflow file

A GitHub App cannot create or update anything under `.github/workflows/` unless
its installation holds the `workflows` permission, and the cloud agent's
`claude[bot]` token does not. The push is rejected outright, not warned about —
probed on this branch on 2026-08-24:

```
! [remote rejected] tmp/workflow-push-probe -> tmp/workflow-push-probe
  (refusing to allow a GitHub App to create or update workflow
   `.github/workflows/codeql.yml` without `workflows` permission)
```

This is the same wall [#119](https://github.com/hleserg/Attadipa/pull/119) hit,
and the reason every workflow change in #85, #96 and #113 was committed from a
local session rather than from a cloud one.

Whether to grant the App `Workflows: Read and write` permanently is an owner
decision with a real argument on each side, and it is not taken here.

## Landing it, from a local session

```bash
git fetch origin && git checkout main && git pull
git apply docs/automation/pending/169-review-convergence.patch
git rm docs/automation/pending/169-review-convergence.md \
       docs/automation/pending/169-review-convergence.patch
git commit -am "Apply the workflow half of the convergence rule from a local session"
```

Push it on a branch and let CI run: the patch touches `ci.yml`, so a broken
apply shows up as a failed parse rather than as a silently absent step.

Two follow-ups belong in that same commit, because leaving them makes the
repository claim something that is not true:

1. `STATUS.md` — the automation section says the rule is written and not
   deployed. Change it to say it is running, and give the first pull request it
   ran on.
2. `docs/automation/CI_AND_REVIEW_PIPELINE.md` § *The convergence rule* — same
   sentence, same reason.

## What the patch does

| File | Change |
|---|---|
| `.github/workflows/claude-pr-review.yml` | the reviewer is told to read the ledger, re-use finding ids, and end its comment with a findings block; five steps after the review read the ledger, run the rule, file the deferred findings, rewrite the ledger comment and set the label from the rule rather than from the reviewer |
| `.github/workflows/ci.yml` | one step, running `.github/tests/review-verdict-test.sh` |

No new permission. The job already holds `pull-requests: write` and
`issues: write`, and the new steps use the built-in `GITHUB_TOKEN`, whose events
GitHub deliberately does not use to start workflow runs — so the follow-up issue
this can file cannot start a billable writer even if somebody later labels the
wrong thing.

## The one thing to check on the first real run

**Does the reviewer actually emit the findings block?** The rule degrades safely
if it does not — `label=unknown`, the reviewer's own label stands, and the ledger
comment says so in as many words — but that degradation is the old behaviour, so
a reviewer that never emits a block means the rule never fires and nothing looks
wrong. The ledger comment on any pull request says which of the two happened.
