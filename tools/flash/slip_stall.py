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
ends a transfer on a *short* packet. When the encoded length is an exact
multiple of 64 there is no short packet and the device sends no zero-length one,
so the last bytes sit in an incomplete URB while the device waits for an
acknowledgement that cannot come. Both sides are then waiting for each other and
esptool times out. 4098 % 64 == 2, so the condition is `escapes % 64 == 62` —
which is why *density* was the wrong test: the densest block in a chunk read
fine at 70 escapes, and a block with exactly 62 stalled.

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
USB_PACKET = 64       # full-speed bulk endpoint, and the whole of the problem


def stalls(block: bytes) -> bool:
    """True if this block's SLIP packet is an exact multiple of the USB packet."""
    escapes = block.count(0xC0) + block.count(0xDB)
    return (len(block) + escapes + FRAMING) % USB_PACKET == 0


def scan(image: bytes) -> list[tuple[int, int]]:
    """Every stalling block, as (address, escape count)."""
    return [(off, image[off:off + BLOCK].count(0xC0) + image[off:off + BLOCK].count(0xDB))
            for off in range(0, len(image), BLOCK)
            if stalls(image[off:off + BLOCK])]


def self_test() -> int:
    # 62 escapes stalls, 61 and 63 do not: the condition is a congruence, not a
    # threshold, and a threshold is what the rejected density hypothesis was.
    for escapes, expected in ((62, True), (61, False), (63, False), (126, True), (0, False)):
        block = bytes([0xC0]) * escapes + bytes([0x00]) * (BLOCK - escapes)
        assert stalls(block) is expected, f"{escapes} escapes: expected {expected}"
    # 0xDB escapes exactly like 0xC0 and the two are counted together.
    assert stalls(bytes([0xDB]) * 31 + bytes([0xC0]) * 31 + bytes(BLOCK - 62))
    print("self-test ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=Path, nargs="?")
    parser.add_argument("--chunk", type=lambda v: int(v, 0), default=0x200000,
                        help="chunk size a reader would use, for the abort list")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.image is None:
        parser.error("an image, or --self-test")

    image = args.image.read_bytes()
    found = scan(image)
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
