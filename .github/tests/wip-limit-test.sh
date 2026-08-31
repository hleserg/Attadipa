#!/usr/bin/env bash
# The pull-request queue has a bounded width -- and the thing that counts it can
# actually run.
#
# THIS FILE USED TO BE HALF A TEST, and the missing half is the whole reason
# #239 exists. It called `attadipa_wip_decide` with hand-built JSON, six cases,
# 6/6 green, on every run from #216 onwards. Meanwhile the live script asked
# `gh pr list` for `baseRepository`, a field `gh` does not have; `gh` exited 1
# before making a request; `2>/dev/null || true` swallowed that; and the
# workflow reported `Could not determine the active pull-request count` on #219,
# #236 and #237. The pure rule was correct the entire time and never reached.
#
# So there are two sections. The first is the rule, unchanged. The second runs
# THE SHIPPING SCRIPT, end to end, against a stub `gh` on PATH that refuses an
# unknown `--json` field exactly as `gh` does -- and the last case in it puts the
# old field list back and requires the suite to go red, because a guard that
# cannot fail guards nothing.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

# shellcheck source-path=SCRIPTDIR
# shellcheck source=../scripts/wip-limit.sh
. .github/scripts/wip-limit.sh

# THE AMBIENT ENVIRONMENT DOES NOT GET TO DECIDE WHAT THIS SUITE PROVES. Every
# case below states the width it runs under; a developer or a runner that
# happens to export ATTADIPA_WIP_LIMIT must not be able to turn the original
# twelve green on a queue width they were never written for.
unset ATTADIPA_WIP_LIMIT

pass=0; fail=0
ok()  { pass=$((pass + 1)); printf 'ok %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; }

echo "The rule"

check() { local got; got="$(attadipa_wip_decide "$2")"; if [ "$got" = "$3" ]; then ok "$1"; else bad "$1: $got"; fi; }
pr() { printf '{"head":{"repo":{"full_name":"hleserg/Attadipa"}},"base":{"repo":{"full_name":"hleserg/Attadipa"}},"labels":%s}' "$1"; }
foreign() { printf '{"head":{"repo":{"full_name":"fork/x"}},"base":{"repo":{"full_name":"hleserg/Attadipa"}},"labels":[]}' ; }
check 'zero active PRs leaves capacity' '[]' 'ok 0'
check 'one active PR leaves one slot' "[$(pr '[]')]" 'ok 1'
check 'two active PRs fill the normal queue' "[$(pr '[]'),$(pr '[]')]" 'full 2'
check 'three active PRs are an incident' "[$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'incident 3'
check 'four active PRs remain an incident' "[$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'incident 4'
check 'seven active PRs remain an incident' "[$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'incident 7'
check 'parked work does not consume a slot' "[$(pr '[{"name":"queue:parked"}]'),$(pr '[]')]" 'ok 1'
check 'emergency recovery does not consume a normal slot' "[$(pr '[{"name":"queue:emergency"}]'),$(pr '[]')]" 'ok 1'
check 'fork PRs do not consume repository capacity' "[$(foreign),$(pr '[]')]" 'ok 1'
check 'an unreadable response is not a healthy queue' '{"message":"no"}' 'unknown unknown'
check 'malformed JSON is not a healthy queue' '[' 'unknown unknown'

echo
echo "The width"

# Each case runs the reader in a `$(...)` subshell, so a value set for one case
# cannot survive into the next -- a bare assignment prefix on a function call
# does survive in some shells, and that is exactly how a suite starts proving
# the previous case twice.
# SC2030/SC2031 say the assignment is local to the `$(...)` subshell and might
# be lost. That is not an accident here, it is the containment: the value must
# not outlive the one case that set it.
# shellcheck disable=SC2030,SC2031
width() {  # $1 = description, $2 = value, or the word `unset`, $3 = expected limit
  local got
  if [ "$2" = unset ]; then
    got="$(unset ATTADIPA_WIP_LIMIT; attadipa_wip_limit)"
  else
    got="$(export ATTADIPA_WIP_LIMIT="$2"; attadipa_wip_limit)"
  fi
  if [ "$got" = "$3" ]; then ok "$1"; else bad "$1: got '$got', wanted '$3'"; fi
}
# shellcheck disable=SC2030,SC2031
check_at() {  # $1 = description, $2 = limit, $3 = payload, $4 = expected decision
  local got; got="$(export ATTADIPA_WIP_LIMIT="$2"; attadipa_wip_decide "$3")"
  if [ "$got" = "$4" ]; then ok "$1"; else bad "$1: got '$got', wanted '$4'"; fi
}

width 'the designed width survives: an unset variable is still two' unset '2'
width 'an empty variable is two, not nothing' '' '2'
width 'the owner can lift it' '4' '4'
width 'a leading zero is a decimal, not an octal literal' '08' '8'
width 'zero is honoured -- an explicit freeze is closed, not open' '0' '0'
# The four that matter. Everything a variable can be set to by accident, by a
# typo, or on purpose must land on the DESIGNED width and never on a wider one.
width 'a word does not unbound the queue' 'unlimited' '2'
width 'a negative number does not unbound the queue' '-1' '2'
width 'whitespace does not unbound the queue' ' 4' '2'
width 'a number too long to be a queue width falls back rather than overflows' '99999999999999999999' '2'

check_at 'a lifted limit admits the count that used to close the queue' 4 \
  "[$(pr '[]'),$(pr '[]')]" 'ok 2'
check_at 'a lifted limit still closes at its own width' 4 \
  "[$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'full 4'
check_at 'and still reports an incident above it' 4 \
  "[$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'incident 5'
check_at 'a frozen queue admits nothing at all' 0 '[]' 'full 0'
check_at 'junk in the variable leaves the queue at its designed width' unlimited \
  "[$(pr '[]'),$(pr '[]')]" 'full 2'
check_at 'an unreadable queue is unknown whatever the width' 4 '{"message":"no"}' 'unknown unknown'

echo
echo "The transport, executed"

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/bin"

# THE FIELDS `gh pr list --json` ACTUALLY ACCEPTS, recorded from `gh pr list
# --json` with no value -- which is a flag-parse error `gh` answers with the
# list, before any network call. Captured from gh 2.97.0 on 2026-08-25. It is
# cross-checked against the installed `gh` below, so this list drifting from
# reality is a thing the suite says out loud rather than a thing it assumes.
kPrListFields='additions,assignees,author,autoMergeRequest,baseRefName,baseRefOid,body,changedFiles,closed,closedAt,closingIssuesReferences,comments,commits,createdAt,deletions,files,fullDatabaseId,headRefName,headRefOid,headRepository,headRepositoryOwner,id,isCrossRepository,isDraft,labels,latestReviews,maintainerCanModify,mergeCommit,mergeStateStatus,mergeable,mergedAt,mergedBy,milestone,number,potentialMergeCommit,projectCards,projectItems,reactionGroups,reviewDecision,reviewRequests,reviews,state,statusCheckRollup,title,updatedAt,url'

# A stub `gh` that behaves like the real one where it matters: it records what it
# was asked for, and it REFUSES AN UNKNOWN `--json` FIELD with gh's own wording
# and exit status. Everything the caller does with the failure -- the hard
# diagnostic, the fail-closed unknown -- hangs off that refusal being real.
cat > "$work/bin/gh" <<STUB
#!/usr/bin/env bash
set -uo pipefail
dir="\${ATTADIPA_STUB_DIR:?stub directory not set}"
printf '%s\n' "\$*" > "\$dir/argv"
[ "\${1-}" = pr ] && [ "\${2-}" = list ] || { echo "stub gh: unexpected command: \$*" >&2; exit 64; }
fields=""
while [ \$# -gt 0 ]; do
  case "\$1" in --json) fields="\${2-}"; shift ;; esac
  shift
done
printf '%s\n' "\$fields" > "\$dir/fields"
if [ "\${ATTADIPA_STUB_MODE:-ok}" = apifail ]; then
  echo "error connecting to api.github.com: dial tcp: lookup api.github.com: no such host" >&2
  exit 1
fi
if [ -z "\$fields" ]; then
  echo "Specify one or more comma-separated fields for \\\`--json\\\`:" >&2
  exit 1
fi
IFS=, read -r -a asked <<< "\$fields"
for f in "\${asked[@]}"; do
  case ",$kPrListFields," in
    *",\$f,"*) ;;
    *)
      # gh's REAL refusal, which is about fifty lines: the offending name and
      # then every field it does have. A one-line stub would hide the fact that
      # an ::error:: annotation ends at the first newline, so the caller's
      # diagnostic has to survive this shape and not the tidy one.
      printf 'Unknown JSON field: "%s"\n' "\$f" >&2
      printf 'Available fields:\n' >&2
      printf '%s\n' "$kPrListFields" | tr , '\n' | sed 's/^/  /' >&2
      exit 1 ;;
  esac
done
cat "\$dir/payload"
STUB
chmod +x "$work/bin/gh"

# gh-shaped fixtures -- `isCrossRepository`, not the REST `base`/`head` pair the
# rule reads. Translating between the two is the seam that broke, so the caller
# test must speak the CLI's vocabulary or it tests nothing new.
ghpr()   { printf '{"number":%s,"isCrossRepository":false,"labels":%s}' "$1" "${2-[]}"; }
ghfork() { printf '{"number":%s,"isCrossRepository":true,"labels":[]}' "$1"; }

# Runs the shipping script -- or a named copy of it -- with the stub on PATH, and
# prints `state count` from the GITHUB_OUTPUT file it wrote, plus everything it
# said, so a caller can assert on the diagnostics too.
run_caller() {  # $1 = script, $2 = payload JSON, $3 = stub mode
  local script=$1 payload=$2 mode=${3:-ok}
  printf '%s' "$payload" > "$work/payload"
  : > "$work/output"
  # WIP_LIMIT, not ATTADIPA_WIP_LIMIT: the variable reaches the script only
  # through this line, so the suite states the width for every executed case
  # instead of inheriting whatever the runner happens to export.
  PATH="$work/bin:$PATH" ATTADIPA_STUB_DIR="$work" ATTADIPA_STUB_MODE="$mode" \
    GITHUB_OUTPUT="$work/output" GITHUB_REPOSITORY=hleserg/Attadipa \
    ATTADIPA_WIP_LIMIT="${WIP_LIMIT-}" \
    bash "$script" > "$work/log" 2>&1
  printf '%s %s\n' \
    "$(sed -n 's/^state=//p' "$work/output")" \
    "$(sed -n 's/^count=//p' "$work/output")"
}

caller() {  # $1 = description, $2 = payload, $3 = expected "state count"
  local got; got=$(run_caller .github/scripts/wip-limit.sh "$2")
  if [ "$got" = "$3" ]; then ok "$1"; else bad "$1: got '$got'
$(sed 's/^/       /' "$work/log")"; fi
}

# 1. The exact field list, asserted as a list. A substring match would pass on a
#    line that also asked for something unsupported.
printf '%s' "[$(ghpr 1)]" > "$work/payload"
: > "$work/output"
if PATH="$work/bin:$PATH" ATTADIPA_STUB_DIR="$work" ATTADIPA_STUB_MODE=ok \
    GITHUB_OUTPUT="$work/output" GITHUB_REPOSITORY=hleserg/Attadipa \
    bash .github/scripts/wip-limit.sh > "$work/log" 2>&1; then
  ok 'the production workflow invocation exits zero after writing its outputs'
else
  bad 'the production workflow invocation exits non-zero and skips its reporting steps'
fi
run_caller .github/scripts/wip-limit.sh "[$(ghpr 1)]" > /dev/null
asked=$(cat "$work/fields")
if [ "$asked" = "$ATTADIPA_WIP_JSON_FIELDS" ]; then
  ok "the script asks gh for exactly the fields it declares ($asked)"
else
  bad "the script declares '$ATTADIPA_WIP_JSON_FIELDS' and asked for '$asked'"
fi
unsupported=""
IFS=, read -r -a declared <<< "$ATTADIPA_WIP_JSON_FIELDS"
for f in "${declared[@]}"; do
  case ",$kPrListFields," in *",$f,"*) ;; *) unsupported="$unsupported $f" ;; esac
done
if [ -z "$unsupported" ]; then
  ok "every field it asks for is one gh pr list has"
else
  bad "gh pr list has no such field:$unsupported -- it exits 1 before making a request"
fi

# 1b. And whether the recorded list still matches the `gh` on this machine. A
#     WARNING and not a failure in either direction: a newer `gh` that adds a
#     field must not red the queue, and a `gh` that is missing one is this
#     machine's business rather than the repository's. Silence here would be the
#     swallow this file exists to refuse, so it is said out loud.
if command -v gh > /dev/null 2>&1; then
  live=$(gh pr list --json 2>&1 | sed -n 's/^  \([a-zA-Z]*\)$/\1/p' | paste -sd, -)
  if [ -z "$live" ]; then
    printf 'note   the installed gh did not print a field list; the recorded one was not cross-checked\n'
  elif [ "$live" = "$kPrListFields" ]; then
    ok "the recorded field list matches the installed gh ($(gh --version | head -1))"
  else
    printf 'note   the recorded field list differs from the installed gh; recorded stays authoritative here\n'
    printf 'note   only in gh: %s\n' "$(comm -13 <(tr , '\n' <<< "$kPrListFields" | sort) <(tr , '\n' <<< "$live" | sort) | paste -sd' ' -)"
    printf 'note   only recorded: %s\n' "$(comm -23 <(tr , '\n' <<< "$kPrListFields" | sort) <(tr , '\n' <<< "$live" | sort) | paste -sd' ' -)"
  fi
else
  printf 'note   no gh on PATH; the recorded field list was not cross-checked\n'
fi

# 2-6. The states, reached through the transport rather than around it.
caller 'one same-repository PR leaves one slot' "[$(ghpr 1)]" 'ok 1'
caller 'two same-repository PRs close admission' \
  "[$(ghpr 1),$(ghpr 2)]" 'full 2'
caller 'three reach incident' \
  "[$(ghpr 1),$(ghpr 2),$(ghpr 3)]" 'incident 3'
caller 'four remain incident' \
  "[$(ghpr 1),$(ghpr 2),$(ghpr 3),$(ghpr 4)]" 'incident 4'
caller 'seven remain incident' \
  "[$(ghpr 1),$(ghpr 2),$(ghpr 3),$(ghpr 4),$(ghpr 5),$(ghpr 6),$(ghpr 7)]" 'incident 7'
caller 'a fork PR does not consume repository capacity' \
  "[$(ghfork 9),$(ghpr 1)]" 'ok 1'
caller 'parked work does not consume a slot' \
  "[$(ghpr 1 '[{"name":"queue:parked"}]'),$(ghpr 2)]" 'ok 1'
caller 'emergency work does not consume a slot' \
  "[$(ghpr 1 '[{"name":"queue:emergency"}]'),$(ghpr 2)]" 'ok 1'
caller 'an empty queue is ok 0, not unknown' '[]' 'ok 0'
caller 'malformed JSON is unknown, not capacity' '[' 'unknown unknown'

# 6b. The width, through the transport rather than around it. The rule already
#     honours a lifted limit; this is the half that broke last time -- a
#     correct rule the shipping script never reaches.
got=$(WIP_LIMIT=4 run_caller .github/scripts/wip-limit.sh "[$(ghpr 1),$(ghpr 2)]")
if [ "$got" = "ok 2" ]; then
  ok "the shipping script honours a lifted limit end to end"
else
  bad "with the limit lifted to 4 the script still returned '$got'
$(sed 's/^/       /' "$work/log")"
fi
got=$(WIP_LIMIT=unlimited run_caller .github/scripts/wip-limit.sh "[$(ghpr 1),$(ghpr 2)]")
if [ "$got" = "full 2" ]; then
  ok "and junk in the variable closes the queue at two, end to end"
else
  bad "junk in the variable returned '$got' through the shipping script"
fi

# 6c. The operator-facing sentence names the limit IN FORCE. Reading
#     "normal limit: 2" under a lifted limit sends an operator looking for a bug
#     in the count, which is the wrong place and the wrong problem.
say=$(env -u ATTADIPA_WIP_LIMIT bash .github/scripts/wip-limit.sh --say full 2)
case "$say" in
  *"normal limit: 2"*) ok "--say names the designed limit when none is set" ;;
  *) bad "--say full said: $say" ;;
esac
say=$(env ATTADIPA_WIP_LIMIT=4 bash .github/scripts/wip-limit.sh --say full 4)
case "$say" in
  *"normal limit: 4"*) ok "--say names the lifted limit rather than the literal two" ;;
  *) bad "--say full under a lifted limit said: $say" ;;
esac
say=$(env ATTADIPA_WIP_LIMIT=4 bash .github/scripts/wip-limit.sh --say incident 5)
case "$say" in
  *"threshold of 5"*) ok "--say moves the hard threshold with the limit" ;;
  *) bad "--say incident under a lifted limit said: $say" ;;
esac

# The operator-facing line is the only way a live run can be confirmed to have
# counted anything real, so it is asserted rather than assumed.
run_caller .github/scripts/wip-limit.sh "[$(ghpr 12),$(ghpr 34)]" > /dev/null
if grep -q '#12 #34' "$work/log"; then
  ok "the log names the pull requests it counted"
else
  bad "the log does not name what it counted: $(head -1 "$work/log")"
fi

# 7. A transient API failure fails closed, loudly.
got=$(run_caller .github/scripts/wip-limit.sh "[$(ghpr 1)]" apifail)
if [ "$got" = "unknown unknown" ]; then
  ok "an API failure fails closed to unknown"
else
  bad "an API failure came back as '$got' -- an unreadable queue must never read as capacity"
fi
if grep -q '::warning' "$work/log" && grep -q 'no such host' "$work/log"; then
  ok "and says what gh actually said, instead of discarding it"
else
  bad "the gh error was swallowed: $(head -3 "$work/log" | tr '\n' ' ')"
fi
if grep -q '::error' "$work/log"; then
  bad "a transient API failure raised the hard diagnostic reserved for a defect in the script"
else
  ok "a transient API failure is not reported as a defect in the script"
fi

# 8. THE MUTATION, and the reason the rest of this section exists. The pre-#239
#    field list is put back into a copy of the shipping script and the caller is
#    run again. It must come out unknown AND carry the hard diagnostic -- if it
#    comes out `ok 2`, the stub is not refusing what `gh` refuses and this whole
#    section is decoration.
kBrokenFields='number,headRefName,headRepository,baseRepository,labels'
sed "s/^ATTADIPA_WIP_JSON_FIELDS='.*'$/ATTADIPA_WIP_JSON_FIELDS='$kBrokenFields'/" \
  .github/scripts/wip-limit.sh > "$work/mutated.sh"
if grep -q "^ATTADIPA_WIP_JSON_FIELDS='$kBrokenFields'$" "$work/mutated.sh"; then
  ok "the mutation applies -- the field list is still a line a test can reach"
else
  bad "the mutation did not apply; this case proves nothing until it does"
fi
got=$(run_caller "$work/mutated.sh" "[$(ghpr 1),$(ghpr 2)]")
if [ "$got" = "unknown unknown" ]; then
  ok "putting baseRepository back makes the caller test red, which is the point"
else
  bad "with baseRepository back the caller returned '$got' -- the stub is not refusing what gh refuses"
fi
if grep -q '::error' "$work/log" && grep -q 'baseRepository' "$work/log"; then
  ok "and the unsupported field is named in a hard diagnostic, not folded into a shrug"
else
  bad "no hard diagnostic naming the field: $(head -3 "$work/log" | tr '\n' ' ')"
fi
# THE WHOLE ANNOTATION IS ON THE `::error::` LINE. An `::error::` ends at the
# first newline, and gh's refusal here is fifty of them -- so a diagnostic built
# from the entire stderr arrives cut off after `Unknown JSON field: "x"`, with
# the rest of the sentence spilled into the log forty-nine field names later.
# Asserting the END of the message is what binds that: the truncation takes the
# tail away and leaves the head, so a check on the head passes either way.
annotation=$(grep '::error' "$work/log" | head -1)
case "$annotation" in
  *baseRepository*Fields\ requested:*)
    ok "the annotation carries its whole message on one line, ending where it was written to" ;;
  *)
    bad "the ::error:: line is cut short -- gh's newlines end the annotation: $annotation" ;;
esac
# And the other half: shortening the annotation must not be how the fifty lines
# get lost. They belong in the log.
if [ "$(grep -c 'Available fields:' "$work/log")" = 1 ]; then
  ok "and gh's full refusal is in the log, where it belongs, rather than discarded"
else
  bad "gh's list of the fields it does have reached the log $(grep -c 'Available fields:' "$work/log") times, not once"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
