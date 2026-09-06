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

> **Next physical seam after T-168:** prove one corrected Room Server delivery
> and a causally corresponding reply rendered on the watch. The gate this note
> used to carry — "only when the current node is observably advertising the
> published Companion service" — is answered and has been since 2026-08-28:
> [MESHCORE_T114_FIRST_CONTACT](research/MESHCORE_T114_FIRST_CONTACT.md) §1a —
> "Both advertise the Companion service and both pair with the same operator" —
> is `MEASURED`. What is still open is narrower and is
> [#304](https://github.com/hleserg/Attadipa/issues/304), and it is *selection*
> rather than identification: the firmware connects to whichever advertisement
> arrives first, and whether the service UUID or the name substring is what
> matched is not recorded at all. Which node a run is talking to is no longer
> in doubt — the watch pins one by its public key and terminates every other
> connection (`firmware/main/meshcore_node_pin.h`), and draws four bytes of that
> key beside the name:
> `firmware/main/waveshare_board.cpp:801` — "// Four bytes of a node's public key as hex. The bench reports identify nodes by".
> The delivery and the reply are the parts that remain `NOT OBSERVED`. None of this is permission to change node firmware, add a
> local radio provider, or grow a messenger UI.

> **Direction, 2026-09-06 — two topologies, not one board with a spare.** The
> owner settled this on 2026-09-06 and it is the frame every paragraph below
> reads in. Atta-dipa is a companion that carries GNSS and LoRa, and a wearable
> terminal on the wrist. **How many devices that is depends on the board, and
> both answers are the product:**
>
> - **T-Watch S3 Plus — one device, not two.** The owner's product statement:
>   watch and companion are meant to be the same unit here, so whatever this
>   board provides, it provides on the same body. **What is verified is the
>   receiver half, and that is the half #442 needs.** Its GNSS named itself
>   `MIA-M10Q` on 2026-09-05
>   ([TWATCH_GNSS_READOFF_2026-09-05](research/TWATCH_GNSS_READOFF_2026-09-05.md)),
>   so `own` comes from a receiver on this body — the body rule satisfied rather
>   than bent, with no owner decision and no confirmation involved.
>   **The radio half is not verified and this block does not assert it.** Nobody
>   has read the marking off the chip, five parts are possible and only some of
>   them do LoRa at all
>   ([HARDWARE_MATRIX](research/HARDWARE_MATRIX.md) — "not a marking read off the part, so `RadioChip::Unknown` does not move and"),
>   which is the precise failure [ADR-0003](adr/0003-radio-not-lora.md) is
>   accepted to prevent — "A T-Watch fitted with a CC1101 would have advertised mesh messaging, offered the".
>   So "self-contained" here says `own` needs no companion, and says **nothing**
>   about this board carrying mesh.
> - **Waveshare AMOLED 2.06 — two nodes, split.** The wrist, and a companion
>   somewhere else. The board has neither receiver nor radio
>   ([HARDWARE_MATRIX](research/HARDWARE_MATRIX.md) — "| GNSS | yes — **two possible modules** | **absent** |")
>   and is not meant to: the split is the point. This is the topology where
>   `own` has no local source, and the whole of OD-28 exists for it alone.
>
> **A T-Watch navigation page is therefore not scope creep and not a "GPS
> watch".** It is the self-contained topology doing what it is for. An earlier
> revision of this block called it out of scope; that was wrong, it read the
> two boards as one product with a spare, and the correction is the owner's.
> What makes both topologies one codebase is the rule
> [AGENTS.md](../AGENTS.md) already states — "Applications ask what a device can
> do, not which board it is": whether `own` has a local source is a capability
> question, not a board question, and the Navigator is already written that way.
>
> The first end-to-end path between the halves is
> [#450](https://github.com/hleserg/Attadipa/issues/450): companion coordinates
> reach the wrist as a distance and a direction relative to the watch body, with
> no phone and no internet. That reorders the seam above rather than replacing
> it.
>
> **Neither half's first shippable state is gated on hardware.** ADR-0009
> decided the whole heading model on 2026-08-21 and its Consequences put the
> no-heading path first — "The Navigator's states are enumerable and testable
> before any GNSS hardware exists" — so the slice has an accepted end state that
> contains no magnetometer: a north-up readout with the bearing marked. That
> state is not a placeholder for the arrow, it is one of the seven rows
> ADR-0009 §5 enumerates. Neither board draws it today, and for two different
> reasons. The Waveshare has the page and not the coordinate: `refresh_nav()`
> reads own position from the pads of a board with no receiver, so
> `Unprovisioned`, and the face says "Waiting for GPS". The T-Watch has the
> receiver and not the page:
> `firmware/main/local_gnss.h:50` — "call `local_gnss_location()` at all yet — it has no navigation page — so the".
>
> **And the link carries one coordinate, not two.** The readout needs a place
> to walk from and a place to walk to. What arrives over BLE is the connected
> node's own position, out of `RESP_CODE_SELF_INFO`:
> `link/src/meshcore_companion.cpp:546` — "        // THE COORDINATE, AND ONLY FROM A NODE THIS WATCH ACCEPTED. Every"
> Nothing in **this repository** parses a remote peer's: a contact record is a
> public key and a name and nothing else —
> `core/include/attadipa/core/mesh_service.h:27` — "struct MeshPeer {".
> The protocol is not the gap. A node emits its position in three places and two
> of them serve a node other than the connected one —
> [NODE_POSITION_FROM_MESHCORE](research/NODE_POSITION_FROM_MESHCORE.md) §1 — "A MeshCore node emits its position in three places. The existing research" —
> and what §6 argues against is *starting* with the telemetry path: a hundred
> times coarser, four gates, a radio round trip. That is a price, not an
> absence — and it is not the whole price either. Both remote paths are shut at
> the node by default, and the telemetry one is shut by a *persisted user
> preference*:
> `docs/research/NODE_POSITION_FROM_MESHCORE.md:140` — "`gps_active` is **off by default** — `_prefs.gps_enabled = 0;` in `MyMesh.cpp`,".
> Path C sits behind its own policy and is not costed at all —
> `docs/research/NODE_POSITION_FROM_MESHCORE.md:55` — "requester mask ∧ `gps_active` | `advert_loc_policy` |".
> A shut gate does not answer with an error, which is what makes this worth
> writing down here rather than discovering in a decoder —
> `docs/research/NODE_POSITION_FROM_MESHCORE.md:135` — "   returns a well-formed telemetry reply **with no GPS record in it**, and that".
> So what #450 owes on this half is which path to pay for, and opening that
> path's gate is bench configuration of a node, a step beside H16. It is not a
> wire to invent, and it is certainly not a change to node firmware, which the
> seam note above forbids in as many words.
>
> **The default slot for a companion's own coordinate is `target`,** and it is
> settled by body rather than by preference —
> `apps/include/attadipa/apps/navigation.h:19` — "// **own** position comes from a receiver on this body, **target** position is".
> A node the wearer has said nothing about — in a rucksack, on a windowsill,
> bolted to a wall, or in a pocket the watch has no way to know about — is not
> promoted to `own` by proximity, because a body the device cannot verify is a
> different body and no transform is known —
> `docs/research/NODE_POSITION_FROM_MESHCORE.md:443` — "position, and a detached node reports a place the wearer is not. Any consumer".
> **What moved on 2026-09-06 is not that rule but what can lift it.** A pocket
> is no longer automatically the refused case; an *unconfirmed* one still is,
> and that remains the default. Which leaves the question this block used to
> call open, and it was the Waveshare's rather than the slice's: **what may ever
> fill `own` on that wrist, which has no receiver.**
> **It was answered on 2026-09-06 and it is no longer open.** The owner took
> the first of three priced options: a companion the wearer has *explicitly
> confirmed is on their body* may fill `own`, and without that confirmation its
> coordinate stays `target` — [OWNER_DECISIONS](research/OWNER_DECISIONS.md)
> OD-28. So the `OwnPosition` payload in the direction prompt is no longer a
> proposal against a rule; it is how the *coordinate* arrives. **Where the
> confirmation itself lives is now decided, and it is not this payload's field**
> — [ADR-0019](adr/0019-confirmed-companion-body.md) puts it on the watch's own
> screen through OD-26's channel, holds it in RAM against the bonded peer and
> the time that peer was last heard, and never persists it. The two things that
> ADR had to keep apart are the confirmation and the validity, and it did: the
> word carrying the whole decision is *confirmed*, and a confirmation that is
> right and then stale is how the refused failure arrives looking like a
> success. The two options not
> taken were
> soldering a receiver to the traced Waveshare pads, and dropping the distance
> altogether. **The question was always the split topology's**, and OD-28 says
> so in as many words: the self-contained one never had it, because there the
> receiver is on the same body and the rule is satisfied rather than lifted.
> What holds `own` off on the T-Watch is neither hardware nor a rule but a
> default waiting for a caller —
> `firmware/main/Kconfig.projbuild:173` — "        bring-up slice, so listening to it is opt-in until something above" —
> and [#442](https://github.com/hleserg/Attadipa/issues/442) is the task that
> supplies one. So the two are not a choice: **#450 is the split topology's
> vertical slice and #442 is the self-contained one's**, they close different
> devices, and neither is scope creep against the other. Which runs first is a
> sequencing call about hands and bench time, not about product scope.
>
> **And whatever answers it runs into one more rule.** Every position the MeshCore channel
> produces states no fix type and therefore classifies `NoFix`:
> `link/src/meshcore_companion.cpp:564` — "path in this repository can reach `PositionValidity::Valid` from it."
> And `NoFix` is exactly what an own position may not be —
> `apps/src/navigation.cpp:148` — "const bool own_ok = usable(state.own) &&" — on
> purpose. **ADR-0019 answered that gate by moving `own_ok` rather than by
> moving `validity`**: a confirmed coordinate still classifies `NoFix` at every
> age, `PositionSource` stays `NodeGnss`, and what changes is which slot of
> `NavState` the firmware routes it to. So the `validity` field in #450's
> `OwnPosition` payload is a detail of the payload after all, not its point —
> and BLE alone still does not unblock the Waveshare, for a different reason
> than this paragraph used to give: a confirmed companion's coordinate becomes
> `own` and is therefore **not** `target`, so the distance stays unrenderable
> until a *remote* node's coordinate can be fetched, which OD-28 leaves open.
>
> **What hardware does gate is the arrow, and its first step is H16** — four
> ohmmeter readings on two bare magnetometer modules, which arrived 2026-09-05
> ([BENCH_DEVICES](research/BENCH_DEVICES.md)) and have not been read off, so
> what H16 waits on now is an ohmmeter and a hand. Until it is answered no
> magnetometer goes on a board, and until one is on a board and reads correctly
> the vibration motor is not wired — a motor beside an uncalibrated compass
> makes two unknowns out of one. That gate is a magnetometer gate. It is not a
> gate on the slice, and reading it as one is what parked this direction on a
> shipment.

## Where the project actually is

The engineering base is genuinely strong: a capability model with two layers and
a provider registry, host tests and a simulator, strict warnings and sanitizers
in CI, a research discipline that traces hardware claims to schematics, a debug
channel that screenshots the interface and injects input, and a queue that turns
findings into issues into branches into reviewed pull requests without a person
carrying prompts between agents.

At the time of this decision, **none of it had ever run on a watch.**

That is not an impression. It is what the tree said, and every bullet below is
that record rather than a live claim — the first of them, "there is no ESP-IDF
project", stopped being true with T-165:

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
finish M2: the T-Watch half remains (T-010). That half was blocked on a board
that was `ORDERED` and not in hand; the board arrived 2026-08-27
([BENCH_DEVICES](research/BENCH_DEVICES.md)) and what remains is the bring-up
itself. Nobody may write *"M2 complete"* off this list alone.

The shape, end to end:

```
real ESP32-S3 → Attadipa firmware → BSP → display → input → UI/app
              → debug channel → sleep/wake → a hardware test somebody can repeat
```

**One board, brought up vertically, before two boards brought up halfway.** The
board is the **Waveshare ESP32-S3-Touch-AMOLED-2.06**, and this is not a
preference — it was the only board in the building when this was written. Both
are on the bench now ([BENCH_DEVICES](research/BENCH_DEVICES.md)) and the order
still holds: the Waveshare is the one brought up vertically, its eFuses have
been read
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
