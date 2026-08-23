#!/usr/bin/env bash
# Does a label edit reach the right GitHub object?
#
# Offline: a stub `gh` on PATH records its arguments instead of calling GitHub.
# See the header of .github/scripts/gh-label.sh for what this is guarding
# against — in one sentence, that `gh issue edit` does not resolve pull
# requests, that claude-agent.yml is started from pull requests routinely, and
# that every such call is `|| true` so neither outcome is reported.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
# shellcheck source-path=SCRIPTDIR
# shellcheck source=../scripts/gh-label.sh
. "$here/../scripts/gh-label.sh"

pass=0; fail=0

stub=$(mktemp -d) || exit 1
trap 'rm -rf "$stub"' EXIT
cat > "$stub/gh" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*"
STUB
chmod +x "$stub/gh"
PATH="$stub:$PATH"

# want DESCRIPTION EXPECTED_COMMAND_LINE -- ACTUAL...
want() {
  local desc="$1" expected="$2" got="$3"
  if [ "$got" = "$expected" ]; then
    pass=$((pass + 1)); printf '  ok    %s\n' "$desc"
  else
    fail=$((fail + 1)); printf '  FAIL  %s\n         wanted "%s"\n            got "%s"\n' "$desc" "$expected" "$got"
  fi
}

echo "Label edits reach the object they name"

want "a pull request is edited with 'gh pr edit', never 'gh issue edit'" \
     "pr edit 85 --repo o/r --add-label agent:working" \
     "$(attadipa_label_edit pr 85 o/r --add-label agent:working)"

want "an issue is edited with 'gh issue edit'" \
     "issue edit 82 --repo o/r --add-label agent:working" \
     "$(attadipa_label_edit issue 82 o/r --add-label agent:working)"

# An unreadable lookup must not silently take the pull request path: every
# scheduled trigger in this repository dispatches an issue number.
want "an unknown kind falls back to the issue path" \
     "issue edit 82 --repo o/r --remove-label agent:ready" \
     "$(attadipa_label_edit "" 82 o/r --remove-label agent:ready)"
want "and so does a kind that is neither word" \
     "issue edit 82 --repo o/r --remove-label agent:ready" \
     "$(attadipa_label_edit unknown 82 o/r --remove-label agent:ready)"

want "several label arguments survive in order" \
     "pr edit 85 --repo o/r --add-label agent:review --remove-label agent:working" \
     "$(attadipa_label_edit pr 85 o/r --add-label agent:review --remove-label agent:working)"

# A label containing a space would become two labels under word splitting, and
# the shell would not mention it. claude-agent.yml's claim step builds an
# argument array for the same reason and says so.
want "a label with a space in it stays one argument" \
     "issue edit 82 --repo o/r --add-label needs owner" \
     "$(attadipa_label_edit issue 82 o/r --add-label "needs owner")"

echo
echo "Comments too"

want "a pull request is commented on with 'gh pr comment'" \
     "pr comment 85 --repo o/r --body-file /tmp/outcome.md" \
     "$(attadipa_label_comment pr 85 o/r --body-file /tmp/outcome.md)"

want "an issue is commented on with 'gh issue comment'" \
     "issue comment 82 --repo o/r --body-file /tmp/outcome.md" \
     "$(attadipa_label_comment issue 82 o/r --body-file /tmp/outcome.md)"

want "an unknown kind comments on the issue path" \
     "issue comment 82 --repo o/r --body-file /tmp/outcome.md" \
     "$(attadipa_label_comment "" 82 o/r --body-file /tmp/outcome.md)"

echo
echo "The exit status is the real one"

# Every call site in claude-agent.yml is `|| true`, which is a decision about
# what to do with a failure -- not a licence for this file to swallow one.
cat > "$stub/gh" <<'STUB'
#!/usr/bin/env bash
exit 7
STUB
if attadipa_label_edit issue 82 o/r --add-label x; then
  fail=$((fail + 1)); printf '  FAIL  a failing gh is reported as a failure\n'
else
  status=$?
  if [ "$status" -eq 7 ]; then
    pass=$((pass + 1)); printf '  ok    a failing gh is reported as a failure, with its own status\n'
  else
    fail=$((fail + 1)); printf '  FAIL  a failing gh is reported as a failure, with its own status\n         wanted 7, got %s\n' "$status"
  fi
fi

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
