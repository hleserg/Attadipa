#!/usr/bin/env python3
"""`ramhold.py` resolves the watch by USB serial, and never guesses.

There are three ESP32-S3 boards on this bench and all enumerate as `303a:1001`
(docs/research/BENCH_DEVICES.md). The failure this guards against is not a crash
— it is `ramhold.py` cheerfully loading a watch image into the MeshCore node
because `/dev/ttyACM0` came up first today. So the cases that matter are the
ones where the answer is *refusal*: no match, and more than one match.

No device is needed. `resolve_port` reads a directory, so a temporary directory
with the right names in it is the whole fixture. `main()` itself runs against
stand-in `esptool` and `serial` modules, because the by-id link only *names* a
unit: what proves it is the base MAC the loader reads back, and that check has
to sit between `detect_chip` and `load_ram` (#717).
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import types
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

    # One serial on two links, two interfaces of one unit. Contrived, and the
    # point is that the ambiguity is refused rather than resolved to the first.
    ambiguous = tmp / "ambiguous"
    ambiguous.mkdir()
    for suffix in ("if00", "if02"):
        (ambiguous / f"usb-Espressif_USB_JTAG_serial_debug_unit_{WATCH}-{suffix}").symlink_to(
            tmp / "ttyACM1")
    if "more than one" not in refusal_in(ambiguous, WATCH):
        failures.append("two links matching one serial were not refused")

    # Case. The by-id name carries whatever case udev read off the descriptor;
    # a serial typed by hand is whatever the hand typed, and `identity_mismatch`
    # in `ramhold.py` already folds it. LOWER is the direction that
    # exercises the fold, the constants here being upper case.
    try:
        lowered = resolve_in(both, WATCH.lower())
    except SystemExit:
        lowered = None      # a refusal is the failure, not an abort of the run
    if lowered != str(tmp / "ttyACM1"):
        failures.append("a lower-case serial did not find the port, so it "
                        "passes every later identity check and then resolves "
                        "to nothing")

    # A partial serial names no unit. It used to resolve by substring and then
    # be refused by `identity_mismatch` with the port already open (#731).
    if "no serial device" not in refusal_in(both, WATCH[:8]):
        failures.append("a partial serial resolved to a port")

    # The directory itself missing — a host with no udev by-id links at all.
    if "does not exist" not in refusal_in(tmp / "absent", WATCH):
        failures.append("a missing by-id directory was not reported as one")

    return failures


class FakePort:
    def __init__(self) -> None:
        self.timeout = None
        self.pending = [b"I (12) app: running\n"]

    def read(self, _size: int) -> bytes:
        return self.pending.pop() if self.pending else b""


def run_main(tmp: Path, argv: list[str], chip_mac: str) -> dict:
    """Run ramhold.main() with stand-ins; return what reached each seam."""
    seen: dict = {"loaded": [], "opened": [], "detected": None, "exit": None}

    def detect_chip(port, baud, connect_mode):
        seen["detected"] = port
        return types.SimpleNamespace(
            CHIP_NAME="ESP32-S3", _port=FakePort(),
            read_mac=lambda: bytes.fromhex(chip_mac.replace(":", "")))

    def opened(port, **kwargs):
        seen["opened"].append((port, kwargs))
        return FakePort()

    esptool = types.ModuleType("esptool")
    esptool.__version__ = "4.12.0"
    esptool.detect_chip = detect_chip
    esptool.cmds = types.ModuleType("esptool.cmds")
    esptool.cmds.load_ram = lambda esp, image: seen["loaded"].append(image)
    pyserial = types.ModuleType("serial")
    pyserial.Serial = opened

    image = tmp / "ram.bin"
    image.write_bytes(b"\xe9")
    fakes = {"esptool": esptool, "esptool.cmds": esptool.cmds, "serial": pyserial}
    saved = {name: sys.modules.get(name) for name in fakes}
    saved_argv = sys.argv
    sys.modules.update(fakes)
    sys.argv = ["ramhold.py", str(image), "0.05", *argv]
    try:
        with contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            seen["exit"] = ramhold.main()
    except SystemExit as exit_:
        seen["exit"] = exit_.code
    finally:
        sys.argv = saved_argv
        for name, module in saved.items():
            if module is None:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = module
    return seen


def check_main(tmp: Path) -> list[str]:
    failures = []
    ramhold.BY_ID = tmp / "both"
    watch_tty = str(tmp / "ttyACM1")

    for mode in ("default_reset", "no_reset"):
        argv = ["--serial", WATCH, "--connect-mode", mode]
        ok = run_main(tmp, argv, WATCH)
        if len(ok["loaded"]) != 1 or ok["exit"] != 0:
            failures.append(f"{mode}: the right chip was not loaded ({ok})")
        if mode == "no_reset":
            if [port for port, _ in ok["opened"]] != [watch_tty] or not all(
                    kw.get("rtscts") and kw.get("dsrdtr") for _, kw in ok["opened"]):
                failures.append("no_reset no longer pre-opens the port with "
                                "rtscts/dsrdtr, the only route to the T-Watch")
        elif ok["detected"] != watch_tty:
            failures.append(f"default_reset opened {ok['detected']}, not the watch")

        # The case the issue is about: the link said watch, the chip did not.
        wrong = run_main(tmp, argv, OTHER)
        if wrong["loaded"]:
            failures.append(f"{mode}: an image was loaded into a chip whose MAC "
                            "is not --serial")
        if not isinstance(wrong["exit"], str) or "no RAM image loaded" not in wrong["exit"]:
            failures.append(f"{mode}: a MAC mismatch did not exit saying nothing "
                            f"was loaded ({wrong['exit']!r})")

    # The loader reports lower case; a serial typed in lower case is the same unit.
    lower = run_main(tmp, ["--serial", WATCH.lower()], WATCH)
    if len(lower["loaded"]) != 1:
        failures.append("a lower-case --serial was refused for the right chip")

    # --port was the bypass: it named a tty, not a unit, and nothing checked it.
    port = run_main(tmp, ["--port", watch_tty], WATCH)
    if port["exit"] != 2 or port["detected"] is not None:
        failures.append("--port is still accepted")

    return failures


def main() -> int:
    original = ramhold.BY_ID
    try:
        with tempfile.TemporaryDirectory() as raw:
            failures = check(Path(raw))
            failures += check_main(Path(raw))
    finally:
        ramhold.BY_ID = original

    if failures:
        print("ramhold.py does not resolve the port the way it claims:\n")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("ramhold selftest: 13 cases, all as expected.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
