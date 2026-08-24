#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
pass=0; fail=0
check() { local got; got="$(. .github/scripts/wip-limit.sh; attadipa_wip_decide "$2")"; if [ "$got" = "$3" ]; then echo "ok $1"; pass=$((pass+1)); else echo "FAIL $1: $got"; fail=$((fail+1)); fi; }
pr() { printf '{"head":{"repo":{"full_name":"hleserg/Attadipa"}},"base":{"repo":{"full_name":"hleserg/Attadipa"}},"labels":%s}' "$1"; }
foreign() { printf '{"head":{"repo":{"full_name":"fork/x"}},"base":{"repo":{"full_name":"hleserg/Attadipa"}},"labels":[]}' ; }
check 'two active PRs are normal' "[$(pr '[]'),$(pr '[]')]" 'ok 2'
check 'three is the hard temporary maximum' "[$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'full 3'
check 'four is an incident' "[$(pr '[]'),$(pr '[]'),$(pr '[]'),$(pr '[]')]" 'incident 4'
check 'parked work does not consume a slot' "[$(pr '[{"name":"queue:parked"}]'),$(pr '[]'),$(pr '[]')]" 'ok 2'
check 'fork PRs do not consume repository capacity' "[$(foreign),$(pr '[]')]" 'ok 1'
check 'an unreadable response is not a healthy queue' '{"message":"no"}' 'unknown unknown'
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
