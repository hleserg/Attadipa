#!/usr/bin/env python3
"""Prove a firmware ELF is the image it claims to be.

Two questions, both answered from the linked artefact rather than from a
configuration file that may not be the one the toolchain read:

* did every required Attadipa library contribute code, and
* is the unauthenticated USB watch-control endpoint present or absent, as this
  variant requires.
"""

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
    "hil": frozenset(),
}

# The USB watch-control endpoint has no authentication and is not meant to be
# in a product image (#346). "Configured off" is not the property worth
# checking -- a stale sdkconfig, a stacked defaults file that did not take, or
# a future CMakeLists that links attadipa_debug for some other reason all leave
# the configuration looking right and the code in the binary. So this asks the
# ELF.
#
# Bridge::handle is the single function every privileged opcode is dispatched
# from: screenshot, input injection, time, mesh. If it is linked, all of them
# are reachable; if it is not, none of them are, whatever else is present.
DEBUG_ENDPOINT_SYMBOL = (
    "attadipa::debug::Bridge::handle(unsigned char const*, unsigned int, "
    "unsigned long, void (*)(void*, unsigned char const*, unsigned int), void*)"
)

# Absent for the two product images, and *required* for the HIL one -- so a
# development build that quietly lost its endpoint is caught by the same check,
# rather than passing as if it were a product.
VARIANT_ENDPOINT = {
    "flash": "absent",
    "pure-ram": "absent",
    "hil": "present",
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


def endpoint_fault(nm_output: str, variant: str = "flash") -> str | None:
    """Return a message if the endpoint's presence is wrong for this variant."""
    present = DEBUG_ENDPOINT_SYMBOL in defined_symbols(nm_output)
    required = VARIANT_ENDPOINT[variant]
    if required == "absent" and present:
        return (f"{variant} image links the USB watch-control dispatcher "
                f"({DEBUG_ENDPOINT_SYMBOL}); a product image must not. Build "
                f"it from sdkconfig.defaults alone, without sdkconfig.hil.")
    if required == "present" and not present:
        return (f"{variant} image does not link the USB watch-control "
                f"dispatcher ({DEBUG_ENDPOINT_SYMBOL}); the HIL image exists "
                f"to carry it. Was sdkconfig.hil stacked, and did it take?")
    return None


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
    # Both directions of the endpoint rule, for every variant, from the same
    # two symbol tables -- one carrying the dispatcher, one without it.
    with_endpoint = complete + f"\n40380000 T {DEBUG_ENDPOINT_SYMBOL}"
    for variant, required in VARIANT_ENDPOINT.items():
        wrong = with_endpoint if required == "absent" else complete
        right = complete if required == "absent" else with_endpoint
        if endpoint_fault(right, variant) is not None:
            print(f"FAIL: {variant} rejected a correct endpoint state")
            return 1
        if endpoint_fault(wrong, variant) is None:
            print(f"FAIL: {variant} accepted the wrong endpoint state")
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
                             "libraries that variant legitimately drops, and "
                             "hil is the only variant allowed to carry the USB "
                             "watch-control endpoint")
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
    fault = endpoint_fault(result.stdout, args.variant)
    if missing or fault is not None:
        for library in missing:
            print(f"firmware ELF has no required symbol from {library}: "
                  f"{REQUIRED_SYMBOLS[library]}")
        if fault is not None:
            print(fault)
        return 1
    print(f"firmware ELF contains every Attadipa library required of a "
          f"{args.variant} image, and the USB watch-control endpoint is "
          f"{VARIANT_ENDPOINT[args.variant]} as that variant requires: "
          f"{args.elf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
