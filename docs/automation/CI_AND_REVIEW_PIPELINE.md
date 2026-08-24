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
        │     simulator                     both geometries     │
        │     evidence summary              what was NOT proved │
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
| **simulator** | that LVGL still renders at 240×240 and 410×502 | one runner + SDL2 |
| **CodeQL** | patterns a compiler does not look for | weekly plus per push |

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
| ESP32-S3 firmware build | `NOT EXECUTED` — the ESP-IDF version is undecided (`TASKS.md` T-004) |
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

## The convergence rule

**A re-review is a fresh review, and that is the problem.** The reviewer reads
the head commit. Answer every finding, push, and the next run reads a new head —
a different diff, and therefore different findings. On 2026-08-24 four branches
sat at rounds five, eight, ten and two: the findings shrank each round to a
`§4.1` that should read `§2.4` and a line number that had drifted three lines,
and `ai-review:blocking` never came off. The orchestrator merges once CI is
green, so those pull requests were waiting for something that could not arrive.
`main` did not move for 25 hours with 39 open pull requests
([#169](https://github.com/hleserg/Attadipa/issues/169)).

The rule, in one line:

> An open finding holds a pull request **if and only if** it is *floor*, or it
> was first raised **before** the floor round.

*Floor* is four kinds and it is not a severity rating: **a hardware fact with no
source, a `PASS` for a test that did not run on a board, an application-layer
hardware access, an architecture-boundary violation.** Those hold at any round,
however late they are found — the acceptance in #169 refuses to weaken them and
this does not. Everything else, first raised at or after the floor, is published,
marked *deferred*, and filed as a follow-up issue rather than held.

**The floor is round 4**, so rounds 1 to 3 behave exactly as this repository
always did. That is the bound #169 asked to have written down, and the number is
an owner decision rather than a default: three passes is more than the observed
diffs needed to surface everything of substance, and from round 4 the blocking
set can only shrink.

Two things make it converge rather than merely look as though it does:

- **A deferred finding never ages into a blocker.** #169's own phrasing —
  *"block on a finding it already raised and the push did not fix"* — does not
  converge on its own, because each round's new prose defect is next round's
  carry-over, and the queue is unbounded again. The clause is *before the floor*,
  not *before this round*.
- **Dating is not the reviewer's.** The round a finding was first seen comes from
  a ledger comment the workflow keeps, so a finding cannot be re-dated into
  blocking or out of it. The reviewer supplies findings; the rule supplies the
  verdict.

That rule is [`.github/scripts/review-verdict.sh`](../../.github/scripts/review-verdict.sh),
with 87 assertions in `.github/tests/review-verdict-test.sh` behind it, proven to
fail against six deliberate defects including the literal reading of #169 above.
It is executable rather than a paragraph in the prompt for the same reason the
intake gate is a script: a rule that has never been run against a hostile input
is a hypothesis, and here the input is written by a model.

Every unreadable input is read in the direction that blocks — an unknown kind is
`floor`, an unknown state is `open`, a finding the reviewer did not mention stays
open, a corrupt ledger restarts at round 1. A review that publishes no findings
block gets no computed verdict at all: the label the reviewer set itself stands,
and the ledger comment says so.

Three cases where that sentence was true of every ambiguity the rule enumerates
and false of the ones it did not, found in review and now closed. **A block whose
lines could not be parsed** is not a block that said nothing: it used to compute
`nothing-holding` and strip the reviewer's own `ai-review:blocking`, so two
`floor` findings written as markdown bullets read as a pass. It is `unknown`.
**A `-->` inside a finding's title** closes the HTML comment, so the block ends
there and everything below it is outside — silently, because the line was never
printed and nothing counted it. The truncation is now counted, which makes the
round `unknown` rather than a pass over findings nobody saw. And **the round
counter no longer advances on a round that did not review**: upstream `ran` means
only that the action wrote an execution log, and a run killed by a turn ceiling
used to spend floor budget having reviewed nothing.

**Retiring a finding that will not close.** A finding closes when a later round
names the same id and marks it `fixed`, and ids come from the model, so a drifted
id — `gnss-trust` in round 2, `gnss-trust-source` in round 3 — leaves the original
open with nothing able to close it. Before the floor round that costs a round;
after it, the branch blocks indefinitely. The remedy is deliberate and it is a
person's: **edit the ledger comment's state block and set the stale id to
`fixed`**, in the same edit naming why in the comment body, so the next reader
sees a retirement rather than a mismatch. An agent must not do this to its own
pull request — it is the one place where the thing being judged could clear its
own judgement — so it belongs to the owner or to an orchestrator session acting
on a branch that is not its own. The convergence claim rests on id stability, and
this is what to do on the day that fails.

**Not deployed yet.** The rule and its tests are on `main`; the workflow half is
`docs/automation/pending/169-review-convergence.patch`, because a GitHub App
cannot push `.github/workflows/`. Until a local session applies it, every round
is still a first round. See
[the note beside the patch](pending/169-review-convergence.md).

**When it cannot run, neither label is set and it says so on the pull request.**
That note is the only signal that `main`'s second protection is absent for a
commit, so it reads the action's own execution log and quotes the result record —
`is_error`, the subtype, the turn count, the cost. It used to offer two candidate
causes instead, and on 2026-08-22 the real cause was a third: the review had run,
returned `is_error: false`, and been cut off at turn 50 by a 40-turn ceiling. The
note also carries the head SHA in its HTML marker, because matching on the marker
alone posted it once per pull request *ever* — so a stale note from an old commit
sat there while every push after it failed in silence.

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
