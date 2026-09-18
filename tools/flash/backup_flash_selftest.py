#!/usr/bin/env python3
"""Host-only regression checks for atomic publication of a verified backup."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from contextlib import redirect_stderr, redirect_stdout
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
        *, no_verify: bool = False, size: int = 8, chunk: int | None = None,
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
                "--chunk", str(size // 2 if chunk is None else chunk)]
    if no_verify:
        sys.argv.append("--no-verify")
    spoken = io.StringIO()
    try:
        # argparse refuses on stderr, and a refusal is a result these checks
        # read rather than something to print over the run.
        with redirect_stdout(spoken), redirect_stderr(spoken):
            code = backup.main()
        return code, None, calls
    except SystemExit as exc:
        return None, str(exc), calls
    finally:
        if said is not None:
            said.append(spoken.getvalue())


def run_cli(output: Path, *geometry: str) -> tuple[int | None, str, list[str]]:
    """The public argument boundary, with everything past it wired to explode.

    `run()` above substitutes a read that works, which is what the publication
    checks need. This one asserts the opposite: for a geometry that cannot
    describe a backup, nothing past `parse_args()` runs at all -- no port is
    resolved, no chunk is read, no candidate is created beside the operator's
    image and `esptool` is never asked to compare anything.
    """
    reached: list[str] = []

    def forbidden(name: str):
        def stub(*_args, **_kwargs):
            reached.append(name)
            raise AssertionError(f"{name} ran for a geometry argparse should "
                                 f"have refused")
        return stub

    backup.resolve_port = forbidden("resolve_port")
    backup.read_chunk = forbidden("read_chunk")
    backup.esptool = forbidden("esptool")
    sys.argv = [str(SCRIPT), str(output), *geometry]
    spoken, complained = io.StringIO(), io.StringIO()
    try:
        with redirect_stdout(spoken), redirect_stderr(complained):
            code = backup.main()
        return code, spoken.getvalue() + complained.getvalue(), reached
    except (SystemExit, AssertionError) as exc:
        return None, spoken.getvalue() + complained.getvalue() + str(exc), reached


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

    # THE ISSUE'S OWN REPRODUCTION, THROUGH THE WHOLE TOOL: a zero size with a
    # chunk that works, a read that is never called because the loop is empty,
    # and an `esptool` returning exactly what upstream returns for an empty
    # range -- success, because it MD5s no bytes on both sides. Before the
    # guard this published an empty file over `factory.bin` and printed
    # VERIFIED. This is the preservation case: take the positive-size
    # validation out and it is what fails.
    with tempfile.TemporaryDirectory() as raw:
        output = Path(raw) / "factory.bin"
        output.write_bytes(b"KNOWN-GOOD")
        code, error, calls = run(
            output, CompletedProcess([], 0, "Verification successful (digest "
                                            "matched).", ""),
            size=0, chunk=4)
        check(code is None and error is not None,
              "a zero-length read was assembled and published", failures)
        check(output.read_bytes() == b"KNOWN-GOOD",
              "a zero-length read replaced the trusted backup", failures)
        check(not calls,
              "esptool was asked to verify an empty candidate", failures)

    # A ZERO-LENGTH READ IS NOT A SMALL BACKUP, AND THE TOOL USED TO PUBLISH IT.
    # `size % chunk` was the only geometry check and `0 % chunk` is 0, so
    # `--size 0` read nothing, assembled an empty candidate, compared 0 written
    # bytes against 0 expected, and had `verify-flash` agree -- upstream MD5s no
    # bytes on both sides and succeeds -- after which #243's atomic replace put
    # that empty file over the trusted backup under the word VERIFIED. These
    # cases are at the CLI boundary on purpose: every case above derives its
    # chunk as `size // 2`, so none of them could ever have reached zero.
    for geometry, why in (
            (["--size", "0"], "a zero size"),
            (["--size", "-8"], "a negative size"),
            (["--size", "0x0"], "a zero size written in hex"),
            (["--chunk", "0"], "a zero chunk"),
            (["--chunk", "-4"], "a negative chunk"),
            (["--attempts", "0"], "zero attempts"),
            (["--attempts", "-1"], "negative attempts"),
    ):
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "factory.bin"
            output.write_bytes(b"KNOWN-GOOD")
            code, said, reached = run_cli(output, *geometry)
            check(code is None, f"{why} was accepted", failures)
            check(output.read_bytes() == b"KNOWN-GOOD",
                  f"{why} replaced the trusted backup", failures)
            check(not reached,
                  f"{why} reached {reached} instead of being refused", failures)
            check([entry.name for entry in Path(raw).iterdir()] == ["factory.bin"],
                  f"{why} left a scratch directory or candidate on disk beside "
                  f"the trusted backup", failures)
            check("positive" in said,
                  f"{why} was refused without saying why", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print(f"backup_flash self-test: {CHECKED} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
