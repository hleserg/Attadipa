#!/usr/bin/env python3
"""Every write to MeshCore's `snapshot` happens under `snapshot_lock` (#344).

The UI task reads `snapshot` under the lock from before start_meshcore_ble()
runs, so a single unlocked write is a data race. The rule checked here: a line
that assigns to `snapshot` (or a member of it) must directly follow
`taskENTER_CRITICAL(&snapshot_lock);`. The one static initializer is exempt —
it runs before any task exists.
"""
import pathlib
import re
import sys

SOURCE = (pathlib.Path(__file__).resolve().parent.parent
          / "firmware" / "main" / "meshcore_ble.cpp")
WRITE = re.compile(
    r"(?<![\w.>:])snapshot(?:\.\w+|\[[^\]]*\])*\s*(?:[-+*/%|&^]|<<|>>)?=(?!=)")
LOCK = "taskENTER_CRITICAL(&snapshot_lock);"
INITIALIZER = "attadipa::core::MeshStatus snapshot = provider.status();"


def unlocked_writes(text):
    bad = []
    previous = ""
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.split("//", 1)[0].strip()
        if not line:
            continue
        if WRITE.search(line) and line != INITIALIZER and previous != LOCK:
            bad.append(number)
        previous = line
    return bad


def self_test():
    locked = f"{LOCK}\nsnapshot = next;\n"
    assert unlocked_writes(locked) == [], "a locked write was refused"
    for mutant in ("snapshot.availability = x;", "snapshot = next;",
                   "if (a) snapshot.mtu += 1;", f"{LOCK}\nf();\nsnapshot = n;"):
        assert unlocked_writes(mutant), f"an unlocked write passed: {mutant!r}"
    assert unlocked_writes("location_snapshot = p;\nx = session_snapshot();") == []


def main():
    self_test()
    bad = unlocked_writes(SOURCE.read_text(encoding="utf-8"))
    for number in bad:
        print(f"{SOURCE}:{number}: write to snapshot outside snapshot_lock",
              file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
