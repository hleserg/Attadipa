# Which device is which on the bench

> **Status:** read off the hardware, 2026-08-25 and 2026-08-27, on the
> development host.
> Every value below came from `esptool flash-id` and `udevadm`; nothing here is
> inferred from a product name — **except the last section**, two GNSS modules
> recorded from their listings on 2026-09-02 and labelled so on every row.

This document exists because of one sentence in
[WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §2 that was true and
had nowhere durable to live: **three ESP32-S3 boards on this bench all enumerate
as `303a:1001`, `USB JTAG/serial debug unit`, and the USB descriptor does not
say which is which.** `/dev/ttyACM0` and `/dev/ttyACM1` are assigned in
enumeration order, so they swap on a replug. Anything that picks a port by
number will eventually pick the wrong board.

## The three units

| | Waveshare watch | T-Watch S3 Plus | The other board |
|---|---|---|---|
| USB serial | **`28:84:85:B2:18:A4`** | **`DC:B4:D9:18:49:40`** | `F8:5B:1B:A1:98:24` |
| `by-id` link | `usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B2:18:A4-if00` | `…_DC:B4:D9:18:49:40-if00` | `…_F8:5B:1B:A1:98:24-if00` |
| Chip | ESP32-S3 (QFN56) rev **v0.2** | ESP32-S3 (QFN56) rev **v0.2** | ESP32-S3 (QFN56) rev v0.2 |
| PSRAM | **8 MB, `AP_3v3`** | **8 MB, `AP_3v3`** | 2 MB, `AP_3v3` |
| Flash | **`0xC8 0x4019` — GigaDevice, 32 MB** | **`0xEF 0x4018` — Winbond, 16 MB** | `0x68 0x4018` — 16 MB |
| Identification | Waveshare `ESP32-S3-Touch-AMOLED-2.06` | LilyGO T-Watch S3 Plus; the shipped firmware's own FQBN is `esp32:esp32:twatchs3:Revision=Radio_SX1262` | a MeshCore node, per [#116](https://github.com/hleserg/Attadipa/issues/116) |
| Current firmware | **Attadipa T-166 bench candidate**; display at the measured 5% visible floor and physical touch working | **factory, untouched** — nothing has ever been written to this unit | unchanged; do not write |

The **T-Watch column is the only one of the three whose flash is still exactly
as the factory shipped it.** A complete 16 777 216-byte image was read off it on
2026-08-27 and proved three independent ways — on-chip MD5, a second byte-identical
read, and a structural parse — before anything else was attempted. Its SHA-256 is
`e28f5cdd79552950d7f73fc2776023e297bfcd5dcc320d667ee065b0ebd37202`; the evidence and
the reproduction notes are
[TWATCH_S3_PLUS_BRINGUP_2026-08-27](TWATCH_S3_PLUS_BRINGUP_2026-08-27.md), and as with
the Waveshare image the binary's local path is deliberately not a repository artefact.
That report also carries two host-side facts specific to this unit — no software
reset works on it in either direction, and its first USB control
transfer after every `open()` fails with `EPROTO` — either of which reads as a dead
board to a session that has not met them.

The Waveshare row matches [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.1–1.3
on all three of revision, PSRAM capacity and flash JEDEC id, which is what
identifies it — not the port it happened to appear on. `PSRAM_CAP = 8M` and
`PSRAM_VENDOR = AP_3v3` were read from the die's own fuses on 2026-08-23 by a
different session on a different host, and the same unit answers the same way
here.

Before Attadipa was flashed, a complete 33 554 432-byte factory image was saved
on the development host and checked with `esptool verify-flash`. Its SHA-256 is
`c423dad3f0d33d56fa96f8590b3da583b05584e85bc2701a7c48c031ad747dbd`.
The durable evidence and restore procedure are in
[BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §2; the binary's local
path is intentionally not a repository artefact.

Attadipa T-165 subsequently booted from flash and produced its measured
acceptance transcript. Later the same day the complete backup was restored at
the owner's request because T-165 deliberately has no display/touch driver.
The write's integrated hash check succeeded; a separate full post-restore
`verify_flash` was interrupted and has no verdict. After reset the owner
observed the factory UI bright with working touch and set brightness to minimum.
The exact sequence is [BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §6.

Later on 2026-08-25, T-166 replaced the factory image with the current Attadipa
bench candidate and exercised its display, touch, PMU and RTC paths. After a
subsequent replug, the owner installed a FAT32-formatted microSD/TF card. The
card's physical presence and format are owner-reported bench state; mounting,
reading and writing it from Attadipa are `NOT EXECUTED — HARDWARE REQUIRED`.

**`F8:5B:1B:A1:98:24` is not ours to write to.** It is a MeshCore node somebody
is using, and the correct handling is to leave it alone. Name it by serial, not
by position: it used to be *the other one* of two, and it is now one of three.

**No single silicon property separates all three units any more**, and that is
worth stating plainly because the old text leaned on one. The chip reads
`ESP32-S3 (QFN56) rev v0.2` on all three. PSRAM ties the Waveshare to the
T-Watch at 8 MB `AP_3v3` and singles out only the node's 2 MB. Flash *capacity*
ties the T-Watch to the node at 16 MB and singles out only the Waveshare's
32 MB. What still splits all three is the **full JEDEC id including the vendor
byte** — `0xC8 0x4019`, `0xEF 0x4018`, `0x68 0x4018` — so a `flash_id`
cross-check retains its value, but only when the vendor byte is read and not
the capacity alone. The USB serial stays the identifier; the JEDEC id is a
post-hoc confirmation, never a substitute.

## The MeshCore nodes, over BLE rather than USB

The table above names one MeshCore node, and it names it by **USB serial**.
That is the right identifier for a board you might flash. It is not the
identifier a companion application sees, and over BLE this bench has **two**
MeshCore nodes, not one — `MEASURED` on 2026-08-28 during T-169 and recorded in
[MESHCORE_T114_FIRST_CONTACT](MESHCORE_T114_FIRST_CONTACT.md) §1a.

| | node A | node B |
| --- | --- | --- |
| model, from `RESP_CODE_DEVICE_INFO` | `Heltec T114` | `Heltec V4.3 OLED` |
| MeshCore firmware | `v1.17.1-d929643` | `v1.17.dev` |
| `RESP_CODE_SELF_INFO` name | `Beta test companion` | `✂️Beta Serega` |
| advertised name | `UNKNOWN` | `UNKNOWN` |
| **public key** | **`5c62d9bc82e530fc…`** | **`044e2de8068447d3…`** |
| negotiated ATT MTU | 247 | 176 |

**Neither has been tied to `F8:5B:1B:A1:98:24`.** A USB serial and a MeshCore
public key are different identifiers of different layers, and nothing measured
connects them. `MEASURED` from this host's kernel log: that serial last
enumerated on **2026-08-26 16:33:24** and disconnected 29 s later; no attachment
of it appears afterwards, and the log runs continuously across both T-169 bench
days, so it was not on this host's USB during any of the runs and could not be
compared against them. What that does *not* establish is where the board was
instead — the kernel log sees this host's USB and nothing else, so the unit may
have been powered elsewhere, on battery, or off. Whether either BLE node *is*
that unit is `UNKNOWN`. Do not close that gap by inference — read it off the
hardware when the node is next on USB.

**"Do not write" still means what it says, and BLE does not reach it.** The
prohibition on `F8:5B:1B:A1:98:24` is about writing its flash. T-169 used both
nodes over BLE as a companion application — pair, read device and contact info,
send a text — with passkeys the owner set for that purpose, and wrote nothing to
either board's flash. Those are different operations on different transports and
should not be read as one permitting the other.

**The firmware cannot yet tell you which node a log came from by address.** The
adapter connects to whichever Companion advertisement arrives first and does not
log the peer's BLE address, so identity is recovered from `RESP_CODE_SELF_INFO`
in that run's own transcript. That works, and it is application-layer evidence
rather than a link-layer one — but it means a run that fails before `SELF_INFO`
cannot say who it was talking to. Both halves are
[#304](https://github.com/hleserg/Attadipa/issues/304).

## How tools resolve it

`tools/flash/ramhold.py` looks up `/dev/serial/by-id` by USB serial and exits
non-zero when the unit is absent, rather than falling back to a port number.
`--serial` overrides it for a different unit; there is deliberately no "just
use the first ESP32 you find" path.

```
$ ls /dev/serial/by-id/          # 2026-08-27, the MeshCore node unplugged
usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B2:18:A4-if00 -> ../../ttyACM1
usb-Espressif_USB_JTAG_serial_debug_unit_DC:B4:D9:18:49:40-if00 -> ../../ttyACM0
```

That listing is from one moment on one host. **The serials are the durable
fact; the `ttyACM` numbers in it are not**, and are reproduced only to show what
the mapping looked like when it was recorded.

**Resolving the port is not the same as being able to drive it, and the two
units differ here.** The Waveshare accepts the CDC control-line requests every
esptool reset strategy is built on; the T-Watch refuses all of them with
`errno 71`, on a stable enumeration, with the same script on the same host. So
the Waveshare can be driven unattended and **the T-Watch cannot be put into
download mode by the host's flashing tools** — every load on it needs a hand on
the BOOT button, which sits on its GNSS daughterboard.

**The hand sequence that worked — MEASURED 2026-08-28:** unplug the micro-USB
cable, press and hold BOOT, plug the cable back in while still holding it, then
release BOOT. The screen stays dark. The vendor and Meshtastic recipe — hold
BOOT and click RESET — was tried repeatedly on this unit that day and never
produced an enumeration, although a RESET-based entry did work once on
2026-08-27. That is CONFLICTING and unexplained.

Once a hand has put it there, use `ramhold.py --connect-mode no_reset`, which
also opens the port with `rtscts`/`dsrdtr` so pyserial does not assert DTR/RTS
inside `open()`. The option alone is not enough: the refusal happens at the
open, before esptool sees the port. **That path loaded a RAM image and ran it —
MEASURED.** Whether the ROM in download mode refuses control lines too is still
`UNKNOWN`; the refusal was only ever measured with the factory application
running.
Measured with a same-host control on 2026-08-28:
[TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28](TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md)
§2. Why it refuses is `UNKNOWN`.

## What answers on the Waveshare's PMU bus — MEASURED 2026-08-28

Read with the `CONFIG_ATTADIPA_I2C_PROBE` build on SDA 15 / SCL 14 at 100 kHz,
loaded over the RAM route, writing nothing:

| Address | What it is |
| --- | --- |
| `0x18` | `UNKNOWN` |
| `0x34` | AXP2101 — the address the board driver already uses |
| `0x40` | `UNKNOWN` |
| `0x51` | PCF85063 — likewise `kPcf85063Address` in the board driver |
| `0x6b` | `UNKNOWN` |

`REG 0x64` on `0x34` read back `0x03`. That is **this** board's AXP2101, not the
T-Watch's — **D22 was closed separately, on the T-Watch itself, where the same
byte reads `0x04` = 4.35 V** ([TWATCH_S3_PLUS_DOWNLOAD_MODE](TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md) §8). This reading settles nothing
about D22; it is recorded because the same
byte on the other unit is the thing D22 asks for, and a reader deserves to know
what the instrument returns on a bus that is already understood.

The three `UNKNOWN` rows are addresses, not identifications: this scan read no
chip-ID register. [HARDWARE_MATRIX](HARDWARE_MATRIX.md) does name all three from
this board's schematic and from register reads made elsewhere — `0x18` ES8311,
`0x40` ES7210, `0x6b` QMI8658 — so `UNKNOWN` here means "not established *by
this instrument*", and the matrix is the canonical source.

## Two GNSS modules, delivered 2026-09-02 — NOT YET READ

The owner ordered two GNSS receiver boards for the wearable-node experiments
that [OD-1](OWNER_DECISIONS.md#od-1--there-is-a-separate-attadipa-node-and-the-watch-uses-it)
opens — a node carrying LoRa, GNSS and an ESP32, which the watch connects to.
They arrived on 2026-09-02. **Nothing below has been read off the hardware.**
Every value comes from the owner's four screenshots of the two marketplace
listings — title, product photo and description text — sent in the working
session of 2026-09-02 and recorded through
[#415](https://github.com/hleserg/Attadipa/pull/415); the marketplace itself
and the order references were not captured, so a claim that disagrees with
the board in hand cannot be re-checked against its listing later. A listing is
an advertisement, not a datasheet; until a module has been powered, its NMEA
read on a host and its chip identified from its own version sentence, each row
is `UNKNOWN` in the sense AGENTS.md means. This section exists so the next
session knows the boards are on the bench and does not order or assume a
third. The read-off is [H18](OPEN_QUESTIONS.md#hardware--measurement-required).

| | "GT-U12" | QUESCAN "AN3126" |
|---|---|---|
| Listing claims | dual-band GNSS; "new BDS SoC"; BDS, GPS, GLONASS, Galileo, IRNSS, QZSS, SBAS | u-blox **M10** platform; L1 only; GPS, GLONASS, Galileo, BeiDou; QZSS/SBAS; up to 25 Hz "in high-performance mode" |
| Chip | **UNKNOWN** — the listing names no part; "dual-band" and "BDS SoC" suggest an Allystar or Unicore die, and that is a guess | claimed u-blox M10; which module is a guess nobody has grounds for yet; **UNKNOWN** until `$GNTXT` / `UBX-MON-VER` is read |
| Header, as printed | `VCC GND TX RX PPS` (5 pins, photographed) | 5 pins; silkscreen not legible in the listing — **UNKNOWN** |
| Antenna | external: a separate active patch on its own carrier, on a u.FL pigtail | ceramic patch soldered to the board |
| Interface | UART presumed from the header; baud, protocol and level **UNKNOWN** | UART presumed; baud and level **UNKNOWN** |
| Supply | **UNKNOWN** — whether the carrier regulates, and what `VCC` wants, is not in the listing | **UNKNOWN** — same |
| Datasheet | none found yet | none found yet; u-blox publishes MIA-M10Q / MAX-M10S sheets, which apply only once the part is identified |
| Price paid | 1 596 ₽ | 791 ₽ |

What these are for, and what they are not: OD-1 puts GNSS on the **node**, not
the watch, and [HARDWARE_MATRIX](HARDWARE_MATRIX.md) already records the
T-Watch's own on-board receiver. These two boards are for bench experiments on
the node side — a first `PositionProvider` fed by a real receiver over UART, and
the questions OD-5 raises about a receiver's own integrity protection — and the
first thing owed is the read-off — and before any power, the supply: find the
regulator or its absence on each carrier and what `VCC` wants, because two
units with no spare do not survive a guess. Then a USB-UART bridge, a minute of
NMEA, and the `UNKNOWN`s above replaced with what the module says about
itself. Until then no code depends on either.

## What this does not say

The USB serial of an ESP32-S3's USB-Serial/JTAG peripheral is derived from the
factory MAC, so it identifies the *die*. It says nothing about which board that
die is soldered to, and a second Waveshare unit would have a different one. The
chip, PSRAM and flash columns are what tie this serial to this board; if a unit
is replaced, they are what to re-read.
