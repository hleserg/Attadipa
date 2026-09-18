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
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ramhold import resolve_port  # noqa: E402
from firmware_elf_check import (  # noqa: E402
    APP_DESC_ELF_SHA256, APP_DESC_MAGIC, APP_DESC_OFFSET, BOARD_SYMBOLS,
    app_elf_sha256, board_fault, elf_sha256,
)

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
# THE UNIT ON THE PORT IS NOT THE ONLY IDENTITY THAT HAS TO MATCH.
#
# `identity_mismatch()` below proves which watch is connected. It says nothing
# about which watch the bytes were built for, and the two boards are
# indistinguishable from the flash plan: both emit `bootloader.bin`,
# `partition-table.bin` and `attadipa.bin` at the same three offsets, under the
# same project name. So `firmware/build` -- the default, which is the Waveshare
# -- typed where `firmware/build-twatch` was meant passed every check this
# script had and reached `write_flash` on a correctly identified T-Watch.
#
# What is asked instead is the linked artefact: which board entry point this
# ELF defines. That is decided at compile time and cannot be copied next to
# someone else's binary the way a generated `sdkconfig` can. The ELF is then
# bound to the `.bin` that will actually be written by the SHA-256 the
# toolchain records in the application descriptor, so a right ELF beside a
# wrong image is refused too.
#
# There is no flag that skips this, for the reason VERIFIED_BACKUPS gives: a
# switch that restores the old behaviour is the old behaviour, one argument
# further away.
#
# WHAT IT DOES NOT PROVE, WRITTEN HERE RATHER THAN LEFT TO BE FOUND OUT. Three
# files are written and only one of them is bound to the proved ELF. The
# partition table is not a hazard: both boards build the same `partitions.csv`
# -- `firmware/sdkconfig.defaults:121` -- "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions.csv\"".
# The bootloader is, because it is the artefact that differs and the one that
# acts, and **nothing in it names a board**: `esp_bootloader_desc_t` at 0x20
# carries a magic, an IDF version and a build timestamp -- read off both
# boards' builds on this bench, `v5.5.5-dirty` in each -- and no board field.
# Comparing that timestamp with the application's would reject an ordinary
# incremental rebuild, so it is not done. So a directory whose `attadipa.bin`
# and `attadipa.elf` were copied in from the other board's build passes this
# gate and still writes that build's bootloader at 0x0. It takes a hand-mixed
# directory rather than a mistyped one, which is the mistake this gate is
# about; the point of saying so is that the refusals below must not be read as
# proving more than the application image.
BOARD_VARIANT = "twatch"
NM = "xtensa-esp32s3-elf-nm"


def read_symbols(elf: Path, nm: str) -> str:
    # A toolchain that is not there is a missing proof, not a pass. Both the
    # `nm` that cannot be run and the `nm` that ran and failed end here, and
    # this script writes boot-critical flash, so both refuse.
    try:
        result = subprocess.run([nm, "-C", "--defined-only", str(elf)],
                                capture_output=True, text=True)
    except OSError as unavailable:
        why = str(unavailable)
    else:
        why = (result.stderr.strip() or f"exit {result.returncode}"
               if result.returncode != 0 else "")
    if why:
        raise SystemExit(
            f"{nm} could not read {elf}: {why}.\n"
            f"Without it there is no proof this build is the "
            f"{BOARD_VARIANT}'s, and this script writes boot-critical flash. "
            f"Export ESP-IDF, or point --nm at the toolchain's nm.")
    return result.stdout


def artifact_fault(build_dir: Path, nm: str) -> str | None:
    """Return a message if this build is not the board's, or is not one build."""
    elf = build_dir / "attadipa.elf"
    app = build_dir / "attadipa.bin"
    if not elf.is_file():
        return (f"{elf} does not exist, so nothing in this directory says "
                f"which board it was built for. A {BOARD_VARIANT} build "
                f"defines {BOARD_SYMBOLS[BOARD_VARIANT]}; the flash plan is "
                f"the same for both boards and proves nothing.")
    recorded = app_elf_sha256(app)
    if recorded is None:
        return (f"{app} carries no ESP-IDF application descriptor, so it "
                f"cannot be tied to {elf.name}. An application image assembled "
                f"by hand, or truncated, is not a build output this script "
                f"will write -- though the bootloader beside it is checked by "
                f"nothing either way.")
    linked = elf_sha256(elf)
    if recorded != linked:
        return (f"{app.name} was built from a different ELF than {elf.name}: "
                f"the image records {recorded} and the ELF hashes to {linked}. "
                f"The board this directory proves is not the board whose bytes "
                f"would be written. Rebuild, rather than pairing them by hand.")
    return board_fault(read_symbols(elf, nm), BOARD_VARIANT)


def plan_from_build(build_dir: Path,
                    nm: str = NM) -> tuple[dict[str, str], list[tuple[int, Path]]]:
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
    # Last, and still before the port is resolved, before `esptool` is
    # imported and before anything is opened: is this the board's build at all.
    wrong = artifact_fault(build_dir, nm)
    if wrong is not None:
        raise SystemExit(f"{wrong}\nNothing was written.")
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
    # THE FIXTURES LIVE INSIDE THE SELF-TEST, and the restore is this function
    # rather than one `finally` deep inside it. `write_build_artefacts()` binds
    # a stub over `read_symbols` -- a switch that turns the board gate off,
    # which has no business being reachable from module scope in a tool that
    # writes boot-critical flash. Nothing imports this module today; that is
    # not a reason to leave the switch where an import would find it. The
    # restore here also covers the cases that run before the inner
    # `try`/`finally`, so an exception among them cannot leave the stub behind.
    real_read_symbols = read_symbols
    try:
        return _selftest_cases()
    finally:
        globals()["read_symbols"] = real_read_symbols


def _selftest_cases() -> int:
    import tempfile

    # Captured again here: the caller above restores it either way, and a case
    # below puts the real one back deliberately to reach the `--nm` refusal.
    real_read_symbols = read_symbols

    # The symbol table a build of each board defines, as `nm -C --defined-only`
    # prints it. The self-test has no cross-compiler, so it hands these to the
    # same `board_fault()` the real path calls -- the rule is tested, the
    # subprocess is not, and the subprocess is the part that has nothing to get
    # wrong.
    symbols_of = {board: f"42000000 T {symbol}"
                  for board, symbol in BOARD_SYMBOLS.items()}


    def write_build_artefacts(build: Path, board: str | None, *,
                              descriptor: bool = True, bind: bool = True,
                              app_bytes: int = 0x1000) -> None:
        """An app image and the ELF it says it came from, as a build emits them.

        `board` picks which symbol table `read_symbols` will answer with; None
        leaves the ELF out entirely. `descriptor` and `bind` are the two ways the
        pair can fail to be one build: no application descriptor at all, and a
        descriptor recording somebody else's ELF.
        """
        app = bytearray(b"\xe9" * max(app_bytes, 0x100))
        elf = build / "attadipa.elf"
        if board is not None:
            elf.write_bytes(f"ELF of the {board} build".encode())
        elif elf.exists():
            elf.unlink()
        if descriptor:
            app[APP_DESC_OFFSET:APP_DESC_OFFSET + 4] = \
                APP_DESC_MAGIC.to_bytes(4, "little")
            recorded = (elf_sha256(elf) if bind and board is not None
                        else hashlib.sha256(b"a different build").hexdigest())
            app[APP_DESC_OFFSET + APP_DESC_ELF_SHA256:
                APP_DESC_OFFSET + APP_DESC_ELF_SHA256 + 32] = bytes.fromhex(recorded)
        (build / "attadipa.bin").write_bytes(bytes(app[:app_bytes] if app_bytes >= 0x100
                                                   else app))
        globals()["read_symbols"] = (
            lambda _elf, _nm, board=board: symbols_of.get(board, ""))

    with tempfile.TemporaryDirectory() as scratch:
        build = Path(scratch)
        (build / "bootloader").mkdir()
        (build / "partition_table").mkdir()
        (build / "bootloader/bootloader.bin").write_bytes(b"\xe9" * 0x100)
        (build / "partition_table/partition-table.bin").write_bytes(b"\x00" * 0x1000)
        write_build_artefacts(build, "twatch")
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

        write_build_artefacts(build, "twatch",
                              app_bytes=RESTORE_SPAN - 0x10000 + 1)
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
        write_build_artefacts(build, "twatch")
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

    # THE BOARD THE BYTES WERE BUILT FOR, WHICH THE PLAN CANNOT SAY.
    #
    # Each case runs the whole CLI, not `plan_from_build()` on its own, with
    # `resolve_port` and the two write-capable imports replaced by tripwires.
    # A refusal that reached any of them would raise from the tripwire instead
    # of exiting with the message, so "nothing was opened" is proved by the
    # run rather than asserted about it.
    with tempfile.TemporaryDirectory() as scratch:
        build = Path(scratch)
        (build / "bootloader").mkdir()
        (build / "partition_table").mkdir()
        (build / "bootloader/bootloader.bin").write_bytes(b"\xe9" * 0x100)
        (build / "partition_table/partition-table.bin").write_bytes(b"\x00" * 0x1000)
        (build / "flasher_args.json").write_text(json.dumps({
            "flash_settings": {"flash_mode": "dio", "flash_freq": "80m",
                               "flash_size": "16MB"},
            "flash_files": {"0x0": "bootloader/bootloader.bin",
                            "0x8000": "partition_table/partition-table.bin",
                            "0x10000": "attadipa.bin"},
        }))

        class Tripwire:
            def __init__(self, what: str) -> None:
                self.what = what

            def __getattr__(self, name: str):
                raise AssertionError(f"{self.what}.{name} was reached for a "
                                     f"build this script must refuse")

        def no_port(_serial):
            raise AssertionError("resolve_port was reached for a build this "
                                 "script must refuse")

        kept_resolve = resolve_port
        kept_modules = {name: sys.modules.get(name)
                        for name in ("esptool", "serial")}
        globals()["resolve_port"] = no_port
        sys.modules["esptool"] = Tripwire("esptool")
        sys.modules["serial"] = Tripwire("serial")
        try:
            def run(*argv: str) -> str | None:
                sys.argv = [__file__, str(build), *argv]
                try:
                    main()
                except SystemExit as refused:
                    return str(refused)
                return None

            # The defect itself: the default build directory is the Waveshare's
            # and its plan is identical to the T-Watch's in every byte this
            # script used to read.
            write_build_artefacts(build, "waveshare")
            said = run()
            assert said and "waveshare" in said and "start_twatch_ui()" in said, said

            # An image with no board entry point at all -- a variant that
            # dropped it, or an ELF from something else entirely.
            write_build_artefacts(build, None)
            said = run()
            assert said and "attadipa.elf does not exist" in said, said

            # A stale or copied `sdkconfig` cannot help here, because nothing
            # reads one: the proof is the pair, and a pair that is not one
            # build is refused even when the ELF is the right board's.
            write_build_artefacts(build, "twatch", bind=False)
            said = run()
            assert said and "built from a different ELF" in said, said

            write_build_artefacts(build, "twatch", descriptor=False)
            said = run()
            assert said and "no ESP-IDF application descriptor" in said, said

            # An ELF the toolchain cannot read is not a pass by default. This
            # is the fail-closed direction: no proof, no write.
            write_build_artefacts(build, "twatch")
            globals()["read_symbols"] = real_read_symbols
            said = run("--nm", str(build / "nm-that-is-not-there"))
            assert said and "could not read" in said, said

            # And the control: the right board, bound to its own image, gets
            # past the gate -- proved by --dry-run, which is the last thing
            # before the port would be opened.
            write_build_artefacts(build, "twatch")
            sys.argv = [__file__, str(build), "--dry-run"]
            assert main() == 0, "a genuine twatch build was refused"
        finally:
            globals()["resolve_port"] = kept_resolve
            globals()["read_symbols"] = real_read_symbols
            for name, module in kept_modules.items():
                if module is None:
                    sys.modules.pop(name, None)
                else:
                    sys.modules[name] = module

    print("flash_no_reset selftest: plan, overlap, span, backup provenance, "
          "artefact board binding and identity cases pass.")
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
    parser.add_argument("--nm", default=NM,
                        help=f"the toolchain nm that reads which board this "
                             f"build is (default {NM})")
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
        settings, files = plan_from_build(args.build_dir, args.nm)

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
