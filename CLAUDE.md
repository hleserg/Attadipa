# Working on Firefly OS

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

- the T-Watch ships with **one of five** LoRa chips and **one of two** GNSS
  modules — the product name does not tell you which;
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
supplied by an attached Firefly node — live in the provider registry beside the
BSP, because no BSP can know at build time what will be plugged in later. Same
rule, one more source: nothing above the capability registry learns where an
answer came from. An application never learns which GPIO powers the GNSS
module, which SPI the radio is on, or that the haptic driver sits behind a PMU
rail. Because the two boards share almost nothing but the SoC and the PMU, this
is the only thing keeping one codebase viable.

Every part on the board gets a seat in the core — including the ones no
application uses yet, and the ones the vendor's own BSP ignores. So does every
capability that reaches the device *without* being on the board: a Firefly node
supplies LoRa and GNSS to a watch that has neither, and those need an owner,
a power story and a state to be in when the node walks away.

## Reuse before writing

Check [`docs/research/REUSE_LEDGER.md`](docs/research/REUSE_LEDGER.md) before
implementing anything non-trivial. Several open-source firmwares already target
these exact boards. Record the decision either way — "we wrote our own" is
allowed, undocumented is not. Check the license before depending on anything.

## Definition of Done

Compiles for every supported target · host tests pass, hardware tests honestly
marked · no application-layer hardware access · simulator tested · reviewed at
both geometries · day and night themes checked · Child Mode considered · power
and coexistence implications considered · errors handled in human language ·
loading, empty, offline and error states exist · docs and `STATUS.md` updated.

A screen with the right elements on it is not done. Design is part of Done.

## Conventions

- Code, comments, and documentation in English. The two specification documents
  are the author's own and stay in Russian.
- Small logical commits, clear messages. No destructive git operations, no
  rewriting published history, no pushing anywhere that was not agreed.
- Leave the repository in a state where the next person — or the next agent —
  can continue. That is a deliverable, not a courtesy.
