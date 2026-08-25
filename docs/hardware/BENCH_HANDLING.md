# Handling the boards on the bench

Rules for the physical units while they sit on a desk between sessions. Not
about what the firmware should do — that is
[WAVESHARE_ARRIVAL](../research/WAVESHARE_ARRIVAL.md) §1 and §3.5, which weigh a
near-white face, image retention and static content as *product* decisions and
leave them to the owner. This file is about the hours nobody is looking.

## The AMOLED sits lit on a static screen, and the owner has decided it stays

The Waveshare ESP32-S3-Touch-AMOLED-2.06 boots `phone_s3_box_3` to a fixed
desktop and **has been seen sitting on it for hours at a time, on USB** — that
much is observed, and it is the whole of what the risk case rests on. It has
never been observed **on battery**, and what it does when left idle **on
battery** is `UNKNOWN`. Every `UNKNOWN` about idle behaviour in this file is
scoped to battery for that reason; one of them was written unqualified until the
fifteenth review round of #134, which gave an agent reading it warrant to
conclude that nothing establishes the screen stays lit at all — and OD-18 comes
apart from underneath rather than from the front. **That is the state
[OD-18](../research/OWNER_DECISIONS.md) chose, knowingly, over the safer one, and
an agent does not ask for it to be undone** — it does not power the unit down and
does not offer "unplug it" as the recommendation. What follows is why the state
costs something, what the mitigation in force is, and the one thing an agent
owes about it; the general ranking below is the *general* case and this unit is
not it. This heading used to be an instruction, forty lines above the decision
that reverses it.

The rest of this section is the reasoning behind the risk, not a request to act
on it.

OLED emitters age with the current through them, and current tracks luminance.
A static image with bright elements on a dark ground ages *those elements* and
leaves their shape behind — which is the thing people mean by burn-in, and the
thing a uniformly bright page does **not** do (it dims the whole panel evenly
instead). The two failure modes pull in opposite directions, and the one that
leaves a mark here is the differential one — but not to the exclusion of the
other, because the only lifetime figure this repository has is a *uniform*-ageing
number, and the bound further down leans on it for want of anything closer.

Waveshare's **example sources** implement none of the mitigations —
[WAVESHARE_ARRIVAL](../research/WAVESHARE_ARRIVAL.md) §3.5, *"Waveshare's own
examples do none of it"*, written against a Rust firmware for this same board
that steps brightness at 8 s and 15 s and blanks at 180 s idle. **One of those
three is not an omission but a limit of the part**, and the distinction matters
because no firmware can fix it: the CO5300 has **no pixel-shift and no scroll
command at all**, so shifting is software or it does not happen. An idle dim and
a screen timeout are ordinary software and genuinely absent. **A fourth thing
the part does offer was missing from this list altogether** — Auto Current Limit
(`55h`), which defaults to disabled and which no driver in the ecosystem writes
([OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md) A10); its cost and saving here
are both unmeasured. The BSP also leaves the panel at `0x51 = 0xFF`, 100 %
brightness, with the hardware dimming ramp turned off.

**That is about example code, and the unit on the desk is not running it.** It
runs **`phone_s3_box_3`**, `v0.4.2-92-g5c6be6c-dirty`, built 4 Nov 2025 against
IDF `v5.5.1-dirty` — this project's own flash dump, `WAVESHARE_FLASH_LAYOUT` §2.1:
`otadata` is blank end to end, so the bootloader falls through to `factory`, and
the `xiaozhi 1.8.5` image in `ota_0` has never been selected. An earlier version
of this paragraph called the running image *"the same opaque one"* this file
refuses to reason from about the charger. **That opacity belongs to `xiaozhi`,
which is not on screen** — a verdict borrowed from the wrong binary, which then
made a question owner-only that a source read can reach.

What the running image does when left idle **on battery** is still `UNKNOWN`,
unobserved — the qualifier is load-bearing, per the first section: on USB it has
been seen staying lit for hours, and that observation is not in doubt. But the
image is not anonymous either, and the nearest source has now been read rather
than assumed — see row 1 and *"What was tried"* below.

### What to do, in order of preference

| | Effect | Cost to development |
|---|---|---|
| **Screen timeout, shortest available** | removes the static image entirely | **none** — and whether the received unit offers one is `UNKNOWN`, unobserved, **not** absent. The launcher it boots carries a **`Settings`** app ([WAVESHARE_BOARD_RECEIVED](../research/WAVESHARE_BOARD_RECEIVED.md) §1.9) and **OD-18** is the owner having already been inside it — «нашел **в настройках** яркость экрана». Nobody has enumerated the rest of that menu. This is the cheapest open question in the file: thirty seconds, and unlike the one below it needs no cable out. The likeliest upstream **has** been read — see *"What was tried"* — and its display page offers brightness and theme and no timeout, which makes "no" `LIKELY` and leaves the unit itself unobserved |
| **Brightness at minimum** | slows ageing at least in proportion to luminance; does not stop it | **not none** — it is the *plugged-in* state, and what that costs the cell is `UNKNOWN` rather than nothing: see **the cell** under *"What is not established"* |
| **Unplug** | **cannot be assumed to stop it** — the cell is fitted and `VBAT1` has no disconnect switch, so removing USB does not remove power; what the factory image does on battery is `UNKNOWN`, unobserved | the unit is not reachable, *and* the cell carries the load instead of USB. Direction only: **how fast, and therefore whether it is left sitting at a low state of charge, depends on the same unread image behaviour the effect cell declines to lean on** |

That third row used to read *"stops it completely"*, which is what pulling a
cable does on a board with no battery. This is not one: the schematic gives the
cell no way to be disconnected, so unplugging does not remove power — see **the
cell** under *"What is not established"*.

**What it does not say is that the desktop stays lit on battery.** That has never
been observed: this unit has only ever been seen on USB, and nothing in
`WAVESHARE_RUNNING_OUR_CODE`, `WAVESHARE_BOARD_RECEIVED`, `WAVESHARE_ARRIVAL` or
`VERIFIED_FACTS` records a run with the cable out. Devices dim, blank or sleep on
battery routinely, and this file calls the running image opaque forty lines
below — so asserting what it does there would be leaning on exactly the unread
state the same file refuses to lean on about the charger. **A thirty-second look
by the owner would settle it as `MEASURED`, and nobody has looked.** If it does
blank, row 3 becomes the only mitigation that works and this table is wrong about
it.

That ranking is the general case. **For the unit on this desk the owner has
chosen row 2**, and an owner decision outranks a preference table — see below.

**A dark panel costs a hardware session nothing.** What a bench run needs is the
ESP32-S3 reachable over USB, not the display showing anything. The RAM-load
route established in [#110](https://github.com/hleserg/Attadipa/pull/110) writes
no flash and need not light the panel at all. So "powered, screen off" **will
be** the state to aim for: fully available, ageing nothing.

Future tense on purpose. T-165 boots, but contains no display driver — nothing
of ours can address its display. On 2026-08-25 the owner therefore requested a
factory-image restore when T-165 left the panel bright and non-interactive. The
factory UI booted bright, touch worked, and the owner returned brightness to
minimum. That makes row 2 the measured current state; "powered, screen off"
still waits for the T-166 display path.

**[OD-18 — *The received unit stays powered, with its brightness at
minimum*](../research/OWNER_DECISIONS.md), 2026-08-23:** the received unit stays powered and attached so that hardware runs
are possible on demand, with the vendor firmware's brightness at minimum. The
decision and its wording live there; this file only acts on it.

### What an agent must not do about it

**An agent cannot blank the panel and must not act as though it can.** There is
no Attadipa display driver on the unit, so nothing of ours can address the
panel.

And the obvious workaround — drive the port and hope something blanks it — has
**two** ways to reboot the board, both recorded in
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §2.2 and
§7. **DTR and RTS are not pins on this board** — it has no USB-UART bridge:
`USB_N`/`USB_P` go through 22 Ω resistors R19/R20 to the SoC's **native** USB
pins ([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md), `VERIFIED`), and every
session here has used `USB-Serial/JTAG`. They are **CDC control-line bits**, and
the USB-Serial/JTAG peripheral resets the digital core when the host changes
them. That is not a reading of a schematic, it is what the board *reported*:
`rst:0x15 (USB_UART_CHIP_RESET)`, which by definition is the peripheral doing it
at the host's request
([WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §2.2) —
a reset delivered by pulling EN would not name itself that. This file said *"DTR
and RTS are GPIO0 and EN here"* in two places until the thirteenth review round
of [#134](https://github.com/hleserg/Attadipa/pull/134), inherited from `main`
and unsourced in either; it matters because **T-116** goal 3 is a hardware
observation and whoever designs that experiment against pins will scope GPIO0
and learn nothing. **The operational rule is unchanged either way: plan for the
board to reset on open.** And:

- **pyserial asserts both on `open()`**, so naive tooling resets the board just
  by attaching to watch it. Two RAM images were destroyed that way. Setting both
  `False` on the `Serial` object *before* `open()` is **`LIKELY` to be defeated**, not established: on Linux `cdc_acm`
  raises DTR and RTS in the kernel on the tty open path
  (`acm_port_dtr_rts()`, `drivers/usb/class/cdc-acm.c`, the `dtr_rts` member of
  `acm_port_ops`, setting `USB_CDC_CTRL_DTR | USB_CDC_CTRL_RTS`, reached from
  `tty_port_block_til_ready()` — **not** `acm_port_activate()`, which is the
  `activate` op and touches no control line; corrected in round 15 of #134) —
  before any userspace code runs — so a pyserial-side pre-set can only lower the lines
  again *after* the assertion, not precede it. Reading the driver is **not** a
  measurement on this unit: what it establishes is that the mechanism exists,
  not that it fires here. It has never been observed on
  this unit either way, and proving it is T-116's third goal.
- **the kernel drops both on the *last* close** of a `ttyACM`, so a tool exiting
  is itself a reset. *This* is the one that presented as `rst:0x15
  (USB_UART_CHIP_RESET)` and cost four experiments before it was read correctly.
  Defeated by never letting the port close — §2.3.

One is solved and the other is `LIKELY`, which is worth separating before this
paragraph is read as a blanket permission. **Hold-open has a bench result**:
§2.3 is a recorded experiment, and it is what the RAM-load route above rests on.
**Pre-open does not.** It is one asserted line in `WAVESHARE_RUNNING_OUR_CODE`
§2.2, inside a list of things that turned out *not* to be the cause, and on
Linux `cdc_acm` raises DTR and RTS in the kernel on the tty open path
(`acm_port_dtr_rts()`, via `tty_port_block_til_ready()`) — before any userspace
code runs — so a pyserial-side pre-set cannot precede the assertion, only lower
the lines again after it.

**`stty -hupcl` is a separate failure and fails for a separate reason.**
`HUPCL` governs the lines being dropped on **close**, not raised on **open**, so
clearing it could never have prevented an open-time assertion in the first
place; and `WAVESHARE_RUNNING_OUR_CODE` §2.2 records why it did not survive
either — *"esptool reopens the port, and pyserial restores termios on open"*.
Two different mechanisms, and reading the `cdc_acm` fact as the explanation for
`-hupcl` sends whoever picks up T-116 goal 3 after the wrong one. Proving the
open-time assertion on the unit is that goal. So a
session may open and hold the port — **having resolved it by USB serial first, which is the
rule at the bottom of this file and not optional here**; this sentence sits in a
section headed by what an agent must not do, and on its own it reads like
permission. What does not follow is that an agent may reboot the
owner's device to save its screen — that is a trade to be asked for, not made.

So the action is the owner's — and under OD-18 there is very little left to
recommend: minimum brightness is already set, nothing of ours can blank the
panel, and item 1 of that decision rules out offering "unplug it" as the
recommendation. **One thing does remain, and it stays on the list until somebody
looks:** row 1 of the table — whether that `Settings` menu holds a display
timeout. It is unobserved rather than unavailable, and it is the only row whose
effect is to remove the static image at no cost to development.

**What was tried, so the next agent does not repeat it.** The running app is
named, so the obvious move is to read its source instead of asking the owner.
`REUSE_LEDGER` already has `espressif/esp-brookesia` cloned at `01939b5e`,
Apache-2.0, and `WAVESHARE_ARRIVAL` §3.1 lists `03_esp-brookesia` among the vendor
examples, so the framework is the likely upstream. At that revision the Settings
app's display page holds a **brightness slider and light/dark theme modes and
nothing else** — the only display actions in the whole app are
`ACTION_DISPLAY_BRIGHTNESS` and the two theme modes; there is no timeout, sleep
or auto-lock entry. Read 2026-08-23, **and written into
[REUSE_LEDGER](../research/REUSE_LEDGER.md)'s own row** rather than left here —
a reading that lives only in the file that needed it is a reading the next agent
re-clones and repeats.

That is `LIKELY` and **not** an answer, for two reasons that are worth writing
down rather than glossing: the clone is at `01939b5e` (2026-08-10) while the
unit runs a build from 4 Nov 2025, and `5c6be6c` is not an object in it; and
`phone_s3_box_3` has not been *identified* with esp-brookesia — the name reads
like an ESP32-S3-BOX-3 demo, and the app list matching §1.9 is a resemblance,
not a provenance. So the source read narrows it and the owner's thirty seconds
still settles it. Beyond that, what remains is a fact worth stating, not a
request dressed as one.

**The trigger, so this is neither guesswork nor boilerplate:** say it when
either the session is ending or the next stretch of work does not need the unit,
and **at most once per session**. An earlier version of this rule also said
*"since the last time it was said"*, which is state nothing in this repository
records — a fresh session cannot know, so it is true every time and the clause
was decoration on a threshold that the once-per-session bound is already
carrying alone. Say that it is sitting lit, name the mitigation in force
as the brightness the owner themself set, and stop there rather than inventing an
action to accompany it. **After any reset, do not assume the setting survived.**
On 2026-08-25 a full factory restore followed by a hard reset brought the panel
up bright; whether the saved setting was absent or boot code overwrote it was
not isolated. Touch worked and the owner manually returned brightness to
minimum. The operational rule is therefore unchanged: after a reset report
brightness as unconfirmed until somebody observes it. In this session that
observation happened. Repeated in every report, an observation the owner acted
on once becomes a line they learn to skip, which is the failure mode of a rule
with no threshold.

### What is not established

**The panel.** No lifetime figure for this specific panel. The controller is a
**CO5300**, driven by an SH8601-family driver
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)), and
[D7](../research/OPEN_QUESTIONS.md) has not settled even its initialisation
sequence; no datasheet for the emitter has been obtained. Class figures for
"AMOLED" are not this part's. **The residual risk at minimum brightness is
`UNKNOWN`, not "safe"**, and a number here would be an estimate wearing a
measurement's clothes.

What the repository *does* have is a hedged bound from two comparable modules,
and it is worth having because the owner's question was about a working session:
[WAVESHARE_ARRIVAL](../research/WAVESHARE_ARRIVAL.md) §3.5 reads them together as
*"one debugging afternoon of a static frame will not leave visible sticking …
but twelve hours of full white is roughly 8 % of a rated white-pattern lifetime,
so repeated sessions spend real life"*, and marks both *"it will burn in this
afternoon"* and *"it is free"* unsupported.

Those conditions are **not one condition**, and this sentence used to combine
them. On that page the maximum-luminance condition belongs to the *image-sticking*
test, whose pattern is an 8×8 chessboard; the **150 hrs** white-pattern lifetime
is quoted with no luminance at all; and the **≥ 200 hours** figure is at white
light and 600 cd/m², which the source nowhere calls that module's maximum. §3.5
closes by saying none of the three is this panel's. So against OD-18's minimum
brightness they bound the risk **in direction, not in size** — sizing it would
need two numbers this repository has not got: the luminance each figure was rated
at, and this panel's luminance at minimum brightness. `51h WRDISBV` is a register
nobody here has mapped to cd/m², and no cd/m² figure for this panel exists in
`docs/research/` at all. Which is why the residual risk stays `UNKNOWN` and this
paragraph is a bound, not a number.

**The cell, which nothing above weighs.** The unit has a battery fitted — a
`402728` marked 400 mAh, its plug visibly mated
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)) — and net `VBAT1` has **no
protection FET, no fuel gauge, no load switch and no disconnect switch**
([BATTERY_UPGRADE](../research/BATTERY_UPGRADE.md) §1.1). Honest expectation for
what that cell actually holds: **250–310 mAh `ESTIMATED`, with 300 mAh as the
working figure** (`BATTERY_UPGRADE` §1.2 for the pair, §3 for the three
independent lines that converge on it; nothing weighed).

An earlier version of that parenthesis corroborated the figure with §2's
*"expect 250–300 mAh"*, which is **a different cell** — the expected capacity of
a *replacement* pack, whose very next sentence uses the fitted cell as its
denominator. Two near-identical numbers about two different objects, reading as
agreement.
Two consequences follow, both from facts this repository already holds at
`VERIFIED`, and neither of them a conclusion about harm:

- **Unplugging does not remove power**, it changes which consumable pays. Whether
  the panel keeps ageing on battery depends on what the factory image does there,
  which is `UNKNOWN` — unobserved, not weighed and discarded. This is the
  correction to the third row of the table.
- **Leaving it plugged sits on a charge path nobody here has read.** The
  AXP2101 `TS` pin goes through `RP2` to `GND` and never reaches `J1`, so **the
  charger never sees cell temperature**; Waveshare's BSP configures the charger
  not at all — though its **demo** does, and that is the nearest evidence anyone
  has: 400 mA CC, 4.2 V, precharge 50 mA, **termination 25 mA**, `TS` disabled
  (`BATTERY_UPGRADE` §1.1, `VERIFIED`), which points *away* from the float-charge
  case rather than toward it — and what `phone_s3_box_3` does to those registers
  has not been read either; and **the PMU never
  sees a POR while the cell is connected**, so `REG 0x64` (CV target) and
  `REG 0x63[4]` (termination enable) persist as whatever that image last wrote.
  With `0x63[4]` clear the charger holds CV indefinitely, float-charging a
  Li-ion — `BATTERY_UPGRADE` §6.

  **`REG 0x61` (precharge) persists the same way and is live down a different
  path** — row 3 of the table above, the one whose cost is the unit *"left
  sitting at a low state of charge"*. Plug back in below 3.0 V and precharge
  runs at whatever the factory image wrote; the POR figure is 125 mA, 0.42C on a
  ~300 mAh cell, four times the convention for a cell in exactly that state
  (`BATTERY_UPGRADE` §6). It is not live in the state OD-18 actually holds the
  unit in — plugged and full, precharge does not run — which is why it is
  written down here rather than raised as an alarm.

**And that is where it stops.** Nothing here says the cell is being harmed:
those registers have not been read, and reading them means running our code on
the unit. **The owner was asked whether he wanted them read and said yes**
(2026-08-24, under OD-18 in
[`../research/OWNER_DECISIONS.md`](../research/OWNER_DECISIONS.md)), so the read
is owed on the next bench trip rather than merely available — which changes when
these `UNKNOWN`s get an answer and changes none of them today. The task is
**T-106**, whose register list now carries `0x63`, `0x64` and `0x61`
for exactly this reason: three cell-safety registers, not one question and not a
pair, and one burst reads all three. An earlier version of this sentence handed it to `T-114`,
which has no heading in `TASKS.md` on `main` or on this branch — so at the time
of writing the citation pointed at nothing. **It will not stay that way:**
[#121](https://github.com/hleserg/Attadipa/pull/121) adds
`### T-114 · The debug channel needs a firmware end` (issue
[#117](https://github.com/hleserg/Attadipa/issues/117)), about the debug
channel and not about a charger register. Once that merges the citation
resolves — to the wrong task — and a reader following the retraction to check it
cannot tell whether the registers moved there or this sentence went stale.
Which is why the claim is scoped to the two branches it was checked against
rather than to *the repository*: absent-everywhere is a claim about branches,
and the sweep that found five `OD-16`s is the method that answers it. It found five branches and there were six occurrences: the sixth was this file's own row 1 above, renumbered in `STATUS.md` and not here — one sentence, two files — and found only in the thirteenth review round of [#134](https://github.com/hleserg/Attadipa/pull/134). Nothing catches a bare `OD-NN` in prose: `check_decision_ids` reads headings, `check_links` needs a link, `check_citation_lines` needs a `path:line`. The renumber's own recorded price is what makes such a miss invisible — the count matches what changed, so nothing is left over to notice. (This sentence used to price it at *"15 occurrences across six files"*, which was true of `0e9f86f` and is not true of the tree: `STATUS.md` now records **21** across six, because the OD-17 → OD-18 renumber came after. Quoting a number from a commit that has moved is the same defect one level down, found in round 15.)

So the file's cell-safety questions read as *assigned* while having no owner
anywhere, which is worse than reading as absent, because a reader who checks
stops at the citation. That is T-117 item 2's surviving blind spot —
`check_docs.py` never finds a **dangling** task ID, so a green run says nothing
about it; its anchor half is built on
[#92](https://github.com/hleserg/Attadipa/pull/92). Nor does it say which
state is kinder — neither is established, which is precisely why this file must
not recommend one on the cell's account. What it does say is that *"powered
indefinitely"* has a **second consumable** in it, that the panel's risk was
weighed across three sources while this one was not weighed at all, and that a
reader had no way to tell the difference. **Absent is not `UNKNOWN`; absent
reads as weighed.**

## Identify a board by its USB serial, never by its port

More than one ESP32-S3 is attached to the development host, and they enumerate
identically as `303a:1001` — the watch and a MeshCore node. `/dev/ttyACM0` is
not a stable name for either.

**Every tool that opens a port on a device must resolve it from the unit's USB
serial and exit non-zero rather than guess — and none does yet.** *Opens*, not
*writes*: the casualty forty lines above wrote nothing. pyserial asserts DTR and
RTS inside `Serial(...)`, and this board's USB-Serial/JTAG peripheral resets the
digital core when it sees that — no pin is involved, see the correction above —
so the reset is done by the time a tool has decided whether it has anything to
say. What exists is
the ad-hoc guard every write in the [#110](https://github.com/hleserg/Attadipa/pull/110)
session went through, which was that session's own script and did not survive it.
The shared one is **T-116** in [`TASKS.md`](../../TASKS.md), together with the
lint that stops a fourth ad-hoc guard from being written in the meantime; it is
named here because this is the newer and more discoverable of the two files that
state this rule, and it was the one without the pointer. Until it lands, **any
task that opens a port** brings its own resolver or does not open one — which
covers a screenshot, a log tail and `idf.py monitor` as squarely as a flash.

`--port /dev/ttyACM0` is the failure this rule exists to prevent: both devices
answer to that name and the **open** lands on whichever enumerated first, which
may be the owner's MeshCore node rather than the watch. Nothing needs to be
written for that node to reboot in service.

On an ESP32-S3 the USB serial string **is the base MAC**, which is the owner's
and does not belong in this repository — so it is resolved at runtime and never
written down here.
