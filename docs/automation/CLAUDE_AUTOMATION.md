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
3. **One named bot, and it is this repository's own dispatcher.** On a public
   repository `'*'` would let any installed GitHub App drive a write-capable
   agent with a prompt it controls, so the list is never a star. It is also not
   empty, and the reason is worth reading before anybody "tightens" it back:
   `allowed_bots: ""` meant the **hourly watchdog had never once started an
   agent**. It hands a task over with `gh workflow run` under the built-in
   `GITHUB_TOKEN`, so the dispatching actor is `github-actions[bot]`, and the
   action refused it in about five seconds — before the agent read a file,
   without writing an execution log, so the hand-over could only say `no
   conclusion`. Four issues were written off as unexplained model deaths. The
   list therefore names exactly that dispatcher:

   | Workflow | `allowed_bots` | Why |
   |---|---|---|
   | `claude-agent.yml` | `github-actions` | the watchdog dispatches it as `github-actions[bot]`; nothing else in this repository holds `actions: write` |
   | `claude-pr-review.yml` | `claude` | its `if:` deliberately admits `claude[bot]`, because a blanket bot guard skipped the review on the agent's own pull requests — the ones it exists for |
   | `claude-ci-repair.yml` | `""` | its trigger is `workflow_run`; the actor is whoever pushed, today a person holding `ATTADIPA_AGENT_TOKEN`. It admits no bot, so it needs to name none — but it is one token change from the same failure |

   **The reviewer had the identical defect, and it hid better.** Its `if:`
   deliberately lets `claude[bot]` through — the comment there says a blanket
   bot guard "skipped the review on exactly the pull requests this workflow
   exists to review: the agent's own" — and then the step handed the action the
   one list that does not contain `claude`. So the guard let the job start and
   the action refused it:

   ```
   Checking permissions for actor: claude[bot]
   Actor is a GitHub App: claude[bot]
   Actor type: Bot
   ##[error]Action failed with error: Workflow initiated by non-human actor:
   claude (type: Bot). Add bot to allowed_bots list or use '*' to allow all bots.
   ```

   Byte-identical on runs `32597016812` (#95), `32596445164` (#94),
   `32595947792` (#92) and `32595273274` (#88) — five, five, five and four
   seconds. **No agent-authored pull request had ever been reviewed.** Worse
   than the watchdog's version, because every one of those jobs reported
   **success**: the `Review` step carries `continue-on-error`, which is correct
   for its own reason — a red check meaning "we ran out of quota" is
   indistinguishable from "the reviewer found something" — and it turned a
   silent refusal into a green tick.

   The three alternatives were ruled out by evidence rather than argued away:
   the `claude-*.yml` files on those branches are byte-identical to `main`, so
   the action's workflow-validation refusal cannot apply; a human-authored
   review ran normally for six minutes at 20:39, so the credential was not
   spent; and no execution log exists at all, which excludes the tool-list
   cause, since that one needs real work before it shows up.

   The workflow's own "the review did not run" comment listed five candidate
   causes and **this was not among them** — it steered every reader towards the
   branch-behind-`main` case, which was the wrong answer. It is now cause 1.

   `github-actions` is not a concession to outside apps: it is the actor of
   this repository's own workflows, and no third party can present as it. It is
   also **not** a producer grant — `.github/scripts/queue-scan.jq` still refuses
   `claude` and `github-actions` in `ATTADIPA_TRUSTED_PRODUCERS`, so our own
   output still cannot enqueue a billable writer. Those are two different rules
   and `.github/tests/bot-actor-test.sh` asserts both, including
   that the list never becomes `'*'`. The workflow also refuses bot actors
   itself where the actor is not the dispatcher, because the loop it prevents —
   Claude comments, the comment mentions `@claude`, Claude runs — costs money
   until somebody notices.
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

  It stays off, and the price is paid elsewhere rather than waived: with it off,
  a failure's cause is on the runner and nowhere else, so both agent workflows
  now read the unpublished execution log through
  [`failure-reason.sh`](../../.github/scripts/failure-reason.sh) and put one
  whitelisted line on the issue. Turning `show_full_output` on to answer the
  same question would publish every tool result to answer one of them.

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
| **It always answers, and says why** — an 👀 reaction within seconds, a receipt saying what was understood, and an outcome comment on every exit path. A failure carries the reason, extracted on the runner from a log that is not published | `acknowledge` job, `Hand over` step, `agent-say.sh`, `failure-reason.sh` |
| **Turn limits** — 200 for implementation, 100 for review, 40 for repair. The writer's was 60 until 2026-08-22, when six runs died at turn 61 with an accurate plan posted and nothing on the branch | `claude_args: --max-turns` |
| **Model and effort are pinned, not defaulted** — `claude-opus-5` at `--effort max` in all three. The action has **no `model:` input**, so the model is a string inside `claude_args` and its absence is not an error: it silently falls back to whatever the CLI defaults to. This loop ran that way from the day it was built until 2026-08-22, so no past run can be attributed to a model and no two runs can be compared. Flags read off `claude --help`: `--model` takes an alias or a full name, `--effort` takes one of `low, medium, high, xhigh, max`. The full name is pinned rather than the `opus` alias so a new Opus cannot silently change what this loop is — somebody has to come and move it, which is the intended cost. Owner decision, 2026-08-22 | `claude_args: --model`, `--effort`, `.github/tests/bot-actor-test.sh` |
| **Job timeouts** — 60, 30 and 45 minutes | `timeout-minutes` |
| **Two repair attempts** — per problem chain, then it stops and says why | `claude-ci-repair.yml` |
| **A bounded retry, not an hourly one** — the hand-over relabels a generically-failed task `agent:failed` + `agent:ready` so it comes back, and the watchdog honours that pair. It honours it *once*. A candidate that has failed more than once since a **person** last put it in the queue is relabelled `agent:blocked` + `needs-owner` and told so in a comment, and the scan moves to the next candidate rather than spending the tick on nothing. Six runs on 2026-08-22 were the same task buying the same answer | `agent-queue-watchdog.yml` (`scan`), `.github/scripts/failure-count.jq` |
| **Only a person resets the budget** — `failure-count.jq` counts `agent:failed` labellings since the last `agent:ready` labelling *by a non-bot actor*. The hand-over's own `agent:ready` is added by `github-actions[bot]`, and if that counted as a reset the bound would not exist | `.github/scripts/failure-count.jq`, `.github/tests/failure-count-test.sh` |
| **Nothing strands silently** — `agent:failed` with no `agent:ready` beside it is a task nothing would ever pick up, with nothing on the issue saying so; #27 and #28 sat in exactly that state. An hourly sweep re-queues them and says what happens next. It **re-queues, it does not escalate**: "a run died, press the button again" is the message-bus role the queue exists to remove. `agent:failed` deliberately stays, so the bound above still decides | `agent-queue-watchdog.yml` (`stranded`), `.github/scripts/stranded-failures.jq`, `.github/tests/stranded-failures-test.sh` |
| **Restarting an escalated task takes two labels, and the comment says so** — `agent:blocked` is refused by both dispatch paths, so adding `agent:ready` beside it does nothing. The escalation comment leads with `@claude` (which needs no label surgery) and spells out `remove agent:blocked` **and** `add agent:ready` for the label route. A comment promising a restart that the labels forbid is the defect #82 was opened about, reproduced in the escalation path; it is asserted rather than reviewed | `agent-queue-watchdog.yml`, `.github/tests/watchdog-filter-test.sh` |
| **The whole queue is read, not the first page** — `gh api --paginate` alone writes one JSON array *per page* back to back, and `jq -f` then runs once per page: only page one's pick was ever read, so a P0 on page 2 would lose to a P2 on page 1 forever, green all the way. `--paginate --slurp` plus a shape-tolerant `add` flattens it. Latent at 15 open issues, real past 100 | `agent-queue-watchdog.yml` |
| **Sticky review comment** — one comment edited in place, not a new one per push | `use_sticky_comment` |
| **Bots named, never starred** — a workflow that admits a bot actor must name it, and nothing may name `'*'`. The writer admits `github-actions`, the actor its own watchdog dispatches as; the reviewer admits `claude`, the actor that opens the pull requests it exists to review; the CI repairer admits none and names none. Empty lists had made the first two refuse silently — the watchdog had never started an agent, and no agent-authored pull request had ever been reviewed. The test asserts the rule rather than the three instances, so a fourth workflow is checked the day it grows an exemption | `allowed_bots`, `.github/tests/bot-actor-test.sh` |

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

**And the lesson was not applied to the writer, which cost six runs the same
afternoon.** The agent's ceiling stayed at 60 while the reviewer's went to 100,
and on 2026-08-22 issues #71 (three times), #67, #75 and #78 all ended
identically: accepted, an accurate plan comment about three minutes in, then
dead with nothing on the branch. Read from run `32587675386`'s execution log —
`"subtype": "error_max_turns"`, `"num_turns": 61`, `"total_cost_usd": 3.00`,
after 8 min 49 s of real work. **An accurate report over an empty branch is the
one outcome nobody can act on**, and every one of them was paid for in full.

The agent is the job that does strictly more than the reviewer: it reads the
same material and then implements, tests, pushes and reports. Its ceiling is now
**200**, and that is not a round guess — the reading order the prompt mandates
(CLAUDE.md, the specification, STATUS, TASKS, the ADRs, the reuse ledger, then
the open issues and pull requests) spends twenty to thirty turns before a line
is written, and the run above was still mid-implementation at sixty-one. Spend
stays bounded by `timeout-minutes: 60`, which is denominated in the thing
actually being billed.

`claude-ci-repair.yml` is still at 40 and has **not** been examined against this;
nothing has been observed hitting it, and raising a limit on a hunch is how the
review got 40 in the first place.

### Raising the ceiling exposed the failure underneath it

The very next run of #67 on the raised ceiling died again, in ninety seconds
instead of nine minutes, and said something new by saying nothing:

```json
{ "type": "result", "subtype": "success", "is_error": true,
  "duration_ms": 84607, "num_turns": 20, "total_cost_usd": 0.689,
  "permission_denials_count": 0 }
```

Run `32589375744`. `subtype: success` with `is_error: true` is a real session
that ended badly under a name reserved for one that did not — and unlike
`error_max_turns` it names no cause. `permission_denials_count: 0` rules out the
tool-list failure this document describes above. Twenty turns and sixty-nine
cents rule out never starting. The published run log contains the SDK options,
an init line, and that object; there is nothing else in it, because
`show_full_output` is off and that is correct.

So the outcome comment on the issue said *"the cause is in there, and it is worth
reading before retrying"* about a log that had been emptied of the cause on
purpose. The advice was right and the address was wrong, and an afternoon went
into guessing at what the address would have said.

**[`failure-reason.sh`](../../.github/scripts/failure-reason.sh) is the fix.** The
action writes its *full* execution log to `$RUNNER_TEMP` and that file is never
published; the extractor reads it there and prints one line, chosen by a
whitelist of error grammars — an API status line, a context refusal, a credit
balance, an expired OAuth token, a service `overloaded_error`. Anything it does
not recognise is reported as `unclassified` together with facts that are
structural rather than textual: the SDK's own subtype, the turn count, and
whether a final message existed at all.

**The whitelist is the security model, not a convenience.** The same log holds
every tool result — file contents, `gh` output, environment echoes — which is
exactly why it is not published, and an extractor that printed "whatever was
last" would have published it one line at a time.
[`failure-reason-test.sh`](../../.github/tests/failure-reason-test.sh) therefore
puts an API key, a token-shaped string and a private key into a log beside a real
error and asserts that only the error comes out, including on the fallback path
where nothing matched. An `unclassified` failure is a gap in that list, and the
honest response is to widen the list rather than to widen the grammar until
something matches — the failure comment says so, in those words, on the issue.

### A green check is not a review, and until now they looked the same

The reviewer's own reporting keyed off `steps.review.outcome == 'failure'`,
which catches a review that ran and died. It did not catch the action deciding
not to run at all, because the action reports that as **success**:

```
##[warning]Skipping action due to workflow validation: Workflow validation
failed. The workflow file must exist and have identical content to the version
on the repository's default branch.
Exiting due to workflow validation skip
```

Observed on [#81](https://github.com/hleserg/Attadipa/pull/81) at `7a4d0f1`,
run `32591032435`, 1.6 seconds. The rule is a good one — without it a pull
request could edit this reviewer into rubber-stamping the same pull request, and
#81 edits `claude-pr-review.yml` — but what a reader saw on the page was a green
check, no comment and no label. That is indistinguishable from a review that ran
and found nothing, and it is the reading somebody will take.

The note listing the causes already described this one, in full, as cause 3. It
had simply never fired for it.

The detector is **structural, not textual**: the action writes its execution log
only once the model has been invoked, so a step that reports success and left no
log never reached the model. That covers the validation skip, a refused
credential, and anything future that exits early, without the workflow having to
recognise each by name.

What #67 was doing when it died is still **UNKNOWN**, and this document will not
guess at it: the next occurrence names itself. One measurement is worth
recording as motive rather than conclusion, though — the reading order the prompt
mandates is over 500 KB of Markdown before the agent opens a file of its own,
with `TASKS.md` alone at 149 KB.

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

Auto-merge for documentation-only changes **has since been decided**, owner
2026-08-21, and it is narrower than "documentation":

It is expressed as an **allowlist** rather than as `docs/` minus exclusions, and
that direction is deliberate: a list of what is permitted fails closed when
somebody adds a directory, a list of what is forbidden fails open. A rule holding
unattended write access to `main` takes the one that fails closed.

| | May be merged unattended | By what |
|---|---|---|
| `docs/architecture/` `docs/community/` `docs/hardware/` `docs/mobile/` `docs/node/` `docs/research/` `docs/testing/` `docs/ui/` `docs/upstream/` `STATUS.md` `TASKS.md` | yes | the backstop routine, under the conditions in [its prompt](attadipa-backstop-routine.md). `STATUS.md` and `TASKS.md` are on the list because CLAUDE.md *requires* them in the same commit — excluding them would disqualify every compliant pull request |
| `docs/master-prompt-final.md` and the two superseded prompts | **no** | the specification in force. A process that can edit the requirements it is judged against is not a process |
| `docs/research/OWNER_DECISIONS.md` | **no** | *"not ours to overturn"*, in the file's own words. The one file in `docs/research/` that records authority rather than findings |
| `docs/adr/` | **no** | decisions of record. ADR-0003 is what stands between this project and assuming a T-Watch has a LoRa transceiver |
| `docs/automation/` | **no** | that directory governs the automation, including the backstop's own instructions. A gate that can widen itself is not a gate |
| `docs/index.html` `docs/404.html` `docs/assets/` `docs/brand/` `docs/robots.txt` `docs/sitemap.xml` `docs/manifest.webmanifest` | **no** | GitHub Pages is served from `/docs` and there is no build or deploy workflow at all, so a merge here is a live publication with nothing between it and the public but this rule. `docs/brand/` is an identity decision as well, and identity is the owner's |
| `core/` `platform/` `link/` `apps/` `sim/` `boards/` `.github/` | **no** | the rule above stands. Green CI proves nothing about a board |
| anything added to this repository after this table was written | **no** | that is what an allowlist is for |

The backstop does not form an opinion about a change. It merges only where the
independent reviewer has already published `ai-review:pass`, every check is
green, no review thread or Codex comment is outstanding, and the **head commit**
is over six hours old — each of which is a decision taken by something other
than the backstop. The head commit and not the pull request's `updatedAt`: that
condition exists to show no session is still pushing, and a label or a bot
comment bumps `updatedAt` without a line of code arriving. Three per run, and a comment on each naming
what was checked.

Dependabot is still not auto-merged; that remains a separate decision and is
still not made here.

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
