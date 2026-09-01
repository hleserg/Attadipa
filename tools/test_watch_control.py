#!/usr/bin/env python3
"""What does this command line accept, and what does it refuse?

Two findings, one argument surface. The first is #267 -- does an explicitly
named watch stay named. The second is #316 -- can a MeshCore credential still
become a command-line argument. Both are answered against the real parser and
the real `main()`, with only the transport faked.

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
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from watch import protocol as p  # noqa: E402
import watch_control as wc  # noqa: E402

PASS = 0
FAIL = 0

# Not a credential. Fourteen bytes, inside the MeshCore Room Server's
# fifteen-byte ceiling, and unmistakable if it ever turns up somewhere it should
# not. No real credential appears in this file.
CANARY = "CANARY-NOTREAL"
ROOM = "00" * 32


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

class Recorder:
    """A watch that records what it was asked to send and reaches no device."""

    def __init__(self, calls):
        self.calls = calls

    def mesh_room_send(self, room, password, text, utc_seconds):
        self.calls.append(("mesh_room_send", room, password, text, utc_seconds))

    def mesh_configure(self, passkey):
        self.calls.append(("mesh_configure", passkey))

    forget_bond_error = None

    def mesh_forget_bond(self):
        self.calls.append(("mesh_forget_bond",))
        if Recorder.forget_bond_error is not None:
            raise Recorder.forget_bond_error

    def input_reset(self):
        return (None, 0)

    def close(self):
        pass


def run(argv, stdin_text=""):
    """main() with the transport faked and stdin a pipe, never a terminal."""
    calls = []
    real_connect, real_stdin = wc.connect, sys.stdin
    wc.connect = lambda **kwargs: Recorder(calls)
    sys.stdin = io.StringIO(stdin_text)
    err, out = io.StringIO(), io.StringIO()
    try:
        with redirect_stderr(err), redirect_stdout(out):
            code = wc.main(["--socket", "/tmp/i316-no-such.sock"] + argv)
    except SystemExit as exc:  # argparse refuses an unknown flag this way
        code = exc.code if isinstance(exc.code, int) else 2
    finally:
        wc.connect, sys.stdin = real_connect, real_stdin
    return code, calls, err.getvalue() + out.getvalue()


print("MeshCore credentials stay out of argv")

# THE FINDING, both halves. Neither flag exists any more, so neither credential
# can be put on a command line by an operator or by a script that copied one.
code, calls, err = run(["mesh-room-send", "--room", ROOM,
                        "--password", CANARY, "--text", "hi"])
if code != 0 and not calls:
    ok("--password is refused by the parser")
else:
    no("--password is refused by the parser", f"exit {code}, calls {calls}")

code, calls, err = run(["mesh-configure", "--passkey", "123456"])
if code != 0 and not calls:
    ok("--passkey is refused by the parser")
else:
    no("--passkey is refused by the parser", f"exit {code}, calls {calls}")

# And the replacement works: piped in, the secret reaches the client verbatim
# without ever having been an argument.
code, calls, err = run(["mesh-room-send", "--room", ROOM, "--text", "hi",
                        "--utc-seconds", "1000"],
                       stdin_text=CANARY + "\n")
sent = [c for c in calls if c[0] == "mesh_room_send"]
if code == 0 and sent and sent[0][2] == CANARY:
    ok("a password piped on stdin reaches the client unchanged")
else:
    no("a password piped on stdin reaches the client unchanged",
       f"exit {code}, calls {calls}, err {err!r}")

# The canary must not be anywhere the operator's terminal can see it, which is
# the whole point of moving it off the command line.
if CANARY not in err:
    ok("and is not echoed back to the console")
else:
    no("and is not echoed back to the console", repr(err))

# Fail-closed at the boundary, three ways. Each of these must refuse rather than
# send a truncated or malformed credential to the watch.
for name, text in (("an empty password", "\n"),
                   ("an over-long password", "x" * 16 + "\n"),
                   ("a password carrying a NUL", "ab\x00cd\n")):
    code, calls, err = run(["mesh-room-send", "--room", ROOM, "--text", "hi"],
                           stdin_text=text)
    if code != 0 and not [c for c in calls if c[0] == "mesh_room_send"]:
        ok(f"{name} is refused, and nothing is sent")
    else:
        no(f"{name} is refused, and nothing is sent", f"exit {code}, calls {calls}")

# The passkey moves the same way, and is validated as six digits before it goes.
code, calls, err = run(["mesh-configure"], stdin_text="123456\n")
if code == 0 and ("mesh_configure", 123456) in calls:
    ok("a six-digit passkey piped on stdin reaches the client")
else:
    no("a six-digit passkey piped on stdin reaches the client",
       f"exit {code}, calls {calls}, err {err!r}")

code, calls, err = run(["mesh-configure"], stdin_text="12x456\n")
if code != 0 and not calls:
    ok("a passkey that is not six digits is refused")
else:
    no("a passkey that is not six digits is refused", f"exit {code}, calls {calls}")

# 000000 is the one six-digit string that is not a passkey: the firmware reads
# it as "do not pair". Reaching mesh_configure with it would come back
# "configured" over a link with no encryption, which is the opposite of what
# this branch is for.
code, calls, err = run(["mesh-configure"], stdin_text="000000\n")
if code != 0 and not calls and "unpaired-probe" in err:
    ok("000000 is refused, and the error names the flag that means it")
else:
    no("000000 is refused, and the error names the flag that means it",
       f"exit {code}, calls {calls}, err {err!r}")

# THE OTHER HALF. `--passkey 0` was the unpaired diagnostic probe -- not a
# secret, and something a script runs unattended. It keeps a flag of its own, so
# removing the credential from argv did not cost the bench its probe.
code, calls, err = run(["mesh-configure", "--unpaired-probe"])
if code == 0 and ("mesh_configure", 0) in calls:
    ok("the unpaired probe stays scriptable and reads no secret")
else:
    no("the unpaired probe stays scriptable and reads no secret",
       f"exit {code}, calls {calls}, err {err!r}")


# mesh-forget-bond's expected answer is a refusal: the firmware sends BAD_INPUT
# whenever no stale bond is recorded, which is the ordinary state. The generic
# text for that code is written for the touch and button opcodes and names a
# button this board does not have -- for a zero-argument command it says nothing
# true, and the operator cannot tell "nothing to forget" from "malformed
# request".
Recorder.forget_bond_error = p.ProtocolError("refused", p.ErrorCode.BAD_INPUT)
code, calls, err = run(["mesh-forget-bond"])
if code != 0 and "nothing to forget" in err and "button" not in err:
    ok("a refused forget-bond says no bond is recorded, not that a button is wrong")
else:
    no("a refused forget-bond says no bond is recorded, not that a button is wrong",
       f"exit {code}, calls {calls}, err {err!r}")

# Every other typed error still travels as itself. Swallowing BAD_INPUT must not
# turn into swallowing the transport failing, which is the answer the same
# command gives when the firmware's event queue is full.
Recorder.forget_bond_error = p.ProtocolError(
    "the device could not complete the operation", p.ErrorCode.OPERATION_FAILED)
code, calls, err = run(["mesh-forget-bond"])
if code != 0 and "nothing to forget" not in err:
    ok("a forget-bond that failed in the transport is not reported as no bond")
else:
    no("a forget-bond that failed in the transport is not reported as no bond",
       f"exit {code}, calls {calls}, err {err!r}")

# And the accepted path still reaches the client, so the guards above are not
# simply refusing everything.
Recorder.forget_bond_error = None
code, calls, err = run(["mesh-forget-bond"])
if code == 0 and ("mesh_forget_bond",) in calls:
    ok("an accepted forget-bond reaches the client")
else:
    no("an accepted forget-bond reaches the client",
       f"exit {code}, calls {calls}, err {err!r}")


print(f"\n{PASS} passed, {FAIL} failed")
sys.exit(1 if FAIL else 0)
