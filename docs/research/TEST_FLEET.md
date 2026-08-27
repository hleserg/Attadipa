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

| Node | Count | State | Reachable over |
|---|---|---|---|
| **T114** | 2 | one headless on Home Assistant, one with GPS and a screen, free | BLE, USB |
| **V4** companion | 1 | on Home Assistant, to be freed for experiments | BLE, USB |

- The **V4** runs [`dt267/MeshCore-Low-Power-Firmware`](https://github.com/dt267/MeshCore-Low-Power-Firmware),
  latest release at the time of writing. It is physically connected by USB to
  the owner's machine — see §2 before assuming that means anything from here.
- The two **T114** nodes are to be flashed to the latest official MeshCore
  release. One carries no screen and no GNSS and stays on Home Assistant; the
  other has both and is available.
- **Both T114s advertise over BLE under the same name, `Beta Serega`.** Two devices
  answering to one name is a discovery problem, not a detail: anything that
  selects a node by advertised name will pick whichever answered first. Select
  by address.
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
  Three revisions across the fleet, and the revision this project pins for
  protocol work — `d929643` — is none of them. Protocol behaviour observed
  against a node is evidence about *that* revision until the two are reconciled.
- **No indoor GNSS fix, on either T114.**
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
