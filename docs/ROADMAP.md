# Roadmap — and the one correction that reorders it

`TASKS.md` is the task list and `STATUS.md` is where things are. This file is
neither. It says **which of two possible next tasks wins**, and it exists
because that question stopped answering itself.

Written 2026-08-24, after an independent cold read of the repository.

## Where the project actually is

The engineering base is genuinely strong: a capability model with two layers and
a provider registry, host tests and a simulator, strict warnings and sanitizers
in CI, a research discipline that traces hardware claims to schematics, a debug
channel that screenshots the interface and injects input, and a queue that turns
findings into issues into branches into reviewed pull requests without a person
carrying prompts between agents.

And **none of it has ever run on a watch.**

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
| 3 | **T-166** ([#190](https://github.com/hleserg/Attadipa/issues/190)) | the Waveshare BSP driven vertically — display, LVGL, touch, PMU, RTC, **up to the driver** | T-165; **D21** on the first line of display bring-up |
| 4 | **T-114** ([#117](https://github.com/hleserg/Attadipa/issues/117)) | the debug channel's firmware end, so the agent's screenshot loop reaches the real panel — **and the `InputOrigin::Physical` producer for touch *and buttons*, which is T-114's alone** | T-165, T-166 |
| 5 | **T-037** | the first Clock, running on the watch, on real input, with the real tokens and fonts | T-166 |
| 6 | **T-167** ([#191](https://github.com/hleserg/Attadipa/issues/191)) | screen off, controlled sleep, wake, UI restored, wake reason diagnosable, and the cycle repeatable under the debug channel | T-166; T-068 |

None of the three new ones is `agent:ready` yet, and that is deliberate:
eighteen open pull requests were conflicted against `main` when this was
written, and adding a writer to that is how a course correction turns into
chaos.

**A fence needs a gate, so here is the gate.** `agent:ready` is the queue's only
entry point — `claude-agent.yml` fires on it and the watchdog scans for it — so
a P0 task without it is invisible to the automation, and nothing watches for
that. The three labels are flipped by **the session that holds the queue**, and
the condition is observable rather than remembered: **when the branch it is
about to start is not itself conflicted against `main`.** That is per-issue, so
#189 can start while #190 and #191 wait. Draining the queue is the step before
the slice rather than a competitor to it, and it is
[#172](https://github.com/hleserg/Attadipa/issues/172)'s own subject — the
conflicts come from every pull request editing `TASKS.md` and `STATUS.md`.

Steps 4 and 5 do not strictly order against each other. Step 4 first is the
better bet, because it is the instrument that makes step 5 checkable — which is
the whole argument for having built it.

**D21 is the one technical unknown on the path**, and it is small and named:
in what byte order does the CO5300 want a 16-bit pixel on the wire. It is
answerable either from the datasheet's `3Ah`/`2Ch` packing, or by a
measurement — a known asymmetric pattern written with the swap off, and a
photograph. It blocks the first line of display bring-up and nothing before it,
so it is a probe to run *early* rather than a reason to delay the skeleton.

## What no agent may do, and the owner must

**No agent flashes a physical device.** That is `CLAUDE.md`'s rule and this
milestone does not bend it. Every hardware step therefore ends at:

- a build that is reproducible from a clean checkout;
- a flash procedure written down well enough to follow without improvising;
- and a line that says `NOT EXECUTED — HARDWARE REQUIRED`, never `PASS`.

**The `PASS` arrives when the owner runs the procedure** and reports what
happened. Having the board on the desk does not confer that permission — an
agent on the bench machine reads the board (probes, markings, bus scans,
`MEASURED` numbers) and does not write to it. That hand-off is part of the
milestone, not an afterthought to it, and the procedure is the deliverable that
makes it cheap.

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

- **Correctness and security defects**, and any automation defect that actually
  stalls the queue — a fail-open merge decision, a lost task, a stuck pull
  request, a wrong blocker transition, a provenance hole. Those stay ahead of
  everything, because a stalled queue delivers no slices either.
- **Research tied to a decision in one of the next slices** — bring-up,
  display, touch, power, flash, input, sleep/wake, battery; MeshCore when its
  adapter is actually being written, GNSS when its provider is; Clock and UI
  research immediately before the Clock. This project's research discipline is
  one of its best properties and it is not being traded away. What changes is
  that new research should be able to name the decision it unblocks.
- **Anything already nearly finished.** Finishing beats reprioritising; a branch
  abandoned at eighty percent is worth less than one landed.

Automation improvements beyond the stalling class are now judged by return:
*does this measurably reduce agent idle time, manual work, or the risk of a
wrong merge?* If not, it is below hardware and product work.

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
