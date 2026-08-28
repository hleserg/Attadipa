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

# The pure-RAM probe is not a smaller version of the product: it disables
# Bluetooth (bonds live in NVS, and the image promises to touch no flash) and
# the watch-control endpoint, which between them are every consumer of
# attadipa_link in that variant. Requiring the link library there would only be
# satisfiable by carrying a LinkState the image never uses -- scaffolding kept
# alive to pass its own check. The flash image, which is the product, still
# requires all four.
VARIANT_EXEMPTIONS = {
    "flash": frozenset(),
    "pure-ram": frozenset({"attadipa_link"}),
}


def defined_symbols(nm_output: str) -> set[str]:
    symbols = set()
    for line in nm_output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3:
            symbols.add(fields[2])
    return symbols


def missing_libraries(nm_output: str, variant: str = "flash") -> list[str]:
    symbols = defined_symbols(nm_output)
    exempt = VARIANT_EXEMPTIONS[variant]
    return [library for library, symbol in REQUIRED_SYMBOLS.items()
            if library not in exempt and symbol not in symbols]


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
    cases = 1 + len(REQUIRED_SYMBOLS)
    # The exemption is narrow in both directions: the exempt library may be
    # absent from the RAM probe, and every other library still may not.
    for variant, exempt in VARIANT_EXEMPTIONS.items():
        for library in exempt:
            mutated = complete.replace(
                f"40370000 T {REQUIRED_SYMBOLS[library]}", "")
            if missing_libraries(mutated, variant):
                print(f"FAIL: {variant} did not exempt {library}")
                return 1
            if missing_libraries(mutated) != [library]:
                print(f"FAIL: flash stopped requiring {library}")
                return 1
            cases += 2
    print(f"firmware ELF checker self-test: {cases} cases passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="?", type=Path)
    parser.add_argument("--nm", default="xtensa-esp32s3-elf-nm")
    parser.add_argument("--variant", choices=sorted(VARIANT_EXEMPTIONS),
                        default="flash",
                        help="which image this ELF is; pure-ram exempts the "
                             "libraries that variant legitimately drops")
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
    missing = missing_libraries(result.stdout, args.variant)
    if missing:
        for library in missing:
            print(f"firmware ELF has no required symbol from {library}: "
                  f"{REQUIRED_SYMBOLS[library]}")
        return 1
    print(f"firmware ELF contains every Attadipa library required of a "
          f"{args.variant} image: {args.elf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
