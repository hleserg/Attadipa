#!/usr/bin/env python3
"""Prove that every required Attadipa library contributed code to a firmware ELF."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


REQUIRED_SYMBOLS = {
    "attadipa_core": "attadipa::core::to_string(attadipa::core::ResetReason)",
    "attadipa_platform": "attadipa::platform::find_board_profile(char const*)",
    "attadipa_link": "attadipa::link::LinkState::reset()",
    "attadipa_l10n": "attadipa::l10n::tr(attadipa::l10n::StringId)",
}


def defined_symbols(nm_output: str) -> set[str]:
    symbols = set()
    for line in nm_output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3:
            symbols.add(fields[2])
    return symbols


def missing_libraries(nm_output: str) -> list[str]:
    symbols = defined_symbols(nm_output)
    return [library for library, symbol in REQUIRED_SYMBOLS.items() if symbol not in symbols]


def self_test() -> int:
    complete = "\n".join(
        f"40370000 T {symbol}" for symbol in REQUIRED_SYMBOLS.values()
    )
    if missing_libraries(complete):
        print("FAIL: complete firmware symbols were rejected")
        return 1
    for library, symbol in REQUIRED_SYMBOLS.items():
        mutated = complete.replace(f"40370000 T {symbol}", "")
        if missing_libraries(mutated) != [library]:
            print(f"FAIL: removing {library} was not detected")
            return 1
    print("firmware ELF checker self-test: 5 cases passed")
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
    missing = missing_libraries(result.stdout)
    if missing:
        for library in missing:
            print(f"firmware ELF has no required symbol from {library}: "
                  f"{REQUIRED_SYMBOLS[library]}")
        return 1
    print(f"firmware ELF contains all required Attadipa libraries: {args.elf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
