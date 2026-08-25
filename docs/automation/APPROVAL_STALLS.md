# A check that waits for an approval nobody is watching for

[← CI, and the review that follows it](CI_AND_REVIEW_PIPELINE.md) ·
[automation index](CLAUDE_AUTOMATION.md)

An agent pushes a fix to its own pull request. The runs are created and
immediately finish, having done nothing. No check appears. Nothing is red, so
nothing is investigated, and the agent's last comment — *"waiting on CI"* — is
true and stays true forever.

This page is the rule that causes it, why the four settings you would reach for
first are all the wrong ones, what it costs to fix, and what the pipeline does
in the meantime. Opened from [#75](https://github.com/hleserg/Attadipa/issues/75).

## What was actually observed

Attempt 1 of both runs for head `488be1e` on
[#71](https://github.com/hleserg/Attadipa/pull/71), read back from the REST API
on 2026-08-23 rather than taken from the issue:

| | CI `32581052659` | review `32581052664` |
|---|---|---|
| `status` | `completed` | `completed` |
| `conclusion` | **`action_required`** | **`action_required`** |
| jobs (`total_count`) | **0** | **0** |
| `created_at` = `run_started_at` = `updated_at` | `2026-08-22T15:13:36Z` | same second |
| `actor` / `triggering_actor` | `github-actions[bot]` | `github-actions[bot]` |
| `head_repository` vs `repository` | both `hleserg/Attadipa` | both `hleserg/Attadipa` |

Two of those rows decide everything that follows.

**`head_repository` equals `repository`.** This was a branch in this
repository, not a fork.

**The commit's author is not the push's identity.** `git log 488be1e` gives
author *and* committer as `claude[bot]` — the action sets the git identity — while
the run's actor is `github-actions[bot]`, which is the identity of the
credential the push was *authenticated* with. Those two disagreeing is the
fingerprint of this failure, and reading only the first is how it hides.

## The rule

> When a pull request is created or updated by a workflow using `GITHUB_TOKEN`,
> `pull_request` events with the `opened`, `synchronize`, or `reopened` activity
> types create workflow runs that require approval. A user with write access to
> the repository can approve these runs from the pull request page.

— GitHub Docs, [Events that trigger workflows §
`pull_request`](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request),
read **2026-08-23**.

It is the `pull_request`-shaped corner of the wider anti-recursion rule:

> When you use the repository's `GITHUB_TOKEN` to perform tasks, events
> triggered by the `GITHUB_TOKEN` will not create a new workflow run […]
> `workflow_dispatch` and `repository_dispatch` events always create workflow
> runs.

— GitHub Docs,
[`GITHUB_TOKEN`](https://docs.github.com/en/actions/concepts/security/github_token),
read **2026-08-23**. The same page gives the remedy: *"use a GitHub App
installation access token or a personal access token instead of
`GITHUB_TOKEN`"*.

So the two behaviours are siblings, and the difference matters when reading a
run list. For most events a `GITHUB_TOKEN` push produces **no run at all**. For
`opened`/`synchronize`/`reopened` on a pull request it produces **a run record
that never starts** — which is worse, because a run record looks like something
happened.

> [!NOTE]
> `STATUS.md` already says *"GitHub raises no workflow run from a
> `GITHUB_TOKEN` event"* about undrafting. That statement is still correct:
> `ready_for_review` is not one of the three activity types above, so the
> general rule applies to it and no run is created. This page narrows the claim
> rather than contradicting it.

## Why it is none of the four settings

[#75](https://github.com/hleserg/Attadipa/issues/75) listed four candidates and
said not to guess between them. The answer is that **none of them applies**, and
changing any one of them would fix nothing while removing a real protection from
a public repository.

| Candidate | Verdict |
|---|---|
| *Require approval for first-time contributors who are new to GitHub* | fork pull requests only |
| *Require approval for first-time contributors* | fork pull requests only |
| *Require approval for all external contributors* | fork pull requests only |
| the pull request's author being an app (`claude[bot]`) | contradicted by the evidence |

The three settings live together under **Settings → Actions → General →
Approval for running fork pull request workflows from contributors**
([reference](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/enabling-features-for-your-repository/managing-github-actions-settings-for-a-repository),
read 2026-08-23), and the page that documents the approval they produce opens
with *"Workflow runs triggered by a contributor's pull request from a fork may
require manual approval"*
([reference](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/approve-runs-from-forks),
read 2026-08-23). `head_repository == repository`, so no fork was involved and
no fork policy could have fired.

The fourth candidate dies on the issue's own evidence table: head `31c2c39` was
pushed by `claude[bot]` and ran normally. The pull request's author was
`claude[bot]` for all three heads. An input that is identical across a stall and
two non-stalls is not the cause.

**There is no repository setting that disables this.** GitHub documents the
behaviour unconditionally and offers a different credential as the remedy, not a
checkbox. If a future reader finds one, this table is the thing to correct.

## Where this repository actually produces it

`.github/workflows/claude-agent.yml`, the writer job:

```yaml
      - uses: actions/checkout@v7
        if: steps.auth.outputs.ok == 'true'
        with:
          fetch-depth: 0
```

Both relevant inputs are left at their defaults, and
[`actions/checkout`](https://github.com/actions/checkout) defaults `token` to
`${{ github.token }}` and `persist-credentials` to `true` — *"Whether to
configure the token or SSH key with the local git config"* (read 2026-08-23).
The remote is therefore left authenticated as the built-in `GITHUB_TOKEN`, and
**any `git push` the agent runs in a shell of its own goes out as
`github-actions[bot]`.**

The other two checkouts in the same file (`:114` and `:336`) both set
`persist-credentials: false`. The writer's does not, and the writer is the only
one that pushes.

That closes the issue's evidence table exactly:

| head | pushed by | outcome |
|---|---|---|
| `31c2c39` | `claude-code-action`, over the Claude App installation token | ran |
| `c9e00d0` | the owner | ran |
| `488be1e` | **the agent's own `git push`, over the checkout's persisted `GITHUB_TOKEN`** | `action_required`, 0 jobs |

The workflow already knows the rule — `claude-agent.yml:810-819` "Deliberately NOT secrets.GITHUB_TOKEN" explains why
`claude-code-action` is deliberately *not* handed `secrets.GITHUB_TOKEN`. The
action's push path was fixed. The agent's own push path was never covered by it.

## The options, and what each costs

**A — a fine-grained PAT in `ATTADIPA_AGENT_TOKEN`, plus one line in the
writer's checkout.** GitHub's own documented remedy. `ATTADIPA_AGENT_TOKEN`
already exists as a documented, deliberately-optional secret, and
`claude-code-action` already reads it; the missing half is that the *checkout*
does not, so setting the secret alone does not fix this. Cost: a long-lived
credential on a public repository, and every agent commit becomes attributable
to the account that owns the PAT rather than to `claude[bot]` — the audit line
between "the owner did this" and "an agent did this" goes away. **Scope it
without `Workflows: write`**: granting that would also retire
[`pending/`](pending/README.md), which is convenient and is exactly the widening
that lets an agent rewrite the gate that governs it.

**B — mint a GitHub App installation token in the workflow.** Same remedy, a
credential that expires in an hour, and commits stay attributed to an app. Cost:
two more secrets (App ID and private key) and one more action in the supply
chain. Whether the *Claude* App's installation token can be obtained outside
`claude-code-action` — which mints one over OIDC, see `claude-agent.yml:508-517` "id-token: write is not optional"
— is **UNKNOWN**; a separate App would have to be created if not.

**C — `persist-credentials: false` on the writer's checkout.** Free, no
credential, no owner. It does not fix the stall; it converts it from silence
into a loud push failure, which is strictly better than today. Cost: the agent
loses `git push` entirely and falls back on whatever `claude-code-action`
commits for it. Whether that covers everything the agent does is **UNKNOWN**,
and getting it wrong breaks the fix-your-own-pull-request path outright.

**D — detect it and say so.** What is implemented now; see below. Costs one
watchdog job and nothing from Anthropic. It fixes no stall — it ends the
silence, which is the part that made this expensive.

**E — re-run the stalled run from the watchdog.** `POST
/repos/{owner}/{repo}/actions/runs/{run_id}/rerun` with `actions: write`.
**Not implemented and not recommended yet.** Whether a re-run dispatched by
`GITHUB_TOKEN` clears an approval requirement caused by `GITHUB_TOKEN` is
**UNKNOWN**, and a re-run loop that does not clear it is an hourly bill for the
same answer. Settle that one by observation on the next real stall before
writing any code.

**Two things are no longer unknown, and both were established on
[#173](https://github.com/hleserg/Attadipa/pull/173) on 2026-08-24 rather than
read off documentation.**

- `POST /repos/{owner}/{repo}/actions/runs/{run_id}/approve` **answered `403
  Resource not accessible by integration`** for `claude[bot]` under this
  repository's app installation. The endpoint is documented for *"a pull
  request from a public fork of a first time contributor"*, which is not this
  case — but the reason it is not a way out here is the 403, which is an
  observation, not the documentation, which is an inference. An agent cannot
  approve its own stalled run, and no permission an agent can grant itself
  changes that.
- **Closing and reopening the pull request re-queued all three runs on the same
  head**, `gh pr close` then `gh pr reopen`. That is a second observed remedy
  beside a maintainer's re-run, and unlike the re-run it is inside what an
  agent with *Pull requests: write* can already do. It is **not** the same
  event as a re-run — reopening raises `pull_request.reopened`, so a workflow
  that does not listen for it will not start — and it costs a full CI cycle on
  every workflow watching the pull request. Recorded because it works, not
  recommended as a loop: an unattended job that closes and reopens pull
  requests to clear its own stalls is a worse failure mode than the silence it
  replaces.

Both are in the guard's comment text, so the pull request that carries a stall
now names the remedy that has been seen to work rather than only the one a
maintainer has to be present for.

### What is recommended, as one action

**Option B — a second GitHub App — unless the owner has already made the PAT.**
This section recommended **A** flatly until the fifth review round of
[#128](https://github.com/hleserg/Attadipa/pull/128), which pointed out that it
contradicted *What this does not cover* below in the same file: that section
established a **fourth** cost of A — the anti-recursion guard becoming
unreachable — and said in as many words *"Option B keeps the boundary intact …
now a reason to prefer it beyond attribution"*. A document cannot both establish
that and recommend past it. **The fourth cost is not a trade-off; it is a
condition**, and the two consequences it opens are reachable today rather than
hypothetically: a billable writer dispatched hourly on our own blocker issue,
and the one unattended merge rule proceeding over findings nobody answered.

**A remains available and is not wrong — it is A-with-T-145-first.** If the PAT
already exists the cheaper path is to keep it and land **T-145** (P1) before
the secret is used, rather than to build an App for the sake of the ordering.
What is not available is A with the guard left as it is.

**If A is taken, the PAT is scoped to this repository only with `Workflows`
left unchecked** — *Contents: Read and write*, *Pull requests: Read and write*
**and *Issues: Read and write***, set as the repository secret
`ATTADIPA_AGENT_TOKEN`.

**The third permission is not optional and an earlier version of this section
omitted it.** `ATTADIPA_AGENT_TOKEN` is not a checkout credential: it is the
`github_token:` for `claude-code-action` — one citation per line, because the
checker reads a fingerprint only from the citation's own physical line:
`claude-agent.yml:819` "github_token: ${{ secrets.ATTADIPA_AGENT_TOKEN }}",
`claude-pr-review.yml:116` "github_token: ${{ secrets.ATTADIPA_AGENT_TOKEN }}",
`claude-ci-repair.yml:289` "github_token: ${{ secrets.ATTADIPA_AGENT_TOKEN }}".
`display_report: "true"` posts the agent's summary **on the triggering issue**
over it, and the
agent's own `gh issue comment` and `gh issue edit --add-label` use it too —
`claude-agent.yml:899` "depends entirely on `gh` being allowed" says the
action has no label feature of its own. That citation read `:846` until
2026-08-24 — 38 lines short, landing on `# .github/tests/bot-actor-test.sh
asserts this stays in`, an unrelated comment — so a reader checking the evidence
for the blocking claim found nothing. Found in review, and it is the failure
this document's own §*Citations* warns about at
[`APPROVAL_STALLS.md#citations-in-this-file`](APPROVAL_STALLS.md): a bare line
number rots silently. All eight code citations in this file now carry a
fingerprint **on the citation's own physical line**, which is the only place the
checker looks:
`tools/docs/check_docs.py:498` "FINGERPRINT.match(line[match.end()" reads it out
of the remainder of that same line, so a citation whose quoted snippet wraps
onto the next line is checked for nothing but non-blankness and drifts as
silently as a bare number. Three here did
until 2026-08-24 — two found in review, the third found by counting the file's
citations with the checker's own `CITATION` and `FINGERPRINT` patterns rather
than by eye, which is also how the count in this sentence stopped being six. All
three were reflowed rather than reworded; the third also had to drop a `"` from
its snippet, because `FINGERPRINT`'s `"([^"]{3,80})"` cannot match a quote.
The next one to drift is now reported with the line the text moved to rather
than passing because the line it landed on happened to be non-blank.
[CLAUDE_AUTOMATION.md](CLAUDE_AUTOMATION.md) already records the working scope
for this same secret as all three. Set only the first two and the next
issue-driven run — the canonical path per `CLAUDE.md` — takes a 403 on the issue
write: no report, no `agent:review`, so `queue-scan.jq` still sees the issue
waiting, the watchdog re-queues it hourly, and a billable writer repeats the
same task with every run reporting **green**. Found in review.

It is the documented remedy, the secret it fills already exists and is already
read by two workflows, and it needs no new App, no new action and no setting
that protects the repository from anyone else. The one-line checkout change that
makes the secret *sufficient* is in
[`pending/75-approval-stall.patch`](pending/README.md) — written as
`${{ secrets.ATTADIPA_AGENT_TOKEN || github.token }}`, so applying it while the
secret is unset changes nothing at all and the two can land in either order.

**Four costs are being accepted, and naming one of them was not enough.** This
list said **three** until the fifth review round of
[#128](https://github.com/hleserg/Attadipa/pull/128) — the fourth was written
out at length in *What this does not cover* below and never counted here, which
is how a reader could finish the priced list and still not have met the one that
changes the recommendation.

1. **Attribution.** Agent commits will carry the PAT owner's name. If that is
   the wrong trade, **B** is the same fix without it, at the price of two more
   secrets.
2. **A long-lived credential in `.git/config`, in the job that holds `Bash`.**
   `actions/checkout` defaults `persist-credentials` to `true` and writes the
   token in as an `http.extraheader` for the life of the workspace. The agent
   pushes with `git push` from the model's own shell, so that persistence is
   what makes the push work — turning it off breaks the fix. The built-in
   `GITHUB_TOKEN` it replaces expires with the job; a PAT does not, so the
   exposure outlives the run that leaked it. It is bounded by the job, not by
   the token, and removing it needs a push path that does not persist a
   credential — **T-146**. This is not the generic *"a long-lived credential on
   a public repository"* the section above prices; it is a specific file, in a
   specific job, readable by a specific tool. Found in review.
3. **`claude-ci-repair.yml` becomes reachable.** Today the agent's pushes create
   runs that stall at `action_required` with no jobs, so nothing fails and the
   repair workflow never fires. The moment those pushes run CI, a red one calls
   a second billable writer: two attempts, `timeout-minutes: 60`, against the
   per-run figure `STATUS.md` records. The `approvals` job added by the patch
   costs nothing from Anthropic; this consequence of the same patch does not,
   and the risk list said only the first. Found in review.
4. **The anti-recursion guard becomes unreachable.** Written out in full under
   *What this does not cover* below and absent from this list until the fifth
   review round. It is the one that is **not a trade-off**: a fine-grained PAT
   belongs to a user, so everything the agent writes carries
   `author_association: OWNER`, and `queue-scan.jq` accepts on
   `author_association` before the login test is evaluated. It is why this
   section now recommends **B** unless the PAT already exists, and why taking
   **A** means landing **T-145** first rather than afterwards. It cannot be
   priced in money — it removes a bound, and the two consequences it opens are
   reachable today.

## What happens in the meantime — written, tested, **not deployed**

> [!IMPORTANT]
> The rule below is on `main` and has 51 cases — **but nothing on `main` runs
> them.** `ci.yml` enumerates every shell test by name and this one is absent,
> because the line that would add it rides the same blocked patch: `ci.yml` is
> itself under `.github/workflows/`. `shellcheck` globs the file, so it is
> linted and never executed. Tested **by hand, not by CI, until the patch
> lands** — which in a document about checks that did not run is the one thing
> that must not be glossed. The watchdog job that calls it is
> **not**, and until somebody applies
> [`pending/75-approval-stall.patch`](pending/README.md) this stall is still
> silent.
> GitHub refuses to let a GitHub App update a file under `.github/workflows/`
> without the `workflows` permission, which the agent's `claude[bot]` token does
> not have — the same wall [#74](https://github.com/hleserg/Attadipa/issues/74)
> hit. That patch also carries the `token:` line that is the actual fix, so
> applying it is one action and it does two things.

The hourly watchdog's `approvals` job reads every open pull request's newest run
per workflow, and `.github/scripts/approval-stall-decision.sh` decides what that
means. A run that concluded `action_required` with **zero jobs** put no check on
the pull request at all — that is this stall, and it gets a comment saying so
with the run's own numbers in it. A run that concluded `action_required` *with*
jobs is an environment or deployment gate: also waiting on a person, but visible
on the pull request, so it gets different words rather than the same ones.

The bound is **one comment per head commit**, carried in an HTML marker
(`<!-- attadipa-approval-stall <sha> -->`) for the same reason the reviewer's
"did not run" note carries one: matching on the marker alone posts once per pull
request *ever*, so a stale note from an old commit sits there while every push
after it stalls in silence.

An unreadable input — a job count that is not a number, a head that is not a SHA
— is never resolved in either direction. It is skipped for that tick and looked
at again in an hour. Guessing "stalled" accuses every pull request the moment
the API has a bad minute, hourly and forever, which is
[#82](https://github.com/hleserg/Attadipa/issues/82)'s shape; guessing "fine"
restores exactly the silence being fixed.

51 cases in `.github/tests/approval-stall-decision-test.sh`, including the two
real runs above with the values the API actually returned, two that lift the
field split out of `pending/75-approval-stall.patch` and run it, eight over the
per-head marker rule, and **three over the deployed lines themselves** — the
writer checkout's `token:` (present, and naming the secret *before* the
`github.token` fallback, because `${{ A || B }}` takes A and the reversed order
is a silent no-op), the `approvals` job existing, and that job calling this
script rather than re-deriving it in a `run:` block. Those three are the fifth
review round's finding: every other case was about the *rule*, and what actually
ends the stall is two lines of workflow that nothing asserted. This repository
has lost a workflow line silently twice already — because the
loop that split lives in is inside an unapplied patch, which neither this suite
nor `ci.yml`'s `actionlint`/`shellcheck` globs can see. Extracted rather than
copied: a copy drifts, an extraction goes red the moment the patch stops
containing a recognisable `while IFS=`.

## What this does not cover

The intake gate is untouched and the actor check is the security boundary —
nothing here reads it. **But Option A does change who may drive a write-capable
agent, and an earlier version of this section said the opposite.** The
anti-recursion boundary is a *login-name* test —
`.github/scripts/intake-decision.sh:148` "|claude|github-actions)" rejects
`*"[bot]"`, `claude` and `github-actions`, and `queue-scan.jq` refuses the last
two as producers because *"our own output would start a billable writer: exactly
the loop the allowlist was built to avoid."* A fine-grained PAT belongs to a
**user**, so everything `claude-code-action` writes under it carries that login
with `author_association: OWNER` — and `queue-scan.jq` accepts on
`author_association` **before** the login test is ever evaluated. The guard is
not bypassed; it becomes unreachable, and no change to the login list can fix
that, because the login is genuinely the owner's.

Two concrete consequences, both reachable today:

- The agent files a blocker issue — which `CLAUDE.md` instructs it to do, and
  the BLOCKED template the workflow shows it ends with the literal *"How to
  resume: comment @claude on this issue."* — carrying `@claude` and the
  `attadipa-agent-task` marker. `queue-scan.jq` matches on the marker with no
  label needed, and the watchdog dispatches a billable writer on our own output,
  hourly.
- `pr-merge-sweep.yml` clears the unanswered-Codex hold only on
  `select(.bot | not)`. Under the PAT the agent's comments are `user.type ==
  "User"`, so the agent answers Codex on the owner's behalf and
  `merge-candidate.sh`'s *"only rule that can put a commit in `main`
  unattended"* merges over findings nobody replied to.

This is not an argument against Option A — it is the part of its price that was
missing. **Option B keeps the boundary intact**, because a second GitHub App
keeps a distinct bot login, and that is now a reason to prefer it beyond
attribution. Whichever is chosen, the recursion bound has to stop depending on
who wrote the issue: filed as its own task rather than decided here, because it
is a change to the intake rules and those are not this document's to make.
Found in review.

[#74](https://github.com/hleserg/Attadipa/issues/74) is the other way a pull
request ends up with no CI while looking merely unfinished — a merge conflict,
where no run is created at all. The two are independent, and a pull request can
be in both states at once.
