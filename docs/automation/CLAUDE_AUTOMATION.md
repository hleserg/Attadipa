# Claude in CI: what runs, what it may touch, and how to stop it

Four workflows, one shared kill switch, and a security model that assumes the
repository is public — because it is, and anybody can open an issue on it.

| Workflow | Trigger | Can it write code? |
|---|---|---|
| [`claude-agent.yml`](../../.github/workflows/claude-agent.yml) | an issue or a comment from somebody trusted; a dispatch from the watchdog | **yes** — branches and pull requests |
| [`claude-pr-review.yml`](../../.github/workflows/claude-pr-review.yml) | a pull request opened, pushed to, reopened or marked ready | **no** — `contents: read` |
| [`claude-ci-repair.yml`](../../.github/workflows/claude-ci-repair.yml) | CI failing on a `claude/*` branch of this repository | **yes**, to that branch only |
| [`agent-queue-watchdog.yml`](../../.github/workflows/agent-queue-watchdog.yml) | hourly | no — it dispatches the first one |

## The security model

The repository is public. The question that matters is: **can a stranger make a
write-capable agent do something?**

**No, and there are four independent reasons.**

1. **Actor permission.** Before any billable step, `claude-agent.yml` asks the
   GitHub API for the triggering actor's permission and requires `write`,
   `maintain` or `admin`. A drive-by issue saying `@claude delete everything`
   fails here. The `producer:` field in a task marker is *data* and proves
   nothing — see [AI_TASK_PROTOCOL](AI_TASK_PROTOCOL.md#the-marker-is-data-not-a-permission).
2. **The action's own check.** `anthropics/claude-code-action@v1` performs the
   same check independently, and `allowed_non_write_users` is left empty so
   there is no bypass list to get onto.
3. **No bots.** `allowed_bots` is empty, which means no bot may trigger the
   action at all. On a public repository `'*'` would let any installed GitHub
   App drive a write-capable agent with a prompt it controls. The workflow also
   refuses bot actors itself, because the loop it prevents — Claude comments,
   the comment mentions `@claude`, Claude runs — costs money until somebody
   notices.
4. **No `pull_request_target`.** That trigger grants secrets to a workflow
   examining untrusted code, and it is how tokens leak. A fork's pull request
   therefore gets ordinary CI and no AI review, which is the correct trade and
   not a limitation to work around.

### The gate is a tested file, not a paragraph of YAML

The decision is [`.github/scripts/intake-decision.sh`](../../.github/scripts/intake-decision.sh),
and [`.github/tests/intake-gate-test.sh`](../../.github/tests/intake-gate-test.sh)
runs that same function over sixteen cases — a stranger who has copied the task
marker verbatim, a read-only collaborator, a triage collaborator who can label
but not write, a bot answering its own comment, an already-claimed task, a
closed issue. CI runs it on every push.

The reason it is a file rather than an `if:` is that a security boundary nobody
has executed against a hostile input is a hypothesis. The reason there is one
file rather than a workflow and a test that mirrors it is that a mirror drifts,
silently, in the direction of whichever copy somebody edited.

One detail in the workflow is easy to miss and load-bearing: the checkout that
fetches the script is pinned to the **default branch**. For a
`pull_request_review_comment` event `GITHUB_REF` is `refs/pull/N/merge`, so an
ordinary checkout would fetch a fork's version of the very script that decides
whether a write-capable agent may run. The gate's rules come from `main` or from
nowhere.

Two further habits, both deliberate:

- **Untrusted text never reaches a shell.** An issue body is passed through an
  `env:` variable, never interpolated into a `run:` block. `${{ github.event.issue.body }}`
  inside a script is a command injection with the attacker holding the pen.
- **`show_full_output` is off; `display_report` is on.** They are different
  things and only one of them is a leak. Full output prints every message
  including tool results, which can contain tokens, into a world-readable log.
  The report is Claude's own summary of what it did — and turning *that* off as
  well was a mistake, found by smoke test A: the agent ran twenty-eight turns
  successfully and left no branch, no pull request and no comment, so the only
  evidence any work had happened was a green tick. An agent whose conclusions
  nobody can read is not an agent, it is a bill.

Permissions are per job. The top of every file is `permissions: {}` and each job
asks for exactly what it needs; the reviewer gets `contents: read` and cannot
push, whatever its opinion.

One grant in that list is not about least privilege and is easy to mistake for
a mistake: **`id-token: write`** on every job that runs the action. When
`ATTADIPA_AGENT_TOKEN` is empty — the supported default — the action authenticates
as the Claude GitHub App by exchanging this workflow's GitHub OIDC token for an
installation token, and without the permission that exchange fails with
`Unable to get ACTIONS_ID_TOKEN_REQUEST_URL`. The error surfaces as *"Could not
fetch an OIDC token"* and reads like a problem with the Anthropic credential,
which it is not.

## Authentication

Two credentials matter and they do different things.

### 1. The Anthropic credential — required

Either works, and **they are billed differently** — which is the first thing to
decide, not the last:

| Secret | Billed to | Get it |
|---|---|---|
| **`CLAUDE_CODE_OAUTH_TOKEN`** | a Claude **Pro or Max subscription**. No per-token charge; it consumes the subscription's quota | `claude setup-token` on your own machine |
| `ANTHROPIC_API_KEY` | an **API account, per token**. A separate bill from the subscription | console.anthropic.com |

The action's own setup guide is explicit that the OAuth path is the
subscription one: *"Pro and Max users can generate this by running
`claude setup-token` locally"*. If the point of this loop is to remove courier
work rather than to open a metered account, that is the secret to add.

Set it without the value passing through a terminal history or a chat log:

```bash
claude setup-token                                     # prints the token
gh secret set CLAUDE_CODE_OAUTH_TOKEN --repo <owner>/<repo>   # paste; input is not echoed
```

Every workflow checks for one before doing anything and, if neither is present,
**comments once on the issue and exits green**. A workflow that is red because
a secret was never added is a workflow people learn to ignore.

A caution about OAuth: tokens produced by `claude setup-token` have not always
worked with the action. Do not assume the path works because the secret exists
— the smoke test in [CI_AND_REVIEW_PIPELINE](CI_AND_REVIEW_PIPELINE.md#smoke-tests)
is how you find out. If OAuth fails, `ANTHROPIC_API_KEY` is the fallback and
the workflows accept whichever is present — but it is a fallback with a meter
on it, so try the subscription path first and find out rather than assume.

### 2. The GitHub credential — and why it is *not* `GITHUB_TOKEN`

This is the non-obvious one, and it decides whether the loop closes at all.

> **GitHub does not start workflow runs for events created with the built-in
> `GITHUB_TOKEN`** (except `workflow_dispatch` and `repository_dispatch`).

So a pull request opened with `GITHUB_TOKEN` would never run CI — and the whole
point of this loop is that CI runs without anybody asking. The failure is
silent: a green-looking pull request with no checks on it at all.

The workflows therefore pass `github_token: ${{ secrets.ATTADIPA_AGENT_TOKEN }}`,
which gives two working paths:

| If | Then | Commits authored by |
|---|---|---|
| `ATTADIPA_AGENT_TOKEN` is unset (empty) | the action uses the **Claude GitHub App**, whose installation token does trigger workflows | `claude[bot]` |
| `ATTADIPA_AGENT_TOKEN` is a fine-grained PAT with `contents: write`, `pull requests: write`, `issues: write` | the action uses that | the token's owner |

Either is fine. The app is one click at <https://github.com/apps/claude> and
needs no secret to rotate; the PAT needs no app installed. What is **not** fine
is `secrets.GITHUB_TOKEN`, and that is why it does not appear in any of these
files.

Ordinary bookkeeping — adding labels, posting the "no credential" comment — does
use `github.token`, because a label change is not supposed to start a workflow.

## The tool list, and why an empty one fails silently

`prompt:` selects the action's **agent mode**. Agent mode sets no default
`--allowedTools` and no `--permission-mode` — tag mode sets both, agent mode
passes through only what the workflow writes. The headless SDK has no prompt
handler, so any tool that would fall through to *ask* is **denied, with no error
and no line in the log**.

Observed on 2026-08-21, before the tool lists existed:

| Run | What it did | What reached anybody |
|---|---|---|
| `Independent review` on #9 | ran 41 s, exit 0 | no comment, no label |
| `Claude agent` on issue #5 | ran ~7 min, exit 0, `CONCLUSION: success` | no branch, no pull request, no comment |

Both had read everything they needed. Neither could say so. A green check that
means "the agent was not allowed to speak" is worse than a red one.

So every Claude step in this repository names its tools explicitly:

| Workflow | Tools | Why |
|---|---|---|
| `claude-pr-review.yml` | `Read,Glob,Grep`, read-only `git`, and the four `gh pr` verbs that publish a review | it has an opinion and exactly enough hands to say it. No `Write`, no `Edit` |
| `claude-agent.yml` | `Read,Glob,Grep,Edit,Write,Bash,WebFetch,WebSearch,TodoWrite,Task` | it implements; its boundary is the job's `permissions:` and its branch |
| `claude-ci-repair.yml` | `Read,Glob,Grep,Edit,Write,Bash,TodoWrite` | same, narrower — it fixes one failure |

Verified against `anthropics/claude-code-action` at the `v1` tag (v1.0.198,
`3f854a8`). Two facts from that reading are worth keeping:

- `grep -rn "addLabels" src/` returns nothing. **The action has no label
  feature.** `ai-review:pass` and `ai-review:blocking` exist only because the
  prompt tells Claude to run `gh pr edit`, which needs `Bash(gh pr edit:*)`.
- `display_report` writes the **GitHub Step Summary**, not a pull request
  comment, and `show_full_output` governs the **runner log**. Neither has ever
  posted to a pull request, in any version — `display_report`'s only consumer is
  `src/entrypoints/run.ts`, which calls `writeStepSummary`.

  This matters because both were changed at once while chasing the same symptom.
  Turning `display_report` back on was right and is kept: a run whose reasoning
  nobody can read is a bill. But it was not what stopped findings reaching the
  pull request — that was the missing tool list above, and the two fixes are
  independent. `show_full_output` stays off; it is the one that leaks.

### One live hazard

`base-action/src/parse-sdk-options.ts`:

```ts
const showFullOutput = options.showFullOutput === "true" || isDebugMode;
```

Turning on GitHub's **step-debug logging** (`ACTIONS_STEP_DEBUG`) overrides
`show_full_output: "false"` and writes every tool result — which can contain
tokens — into a run log that is world-readable on a public repository. Do not
enable step debugging on this repository while the agent workflows are live.

## Cost control

What "cost" means depends on which credential is in use. With
`ANTHROPIC_API_KEY` it is money, per token. With `CLAUDE_CODE_OAUTH_TOKEN` it is
the subscription's quota — a runaway loop does not produce an invoice, it
produces a rate limit at the moment you wanted to use Claude yourself. Every
control below applies either way, and the reason they exist is the second case
as much as the first.

| Control | Where |
|---|---|
| **Kill switch** — repository variable `CLAUDE_AUTOMATION_ENABLED=false` stops every Anthropic-billed step everywhere, leaving ordinary CI running | checked in all four workflows |
| **Empty queue costs nothing** — the watchdog's scan is shell and one API call; Claude is invoked only when there is a task | `agent-queue-watchdog.yml` |
| **One writer** — a concurrency group on the agent job, so writers queue instead of colliding. On the job and not on the workflow: a workflow-level group also holds the intake gate, and GitHub cancels a *pending* run when a newer one joins the group, so a burst of events loses everything but the last before anything reads it. Three tasks were queued and none started this way on 2026-08-22 | `claude-agent.yml` |
| **Deduplication** — an issue already claimed is not picked up again | intake gate |
| **It always answers** — an 👀 reaction within seconds, a receipt saying what was understood, and an outcome comment on every exit path. Silence and a dead pipeline used to be the same experience | `acknowledge` job, `Hand over` step, `agent-say.sh` |
| **Turn limits** — 60 for implementation, 100 for review, 40 for repair | `claude_args: --max-turns` |
| **Job timeouts** — 60, 30 and 45 minutes | `timeout-minutes` |
| **Two repair attempts** — per problem chain, then it stops and says why | `claude-ci-repair.yml` |
| **Sticky review comment** — one comment edited in place, not a new one per push | `use_sticky_comment` |
| **No bots** — nothing can trigger a run by replying to a run | `allowed_bots: ""` |

The review's limit was 40 until 2026-08-22, and it was the wrong number for the
wrong reason. On pull request #39 the reviewer read a thirty-file diff, worked
for six and a half minutes, returned `is_error: false` — and was killed at turn
50 for exceeding 40, having posted no comment and set no label. The run cost
exactly what it would have cost with a higher ceiling and delivered nothing. A
limit that stops work *after* it has been paid for is not a cost control; the
control that actually bounds spend is `timeout-minutes`, which is denominated in
the thing being billed. `--max-turns` bounds a different failure — a session
stuck in a loop — and for that, 100 is above what a real diff was observed to
need.

The same incident is why the "review did not happen" note now reads the action's
own execution log instead of listing two possible causes: it named neither the
turn limit nor a tool denial, so the first person to hit one went looking for a
spent quota that was not spent.

To stop all spending immediately:

```bash
gh variable set CLAUDE_AUTOMATION_ENABLED --body false
```

and to resume:

```bash
gh variable set CLAUDE_AUTOMATION_ENABLED --body true
```

Unset reads as enabled, so a fresh clone is not silently inert. More ways out,
including disabling workflows entirely, are in [RECOVERY](RECOVERY.md).

## What is deliberately not automated

**Merging.** No auto-merge for anything under `core/`, `platform/`, `link/`,
`apps/` or `sim/`. The point of this loop is to remove the courier work, not
the last meaningful gate: an agent does the work, opens a **draft** pull
request, fixes CI and collects an independent review, and then a human decides.

Auto-merge for Dependabot patch updates and documentation-only changes is a
reasonable thing to consider later. It is a separate decision, and it is not
made here.

**Hardware.** No workflow claims a hardware result, and there is no
hardware-in-the-loop runner. CI prints
`NOT EXECUTED — HARDWARE REQUIRED` on every run for exactly this reason.

## Setting it up from nothing

1. `gh variable set CLAUDE_AUTOMATION_ENABLED --body true`
2. Add `CLAUDE_CODE_OAUTH_TOKEN` (subscription) or `ANTHROPIC_API_KEY` (metered
   API account) under Settings → Secrets and variables → Actions. See
   [Authentication](#authentication) — the difference is a bill.
3. Either install <https://github.com/apps/claude> on the repository, or add a
   fine-grained PAT as `ATTADIPA_AGENT_TOKEN`.

That is the whole list. Everything else in this directory is already in the
repository and works without further configuration.
