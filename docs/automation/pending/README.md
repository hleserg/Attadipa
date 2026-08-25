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

**"Carries" means a hunk OR a named instruction in the header, and for this
table it has to be the instruction.** A hunk that edits *Waiting now* deletes or
rewrites rows it does not own, so it breaks the moment anybody parks another
patch — `git apply` tolerates an offset and never an insertion inside its
preimage, and it is all-or-nothing, so one stale row takes that patch's workflow
edits down with it. That is not hypothetical: `75-approval-stall.patch` carried
such a hunk, written when it was the only patch here, and #177 adding a row
broke all six of its diffs at once. Its header names the edit instead now.
`merge-candidate-test.sh` runs `git apply --check` over **every** `*.patch` in
this directory on every push, so the next one to rot says so in CI rather than
on the day somebody tries to land it.

Check it before trusting it. `git apply --check` says whether it still applies;
a patch written against a `main` that has since moved may need `git apply -3`,
and one whose target job has been rewritten needs reading rather than applying.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

## Waiting now

| Patch | For | Written |
|---|---|---|
| `75-approval-stall.patch` | [#75](https://github.com/hleserg/Attadipa/issues/75) — the writer checkout's `token:`, the watchdog's `approvals` job, and the test's line in `ci.yml`. See [APPROVAL_STALLS.md](../APPROVAL_STALLS.md) | 2026-08-23 |
| `170-merge-sweep-completeness.patch` | [#170](https://github.com/hleserg/Attadipa/issues/170), [#199](https://github.com/hleserg/Attadipa/issues/199) **and** [#130](https://github.com/hleserg/Attadipa/issues/130) — the caller half of the completeness rule, of the head-trust rule and of the Codex answer rule, all in `pr-merge-sweep.yml`. **While this waits, the half-hourly merge sweep merges nothing at all**: the rule refuses the nine-argument caller by arity and holds every pull request, once per sweep, naming this file. See [CLAUDE_AUTOMATION.md](../CLAUDE_AUTOMATION.md) and T-144 | 2026-08-24, extended 2026-08-25 |
| `240-review-invalidation-order.patch` | [#240](https://github.com/hleserg/Attadipa/issues/240) — the caller half, and `claude-pr-review.yml` alone: the two steps that invalidate a stale verdict do it **before** the fallible notification. **While this waits, a silent review whose `gh pr comment` fails still leaves the previous head's `ai-review:pass` on the pull request** — the rule is on `main` and nothing calls it. Unusually, the fix here is not unexecuted while it waits: `review-invalidate-workflow-test.sh` applies this patch to a scratch copy and runs the two steps against a stub `gh` on every push. See [CI_AND_REVIEW_PIPELINE.md](../CI_AND_REVIEW_PIPELINE.md) and T-168 | 2026-08-25 |

Verified before it was parked: `actionlint` clean over all seven workflows with
the patch applied, `shellcheck -x` clean, and the `approvals` job's body
dry-run against the live repository — the pagination, the jq, the marker
written and read back, and the rendered comment. What that does **not** prove
is the job running on a schedule under its own permissions, which no local run
can prove and which is therefore `NOT EXECUTED` until it is deployed.

For `170-merge-sweep-completeness.patch`: the rules, their filters, their query
and their 184 assertions are already on `main` and run in CI on every push — only
the eleven edits that make the sweep *call* them are in the patch. `git apply
--check` is asserted by `merge-candidate-test.sh` itself, so a patch that stops
applying turns CI red rather than rotting quietly, and the suite reports the same
184 either way, in the parked state and in the applied one.
`codex-answered-test.sh` keys on the same two states and reports 104 parked
against 103 applied, because the parked state has one more thing to assert —
that the live caller is still the shape the patch replaces. The GraphQL document
it points the sweep at was run read-only against this repository's #173, #176,
#180, #188 and #193 on 2026-08-24, and both rules answered over the real
replies. What none of that proves is the sweep running on its schedule with the
patch applied, which is `NOT EXECUTED` until it lands — T-126.

**Three issues, one patch, and that is the decision rather than an accident.**
#199 landed its rule while #170's caller edits were still parked here, #130's
reopened half arrived while both were, and all three fixes edit
`pr-merge-sweep.yml`. Separate patches would have meant three apply orders
against one file and a `merge-candidate.sh` arity moving twice — nine to ten to
eleven, with a middle state somebody could land and CI could not recognise.
Folding them keeps one transition: nine arguments today, eleven the moment this
lands.

**And #130's three edits are what make the other eight safe to apply.** The
patch as it stood before them deleted `COMMITTED` while two calls further down
still read `"$COMMITTED"`, under `set -u` — so applying it alone aborted the
step on the first pull request whose comments it read. Found by applying the
patch and running what came out, which is the argument for `git apply` before
parking rather than after.

**One patch in this directory edits `pr-merge-sweep.yml`** — this one, and
nothing collides with it. `75-approval-stall.patch` beside it edits
`agent-queue-watchdog.yml`, `ci.yml`, `claude-agent.yml` and two documents, and
never the sweep, so the two are independent and may land in either order or
apart. `240-review-invalidation-order.patch` carries
`claude-pr-review.yml` alone and is independent too. The one that used to
collide was `130-merge-sweep-caller.patch` on
[#154](https://github.com/hleserg/Attadipa/pull/154); that pull request was
closed unmerged in the recovery, and #130's caller edits are inside this patch
now rather than beside it — T-144. Every patch here `git rm`s only its own file;
none removes this directory, which three links in `APPROVAL_STALLS.md` point at.

**No two patches here may carry the same `.github/workflows/` file, and
`gh-api-usage-test.sh` fails the suite when they do.** Landing one leaves the
other's copy of that workflow stale, and a stale `claude-*.yml` is not a merge
conflict somebody notices — it is a review that silently does not run, on `main`
and on every open pull request, for the reason `CI_AND_REVIEW_PIPELINE.md` gives
about byte-identical workflow files. Two patches needing one file means folding
them, as #170 and #199 did above. **A document is not covered by this** — two
patches may edit one `.md`, because a stale prose hunk fails loudly under `git
apply` instead of quietly at runtime.

The rule bites hardest on `ci.yml`, because a new test wants a line in it and
`75-approval-stall.patch` holds it. `240-review-invalidation-order.patch` is
what that looks like solved without folding two issues together: rather than
take a `ci.yml` hunk, its test runs from inside an existing test file, and
rather than sit dormant, it applies **this directory's own patch** to a scratch
copy of the workflow and asserts against the result. So the fix is executed
against a stub `gh` on every push while it waits, and the day it lands the same
assertions read the real file with no edit. That is worth copying: a parked
patch nothing runs is a fix whose first real execution is in production.
