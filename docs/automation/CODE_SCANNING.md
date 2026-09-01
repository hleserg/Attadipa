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
parser. It used to write them to `/tmp/attadipa-replay-malformed.trace` and
`/tmp/attadipa-replay-freshness.trace` — fixed names in a directory anyone can
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

### A constructed path reaching `std::ifstream` in the replay tooling

`cpp/path-injection`, five instances across `tests/replay/replay_main.cpp` and
`tests/test_replay_rig.cpp`. The lines the scan named are deliberately not
recorded: they were a snapshot of a tree that has moved since, and a bare line
number into code we edit is the thing this repository stopped writing.

**Neither binary reads `argv`.** `replay_main` is `int main()` with no
parameters, and the directory it walks is a compile-time constant CMake defines
(`tests/replay/replay_main.cpp:21` —
"const std::filesystem::path fixture_dir = ATTADIPA_REPLAY_FIXTURE_DIR;").
`test_replay_rig` writes the fixtures it opens, into a directory it creates for
itself (`tests/test_replay_rig.cpp:55` — "::mkdtemp(&path_[0])"). Nothing
outside the build reaches either path.

So the query is not modelling an attacker here; it is modelling a caller that
does not exist. Neither binary is a service, neither crosses a privilege
boundary, and neither runs as anybody but the developer or the CI runner who
invoked it.

The remediation the query wants — canonicalise the path and check it against an
allowed root — would mean a replay tool that refuses to replay a trace stored
somewhere the tool did not choose, and a test that validates a path CMake
handed it. That is code written to satisfy a scanner rather than to do
anything, and it would make the tooling worse.

Dismissed as *used in tests*, one alert at a time. This note used to say that
the per-alert form was deliberate — that a `paths-ignore` for `tests/` would
also stop the scan seeing genuine defects in code that links against `core`.

**That is no longer what runs.** `.github/codeql/codeql-config.yml:2` —
"- tests/**" excludes the test tree, and the scan is given that config by
`.github/workflows/codeql.yml:47` — "config-file: ./.github/codeql". The five
dismissals above are history rather than the mechanism in force.

What that exclusion does and does not do is worth being exact about. The tests
are still **built and analysed**
(`.github/workflows/codeql.yml:5` — "Tests are built in this job"), so the
compilation path stays covered and the database is complete. What the exclusion
removes is the *alerts*: nothing in `tests/` is ever reported. So the cost the
old note named is being paid in full — a genuine defect in test code raises
nothing to triage — and it is paid silently, because a clean scan looks the
same either way. Whether that is the right scope is a
decision for whoever changes it; what this file records is which one is
running.

## If you are about to dismiss something

Say which query, where, and why the query's threat model does not apply here —
not that it "looks fine". A dismissal that only asserts the code is safe is
indistinguishable from one that never read it.
