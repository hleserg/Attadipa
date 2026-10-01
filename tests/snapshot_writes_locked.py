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
with no lock section or no write to check fails too: a check that found nothing
to check did not run.
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
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("//", 1)[0].strip()
        if line == ENTER:
            if held:
                bad.append((number, f"ENTER while the ENTER at {held} has no EXIT"))
            held = number
        elif line == EXIT:
            if not held:
                bad.append((number, "EXIT with no ENTER"))
            held = 0
        elif "snapshot_lock" in line and line != DECLARATION:
            # Fail closed: a lock line this file cannot read would leave `held`
            # wrong for the rest of the file (#736).
            bad.append((number, "names snapshot_lock in a form this test cannot "
                                "read; teach it the form"))
        elif WRITE.search(line) and line != INITIALIZER and not held:
            bad.append((number, "write outside snapshot_lock"))
    if held:
        bad.append((held, "ENTER with no EXIT before the end of the file"))
    return bad


def self_test():
    for locked in (f"{ENTER}\nsnapshot = next;\n{EXIT}\n",
                   f"{ENTER}\nsnapshot = n;\nsnapshot.availability = a;\n{EXIT}",
                   f"{ENTER}\nif (a) {{\n  location_snapshot = p;\n}}\n{EXIT}"):
        assert unlocked_writes(locked) == [], f"a locked write was refused: {locked!r}"
    for mutant in ("snapshot.availability = x;", "snapshot = next;",
                   "if (a) snapshot.mtu += 1;", "location_snapshot = p;",
                   f"{ENTER}\n{EXIT}\nsnapshot = n;",
                   f"{ENTER}\nif (a) taskEXIT_CRITICAL(&snapshot_lock);\n{EXIT}",
                   f"{ENTER}\n{ENTER}\nsnapshot = n;\n{EXIT}",
                   f"{ENTER}\nsnapshot = n;", f"{EXIT}\nx = 1;"):
        assert unlocked_writes(mutant), f"an unlocked write passed: {mutant!r}"
    assert unlocked_writes("my_snapshot = p;\nx = session_snapshot();") == []


def main():
    self_test()
    text = SOURCE.read_text(encoding="utf-8")
    if ENTER not in text or EXIT not in text or not WRITE.search(text):
        # A check that found nothing to check did not run.
        print(f"{SOURCE}: no snapshot_lock section or snapshot write to check",
              file=sys.stderr)
        return 1
    bad = unlocked_writes(text)
    for number, reason in bad:
        print(f"{SOURCE}:{number}: {reason}", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
