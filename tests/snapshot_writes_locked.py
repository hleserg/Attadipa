#!/usr/bin/env python3
"""Every assignment to what `snapshot_lock` guards happens under it (#344).

The UI task reads `snapshot` and `location_snapshot` under the lock from before
start_meshcore_ble() runs, so a single unlocked write is a data race -- and for
`location_snapshot` a torn read pairs one fix's coordinate with another's age.
The rule checked here: a line that assigns to either (or a member of it) lies
between `taskENTER_CRITICAL(&snapshot_lock);` and the matching
`taskEXIT_CRITICAL(&snapshot_lock);`. The one static initializer is exempt --
it runs before any task exists.

It reads text, so it sees assignments only: a `memcpy` into the snapshot, a
write through a reference, or an RAII guard in place of the two macros are
not recognised. A guard for `snapshot_lock` has to teach this file its name:
any other line that names the lock is refused rather than guessed at. A file
with no write to check fails too: a check that found nothing to check did not
run. The count comes from the same pass, so a comment or the exempt initializer
cannot stand in for one (#743).
"""
import pathlib
import re
import sys

SOURCE = (pathlib.Path(__file__).resolve().parent.parent
          / "firmware" / "main" / "meshcore_ble.cpp")
WRITE = re.compile(
    r"(?<![\w.>:])(?:location_)?snapshot(?:\.\w+|\[[^\]]*\])*\s*"
    r"(?:[-+*/%|&^]|<<|>>)?=(?!=)")
ENTER = "taskENTER_CRITICAL(&snapshot_lock);"
EXIT = "taskEXIT_CRITICAL(&snapshot_lock);"
INITIALIZER = "attadipa::core::MeshStatus snapshot = provider.status();"
DECLARATION = "portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;"


def unlocked_writes(text):
    bad = []
    held = 0
    entered = writes = False
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("//", 1)[0].strip()
        if line == ENTER:
            if held:
                bad.append((number, f"ENTER while the ENTER at {held} has no EXIT"))
            held = entered = number
        elif line == EXIT:
            # An early release before a second EXIT is correct code; an EXIT
            # before any ENTER is not (#743).
            if not entered:
                bad.append((number, "EXIT with no ENTER"))
            held = 0
        elif "snapshot_lock" in line and line != DECLARATION:
            # Fail closed: a lock line this file cannot read would leave `held`
            # wrong for the rest of the file (#736).
            bad.append((number, "names snapshot_lock in a form this test cannot "
                                "read; teach it the form"))
        elif WRITE.search(line) and line != INITIALIZER:
            writes = True
            if not held:
                bad.append((number, "write outside snapshot_lock"))
    if held:
        bad.append((held, "ENTER with no EXIT before the end of the file"))
    if not writes:
        bad.append((0, "no snapshot write to check"))
    return bad


def self_test():
    for locked in (f"{ENTER}\nsnapshot = next;\n{EXIT}\n",
                   f"{ENTER}\nsnapshot = n;\nsnapshot.availability = a;\n{EXIT}",
                   f"{ENTER}\nif (a) {{\n  location_snapshot = p;\n}}\n{EXIT}",
                   f"{ENTER}\nsnapshot = n;\nif (a) {{\n  {EXIT}\n  return;\n}}\n{EXIT}"):
        assert unlocked_writes(locked) == [], f"a locked write was refused: {locked!r}"
    for mutant in ("snapshot.availability = x;", "snapshot = next;",
                   "if (a) snapshot.mtu += 1;", "location_snapshot = p;",
                   f"{ENTER}\n{EXIT}\nsnapshot = n;",
                   f"{ENTER}\nif (a) taskEXIT_CRITICAL(&snapshot_lock);\n{EXIT}",
                   f"{ENTER}\n{ENTER}\nsnapshot = n;\n{EXIT}",
                   f"{ENTER}\nsnapshot = n;",
                   f"{EXIT}\n{ENTER}\nsnapshot = n;\n{EXIT}",
                   # Nothing to check: no lock, or only what the scan skips.
                   "", f"{ENTER}\n// snapshot = n;\n{EXIT}\n{INITIALIZER}"):
        assert unlocked_writes(mutant), f"an unlocked write passed: {mutant!r}"
    assert unlocked_writes(f"{ENTER}\nsnapshot = n;\n{EXIT}\nmy_snapshot = p;\n"
                           "x = session_snapshot();") == []


def main():
    self_test()
    bad = unlocked_writes(SOURCE.read_text(encoding="utf-8"))
    for number, reason in bad:
        print(f"{SOURCE}:{number}: {reason}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
