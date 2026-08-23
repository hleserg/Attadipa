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
work out.

The failure mode being designed against is *`CI failed → change something →
rerun`*, which is a random walk with a budget attached. The attempt marker is
written **before** the attempt for the same reason: a counter that only
increments on success counts to two forever.

### And the way back out of it

Giving up raises `ci:failed` + `agent:blocked`, and **`agent:blocked` is a
named hold** in [`merge-candidate.sh`](../../.github/scripts/merge-candidate.sh)
and one of the labels the unattended backstop requires absent. Until #129 the
reset command cleared only the attempt *counter* — which the gate reads on the
**next** CI failure and nothing reads at the moment somebody types it — so a
pull request whose cause had been found, whose fix had been pushed, whose CI was
green and whose review had passed carried the escalation for ever, and nothing
said why.

So the command now clears the labels too, and it is deliberately a command
rather than any comment:

| | |
|---|---|
| **who** | a non-bot actor with `write`, `maintain` or `admin` |
| **where** | on the pull request, as a comment |
| **what** | `/ci-repair reset` **as a whole line**, on its own or above an explanation |
| **does** | removes `agent:blocked` and `ci:failed`, and says on the pull request what it removed |
| **does not** | touch `needs-owner`, touch the attempt counter, or merge anything |

The whole-line rule is not fussiness. The give-up comment above *names* the
command while telling you to use it, so the command's own spelling sits in a bot
comment on every escalated pull request — and in this file, and in any human
comment explaining the loop. That is the same collision
[`intake-decision.sh`](../../.github/scripts/intake-decision.sh) describes being
sprung twice in one day on `@claude`.

**A plain `@claude` still clears nothing on a pull request**, and that is the
other half of the same rule: `claude-agent.yml`'s claim step deliberately does
not strip `agent:blocked` from one, so that commenting cannot dissolve an
escalation raised for a person and hand the branch to the unattended backstop.
Both directions are asserted in
[`blocked-restart-test.sh`](../../.github/tests/blocked-restart-test.sh), which
runs the reset step itself and then asks `merge-candidate.sh` whether the pull
request it left behind would merge.

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
