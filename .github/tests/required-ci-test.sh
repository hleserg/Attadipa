#!/usr/bin/env bash
# Required CI is the stable merge gate.  This test executes its real shell
# body and mutation-checks the workflow wiring that feeds it.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

CI=.github/workflows/ci.yml
pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n' "$1"; fail=$((fail + 1)); }

required_jobs='host-build:HOST_BUILD_RESULT
strict-warnings:STRICT_WARNINGS_RESULT
clang:CLANG_RESULT
sanitizers:SANITIZERS_RESULT
workflows:WORKFLOWS_RESULT
docs:DOCS_RESULT
simulator:SIMULATOR_RESULT
firmware-build:FIRMWARE_BUILD_RESULT'

job_block() {
  awk '
    /^  required-ci:/ { found = 1 }
    found && seen && /^  [[:alnum:]_-]+:/ { exit }
    found { print; seen = 1 }
    END { if (!found) exit 1 }
  ' "$1"
}

extract_run_block() {
  awk '
    /- name: Verify required jobs/ { instep = 1; next }
    instep && /^[[:space:]]*run: \|[[:space:]]*$/ {
      match($0, /^[[:space:]]*/); indent = RLENGTH; inrun = 1; next
    }
    inrun {
      if (/^[[:space:]]*$/) { print ""; next }
      match($0, /^[[:space:]]*/)
      if (RLENGTH <= indent) exit
      print substr($0, indent + 3)
    }
  '
}

wiring_ok() {
  block=$1
  printf '%s\n' "$block" | grep -Fqx '    name: Required CI' || return 1
  printf '%s\n' "$block" | grep -Fqx '    if: always()' || return 1

  expected=$(printf '%s\n' "$required_jobs" | cut -d: -f1 | sort)
  actual=$(printf '%s\n' "$block" | awk '
    /^    needs:$/ { in_needs = 1; next }
    in_needs && /^      - / { sub(/^      - /, ""); print; next }
    in_needs { exit }
  ' | sort)
  [ "$actual" = "$expected" ] || return 1

  while IFS=: read -r job variable; do
    printf '%s\n' "$block" |
      grep -Fq "$variable: \${{ needs.$job.result }}" || return 1
  done <<EOF
$required_jobs
EOF

  printf '%s\n' "$block" | grep -Fq 'run: |' || return 1
}

execute_gate() {
  script=$1
  host_result=$2
  HOST_BUILD_RESULT="$host_result" \
  STRICT_WARNINGS_RESULT=success \
  CLANG_RESULT=success \
  SANITIZERS_RESULT=success \
  WORKFLOWS_RESULT=success \
  DOCS_RESULT=success \
  SIMULATOR_RESULT=success \
  FIRMWARE_BUILD_RESULT=success \
    bash -c "$script" >/dev/null 2>&1
}

behavior_ok() {
  script=$1
  execute_gate "$script" success || return 1
  for bad in failure cancelled skipped; do
    if execute_gate "$script" "$bad"; then return 1; fi
  done
}

echo 'Required CI workflow contract'

if block=$(job_block "$CI"); then
  ok 'the Required CI job exists'
else
  no 'the Required CI job exists'
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi

if wiring_ok "$block"; then
  ok 'the gate has exactly the mandatory needs and result inputs'
else
  no 'the gate has exactly the mandatory needs and result inputs'
fi

mutated_block=$(printf '%s\n' "$block" | grep -Fv '      - firmware-build')
if wiring_ok "$mutated_block"; then
  no 'removing one mandatory need is rejected'
else
  ok 'removing one mandatory need is rejected'
fi

run_block=$(printf '%s\n' "$block" | extract_run_block)
if [ -n "$run_block" ] && behavior_ok "$run_block"; then
  ok 'success passes; failure, cancellation and skip fail closed'
else
  no 'success passes; failure, cancellation and skip fail closed'
fi

mutated_run=$(printf '%s\n' "$run_block" | sed 's/exit "$failed"/exit 0/')
if [ "$mutated_run" = "$run_block" ]; then
  no 'the unconditional-success mutation changed the production seam'
elif behavior_ok "$mutated_run"; then
  no 'an unconditional-success gate is rejected'
else
  ok 'an unconditional-success gate is rejected'
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
