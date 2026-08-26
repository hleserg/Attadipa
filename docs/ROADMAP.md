# Roadmap — and the one correction that reorders it

[GitHub Issues](https://github.com/hleserg/Attadipa/issues) are the task list;
linked pull requests and checks show live status. This file only records durable
product ordering: **which of two possible next tasks wins**.

Written 2026-08-24, after an independent cold read of the repository.

> **Update 2026-08-26:** T-165 and T-166 established the physical firmware;
> T-114 connected the real screenshot/control endpoint; T-037 shipped the first
> Clock; T-068 resolved the RTC slow-clock prerequisite; and T-167 completed the
> sleep/wake lifecycle. The diagnosis below records the earlier state; GitHub
> Issues and pull requests hold live status and select the next device work.

## Where the project actually is

The engineering base is genuinely strong: a capability model with two layers and
a provider registry, host tests and a simulator, strict warnings and sanitizers
in CI, a research discipline that traces hardware claims to schematics, a debug
channel that screenshots the interface and injects input, and a queue that turns
findings into issues into branches into reviewed pull requests without a person
carrying prompts between agents.

At the time of this decision, **none of it had ever run on a watch.**

That is not an impression. It is what the tree says:

- There is **no ESP-IDF project**. No `main/` component, no
  `idf_component_register` anywhere, no `sdkconfig.defaults`, no partition
  table. Every `CMakeLists.txt` in the tree is host-native, and the root one
  says so in its own words: *"Host-native build. ESP-IDF is deliberately not
  involved here."* That was the right call for M1 and it is not a device
  target.
- **T-004** pins the ESP-IDF version and sits under `READY`, behind roughly
  fifty research tasks — while its own body records that the toolchain is
  installed and a trivial `esp32s3` build already passes. What is missing is a
  decision record, not a toolchain.
- **T-114** — the firmware end of the debug channel — states the gap without
  ambiguity: *"Dependencies: an ESP-IDF firmware project. There is none."* The
  whole vertical `agent → host tool → protocol → input → UI → framebuffer → PNG`
  is finished except the transport at the device end.
- **T-010**, board bring-up, is `BLOCKED` on the T-Watch, which is `ORDERED` and
  not in hand — and says in the same breath that **Waveshare bring-up is not
  blocked, only not done.**

So the gap is not knowledge, and it is not architecture. It is that the
infrastructure has been allowed to run ahead of the device indefinitely, and
nothing in the priority order was pulling it back.

## The correction

Not a reset. Not a lowered bar. An ordering change:

> Attadipa should stop being a well-verified architecture of a future firmware
> and become a minimal *real* watch firmware — one that can be built, flashed,
> booted, touched, seen on a screen, put to sleep, woken, and checked with this
> project's own tools.

Nothing already decided is overturned by this. No architectural decision is
reopened because the priority moved, no open question is closed by pretending,
and no branch in flight is abandoned to make room.

## Which of two tasks wins

Ask this of every task before starting it:

> When this is finished, what can a real user — or a real physical Attadipa unit
> — do that it could not do before?

If the answer is *"nothing, but the architecture is more correct"*, the task may
still be a good task. It now **loses** to one that closes part of a device
vertical slice, unless it blocks one.

Things that now need a reason why **now**, rather than a reason why at all:

- a new meta-framework, or a new layer of orchestration;
- automation added for automation's sake, rather than to stop the queue
  stalling;
- a large future subsystem several floors above a foundation that is not built;
- an ML runtime, before there is a firmware for it to run in;
- a new generic abstraction with fewer than two real consumers — or one, plus a
  second already scheduled and already shown to differ.

None of those is forbidden. Each needs an argument that is about *now*.

## M2 — the first device vertical slice

**On the name.** [`master-prompt-final.md`](master-prompt-final.md) §2703
defines M2 as *"Board Bring-Up / **Per board:** boot; display; touch; PMU
basics; input; diagnostics"* — and that document is binding, so this is **the
specification's M2 narrowed to one board, with the second still owed**, not a
replacement for it. Finishing the six items below on the Waveshare does not
finish M2: the T-Watch half remains, blocked on a board that is `ORDERED` and
not in hand (T-010). Nobody may write *"M2 complete"* off this list alone.

The shape, end to end:

```
real ESP32-S3 → Attadipa firmware → BSP → display → input → UI/app
              → debug channel → sleep/wake → a hardware test somebody can repeat
```

**One board, brought up vertically, before two boards brought up halfway.** The
board is the **Waveshare ESP32-S3-Touch-AMOLED-2.06**, and this is not a
preference — it is the only board in the building. The T-Watch S3 Plus is
`ORDERED`; the Waveshare is on the desk, its eFuses have been read
(`ESP32-S3R8`), its flash has been identified, and its schematic has been
traced. The abstraction boundaries get their real test on the *second* board,
which is the honest order: a boundary that has never had a second implementation
behind it has not been tested, only asserted.

| # | Task | What it delivers | Real blocker |
|---|---|---|---|
| 1 | **T-004** | the ESP-IDF pin as a decision, not an installation | none — it is one row in [DEPENDENCIES](research/DEPENDENCIES.md) away from done |
| 2 | **T-165** ([#189](https://github.com/hleserg/Attadipa/issues/189)) | an ESP-IDF project that builds: `main/`, `sdkconfig.defaults`, a partition table, a boot path, serial diagnostics, a reproducible build, a documented flash procedure | T-004 |
| 3 | **T-166** ([#190](https://github.com/hleserg/Attadipa/issues/190)) | the Waveshare BSP driven vertically — display, LVGL, touch, PMU, RTC, **up to the driver** | done; D21 resolved by the physical asymmetric RGB pattern |
| 4 | **T-114** ([#117](https://github.com/hleserg/Attadipa/issues/117)) | the debug channel's firmware end, so the agent's screenshot loop reaches the real panel — **and the `InputOrigin::Physical` producer for touch *and buttons*, which is T-114's alone** | done |
| 5 | **T-037** | the first Clock, running on the watch, on real input, with the real tokens and fonts | done |
| 6 | **T-167** ([#191](https://github.com/hleserg/Attadipa/issues/191)) | screen off, controlled sleep, wake, UI restored, wake reason diagnosable, and the cycle repeatable under the debug channel | done |

All six Waveshare steps above are complete. GitHub Issues select the next finite
device task; there is no automation umbrella waiting to take their place. The
old cross-branch conflict gate is gone: [#172](https://github.com/hleserg/Attadipa/issues/172)
removed the shared `TASKS.md` and `STATUS.md` ledgers that made unrelated work
collide.

**D21 was the one technical unknown on the path:** in what byte order does the
CO5300 want a 16-bit pixel on the wire. T-166 resolved the operational board
setting on 2026-08-25: the physical panel showed the intended asymmetric red,
green and blue swatches with the board profile's RGB565 byte swap enabled. The
measurement belongs to the board flush path; it says nothing about asset-file
byte order.

## What a session with the board may do, and what stays the owner's

**An earlier draft of this section said that no agent flashes a physical device,
and that every hardware step therefore ends at `NOT EXECUTED — HARDWARE
REQUIRED`. The owner struck that on 2026-08-24**, and the reason is worth keeping
with the rule: the screenshot-and-input tooling was built precisely so that a
session could run this firmware on a watch and look at the result. A milestone
whose every step stops one command short of the board defeats the tool built to
finish it.

**A session with the board physically on its desk flashes it, runs the firmware,
drives the screen and reports what happened.** Those results are `MEASURED`, and
they are the point of M2 rather than a bonus at the end of it. Two things make
that safe rather than brave, and both already exist:

- the factory image is backed up and was verified byte-for-byte (T-099), so a bad
  flash is recoverable rather than terminal;
- and there is a route that writes **nothing at all** —
  `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP` loaded over USB-Serial/JTAG, established on
  this exact unit in
  [WAVESHARE_RUNNING_OUR_CODE](research/WAVESHARE_RUNNING_OUR_CODE.md). Prefer it
  for probes, because it costs nothing to undo.

**What is still the owner's, and not as a formality.** These cannot be undone by
re-flashing, so each one waits for an explicit request every time, however
convenient it would be in the moment:

- burning eFuses — the revision, download-mode and JTAG-disable fuses included;
- enabling secure boot or flash encryption;
- writing production secrets, or destroying keys.

**A session without the board still writes `NOT EXECUTED — HARDWARE REQUIRED`,**
because that line is a statement about evidence and not about permission. Nothing
here lets a simulator screenshot, a datasheet figure or an upstream measurement
be reported as a measurement taken from this board.

## Use the hardware earlier, not at the end

The process change that matters most, and the cheapest one: **do not defer
hardware validation to the end of a feature.**

If a change rests on an assumption about display, touch, GPIO, flash, power,
timing, PSRAM, I²C, GNSS, radio, sleep/wake, memory or performance — ask whether
a small probe can be run *first*. A ten-minute measurement at the start can save
days of architecture built around a wrong number. The rules that make a probe
worth anything are unchanged and not negotiable:

- simulator is not hardware;
- a datasheet is not a measurement;
- somebody else's measurement on their board is not a measurement on ours;
- `NOT EXECUTED` never becomes `PASS` because a deadline arrived.

## What is not slowed down by this

- **Demonstrated P0/P1 security, corruption or data-loss defects**, a
  demonstrated queue stall that blocks product work, or automation work the
  owner explicitly requests. Agent automation is maintenance infrastructure,
  not a permanent product workstream: every such change has one finite issue
  and Definition of Done, then closes.
- **Research tied to a decision in one of the next slices** — bring-up,
  display, touch, power, flash, input, sleep/wake, battery; MeshCore when its
  adapter is actually being written, GNSS when its provider is; Clock and UI
  research immediately before the Clock. This project's research discipline is
  one of its best properties and it is not being traded away. What changes is
  that new research should be able to name the decision it unblocks.
- **Anything already nearly finished.** Finishing beats reprioritising; a branch
  abandoned at eighty percent is worth less than one landed.

## What does not change

Host-testability. The simulator. Strict warnings and the sanitizer runs. An ADR
wherever a durable decision is actually taken. Hardware provenance and explicit
`UNKNOWN`. Source checks. Independent review. The Definition of Done.

One thing is **added** to the Definition of Done, for tasks about physical
behaviour: **real hardware evidence**, or an explicit and accurate statement
that there is none yet.

## The success criterion

Written so that somebody outside the project can check it:

> I clone Attadipa, follow the documented build, flash a supported watch, and it
> boots into Attadipa. It shows a Clock. It responds to touch. It goes to sleep
> and wakes up. An agent connects to it, takes a screenshot, performs a gesture,
> and checks the result.

At that point the project has a firmware product loop, and mesh, navigation,
Child Mode, sensors, advanced power, ML and the second board can all be built on
something that exists.

Until then the sentence to aim for at the end of a task stops being

    HOST TESTS PASS / SIMULATOR PASS / RESEARCH COMPLETE

and becomes

    RUN ON THE ACTUAL WATCH — PASS.
