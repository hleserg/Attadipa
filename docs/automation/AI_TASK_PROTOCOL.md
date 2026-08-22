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
<!-- attadipa-agent-task
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
| `reviewed_head` | the commit the producer looked at | a short SHA. **Checked, not decorative** — see below |
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

### A producing app may be named, and only named

The trust boundary above is the actor's write access. A producing agent may hold
no GitHub account at all: **ChatGPT reaches this repository through its GitHub
App and arrives as `chatgpt-codex-connector[bot]` with `author_association:
NONE`** — observed, not assumed; that is the login that reviewed pull request #11
on 2026-08-21. The bot rule refuses it, correctly, and leaves the queue with no
input.

So the owner may name app logins in the repository variable
**`ATTADIPA_TRUSTED_PRODUCERS`**, comma-separated:

```bash
gh variable set ATTADIPA_TRUSTED_PRODUCERS --body 'chatgpt-codex-connector[bot]'
```

Four properties keep that from being a hole, and each has a test:

| Property | Why |
|---|---|
| **empty by default** | no repository gains an exemption by taking this file |
| **`issues` events only** | the loop is an agent's own comment mentioning `@claude`; no entry can exempt a comment |
| **`claude` and `github-actions` can never be listed** | checked *after* the list in the gate **and again in the watchdog's scan**, so naming them does nothing in either place |
| **exact login match** | `codex-connector[bot]` does not match `chatgpt-codex-connector[bot]` |

Being on the list *is* the authorisation — an app is not a collaborator and has
no permission to look up. The owner editing that variable is the human decision,
and it is deliberately a variable rather than a code change, so choosing a
producer never requires a pull request against the security boundary.

The list is also read by `agent-queue-watchdog.yml`, which filters on
`author_association` and would otherwise skip exactly these tasks — issue #10 was
refused by the gate *and* invisible to the watchdog at the same time, which is
how a task disappears completely.

**The non-listable rule is repeated there rather than inherited, and that is not
duplication.** The watchdog hands over by `workflow_dispatch`, which the gate
trusts by construction and does not re-check the actor for. A `claude[bot]` entry
that the gate refuses to honour would therefore have been honoured by the
watchdog and dispatched into the one door that no longer asks — the repository's
own output starting a billable writer, which is the loop the whole allowlist
exists to avoid. Caught in review on #19, and now covered by
`.github/tests/watchdog-filter-test.sh`, which CI runs.

The scan filter lives in `.github/scripts/queue-scan.jq` for the same reason the
gate lives in a script: a filter inside a YAML block cannot be executed, and a
security boundary that has never been executed against a hostile input is a
hypothesis.

### `reviewed_head` is checked

The gate compares `reviewed_head` against the tip of the default branch through
the compare API and tells the agent, in its prompt, how many commits the tree has
moved and which files changed since. Three outcomes:

| Compare says | The agent is told |
|---|---|
| `ahead_by: 0` | the finding was made against current code |
| `ahead_by: N` | the branch has moved N commits; these files changed; **verify before implementing** |
| not a commit here | the field could not be checked at all |

This exists because the expensive failure of a review queue is not a bad finding,
it is a **stale** one: a problem that was already fixed, implemented again by an
agent that had no reason to doubt the issue. Neither the issue nor its producer
is automatically right. A finding that no longer holds is closed with the
evidence — a diff, a file, a line — and not implemented.

Omitting the field is allowed. The agent is then told that nothing can be said
about what has changed, which is worse for it than a SHA and better than a
number it would have trusted.

### A refused task says so

Most refusals are ordinary: an issue with no marker, a task somebody already
claimed. Those stay in the run log.

An issue that **carries a task marker and is still refused** is different — a
producer believes it has filed work and the repository has silently dropped it.
That case gets one comment on the issue naming the guard that rejected it and
the actor it saw, plus `needs-owner`. The comment is posted with the built-in
`GITHUB_TOKEN`, whose events GitHub does not use to start workflow runs, so it
cannot start another gate run.

This is aimed squarely at the likeliest silent failure in the whole loop: a
producing agent that files through a **GitHub App** rather than a user account.
Its login ends in `[bot]`, the gate rejects every bot by design, and without this
comment the task would simply never be picked up and nobody would be told.

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
                 │ agent:working│  one writer at a time, and the writer job
                 └──┬────┬──────┘  sets this label itself — so a claim never
      draft PR      │    │         outlives the agent that made it. A receipt
                    │    │         comment lands here within seconds
                 ┌──▼──┐ │ ┌────▼─────────┐
                 │review│ │ │agent:blocked │ + needs-owner / needs-hardware
                 └──┬──┘ │ └──────────────┘   cannot proceed
     CI green,      │    │
     merged         │    │
                 ┌──▼────▼──┐
                 │agent:done│
                 └──────────┘
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

## What the pipeline says, and when

**Rule: a request is never left unanswered.** Silence reads as "thinking", and
thinking is indistinguishable from dead. Before 2026-08-22 a task could be
accepted, worked for forty minutes and finished with nothing on the issue but a
label changing colour — which on a phone is invisible, and which made a working
pipeline and a broken one produce the same experience.

Three fixed points, and the first and third are structural rather than the
agent's good intentions — an agent that has to be running before it can say it
is running cannot report the run that never started.

| | who writes it | when | what it must contain |
|---|---|---|---|
| **Receipt** | the `acknowledge` job | seconds after the trigger, in parallel with the agent starting | that it was accepted, what was understood (kind, priority, research or implementation), the run link, what happens next, and the staleness verdict if the tree moved |
| **Progress** | the agent | once early with a plan; again only when the answer changes | what is actually going to change and where; a finding that does not reproduce, said as soon as it is known; a change of shape from the plan; work that will not fit |
| **Outcome** | the `Hand over` step | always, on every exit path | the pull request and **what is now being waited on**, or a clean run that produced nothing and why that is suspicious, or the conclusion word and what happens next |

A comment on the triggering comment gets an **👀 reaction within seconds**, before
any of the above renders. It is the only signal that appears on the comment
itself rather than below it.

**Bounds, because the failure mode on this side is noise.** At most three agent
comments before the outcome. A comment that repeats the previous one is worse
than silence: it teaches people to stop reading, and then the one that mattered
is missed too. No narrating tool calls, no progress bars, no "working on it".

**A blocked task is the one case the outcome step stays quiet.** The agent's own
`BLOCKED:` comment says more than the step could, so the step releases the claim
and does not talk over it.

None of this can loop. Every one of these is written with the built-in
`GITHUB_TOKEN`, and GitHub deliberately does not start workflow runs from events
that token creates.

The wording lives in [`.github/scripts/agent-say.sh`](../../.github/scripts/agent-say.sh)
as pure text renderers with no network and no environment, so
[`.github/tests/agent-say-test.sh`](../../.github/tests/agent-say-test.sh) can
assert the exact text rather than the workflow's intentions. Text nobody asserts
on drifts back to silence one edit at a time.

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
4. **One branch, one pull request.** Draft while it moves, ready when it does
   not, and **merged by the orchestrator once CI is green** (owner decision,
   2026-08-21). Nothing waits on a person for the merge itself.

   **Two different actors merge, under two different rules, and conflating them
   is how a finished `core/` pull request sits forever.** The *orchestrator* is
   a live session with the owner reachable: it merges anything once CI and the
   independent review are green, across every path in the repository. The
   *backstop routine* runs unattended with nobody watching, and it may merge
   only changes under `docs/` and not `docs/automation/`, at most three per run,
   under the six conditions in
   [attadipa-backstop-routine.md](attadipa-backstop-routine.md). So a green
   `core/` pull request is **not** picked up by the backstop and is not waiting
   for the owner either — it is waiting for an orchestrator session, and if none
   is running it waits. That is a real gap and it is named here rather than
   discovered.
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
  The mention has to be in the comment you are writing: the gate reads the
  comment for it, and the issue body only for the marker. Case does not matter
  — `@Claude` and `@CLAUDE` are the same request.
- **Stranded tasks**: `agent:working` with no activity for two hours goes back
  to `agent:ready` with a comment saying so. `agent:failed` with no
  `agent:ready` beside it — a shape only a run that died before finishing its
  hand-over can leave — goes back to the queue the same way, on a sweep of its
  own. It is **re-queued, not escalated**: a run that died is not a decision
  anybody has to make, and `needs-owner` means a decision only the owner can
  make.
- **A failed task gets one automatic retry, not an unbounded one.** The
  hand-over labels a generic failure `agent:failed` **and** `agent:ready`
  together — back in the queue, marked as not its first run — and the
  watchdog's scan honours that pair rather than dropping it, which is the
  defect [#82](https://github.com/hleserg/Attadipa/issues/82) found: the
  outcome comment promised a pick-up the filter was silently refusing to
  deliver. A **second** failure since a person last queued it gets
  `agent:blocked` + `needs-owner` instead of a third automatic run, because
  the cause did not go away between runs and retrying an unchanged failure
  hourly is buying the same answer on a bill — six runs on 2026-08-22 were
  exactly that.
- **A label and a comment are not the same restart.** Adding `agent:ready`
  yourself resets the retry budget; commenting `@claude` starts a run but does
  **not**. So if you have actually fixed what was breaking a task, label it —
  otherwise its next failure escalates straight back to you, carrying every
  failure from before your fix. Only a labelling by a person counts: the
  hand-over's own `agent:ready` is added by `github-actions[bot]`, and if that
  reset the count the bound would not exist.
- **Restarting an escalated task takes two labels, not one.** An issue the
  bound escalated carries `agent:blocked` + `needs-owner`, and **adding
  `agent:ready` beside them does nothing**: `.github/scripts/queue-scan.jq`
  drops anything carrying `agent:blocked` before it reads the ready/failed
  pairing at all, and `.github/scripts/intake-decision.sh` rejects the
  `labeled` event as already claimed. The issue would sit with three labels
  and no way through — the exact shape #82 was opened about, which is why this
  paragraph exists rather than being left as something to find out. So either
  **remove `agent:blocked` and add `agent:ready`**, or **comment `@claude`**,
  which needs no label surgery because a comment event skips the claimed-state
  check by design. `.github/tests/watchdog-filter-test.sh` asserts both halves
  of this — that the three-label state is refused, and that the escalation
  comment says so.
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
