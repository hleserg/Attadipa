#!/usr/bin/env python3
"""`ramhold.py` resolves the watch by USB serial, and never guesses.

There are two ESP32-S3 boards on this bench and both enumerate as `303a:1001`
(docs/research/BENCH_DEVICES.md). The failure this guards against is not a crash
— it is `ramhold.py` cheerfully loading a watch image into the MeshCore node
because `/dev/ttyACM0` came up first today. So the cases that matter are the
ones where the answer is *refusal*: no match, and more than one match.

No device is needed. `resolve_port` reads a directory, so a temporary directory
with the right names in it is the whole fixture.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ramhold  # noqa: E402

WATCH = "28:84:85:B2:18:A4"
OTHER = "F8:5B:1B:A1:98:24"


def link_name(serial: str) -> str:
    return f"usb-Espressif_USB_JTAG_serial_debug_unit_{serial}-if00"


def resolve_in(directory: Path, serial: str) -> str:
    ramhold.BY_ID = directory
    return ramhold.resolve_port(serial)


def refusal_in(directory: Path, serial: str) -> str:
    """The message from a refusal, or a failure if it did not refuse."""
    try:
        port = resolve_in(directory, serial)
    except SystemExit as exit_:
        return str(exit_)
    raise AssertionError(f"resolved to {port} where it should have refused")


def check(tmp: Path) -> list[str]:
    failures = []

    both = tmp / "both"
    both.mkdir()
    for serial, tty in ((WATCH, "ttyACM1"), (OTHER, "ttyACM0")):
        (tmp / tty).write_text("")  # the device node's stand-in
        (both / link_name(serial)).symlink_to(tmp / tty)

    # The case the whole thing exists for: two boards present, the watch's own
    # serial picks the watch and not the one that sorts first.
    if resolve_in(both, WATCH) != str(tmp / "ttyACM1"):
        failures.append("with both boards present, the watch serial did not "
                        "resolve to the watch")
    if resolve_in(both, OTHER) != str(tmp / "ttyACM0"):
        failures.append("the second board's serial did not resolve to it")

    # Unplugged. A fallback to "the only ESP32 present" would be the dangerous
    # answer here, because the only ESP32 present is the MeshCore node.
    only_other = tmp / "only_other"
    only_other.mkdir()
    (only_other / link_name(OTHER)).symlink_to(tmp / "ttyACM0")
    message = refusal_in(only_other, WATCH)
    if WATCH not in message or link_name(OTHER) not in message:
        failures.append("the refusal does not name the serial looked for and "
                        "the devices that were present, so it cannot be acted on")

    # Nothing at all. Still a refusal, still not an exception nobody can read.
    empty = tmp / "empty"
    empty.mkdir()
    if "(none)" not in refusal_in(empty, WATCH):
        failures.append("an empty by-id directory does not say so")

    # A serial that is a prefix of two links. Contrived, and the point is that
    # the ambiguity is refused rather than silently resolved to the first.
    ambiguous = tmp / "ambiguous"
    ambiguous.mkdir()
    for suffix in ("if00", "if02"):
        (ambiguous / f"usb-Espressif_USB_JTAG_serial_debug_unit_{WATCH}-{suffix}").symlink_to(
            tmp / "ttyACM1")
    if "more than one" not in refusal_in(ambiguous, WATCH):
        failures.append("two links matching one serial were not refused")

    # The directory itself missing — a host with no udev by-id links at all.
    if "does not exist" not in refusal_in(tmp / "absent", WATCH):
        failures.append("a missing by-id directory was not reported as one")

    return failures


def main() -> int:
    original = ramhold.BY_ID
    try:
        with tempfile.TemporaryDirectory() as raw:
            failures = check(Path(raw))
    finally:
        ramhold.BY_ID = original

    if failures:
        print("ramhold.py does not resolve the port the way it claims:\n")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("ramhold selftest: 7 cases, all as expected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
