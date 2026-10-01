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
any other line that names the lock is refused rather than guessed at.
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
    held = False
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("//", 1)[0].strip()
        if line == ENTER:
            held = True
        elif line == EXIT:
            held = False
        elif "snapshot_lock" in line and line != DECLARATION:
            # Fail closed: a lock line this file cannot read would leave `held`
            # wrong for the rest of the file (#736).
            bad.append(number)
        elif WRITE.search(line) and line != INITIALIZER and not held:
            bad.append(number)
    return bad


def self_test():
    for locked in (f"{ENTER}\nsnapshot = next;\n{EXIT}\n",
                   f"{ENTER}\nsnapshot = n;\nsnapshot.availability = a;\n{EXIT}",
                   f"{ENTER}\nif (a) {{\n  location_snapshot = p;\n}}\n{EXIT}"):
        assert unlocked_writes(locked) == [], f"a locked write was refused: {locked!r}"
    for mutant in ("snapshot.availability = x;", "snapshot = next;",
                   "if (a) snapshot.mtu += 1;", "location_snapshot = p;",
                   f"{ENTER}\n{EXIT}\nsnapshot = n;",
                   f"{ENTER}\nif (a) taskEXIT_CRITICAL(&snapshot_lock);\n{EXIT}"):
        assert unlocked_writes(mutant), f"an unlocked write passed: {mutant!r}"
    assert unlocked_writes("my_snapshot = p;\nx = session_snapshot();") == []


def main():
    self_test()
    bad = unlocked_writes(SOURCE.read_text(encoding="utf-8"))
    for number in bad:
        print(f"{SOURCE}:{number}: write outside snapshot_lock", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
