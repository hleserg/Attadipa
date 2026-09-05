#!/usr/bin/env python3
"""Refuse a second translation unit that stops the CPU or switches a rail.

[ADR-0016](../../docs/adr/0016-one-power-owner.md) section 1 says it plainly:
sleep entry, `esp_sleep_enable_*` and writes to AXP2101 registers `0x80`,
`0x90`, `0x82`, `0x92`-`0x95` and `0x96` "must appear in exactly one
translation unit, checked in CI". This is that check.

It exists because the rule it enforces was *already broken* when the ADR was
written, and broken in the way rules like this always are -- not by anybody
deciding to break it, but by two files growing independently. The sleep path
lived in `physical_input.cpp` and the rail writes in `waveshare_board.cpp`, and
nothing joined them; the research that found it
([POWER_OWNERSHIP.md](../../docs/research/POWER_OWNERSHIP.md)) also found that
`esp_sleep_disable_wakeup_source` appeared exactly once in the whole tree while
`esp_sleep_enable_*` appeared four times.

## What it can and cannot see

This reads text. It is a tripwire, not a proof:

* It **will** catch a new file that calls `esp_light_sleep_start()`, arms a wake
  source, or writes a rail register through anything that reads as a write.
* It will **not** catch a call reached through a function pointer, a macro that
  hides the symbol, or a rail written by a library this project links. Those are
  the reasons the owner is also an architectural boundary and not only a grep.
* It will **not** catch a rail write whose register and write verb are on
  different lines -- `{0x93, 0x1C}` built into an array on one line and passed
  to `write_reg()` on the next reads as neither. That is the price of the
  one-line rule below, which is what keeps a UUID byte from being reported as a
  rail write.
* It will **not** catch a write verb that does not *begin* an identifier.
  `WRITE_VERB` opens with a word boundary, so `i2c_master_transmit(pmu, 0x93,
  2)` reads as no write at all even with the register on the same line -- the
  underscore before `transmit` is a word character, so there is no boundary
  there. What this check actually sees is the `write_reg()` wrapper this tree
  happens to use. Both holes are real rather than theoretical.

The allowed file is named here rather than inferred, because "the file with the
most sleep calls in it" is not a rule anybody can rely on.

    python3 tools/flash/one_power_owner.py [ROOT]
    python3 tools/flash/one_power_owner.py --selftest

There is deliberately no flag to exempt a file. Moving the owner means editing
`OWNER` in one commit, and the diff of that commit is the record.

A second board is not a second owner, and the temptation when this first fails
for the T-Watch will be to make `OWNER` a list. Don't: a list is the rule saying
"one per file I happened to name", which is not a rule. One board is compiled at
a time, so the check should then read which board the build selected and hold
that board's file to the same single-owner rule -- the same invariant, asked of
the tree that is actually being built. That is a change worth making when the
second backend exists and not before; ADR-0017 is where the backend boundary is
decided, and `firmware/main/CMakeLists.txt` is where the selection will appear.
"""

from __future__ import annotations

import contextlib
import io
import re
import sys
from pathlib import Path

# The one translation unit, relative to the repository root.
OWNER = "firmware/main/board_power.cpp"

# Where firmware sources live. Everything under a build directory or a fetched
# component is somebody else's code and is not ours to hold to this rule.
SEARCH_ROOTS = ("firmware/main",)
SKIP_DIR_MARKERS = ("build", "managed_components", "dependencies")

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp")

# Stopping the CPU, and arming or disarming what brings it back. These names are
# unambiguous: nothing else in ESP-IDF is spelled this way.
SLEEP_PATTERNS = (
    ("sleep entry", re.compile(r"\besp_(light|deep)_sleep_start\s*\(")),
    ("sleep entry", re.compile(r"\besp_deep_sleep\s*\(")),
    ("wake arming", re.compile(r"\besp_sleep_enable_\w+\s*\(")),
    ("wake arming", re.compile(r"\besp_sleep_disable_wakeup_source\s*\(")),
    ("wake arming", re.compile(r"\bgpio_wakeup_(enable|disable)\s*\(")),
)

# The AXP2101 registers ADR-0016 names: DC enable, LDO enable, DC1 voltage, the
# ALDO1-4 voltages, and BLDO1's. BLDO1 (0x96) joined them when GNSS arrived,
# which the ADR's own Context said would happen -- `docs/adr/0016-one-power-owner.md:13`
# -- "GNSS arrives." It is the rail `PowerDomain::Gnss` resolves to, and until
# it was listed here it was the one rail register this firmware writes that a
# second translation unit could have set the *voltage* of unseen. Switching it
# always went through 0x90 and was always caught; its voltage was the gap, and
# every other rail's voltage register in this tuple is fenced.
RAIL_REGISTERS = ("0x80", "0x90", "0x82", "0x92", "0x93", "0x94", "0x95",
                  "0x96")

# A register literal on its own is not a rail write -- `0x93` is also a byte of
# a BLE UUID in this tree, which is exactly the false positive that would have
# made this check untrustworthy on its first run. A write verb has to be on the
# same line.
WRITE_VERB = re.compile(r"\b(write|transmit|set_reg|send)\w*\s*\(", re.IGNORECASE)
REGISTER_LITERAL = re.compile(
    r"(?<![0-9A-Za-z_])(" + "|".join(RAIL_REGISTERS) + r")(?![0-9A-Za-z_])"
)


def is_skipped(path: Path, root: Path) -> bool:
    parts = path.relative_to(root).parts
    return any(
        part == marker or part.startswith(marker + "-") or part.startswith(marker + ".")
        for part in parts
        for marker in SKIP_DIR_MARKERS
    )


def findings_in_text(text: str) -> list[tuple[int, str, str]]:
    """Every line that does something only the power owner may do."""
    found: list[tuple[int, str, str]] = []
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            # A comment naming the rule is not a breach of it. This file's own
            # prose would otherwise fail the check it defines.
            continue
        for what, pattern in SLEEP_PATTERNS:
            if pattern.search(line):
                found.append((number, what, stripped))
                break
        else:
            if WRITE_VERB.search(line) and REGISTER_LITERAL.search(line):
                found.append((number, "rail write", stripped))
    return found


def scan(root: Path) -> dict[str, list[tuple[int, str, str]]]:
    by_file: dict[str, list[tuple[int, str, str]]] = {}
    for search_root in SEARCH_ROOTS:
        base = root / search_root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if is_skipped(path, root):
                continue
            findings = findings_in_text(path.read_text(encoding="utf-8", errors="replace"))
            if findings:
                by_file[str(path.relative_to(root))] = findings
    return by_file


def report(by_file: dict[str, list[tuple[int, str, str]]]) -> int:
    strangers = {name: hits for name, hits in by_file.items() if name != OWNER}
    if not strangers:
        if OWNER not in by_file:
            print(
                f"One power owner: FAIL\n"
                f"  {OWNER} does not stop the CPU, arm a wake source or write a\n"
                f"  rail. Either the owner moved and OWNER here did not, or the\n"
                f"  firmware lost its sleep path entirely. Both need a human.",
                file=sys.stderr,
            )
            return 1
        print(f"One power owner: OK ({len(by_file[OWNER])} operations, all in {OWNER})")
        return 0

    print("One power owner: FAIL", file=sys.stderr)
    print(
        "  ADR-0016 section 1: sleep entry, esp_sleep_enable_* and AXP2101 rail\n"
        "  writes live in exactly one translation unit. These are elsewhere:",
        file=sys.stderr,
    )
    for name, hits in sorted(strangers.items()):
        for number, what, line in hits:
            print(f"    {name}:{number}  [{what}]  {line}", file=sys.stderr)
    print(
        f"\n  Route it through attadipa::core::PowerOwner instead, or move the\n"
        f"  operation into {OWNER}.",
        file=sys.stderr,
    )
    return 1


def selftest() -> int:
    failures = 0

    def check(condition: bool, what: str) -> None:
        nonlocal failures
        if not condition:
            print(f"selftest FAIL: {what}", file=sys.stderr)
            failures += 1

    check(findings_in_text("  esp_light_sleep_start();") != [], "sleep entry is caught")
    check(
        findings_in_text("r = esp_sleep_enable_timer_wakeup(us);") != [],
        "wake arming is caught",
    )
    check(
        findings_in_text("gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);") != [],
        "a GPIO wake source is caught",
    )
    check(
        findings_in_text("write_reg(pmu, 0x93, 0x1C);") != [],
        "a rail write is caught",
    )
    # The false positive that would have made this check untrustworthy: this is
    # a real line from meshcore_ble.cpp, and 0x93 in it is a byte of a UUID.
    check(
        findings_in_text("    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);") == [],
        "a register-shaped byte in a UUID is not a rail write",
    )
    check(
        findings_in_text("const std::uint8_t kAxpInterruptStatus2 = 0x49;") == [],
        "an interrupt register is not a rail register",
    )
    check(
        findings_in_text("  write_reg(pmu, kAxpInterruptStatus2, edges);") == [],
        "writing the interrupt-status register is allowed anywhere",
    )
    check(
        findings_in_text("// esp_light_sleep_start() is the owner's, not yours") == [],
        "a comment naming the rule does not breach it",
    )
    def quiet_report(by_file: dict[str, list[tuple[int, str, str]]]) -> int:
        # report() is meant to be read by a person. Here it is the thing under
        # test, and its own output would read as three CI failures.
        with contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            return report(by_file)

    check(quiet_report({OWNER: [(1, "sleep entry", "x")]}) == 0, "the owner alone passes")
    check(
        quiet_report(
            {OWNER: [(1, "sleep entry", "x")], "other.cpp": [(2, "rail write", "y")]}
        ) == 1,
        "a second file fails",
    )
    check(quiet_report({}) == 1, "an owner that does nothing fails")

    if failures:
        print(f"{failures} selftest check(s) failed", file=sys.stderr)
        return 1
    print("one_power_owner selftest: OK")
    return 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()
    root = Path(argv[1] if len(argv) > 1 else ".").resolve()
    return report(scan(root))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
