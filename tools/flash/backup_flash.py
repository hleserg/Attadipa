#!/usr/bin/env python3
"""Read the whole flash off the watch, in chunks, and refuse a short one.

    python3 tools/flash/backup_flash.py ~/attadipa-bench/factory.bin

This is the asset the flash route rests on. OD-19 lets a bench session flash the
board because flashing it is *reversible*, and the only thing that makes it
reversible is a byte-verified image of what was there before. A backup that is
subtly wrong is worse than none, because it is the thing somebody will trust at
the moment they most need it to be right.

**Why chunks, and why the size check is the whole point.** `esptool` writes its
output file incrementally, so a read that dies partway still leaves a plausible
file behind — right kind of thing, wrong length, and everything after the first
failure shifted. Concatenating those does not announce itself.
docs/research/WAVESHARE_EFUSE_READ.md §2.4 is the account of that trap; this
refuses to append a chunk that is not exactly its nominal size.

**A read can be interrupted by things that have nothing to do with the board.**
The first attempt at this backup died at 7% with `Packet content transfer
stopped` while a large `git clone` was running — and on this host the root
filesystem is a USB-SATA disk sharing a controller with the board. So a failed
chunk is retried rather than fatal, and the reason to look at first is host load
rather than the flash.

Verification is not optional and is not this script's opinion: `esptool
verify-flash` compares by on-chip MD5 over the range, so it costs seconds and it
is the only evidence that counts. A successful read is not it.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ramhold import DEFAULT_SERIAL, resolve_port  # noqa: E402

FLASH_SIZE = 0x2000000    # 32 MB — GD25Q256, docs/research/HARDWARE_MATRIX.md
CHUNK = 0x200000          # 2 MB, the size the 2026-08-23 session used

# The image three independent reads of this unit have agreed on
# (docs/research/WAVESHARE_FLASH_LAYOUT.md §2). A fresh backup that matches it
# says the board is still as it was found; one that does not is not necessarily
# wrong — anything written since would change it — but it is worth knowing which.
KNOWN_FACTORY_SHA256 = "2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5"


def esptool(python: str, port: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([python, "-m", "esptool", "--port", port, *args],
                          capture_output=True, text=True)


def read_chunk(python: str, port: str, offset: int, size: int, path: Path,
               attempts: int) -> None:
    """One chunk, retried, and never accepted at the wrong length."""
    for attempt in range(1, attempts + 1):
        path.unlink(missing_ok=True)
        result = esptool(python, port, "read-flash",
                         str(offset), str(size), str(path))
        actual = path.stat().st_size if path.exists() else 0
        if result.returncode == 0 and actual == size:
            return
        print(f"  chunk at 0x{offset:07x}: attempt {attempt} of {attempts} gave "
              f"{actual} of {size} bytes"
              f"{'' if result.returncode == 0 else ' (esptool failed)'}",
              file=sys.stderr)
        tail = result.stderr.strip().splitlines()
        if tail:
            print(f"    {tail[-1]}", file=sys.stderr)
    raise SystemExit(
        f"chunk at 0x{offset:07x} never read cleanly in {attempts} attempts.\n"
        f"Nothing was assembled — a short chunk is a failed read, not a small one.\n"
        f"Check host load first: a busy USB bus stops these transfers."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", type=Path, help="where to write the image")
    parser.add_argument("--serial", default=DEFAULT_SERIAL)
    parser.add_argument("--port", default=None)
    parser.add_argument("--size", type=lambda v: int(v, 0), default=FLASH_SIZE)
    parser.add_argument("--chunk", type=lambda v: int(v, 0), default=CHUNK)
    parser.add_argument("--attempts", type=int, default=4)
    parser.add_argument("--python", default=sys.executable,
                        help="the interpreter that has esptool installed")
    parser.add_argument("--no-verify", action="store_true",
                        help="skip verify-flash. There is no good reason to")
    args = parser.parse_args()

    if args.size % args.chunk:
        raise SystemExit(f"size 0x{args.size:x} is not a whole number of "
                         f"0x{args.chunk:x} chunks")

    port = args.port or resolve_port(args.serial)
    scratch = args.output.parent / f".{args.output.name}.chunks"
    scratch.mkdir(parents=True, exist_ok=True)
    print(f"# port {port}\n# reading 0x{args.size:x} bytes in "
          f"0x{args.chunk:x} chunks", flush=True)

    parts = []
    for offset in range(0, args.size, args.chunk):
        part = scratch / f"{offset:08x}.bin"
        read_chunk(args.python, port, offset, args.chunk, part, args.attempts)
        parts.append(part)
        print(f"# 0x{offset:07x} ok  "
              f"({(offset + args.chunk) * 100 // args.size}%)", flush=True)

    digest = hashlib.sha256()
    with args.output.open("wb") as out:
        for part in parts:
            data = part.read_bytes()
            # Belt and braces: the length was checked when the chunk was read,
            # and it is checked again here because the file has been on disk in
            # between and this is the assembly step §2.4 is about.
            if len(data) != args.chunk:
                raise SystemExit(f"{part} changed size between read and assembly")
            out.write(data)
            digest.update(data)

    written = args.output.stat().st_size
    if written != args.size:
        raise SystemExit(f"assembled {written} bytes, expected {args.size}")
    for part in parts:
        part.unlink()
    scratch.rmdir()

    sha = digest.hexdigest()
    print(f"\n{written} bytes  sha256 {sha}")
    if sha == KNOWN_FACTORY_SHA256:
        print("# matches the recorded factory image — the part is as it was found")
    else:
        print("# does NOT match the recorded factory image "
              f"({KNOWN_FACTORY_SHA256}).\n"
              "# Expected if anything has been written since; investigate if not.")

    if args.no_verify:
        print("# NOT VERIFIED against the device — this image is not yet a backup")
        return 0

    print("# verify-flash against the device (on-chip MD5, seconds not minutes)",
          flush=True)
    result = esptool(args.python, port, "verify-flash", "0x0", str(args.output))
    ok = "successful" in (result.stdout + result.stderr).lower()
    print(result.stdout.strip()[-2000:] or result.stderr.strip()[-2000:])
    if not ok:
        raise SystemExit("verify-flash did not report success. The file on disk "
                         "is not a backup until it does.")
    print("# VERIFIED — this image restores the board")
    return 0


if __name__ == "__main__":
    sys.exit(main())
