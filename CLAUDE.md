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

## Definition of Done

Compiles for every supported target · host tests pass, hardware tests honestly
marked · no application-layer hardware access · simulator tested · reviewed at
both geometries · day and night themes checked · Child Mode considered · power
and coexistence implications considered · errors handled in human language ·
loading, empty, offline and error states exist · docs and `STATUS.md` updated.

A screen with the right elements on it is not done. Design is part of Done.

## Conventions

- **Two audiences, two languages, and the split is not negotiable.**
  - **The repository is English.** Code, comments, documentation, commit
    messages, pull request titles and bodies, GitHub issue and review comments,
    branch names. The two specification documents are the author's own and stay
    in Russian.
  - **The conversation with the owner is Russian.** Every chat reply, question,
    status report, explanation and apology. Including the one-word ones —
    *"готово"*, not *"done"*. Including thinking that is shown to him.

  The owner asked for this repeatedly and had to ask again in anger, which is
  the failure this line exists to stop. Drifting back into English mid-session
  is the common way it happens: an agent writes an English commit message, then
  keeps going in English into the chat. Repository text and conversation are
  different artefacts with different readers; the language follows the reader,
  not the paragraph before it.

  The only exception is the owner writing in English and asking for an English
  answer.
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
