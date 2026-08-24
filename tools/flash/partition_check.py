#!/usr/bin/env python3
"""Refuse a partition table that reaches past the 16 MB flash addressing ceiling.

The received Waveshare unit carries a 32 MB part, and half of it is not usable
by anything that has been proved to work. `ota_1` sits at exactly `0x1000000`
and the vendor's own second-stage bootloader cannot boot it: the address wraps
to `0x0` and the bootloader read its own image back. That is a **measurement**,
on this board, in
[`WAVESHARE_RUNNING_OUR_CODE.md`](../../docs/research/WAVESHARE_RUNNING_OUR_CODE.md)
section 1.

What follows from that measurement for a *running application* is a different
question, and it is answered nowhere by a measurement.
[`FLASH_ADDRESSING_LIMITS.md`](../../docs/research/FLASH_ADDRESSING_LIMITS.md)
traces every runtime path through ESP-IDF v5.5.5 and finds that exactly one of
them — `esp_partition_mmap` — refuses an address above the ceiling. Read, write
and erase do not: they emit four-byte-address SPI commands and hope, and the one
ESP-IDF guard that could have stopped them assigns to a local variable and is
therefore a log line rather than a check. Nothing on the board has ever executed
those paths above `0x1000000`.

So the rule this file enforces is: **until somebody measures it, nothing of ours
lives at or above `0x1000000`.** Not an app partition, not a data partition, not
a partition that merely ends up there because the one before it grew.

There is deliberately **no flag to switch this off.** When the capability is
measured, `ADDRESSING_CEILING` moves in one commit that cites the measurement,
and the diff of that commit is the record. A `--i-know-what-i-am-doing` option
would be used once in a hurry and then forever.

    python3 tools/flash/partition_check.py [FILE ...]

With no arguments it checks every `*partition*.csv` in the repository. It is
also the reason blank offsets are refused rather than computed — see
`parse_table` below.

This is **not** a replacement for ESP-IDF's `gen_esp32part.py`, which validates
far more and knows nothing about this ceiling. When there is a firmware project
here, run both. Until then this is the only thing that reads these files at all.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# The line the hardware draws. 24-bit SPI addressing wraps here, which is what
# the bootloader was measured doing. Moving this number is a hardware claim and
# needs the evidence CLAUDE.md asks for: a measurement on the specific board,
# not a datasheet and not a successful host-side tool.
ADDRESSING_CEILING = 0x1000000

# Flash erase granularity, and therefore the granularity at which a partition
# boundary means anything at all. A partition that starts mid-sector shares its
# first sector with its neighbour, and erasing either destroys both.
SECTOR_SIZE = 0x1000

# ESP-IDF requires app partitions to start on a 64 KiB boundary, because the
# MMU maps flash in pages of that size on the targets this project uses.
APP_ALIGNMENT = 0x10000

# Defaults matching ESP-IDF's own: the table lives at 0x8000 and occupies one
# sector, so nothing may start before 0x9000. Both are overridable because the
# Waveshare's vendor image does not use the default `otadata` offset either, and
# a checker that cannot describe the board in front of it is not much use.
DEFAULT_TABLE_OFFSET = 0x8000
DEFAULT_TABLE_SIZE = 0x1000
DEFAULT_FLASH_SIZE = 32 * 1024 * 1024

APP_TYPES = {"app", "0"}

SIZE = re.compile(r"^(0x[0-9a-fA-F]+|\d+)([KMkm]?)$")


class Row:
    """One partition, and where in the file it was written."""

    def __init__(self, path: Path, lineno: int, name: str, ptype: str,
                 subtype: str, offset: int, size: int) -> None:
        self.path = path
        self.lineno = lineno
        self.name = name
        self.type = ptype
        self.subtype = subtype
        self.offset = offset
        self.size = size

    @property
    def end(self) -> int:
        """One past the last byte. May exceed 2**32; see check_table."""
        return self.offset + self.size

    def where(self) -> str:
        return f"{self.path}:{self.lineno}: {self.name}"


def parse_number(text: str) -> int:
    """An ESP-IDF size or offset: decimal, 0x-hex, with an optional K or M."""
    match = SIZE.match(text.strip())
    if match is None:
        raise ValueError(f"{text!r} is not a number ESP-IDF would accept")
    value = int(match.group(1), 0)
    suffix = match.group(2).upper()
    if suffix == "K":
        value *= 1024
    elif suffix == "M":
        value *= 1024 * 1024
    return value


def parse_table(path: Path) -> tuple[list[Row], list[str]]:
    """Read one partition CSV. Returns the rows and any parse problems.

    Blank offsets are **refused rather than computed**, which is a deliberate
    departure from `gen_esp32part.py`. A blank offset means "wherever the
    previous partition happens to end", and that is precisely how a table
    silently drifts upward: somebody adds two megabytes to a partition halfway
    down and everything after it moves, with nothing in the diff to show it. The
    address is the thing this checker exists to police, so it has to be written
    down where a reader and a reviewer can both see it.

    A row this parser cannot read is an error and never a skip. A checker that
    quietly ignores what it does not understand reports success on the one file
    that needed it most.
    """
    rows: list[Row] = []
    problems: list[str] = []

    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue

        fields = [field.strip() for field in line.split(",")]
        # Name, Type, SubType, Offset, Size, and optionally Flags.
        if len(fields) not in (5, 6):
            problems.append(
                f"{path}:{lineno}: {len(fields)} fields, expected 5 or 6 "
                f"(Name, Type, SubType, Offset, Size[, Flags]): {raw.strip()!r}"
            )
            continue

        name, ptype, subtype, offset_text, size_text = fields[:5]
        if not offset_text:
            problems.append(
                f"{path}:{lineno}: {name or '<unnamed>'} has no offset. This "
                f"checker does not compute implicit offsets — write the offset "
                f"out, so that moving a partition is visible in the diff"
            )
            continue
        if not size_text:
            problems.append(f"{path}:{lineno}: {name or '<unnamed>'} has no size")
            continue

        try:
            offset = parse_number(offset_text)
            size = parse_number(size_text)
        except ValueError as exc:
            problems.append(f"{path}:{lineno}: {name or '<unnamed>'}: {exc}")
            continue

        rows.append(Row(path, lineno, name, ptype.lower(), subtype.lower(),
                        offset, size))

    return rows, problems


def check_table(rows: list[Row], flash_size: int, first_legal: int) -> list[str]:
    """Every rule, applied to every row. Reports all findings, not the first."""
    problems: list[str] = []

    for row in rows:
        if row.size == 0:
            problems.append(f"{row.where()}: size is zero")
            continue

        if row.offset % SECTOR_SIZE:
            problems.append(
                f"{row.where()}: offset {row.offset:#x} is not a multiple of "
                f"the {SECTOR_SIZE:#x}-byte erase sector"
            )
        if row.size % SECTOR_SIZE:
            problems.append(
                f"{row.where()}: size {row.size:#x} is not a multiple of the "
                f"{SECTOR_SIZE:#x}-byte erase sector"
            )
        if row.type in APP_TYPES and row.offset % APP_ALIGNMENT:
            problems.append(
                f"{row.where()}: an app partition must start on a "
                f"{APP_ALIGNMENT:#x} boundary, not {row.offset:#x}"
            )

        if row.offset < first_legal:
            problems.append(
                f"{row.where()}: starts at {row.offset:#x}, inside the "
                f"bootloader and partition table below {first_legal:#x}"
            )

        # Before anything compares addresses: a partition whose end does not fit
        # in 32 bits has already lost the argument. The arithmetic every check
        # below does would wrap on the device exactly as it wraps here.
        if row.end > 2 ** 32:
            problems.append(
                f"{row.where()}: {row.offset:#x} + {row.size:#x} = "
                f"{row.end:#x} overflows the 32-bit flash address space"
            )
            continue

        if row.end > flash_size:
            problems.append(
                f"{row.where()}: ends at {row.end:#x}, past the "
                f"{flash_size:#x} the part holds"
            )

        # The rule this file exists for, stated as two separate findings
        # because they are two different mistakes. One is a partition somebody
        # placed up there; the other is a partition that grew into it, which is
        # the one nobody sees in review.
        if row.offset >= ADDRESSING_CEILING:
            problems.append(
                f"{row.where()}: starts at {row.offset:#x}, at or above the "
                f"{ADDRESSING_CEILING:#x} addressing ceiling. Reading, erasing "
                f"and programming above it have never been executed on this "
                f"hardware — docs/research/FLASH_ADDRESSING_LIMITS.md"
            )
        elif row.end > ADDRESSING_CEILING:
            problems.append(
                f"{row.where()}: spans {row.offset:#x}..{row.end:#x} and so "
                f"crosses the {ADDRESSING_CEILING:#x} addressing ceiling. The "
                f"bytes above the line are the unverified half — "
                f"docs/research/FLASH_ADDRESSING_LIMITS.md"
            )

    ordered = sorted((row for row in rows if row.size), key=lambda r: r.offset)
    for earlier, later in zip(ordered, ordered[1:]):
        if later.offset < earlier.end:
            problems.append(
                f"{later.where()}: starts at {later.offset:#x}, inside "
                f"{earlier.name} which runs to {earlier.end:#x}"
            )

    return problems


def discover(root: Path) -> list[Path]:
    """Every partition table in the tree, excluding documents and fixtures.

    The fixtures are excluded on purpose: `fixtures/` holds tables that are
    *supposed* to fail, including the vendor's own, and sweeping them into the
    repository check would make the check permanently red for the wrong reason.
    tools/flash/selftest.py runs them, and asserts the refusal.
    """
    skip = {".git", "docs", "build"}
    found = []
    for path in sorted(root.rglob("*.csv")):
        parts = set(path.relative_to(root).parts)
        if parts & skip or "fixtures" in parts:
            continue
        if "partition" in path.name.lower():
            found.append(path)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="*", type=Path,
                        help="partition CSVs; default is every one in the tree")
    parser.add_argument("--flash-size", type=parse_number,
                        default=DEFAULT_FLASH_SIZE,
                        help="size of the part (default 32M)")
    parser.add_argument("--table-offset", type=parse_number,
                        default=DEFAULT_TABLE_OFFSET,
                        help="where the partition table itself sits "
                             "(default 0x8000)")
    parser.add_argument("--table-size", type=parse_number,
                        default=DEFAULT_TABLE_SIZE,
                        help="how much the table occupies (default 0x1000)")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    files = args.files or discover(root)

    if not files:
        # Not a pass dressed up as one. There is genuinely no firmware project
        # in this repository yet (issue #127 says so in its first paragraph),
        # so there is no table to check — and this check starts biting the
        # moment the first one lands, which is the point of registering it now
        # rather than then.
        print(f"partition_check: no partition tables under {root} "
              f"(looked for **/*partition*.csv). Nothing was checked.")
        return 0

    problems: list[str] = []
    total = 0
    for path in files:
        if not path.exists():
            problems.append(f"{path}: no such file")
            continue
        rows, parse_problems = parse_table(path)
        problems.extend(parse_problems)
        problems.extend(check_table(rows, args.flash_size,
                                    args.table_offset + args.table_size))
        total += len(rows)

    if problems:
        for problem in problems:
            print(problem)
        print(f"\npartition_check: {len(problems)} problem(s) in "
              f"{len(files)} table(s).")
        return 1

    print(f"partition_check: {total} partition(s) in {len(files)} table(s), "
          f"all below {ADDRESSING_CEILING:#x}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
