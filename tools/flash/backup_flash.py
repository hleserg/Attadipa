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

**A stub read that fails does not fail again differently.** `Packet content
transfer stopped` part-way through a chunk is a *content*-determined failure of
the device-to-host transfer path, reproducible to the same absolute flash
address from any starting offset — WAVESHARE_EFUSE_READ §2.2, and confirmed here
by three reads that all died at `0x023d000`. Repeating the same method is a
random walk with a budget attached, so the retry here **changes method**: the
stub first, the ROM loader (`--no-stub`) after, which reads what the stub
refuses. §2.3 is that recipe and this is it in code.

Verification is not optional and is not this script's opinion: `esptool
verify-flash` compares by on-chip MD5 over the range, so it costs seconds and it
is the only evidence that counts. A successful read is not it.
"""

from __future__ import annotations

import argparse
import hashlib
import re
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


def esptool(python: str, port: str, *args: str,
            rom: bool = False) -> subprocess.CompletedProcess:
    """`--no-stub` is a global option and has to precede the subcommand."""
    command = [python, "-m", "esptool", "--port", port]
    if rom:
        command.append("--no-stub")
    return subprocess.run([*command, *args], capture_output=True, text=True)


def stopped_at(output: str) -> str | None:
    """The block the stub died on, from its own progress line.

    esptool prints `Reading from 0x…` for the block it is about to fetch, so the
    last one printed names the block that killed the transfer. Recorded rather
    than acted on: which addresses do this is the open question of
    WAVESHARE_FLASH_LAYOUT §2.2, and a run that names them is worth more than one
    that only says a chunk failed.
    """
    seen = re.findall(r"Reading from (0x[0-9a-f]+)", output)
    return seen[-1] if seen else None


def read_chunk(python: str, port: str, offset: int, size: int, path: Path,
               attempts: int) -> None:
    """One chunk: the stub first, then the ROM loader. Never a wrong length."""
    for attempt in range(1, attempts + 1):
        rom = attempt > 1          # a deterministic failure needs a different method
        path.unlink(missing_ok=True)
        result = esptool(python, port, "read-flash",
                         str(offset), str(size), str(path), rom=rom)
        actual = path.stat().st_size if path.exists() else 0
        if result.returncode == 0 and actual == size:
            if rom:
                print(f"  chunk at 0x{offset:07x}: read by the ROM loader",
                      file=sys.stderr)
            return
        where = stopped_at(result.stdout + result.stderr)
        print(f"  chunk at 0x{offset:07x}: attempt {attempt} of {attempts} "
              f"({'ROM loader' if rom else 'stub'}) gave {actual} of {size} bytes"
              f"{'' if result.returncode == 0 else ' (esptool failed)'}"
              f"{f', stopped at {where}' if where else ''}",
              file=sys.stderr)
        tail = result.stderr.strip().splitlines()
        if tail:
            print(f"    {tail[-1]}", file=sys.stderr)
    raise SystemExit(
        f"chunk at 0x{offset:07x} never read cleanly in {attempts} attempts, "
        f"by either loader.\n"
        f"Nothing was assembled — a short chunk is a failed read, not a small one.\n"
        f"Above 0x1000000 the ROM loader is not a fallback: it warns that large "
        f"flash is unsupported, and those ranges are the stub's to read."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("output", type=Path, help="where to write the image")
    parser.add_argument("--serial", default=DEFAULT_SERIAL)
    parser.add_argument("--port", default=None)
    parser.add_argument("--size", type=lambda v: int(v, 0), default=FLASH_SIZE)
    parser.add_argument("--chunk", type=lambda v: int(v, 0), default=CHUNK)
    parser.add_argument("--attempts", type=int, default=3,
                    help="1 = stub only; later attempts use the ROM loader")
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
