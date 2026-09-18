#!/usr/bin/env python3
"""Prove a firmware ELF is the image it claims to be.

Three questions, each answered from the linked artefact rather than from a
configuration file that may not be the one the toolchain read:

* did every required Attadipa library contribute code,
* is the unauthenticated USB watch-control endpoint present or absent, as this
  variant requires, and
* is this the board's image at all.

The last one is asked because a destructive writer needs it. Both boards emit
the same three filenames into the same flash plan, so an ESP-IDF build
directory does not say which board it was composed for -- the linked entry
point does, and `app_elf_sha256()` binds it to the `.bin` that will be written.

Run on a build directory's `attadipa.elf`, this also checks that binding
against the `attadipa.bin` beside it, and says in the last line of its output
which of the two it did. CI already calls this on five real builds -- both
boards, the HIL image and both pure-RAM ones -- so the descriptor arithmetic
meets the toolchain's own output on every push rather than only fixtures that
were written to agree with it.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


REQUIRED_SYMBOLS = {
    "attadipa_core": "attadipa::core::to_string(attadipa::core::ResetReason)",
    "attadipa_platform": "attadipa::platform::find_board_profile(char const*)",
    "attadipa_link": "attadipa::link::LinkState::reset()",
    "attadipa_l10n": "attadipa::l10n::tr(attadipa::l10n::StringId)",
}

# The pure-RAM probe and T-Watch image disable Bluetooth; the probe because
# bonds live in NVS and it promises to touch no flash, and T-Watch because
# sdkconfig.twatch scopes slice #417 to panel, touch and their rails. CI checks
# the generated CONFIG_BT_ENABLED state immediately before selecting this
# variant. These images therefore have no attadipa_link consumer. Requiring the
# library would only keep dead scaffolding alive to pass its own check. The
# Waveshare flash image still requires all four.
VARIANT_EXEMPTIONS = {
    "flash": frozenset(),
    "pure-ram": frozenset({"attadipa_link"}),
    "twatch": frozenset({"attadipa_link"}),
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
    "twatch": "absent",
    "hil": "present",
}


# WHICH BOARD THIS IMAGE IS, ASKED OF THE LINKED ARTEFACT AND NOT OF A FILE
# BESIDE IT. The two backends are mutually exclusive at compile time --
# `firmware/main/CMakeLists.txt:2` -- "if(CONFIG_ATTADIPA_BOARD_TWATCH_S3_PLUS)"
# -- and nothing chooses between them at runtime, so the board an image is for
# is settled before its bytes exist and is carried by exactly one linked entry
# point. A generated `sdkconfig` is a text file that can be copied next to
# somebody else's binary; this cannot.
BOARD_SYMBOLS = {
    "waveshare": "start_waveshare_ui()",
    "twatch": "start_twatch_ui()",
}

# `pure-ram` is unconstrained on purpose. The call to the board entry point is
# compiled out of that image -- `firmware/main/attadipa_main.cpp:316` --
# "#if !CONFIG_APP_BUILD_TYPE_PURE_RAM_APP" -- so `--gc-sections` is free to
# drop the function entirely, and requiring either symbol would be a claim
# about the linker rather than about the board. Nothing writes that image to
# flash: `ramhold.py` loads it into RAM, which is the whole point of it.
VARIANT_BOARD = {
    "flash": "waveshare",
    "pure-ram": None,
    "twatch": "twatch",
    "hil": "waveshare",
}

# The ESP-IDF application descriptor, `esp_app_desc_t`, sits at a fixed offset
# in an app image: 0x18 of image header plus 0x8 of the first segment header.
# `app_elf_sha256` is 0x90 into it, after the magic, the secure version, two
# reserved words and the version, project, time, date and IDF-version strings.
# The toolchain writes the SHA-256 of the `.elf` there when it makes the
# `.bin`, so the two are bound by the build that produced them and no manifest
# of ours has to invent that link.
APP_DESC_OFFSET = 0x20
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_ELF_SHA256 = 0x90

# WHICH VARIANTS CARRY THAT DESCRIPTOR AT ALL, MEASURED RATHER THAN ASSUMED.
# Every flash-type build directory on this bench -- nineteen of them, both
# boards, `hil` and `twatch` included -- has `APP_DESC_MAGIC` at
# `APP_DESC_OFFSET` and an `app_elf_sha256` that equals the SHA-256 of the
# `.elf` beside it. Every pure-RAM one has `0xB33FFFFF` there instead and no
# descriptor at that offset, so the binding cannot be asked of them. That is
# the same shape as the board exemption above and it rests on the same kind of
# evidence: a thing read off real artefacts, not a claim about the linker.
VARIANT_BINDS_APP = {
    "flash": True,
    "pure-ram": False,
    "twatch": True,
    "hil": True,
}


def app_elf_sha256(app: Path) -> str | None:
    """The SHA-256 of the ELF this app image was made from, or None."""
    end = APP_DESC_OFFSET + APP_DESC_ELF_SHA256 + 32
    head = app.read_bytes()[:end]
    if len(head) < end:
        return None
    magic = int.from_bytes(head[APP_DESC_OFFSET:APP_DESC_OFFSET + 4], "little")
    if magic != APP_DESC_MAGIC:
        return None
    return head[APP_DESC_OFFSET + APP_DESC_ELF_SHA256:end].hex()


def elf_sha256(elf: Path) -> str:
    return hashlib.sha256(elf.read_bytes()).hexdigest()


def binding_fault(elf: Path, variant: str = "flash") -> tuple[str | None, str]:
    """Is the app image beside this ELF the one the build made from it?

    THIS IS THE ONLY PLACE THE DESCRIPTOR ARITHMETIC MEETS A REAL ARTEFACT.
    `app_elf_sha256()` and its three constants are otherwise exercised only by
    fixtures that write the descriptor at the offset they then read it back
    from, which is self-consistent for any value: move `APP_DESC_ELF_SHA256`
    from 0x90 to 0x94 and every constructed case still passes while every real
    build is refused at the bench. So this runs against whatever the toolchain
    actually produced, and CI calls it on five real builds -- the two boards,
    the HIL image and both pure-RAM ones -- rather than on anything this file
    made up. An ESP-IDF release that moves the field turns those jobs red on
    the branch that bumps it.

    The second half of the answer says what was checked, and it is printed
    whether or not there was a fault: a check that can quietly not run is the
    one that rots, and `pure-ram` legitimately has nothing to check.
    """
    app = elf.with_name("attadipa.bin")
    if not VARIANT_BINDS_APP[variant]:
        return None, (f"a {variant} image carries no application descriptor, "
                      "so the ELF-to-image binding does not apply")
    if not app.is_file():
        return None, (f"there is no {app.name} beside it, so the "
                      "ELF-to-image binding was not checked")
    recorded = app_elf_sha256(app)
    linked = elf_sha256(elf)
    if recorded is None:
        return (f"{app} carries no ESP-IDF application descriptor, so nothing "
                f"ties it to {elf.name}. A {variant} image is built with one; "
                "an image assembled by hand, or by a toolchain this check has "
                "not been read against, is not something to trust a board "
                "to."), ""
    if recorded != linked:
        return (f"{app.name} was built from a different ELF than {elf.name}: "
                f"its descriptor records {recorded} and {elf.name} hashes to "
                f"{linked}. One of the two is stale."), ""
    return None, f"{app.name} beside it is bound to it by the build"


def board_fault(nm_output: str, variant: str = "flash") -> str | None:
    """Return a message if this ELF is not the board the variant names."""
    expected = VARIANT_BOARD[variant]
    if expected is None:
        return None
    symbols = defined_symbols(nm_output)
    linked = sorted(board for board, symbol in BOARD_SYMBOLS.items()
                    if symbol in symbols)
    if linked == [expected]:
        return None
    if not linked:
        return (f"this ELF links no board entry point at all, so it is not a "
                f"{variant} image: that one defines {BOARD_SYMBOLS[expected]}")
    return (f"this ELF is the {' and '.join(linked)} image — it defines "
            f"{', '.join(BOARD_SYMBOLS[board] for board in linked)} — and a "
            f"{variant} image defines {BOARD_SYMBOLS[expected]}")


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

    # THE BOARD RULE, BOTH WAYS ROUND, FOR EVERY VARIANT THAT HAS ONE. A
    # wrong-board image is exactly the mistake this rule exists for -- one
    # build directory typed instead of another -- and it has to be refused
    # whether the artefact carries the other board's entry point or neither.
    for variant, expected in VARIANT_BOARD.items():
        right = complete + f"\n40390000 T {BOARD_SYMBOLS[expected]}" \
            if expected is not None else complete
        if board_fault(right, variant) is not None:
            print(f"FAIL: {variant} rejected its own board's image")
            return 1
        cases += 1
        if expected is None:
            continue
        for other, symbol in BOARD_SYMBOLS.items():
            if other == expected:
                continue
            if board_fault(complete + f"\n40390000 T {symbol}", variant) is None:
                print(f"FAIL: {variant} accepted a {other} image")
                return 1
            # Both linked is not "close enough": the backends are mutually
            # exclusive, so an ELF defining both is not a build of either.
            if board_fault(right + f"\n403a0000 T {symbol}", variant) is None:
                print(f"FAIL: {variant} accepted an image linking both boards")
                return 1
            cases += 2
        if board_fault(complete, variant) is None:
            print(f"FAIL: {variant} accepted an image with no board at all")
            return 1
        cases += 1

    # And the ELF-to-image binding, from bytes rather than from a description
    # of them: a descriptor with the wrong magic is not a proof, and a short
    # file is not one either.
    descriptor = bytearray(b"\xe9" * (APP_DESC_OFFSET + APP_DESC_ELF_SHA256 + 32))
    descriptor[APP_DESC_OFFSET:APP_DESC_OFFSET + 4] = \
        APP_DESC_MAGIC.to_bytes(4, "little")
    descriptor[APP_DESC_OFFSET + APP_DESC_ELF_SHA256:] = bytes(range(32))
    import tempfile
    with tempfile.TemporaryDirectory() as scratch:
        app = Path(scratch) / "attadipa.bin"
        app.write_bytes(bytes(descriptor))
        if app_elf_sha256(app) != bytes(range(32)).hex():
            print("FAIL: the recorded ELF digest was not read back")
            return 1
        cases += 1
        app.write_bytes(bytes(descriptor[:APP_DESC_OFFSET]) +
                        b"\x00\x00\x00\x00" +
                        bytes(descriptor[APP_DESC_OFFSET + 4:]))
        if app_elf_sha256(app) is not None:
            print("FAIL: a file with no application descriptor was read as one")
            return 1
        cases += 1
        app.write_bytes(bytes(descriptor[:-1]))
        if app_elf_sha256(app) is not None:
            print("FAIL: a truncated descriptor was read as a proof")
            return 1
        cases += 1

        # `binding_fault()` on the same bytes. These cases are self-consistent
        # for any offset, which is the whole reason the real call in `main()`
        # exists -- they check the branching, not the arithmetic.
        elf = Path(scratch) / "attadipa.elf"
        elf.write_bytes(b"an ELF")
        app.write_bytes(bytes(descriptor))
        wrong, said = binding_fault(elf, "flash")
        if wrong is None or "different ELF" not in wrong:
            print("FAIL: a descriptor recording another ELF was accepted")
            return 1
        cases += 1
        bound = bytearray(descriptor)
        bound[APP_DESC_OFFSET + APP_DESC_ELF_SHA256:] = \
            bytes.fromhex(elf_sha256(elf))
        app.write_bytes(bytes(bound))
        wrong, said = binding_fault(elf, "flash")
        if wrong is not None or "bound to it" not in said:
            print(f"FAIL: a bound pair was refused: {wrong}")
            return 1
        cases += 1
        # Every variant that claims the binding refuses a missing descriptor,
        # and the one that does not claim it says so instead of staying quiet.
        app.write_bytes(b"\xe9" * 0x200)
        for variant, binds in VARIANT_BINDS_APP.items():
            wrong, said = binding_fault(elf, variant)
            if binds and wrong is None:
                print(f"FAIL: {variant} accepted an image with no descriptor")
                return 1
            if not binds and (wrong is not None or "does not apply" not in said):
                print(f"FAIL: {variant} claimed a binding it cannot have")
                return 1
            cases += 1
        app.unlink()
        wrong, said = binding_fault(elf, "flash")
        if wrong is not None or "not checked" not in said:
            print("FAIL: a missing image was not reported as unchecked")
            return 1
        cases += 1

    print(f"firmware ELF checker self-test: {cases} cases passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", nargs="?", type=Path)
    parser.add_argument("--nm", default="xtensa-esp32s3-elf-nm")
    parser.add_argument("--variant", choices=sorted(VARIANT_EXEMPTIONS),
                        default="flash",
                        help="which image this ELF is; pure-ram and twatch "
                             "exempt libraries those variants legitimately drop, and "
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
    board = board_fault(result.stdout, args.variant)
    binding, bound = binding_fault(args.elf, args.variant)
    if missing or fault is not None or board is not None or binding is not None:
        for library in missing:
            print(f"firmware ELF has no required symbol from {library}: "
                  f"{REQUIRED_SYMBOLS[library]}")
        for message in (fault, board, binding):
            if message is not None:
                print(message)
        return 1
    expected = VARIANT_BOARD[args.variant]
    board_line = (f", and it is the {expected} board's image"
                  if expected is not None else "")
    print(f"firmware ELF contains every Attadipa library required of a "
          f"{args.variant} image, the USB watch-control endpoint is "
          f"{VARIANT_ENDPOINT[args.variant]} as that variant requires"
          f"{board_line}: {args.elf}")
    print(f"  {bound}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
