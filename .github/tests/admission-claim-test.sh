#!/usr/bin/env bash
# The production writer gate and the repository-backed exclusive claim.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0; fail=0
ok() { pass=$((pass + 1)); printf 'ok %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s: %s\n' "$1" "$2"; }

ADMISSION=.github/scripts/writer-admission.sh
CLAIM=.github/scripts/claim.sh
AGENT=.github/workflows/claude-agent.yml
REPAIR=.github/workflows/claude-ci-repair.yml
WATCHDOG=.github/workflows/agent-queue-watchdog.yml

for required in "$ADMISSION" "$CLAIM"; do
  if [ -f "$required" ]; then ok "$required exists"; else bad "$required exists" missing; fi
done
if [ ! -f "$ADMISSION" ] || [ ! -f "$CLAIM" ]; then
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
  POST:repos/o/r/git/tags)
    token=$(field message); date=$(field 'tagger[date]')
    sha=$(printf '%s' "$token" | sha1sum | cut -c1-40)
    jq -nc --arg sha "$sha" --arg message "$token" --arg date "$date" \
      '{sha:$sha,message:$message,tagger:{date:$date}}' > "$state/tag.$sha"
    printf '{"sha":"%s"}\n' "$sha" ;;
  POST:repos/o/r/git/refs)
    if [ "${ATTADIPA_STUB_BARRIER:-0}" = 1 ]; then
      : > "$state/ready.$$"
      for _ in $(seq 1 200); do
        [ "$(find "$state" -name 'ready.*' | wc -l)" -ge 2 ] && break
        sleep 0.01
      done
    fi
    if mkdir "$state/ref.lock" 2>/dev/null; then
      field sha > "$state/ref.sha"
      printf '{"ref":"refs/tags/attadipa-claims/7"}\n'
    else
      echo 'Reference already exists' >&2; exit 1
    fi ;;
  GET:repos/o/r/git/ref/tags/attadipa-claims/7)
    [ -d "$state/ref.lock" ] || exit 1
    printf '{"object":{"sha":"%s"}}\n' "$(cat "$state/ref.sha")" ;;
  GET:repos/o/r/git/tags/*)
    cat "$state/tag.${path##*/}" ;;
  DELETE:repos/o/r/git/refs/tags/attadipa-claims/7)
    rm -rf "$state/ref.lock"; rm -f "$state/ref.sha" ;;
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

reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" acquire o/r 7 crashed >/dev/null
tag=$(cat "$work/state/ref.sha"); jq '.tagger.date="2000-01-01T00:00:00Z"' "$work/state/tag.$tag" > "$work/state/old"; mv "$work/state/old" "$work/state/tag.$tag"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$CLAIM" reap o/r 7 7200 >/dev/null
[ ! -d "$work/state/ref.lock" ] && ok 'a crashed writer is reaped after the stale bound' || bad 'a crashed writer is reaped after the stale bound' 'lock remains'

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

sed 's#bash ".github/scripts/writer-admission.sh"#true # admission removed#' "$AGENT" > "$work/no-admission.yml"
step_script "$work/no-admission.yml" 'Check writer admission' > "$work/no-admission.sh"
reset_state; : > "$work/output"
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" GITHUB_OUTPUT="$work/output" REPO=o/r ISSUE=7 \
  bash "$work/no-admission.sh" >/dev/null 2>&1
if grep -q '^allow=' "$work/output"; then bad 'removing admission makes the mutation red' 'mutant still admitted'; else ok 'removing admission makes the mutation red'; fi

grep -q 'group: attadipa-agent-writer' "$REPAIR" && ok 'CI repair shares the one-writer concurrency group' || bad 'CI repair shares the one-writer concurrency group' missing
grep -q 'bash .*claim.sh.*acquire' "$AGENT" && grep -q 'bash .*claim.sh.*release' "$AGENT" \
  && ok 'the task writer acquires and releases the repository claim' || bad 'the task writer acquires and releases the repository claim' missing
grep -q 'wip-limit.sh --admit' "$WATCHDOG" && ok 'the watchdog checks admission before dispatch' || bad 'the watchdog checks admission before dispatch' missing

mutant="$work/check-then-set.sh"
sed -e 's/attadipa_claim_create_ref "$repo" "$number" "$tag_sha"/true/' \
    -e 's/winner="$(attadipa_claim_owner "$repo" "$number")"/winner="$token"/' "$CLAIM" > "$mutant"
reset_state
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$mutant" acquire o/r 7 mutant-a >/dev/null 2>&1 & ma=$!
PATH="$work/bin:$PATH" ATTADIPA_STUB_STATE="$work/state" bash "$mutant" acquire o/r 7 mutant-b >/dev/null 2>&1 & mb=$!
wait "$ma"; rma=$?; wait "$mb"; rmb=$?
if [ "$rma" -eq 0 ] && [ "$rmb" -eq 0 ]; then ok 'removing the atomic create makes the race mutation red'; else bad 'removing the atomic create makes the race mutation red' "mutation did not recreate race ($rma,$rmb)"; fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
