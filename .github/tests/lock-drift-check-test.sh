#!/usr/bin/env bash
# Does the dependency-lock gate actually gate?
#
# Offline and deterministic: two files in, an exit status out. The failure this
# guards against is the one the review of #397 named -- change `exit 1` to
# `exit 0` in the shipping script and every other check in the repository stays
# green, because `shellcheck -x` parses a script it never runs and
# `check-suite-coverage.sh` only asks whether a suite exists. This suite is what
# makes that edit red.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
script="$here/../scripts/lock-drift-check.sh"

pass=0; fail=0

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT

# A lock is a solved graph, not a list; the fixtures are shaped like one so that
# a diff of them reads the way a real one does.
cat > "$work/committed.lock" <<'LOCK'
dependencies:
  espressif/esp_lcd_touch:
    component_hash: 1111111111111111111111111111111111111111111111111111111111111111
    source:
      registry_url: https://components.espressif.com/
      type: service
    version: 1.2.1
manifest_hash: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
target: esp32s3
version: 2.0.0
LOCK

cp "$work/committed.lock" "$work/same.lock"

# One transitive version moved. This is the whole failure mode: the manifest
# still asks for `^1.2.0`, so nothing about the request changed and only the
# lock records that the answer did.
sed 's/version: 1\.2\.1/version: 1.2.2/' "$work/committed.lock" > "$work/moved.lock"

# want DESCRIPTION WANTED_STATUS ARGS...
want() {
  local desc="$1" wanted="$2"; shift 2
  local got=0
  bash "$script" "$@" >/dev/null 2>&1 || got=$?
  if [ "$got" -eq "$wanted" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted exit %s, got %s\n' "$desc" "$wanted" "$got"
  fi
}

# says DESCRIPTION SUBSTRING ARGS...
says() {
  local desc="$1" needle="$2"; shift 2
  local got
  got=$(bash "$script" "$@" 2>&1 >/dev/null)
  case "$got" in
    *"$needle"*)
      pass=$((pass + 1)); printf '  ok    %s\n' "$desc" ;;
    *)
      fail=$((fail + 1)); printf '  FAIL  %s\n         wanted stderr containing "%s", got "%s"\n' "$desc" "$needle" "$got" ;;
  esac
}

echo "Lock drift — what the gate does with the two files it is given"

want "a build that resolved what is committed passes" \
     0 "$work/committed.lock" "$work/same.lock"
want "a moved transitive version fails" \
     1 "$work/committed.lock" "$work/moved.lock"
want "drift is symmetric: which file moved is not the gate's business" \
     1 "$work/moved.lock" "$work/committed.lock"
want "no resolved lock is a broken build, not a clean graph" \
     2 "$work/committed.lock" "$work/absent.lock"
want "no committed lock is the same" \
     2 "$work/absent.lock" "$work/committed.lock"
want "one argument is a broken invocation" \
     2 "$work/committed.lock"
want "no arguments is a broken invocation" \
     2

echo
echo "What it says"

# The annotation is the only part a person reads on a red run, and `::error`
# without `title=` lands on no file. Both halves are checked.
says "drift is annotated so it lands on the run, not just in the log" \
     "::error title=Resolved dependency graph moved::" \
     "$work/committed.lock" "$work/moved.lock"
says "the annotation says what to do, not just that something moved" \
     "re-run the licence audit" \
     "$work/committed.lock" "$work/moved.lock"
says "a missing file names the file" \
     "$work/absent.lock" \
     "$work/committed.lock" "$work/absent.lock"

# The diff itself goes to stdout, because a reviewer needs to see which
# component moved and to what -- an exit status alone sends them to rerun the
# build locally.
moved_out=$(bash "$script" "$work/committed.lock" "$work/moved.lock" 2>/dev/null)
case "$moved_out" in
  *"-    version: 1.2.1"*"+    version: 1.2.2"*)
    pass=$((pass + 1)); printf '  ok    the drift itself is printed, both sides\n' ;;
  *)
    fail=$((fail + 1)); printf '  FAIL  the drift itself is printed, both sides\n         got "%s"\n' "$moved_out" ;;
esac

# A pass prints nothing. A gate that logs on every green run trains people to
# ignore it, and this one runs on every firmware build.
if [ -z "$(bash "$script" "$work/committed.lock" "$work/same.lock" 2>&1)" ]; then
  pass=$((pass + 1)); printf '  ok    a clean graph is silent\n'
else
  fail=$((fail + 1)); printf '  FAIL  a clean graph is silent\n'
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
