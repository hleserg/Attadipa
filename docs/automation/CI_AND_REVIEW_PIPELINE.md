# CI, and the review that follows it

What runs, what each result actually proves, and what none of it proves.

## The pipeline

```
push / pull request
        │
        ├── CI ─────────────────────────────────────────────────┐
        │     host build + tests            GCC, Release        │
        │     strict warnings               GCC, -Werror        │
        │     clang build + tests           second front end    │
        │     sanitizers                    ASan + UBSan        │
        │     host coverage                 artifact, not gate  │
        │     workflow lint                 actionlint          │
        │     documentation consistency    repository contract │
        │     simulator                     both geometries     │
        │     firmware build               flash + PURE_RAM    │
        │     evidence summary              what was NOT proved │
        │              │                                         │
        │              └── Required CI      stable merge gate   │
        │                                                        │
        ├── CodeQL             security-and-quality, host build │
        │                                                        │
        └── Claude PR review   independent, read-only ───────────┘
                  │
        CI failed on a claude/* branch?
                  └── Claude CI repair, at most twice
```

## The jobs, and why each exists

| Job | Catches | Cost |
|---|---|---|
| **host build and tests** | the obvious | one runner |
| **strict warnings** | shadowing, narrowing conversions, sign conversions, old-style casts | one runner |
| **clang** | what GCC does not diagnose, and non-conforming code GCC accepts | one runner + apt |
| **sanitizers** | out-of-bounds, use-after-free, leaks, undefined behaviour | one runner |
| **coverage** | which of the logic has never been executed by a test | one runner |
| **workflow lint** | a typo in the automation that decides who may drive an agent | seconds |
| **documentation consistency** | broken links, citations and generated-site contracts | seconds |
| **simulator** | that LVGL still renders at 240×240 and 410×502 | one runner + SDL2 |
| **firmware build** | ESP32-S3 flash and PURE_RAM builds, ELF linkage and partition safety | one pinned ESP-IDF runner |
| **CodeQL** | patterns a compiler does not look for | weekly plus per push |

`Required CI` is the single ordinary status check required by the `main`
ruleset. It runs after the eight mandatory CI jobs above (coverage and the
evidence summary are deliberately optional) and fails unless every dependency
finished with `success`; failure, cancellation and an unexpected skip all fail
closed. CodeQL remains a separate native code-scanning requirement in the
ruleset.

The same active ruleset requires a pull request, blocks deletion and force
pushes, requires no human approval, grants no standing bypass, and does not
require a branch to be updated after an unrelated change lands on `main`.

CodeQL is the one of these whose output needs a person. Every alert it
raises is either fixed or written down in
[`CODE_SCANNING.md`](CODE_SCANNING.md) with the reason it does not apply —
there is no third option where it is dismissed in the web UI and the
reasoning lives nowhere.

Three notes on choices that could reasonably have gone the other way:

**`-Werror` is a separate job, and it is defensible only because the debt is
zero.** That was measured before the job was added, not assumed: the full set,
including `-Wshadow`, `-Wconversion`, `-Wsign-conversion` and
`-Wold-style-cast` — the ones that usually produce hundreds of warnings on an
existing codebase — produced **none**. It is a separate job so that a new
compiler release with a new warning fails one clearly named check rather than
the build everybody is waiting on.

**The simulator is not built with the strict flags.** It pulls LVGL, and holding
somebody else's vendored library to our warning set produces noise, not quality.

**Coverage is an artifact, not a gate.** A threshold enforced before anybody has
looked at what is covered produces tests written for the number.

## What a green run means

Exactly this, and the `evidence` job prints it on every run so it cannot quietly
stop being said:

| Target | Status |
|---|---|
| host build, GCC and Clang | `UNIT-TESTED` |
| strict warnings, sanitizers | `UNIT-TESTED` |
| simulator, both geometries | `SIMULATED` |
| ESP32-S3 firmware build | `COMPILED` when `firmware-build` succeeds — pinned ESP-IDF v5.5.5, flash and PURE_RAM variants |
| anything on a physical board | **`NOT EXECUTED — HARDWARE REQUIRED`** |

There is no hardware-in-the-loop runner and no fake one. When a physical check
is needed it is written up as a plan — equipment, procedure, measured quantity,
pass and fail criteria — in `docs/testing/HIL_PLANS.md`, so that somebody with a
board can execute it and record a real result.

## The independent review

`claude-pr-review.yml` runs in a context that did not write the code, because an
author reviewing their own work re-derives the same assumptions that produced it.

It cannot push: `contents: read`. It has an opinion and no hands.

The question it is asked is deliberately not "does this compile":

> Assume it compiles and every existing test is green. **How can it still break
> Attadipa?**

and the checklist is the one this project has actually been bitten by —
architecture boundaries, lifetime and memory, concurrency and ISR context,
power state and wake sources, crash-safe persistence and migrations, protocol
framing and backpressure, GNSS fix age and trust, offline and degraded modes,
Child Mode, localisation, and — the recurring one — a hardware claim with no
measurement behind it.

It finishes by setting exactly one of `ai-review:pass` or `ai-review:blocking`,
and it is asked to say plainly when it found nothing. A reviewer that always
finds something is noise, and noise is how a review stops being read.

The action's step outcome is not the publication record. `--max-turns` is
checked after a session can already have posted its sticky comment, so a fresh
trusted reviewer comment remains authoritative even if the action subsequently
reports `failure`. The workflow emits a warning and keeps that verdict. Only a
failure with no fresh reviewer comment enters the no-review diagnostic path.

**When it cannot run, neither label is set and it says so on the pull request.**
That note is the only signal that `main`'s second protection is absent for a
commit, so it reads the action's own execution log and quotes the result record —
`is_error`, the subtype, the turn count, the cost. It used to offer two candidate
causes instead, and on 2026-08-22 the real cause was a third: the review had run,
returned `is_error: false`, and been cut off at turn 50 by a 40-turn ceiling. The
note also carries the head SHA in its HTML marker, because matching on the marker
alone posted it once per pull request *ever* — so a stale note from an old commit
sat there while every push after it failed in silence.

**A silent review invalidates the previous head's verdict**, and the order in
which it does the two halves of that is the whole of the guard. A review that
reached the model and published nothing leaves the pull request carrying whatever
`ai-review:pass` the *previous* head earned, which nothing has said anything
about this one. So the labels come off first and the note goes out second:
`.github/scripts/review-invalidate.sh` strips both, treats a failed removal as an
explicit failure, and demotes a failed `gh pr comment` or a failed dedupe read to
a warning. A pull request that lost a stale verdict and did not get a note is one
nothing can wrongly merge; the reverse is not true, and for a while it was what
happened — the removals used to come last, after two network calls, under
`set -euo pipefail`, so a 502 on either left the stale `ai-review:pass` in place
(#240). The workflow now calls the helper in that order; the previously parked
caller patch was applied during the queue recovery and removed. The executable
workflow test still extracts both shipping steps and proves that notification
failure cannot preserve the previous head's verdict.

**And then the check goes red.** The step used to exit with the helper's status,
so a *successful* invalidation of a silent review ended green: the check said
`success`, `mergeStateStatus` said `CLEAN`, neither verdict label was present,
and the only signal that said otherwise was the body of a comment. Four cheap
signals out of five read as mergeable, and the fifth was an absence — which is
the thing the note exists to say is not a pass. `merge-candidate.sh` held on that
absence, so the automated path was safe; a hand merge was not (#339). The silence
is now what the exit status carries, and the helper's own failure stays legible
beside it as a second `::error::`: "nothing was published" and "and the stale
label is still there" are different sentences.

**The stale pass comes off when the new head arrives, not when the run ends.**
The invalidation above is correct and late: it runs at publish time, so between a
push and the end of the next review — thirteen minutes on a busy pull request —
the label still says `ai-review:pass` about the *previous* head, while only
`mergeStateStatus` and a pending check disagree. Anything that gates on the label
alone merges an unreviewed head. So a step before the credential gate strips
`ai-review:pass` on every `synchronize`. Only that one: a stale `ai-review:pass`
releases a merge and a stale `ai-review:blocking` holds one, and a review that
has not run yet has said nothing that justifies releasing somebody else's hold.

### How many rounds a review may hold a pull request

The review reads the head commit, so answering every finding produces a new head
and therefore a new review. Left unbounded that does not terminate: each round's
own fix is the next round's subject. Two rules bound it, both in
`.github/scripts/review-verdict.sh`, and the ledger comment on the pull request
states both every round.

**The floor is round 4.** From it on, an open finding holds the merge only if it
is *floor*-category — a hardware fact with no source, a `PASS` for a test that
did not run on a board, an application-layer hardware access, or an
architecture-boundary violation — or if it was first raised before round 4 and
the pushes since did not fix it. Everything else is published, marked deferred,
and filed as a follow-up issue instead of held. The round a finding was first
seen is read from the ledger the script itself wrote, never from what the
reviewer says this round, so a finding cannot be re-aged into a blocker.

**The ceiling is round 5, and round six does not run.** The floor caps which
categories may hold late; it does not cap how many rounds there can be, and #338
ran sixteen because every round's fix minted the next round's floor finding in
the same document. Past round five nothing holds at all, *floor* included — so a
sixth round could only ever return the verdict already reached, and since
2026-09-01 it is not bought. `attadipa_review_gate` reads the round out of the
ledger before the model is invoked; the `Has this review already had its five
rounds` step skips the paid step, applies `ai-review:pass` and posts a note
saying the review is over rather than that it found nothing. Findings open at
the cap stay in the ledger comment, which is where that note points.

The cap counts paid rounds two ways and takes the larger, because the ledger's
own count stops. `Converge the published reviewer verdict` is what advances
`round=`, and it is skipped whenever `review-published.sh` answers `unknown` —
which it does when the comment read behind it returns nothing. That read used
to be silenced with `2>/dev/null`, so nothing was written and nothing was said;
the run stayed green. #382 is the worked example: its ledger says `round=5`, last
edited at 15:35:45, and the reviewer published findings four more times — 16:08,
16:34, 17:26, 17:59 — with converge `skipped` on every one. Nine paid rounds, a
ledger claiming five. Frozen at five the ledger happens to cap correctly; frozen
at three it would never cap at all. So the gate is also handed a count of the
published findings blocks, which measures what is being paid for and cannot
freeze while rounds run, and it judges the larger of the two. Since #391 the
read is retried and an `unknown` fails the publication step, so a ledger this
freezes is a red check rather than a green one; the gate keeps both counts,
because a red run has still converged nothing.

A standing `ai-review:blocking` is the one thing the cap will not clear. The
invalidation step drops both labels on a push, so a pushed fix reaches the cap
with no label and gets its pass; a bare workflow re-run reaches it with the
block still on and leaves it there, because a verdict cleared with no commit in
between is not a verdict.

That is OD-25, and the number is an owner decision —
`docs/research/OWNER_DECISIONS.md` is where it changes, not this file.

All of it is asserted offline in `.github/tests/review-verdict-test.sh`: two
files and three numbers in, `key=value` lines out.

### When the review publishes no verdict

The pull-request note links here instead of embedding a troubleshooting manual
in workflow YAML. Check, in order:

1. the actor is named explicitly in `allowed_bots` when it is a bot;
2. the action did not reach its turn or job timeout;
3. the branch contains the current default-branch `claude-*.yml` bytes;
4. the pull request is not itself changing a protected Claude workflow;
5. the Anthropic credential and quota are available;
6. the required read-only tool is present in `--allowedTools`.

The run log distinguishes the first five before model work from a missing tool
after real turns. `.github/scripts/failure-reason.sh` publishes only a
whitelisted error summary; do not expose full tool output to diagnose it.

A pull request that reaches `main` carrying `<!-- attadipa-review-did-not-run -->`
and no `ai-review:*` label has been merged on ordinary CI and a person's reading.
That is a legitimate route — it is the only route for a change to
`.github/workflows/claude-*.yml` — but it is a decision, not a default.

## Automatic CI repair

Only on a `claude/*` branch of this repository, only with an open pull request
labelled `agent:claude`, and never on `main` or a fork.

It is given the actual failing log (`gh run view --log-failed`, trimmed) and
told to find the root cause before changing anything. Two attempts per problem
chain; then `ci:failed`, `agent:blocked` and a comment saying what it could not
work out. `/ci-repair reset` clears the counter.

The failure mode being designed against is *`CI failed → change something →
rerun`*, which is a random walk with a budget attached. The attempt marker is
written **before** the attempt for the same reason: a counter that only
increments on success counts to two forever.

## Smoke tests

The workflows were exercised, not merely read. What was verified, and how:

| # | What | How |
|---|---|---|
| A | intake accepts a trusted task and applies labels | a real issue with a `attadipa-agent-task` marker |
| B | the reviewer runs on a pull request and changes no file | this pull request |
| C | repair triggers only on an agent branch | a deliberate, temporary test failure on a `claude/*` branch |
| D | the watchdog finds a task whose event was missed | an issue labelled `agent:ready` with no trigger event |
| E | an external user cannot drive a write-capable agent | the permission gate, and `actionlint`'s parse of every `if:` |

Results are recorded in the pull request that introduced these files. Where a
test could not complete because no Anthropic credential was configured, that is
stated rather than glossed: the gate, the labels and the no-op path were
exercised; the Claude step itself was not.

## One thing the reviewer cannot review

A pull request that changes the Claude workflow files themselves is skipped by
the review, with this in the log:

> Workflow validation failed. The workflow file must exist and have identical
> content to the version on the repository's default branch.

That is the action refusing to run a version of itself that a pull request has
edited — which is the right refusal, since otherwise a pull request could supply
the prompt that reviews it. The consequence is a rule rather than a bug:
**changes to `.github/workflows/claude-*.yml` are merged on ordinary CI and a
human's reading, never on an AI review.** Nothing needs configuring; it is worth
knowing so the silence is not mistaken for approval.
