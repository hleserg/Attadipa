#!/usr/bin/env python3
"""Host-only regression checks for atomic publication of a verified backup."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from subprocess import CompletedProcess
import sys
import tempfile


SCRIPT = Path(__file__).with_name("backup_flash.py")
SPEC = importlib.util.spec_from_file_location("attadipa_backup_flash", SCRIPT)
assert SPEC and SPEC.loader
backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(backup)


def run(output: Path, verification: CompletedProcess,
        *, no_verify: bool = False) -> tuple[int | None, str | None, list[tuple]]:
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
    sys.argv = [str(SCRIPT), str(output), "--size", "8", "--chunk", "4"]
    if no_verify:
        sys.argv.append("--no-verify")
    try:
        return backup.main(), None, calls
    except SystemExit as exc:
        return None, str(exc), calls


def check(condition: bool, message: str, failures: list[str]) -> None:
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

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("backup_flash self-test: 4 cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
