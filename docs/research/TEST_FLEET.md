# The physical test fleet

What hardware actually exists to test against, who can reach it, and what is
still unknown about it.

This file exists because [`HIL_PLANS`](../testing/HIL_PLANS.md) says *"a second
Attadipa node or any transmitter"* in two places (H-3, H-4) and never says which
nodes those are. A plan whose equipment list is a category rather than a part is
a plan nobody can start.

Owner-supplied, 2026-08-22. Everything in §1 is **reported by the owner and not
independently verified** — nothing here has been read off a device by this
project. §2 is the opposite: it is measured, and it is the part most likely to
save the next agent a wasted run.

## 1. The nodes

**Five MeshCore nodes: four Heltec T114 and one Heltec V4.3.** Owner, 2026-08-31,
answering [#124](https://github.com/hleserg/Attadipa/issues/124). The count used
to read "T114, 2", and every sentence in this file that said *both* T114s
quantified over a set of two that does not exist. The role is what identifies a
node here, not the number.

| Node | Count | Role | Reachable over |
|---|---|---|---|
| **T114** | 4 | Home Assistant · **the free bench node** · Room Server · repeater | BLE, USB |
| **V4.3** companion | 1 | the second bench node, and the one that answers for the Beta Room | BLE, USB |

- The **free T114** is the bench node of T-169. It stays on
  `v1.17.1-d929643` — owner decision, [#90](https://github.com/hleserg/Attadipa/issues/90),
  2026-08-31 — because that is the revision this repository pins for protocol
  work, and reflashing it would leave ADR-0003 with no node to re-check against.
  It is the only node in the fleet matching the pin.
- **The other three T114s** are to be flashed to the latest official MeshCore
  release. The Home Assistant one carries no screen and no GNSS; the free one has
  both. Roles for the Room Server and repeater nodes are owner-reported and their
  firmware has not been read off the device.
- The **V4.3** runs [`dt267/MeshCore-Low-Power-Firmware`](https://github.com/dt267/MeshCore-Low-Power-Firmware)
  (`v1.17.dev`, `9 Aug dt267`). It is physically connected by USB to the owner's
  machine — see §2 before assuming that means anything from here.
- **Node selection is by advertisement order, not by name, and that is a real
  problem.** `advertises_meshcore()` matches the Companion service UUID or the
  name substring `MeshCore` and connects to whichever advertisement arrives
  first, so which node a run talked to is not a choice the firmware makes. Filed
  as [#304](https://github.com/hleserg/Attadipa/issues/304); every T-169 claim
  names its node for this reason.

  The advertised names are **not** what this file used to say. `MEASURED`
  2026-08-28, [`MESHCORE_T114_FIRST_CONTACT.md:62`](MESHCORE_T114_FIRST_CONTACT.md)
  "`RESP_CODE_SELF_INFO` name": the T114 answers `Beta test companion` and the
  **V4.3** answers `✂️Beta Serega`. The earlier claim here — that two T114s shared
  the name `Beta Serega` — was wrong in both halves, and the report that measured
  it had already withdrawn the reading behind it
  ([`MESHCORE_T114_FIRST_CONTACT.md:81`](MESHCORE_T114_FIRST_CONTACT.md)
  "The earlier revision of this report recorded").

### 1a. Parts on hand that are not a node yet

Owner, 2026-08-31, recorded here because §1's provenance is exactly right for it:
**reported by the owner, nothing read off a device.** None of it was on the bench
during any run in this repository.

| Part | What it is |
|---|---|
| Seeed **XIAO nRF52840** | the MCU board |
| Seeed **Wio-SX1262** shield | the LoRa radio |
| u-blox GNSS module | recorded as `quescan ublox10 an3126` |
| **QMC5883L** | magnetometer |
| **W25Q128** | external flash |

This is in the fleet file rather than in #90 alone because it changes what the
fleet *can* become. Read in upstream MeshCore at the pinned commit
`d92964352441e53b93e8667b802e04f6e072b39e`, `variants/xiao_nrf52/platformio.ini`
(fetched and read 2026-09-01, not taken from a summary): a base section
`[Xiao_nrf52]` carrying `board = seeed-xiao-afruitnrf52-nrf52840` and
`-D RADIO_CLASS=CustomSX1262`, and **five** buildable environments inheriting it
— `Xiao_nrf52_companion_radio_ble`, `_companion_radio_usb`, `_repeater`,
`_room_server`, `_kiss_modem`. The first is the role the watch talks to. So a
node can be built from parts already owned, at exactly the revision this
repository pins, without touching any existing node.

The count is five and not six: `[Xiao_nrf52]` has no `env:` prefix and is a
shared base rather than a target. Written out because "six environments"
circulated before the file was read.

It is not a task and nothing proposes building one. It is written down so that
"the free T114 is the only pin-matched node" is read as a present fact rather
than a permanent constraint.

**`UNKNOWN`, and not to be inferred from the parts list:** band and antenna for
the region in use (see [#55](https://github.com/hleserg/Attadipa/issues/55) for
why that matters), whether any stock variant wires the u-blox module or the
QMC5883L at all, power budget, and enclosure. None of it is traced to a
datasheet, a schematic or a bench result.

### 1b. Access

- **A BLE pairing PIN is required, and it is deliberately not in this
  repository.** This repository is public, and a pairing PIN is a device access
  credential — writing it down here would publish it, permanently, to anyone who
  clones. The owner holds it. Ask for it in the session that needs it.

## 2. What a cloud agent session can reach: nothing

**MEASURED**, 2026-08-22, inside a Claude Code remote container:

| Checked | Result |
|---|---|
| `/dev/ttyUSB*`, `/dev/ttyACM*`, `/dev/serial` | absent |
| `usbip`, `usbipd` | not installed |
| `lsusb` | not installed |
| `esptool`, `esptool.py` | not installed |

So a cloud session cannot open a serial port, cannot enumerate USB, and cannot
flash or read a device. The USB attachment in §1 is to the **owner's** machine,
which is not this one. Every plan in `HIL_PLANS` therefore stays
`NOT EXECUTED — HARDWARE REQUIRED` from a session like this, and an agent that
reports otherwise has not run what it says it ran.

**One thing that is deliberately left open.**
[`WAVESHARE_FLASH_LAYOUT`](WAVESHARE_FLASH_LAYOUT.md) §2.2 records two flash
reads done "over USB/IP", which means *some* session did reach hardware that
way. Which kind of session that was is not recorded, and this container has none
of the tooling that would make it possible. Do not plan work on the assumption
that USB/IP is available — establish it first, in the session that needs it, and
write down which kind of session it was so this paragraph can stop hedging.

## 3. What is still unknown about these nodes

Three open questions, each with an issue, none of them answerable from here:

- **Which band?** [#89](https://github.com/hleserg/Attadipa/issues/89). If
  neither companion is 868 MHz there is no mesh for the T-Watch's radio to
  join, and every LoRa interoperability plan is untestable rather than merely
  unstarted. This is the one that gates the others.
- **Which firmware?** [#90](https://github.com/hleserg/Attadipa/issues/90).
  Several revisions across the fleet. This entry used to say the pinned
  `d929643` was none of them; that is no longer true and the correction matters,
  because it is the whole reason the free T114 is not being reflashed. The bench
  node was `MEASURED` at `v1.17.1-d929643` on 2026-08-28
  ([`MESHCORE_T114_FIRST_CONTACT.md:61`](MESHCORE_T114_FIRST_CONTACT.md)
  "14-Aug-2026"), which is the revision `MESHCORE_BLE_FRAME_CAPACITY.md` read its
  source against. Protocol behaviour observed against any *other* node is
  evidence about that node's revision, not about the pin.
- **No indoor GNSS fix, on the T114 that carries GNSS or on the V4.3.**
  [#91](https://github.com/hleserg/Attadipa/issues/91). Owner-reported, and it
  is **not** a receiver defect: a GNSS module indoors in a flat is expected to
  see nothing. It is written down so that the next person testing from a desk
  does not file it as one, and so that any time-to-first-fix work (H-5) is
  planned outdoors from the start.

## 4. What this fleet makes testable

Not a promise that any of it has been run — see §2 — but the plans whose
equipment line is now answered rather than open:

- **H-3** (deep sleep really is deep) needs "a second Attadipa node or any
  transmitter". The free T114 is that transmitter, subject to #89.
- **H-4** (front-end noise floor) needs "a second node with a calibrated
  output". The T114 is a second node; *calibrated* it is not, so H-4 still needs
  a signal generator for the numeric half and the node only for a presence check.
- **H-9** (bonded reconnect) needs a phone and the board, not these nodes, and
  is unaffected.
- Anything about the companion link protocol —
  [`MESHCORE_COMPANION_PROTOCOL`](MESHCORE_COMPANION_PROTOCOL.md) — now has a
  physical counterpart to talk to, once #90 settles which revision it is.
