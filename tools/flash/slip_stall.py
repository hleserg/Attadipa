#!/usr/bin/env python3
"""Predict, from an image, which 4 KB blocks stall an esptool *stub* read.

    python3 tools/flash/slip_stall.py ~/attadipa-bench/factory-2026-08-25.bin
    python3 tools/flash/slip_stall.py --self-test

`esptool read-flash` with the stub loader aborts part-way through this board's
flash — `Packet content transfer stopped` — reproducibly at the same absolute
addresses from any starting offset. Five of them were known and what they had in
common was recorded as `UNKNOWN`: not the byte histogram, not the host, not an
erratum (docs/research/WAVESHARE_FLASH_LAYOUT.md §2.2).

It is arithmetic, and this is it. The stub answers `read_flash` with one SLIP
packet per 4096-byte block and then waits for the host's four-byte
acknowledgement before sending the next, so each packet is a USB transfer on its
own. SLIP framing adds two `0xC0` delimiters and escapes every `0xC0` and `0xDB`
in the payload into two bytes, so the packet on the wire is

    4096 + escapes + 2 bytes,  escapes = count(0xC0) + count(0xDB)

A full-speed USB bulk endpoint carries 64 bytes per packet, and a host driver
hands data up when its read buffer fills or when a *short* packet ends the
transfer. Linux `cdc-acm` reads in buffers of twice the endpoint size — 128
bytes — so a packet whose encoded length is 64 more than a multiple of 128 ends
with a full 64-byte packet sitting in a half-filled buffer: nothing short
arrives to release it, the device is already waiting for an acknowledgement that
needs those bytes, and esptool times out. 4098 % 128 == 2, so the condition on
this host is `escapes % 128 == 62`.

**Which is why density was the wrong test, and why the address list is a fact
about the host as much as the flash.** The densest page in a chunk reads fine at
70 escapes; a page with exactly 62 stalls. And a page with 126 — a multiple of
64 but not of 128 plus 64 — *also* reads fine here, while the 2026-08-22 passes
on Windows and over USB/IP recorded `0x476000`, which is exactly such a page, as
one of their five failures. A host reading in 64-byte units stalls on every
multiple of 64; this one stalls on half of them. Both are the same mechanism
with a different buffer size, which is what `--host-read` is for.

The ROM loader (`--no-stub`) uses different packet sizes and is unaffected;
`tools/flash/backup_flash.py` falls back to it. This script is the check on the
explanation rather than part of the backup: run it over an image and the
addresses it names are the addresses a stub read of that image aborts at.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

BLOCK = 4096          # esptool's FLASH_SECTOR_SIZE — one SLIP packet per block
FRAMING = 2           # the two 0xC0 delimiters
USB_PACKET = 64       # full-speed bulk endpoint
HOST_READ = 128       # Linux cdc-acm: readsize = 2 * wMaxPacketSize


def stalls(block: bytes, host_read: int = HOST_READ) -> bool:
    """True if this block's SLIP packet ends in a buffer nothing will release."""
    escapes = block.count(0xC0) + block.count(0xDB)
    return (len(block) + escapes + FRAMING) % host_read == USB_PACKET % host_read


def scan(image: bytes, host_read: int = HOST_READ) -> list[tuple[int, int]]:
    """Every stalling block, as (address, escape count)."""
    return [(off, image[off:off + BLOCK].count(0xC0) + image[off:off + BLOCK].count(0xDB))
            for off in range(0, len(image), BLOCK)
            if stalls(image[off:off + BLOCK], host_read)]


def self_test() -> int:
    # Measured on the received unit, 2026-08-25: every one of these was read
    # block by block over USB-Serial/JTAG and either stalled or did not.
    # 62 escapes stalls and 126 does not, which is the whole difference between
    # this predicate and "an exact multiple of 64".
    for escapes, expected in ((62, True), (61, False), (63, False),
                              (126, False), (190, True), (0, False)):
        block = bytes([0xC0]) * escapes + bytes(BLOCK - escapes)
        assert stalls(block) is expected, f"{escapes} escapes: expected {expected}"
    # A host reading one packet at a time stalls on every multiple of 64 — the
    # shape the Windows and USB/IP passes saw.
    assert stalls(bytes([0xC0]) * 126 + bytes(BLOCK - 126), host_read=64)
    # 0xDB escapes exactly like 0xC0 and the two are counted together.
    assert stalls(bytes([0xDB]) * 31 + bytes([0xC0]) * 31 + bytes(BLOCK - 62))
    print("self-test ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=Path, nargs="?")
    parser.add_argument("--chunk", type=lambda v: int(v, 0), default=0x200000,
                        help="chunk size a reader would use, for the abort list")
    parser.add_argument("--host-read", type=lambda v: int(v, 0), default=HOST_READ,
                        help="the host's USB read granularity in bytes "
                             f"(default {HOST_READ}, Linux cdc-acm; 64 for a "
                             "host that reads one packet at a time)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.image is None:
        parser.error("an image, or --self-test")

    image = args.image.read_bytes()
    found = scan(image, args.host_read)
    print(f"# {args.image}: {len(image)} bytes, "
          f"{len(image) // BLOCK} blocks, {len(found)} that stall a stub read")
    for address, escapes in found:
        print(f"0x{address:07x}  {escapes} escapes")

    # What a chunked read actually sees: the transfer dies at the first stalling
    # block in its range, so only those addresses ever appear in a log.
    print(f"\n# a reader using 0x{args.chunk:x} chunks aborts at:")
    for start in range(0, len(image), args.chunk):
        first = next((a for a, _ in found if start <= a < start + args.chunk), None)
        if first is not None:
            print(f"chunk 0x{start:07x} -> 0x{first:07x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
