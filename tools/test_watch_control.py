#!/usr/bin/env python3
"""Does an explicitly named watch stay named?

`watch_control.py` resolves the bench watch by USB serial and, failing that,
falls back to a simulator socket. That fallback used to run for *every* failure:

    try:
        args.port = resolve_port(args.serial)
    except SystemExit:
        pass

and `--serial` carried a default, so nothing downstream could tell "the
operator asked for device AA:BB" from "nobody said". An operator naming an
absent unit was silently connected to whatever simulator was listening, and the
command exited 0 -- the tool reporting success about a device it never reached.
Issue #267.

The direction matters more than the tidiness. The bench has a T-Watch still
carrying its factory image; a scenario aimed at a named unit that lands
somewhere else is the accident that costs something unrecoverable.

Run: python3 tools/test_watch_control.py
"""

from __future__ import annotations

import io
import os
import sys
from contextlib import redirect_stderr

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import watch_control as wc  # noqa: E402

PASS = 0
FAIL = 0


def ok(name: str) -> None:
    global PASS
    PASS += 1
    print(f"  ok    {name}")


def no(name: str, detail: str = "") -> None:
    global FAIL
    FAIL += 1
    print(f"  FAIL  {name}" + (f"\n        {detail}" if detail else ""))


def run(argv, *, serial_present=None, env=None):
    """Run main() with resolve_port and connect replaced.

    Returns (exit code, list of connect kwargs, stderr). `connect` always
    refuses, so the assertions are about *what was asked for*, which is the
    whole of the finding -- not about a session nobody can have offline.
    """
    calls = []

    def fake_resolve(serial):
        if serial_present is not None and serial == serial_present:
            return f"/dev/ttyFAKE-{serial}"
        raise SystemExit(f"no serial device with USB serial {serial}.")

    def fake_connect(**kwargs):
        calls.append(kwargs)
        raise wc.WatchError("no endpoint in this test")

    real_resolve, real_connect = wc.resolve_port, wc.connect
    real_env = os.environ.get("ATTADIPA_WATCH_SERIAL")
    wc.resolve_port, wc.connect = fake_resolve, fake_connect
    if env is None:
        os.environ.pop("ATTADIPA_WATCH_SERIAL", None)
    else:
        os.environ["ATTADIPA_WATCH_SERIAL"] = env
    err = io.StringIO()
    try:
        with redirect_stderr(err):
            code = wc.main(argv)
    finally:
        wc.resolve_port, wc.connect = real_resolve, real_connect
        os.environ.pop("ATTADIPA_WATCH_SERIAL", None)
        if real_env is not None:
            os.environ["ATTADIPA_WATCH_SERIAL"] = real_env
    return code, calls, err.getvalue()


print("An explicitly named watch stays named")

# THE FINDING. A named device that is not there is an error about that device.
code, calls, err = run(["--serial", "AA:BB:CC:DD:EE:FF", "info"])
if code == 2 and not calls:
    ok("--serial for an absent device fails instead of connecting elsewhere")
else:
    no("--serial for an absent device fails instead of connecting elsewhere",
       f"exit {code}, connect called with {calls}")
if "AA:BB:CC:DD:EE:FF" in err:
    ok("and the message names the device that was asked for")
else:
    no("and the message names the device that was asked for", repr(err))

# Setting the environment variable is naming a device too -- it is the
# documented way to point the tool at a second unit, so it must not decay into
# a fallback either.
code, calls, err = run(["info"], env="AA:BB:CC:DD:EE:FF")
if code == 2 and not calls:
    ok("ATTADIPA_WATCH_SERIAL for an absent device fails the same way")
else:
    no("ATTADIPA_WATCH_SERIAL for an absent device fails the same way",
       f"exit {code}, connect called with {calls}")

# THE OTHER HALF, and the reason this is not just `raise`. With nothing named,
# the bench serial was only ever a guess, and the epilog promises the simulator.
code, calls, err = run(["info"])
if calls and calls[0].get("port") in (None, ""):
    ok("with no device named the simulator fallback survives")
else:
    no("with no device named the simulator fallback survives",
       f"exit {code}, connect called with {calls}")

# An explicit socket means the serial path is not consulted at all: a bench with
# no by-id directory must not turn a simulator run into an error.
code, calls, err = run(["--socket", "/tmp/nothing.sock", "info"])
if calls and calls[0].get("socket_path") == "/tmp/nothing.sock":
    ok("--socket bypasses serial resolution entirely")
else:
    no("--socket bypasses serial resolution entirely",
       f"exit {code}, connect called with {calls}")

# And the happy path still reaches the device it named, so the guard above is
# not simply refusing everything.
code, calls, err = run(["--serial", "28:84:85:B2:18:A4", "info"],
                       serial_present="28:84:85:B2:18:A4")
if calls and calls[0].get("port") == "/dev/ttyFAKE-28:84:85:B2:18:A4":
    ok("a --serial that resolves still reaches that port")
else:
    no("a --serial that resolves still reaches that port",
       f"exit {code}, connect called with {calls}")

print(f"\n{PASS} passed, {FAIL} failed")
sys.exit(1 if FAIL else 0)
