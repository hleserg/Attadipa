# Working on Attadipa

## Read first

- [`docs/master-prompt-final.md`](docs/master-prompt-final.md) — **the
  specification in force.** Owner-supplied, 2026-08-21. Product requirements
  there are binding. Technical claims there are **not**, and it says so itself
  in §1: *"Technical claims are not automatically facts, including claims in
  this file."* The board survey has already contradicted several.
- [`docs/master-prompt.md`](docs/master-prompt.md) and
  [`docs/development-addendum.md`](docs/development-addendum.md) — **superseded
  history.** Kept because ADRs quote them. Do not fix anything in them, and do
  not take a requirement from them without checking the final prompt first.
  Section numbers in the two do not correspond: a bare "§NN" in older text
  means the old master prompt, and newer text says *final §NN*.
- [`docs/research/OWNER_DECISIONS.md`](docs/research/OWNER_DECISIONS.md) —
  decisions the owner gave us directly, which neither document contains and
  which are not ours to overturn.
- [`STATUS.md`](STATUS.md) — where things actually are.
- [`TASKS.md`](TASKS.md) — what to pick up.

There is a matching `esp-idf-firmware` skill covering ESP-IDF mechanics,
bring-up order and the hardware-verification discipline. Use it rather than
rediscovering the same things.

## The rule that outranks the rest: never trust, verify

Never write code that depends on a hardware fact you have not traced to a
datasheet, a schematic for the specific board revision, or vendor source.

The consequences here are not hypothetical. On these boards:

- the T-Watch ships with **one of five** radio chips and **one of two** GNSS
  modules — the product name does not tell you which. **Two of the five are not
  LoRa transceivers at all**, and of the remaining three the pinned MeshCore
  revision supports exactly one. "It has a radio" and "it can join the mesh" are
  different sentences ([ADR-0003](docs/adr/0003-radio-not-lora.md));
- its GNSS power rail differs between revisions, so the wrong guess means GNSS
  silently never starts;
- the Waveshare board has **no LoRa and no GNSS at all**;
- neither board has a magnetometer;
- the Waveshare vendor BSP does not drive the IMU, PMU or RTC that are on the
  board — "vendor supported" is not "handled".

Record every fact in `docs/research/`. A fact that lives only in a chat log
does not exist.

If you cannot establish something, write a blocker — do not code past it:

```
BLOCKED:
Reason:
Evidence:
Impact:
Possible options:
Recommended next action:
```

## Mock is not hardware

Never write `PASS` for a test that did not run on a physical board. Write
`NOT EXECUTED — HARDWARE REQUIRED`. Label power and timing numbers `MEASURED`,
`ESTIMATED` or `UNKNOWN`, and never let an estimate read as a measurement.

## Never irreversible without being asked

No eFuse burning, no irreversible secure boot or flash encryption, no flashing
a physical device, no production secrets, no destroying keys. Preparing the
config, writing the instructions and generating dev keys are all fine. Never
commit private keys.

## Architecture in one paragraph

Applications ask what the device can do, never which device it is. Board
differences live in `boards/` and `platform/`; `#ifdef BOARD_X` must not appear
in `core/` or `apps/`. Differences that are **not** the board's — a capability
supplied by an attached Attadipa node — live in the provider registry beside the
BSP, because no BSP can know at build time what will be plugged in later. Same
rule, one more source: nothing above the capability registry learns where an
answer came from. An application never learns which GPIO powers the GNSS
module, which SPI the radio is on, or that the haptic driver sits behind a PMU
rail. Because the two boards share almost nothing but the SoC and the PMU, this
is the only thing keeping one codebase viable.

Every part on the board gets a seat in the core — including the ones no
application uses yet, and the ones the vendor's own BSP ignores. So does every
capability that reaches the device *without* being on the board: an Attadipa node
supplies LoRa and GNSS to a watch that has neither, and those need an owner,
a power story and a state to be in when the node walks away.

## Reuse before writing

Check [`docs/research/REUSE_LEDGER.md`](docs/research/REUSE_LEDGER.md) before
implementing anything non-trivial. Several open-source firmwares already target
these exact boards. Record the decision either way — "we wrote our own" is
allowed, undocumented is not. Check the license before depending on anything.

## The agent queue

Work arrives as a **GitHub issue**, and that issue is the canonical task —
[`docs/automation/AI_TASK_PROTOCOL.md`](docs/automation/AI_TASK_PROTOCOL.md).
The owner does not carry prompts between agents, does not turn review
paragraphs into issues by hand, and does not remind anybody to check CI.

If you are working from an issue:

- **Read before writing.** The issue and its comments, this file, the
  specification, `STATUS.md`, `TASKS.md`, the ADRs it touches, and the reuse
  ledger. Then check the open issues and pull requests — solving the same
  finding twice is the failure the queue exists to prevent.
- **Research tasks do not produce implementations.** `next-task-research`,
  `upstream-intelligence` and `readiness-audit` verify sources and write to
  `docs/research/`. A research task that arrives as a pull request full of new
  subsystems has been guessed at, not done.
- **One writer.** Reading, reviewing and analysing in parallel is free; two
  agents editing one branch is a merge conflict with a robot on both ends.
- **A branch and a pull request**, never a push to `main`. Open it as a draft
  while it is still moving; mark it ready when it is not. **The orchestrator
  merges it once CI is green** — owner decision 2026-08-21, replacing the
  earlier rule that merging was the owner's. The owner reviews after the fact
  and reverts anything they disagree with; `main` is protected by CI and the
  independent reviewer, not by a person waiting. The body
  carries `Fixes #<issue>` and says what was tested and what was not.

  That is the *orchestrator* — a live session, over every path in the
  repository. Two unattended things merge far less, and both stay inside the
  same allowlist: the daily **backstop routine**, and the half-hourly
  [`pr-merge-sweep.yml`](.github/workflows/pr-merge-sweep.yml), which exists so
  that a finished documentation pull request does not wait on somebody
  remembering. `docs/` only — never `docs/automation/`, never `docs/adr/`,
  never `OWNER_DECISIONS.md`, never the live Pages files — three per run, under
  conditions each checks rather than infers, and only where the reviewer's
  `ai-review:pass` was set **on the head commit**. So a green pull request
  touching `core/` or `.github/` is not waiting for the owner, and neither of
  those will sweep it up — it is waiting for an orchestrator session to look at
  it. Widening that list is the owner's decision, not an agent's and not a
  reviewer's.

  **The half-hourly sweep merges nothing, from this change until the parked
  patch lands.** Its caller cannot prove it read the whole pull request — a
  GraphQL connection is a page, not a set — so the rule refuses it by arity and
  every run holds. The fix is parked as
  `docs/automation/pending/170-merge-sweep-completeness.patch`, named here as a
  code span rather than linked because the procedure that applies it deletes
  the file, and a link to a deleted file reddens `main`. Until it lands, the
  daily backstop routine is the only thing merging unattended, so a finished
  documentation pull request waits a day rather than half an hour.
  [`CLAUDE_AUTOMATION.md`](docs/automation/CLAUDE_AUTOMATION.md) is the long
  version.
- **Hardware facts are verified or they are `UNKNOWN`.** Never a `PASS` for a
  test that did not run on a board — the rule above, and it does not relax
  because a workflow is watching.
- **CI failing is yours.** Read the actual log and fix the cause. Changing
  things until it goes green is a random walk with a budget attached.
- **Blocked is a real outcome.** Use the format above, add `needs-owner` or
  `needs-hardware`, and leave the owner one concrete action rather than the job
  of reconstructing your reasoning. Do not ask what the specification,
  `STATUS.md`, `TASKS.md`, an ADR or the issue already answers.
- **Update `STATUS.md` and `TASKS.md`** in the same commit as the change they
  describe, so the next agent can continue without this conversation.

`TASKS.md` stays the roadmap; issues are the executable work packages. They
reference each other and neither is a copy of the other.

Everything about the workflows themselves — security model, authentication,
cost control and the kill switch — is in
[`docs/automation/`](docs/automation/CLAUDE_AUTOMATION.md).

## The queue has a width, and it is two

> **Normal PR WIP limit: 2. Hard temporary limit: 3. At 3 open working pull
> requests, finish, merge or close one before opening another. 4+ open pull
> requests is a queue incident and blocks new feature work.**

Owner decision **OD-23**, 2026-08-24, and it is not a style preference. On
2026-08-24 this repository had **35 pull requests open at once**. Every one of
them edited `TASKS.md` and `STATUS.md`, so each merge re-conflicted all the
others; every re-resolution was a push, every push bought another independent
review, and reviews reached *round sixteen* on a single branch. The queue was
consuming more of the budget than the development it existed to serve, and
branches were arriving at review contradicting hardware facts that had been
measured while they waited. **A pull request is a short gate into `main`, not
long-term storage for unfinished work.** Thirty-five gates open at once is not
a busy project; it is a development system that has stopped working.

**Before opening a pull request, count the open ones.** `.github/scripts/wip-limit.sh`
is the same count the guard uses, and `bash .github/scripts/wip-limit.sh --count`
prints it.

- **0–1 open** — start the next task.
- **2 open** — open a third only if one of the two is essentially finished
  (waiting on CI, or on a mechanical rebase), or the new one is genuinely a
  short independent step that cannot conflict with them. Finishing one of the
  two is still the better move.
- **3 open** — **stop starting.** Drive one to `MERGED`, `CLOSED AS STALE`,
  `SUPERSEDED`, or back to an issue with no open pull request, and only then
  open the next.
- **4 or more** — **queue incident.** No new feature, research or meta work
  until the queue is back to two or three. Triage what is open into: merge now
  or after a mechanical update · fix once · close as stale or superseded ·
  external blocker, returned to an issue.

**What counts.** Anything that will need work or a merge. A draft counts as
soon as it carries real content, conflicts with `main`, or is being worked on —
draft is not a way to hold ten branches open. Only two things are exempt, and
both must say so on the pull request: `queue:parked` for work deliberately held
with the owner's agreement, and `queue:emergency` for the four cases that may
be opened over the limit — a security fix, data loss, CI or merge
infrastructure that is fully broken, and a critical regression in `main`. An
exemption label with no stated reason is not an exemption.

**A pull request is short-lived.** Hours, ideally one working session, at the
outside about a day for something genuinely hard. Past a day without a named
external reason it is a triage candidate: finish it, cut its scope, split it,
close it as stale, or return the unfinished part to an issue. Surviving a
dozen changes to `main` is not normal and must not be treated as normal.

**Research does not take a slot.** Findings belong in the issue, a comment, or
`docs/research/` on a branch that is not yet proposed. Open the pull request
when the change is ready to go into `main` — never as a container for work in
progress.

**Findings are collected, not drip-fed.** Gather every substantive finding on a
pull request, fix them in one pass, and go back for one final review. Review →
one fix → review → one fix, round after round, is how a branch reaches round
sixteen. After two or three rounds leave only small or arguable points, the
decision is merge, follow-up issue, or close — not another round.

**Sunk cost is not a reason to keep a branch open.** Close it when `main` has
moved past it, when another pull request has already solved most of it, when
the hardware facts it rests on have since been measured, when reconciling it
with `main` is now larger than the change it was opened for, or when re-doing
it small would cost less than the next review. Reopen the useful remainder as
an issue.

**`STATUS.md` and `TASKS.md` must not hold a branch open.** They are the two
files every branch touches, so they serialise work that is otherwise
independent. When the code is ready and only those two conflict: take `main`'s
version, add this pull request's own result to it, and merge. Do not preserve
the branch's historical wording, and do not let a documentation conflict
reopen a review of code that has not changed.

`docs/research/OWNER_DECISIONS.md` is a third such file, and its conflict is
not the same kind. The other two collect prose, so appending both sides loses
nothing. This one is **numbered**, and every branch that appends an OD appends
it at the same offset against the same last number — so two branches opened on
the same day both become OD-17, and blind-appending them merges cleanly into a
document with two of it. Resolve it by **re-numbering against `main`**, never by
appending: read what `main`'s last OD is at the moment you merge, take the next
number, and correct every reference to it in the branch. The same holds for
`TASKS.md`'s T-numbers.

**Blocked is not parked.** Work that cannot move because it needs the owner's
hands, a delivered part, an external credential, a product decision or a
measurement nobody can take today does not sit as an open pull request for
weeks. Close it, record the state in an issue with the `BLOCKED` block, and
open a fresh pull request from a current `main` when the blocker lifts.

## Look at the screen you changed

A UI change that compiles has not been checked. `tools/watch_control.py` takes a
picture of what is actually on the screen, presses buttons, taps and swipes, and
takes another picture — against the simulator today and against a device when
there is firmware for one.

**After a substantive change to screens, navigation, themes, fonts, widgets,
system dialogs, lock, touch handling or buttons: build it, open the screen, take
a real screenshot, and look at the image.** Check for clipped text, overlaps,
collapsed padding, unreadable contrast, wrong colours, corrupted regions,
animations caught halfway, touch targets that are invisible or too small, a tap
that fires twice, and a UI that is wedged after a series of actions. Fix what
you find and repeat.

Not after every keystroke — at logical checkpoints and before finishing a task
that touched the interface. And **never compute tap coordinates from the source
and fire at them**: that tests your arithmetic. Look at the frame first, find the
element in it, then tap.

Two rules do not relax here. A screenshot from the simulator is evidence about
layout, colour and geometry and about **nothing physical** — the tool prints
`sim` in its build string so that cannot be claimed by accident. And "it built"
is not "it works": *"Compiles"* is the first item in the Definition of Done, not
the last.

[`.claude/skills/watch-ui-testing/SKILL.md`](.claude/skills/watch-ui-testing/SKILL.md)
is the procedure. [`docs/testing/WATCH_CONTROL.md`](docs/testing/WATCH_CONTROL.md)
is the longer version, including what the feature costs and how it works.

## Definition of Done

Compiles for every supported target · host tests pass, hardware tests honestly
marked · no application-layer hardware access · simulator tested · **a real
screenshot taken and looked at, if the interface changed** · reviewed at both
geometries · day and night themes checked · Child Mode considered · power
and coexistence implications considered · errors handled in human language ·
loading, empty, offline and error states exist · docs and `STATUS.md` updated.

A screen with the right elements on it is not done. Design is part of Done.

## Conventions

- Code, comments, and documentation in English. The two specification documents
  are the author's own and stay in Russian.
- **The README exists twice, and the two are one document.** `README.md` is the
  English original and [`README.ru.md`](README.ru.md) is its Russian version.
  Any change to one is made in the other **in the same commit** — not "later",
  not in a follow-up task. A README that is current in one language and stale in
  the other is worse than one language alone, because a reader has no way to
  tell which of the two they are looking at. This applies to every edit, down to
  a corrected link or a changed number; if a change genuinely has no counterpart
  (an English-only typo, say), say so in the commit message rather than leaving
  it to be guessed. The pair of language links at the top of each file is part
  of the contract: keep both pointing at each other.
- Small logical commits, clear messages. No destructive git operations, no
  rewriting published history, no pushing anywhere that was not agreed.
- Leave the repository in a state where the next person — or the next agent —
  can continue. That is a deliverable, not a courtesy.
