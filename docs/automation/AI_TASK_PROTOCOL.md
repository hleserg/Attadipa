# The task protocol

One queue, and it is GitHub Issues.

The problem this solves is not that agents cannot work. It is that the owner
was the transport between them: copying a prompt out of one chat and into
another, turning a review paragraph into an issue by hand, remembering to ask
whether CI had gone green. Every one of those is a message-passing job, and a
person is a slow and forgetful message bus.

So a task is a GitHub issue, and everything an agent needs to start is in it.

---

## The marker

A producing agent puts a machine-readable block at the top of the issue body:

```html
<!-- firefly-agent-task
producer: chatgpt
task_type: continuous-review
reviewed_head: 53f8cea
priority: P1
state: ready
-->

@claude

...the task, in prose...
```

| Field | Meaning | Values |
|---|---|---|
| `producer` | which agent filed it | `chatgpt` · `claude` · `owner` |
| `task_type` | what kind of work it is | see below |
| `priority` | queue order | `P0` … `P3`; default `P2` |
| `reviewed_head` | the commit the producer looked at | a short SHA, so a reviewer can tell what has changed since |
| `state` | where it is | `ready` · `working` · `review` · `blocked` · `done` |

`task_type` is at least:

| Type | Means | Implementation allowed? |
|---|---|---|
| `continuous-review` | review of a commit range | yes |
| `upstream-intelligence` | what changed in a dependency and what it costs us | **research only** |
| `quality-audit` | existing code or documents held against a standard | yes |
| `next-task-research` | find out what the next task needs before it starts | **research only** |
| `readiness-audit` | are we where a milestone says we are | **research only** |

A research-only type means exactly that: verify sources, write to
`docs/research/`, update the reuse ledger, open an ADR if a decision was
genuinely made — and do **not** write speculative implementation code. A
research task that arrives as a pull request full of new subsystems has not
been done, it has been guessed at.

### The marker is data, not a permission

**`producer: chatgpt` proves nothing.** Anybody with a GitHub account can open
an issue on this public repository and type it.

The trust boundary is the *actor*: the workflow checks that whoever created the
issue or comment has `write`, `maintain` or `admin` permission, using the
GitHub API, before any Anthropic-billed step runs. The marker decides *what
kind* of work it is; write access decides *whether there is any*.

That check is [`.github/scripts/intake-decision.sh`](../../.github/scripts/intake-decision.sh)
and it is covered by a test that includes a stranger who has copied this
marker word for word.

There is one deliberate exception, and it is trusted by construction rather
than by exemption: `workflow_dispatch`. GitHub only accepts a manual dispatch
from an actor with write access, and the only other way to produce one is a
workflow already in this repository. That is how the queue watchdog hands over
a task whose event was lost.

---

## Lifecycle

```
                 ┌──────────────┐
   issue filed → │ agent:ready  │ ← watchdog returns stranded tasks here
                 └──────┬───────┘
                        │  claude-agent.yml accepts it
                 ┌──────▼───────┐
                 │ agent:working│  one writer at a time, repository-wide
                 └──┬────┬──────┘
      draft PR      │    │      cannot proceed
                 ┌──▼──┐ │ ┌────▼─────────┐
                 │review│ │ │agent:blocked │ + needs-owner / needs-hardware
                 └──┬──┘ │ └──────────────┘
      owner merges  │    │
                 ┌──▼───▼──┐
                 │agent:done│
                 └─────────┘
```

The labels are the state. There is no separate database, and no field in a
comment that has to be kept in step with reality: if an issue is labelled
`agent:working`, an agent has it, and if that stops being true for two hours
the watchdog says so and puts it back.

### Labels

| Group | Labels |
|---|---|
| state | `agent:ready` `agent:working` `agent:review` `agent:blocked` `agent:done` `agent:failed` |
| who | `agent:claude` · `source:chatgpt` `source:claude` `source:owner` |
| kind | `type:review` `type:upstream` `type:quality` `type:research` `type:readiness` |
| priority | `priority:P0` … `priority:P3` |
| humans | `needs-owner` `needs-hardware` |
| CI | `ci:repairing` `ci:failed` |
| review | `ai-review:pass` `ai-review:blocking` |

The intake workflow derives the `type:`, `priority:` and `source:` labels from
the marker, so a producer does not have to set them and cannot set them
inconsistently with the marker it wrote.

---

## What an agent does with a task

1. **Read before writing.** The issue and all its comments, `CLAUDE.md`,
   `docs/master-prompt-final.md`, `STATUS.md`, `TASKS.md`, the ADRs the task
   touches, and `docs/research/REUSE_LEDGER.md`.
2. **Deduplicate.** Open issues and open pull requests first. Two agents
   solving the same finding twice is the failure this queue exists to prevent,
   not one it is allowed to cause.
3. **Reuse before writing.** `CLAUDE.md`'s rule, and it applies to agents more
   than to people, because an agent will happily write four hundred lines that
   already exist under a licence we can use.
4. **One branch, one draft pull request.** `claude/issue-<number>-<slug>`, and
   the body carries `Fixes #<number>` so the issue closes when it merges.
5. **Never a hardware claim.** Anything needing a board, an instrument or a
   measurement is `NOT EXECUTED — HARDWARE REQUIRED`.
6. **Leave it continuable.** `STATUS.md` and `TASKS.md` updated in the same
   commit as the change they describe, so the next agent does not need this
   conversation.

### The pull request body

Not a formality — it is what the independent reviewer and the owner read
instead of the agent's reasoning, which nobody can see:

```
Fixes #<issue>

## Problem
## Solution
## Upstream and reuse
   what was taken, from where, at which commit, under which licence
## Tests
   what ran, and what the result proves
## Hardware
   NOT EXECUTED — HARDWARE REQUIRED, and what would have to be measured
## Risks
## Remaining blockers
```

---

## Blocking

A blocker is a first-class outcome, not a failure. The comment format is
`CLAUDE.md`'s, with one addition — the owner should be left with a single
concrete action, not with the job of reconstructing what the agent was thinking:

```
BLOCKED
Reason:
Evidence:
Impact:
What can be done automatically:
Owner action required:
How to resume:
```

and the issue gets `agent:blocked` plus `needs-hardware` or `needs-owner`.

**When to stop and ask**, and only these:

1. an unknown product requirement;
2. two architectures with a real trade-off that facts cannot settle;
3. something physical must happen to a board;
4. a secret or credential is needed;
5. an irreversible operation;
6. compatibility or stored data could be damaged and no policy covers it.

**When not to**: anything the specification, `STATUS.md`, `TASKS.md`, an ADR,
the issue or the upstream research already answers. Asking there is not caution,
it is handing the work back.

When an agent does stop, it gives two to four concrete options and a
recommendation. "What should I do?" is not a question, it is an absence of one.

---

## Deduplication and retries

- **Duplicate work**: the intake workflow refuses an issue that already carries
  `agent:working`, `agent:review`, `agent:blocked` or `agent:done`. A fresh
  `@claude` comment overrides that, because a human asking again is a decision.
- **Stranded tasks**: `agent:working` with no activity for two hours goes back
  to `agent:ready` with a comment saying so.
- **CI failures**: repaired automatically at most twice per problem chain, from
  the actual failing log. After that the pull request gets `ci:failed` and
  `agent:blocked` and a human is asked. `/ci-repair reset` clears the counter.
- **Cost**: every workflow checks the `CLAUDE_AUTOMATION_ENABLED` repository
  variable before anything billable, the queue watchdog costs nothing when the
  queue is empty, and one writer runs at a time.

---

## How this relates to TASKS.md

They are not the same list and neither is a copy of the other.

| | Holds | Granularity |
|---|---|---|
| [`TASKS.md`](../../TASKS.md) | the roadmap: milestones, dependencies between large pieces of work, the record of what was decided and why | a task is days of work and outlives any one agent run |
| GitHub Issues | executable work packages, findings, bugs, research assignments | an issue is one agent run, or a few |

The link between them is by reference and nothing else: an issue that
implements part of a roadmap task names it (`T-045`), and a roadmap task that
has been broken into issues names their numbers. Nobody maintains two copies of
the same sentence, because a copy is a thing that goes stale silently.
