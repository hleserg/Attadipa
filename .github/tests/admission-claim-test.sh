#!/usr/bin/env bash
# The production writer gate and the repository-backed exclusive claim.
# shellcheck disable=SC2015,SC2016  # Deliberate table assertions and literal sed mutants.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

# THE AMBIENT ENVIRONMENT DOES NOT GET TO DECIDE WHAT THIS SUITE PROVES,
# the same reason wip-limit-test.sh:29 says it. This suite is the one that
# proves the local writer reads the repository width, and every case below
# states the width it runs under by prefix assignment -- which ADDS to the
# child environment without clearing it. `ATTADIPA_WIP_LIMIT=4` exported by
# a runner turns four cases green on a queue width they were never written
# for, and this repository now has workflows that export exactly that
# (claude-agent.yml, claude-ci-repair.yml) around the step that runs an
# agent asked to run these checks.
unset ATTADIPA_WIP_LIMIT

# AND IT DOES NOT GET TO DECIDE WHERE A CLAIM CAME FROM EITHER. claim.sh writes
# `kind=hosted` when the acquire ran on the workflow path, which it reads from
# GITHUB_ACTIONS and GITHUB_RUN_ID -- and this suite itself runs inside Actions,
# where both are set. Left alone, every case below would claim as hosted against
# the id of whatever run is executing the tests, and the local cases would go
# green without ever exercising a local claim. Each case states its own
# provenance by prefix assignment instead (claim_as).
unset GITHUB_ACTIONS GITHUB_RUN_ID GITHUB_RUN_ATTEMPT

pass=0; fail=0
ok() { pass=$((pass + 1)); printf 'ok %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s: %s\n' "$1" "$2"; }

ADMISSION=.github/scripts/writer-admission.sh
CLAIM=.github/scripts/claim.sh
START=.github/scripts/writer-start.sh
AGENT=.github/workflows/claude-agent.yml
REPAIR=.github/workflows/claude-ci-repair.yml
WATCHDOG=.github/workflows/agent-queue-watchdog.yml

for required in "$ADMISSION" "$CLAIM" "$START"; do
  if [ -f "$required" ]; then ok "$required exists"; else bad "$required exists" missing; fi
done
if [ ! -f "$ADMISSION" ] || [ ! -f "$CLAIM" ] || [ ! -f "$START" ]; then
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin" "$work/state"

cat > "$work/bin/gh" <<'STUB'
#!/usr/bin/env bash
set -uo pipefail
state=${ATTADIPA_STUB_STATE:?}

label_edit() {
  shift 2
  while [ $# -gt 0 ]; do
    case "$1" in
      --add-label) grep -Fxq "$2" "$state/labels" 2>/dev/null || printf '%s\n' "$2" >> "$state/labels"; shift ;;
      --remove-label) sed -i "/^$(printf '%s' "$2" | sed 's/[][\\.^$*]/\\&/g')$/d" "$state/labels"; shift ;;
    esac
    shift
  done
}

if [ "${1-}" = pr ] && [ "${2-}" = list ]; then
  if [ "${ATTADIPA_STUB_MODE:-ok}" = apifail ]; then
    echo 'api unavailable' >&2; exit 1
  fi
  fields=""
  for arg in "$@"; do
    if [ "$fields" = next ]; then fields=$arg; break; fi
    [ "$arg" = --json ] && fields=next
  done
  case ",$fields," in *,baseRepository,*) echo 'Unknown JSON field: "baseRepository"' >&2; exit 1 ;; esac
  cat "$state/prs"
  exit 0
fi

if { [ "${1-}" = issue ] || [ "${1-}" = pr ]; } && [ "${2-}" = edit ]; then
  label_edit "$@"; exit 0
fi

# The repository variable the local entrypoint reads for the queue width.
# ATTADIPA_STUB_WIDTH unset reproduces what gh really does for a variable
# nobody set, checked on the machine 2026-08-31: nothing on stdout, "variable
# ... was not found" on stderr, exit 1. Answering with empty output and exit 0
# would let writer-start.sh pass a test its production caller fails.
if [ "${1-}" = variable ] && [ "${2-}" = get ]; then
  if [ -n "${ATTADIPA_STUB_WIDTH_REFUSED-}" ]; then
    echo "$ATTADIPA_STUB_WIDTH_REFUSED" >&2; exit 1
  fi
  if [ -n "${ATTADIPA_STUB_WIDTH-}" ]; then printf '%s\n' "$ATTADIPA_STUB_WIDTH"; exit 0; fi
  echo "variable ${3-} was not found" >&2; exit 1
fi

[ "${1-}" = api ] || { echo "unexpected gh call: $*" >&2; exit 64; }
shift
method=GET; path=""; fields=()
while [ $# -gt 0 ]; do
  case "$1" in
    --method) method=$2; shift ;;
    -f|-F) fields+=("$2"); shift ;;
    --jq) shift ;;
    -*) ;;
    *) [ -z "$path" ] && path=$1 ;;
  esac
  shift
done
field() { local pair; for pair in "${fields[@]}"; do case "$pair" in "$1="*) printf '%s' "${pair#*=}"; return ;; esac; done; }

case "$method:$path" in
  GET:repos/o/r)
    printf '{"default_branch":"main"}\n' ;;
  GET:repos/o/r/git/ref/heads/main)
    printf '{"object":{"sha":"0123456789abcdef0123456789abcdef01234567"}}\n' ;;
  GET:repos/o/r/issues/7)
    [ "${ATTADIPA_STUB_MODE:-ok}" = issuefail ] && exit 1
    labels=$(jq -Rsc 'split("\n") | map(select(length > 0) | {name:.})' "$state/labels")
    if [ "$(cat "$state/kind")" = pr ]; then pr='{"url":"x"}'; else pr=null; fi
    jq -nc --argjson labels "$labels" --argjson pr "$pr" '{state:"open", labels:$labels, pull_request:$pr}' ;;
  GET:repos/o/r/issues/7/timeline?per_page=100)
    jq -nc --arg date "$(cat "$state/timeline-date")" '[[{event:"labeled",label:{name:"agent:working"},created_at:$date}]]' ;;
  POST:repos/o/r/git/tags)
    token=$(field message); date=$(field 'tagger[date]')
    sha=$(printf '%s' "$token" | sha1sum | cut -c1-40)
    jq -nc --arg sha "$sha" --arg message "$token" --arg date "$date" \
      '{sha:$sha,message:$message,tagger:{date:$date}}' > "$state/tag.$sha"
    printf '{"sha":"%s"}\n' "$sha" ;;
  POST:repos/o/r/git/refs)
    ref=$(field ref); suffix=${ref##*/}
    prefix=ref; [ "$suffix" = writer ] && prefix=writer
    if [ "${ATTADIPA_STUB_BARRIER:-0}" = 1 ]; then
      : > "$state/ready.$$"
      for _ in $(seq 1 200); do
        [ "$(find "$state" -name 'ready.*' | wc -l)" -ge 2 ] && break
        sleep 0.01
      done
    fi
    if mkdir "$state/$prefix.lock" 2>/dev/null; then
      field sha > "$state/$prefix.sha"
      # GitHub's reads lag its writes: ATTADIPA_STUB_REF_LAG=N answers 404 to
      # the next N reads of a ref that was just created (#392).
      [ -z "${ATTADIPA_STUB_REF_LAG-}" ] || printf '%s\n' "$ATTADIPA_STUB_REF_LAG" > "$state/lag.$prefix"
      printf '{"ref":"%s"}\n' "$ref"
    else
      echo 'Reference already exists' >&2; exit 1
    fi ;;
  GET:repos/o/r/git/ref/tags/attadipa-claims/7 | GET:repos/o/r/git/ref/tags/attadipa-claims/writer)
    # A ref that is not there and a ref that cannot be read are different
    # answers, and claim.sh now only accepts the first as proof of absence.
    # Real gh writes "gh: Not Found (HTTP 404)" and exits 1; reproduced here
    # because a stub that exits silently would let the swallowing version pass.
    if [ -n "${ATTADIPA_STUB_REF_GET_FAIL-}" ]; then
      echo 'gh: Server Error (HTTP 500)' >&2; exit 1
    fi
    lock=ref; [ "${path##*/}" = writer ] && lock=writer
    if [ ! -d "$state/$lock.lock" ]; then echo 'gh: Not Found (HTTP 404)' >&2; exit 1; fi
    if [ -s "$state/lag.$lock" ] && [ "$(cat "$state/lag.$lock")" -gt 0 ]; then
      echo "$(($(cat "$state/lag.$lock") - 1))" > "$state/lag.$lock"
      echo 'gh: Not Found (HTTP 404)' >&2; exit 1
    fi
    printf '{"object":{"sha":"%s"}}\n' "$(cat "$state/$lock.sha")" ;;
  GET:repos/o/r/actions/runs/*)
    # The completion evidence behind a hosted holder. No file: no readable run.
    if [ ! -f "$state/run-status" ]; then echo 'gh: Not Found (HTTP 404)' >&2; exit 1; fi
    jq -nc --arg s "$(cat "$state/run-status")" '{status:$s}' ;;
  GET:repos/o/r/git/tags/*)
    cat "$state/tag.${path##*/}" ;;
  DELETE:repos/o/r/git/refs/tags/attadipa-claims/7 | DELETE:repos/o/r/git/refs/tags/attadipa-claims/writer)
    if [ -n "${ATTADIPA_STUB_DELETE_REFUSED-}" ]; then
      echo 'gh: Resource not accessible by integration (HTTP 403)' >&2; exit 1
    fi
    lock=ref; [ "${path##*/}" = writer ] && lock=writer
    rm -rf "$state/$lock.lock"; rm -f "$state/$lock.sha" ;;
  *) echo "unexpected api call: $method $path" >&2; exit 64 ;;
esac
STUB
chmod +x "$work/bin/gh"

reset_state() {
  rm -rf "$work/state"; mkdir -p "$work/state"
  : > "$work/state/labels"; printf 'issue\n' > "$work/state/kind"; printf '[]\n' > "$work/state/prs"
}
pr() { printf '{"number":%s,"isCrossRepository":false,"labels":%s}' "$1" "${2-[]}"; }

run_admission() {
  : > "$work/output"
  PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_OUTPUT="$work/output" \
    GITHUB_REPOSITORY=o/r bash "$ADMISSION" o/r 7 > "$work/log" 2>&1
  printf '%s %s\n' "$(sed -n 's/^allow=//p' "$work/output")" "$(sed -n 's/^state=//p' "$work/output")"
}

echo 'Writer admission executes the real queue transport'
reset_state
[ "$(run_admission)" = 'true ok' ] && ok 'zero active PRs admits' || bad 'zero active PRs admits' "$(cat "$work/log")"
printf '[%s]\n' "$(pr 1)" > "$work/state/prs"
[ "$(run_admission)" = 'true ok' ] && ok 'one active PR admits' || bad 'one active PR admits' "$(cat "$work/log")"
printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
[ "$(run_admission)" = 'false full' ] && ok 'two active PRs fail closed at full' || bad 'two active PRs fail closed at full' "$(cat "$work/log")"
printf '[%s,%s,%s]\n' "$(pr 1)" "$(pr 2)" "$(pr 3)" > "$work/state/prs"
[ "$(run_admission)" = 'false incident' ] && ok 'three active PRs enter incident drain mode' || bad 'three active PRs enter incident drain mode' "$(cat "$work/log")"
printf '[%s,%s,%s,%s,%s,%s,%s]\n' "$(pr 1)" "$(pr 2)" "$(pr 3)" "$(pr 4)" "$(pr 5)" "$(pr 6)" "$(pr 7)" > "$work/state/prs"
[ "$(run_admission)" = 'false incident' ] && ok 'seven active PRs stay incident' || bad 'seven active PRs stay incident' "$(cat "$work/log")"
printf 'queue:emergency\n' > "$work/state/labels"
[ "$(run_admission)" = 'true emergency' ] && ok 'an explicitly labelled emergency may drain the incident' || bad 'an explicitly labelled emergency may drain the incident' "$(cat "$work/log")"
: > "$work/state/labels"; printf 'pr\n' > "$work/state/kind"
[ "$(run_admission)" = 'true recovery' ] && ok 'work on an existing PR may drain the incident' || bad 'work on an existing PR may drain the incident' "$(cat "$work/log")"
printf 'issue\n' > "$work/state/kind"; ATTADIPA_STUB_MODE=issuefail run_admission >/dev/null
grep -q '^allow=false$' "$work/output" && grep -q '^state=unknown$' "$work/output" \
  && ok 'an unreadable target fails closed' || bad 'an unreadable target fails closed' "$(cat "$work/log")"

echo 'The holder id is published, so a credential can never become one'
long_holder=$(printf 'x%.0s' $(seq 1 65))
for unsafe in ghp_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
              github_pat_11AAAAAAA0aaaaaaaaaaaa_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb \
              0123456789abcdef0123456789abcdef01234567 \
              'has space' "$long_holder"; do
  reset_state
  PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 "$unsafe" >/dev/null 2>&1
  rc=$?
  shape=$(printf '%.14s' "$unsafe")
  if [ "$rc" -eq 64 ]; then ok "a credential-shaped holder ($shape) is refused"; else bad "a credential-shaped holder ($shape) is refused" "rc=$rc"; fi
  # The tag object outlives the deletion of its ref, so refusing after the POST
  # would not be refusing at all: assert nothing was ever sent.
  if [ -z "$(find "$work/state" -maxdepth 1 -name 'tag.*' -print -quit)" ] && [ ! -d "$work/state/ref.lock" ]; then
    ok "no tag is created for ($shape)"
  else
    bad "no tag is created for ($shape)" 'a tag object reached the API'
  fi
done

reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 agent-32863426318-1 >/dev/null 2>&1
[ -d "$work/state/ref.lock" ] && ok 'the shape the workflows actually pass is still accepted' || bad 'the shape the workflows actually pass is still accepted' 'refused a valid agent id'
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" release o/r 7 agent-32863426318-1 >/dev/null 2>&1

reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  bash "$START" start o/r 7 ghp_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA >/dev/null 2>&1
rc=$?
[ "$rc" -ne 0 ] && [ ! -d "$work/state/writer.lock" ] \
  && ok 'the documented entrypoint refuses a credential too' || bad 'the documented entrypoint refuses a credential too' "rc=$rc"
grep -q 'AGENT_ID' AGENTS.md && ! grep -q 'writer-start.sh start REPO ISSUE TOKEN' AGENTS.md \
  && ok 'AGENTS.md no longer calls the holder a token' || bad 'AGENTS.md no longer calls the holder a token' 'the misleading name survives'

echo 'The GitHub ref is the atomic claim'
reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_BARRIER=1 \
  bash "$CLAIM" acquire o/r 7 run-a > "$work/a.log" 2>&1 & pa=$!
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_BARRIER=1 \
  bash "$CLAIM" acquire o/r 7 run-b > "$work/b.log" 2>&1 & pb=$!
wait "$pa"; ra=$?; wait "$pb"; rb=$?
successes=0; [ "$ra" -eq 0 ] && successes=$((successes + 1)); [ "$rb" -eq 0 ] && successes=$((successes + 1))
[ "$successes" -eq 1 ] && ok 'two simultaneous claims have exactly one winner' || bad 'two simultaneous claims have exactly one winner' "rc=$ra,$rb"
winner=run-a; [ "$rb" -eq 0 ] && winner=run-b
loser=run-b; [ "$winner" = run-b ] && loser=run-a
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" release o/r 7 "$loser" >/dev/null 2>&1
[ -d "$work/state/ref.lock" ] && ok 'the loser cannot release the winner' || bad 'the loser cannot release the winner' 'lock disappeared'
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" release o/r 7 "$winner" >/dev/null 2>&1
[ ! -d "$work/state/ref.lock" ] && ! grep -Fxq agent:working "$work/state/labels" \
  && ok 'the winner releases ref and visible label' || bad 'the winner releases ref and visible label' 'claim remains'

# A claim is only stale once it is proven finished, and what proves that is the
# Actions run the claim recorded when it was acquired -- not how its holder id
# happens to be spelled (#369).
age_tag() {
  local tag; tag=$(cat "$work/state/${1:-ref}.sha")
  jq '.tagger.date="2000-01-01T00:00:00Z"' "$work/state/tag.$tag" > "$work/state/old"
  mv "$work/state/old" "$work/state/tag.$tag"
}
# claim_as KIND NUMBER HOLDER [SCRIPT] [RUN_ID] -- acquire and age in one step.
# `hosted` runs the acquire with the workflow path's environment, `local` without
# it; the holder id is deliberately free to be identical in both.
claim_as() {
  local kind=$1 number=$2 holder=$3 script=${4:-$CLAIM} run_id=${5:-4242} lock=ref
  case "$number" in writer) lock=writer ;; esac
  if [ "$kind" = hosted ]; then
    PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" \
      GITHUB_ACTIONS=true GITHUB_RUN_ID="$run_id" GITHUB_RUN_ATTEMPT=1 \
      bash "$script" acquire o/r "$number" "$holder" >/dev/null 2>&1
  else
    PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" \
      bash "$script" acquire o/r "$number" "$holder" >/dev/null 2>&1
  fi
  age_tag "$lock"
}
claimed() { claim_as local 7 "${2:-agent-4242-1}" "${1:-$CLAIM}"; }
# Leaves the outcome in `reap_status` and `reap_err` rather than returning it:
# a helper that returned 3 would trip the errexit the cases below switch on, and
# a `set -e` restored unconditionally would switch it on for everything after.
reap_rc() {
  local errexit=0
  case $- in *e*) errexit=1 ;; esac
  set +e
  reap_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" \
    bash "${2:-$CLAIM}" reap o/r "${1:-7}" 7200 2>&1 >/dev/null)"
  reap_status=$?
  [ "$errexit" -eq 0 ] || set -e
}

reset_state; printf 'completed\n' > "$work/state/run-status"; claim_as hosted 7 agent-4242-1
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" reap o/r 7 7200 >/dev/null
[ ! -d "$work/state/ref.lock" ] && ok 'a crashed writer whose run has finished is reaped after the stale bound' \
  || bad 'a crashed writer whose run has finished is reaped after the stale bound' 'lock remains'

# The holder id is a label the caller picks, and AGENTS.md publishes an example
# of it. THE SAME EXACT STRING, CLAIMED FROM A TERMINAL, IS NOT THAT RUN: run
# 4242 finished, the person is still typing, and reaping here would hand a second
# writer the task the first one is holding (#369).
reset_state; printf 'completed\n' > "$work/state/run-status"; claim_as local 7 agent-4242-1
reap_rc 7; collision_rc=$reap_status
[ "$collision_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] \
  && ok 'a local claim is not reaped because a finished run shares its holder id' \
  || bad 'a local claim is not reaped because a finished run shares its holder id' \
        "rc=$collision_rc, lock gone=$([ -d "$work/state/ref.lock" ] || echo yes)"
second_writer_rc=0
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 agent-4243-1 >/dev/null 2>&1 \
  || second_writer_rc=$?
[ "$second_writer_rc" -eq 3 ] \
  && ok 'no second writer follows a local claim the watchdog declined to reap' \
  || bad 'no second writer follows a local claim the watchdog declined to reap' "rc=$second_writer_rc"

# ...and the mirror image: a hosted claim is recoverable whatever its holder is
# called. `ci-repair-<run>-<attempt>` is what claude-ci-repair.yml actually
# passes, and no `agent-` regex ever matched it, so a dead repair run used to
# hold the queue until a human broke it by hand.
reset_state; printf 'completed\n' > "$work/state/run-status"; claim_as hosted 7 ci-repair-4242-1
reap_rc 7
[ "$reap_status" -eq 0 ] && [ ! -d "$work/state/ref.lock" ] \
  && ok 'a finished hosted run is recovered whatever its holder id is called' \
  || bad 'a finished hosted run is recovered whatever its holder id is called' "rc=$reap_status, lock remains"

# The global lease and a per-task claim are the same rule, read from the same
# place. A writer lease nobody can reap is the whole repository, not one issue.
reset_state; printf 'completed\n' > "$work/state/run-status"; claim_as local writer agent-4242-1
reap_rc writer; writer_local_rc=$reap_status
[ "$writer_local_rc" -eq 3 ] && [ -d "$work/state/writer.lock" ] \
  && ok 'the global writer lease follows the same provenance rule as a task claim' \
  || bad 'the global writer lease follows the same provenance rule as a task claim' "rc=$writer_local_rc"
reset_state; printf 'completed\n' > "$work/state/run-status"; claim_as hosted writer agent-4242-1
reap_rc writer
[ "$reap_status" -eq 0 ] && [ ! -d "$work/state/writer.lock" ] \
  && ok 'a hosted writer lease whose run finished is still recovered' \
  || bad 'a hosted writer lease whose run finished is still recovered' "rc=$reap_status, lease remains"

# A claim written before provenance existed carries one line and no `kind`.
# Unattributed is not finished: it holds, and the log says so.
reset_state; printf 'completed\n' > "$work/state/run-status"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 agent-4242-1 >/dev/null 2>&1
legacy_tag=$(cat "$work/state/ref.sha")
jq '.message="agent-4242-1" | .tagger.date="2000-01-01T00:00:00Z"' "$work/state/tag.$legacy_tag" \
  > "$work/state/old" && mv "$work/state/old" "$work/state/tag.$legacy_tag"
reap_rc 7; legacy_rc=$reap_status
[ "$legacy_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] \
  && ok 'a pre-provenance claim is not reaped on the strength of its name' \
  || bad 'a pre-provenance claim is not reaped on the strength of its name' "rc=$legacy_rc"
case "$reap_err" in
  *'records no provenance'*) ok 'the refusal says the claim carries no provenance' ;;
  *) bad 'the refusal says the claim carries no provenance' \
         "stderr was: $(printf '%s' "$reap_err" | tr '\n' ' ')" ;;
esac
# The holder still round-trips: a release by the writer that made it must match.
[ "$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" owner o/r 7)" = agent-4242-1 ] \
  && ok 'a pre-provenance tag still reads back as its holder' \
  || bad 'a pre-provenance tag still reads back as its holder' 'the owner is not the holder'

# #254, root cause 2: age was the whole test, so a local writer still typing was
# indistinguishable from a dead one. It has no Actions run and never will.
reset_state; claimed "$CLAIM" agent-i254-r1
set +e
local_reap_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" \
  bash "$CLAIM" reap o/r 7 7200 2>&1 >/dev/null)"; local_reap_rc=$?
set -e
[ "$local_reap_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] \
  && ok 'an ancient local lease is not reaped by age alone' \
  || bad 'an ancient local lease is not reaped by age alone' "rc=$local_reap_rc, lock gone=$([ -d "$work/state/ref.lock" ] || echo yes)"
case "$local_reap_err" in
  *'claim.sh break o/r 7'*) ok 'refusing to reap names the command that releases it by hand' ;;
  *) bad 'refusing to reap names the command that releases it by hand' "stderr was: $(printf '%s' "$local_reap_err" | tr '\n' ' ')" ;;
esac

# The same refusal for a hosted claim whose run is still going: an agent that
# is slow is not an agent that died.
reset_state; printf 'in_progress\n' > "$work/state/run-status"; claim_as hosted 7 agent-4242-1
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" reap o/r 7 7200 >/dev/null 2>&1
live_reap_rc=$?
set -e
[ "$live_reap_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] \
  && ok 'a hosted claim whose run is still going is not reaped' \
  || bad 'a hosted claim whose run is still going is not reaped' "rc=$live_reap_rc"

# ...and for a hosted claim whose run cannot be read at all. Unknown is not
# finished.
reset_state; claim_as hosted 7 agent-4242-1
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" reap o/r 7 7200 >/dev/null 2>&1
norun_reap_rc=$?
set -e
[ "$norun_reap_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] \
  && ok 'a run that cannot be read is not proof the writer stopped' \
  || bad 'a run that cannot be read is not proof the writer stopped' "rc=$norun_reap_rc"

# #254, root cause 1: recovery reported success while the ref survived, and took
# the visible label off first, so the queue was held by a claim nobody could see.
reset_state; claimed
set +e
break_out="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_DELETE_REFUSED=1 \
  bash "$CLAIM" break o/r 7 2>"$work/break.err")"; break_rc=$?
set -e
[ "$break_rc" -ne 0 ] && [ "$break_out" != 'claim cleared' ] \
  && ok 'a DELETE the token may not make is not a cleared claim' \
  || bad 'a DELETE the token may not make is not a cleared claim' "rc=$break_rc, stdout=$break_out"
[ -d "$work/state/ref.lock" ] && grep -Fxq agent:working "$work/state/labels" \
  && ok 'a refused break leaves the claim visible instead of hiding it' \
  || bad 'a refused break leaves the claim visible instead of hiding it' 'the label went but the lock stayed'
grep -q 'refs/tags/attadipa-claims/7' "$work/break.err" \
  && ok 'the refusal names the ref that still holds the queue' \
  || bad 'the refusal names the ref that still holds the queue' "stderr was: $(tr '\n' ' ' < "$work/break.err")"
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 agent-4243-1 >/dev/null 2>&1
after_break_rc=$?
set -e
[ "$after_break_rc" -eq 3 ] && ok 'no second writer follows a break that did not clear' \
  || bad 'no second writer follows a break that did not clear' "rc=$after_break_rc"

# A 500 is not a 404. The version this replaced treated any unreadable ref as an
# absent one, which is the same bug wearing a different failure.
reset_state; claimed
set +e
transient_out="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" \
  ATTADIPA_STUB_DELETE_REFUSED=1 ATTADIPA_STUB_REF_GET_FAIL=1 bash "$CLAIM" break o/r 7 2>/dev/null)"
transient_rc=$?
set -e
[ "$transient_rc" -ne 0 ] && [ "$transient_out" != 'claim cleared' ] && [ -d "$work/state/ref.lock" ] \
  && ok 'an unreadable ref is never read as an absent one' \
  || bad 'an unreadable ref is never read as an absent one' "rc=$transient_rc, stdout=$transient_out"

# #254, root cause 3: a claim that failed after its ref existed left that ref
# behind, and nothing carried the holder id, so no later release could match it.
# `issuefail` makes the label edit fail exactly where production's would.
reset_state
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_MODE=issuefail \
  bash "$CLAIM" acquire o/r 7 agent-4242-1 >/dev/null 2>&1
orphan_rc=$?
set -e
[ "$orphan_rc" -eq 2 ] && [ ! -d "$work/state/ref.lock" ] \
  && ok 'a claim that fails after its ref exists takes the ref with it' \
  || bad 'a claim that fails after its ref exists takes the ref with it' "rc=$orphan_rc, orphan=$([ -d "$work/state/ref.lock" ] && echo yes)"

reset_state
set +e
orphan_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_MODE=issuefail \
  ATTADIPA_STUB_DELETE_REFUSED=1 bash "$CLAIM" acquire o/r 7 agent-4242-1 2>&1 >/dev/null)"
set -e
case "$orphan_err" in
  *'left behind'*'refs/tags/attadipa-claims/7'*)
    ok 'an orphan it cannot remove is named rather than left silent' ;;
  *) bad 'an orphan it cannot remove is named rather than left silent' \
         "stderr was: $(printf '%s' "$orphan_err" | tr '\n' ' ')" ;;
esac

# #392: the create is the compare-and-set; the read-back only confirms it. A
# ref that answers 404 for a moment after its own creation is lag, not loss.
reset_state
set +e
lag_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_REF_LAG=1 \
  bash "$CLAIM" acquire o/r 7 agent-4242-1 2>&1 >/dev/null)"
lag_rc=$?
set -e
lag_owner="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" owner o/r 7 2>/dev/null)" || lag_owner=none
[ "$lag_rc" -eq 0 ] && [ -d "$work/state/ref.lock" ] && [ "$lag_owner" = agent-4242-1 ] \
  && case "$lag_err" in *'read back after 2 attempts'*) true ;; *) false ;; esac \
  && ok 'a read-back that lags the create is retried, not read as a lost claim' \
  || bad 'a read-back that lags the create is retried, not read as a lost claim' \
         "rc=$lag_rc, owner=$lag_owner, stderr=$(printf '%s' "$lag_err" | tr '\n' ' ')"

# A read-back that never settles cannot un-win the claim: the ref stays, the
# acquire succeeds, and stderr says the confirmation is missing.
reset_state
set +e
unread_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_REF_GET_FAIL=1 \
  bash "$CLAIM" acquire o/r 7 agent-4242-1 2>&1 >/dev/null)"
unread_rc=$?
set -e
[ "$unread_rc" -eq 0 ] && [ -d "$work/state/ref.lock" ] \
  && case "$unread_err" in *'not readable back'*'trusting the create'*) true ;; *) false ;; esac \
  && ok 'an unreadable read-back leaves the won ref intact and reports success' \
  || bad 'an unreadable read-back leaves the won ref intact and reports success' \
         "rc=$unread_rc, ref=$([ -d "$work/state/ref.lock" ] && echo kept || echo gone), stderr=$(printf '%s' "$unread_err" | tr '\n' ' ')"

# The undo path has the same rule: it deletes only a ref it positively read as
# its own. Unknown is left in place and named.
reset_state
set +e
unread_orphan_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_MODE=issuefail \
  ATTADIPA_STUB_REF_GET_FAIL=1 bash "$CLAIM" acquire o/r 7 agent-4242-1 2>&1 >/dev/null)"
unread_orphan_rc=$?
set -e
[ "$unread_orphan_rc" -eq 2 ] && [ -d "$work/state/ref.lock" ] \
  && case "$unread_orphan_err" in *'left behind'*'refs/tags/attadipa-claims/7'*) true ;; *) false ;; esac \
  && ok 'a failed claim never deletes a ref it could not read' \
  || bad 'a failed claim never deletes a ref it could not read' \
         "rc=$unread_orphan_rc, ref=$([ -d "$work/state/ref.lock" ] && echo kept || echo gone), stderr=$(printf '%s' "$unread_orphan_err" | tr '\n' ' ')"

# `release` meets the same lag as the read-back, seconds after the same
# create: the ordinary `held: full` exit of writer-start.sh is acquire, deny,
# release. A lagging read is retried rather than answered "nothing held".
reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 agent-4242-1 >/dev/null 2>&1
printf '1\n' > "$work/state/lag.ref"
set +e
lag_release_out="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" release o/r 7 agent-4242-1 2>/dev/null)"
lag_release_rc=$?
set -e
[ "$lag_release_rc" -eq 0 ] && [ ! -d "$work/state/ref.lock" ] \
  && case "$lag_release_out" in *'released by agent-4242-1'*) true ;; *) false ;; esac \
  && ok 'a release whose read lags the create is retried, not refused' \
  || bad 'a release whose read lags the create is retried, not refused' \
         "rc=$lag_release_rc, ref=$([ -d "$work/state/ref.lock" ] && echo kept || echo gone), stdout=$lag_release_out"

# A release that never learns who holds the ref deletes nothing -- and says so.
# Every caller writes `|| true`, so silence here is a leaked lease nobody sees.
reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 agent-4242-1 >/dev/null 2>&1
set +e
unread_release_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_REF_GET_FAIL=1 \
  bash "$CLAIM" release o/r 7 agent-4242-1 2>&1 >/dev/null)"
unread_release_rc=$?
set -e
[ "$unread_release_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] \
  && case "$unread_release_err" in *'left behind'*'claim.sh break o/r 7'*) true ;; *) false ;; esac \
  && ok 'a release that cannot read the holder names the ref it left behind' \
  || bad 'a release that cannot read the holder names the ref it left behind' \
         "rc=$unread_release_rc, ref=$([ -d "$work/state/ref.lock" ] && echo kept || echo gone), stderr=$(printf '%s' "$unread_release_err" | tr '\n' ' ')"

# `reap` is the third reader, and the one with the most to lose: its migration
# path decides by age alone and deletes before it confirms. An unreadable ref
# over an aged live local claim must not fall into it.
reset_state; claimed "$CLAIM" agent-i254-r1
printf 'agent:working\n' > "$work/state/labels"; printf '2000-01-01T00:00:00Z\n' > "$work/state/timeline-date"
set +e
unread_reap_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_REF_GET_FAIL=1 \
  bash "$CLAIM" reap o/r 7 7200 2>&1 >/dev/null)"
unread_reap_rc=$?
set -e
[ "$unread_reap_rc" -eq 3 ] && [ -d "$work/state/ref.lock" ] && ! grep -Fxq agent:ready "$work/state/labels" \
  && case "$unread_reap_err" in *'could not be read'*) true ;; *) false ;; esac \
  && ok 'an unreadable ref is not reaped by age through the migration path' \
  || bad 'an unreadable ref is not reaped by age through the migration path' \
         "rc=$unread_reap_rc, ref=$([ -d "$work/state/ref.lock" ] && echo kept || echo gone), ready=$(grep -Fxq agent:ready "$work/state/labels" && echo yes || echo no), stderr=$(printf '%s' "$unread_reap_err" | tr '\n' ' ')"

reset_state; printf 'agent:working\n' > "$work/state/labels"; printf '2000-01-01T00:00:00Z\n' > "$work/state/timeline-date"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" reap o/r 7 7200 >/dev/null
! grep -Fxq agent:working "$work/state/labels" && grep -Fxq agent:ready "$work/state/labels" \
  && ok 'a legacy stale working label is recovered from its label event' || bad 'a legacy stale working label is recovered from its label event' remains

echo 'The local entrypoint holds both admission and claim'
reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  bash "$START" start o/r 7 local-a >/dev/null
[ -d "$work/state/writer.lock" ] && [ -d "$work/state/ref.lock" ] \
  && ok 'local start owns the global lease and task claim' || bad 'local start owns the global lease and task claim' missing
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  bash "$START" finish o/r 7 local-a >/dev/null
[ ! -d "$work/state/writer.lock" ] && [ ! -d "$work/state/ref.lock" ] \
  && ok 'local finish releases both locks' || bad 'local finish releases both locks' remains

reset_state
printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  bash "$START" start o/r 7 local-full >/dev/null 2>&1
local_full_rc=$?
set -e
[ "$local_full_rc" -eq 3 ] && [ ! -d "$work/state/writer.lock" ] && [ ! -d "$work/state/ref.lock" ] \
  && ok 'full admission leaves no local writer or claim' || bad 'full admission leaves no local writer or claim' "rc=$local_full_rc"

# THE WIDTH THE OWNER LIFTED HAS TO REACH THE COMMAND A PERSON RUNS.
# In a workflow the gate gets ATTADIPA_WIP_LIMIT from `vars`; writer-start.sh has
# no `vars` context and reads the repository variable itself. Same two pull
# requests as the case above -- the only difference is what the repository
# answers -- so this is the by-hand comparison from #354 made repeatable.
reset_state
printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  ATTADIPA_STUB_WIDTH=4 bash "$START" start o/r 7 local-lifted >/dev/null 2>&1
local_lifted_rc=$?
set -e
[ "$local_lifted_rc" -eq 0 ] && [ -d "$work/state/writer.lock" ] && [ -d "$work/state/ref.lock" ] \
  && ok 'the local writer admits at the width the repository carries' \
  || bad 'the local writer admits at the width the repository carries' "rc=$local_lifted_rc"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  bash "$START" finish o/r 7 local-lifted >/dev/null 2>&1

# An explicit value still wins, so a bench run can pin a width -- and the header
# says not to export one for any other reason, because this is what it does.
reset_state
printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
set +e
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  ATTADIPA_STUB_WIDTH=4 ATTADIPA_WIP_LIMIT=2 bash "$START" start o/r 7 local-pinned >/dev/null 2>&1
local_pinned_rc=$?
set -e
[ "$local_pinned_rc" -eq 3 ] \
  && ok 'an explicit width in the environment still wins over the repository' \
  || bad 'an explicit width in the environment still wins over the repository' "rc=$local_pinned_rc"

# The mutation the finding named: delete the lookup and everything else stays
# green while the local writer is back at the designed width.
#
# writer-start.sh resolves its siblings from `dirname "$0"`, so a mutant dropped
# in a scratch directory dies with 127 rather than exercising anything. Mirror
# the whole script directory by symlink and override the one file, so the mutant
# reaches the same claim.sh and writer-admission.sh production reaches.
mkdir -p "$work/scripts"
for f in "$(dirname "$START")"/*; do ln -sf "$(cd "$(dirname "$f")" && pwd)/$(basename "$f")" "$work/scripts/"; done
rm -f "$work/scripts/$(basename "$START")"
sed 's#gh variable get ATTADIPA_WIP_LIMIT --repo "$repo" 2>&1#true#' \
  "$START" > "$work/scripts/$(basename "$START")"
if grep -q 'gh variable get ATTADIPA_WIP_LIMIT' "$work/scripts/$(basename "$START")"; then
  bad 'deleting the width lookup makes the mutation red' \
      'the mutation did not remove the lookup -- the line moved, repoint this sed'
else
  reset_state
  printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
  set +e
  PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
    ATTADIPA_STUB_WIDTH=4 bash "$work/scripts/$(basename "$START")" start o/r 7 local-mutant >/dev/null 2>&1
  local_mutant_rc=$?
  set -e
  [ "$local_mutant_rc" -eq 3 ] \
    && ok 'deleting the width lookup makes the mutation red' \
    || bad 'deleting the width lookup makes the mutation red' \
          "expected 3 (held: full) from a writer that cannot see the lifted width, got rc=$local_mutant_rc"
fi

# A REFUSAL AND AN ABSENCE ARRIVE AS THE SAME EMPTY STRING, and only one of them
# is worth saying. Reading Actions variables is its own permission: a token that
# can push, comment and list pull requests may still be refused here, and then
# the writer runs at the designed width while Actions admits at the lifted one.
reset_state
printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
set +e
refused_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  ATTADIPA_STUB_WIDTH_REFUSED='HTTP 403: Resource not accessible by integration' \
  bash "$START" start o/r 7 local-refused 2>&1 >/dev/null)"
set -e
case "$refused_err" in
  *'could not read ATTADIPA_WIP_LIMIT'*'403'*)
    ok 'a width lookup the token may not make is spoken, not swallowed' ;;
  *) bad 'a width lookup the token may not make is spoken, not swallowed' \
         "stderr was: $(printf '%s' "$refused_err" | tr '\n' ' ')" ;;
esac

# The other direction, which is what stops this becoming noise: a width nobody
# set is the ordinary case and says nothing about itself.
reset_state
printf '[%s,%s]\n' "$(pr 1)" "$(pr 2)" > "$work/state/prs"
set +e
unset_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  bash "$START" start o/r 7 local-unset 2>&1 >/dev/null)"
set -e
case "$unset_err" in
  *'could not read'*) bad 'a width nobody set is silent' \
      "stderr was: $(printf '%s' "$unset_err" | tr '\n' ' ')" ;;
  *) ok 'a width nobody set is silent' ;;
esac

# `held: full` on its own cannot be compared with what Actions decided, and that
# comparison is what RECOVERY.md promises the operator. Three open pull requests
# at a repository width of three is full -- and is `ok` at the designed two, so
# the number in the line is doing work.
reset_state
printf '[%s,%s,%s]\n' "$(pr 1)" "$(pr 2)" "$(pr 3)" > "$work/state/prs"
set +e
held_err="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_REPOSITORY=o/r \
  ATTADIPA_STUB_WIDTH=3 bash "$START" start o/r 7 local-held 2>&1 >/dev/null)"
set -e
case "$held_err" in
  *'held: full (width 3)'*) ok 'the refusal names the width it refused under' ;;
  *) bad 'the refusal names the width it refused under' \
         "stderr was: $(printf '%s' "$held_err" | tr '\n' ' ')" ;;
esac

step_script() {
  awk -v wanted="$2" '
    $0 == "      - name: " wanted { seen=1; next }
    seen && /^        run: \|/ { body=1; next }
    body && /^      - / { exit }
    body { sub(/^          /, ""); print }
  ' "$1"
}

echo 'Workflow wiring executes the guards'
step_script "$AGENT" 'Check writer admission' > "$work/admission-step.sh"
reset_state; : > "$work/output"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_OUTPUT="$work/output" REPO=o/r ISSUE=7 \
  bash "$work/admission-step.sh" >/dev/null 2>&1
grep -q '^allow=true$' "$work/output" && ok 'the shipping admission step calls the guard' || bad 'the shipping admission step calls the guard' 'no allow output'

sed 's#bash ".github/scripts/writer-admission.sh"#true :#' "$AGENT" > "$work/no-admission.yml"
step_script "$work/no-admission.yml" 'Check writer admission' > "$work/no-admission.sh"
reset_state; : > "$work/output"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_OUTPUT="$work/output" REPO=o/r ISSUE=7 \
  bash "$work/no-admission.sh" >/dev/null 2>&1
if grep -q '^allow=' "$work/output"; then bad 'removing admission makes the mutation red' 'mutant still admitted'; else ok 'removing admission makes the mutation red'; fi

grep -q 'group: attadipa-agent-writer' "$REPAIR" && ok 'CI repair shares the one-writer concurrency group' || bad 'CI repair shares the one-writer concurrency group' missing
grep -q 'bash .*claim.sh.*acquire' "$AGENT" && grep -q 'bash .*claim.sh.*release' "$AGENT" \
  && ok 'the task writer acquires and releases the repository claim' || bad 'the task writer acquires and releases the repository claim' missing
grep -q 'bash /tmp/writer-admission.sh' "$AGENT" && grep -q 'claim.sh acquire "$REPO" writer' "$AGENT" \
  && ok 'the real writer rechecks admission while holding the global lease' || bad 'the real writer rechecks admission while holding the global lease' missing
grep -q 'claim.sh.*acquire.*writer' "$START" && grep -q 'writer-admission.sh' "$START" \
  && ok 'local writers use the same lease and admission path' || bad 'local writers use the same lease and admission path' missing
grep -q 'wip-limit.sh --admit' "$WATCHDOG" && ok 'the watchdog checks admission before dispatch' || bad 'the watchdog checks admission before dispatch' missing

mutant="$work/check-then-set.sh"
sed -e 's/attadipa_claim_create_ref "$repo" "$number" "$tag_sha"/true/' \
    -e 's/winner="$(attadipa_claim_owner "$repo" "$number")"/winner="$holder"/' "$CLAIM" > "$mutant"
reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$mutant" acquire o/r 7 mutant-a >/dev/null 2>&1 & ma=$!
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$mutant" acquire o/r 7 mutant-b >/dev/null 2>&1 & mb=$!
wait "$ma"; rma=$?; wait "$mb"; rmb=$?
if [ "$rma" -eq 0 ] && [ "$rmb" -eq 0 ]; then ok 'removing the atomic create makes the race mutation red'; else bad 'removing the atomic create makes the race mutation red' "mutation did not recreate race ($rma,$rmb)"; fi

# The three fixes, each deleted on its own, must turn one of the cases above red.
echo 'Deleting each #254 fix makes its case red'

swallow="$work/swallow-delete.sh"
sed 's#err="$(gh api "repos/$1/git/ref/tags/attadipa-claims/$2" 2>&1 >/dev/null)"#err="gh: Not Found (HTTP 404)"#' \
  "$CLAIM" > "$swallow"
if grep -q 'err="$(gh api' "$swallow"; then
  bad 'a delete_ref that assumes success makes the mutation red' \
      'the mutation did not replace the confirmation -- the line moved, repoint this sed'
else
  reset_state; claimed
  set +e
  mutant_out="$(PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_DELETE_REFUSED=1 \
    bash "$swallow" break o/r 7 2>/dev/null)"
  set -e
  [ "$mutant_out" = 'claim cleared' ] && [ -d "$work/state/ref.lock" ] \
    && ok 'a delete_ref that assumes success makes the mutation red' \
    || bad 'a delete_ref that assumes success makes the mutation red' \
          "expected the mutant to report a claim it did not clear, got: $mutant_out"
fi

byage="$work/reap-by-age.sh"
sed 's/if ! claim_finished "$repo" "$message"; then/if false; then/' "$CLAIM" > "$byage"
if grep -q 'if ! claim_finished' "$byage"; then
  bad 'a reap that trusts the clock makes the mutation red' \
      'the mutation did not remove the completion check -- repoint this sed'
else
  reset_state; claimed "$CLAIM" agent-i254-r1
  set +e
  PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$byage" reap o/r 7 7200 >/dev/null 2>&1
  set -e
  [ ! -d "$work/state/ref.lock" ] && ok 'a reap that trusts the clock makes the mutation red' \
    || bad 'a reap that trusts the clock makes the mutation red' 'the mutant left the live local lease alone'
fi

keeper="$work/keep-orphan.sh"
sed 's/    discard_own_ref "$repo" "$number" "$tag_sha"/    :/' "$CLAIM" > "$keeper"
if grep -q '^    discard_own_ref' "$keeper"; then
  bad 'an acquire that keeps its failed ref makes the mutation red' \
      'the mutation did not remove the cleanup -- repoint this sed'
else
  reset_state
  set +e
  PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" ATTADIPA_STUB_MODE=issuefail \
    bash "$keeper" acquire o/r 7 agent-4242-1 >/dev/null 2>&1
  set -e
  [ -d "$work/state/ref.lock" ] && ok 'an acquire that keeps its failed ref makes the mutation red' \
    || bad 'an acquire that keeps its failed ref makes the mutation red' 'the mutant left no orphan'
fi

# Deleting the #369 fix has two halves, and each one on its own has to turn the
# collision case above red: reading provenance out of the holder id again, and
# writing `hosted` somewhere other than the workflow path.
echo 'Deleting each #369 fix makes its case red'

regexkind="$work/kind-from-holder.sh"
sed 's@^claim_field() .*@claim_field() { local h; h=$(head -1 <<<"$1"); case "$2" in kind) if grep -Eq "^agent-[0-9]+-[0-9]+$" <<<"$h"; then echo hosted; else echo local; fi ;; run) h=${h#agent-}; echo "${h%-*}" ;; esac; }@' \
  "$CLAIM" > "$regexkind"
if ! grep -q 'kind) if grep -Eq' "$regexkind"; then
  bad 'reading provenance from the holder id again makes the mutation red' \
      'the mutation did not replace claim_field -- the line moved, repoint this sed'
else
  reset_state; printf 'completed\n' > "$work/state/run-status"
  claim_as local 7 agent-4242-1 "$regexkind"
  reap_rc 7 "$regexkind"
  [ ! -d "$work/state/ref.lock" ] \
    && ok 'reading provenance from the holder id again makes the mutation red' \
    || bad 'reading provenance from the holder id again makes the mutation red' \
          'the mutant kept the live local lease, so the collision case proves nothing'
fi

alwayshosted="$work/always-hosted.sh"
sed 's@^  if \[ "${GITHUB_ACTIONS-}".*@  if GITHUB_RUN_ID="${GITHUB_RUN_ID:-4242}"; then@' \
  "$CLAIM" > "$alwayshosted"
if grep -q 'GITHUB_ACTIONS' "$alwayshosted" || ! grep -q 'GITHUB_RUN_ID:-4242' "$alwayshosted"; then
  bad 'writing hosted provenance off the workflow path makes the mutation red' \
      'the mutation did not remove the workflow-path test -- repoint this sed'
else
  reset_state; printf 'completed\n' > "$work/state/run-status"
  claim_as local 7 agent-4242-1 "$alwayshosted"
  reap_rc 7
  [ ! -d "$work/state/ref.lock" ] \
    && ok 'writing hosted provenance off the workflow path makes the mutation red' \
    || bad 'writing hosted provenance off the workflow path makes the mutation red' \
          'the mutant marked a terminal claim local anyway'
fi

# A claim is a git ref and a run status is Actions data. The recovery jobs asked
# for neither, so every DELETE they made was refused -- and reported cleared.
job_permissions() {
  awk -v job="  $2:" '
    $0 == job { seen = 1; next }
    seen && /^  [a-z0-9_-]+:$/ { exit }
    seen && /^    permissions:$/ { perms = 1; next }
    perms && /^    [a-z]/ { exit }
    perms { print }
  ' "$1"
}
case "$(job_permissions "$WATCHDOG" stuck)" in
  *'contents: write'*) ok 'the watchdog job that reaps claims may write refs' ;;
  *) bad 'the watchdog job that reaps claims may write refs' 'contents is still read-only' ;;
esac
case "$(job_permissions "$WATCHDOG" stuck)" in
  *'actions: read'*) ok 'the watchdog job that reaps claims may read the run behind a holder' ;;
  *) bad 'the watchdog job that reaps claims may read the run behind a holder' 'no actions permission' ;;
esac
case "$(job_permissions "$REPAIR" reset)" in
  *'contents: write'*) ok 'the CI repair reset may actually break the claim it reports' ;;
  *) bad 'the CI repair reset may actually break the claim it reports' 'contents is still read-only' ;;
esac

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
