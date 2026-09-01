# When the automation misbehaves

Everything here is meant to be done by one person, quickly, without reading the
rest of this directory first.

> **Blocked on a credential?** Use the owner's `gh` login for repository
> settings and secrets. The recovery commands below name the required state;
> historical first-install handoff instructions live in Git history.

## Stop all Anthropic spending, now

```bash
gh variable set CLAUDE_AUTOMATION_ENABLED --body false
```

Every workflow checks this before any billable step. Ordinary CI keeps running,
so pull requests still get built and tested — only the agent stops.

Resume with:

```bash
gh variable set CLAUDE_AUTOMATION_ENABLED --body true
```

An unset variable reads as **enabled**, so a fresh clone of this repository is
not silently inert.

## Stop a single workflow

```bash
gh workflow disable claude-agent.yml
gh workflow disable claude-pr-review.yml
gh workflow disable claude-ci-repair.yml
gh workflow disable agent-queue-watchdog.yml
```

`gh workflow enable <name>` puts it back. Use this rather than the variable when
one workflow is misbehaving and the others are fine.

## Cancel something that is running

```bash
gh run list --limit 10
gh run cancel <run-id>
```

Cancelling a *writer* mid-run can leave a branch half-pushed and a repository
claim behind. The watchdog releases a claim two hours after the annotated tag's
timestamp **and only once the run behind the holder id has finished** — a holder
is `agent-<run_id>-<attempt>`, and that run's status is the only evidence this
repository has that the writer stopped. A **local** lease (`writer-start.sh`,
holder `agent-i254-r1` and the like) has no such run and is never reaped: end it
with `writer-start.sh finish`, or break it by hand. Do not remove only
`agent:working`: that would hide a live lock. Inspect or deliberately break it
with the shared rule:

```bash
bash .github/scripts/claim.sh owner OWNER/REPO <n>
bash .github/scripts/claim.sh reap OWNER/REPO <n> 7200
# trusted manual recovery only:
bash .github/scripts/claim.sh break OWNER/REPO <n>
```

## Common situations

### An issue is stuck on `agent:working`

The run died, was cancelled, or is still live. The watchdog clears only a claim
that is at least two hours old *and* whose Actions run has completed; a local
lease stays until someone runs `claim.sh break`, and the watchdog log names that
command. A break that could not delete the ref reports failure and leaves
`agent:working` on, so a stuck queue always has a visible owner.
`/ci-repair reset` also breaks the PR claim after its existing
collaborator/whole-command checks. The agent's branch,
if it made one, remains — `git branch -r | grep claude/`.

### The queue is full or in incident mode

The queue has a width. **At the width**, no new ordinary writer starts
(`full`); **above it**, the watchdog dispatches nothing and automation is in
drain/recovery mode (`incident`). Existing PR repair remains admissible; a gate
repair may be labelled `queue:emergency`. Parked work does not spend a slot and
must not be resumed until admission returns `ok`. If the count is `unknown`,
treat it exactly like a closed gate and repair the diagnostic before doing
product work.

The width is the repository variable `ATTADIPA_WIP_LIMIT` and **defaults to
two**, so unset means `full` at two and `incident` at three. Read it, lift it,
and put it back with:

```bash
gh variable get ATTADIPA_WIP_LIMIT        # "was not found", exit 1: the default, 2
gh variable set ATTADIPA_WIP_LIMIT --body 4
gh variable delete ATTADIPA_WIP_LIMIT     # back to the designed width
```

Lifting it is an owner decision and a temporary one: the width is what stops a
queue nobody is watching from turning into thirteen open pull requests, and the
number to put back is `2`. `writer-start.sh` reads the same variable, so a local
run and a workflow run agree — and when it refuses it names the width it refused
under (`held: full (width 4)`), which is what makes that agreement checkable
rather than promised. A lookup the token is not permitted to make says so on
stderr; a variable nobody set is the ordinary case and stays quiet. Anything that
is not a one- or two-digit number — including a value nobody set — is read as
two; the variable cannot be used to remove the limit, only to move it.

**A width of `0` is a freeze, and it reads as `incident` above.** Zero means
"admit nothing", so the state is `full` only while the queue is empty and any
open pull request puts the count above the width. That is a correct refusal and
the wrong word: the operator-facing line says `QUEUE FROZEN` and names the
variable, precisely so nobody arrives at the drain/recovery paragraph above for a
queue the owner closed on purpose. A freeze is lifted by setting or deleting
`ATTADIPA_WIP_LIMIT`, never by draining.

### The same pull request is being repaired over and over

It cannot be: two attempts per problem chain, and the marker is written before
each attempt. If you are seeing more, somebody has been commenting
`/ci-repair reset`. Check the pull request's comments.

### Claude is not reacting to an issue

In order of likelihood:

1. **No credential.** Look for a `BLOCKED` comment on the issue naming the
   missing secret. That is the workflow telling you, once, and exiting green.
2. **The kill switch is off.** `gh variable get CLAUDE_AUTOMATION_ENABLED`.
3. **The actor is not trusted.** The gate requires `write` or better. Look at
   the run's log — it says `actor X has permission 'read'`.
4. **The issue already has a state label.** `agent:working`, `agent:review`
   and `agent:blocked` all suppress a second pickup. Comment
   `@claude` to override deliberately.
5. **The marker is missing.** An issue needs `attadipa-agent-task` *and*
   `@claude` in the body, or the `agent:ready` label.
6. **The queue was full.** The request was accepted and no writer started.
   This is the one case that is not a refusal: look for a comment headed
   *"Accepted, and not started yet"*, which names the admission state and
   the open pull request count. `agent:ready` is on the issue and the hourly
   watchdog starts it once a pull request merges or closes — **do not comment
   again**, a second comment buys a second billed run against the same
   admission answer. On an issue already carrying `agent:working`,
   `agent:review` or `agent:blocked` the label is not added, because the
   watchdog and the intake gate both refuse a dispatch carrying one; there,
   commenting `@claude` once the state label no longer applies is the restart.

**Look at the issue first.** If it carries a task marker and was refused anyway,
the gate has already commented on it saying which guard rejected it and which
actor it saw, and applied `needs-owner`. That comment exists so this list does
not have to be worked through by hand.

### The workflow fires but nothing happens

Look at the `gate` job's log. Every refusal writes a `::notice::` saying which
guard rejected it and why. That is what those notices are for.

### A pull request has no CI checks at all

The classic symptom of the wrong GitHub credential. GitHub does not start
workflow runs for events created with the built-in `GITHUB_TOKEN`, so a pull
request opened with it looks fine and has no checks. Fix by installing
<https://github.com/apps/claude> or by setting `ATTADIPA_AGENT_TOKEN` — see
[CLAUDE_AUTOMATION](CLAUDE_AUTOMATION.md#2-the-github-credential--and-why-it-is-not-github_token).

### An agent branch needs removing

```bash
git push origin --delete claude/issue-<n>-<slug>
```

Nothing depends on the branch once its pull request is closed.

## Doing an agent's job by hand

The loop is a convenience, not a dependency. Any task in the queue can be done
by a person: read the issue, do the work, open a pull request with
`Fixes #<number>` in the body, and set `agent:review` on the issue. Nothing in
the repository knows or cares which of those happened.

## Restoring the labels

```bash
.github/scripts/setup-labels.sh              # this repository
.github/scripts/setup-labels.sh --dry-run    # show what it would do
```

Idempotent — it creates what is missing and corrects the colour and description
of what is not. Losing a label loses no work: the issues keep their history, and
re-applying a label is enough to put a task back in the queue. The set itself is
described in [AI_TASK_PROTOCOL](AI_TASK_PROTOCOL.md#lifecycle).

A label that does not exist is a state a task cannot reach — `gh issue edit
--add-label` fails and the workflows swallow it with `|| true` — so this is worth
running after any manual tidying of the label list.
