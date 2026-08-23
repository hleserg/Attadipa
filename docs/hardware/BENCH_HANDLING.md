# Handling the boards on the bench

Rules for the physical units while they sit on a desk between sessions. Not
about what the firmware should do — that is
[WAVESHARE_ARRIVAL](../research/WAVESHARE_ARRIVAL.md) §1 and §3.5, which weigh a
near-white face, image retention and static content as *product* decisions and
leave them to the owner. This file is about the hours nobody is looking.

## The AMOLED must not sit lit on a static screen

The Waveshare ESP32-S3-Touch-AMOLED-2.06 boots the vendor firmware to a fixed
desktop and stays there for as long as it has power. **Do not leave it that
way.**

OLED emitters age with the current through them, and current tracks luminance.
A static image with bright elements on a dark ground ages *those elements* and
leaves their shape behind — which is the thing people mean by burn-in, and the
thing a uniformly bright page does **not** do (it dims the whole panel evenly
instead). The two failure modes pull in opposite directions and only one of them
is relevant here.

Waveshare's own examples implement **none** of the mitigations: no idle dim, no
screen timeout, no pixel shift. So the default state of a powered unit is the
worst one.

### What to do, in order of preference

| | Effect | Cost to development |
|---|---|---|
| **Screen timeout, shortest available** | removes the static image entirely | **none** — see below |
| **Brightness at minimum** | slows ageing at least in proportion to luminance; does not stop it | none |
| **Unplug** | stops it completely | the unit is not reachable |

That ranking is the general case. **For the unit on this desk the owner has
chosen row 2**, and an owner decision outranks a preference table — see below.

**A dark panel costs a hardware session nothing.** What a bench run needs is the
ESP32-S3 reachable over USB, not the display showing anything. The RAM-load
route established in [#110](https://github.com/hleserg/Attadipa/pull/110) writes
no flash and need not light the panel at all. So "powered, screen off" is the
state to aim for: fully available, ageing nothing.

**[OD-16](../research/OWNER_DECISIONS.md#od-16--the-received-unit-stays-powered-with-its-brightness-at-minimum),
2026-08-23:** the received unit stays powered and attached so that hardware runs
are possible on demand, with the vendor firmware's brightness at minimum. The
decision and its wording live there; this file only acts on it.

### What an agent must not do about it

**An agent cannot blank the panel and must not act as though it can.** There is
no Attadipa firmware on the unit, so nothing of ours can address the display.

And the obvious workaround — drive the port and hope something blanks it — has
**two** ways to reboot the board, both recorded in
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §2.2 and
§7. DTR and RTS are GPIO0 and EN here, and:

- **pyserial asserts both on `open()`**, so naive tooling resets the board just
  by attaching to watch it. Two RAM images were destroyed that way. Defeated by
  setting both `False` on the `Serial` object *before* `open()`.
- **the kernel drops both on the *last* close** of a `ttyACM`, so a tool exiting
  is itself a reset. *This* is the one that presented as `rst:0x15
  (USB_UART_CHIP_RESET)` and cost four experiments before it was read correctly.
  Defeated by never letting the port close — §2.3.

Both are solved, which is why the RAM-load route above stands: a session may
open and hold the port. What does not follow is that an agent may reboot the
owner's device to save its screen — that is a trade to be asked for, not made.

So the action is the owner's. The agent's job is to **say so** at the end of a
bench session, or before a long stretch of work that does not need the unit,
rather than leaving it lit because nothing being run is using it.

### What is not established

No lifetime figure for this specific panel. The controller is a **CO5300**,
driven by an SH8601-family driver
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)), and
[D7](../research/OPEN_QUESTIONS.md) has not settled even its initialisation
sequence; no datasheet for the emitter has been obtained. Class figures for
"AMOLED" are not this part's. **The residual risk at minimum brightness is
`UNKNOWN`, not "safe"**, and a number here would be an estimate wearing a
measurement's clothes.

## Identify a board by its USB serial, never by its port

More than one ESP32-S3 is attached to the development host, and they enumerate
identically as `303a:1001` — the watch and a MeshCore node. `/dev/ttyACM0` is
not a stable name for either.

**Every tool that writes to a device must resolve the port from the unit's USB
serial and exit non-zero rather than guess — and none does yet.** What exists is
the ad-hoc guard every write in the [#110](https://github.com/hleserg/Attadipa/pull/110)
session went through, which was that session's own script and did not survive it.
Until a shared one exists, a flashing task writes its own or does not write.

`--port /dev/ttyACM0` is the failure this rule exists to prevent: both devices
answer to that name and the write lands on whichever enumerated first, which may
be the owner's MeshCore node rather than the watch.

On an ESP32-S3 the USB serial string **is the base MAC**, which is the owner's
and does not belong in this repository — so it is resolved at runtime and never
written down here.
