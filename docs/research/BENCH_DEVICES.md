# Which device is which on the bench

> **Status:** read off the hardware, 2026-08-25, on the development host.
> Every value below came from `esptool flash-id` and `udevadm`; nothing here is
> inferred from a product name.

This document exists because of one sentence in
[WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §2 that was true and
had nowhere durable to live: **two ESP32-S3 boards on this bench both enumerate
as `303a:1001`, `USB JTAG/serial debug unit`, and the USB descriptor does not
say which is which.** `/dev/ttyACM0` and `/dev/ttyACM1` are assigned in
enumeration order, so they swap on a replug. Anything that picks a port by
number will eventually pick the wrong board.

## The two units

| | Watch | The other board |
|---|---|---|
| USB serial | **`28:84:85:B2:18:A4`** | `F8:5B:1B:A1:98:24` |
| `by-id` link | `usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B2:18:A4-if00` | `…_F8:5B:1B:A1:98:24-if00` |
| Chip | ESP32-S3 (QFN56) rev **v0.2** | ESP32-S3 (QFN56) rev v0.2 |
| PSRAM | **8 MB, `AP_3v3`** | 2 MB, `AP_3v3` |
| Flash | **`0xC8 0x4019` — GigaDevice, 32 MB** | `0x68 0x4018` — 16 MB |
| Identification | Waveshare `ESP32-S3-Touch-AMOLED-2.06` | a MeshCore node, per [#116](https://github.com/hleserg/Attadipa/issues/116) |
| Current firmware | **Attadipa T-166 bench candidate**; display at the measured 5% visible floor and physical touch working | unchanged; do not write |

The watch row matches [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.1–1.3
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

**The second board is not ours to write to.** It is nothing like the watch — a
quarter of the PSRAM and half the flash — so a mis-addressed load fails rather
than half-working, but it is a MeshCore node someone is using and the correct
handling is to leave it alone.

## How tools resolve it

`tools/flash/ramhold.py` looks up `/dev/serial/by-id` by USB serial and exits
non-zero when the unit is absent, rather than falling back to a port number.
`--serial` overrides it for a different unit; there is deliberately no "just
use the first ESP32 you find" path.

```
$ ls /dev/serial/by-id/
usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B2:18:A4-if00 -> ../../ttyACM1
usb-Espressif_USB_JTAG_serial_debug_unit_F8:5B:1B:A1:98:24-if00 -> ../../ttyACM0
```

That listing is from one moment on one host. **The serials are the durable
fact; the `ttyACM` numbers in it are not**, and are reproduced only to show what
the mapping looked like when it was recorded.

## What this does not say

The USB serial of an ESP32-S3's USB-Serial/JTAG peripheral is derived from the
factory MAC, so it identifies the *die*. It says nothing about which board that
die is soldered to, and a second Waveshare unit would have a different one. The
chip, PSRAM and flash columns are what tie this serial to this board; if a unit
is replaced, they are what to re-read.
