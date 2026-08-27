# Which device is which on the bench

> **Status:** read off the hardware, 2026-08-25 and 2026-08-27, on the
> development host.
> Every value below came from `esptool flash-id` and `udevadm`; nothing here is
> inferred from a product name.

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
download mode by the host's flashing tools** — every load on it needs a hand
holding BOOT while pressing RESET, and both buttons sit on its GNSS
daughterboard. Once a hand has put it there, `ramhold.py --connect-mode
no_reset` is the mode to reach for: it is the only strategy that got as far as
transmitting on this unit, because every other one toggles the lines before it
sends a byte. Whether the ROM in download mode refuses those lines too is
`UNKNOWN` — the refusal was only ever measured with the factory application
running, and this unit's USB behaviour is stateful. That the load then succeeds
is `NOT EXECUTED — HARDWARE REQUIRED`.
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
T-Watch's, and it settles nothing about D22; it is recorded because the same
byte on the other unit is the thing D22 asks for, and a reader deserves to know
what the instrument returns on a bus that is already understood.

The three `UNKNOWN` rows are addresses, not identifications. Naming them needs a
schematic for this board revision, and none has been read.

## What this does not say

The USB serial of an ESP32-S3's USB-Serial/JTAG peripheral is derived from the
factory MAC, so it identifies the *die*. It says nothing about which board that
die is soldered to, and a second Waveshare unit would have a different one. The
chip, PSRAM and flash columns are what tie this serial to this board; if a unit
is replaced, they are what to re-read.
