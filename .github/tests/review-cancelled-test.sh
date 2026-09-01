#!/usr/bin/env bash
# A CANCELLED REVIEW IS NOT A REFUSED CREDENTIAL, AND THE GUARD IS EVALUATED
# RATHER THAN READ.
#
# `always()` is true on cancellation as well as on success and failure, so the
# no-verdict reporting path in `claude-pr-review.yml` used to run for a review
# the concurrency group had killed because a newer head arrived. On #173 at
# `1c13322e` it posted **the independent review did not run** and blamed a
# refused workflow or a rejected credential, beside a real review of the current
# head. The workflow's own argument against that is in its header: "a workflow
# that is red for a reason outside the code is a workflow people learn to
# ignore".
#
# Grepping for the string `!cancelled()` would pass on a condition that had it
# in a comment and `always()` in the expression. So the `if:` expressions are
# extracted from the live workflow and evaluated under three contexts: a run
# cancelled by the concurrency group, a run that genuinely failed, and a run
# that succeeded and published nothing. The distinction the fix rests on -- that
# a `timeout-minutes` kill reports `failure` and not `cancelled` -- is the
# second context, and it is asserted here rather than taken from the issue.
#
# `ci.yml` runs this file.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }
say() { if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "wanted '$3', got '$2'"; fi; }

WF=.github/workflows/claude-pr-review.yml
if [ ! -r "$WF" ]; then
  no "the reviewed workflow exists" "$WF is not readable"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
echo "  reading  $WF"

# The `if:` of one named step, single-line or `>-` block, with comment lines
# dropped -- a comment inside the step is exactly what a grep would be fooled
# by, so it must not reach the evaluator either.
extract_if() {
  awk -v want="$1" '
    index($0, "- name: " want) { instep = 1; next }
    instep && $0 ~ /^[[:space:]]*- name: / { exit }
    instep && $0 ~ /^[[:space:]]*#/ { next }
    instep && $0 ~ /^[[:space:]]*if:/ {
      sub(/^[[:space:]]*if:[[:space:]]*/, "")
      if ($0 == ">-" || $0 == ">" || $0 == "") { inblock = 1; next }
      print; exit
    }
    inblock {
      if ($0 ~ /^[[:space:]]*[a-z-]+:/) exit
      if ($0 ~ /^[[:space:]]*#/) next
      sub(/^[[:space:]]*/, "")
      printf "%s ", $0
    }
    END { if (inblock) print "" }
  ' "$2"
}

# GitHub expression -> python, then evaluated against one context. Only the
# grammar this workflow actually uses: the two status functions, `!`, `&&`,
# `||`, parentheses, `steps.<id>.outcome`, `steps.<id>.outputs.<name>`,
# `vars.<NAME>` and string comparison. Anything else is a syntax error here
# rather than a quiet `False`, which is the point -- a condition this cannot
# read must fail the suite, not pass it.
evaluate() {
  EXPR="$1" CANCELLED="$2" python3 - "$3" <<'PY'
import os, re, sys, json

expr = os.environ["EXPR"]
cancelled = os.environ["CANCELLED"] == "yes"
ctx = json.loads(sys.argv[1])

# `always()` is true even when the job is cancelled; `cancelled()` is the flag.
expr = re.sub(r"\balways\(\)", "True", expr)
expr = re.sub(r"\bcancelled\(\)", "CANCELLED", expr)
expr = re.sub(r"\bsuccess\(\)", "(not CANCELLED)", expr)
expr = re.sub(r"steps\.([A-Za-z0-9_-]+)\.outputs\.([A-Za-z0-9_-]+)",
              lambda m: "S.get(%r, '')" % ("steps.%s.outputs.%s" % (m.group(1), m.group(2))), expr)
expr = re.sub(r"steps\.([A-Za-z0-9_-]+)\.outcome",
              lambda m: "S.get(%r, '')" % ("steps.%s.outcome" % m.group(1)), expr)
expr = re.sub(r"vars\.([A-Za-z0-9_]+)",
              lambda m: "S.get(%r, '')" % ("vars.%s" % m.group(1)), expr)
expr = expr.replace("&&", " and ").replace("||", " or ")
expr = re.sub(r"!(?!=)", " not ", expr)

try:
    value = eval(expr, {"__builtins__": {}}, {"S": ctx, "CANCELLED": cancelled})
except Exception as exc:                                    # noqa: BLE001
    sys.stdout.write("unreadable: %s" % exc)
    sys.exit(0)
sys.stdout.write("runs" if value else "skipped")
PY
}

# A review the concurrency group killed: no execution log, so `ran` is `no`,
# and `published` never ran so its state is empty. This is #173.
CANCELLED_CTX='{"steps.auth.outputs.ok":"true","steps.review.outcome":"cancelled","steps.happened.outputs.ran":"no","steps.published.outputs.state":""}'
# A review that genuinely could not reach the model, including a
# `timeout-minutes` kill -- which reports `failure`, never `cancelled`.
FAILED_CTX='{"steps.auth.outputs.ok":"true","steps.review.outcome":"failure","steps.happened.outputs.ran":"no","steps.published.outputs.state":""}'
# A review that ran, succeeded and published no verdict. #339's case.
SILENT_CTX='{"steps.auth.outputs.ok":"true","steps.review.outcome":"success","steps.happened.outputs.ran":"yes","steps.published.outputs.state":"silent"}'

check_step() {
  local name="$1" ctx_name="$2" ctx="$3" want="$4"
  local cancelled=no
  [ "$ctx_name" = "cancelled" ] && cancelled=yes
  local cond
  cond="$(extract_if "$name" "$WF")"
  if [ -z "$cond" ]; then
    no "$name has an if: condition" "none found in $WF"
    return
  fi
  say "$name on a $ctx_name run" "$(evaluate "$cond" "$cancelled" "$ctx")" "$want"
}

echo
echo "  the two steps that report a review as not having happened"
check_step "Work out why the review did not happen" cancelled "$CANCELLED_CTX" skipped
check_step "Work out why the review did not happen" failed    "$FAILED_CTX"    runs
check_step "Say that the review did not happen"     cancelled "$CANCELLED_CTX" skipped
check_step "Say that the review did not happen"     failed    "$FAILED_CTX"    runs

echo
echo "  and the step that reports a review which ran and said nothing, which is a"
echo "  different fact and must survive"
check_step "Say that the review published nothing" silent "$SILENT_CTX" runs
check_step "Say that the review published nothing" cancelled "$CANCELLED_CTX" skipped

echo
echo "  the cap: a review that was not bought must not be reported as one that failed"
# A review the five-round cap skipped before the model was ever invoked. `Review`
# did not run, so its outcome is `skipped` and `happened` reports `ran=no` — the
# same shape as a review that could not reach the model, which is precisely what
# the two steps above exist to report. They must stay quiet here. A cap is a
# decision, and the step that made it has already said so on the pull request;
# `the independent review did not run` beside a deliberate skip is #173 again
# with a different cause.
CAPPED_CTX='{"steps.auth.outputs.ok":"true","steps.cap.outputs.run":"no","steps.review.outcome":"skipped","steps.happened.outputs.ran":"no","steps.published.outputs.state":""}'
check_step "Work out why the review did not happen" capped "$CAPPED_CTX" skipped
check_step "Say that the review did not happen"     capped "$CAPPED_CTX" skipped

# AND THE PAID STEP ITSELF, which is the only assertion here that saves anything.
check_step "Review" capped "$CAPPED_CTX" skipped

# The same context with the cap open: every step above must run again, or the
# clause added for the cap would be skipping them for some other reason and the
# three assertions above would be true by accident.
UNCAPPED_CTX='{"steps.auth.outputs.ok":"true","steps.cap.outputs.run":"yes","steps.review.outcome":"failure","steps.happened.outputs.ran":"no","steps.published.outputs.state":""}'
check_step "Work out why the review did not happen" uncapped "$UNCAPPED_CTX" runs
check_step "Say that the review did not happen"     uncapped "$UNCAPPED_CTX" runs
check_step "Review" uncapped "$UNCAPPED_CTX" runs

# A gate that could not execute leaves the output empty, and an empty output is
# not `no`. That is the fail-open direction the gate itself takes, asserted here
# on the workflow side: a broken cap step buys the review, never skips it.
BROKEN_CAP_CTX='{"steps.auth.outputs.ok":"true","steps.cap.outputs.run":"","steps.review.outcome":"failure","steps.happened.outputs.ran":"no","steps.published.outputs.state":""}'
check_step "Review" "cap-that-failed" "$BROKEN_CAP_CTX" runs
check_step "Say that the review did not happen" "cap-that-failed" "$BROKEN_CAP_CTX" runs

echo
echo "  the evaluator itself: an always() condition would run on a cancelled job,"
echo "  which is what makes the two assertions above mean anything"
say "always() runs on a cancelled job" \
    "$(evaluate "always() && steps.auth.outputs.ok == 'true'" yes "$CANCELLED_CTX")" runs
say "!cancelled() does not" \
    "$(evaluate "!cancelled() && steps.auth.outputs.ok == 'true'" yes "$CANCELLED_CTX")" skipped
say "!cancelled() still runs on a failure" \
    "$(evaluate "!cancelled() && steps.auth.outputs.ok == 'true'" no "$FAILED_CTX")" runs

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
