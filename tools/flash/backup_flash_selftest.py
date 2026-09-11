#!/usr/bin/env python3
"""Host-only regression checks for atomic publication of a verified backup."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from contextlib import redirect_stdout
import io
from subprocess import CompletedProcess
import sys
import tempfile


SCRIPT = Path(__file__).with_name("backup_flash.py")
SPEC = importlib.util.spec_from_file_location("attadipa_backup_flash", SCRIPT)
assert SPEC and SPEC.loader
backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(backup)


def run(output: Path, verification: CompletedProcess,
        *, no_verify: bool = False, size: int = 8,
        said: list[str] | None = None) -> tuple[int | None, str | None, list[tuple]]:
    calls: list[tuple] = []

    def fake_read(_python: str, _port: str, offset: int, size: int,
                  path: Path, _attempts: int) -> None:
        path.write_bytes(bytes([offset // size]) * size)

    def fake_esptool(*args, **kwargs):
        calls.append((*args, kwargs))
        return verification

    backup.resolve_port = lambda _serial: "/dev/fake"
    backup.read_chunk = fake_read
    backup.esptool = fake_esptool
    sys.argv = [str(SCRIPT), str(output), "--size", str(size),
                "--chunk", str(size // 2)]
    if no_verify:
        sys.argv.append("--no-verify")
    spoken = io.StringIO()
    try:
        with redirect_stdout(spoken):
            code = backup.main()
        return code, None, calls
    except SystemExit as exc:
        return None, str(exc), calls
    finally:
        if said is not None:
            said.append(spoken.getvalue())


CHECKED = 0


def check(condition: bool, message: str, failures: list[str]) -> None:
    # Counted rather than announced. The closing line used to carry a hand-
    # written total, and it was already wrong by two before this change added
    # three more checks to it.
    global CHECKED
    CHECKED += 1
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    mixed_failure = CompletedProcess(
        [], 2,
        "XMC flash chip boot-up fix successful!\nVerification failed (digest mismatch).",
        "",
    )

    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        output.write_bytes(b"KNOWN-GOOD")
        code, error, calls = run(output, mixed_failure)
        check(code is None and error is not None,
              "non-zero verify-flash was accepted because stdout said successful",
              failures)
        check(output.read_bytes() == b"KNOWN-GOOD",
              "failed verification replaced the existing trusted backup", failures)
        check(bool(calls) and Path(calls[-1][-2]) != output,
              "verify-flash ran against the authoritative output path", failures)

    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        run(output, mixed_failure)
        check(not output.exists(),
              "authoritative output appeared even though verification failed", failures)

    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        output.write_bytes(b"KNOWN-GOOD")
        code, error, _calls = run(output, CompletedProcess([], 0, "", ""),
                                  no_verify=True)
        check(code == 0 and error is None, "--no-verify did not complete", failures)
        check(output.read_bytes() == b"KNOWN-GOOD",
              "--no-verify replaced the trusted backup", failures)

    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        code, error, calls = run(output, CompletedProcess([], 0, "Verification successful", ""))
        check(code == 0 and error is None, "successful verification failed", failures)
        check(output.read_bytes() == b"\x00" * 4 + b"\x01" * 4,
              "verified candidate was not atomically published", failures)
        check(bool(calls) and Path(calls[-1][-2]) != output,
              "successful verification did not check the candidate", failures)

    # THE RESTORE ADVICE IS ONLY TRUE FOR AN IMAGE `--restore` WILL LOOK AT.
    # It refuses on size before it reads `VERIFIED_BACKUPS` at all, and this
    # tool's default is the OTHER board's 32 MB part -- so printed
    # unconditionally, the line sent the operator to edit the table that admits
    # a backup, to add a row that authorises nothing and could not have been
    # read. Found in review.
    advice = "add its sha256 to VERIFIED_BACKUPS"
    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        said: list[str] = []
        run(output, CompletedProcess([], 0, "Verification successful", ""),
            size=backup.FACTORY_FLASH_BYTES, said=said)
        check(advice in said[0],
              "a 16 MiB backup was not offered as a restore source", failures)

    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        said = []
        run(output, CompletedProcess([], 0, "Verification successful", ""),
            size=backup.FACTORY_FLASH_BYTES * 2, said=said)
        check(advice not in said[0],
              "a backup --restore refuses on size was still offered as a "
              "restore source", failures)
        check("NOT a restore source" in said[0],
              "nothing told the operator why that image is not one", failures)

    # And the digest line above that advice has to be true of the image it is
    # describing. The recorded SHA-256 is the 32 MB part's, so comparing a
    # 16 MiB read against it warned of a mismatch that could not have been
    # anything else -- immediately before offering that same image as a
    # restore source. Found in review.
    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        said = []
        run(output, CompletedProcess([], 0, "Verification successful", ""),
            size=backup.FACTORY_FLASH_BYTES, said=said)
        check("does NOT match" not in said[0],
              "a 16 MiB read was reported as failing to match a digest taken "
              "over the whole 32 MB part", failures)
        check("no recorded image" in said[0],
              "nothing said why that image has no recorded digest", failures)

    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        said = []
        run(output, CompletedProcess([], 0, "Verification successful", ""),
            size=backup.FLASH_SIZE, said=said)
        check("does NOT match" in said[0],
              "a full-size read was not compared against the recorded digest, "
              "so the comparison this tool exists for no longer runs", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print(f"backup_flash self-test: {CHECKED} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
