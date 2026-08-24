#!/usr/bin/env python3
"""Prove partition_check.py refuses what it claims to refuse.

Same argument as tools/ui/selftest.py and tools/l10n/selftest.py: a checker that
has only ever been run against a clean tree — and this one currently has no tree
to run against at all, because there is no ESP-IDF project here yet — is
indistinguishable from a checker that returns 0. Every case below is written the
way somebody would actually write it.

Four of them are the boundary itself, and they are four different mistakes:
a partition that starts on the line, one wholly above it, one that crosses it,
and one that ends exactly on it and is therefore *fine*. The last is the case a
checker written as `if offset + size >= CEILING` gets wrong, and it is the
common one — the vendor's own `ota_0` ends exactly on the line.

The vendor's shipped table is checked as a real input rather than a fixture
somebody invented, because a rule tested only against hand-made examples is a
rule tested against its author's imagination.
"""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CHECKER = HERE / "partition_check.py"
VENDOR_TABLE = HERE / "fixtures" / "waveshare-vendor-factory.csv"

HEADER = "# Name, Type, SubType, Offset, Size\n"

# Each case is (csv body, a phrase the refusal must contain). The phrase matters
# as much as the exit code: a checker that rejects everything for the wrong
# reason passes a test that only reads the status.
MUST_REJECT: dict[str, tuple[str, str]] = {
    "a partition starting exactly on the line": (
        "models, data, spiffs, 0x1000000, 4M\n",
        "at or above",
    ),
    "a partition wholly above the line": (
        "storage, data, spiffs, 0x1600000, 6M\n",
        "at or above",
    ),
    "a partition that grew across the line": (
        "models, data, spiffs, 0xf00000, 2M\n",
        "crosses",
    ),
    "a partition one sector past the line": (
        "models, data, spiffs, 0xfff000, 0x2000\n",
        "crosses",
    ),
    "an app partition above the line — the vendor's own dead slot": (
        "ota_1, app, ota_1, 0x1000000, 6M\n",
        "at or above",
    ),
    "an end that overflows the 32-bit address space": (
        "huge, data, spiffs, 0xfff00000, 0x200000\n",
        "overflows the 32-bit",
    ),
    "a partition past the end of the part": (
        "storage, data, spiffs, 0x1f00000, 4M\n",
        "past the",
    ),
    "an offset that is not on a sector boundary": (
        "nvs, data, nvs, 0x9800, 24K\n",
        "erase sector",
    ),
    "a size that is not a whole number of sectors": (
        "nvs, data, nvs, 0x9000, 0x1800\n",
        "erase sector",
    ),
    "an app partition on a 4 KiB rather than 64 KiB boundary": (
        "factory, app, factory, 0x101000, 4M\n",
        "app partition must start",
    ),
    "a partition sitting on top of the partition table": (
        "nvs, data, nvs, 0x8000, 24K\n",
        "inside the bootloader",
    ),
    "two partitions that overlap": (
        "nvs,  data, nvs,    0x9000, 24K\n"
        "misc, data, nvs,    0xe000, 8K\n",
        "inside nvs",
    ),
    "a row with a field missing": (
        "nvs, data, nvs, 0x9000\n",
        "expected 5 or 6",
    ),
    "an offset left blank for the generator to fill in": (
        "nvs, data, nvs, , 24K\n",
        "does not compute implicit offsets",
    ),
    "a size left blank": (
        "nvs, data, nvs, 0x9000, \n",
        "has no size",
    ),
    "an offset that is not a number": (
        "nvs, data, nvs, nine thousand, 24K\n",
        "not a number",
    ),
    "a size with a suffix ESP-IDF does not use": (
        "nvs, data, nvs, 0x9000, 24G\n",
        "not a number",
    ),
}

MUST_ACCEPT: dict[str, str] = {
    "a table that stops exactly on the line":
        "nvs,      data, nvs,     0x9000,   24K\n"
        "otadata,  data, ota,     0xf000,   8K\n"
        "phy_init, data, phy,     0x11000,  4K\n"
        "factory,  app,  factory, 0x20000,  4M\n"
        "ota_0,    app,  ota_0,   0x420000, 4M\n"
        "models,   data, spiffs,  0x820000, 0x7e0000\n",
    "one small data partition and nothing else":
        "nvs, data, nvs, 0x9000, 24K\n",
    "comments, blank lines and the optional Flags column":
        "\n"
        "# the settings live here\n"
        "nvs, data, nvs, 0x9000, 24K,\n"
        "\n"
        "secret, data, nvs, 0xf000, 8K, encrypted   # and a trailing comment\n",
}


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(CHECKER), *args],
                          capture_output=True, text=True)


def write(tmp: Path, name: str, body: str) -> Path:
    path = tmp / f"{name}-partitions.csv"
    path.write_text(HEADER + body, encoding="utf-8")
    return path


def check_rejections(tmp: Path) -> list[str]:
    failures = []
    for index, (case, (body, phrase)) in enumerate(MUST_REJECT.items()):
        path = write(tmp, f"reject{index}", body)
        result = run(str(path))
        output = result.stdout + result.stderr
        if result.returncode == 0:
            failures.append(f"ACCEPTED but must be refused: {case}\n"
                            f"    {body.strip()}\n"
                            f"    checker said: {output.strip()}")
        elif phrase not in output:
            failures.append(f"refused for the wrong reason: {case}\n"
                            f"    expected the message to contain {phrase!r}\n"
                            f"    got: {output.strip()}")
    return failures


def check_acceptances(tmp: Path) -> list[str]:
    failures = []
    for index, (case, body) in enumerate(MUST_ACCEPT.items()):
        path = write(tmp, f"accept{index}", body)
        result = run(str(path))
        if result.returncode != 0:
            failures.append(f"REFUSED but must be accepted: {case}\n"
                            f"    {(result.stdout + result.stderr).strip()}")
    return failures


def check_vendor_table() -> list[str]:
    """The measured vendor table, which has two rows above the line.

    Both must be named. Reporting only the first would leave a reader believing
    one edit fixes the file, and this is the shape — an error list truncated to
    its first entry — that the checker's own loop is written to avoid.
    """
    result = run(str(VENDOR_TABLE))
    output = result.stdout + result.stderr
    failures = []
    if result.returncode == 0:
        failures.append("the vendor's shipped table was accepted; it puts "
                        "ota_1 at 0x1000000 and storage at 0x1600000")
        return failures
    # Matched as `: name:` rather than as a bare substring, because the fixture
    # is *called* waveshare-vendor-factory.csv and every line of the report
    # begins with that path — a plain `"factory" in output` is satisfied by the
    # filename and asserts nothing.
    for name in ("ota_1", "storage"):
        if f": {name}:" not in output:
            failures.append(f"the vendor table was refused without naming "
                            f"{name}:\n    {output.strip()}")
    for name in ("factory", "ota_0", "nvs", "model"):
        if f": {name}:" in output:
            failures.append(f"the vendor table's {name}, which is below the "
                            f"line, was reported as a problem:"
                            f"\n    {output.strip()}")
    return failures


def check_discovery(tmp: Path) -> list[str]:
    """Which files the repository-wide run picks up, and which it must not.

    `fixtures/` is the one that would bite: it holds tables written to fail, so
    sweeping it in would leave the repository check permanently red and teach
    everybody to ignore it. `docs/` is the other — a table quoted in a document
    is an illustration, not a build input.
    """
    module_spec = importlib.util.spec_from_file_location("partition_check",
                                                         CHECKER)
    assert module_spec and module_spec.loader
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)

    root = tmp / "tree"
    for relative in ("boards/waveshare/partitions.csv",
                     "docs/research/partitions-example.csv",
                     "tools/flash/fixtures/bad-partitions.csv",
                     "assets/colours.csv"):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(HEADER, encoding="utf-8")

    # as_posix(), because the expectation below is written with forward
    # slashes and str(Path) uses the platform separator: on Windows this
    # compared "boards\waveshare\partitions.csv" against the same path
    # spelt the other way and reported the checker broken. The separator
    # is not what this case is about.
    found = {path.relative_to(root).as_posix() for path in module.discover(root)}
    expected = {"boards/waveshare/partitions.csv"}
    if found != expected:
        return [f"discover() picked up {sorted(found)}, expected "
                f"{sorted(expected)}"]

    # And the ceiling is the number the docs say it is. A constant nobody
    # asserts drifts, and this one is a hardware claim.
    if module.ADDRESSING_CEILING != 0x1000000:
        return [f"the ceiling is {module.ADDRESSING_CEILING:#x}; moving it is a "
                f"hardware claim and needs a measurement, not an edit"]
    return []


def main() -> int:
    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)
        failures = (check_rejections(tmp) + check_acceptances(tmp)
                    + check_vendor_table() + check_discovery(tmp))

    if failures:
        print("partition_check.py does not do what it says:\n")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    cases = len(MUST_REJECT) + len(MUST_ACCEPT) + 2
    print(f"partition_check selftest: {cases} cases, all as expected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
