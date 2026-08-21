# Code scanning: what it looks at, and what has been triaged

CodeQL runs over the host build path on every pull request, every push to
`main`, and weekly on a schedule — the workflow is
[`.github/workflows/codeql.yml`](../../.github/workflows/codeql.yml) and its
own header explains why it builds explicitly rather than with `autobuild`, and
why the simulator is excluded.

This file is the other half: a scanner is only useful if somebody reads it, and
a scanner nobody reads becomes a red check that gets clicked past. So every
alert that has been dismissed is written down here, with the reason, so that
the next person can disagree with the reasoning instead of guessing at it.

The rule for this file: **an alert is either fixed or recorded here.** There is
no third option where it is quietly dismissed and the reason lives in a chat
log.

## Fixed

### Fixtures written to a fixed path under a world-writable directory

`cpp/world-writable-file-creation`, twice, in `tests/test_replay_rig.cpp`.

The rig's own test builds malformed traces by hand and feeds them to the
parser. It used to write them to `/tmp/firefly-replay-malformed.trace` and
`/tmp/firefly-replay-freshness.trace` — fixed names in a directory anyone can
write to.

That is wrong twice over, and only one half of it is the scanner's half. Two
runs of the same test on one machine — a CI matrix, two people on a shared
build host — race for the same path and corrupt each other's fixture; and in a
world-writable directory, whoever creates the name first decides what it points
at. `std::fopen(path, "w")` compounds it by creating the file `0666` and
leaving it to an inherited umask to narrow, which is not ours to assume.

Fixed rather than dismissed, because the fix is smaller than the argument:
`mkdtemp()` for a directory that is unique and `0700` in a single syscall, and
`open(..., O_EXCL, S_IRUSR | S_IWUSR)` so the file is `0600` from the moment it
exists. No window between choosing a path and owning it.

## Dismissed

### A path from `argv` reaching `std::ifstream` in the replay tooling

`cpp/path-injection`, five instances: `tests/replay/replay_main.cpp:33` and
`tests/test_replay_rig.cpp` at 36, 61, 127 and 150.

Both of these binaries take a path to a trace file and open it. That is not an
oversight in the interface — it *is* the interface. `replay` is a developer
tool run by hand against a scenario file; `test_replay_rig` receives the
scenario directory from CMake and appends known fixture names to it. Neither is
a service, neither crosses a privilege boundary, and neither runs as anybody
but the developer or the CI runner who invoked it. The "attacker" the query
models is the person already typing the command.

The remediation the query wants — canonicalise the path and check it against an
allowed root — would mean a replay tool that refuses to replay a trace stored
somewhere the tool did not choose, and a test that validates a path CMake
handed it. That is code written to satisfy a scanner rather than to do
anything, and it would make the tooling worse.

Dismissed as *used in tests*. The dismissal is per-alert rather than a
`paths-ignore` for `tests/`, deliberately: excluding the test tree would also
stop the scan seeing genuine defects in code that links against `core`, and
those are worth seeing. The cost is that a future test which opens an `argv`
path raises a new alert to triage — which is the correct default, because the
next one might not be a test tool.

## If you are about to dismiss something

Say which query, where, and why the query's threat model does not apply here —
not that it "looks fine". A dismissal that only asserts the code is safe is
indistinguishable from one that never read it.
