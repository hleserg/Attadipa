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

Two further habits, both deliberate:

- **Untrusted text never reaches a shell.** An issue body is passed through an
  `env:` variable, never interpolated into a `run:` block. `${{ github.event.issue.body }}`
  inside a script is a command injection with the attacker holding the pen.
- **`show_full_output` and `display_report` are off.** Claude's full output
  includes tool results, which can contain tokens, and Actions logs on a public
  repository are world-readable.

Permissions are per job. The top of every file is `permissions: {}` and each job
asks for exactly what it needs; the reviewer gets `contents: read` and cannot
push, whatever its opinion.

## Authentication

Two credentials matter and they do different things.

### 1. The Anthropic credential — required

Either works:

- `ANTHROPIC_API_KEY` — an API key. Simple, and what these workflows assume.
- `CLAUDE_CODE_OAUTH_TOKEN` — from `claude setup-token`.

Every workflow checks for one before doing anything and, if neither is present,
**comments once on the issue and exits green**. A workflow that is red because
a secret was never added is a workflow people learn to ignore.

A caution about OAuth: tokens produced by `claude setup-token` have not always
worked with the action. Do not assume the path works because the secret exists
— the smoke test in [CI_AND_REVIEW_PIPELINE](CI_AND_REVIEW_PIPELINE.md#smoke-tests)
is how you find out. If OAuth fails, add `ANTHROPIC_API_KEY` instead; the
workflows accept whichever is present.

### 2. The GitHub credential — and why it is *not* `GITHUB_TOKEN`

This is the non-obvious one, and it decides whether the loop closes at all.

> **GitHub does not start workflow runs for events created with the built-in
> `GITHUB_TOKEN`** (except `workflow_dispatch` and `repository_dispatch`).

So a pull request opened with `GITHUB_TOKEN` would never run CI — and the whole
point of this loop is that CI runs without anybody asking. The failure is
silent: a green-looking pull request with no checks on it at all.

The workflows therefore pass `github_token: ${{ secrets.FIREFLY_AGENT_TOKEN }}`,
which gives two working paths:

| If | Then | Commits authored by |
|---|---|---|
| `FIREFLY_AGENT_TOKEN` is unset (empty) | the action uses the **Claude GitHub App**, whose installation token does trigger workflows | `claude[bot]` |
| `FIREFLY_AGENT_TOKEN` is a fine-grained PAT with `contents: write`, `pull requests: write`, `issues: write` | the action uses that | the token's owner |

Either is fine. The app is one click at <https://github.com/apps/claude> and
needs no secret to rotate; the PAT needs no app installed. What is **not** fine
is `secrets.GITHUB_TOKEN`, and that is why it does not appear in any of these
files.

Ordinary bookkeeping — adding labels, posting the "no credential" comment — does
use `github.token`, because a label change is not supposed to start a workflow.

## Cost control

| Control | Where |
|---|---|
| **Kill switch** — repository variable `CLAUDE_AUTOMATION_ENABLED=false` stops every Anthropic-billed step everywhere, leaving ordinary CI running | checked in all four workflows |
| **Empty queue costs nothing** — the watchdog's scan is shell and one API call; Claude is invoked only when there is a task | `agent-queue-watchdog.yml` |
| **One writer** — a repository-wide concurrency group, so writers queue instead of colliding | `claude-agent.yml` |
| **Deduplication** — an issue already claimed is not picked up again | intake gate |
| **Turn limits** — 60 for implementation, 40 for review and repair | `claude_args: --max-turns` |
| **Job timeouts** — 60, 30 and 45 minutes | `timeout-minutes` |
| **Two repair attempts** — per problem chain, then it stops and says why | `claude-ci-repair.yml` |
| **Sticky review comment** — one comment edited in place, not a new one per push | `use_sticky_comment` |
| **No bots** — nothing can trigger a run by replying to a run | `allowed_bots: ""` |

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
2. Add `ANTHROPIC_API_KEY` under Settings → Secrets and variables → Actions.
3. Either install <https://github.com/apps/claude> on the repository, or add a
   fine-grained PAT as `FIREFLY_AGENT_TOKEN`.

That is the whole list. Everything else in this directory is already in the
repository and works without further configuration.
