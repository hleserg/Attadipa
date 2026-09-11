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

THE TWO PATHS WRITE DIFFERENT THINGS, and the difference is what you lose.

A build directory writes THREE SEPARATE SEGMENTS -- the bootloader, the
partition table and the app that `flasher_args.json` names, at the offsets it
names. The gaps between them are not written, so everything outside those
three segments survives -- `nvs`, PHY data, the FAT and the coredump region.

WHICH OFFSETS THOSE ARE DEPENDS ON WHICH TABLE IS ON THE PART, and the build
path replaces the table at 0x8000 with this repository's. Under
`firmware/partitions.csv:22` -- "nvs,         data, nvs,      0x9000,    0x6000,"
-- `nvs` runs 0x9000-0xf000 and there is NO `otadata` partition at all. The
factory Arduino layout has one --
`docs/research/TWATCH_S3_PLUS_BRINGUP_2026-08-27.md:61` -- "otadata  data ota      0xe000      8K"
-- so on a factory unit flashed from a build directory those 8K survive the
write, and 8K does not fit in what is left of our `nvs`. It STRADDLES TWO
PARTITIONS: 0xe000-0xf000 is the last page of our `nvs`, and 0xf000-0x10000
is the whole of our `phy_init`
(`firmware/partitions.csv:23` -- "phy_init,    data, phy,      0xf000,    0x1000,").
What the first page does to `nvs_flash_init()` is UNKNOWN and has not been
tested. The second is inert here, and for a reason worth stating rather than
assuming: no sdkconfig under `firmware/` sets
`CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION`, and ESP-IDF v5.5.5 defaults it to
`n` (`components/esp_phy/Kconfig`, not a file of this repository), so this
firmware compiles its PHY data into the app and never reads that partition.
That is also why the paragraph above may count PHY data among what survives
the write: the partition survives it, but on a factory unit what survives in
it is otadata.

`--restore` writes ONE CONTIGUOUS BLOCK, 0x0-0x410000 from a full-flash backup.
That covers the same three images and everything between them, so `nvs` --
and the factory `otadata` at 0xe000, if the backup was taken from a factory
unit -- ARE overwritten with whatever the backup holds. Pairing keys, Wi-Fi
credentials and any OTA selection go back to their state when the backup was
read. Above 0x410000 nothing is touched either way.

A backup is accepted on its SHA-256 and the unit it was read from, not on its
length: see `VERIFIED_BACKUPS`.

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
# A RESTORE IS AUTHENTICATED BY WHAT THE FILE IS, NOT BY HOW LONG IT IS.
# Size was standing in for provenance, and 16 MiB of zeroes is exactly 16 MiB:
# it passed, became one write segment at 0x0, and reached esptool. The digest
# was computed and printed the whole time, and compared with nothing.
#
# Digest -> the USB serial of the unit the image was read off, because the
# binding is both halves. A genuine backup of the wrong watch is still a wrong
# image, and 0x0-0x410000 is the span no --restore undoes.
#
# Adding a row here is the explicit, reviewed action that admits a new backup.
# There is deliberately no flag that skips this: a switch that restores the old
# behaviour is the old behaviour, one argument further away.
VERIFIED_BACKUPS = {
    # `docs/research/TWATCH_S3_PLUS_BRINGUP_2026-08-27.md:38` --
    # "**`e28f5cdd79552950d7f73fc2776023e297bfcd5dcc320d667ee065b0ebd37202`**"
    # -- verified three independent ways there: the chip's own MD5 over all
    # 16 MB, a second full read that matched byte for byte, and a structural
    # parse in which `app0`'s self-carried SHA-256 validates.
    "e28f5cdd79552950d7f73fc2776023e297bfcd5dcc320d667ee065b0ebd37202":
        TWATCH_SERIAL,
}
# bootloader 0x0 + table 0x8000 + app0 0x10000 of 0x400000: identical in the
# factory (Arduino default_16MB) table and in firmware/partitions.csv.
RESTORE_SPAN = 0x410000
EXPECTED_FLASH_FILES = (
    (0x0, "bootloader/bootloader.bin"),
    (0x8000, "partition_table/partition-table.bin"),
    (0x10000, "attadipa.bin"),
)
BAUD = 115200  # the S3's USB-Serial/JTAG ignores baud; not changing it keeps
               # esptool from renegotiating on a port it did not open


def plan_from_build(build_dir: Path) -> tuple[dict[str, str], list[tuple[int, Path]]]:
    args = json.loads((build_dir / "flasher_args.json").read_text())
    settings = args["flash_settings"]
    entries = tuple(sorted(((int(offset, 16), name)
                            for offset, name in args["flash_files"].items()),
                           key=lambda pair: pair[0]))
    if entries != EXPECTED_FLASH_FILES:
        raise SystemExit(f"unexpected flash files {entries!r}; expected exactly "
                         f"{EXPECTED_FLASH_FILES!r}")
    files = [(offset, build_dir / name) for offset, name in entries]
    limits = tuple(offset for offset, _name in EXPECTED_FLASH_FILES[1:]) + \
        (RESTORE_SPAN,)
    for (offset, path), limit in zip(files, limits):
        if not path.is_file():
            raise SystemExit(f"flasher_args.json names {path}, which does not exist")
        if offset + path.stat().st_size > limit:
            boundary = ("past RESTORE_SPAN" if limit == RESTORE_SPAN
                        else "crosses the next image")
            raise SystemExit(f"image at 0x{offset:x} {boundary} at 0x{limit:x}")
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
        (build / "bootloader").mkdir()
        (build / "partition_table").mkdir()
        (build / "bootloader/bootloader.bin").write_bytes(b"\xe9" * 0x100)
        (build / "partition_table/partition-table.bin").write_bytes(b"\x00" * 0x1000)
        (build / "attadipa.bin").write_bytes(b"\xe9" * 0x1000)
        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": {"flash_mode": "dio", "flash_freq": "80m",
                               "flash_size": "16MB"},
            "flash_files": {"0x10000": "attadipa.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x0": "bootloader/bootloader.bin"},
        }))
        settings, files = plan_from_build(build)
        assert [offset for offset, _ in files] == [0x0, 0x8000, 0x10000], \
            "only the expected images, sorted by offset"
        argv = esptool_argv(settings, files, "watchdog_reset")
        assert argv[:6] == ["--chip", "esp32s3", "--baud", "115200",
                            "--after", "watchdog_reset"], argv
        assert argv[6] == "write_flash" and argv[7:9] == ["--flash_mode", "dio"], argv
        assert argv[-2:] == ["0x10000", str(build / "attadipa.bin")], argv

        (build / "attadipa.bin").write_bytes(
            b"\xe9" * (RESTORE_SPAN - 0x10000 + 1))
        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": settings,
            "flash_files": {"0x0": "bootloader/bootloader.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x10000": "attadipa.bin"},
        }))
        try:
            plan_from_build(build)
        except SystemExit as refused:
            assert "past RESTORE_SPAN" in str(refused), refused
        else:
            raise AssertionError("a plan ending at 0x410100 was not refused")

        (build / "bootloader/bootloader.bin").write_bytes(b"\xe9" * 0x8001)
        (build / "attadipa.bin").write_bytes(b"\xe9" * 0x1000)
        try:
            plan_from_build(build)
        except SystemExit as refused:
            assert "crosses the next image" in str(refused), refused
        else:
            raise AssertionError("a bootloader overlapping the partition table was accepted")

        (build / "nvs.bin").write_bytes(b"\x00" * 0x1000)
        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": settings,
            "flash_files": {"0x0": "bootloader/bootloader.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x9000": "nvs.bin",
                            "0x10000": "attadipa.bin"},
        }))
        try:
            plan_from_build(build)
        except SystemExit as refused:
            assert "unexpected flash files" in str(refused), refused
        else:
            raise AssertionError("an extra NVS image inside RESTORE_SPAN was accepted")

        (build / "bootloader/bootloader.bin").write_bytes(b"\xe9" * 0x100)
        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": settings,
            "flash_files": {"0x0": "bootloader/bootloader.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x10000": str((build / "attadipa.bin").resolve())},
        }))
        try:
            plan_from_build(build)
        except SystemExit as refused:
            assert "unexpected flash file" in str(refused), refused
        else:
            raise AssertionError("an absolute app path was accepted")

        backup = build / "twatch_factory_16MB.bin"
        backup.write_bytes(b"\x00" * (FACTORY_FLASH_BYTES - 1))
        try:
            plan_from_backup(backup, build, TWATCH_SERIAL)
        except SystemExit as refused:
            assert "not a full 16 MiB" in str(refused), refused
        else:
            raise AssertionError("a 16 MiB - 1 backup was not refused")

        # THE CASE THE OLD SUITE DID NOT HAVE, and the one the defect lived in:
        # right size, wrong content. 16 MiB of zeroes is exactly 16 MiB, so the
        # length check passed it, and every other check in this file was about
        # the build plan or the chip on the port.
        backup.write_bytes(b"\x00" * FACTORY_FLASH_BYTES)
        try:
            plan_from_backup(backup, build, TWATCH_SERIAL)
        except SystemExit as refused:
            assert "not a backup this repository has verified" in str(refused), refused
        else:
            raise AssertionError("16 MiB of zeroes was accepted as the factory backup")
        assert not list(build.glob("*_0x0-*.bin")), \
            "a refused backup still wrote a span file"

        # A VERIFIED IMAGE IS STILL THE WRONG IMAGE ON ANOTHER UNIT. The table
        # is patched rather than mocked, so this exercises the same lookup the
        # real digest goes through -- the private factory image is not in this
        # repository and must not be.
        planted = b"\xe9" + b"twatch-selftest" * ((FACTORY_FLASH_BYTES - 1) // 15)
        planted = (planted + b"\x00" * FACTORY_FLASH_BYTES)[:FACTORY_FLASH_BYTES]
        backup.write_bytes(planted)
        digest = hashlib.sha256(planted).hexdigest()
        VERIFIED_BACKUPS[digest] = TWATCH_SERIAL
        try:
            try:
                plan_from_backup(backup, build, "f4:a4:a3:ec:ee:7d")
            except SystemExit as refused:
                assert "verified backup of" in str(refused), refused
            else:
                raise AssertionError("a backup of another unit was accepted")
            # The digest refusal is checked for a leftover span and this one was
            # not, so moving the binding below `span.write_bytes` would leave
            # another unit's 4 MiB span in the operator's working directory with
            # the suite green. Found in review.
            assert not list(build.glob("*_0x0-*.bin")), \
                "a backup refused on its serial still wrote a span file"

            settings, files = plan_from_backup(backup, build, TWATCH_SERIAL)
            assert settings == {"flash_mode": "keep", "flash_freq": "keep",
                                "flash_size": "keep"}, settings
            assert len(files) == 1 and files[0][0] == 0, files
            assert files[0][1].stat().st_size == RESTORE_SPAN, files[0][1].stat().st_size
            assert files[0][1].read_bytes() == planted[:RESTORE_SPAN], \
                "the span is the head of the backup, unmodified"
            # Case is not identity: the loader reports the MAC in lower case
            # and a serial typed by hand is whatever the hand typed. The table
            # row is upper case, so LOWER is the direction that exercises the
            # fold -- `.upper()` here was the identity function on an
            # already-upper-case constant, and repeated the accepting case
            # above it with the same argument. Found in review.
            plan_from_backup(backup, build, TWATCH_SERIAL.lower())
        finally:
            del VERIFIED_BACKUPS[digest]

        unit = bytes.fromhex(TWATCH_SERIAL.replace(":", ""))
        assert identity_mismatch(unit, TWATCH_SERIAL) is None
        assert identity_mismatch(unit, TWATCH_SERIAL.lower()) is None
        other = bytes.fromhex("f4a4a3ecee7d")
        refused = identity_mismatch(other, TWATCH_SERIAL)
        assert refused and "nothing written" in refused, refused
    print("flash_no_reset selftest: plan, overlap, span, backup provenance "
          "and identity cases pass.")
    return 0


def plan_from_backup(backup: Path, scratch: Path,
                     serial: str) -> tuple[dict[str, str], list[tuple[int, Path]]]:
    """The single contiguous write a verified backup of `serial` plans, or refuse.

    Both refusals happen here, which is before the port is opened and before
    esptool is imported at all -- so a rejected image never reaches a
    write-capable anything. The MAC check in `main` stays where it is and
    answers a different question: this one asks whether the FILE is the right
    image, that one asks whether the CHIP is the right unit, and neither
    substitutes for the other.
    """
    size = backup.stat().st_size
    if size != FACTORY_FLASH_BYTES:
        raise SystemExit(f"{backup} is {size} bytes, not a full 16 MiB flash image")
    blob = backup.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    print(f"# backup sha256 {digest}", flush=True)
    read_from = VERIFIED_BACKUPS.get(digest)
    if read_from is None:
        raise SystemExit(
            f"{backup} hashes to {digest}, which is not a backup this repository "
            f"has verified; nothing written. The size was right and that is the "
            f"point -- 16 MiB of anything is 16 MiB. If this really is a good "
            f"full-flash read, verify it the way "
            f"docs/research/TWATCH_S3_PLUS_BRINGUP_2026-08-27.md records and add "
            f"its digest to VERIFIED_BACKUPS beside the unit it came off.")
    want = serial.strip().lower()
    if read_from.strip().lower() != want:
        raise SystemExit(
            f"{backup} is the verified backup of {read_from}, and --serial names "
            f"{want}; nothing written. A genuine backup of another unit writes "
            f"another unit's bootloader, partition table and NVS over this one.")
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
                        help="write the first 0x410000 bytes of this backup instead; it must be a 16 MiB image whose SHA-256 is in VERIFIED_BACKUPS for --serial, and that block covers nvs and otadata")
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
        settings, files = plan_from_backup(args.restore, scratch, args.serial)
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
