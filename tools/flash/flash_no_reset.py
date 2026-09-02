#!/usr/bin/env python3
"""Write a built image into a unit that a hand has put into ROM download mode.

    python3 tools/flash/flash_no_reset.py firmware/build-twatch [--dry-run]
    python3 tools/flash/flash_no_reset.py --restore twatch_factory_16MB.bin

For the T-Watch S3 Plus, and any other unit whose USB CDC refuses
SET_CONTROL_LINE_STATE (errno 71): no esptool reset strategy can reach it, so
download mode is entered by hand — unplug USB, hold BOOT, plug in while holding,
release — and this script talks to the ROM loader that leaves running.
docs/research/TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md is the account.

The route is `ramhold.py`'s, which is the only one measured to reach that unit:
the port is opened with rtscts/dsrdtr so pyserial never asserts DTR/RTS,
`esptool.detect_chip(connect_mode="no_reset")` takes the pre-opened port, and
`esptool.main(argv, esp=esp)` reuses that connection for `write_flash`, so the
CLI's own port open — the thing the unit refuses — never happens.

What is written: the bootloader, partition table and app that
`flasher_args.json` names, at the offsets it names, and nothing else. On a
16 MB part that is 0x0-0x410000; NVS, PHY data, the FAT and coredump regions
of the factory layout are not touched. `--restore` writes the same span back
from a full-flash backup and refuses a backup that is not exactly 16 MiB.

After the write the default is `--after watchdog_reset`: the flasher stub arms
the RTC watchdog, which needs no control line. If the unit instead stays in the
loader, press RST (on the GNSS daughterboard) or hold PWR. The port is then
re-resolved by USB serial — a reset re-enumerates the USB-Serial/JTAG device —
and the first boot is echoed for `--watch` seconds so it is on the record.

Nothing here touches an eFuse or a security setting.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ramhold import resolve_port  # noqa: E402

# The received LilyGO T-Watch S3 Plus, by the USB serial its ROM reports.
TWATCH_SERIAL = "DC:B4:D9:18:49:40"
FACTORY_FLASH_BYTES = 16 * 1024 * 1024
# bootloader 0x0 + table 0x8000 + app0 0x10000 of 0x400000: identical in the
# factory (Arduino default_16MB) table and in firmware/partitions.csv.
RESTORE_SPAN = 0x410000
BAUD = 115200  # the S3's USB-Serial/JTAG ignores baud; not changing it keeps
               # esptool from renegotiating on a port it did not open


def plan_from_build(build_dir: Path) -> tuple[dict[str, str], list[tuple[int, Path]]]:
    args = json.loads((build_dir / "flasher_args.json").read_text())
    settings = args["flash_settings"]
    files = sorted(((int(offset, 16), build_dir / name)
                    for offset, name in args["flash_files"].items()),
                   key=lambda pair: pair[0])
    for _offset, path in files:
        if not path.is_file():
            raise SystemExit(f"flasher_args.json names {path}, which does not exist")
    end = max(offset + path.stat().st_size for offset, path in files)
    if end > RESTORE_SPAN:
        # --restore is the only way back to the factory image, and it writes
        # RESTORE_SPAN bytes; a plan that reaches past it (an ota_1 at 0x410000,
        # say -- firmware/partitions.csv says the table may change) would leave
        # flash that no restore undoes.
        raise SystemExit(f"the plan writes up to 0x{end:x}, past RESTORE_SPAN "
                         f"0x{RESTORE_SPAN:x}; --restore could not undo it, so "
                         "raise RESTORE_SPAN deliberately or shrink the plan")
    return settings, files


def esptool_argv(settings: dict[str, str], files: list[tuple[int, Path]],
                 after: str) -> list[str]:
    # Underscore spelling (`write_flash`, `--flash_mode`). esptool 4.12 answers
    # `write_flash -h` and `write-flash -h` alike (checked 2026-09-02), so this
    # is the spelling that the machine the backup was taken on accepts; an
    # esptool that drops it refuses the argv itself before touching the port.
    argv = ["--chip", "esp32s3", "--baud", str(BAUD), "--after", after,
            "write_flash", "--flash_mode", settings["flash_mode"],
            "--flash_freq", settings["flash_freq"],
            "--flash_size", settings["flash_size"]]
    for offset, path in files:
        argv += [f"0x{offset:x}", str(path)]
    return argv


def identity_mismatch(mac: bytes, serial: str) -> str | None:
    """Why the chip on the opened port is not the unit --serial names, or None.

    Two ESP32-S3 boards enumerate as 303a:1001 on this bench, and --port skips
    the by-id lookup that tells them apart. The base MAC the loader reports is
    the USB serial in colon form, so the comparison is two values already in
    hand -- and it runs before anything is written, because a T-Watch image
    over the 32 MB Waveshare is exactly what --restore cannot undo.
    """
    seen = ":".join(f"{b:02x}" for b in mac)
    want = serial.strip().lower()
    if seen == want:
        return None
    return f"the chip on this port is {seen}, not {want}: nothing written"


def selftest() -> int:
    import tempfile

    with tempfile.TemporaryDirectory() as scratch:
        build = Path(scratch)
        (build / "bootloader.bin").write_bytes(b"\xe9" * 0x100)
        (build / "app.bin").write_bytes(b"\xe9" * 0x1000)
        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": {"flash_mode": "dio", "flash_freq": "80m",
                               "flash_size": "16MB"},
            "flash_files": {"0x10000": "app.bin", "0x0": "bootloader.bin"},
        }))
        settings, files = plan_from_build(build)
        assert [offset for offset, _ in files] == [0x0, 0x10000], "sorted by offset"
        argv = esptool_argv(settings, files, "watchdog_reset")
        assert argv[:6] == ["--chip", "esp32s3", "--baud", "115200",
                            "--after", "watchdog_reset"], argv
        assert argv[6] == "write_flash" and argv[7:9] == ["--flash_mode", "dio"], argv
        assert argv[-2:] == ["0x10000", str(build / "app.bin")], argv

        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": settings,
            "flash_files": {"0x0": "bootloader.bin", "0x40f100": "app.bin"},
        }))
        try:
            plan_from_build(build)
        except SystemExit as refused:
            assert "past RESTORE_SPAN" in str(refused), refused
        else:
            raise AssertionError("a plan ending at 0x410100 was not refused")

        backup = build / "twatch_factory_16MB.bin"
        backup.write_bytes(b"\x00" * (FACTORY_FLASH_BYTES - 1))
        try:
            plan_from_backup(backup, build)
        except SystemExit as refused:
            assert "not a full 16 MiB" in str(refused), refused
        else:
            raise AssertionError("a 16 MiB - 1 backup was not refused")

        unit = bytes.fromhex(TWATCH_SERIAL.replace(":", ""))
        assert identity_mismatch(unit, TWATCH_SERIAL) is None
        assert identity_mismatch(unit, TWATCH_SERIAL.lower()) is None
        other = bytes.fromhex("f4a4a3ecee7d")
        refused = identity_mismatch(other, TWATCH_SERIAL)
        assert refused and "nothing written" in refused, refused
    print("flash_no_reset selftest: 4 cases, all as expected.")
    return 0


def plan_from_backup(backup: Path, scratch: Path) -> tuple[dict[str, str], list[tuple[int, Path]]]:
    size = backup.stat().st_size
    if size != FACTORY_FLASH_BYTES:
        raise SystemExit(f"{backup} is {size} bytes, not a full 16 MiB flash image")
    blob = backup.read_bytes()
    print(f"# backup sha256 {hashlib.sha256(blob).hexdigest()}", flush=True)
    span = scratch / f"{backup.stem}_0x0-0x{RESTORE_SPAN:x}.bin"
    span.write_bytes(blob[:RESTORE_SPAN])
    # The factory image header carries its own mode/freq/size; esptool keeps them.
    return {"flash_mode": "keep", "flash_freq": "keep", "flash_size": "keep"}, [(0, span)]


def watch_console(serial: str, port: str | None, seconds: float) -> str:
    import serial as pyserial

    deadline = time.time() + seconds
    chunks: list[str] = []
    ser = None
    while time.time() < deadline:
        if ser is None:
            try:
                target = port or resolve_port(serial)
                ser = pyserial.Serial(target, baudrate=BAUD, rtscts=True,
                                      dsrdtr=True, timeout=0.05)
                print(f"# console {target}", flush=True)
            except (SystemExit, pyserial.SerialException):
                time.sleep(0.5)
                continue
        try:
            data = ser.read(8192)
        except pyserial.SerialException:
            ser = None  # the device re-enumerated; find it again
            continue
        if data:
            text = data.decode("utf-8", "replace")
            chunks.append(text)
            sys.stdout.write(text)
            sys.stdout.flush()
    return "".join(chunks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("build_dir", nargs="?", type=Path,
                        help="an idf.py build directory holding flasher_args.json")
    parser.add_argument("--restore", type=Path, default=None,
                        help="write the first 0x410000 bytes of this 16 MiB backup instead")
    parser.add_argument("--serial", default=TWATCH_SERIAL,
                        help=f"USB serial of the unit (default {TWATCH_SERIAL})")
    parser.add_argument("--port", default=None,
                        help="serial port, if the USB-serial lookup is not wanted")
    parser.add_argument("--after", default="watchdog_reset",
                        choices=("watchdog_reset", "no_reset"),
                        help="how to leave the loader (default watchdog_reset)")
    parser.add_argument("--watch", type=float, default=20.0,
                        help="seconds to echo the console afterwards (default 20)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the esptool command and exit without opening the port")
    parser.add_argument("--log", type=Path, default=None,
                        help="write the console transcript here as well")
    parser.add_argument("--selftest", action="store_true",
                        help="check the plan, the span refusal and the esptool "
                             "argv without a device, then exit")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if (args.build_dir is None) == (args.restore is None):
        raise SystemExit("give exactly one of a build directory or --restore BACKUP")

    scratch = Path(args.log).parent if args.log else Path.cwd()
    if args.restore is not None:
        settings, files = plan_from_backup(args.restore, scratch)
    else:
        settings, files = plan_from_build(args.build_dir)

    argv = esptool_argv(settings, files, args.after)
    print("# esptool " + " ".join(argv), flush=True)
    if args.dry_run:
        return 0

    import esptool
    import serial as pyserial

    print(f"# esptool {esptool.__version__} (argv spelling checked on 4.12)",
          flush=True)

    port = args.port or resolve_port(args.serial)
    print(f"# port {port}", flush=True)
    target = pyserial.Serial(port, baudrate=BAUD, rtscts=True, dsrdtr=True,
                             timeout=0.1)
    esp = esptool.detect_chip(port=target, baud=BAUD, connect_mode="no_reset")
    mac = esp.read_mac()
    print(f"# chip {esp.get_chip_description()}  mac {':'.join(f'{b:02x}' for b in mac)}",
          flush=True)
    mismatch = identity_mismatch(mac, args.serial)
    if mismatch:
        raise SystemExit(mismatch)
    if esp.secure_download_mode:
        raise SystemExit("secure download mode: this is not the unit this script is for")

    esptool.main(argv, esp=esp)
    print(f"# write_flash returned; --after {args.after}", flush=True)
    try:
        esp._port.close()  # the loader is gone or resetting; the console is a new open
    except Exception:  # noqa: BLE001 — a re-enumerating device raises whatever it likes
        pass

    blob = watch_console(args.serial, args.port, args.watch)
    if args.log:
        args.log.write_text(blob, encoding="utf-8")
    booted = "app_main" in blob or "Attadipa" in blob or "Board      :" in blob
    print(f"\n# {len(blob)} chars; " + ("BOOT BANNER SEEN" if booted else "no boot banner"),
          flush=True)
    return 0 if booted else 1


if __name__ == "__main__":
    sys.exit(main())
