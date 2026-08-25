#!/usr/bin/env python3
"""Reject flash and partition code that reached a PURE_RAM firmware ELF."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


FORBIDDEN_PREFIXES = ("esp_partition_",)
FORBIDDEN_NAMES = {"esp_flash_read_id"}


def forbidden_symbols(nm_output: str) -> list[str]:
    names = []
    for line in nm_output.splitlines():
        fields = line.split(maxsplit=2)
        if (len(fields) == 3
                and (fields[2].startswith(FORBIDDEN_PREFIXES)
                     or fields[2] in FORBIDDEN_NAMES)):
            names.append(fields[2])
    return sorted(set(names))


def self_test() -> int:
    clean = "40370000 T app_main\n"
    bad = clean + "40370100 T esp_partition_find\n40370200 T esp_flash_read_id\n"
    if forbidden_symbols(clean):
        print("FAIL: clean ELF symbols were rejected")
        return 1
    if forbidden_symbols(bad) != ["esp_flash_read_id", "esp_partition_find"]:
        print("FAIL: forbidden symbols were not identified")
        return 1
    print("PURE_RAM ELF checker self-test: 2 cases passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="?", type=Path)
    parser.add_argument("--nm", default="xtensa-esp32s3-elf-nm")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.elf is None:
        parser.error("ELF is required unless --self-test is used")

    result = subprocess.run(
        [args.nm, "-C", "--defined-only", str(args.elf)],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(result.stderr.strip() or f"{args.nm} failed with exit {result.returncode}")
        return 1
    forbidden = forbidden_symbols(result.stdout)
    if forbidden:
        for symbol in forbidden:
            print(f"PURE_RAM ELF contains forbidden diagnostic symbol: {symbol}")
        return 1
    print(f"PURE_RAM ELF has no forbidden flash/partition symbols: {args.elf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
