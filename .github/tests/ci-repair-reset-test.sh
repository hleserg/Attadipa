#!/usr/bin/env bash
# Who may clear a pull request's CI escalation, and with what words?
#
# The property under test is a trust boundary with two halves, and getting
# either wrong is expensive in a different direction:
#
#   too narrow  -- the escalation has no exit, which is the defect #129
#                  reported: a diagnosed, fixed, green, reviewed pull request
#                  carrying `agent:blocked` forever and no unattended merge.
#   too wide    -- any comment dissolves a human escalation and hands the
#                  branch to the unattended backstop, which requires exactly
#                  that label to be absent. claude-agent.yml refuses to strip
#                  it from a pull request for this reason; a reset command that
#                  fired on prose would put the hole back one file over.
#
# The awkward case in the middle is this repository's own text. The give-up
# comment claude-ci-repair.yml writes NAMES the command, so the command's own
# spelling appears in a bot comment on every escalated pull request -- and in
# the documentation, and in any human comment explaining the loop. That is the
# same trap intake-decision.sh's header describes being sprung twice in one day
# on `@claude`, and it is why the command has to be a whole line.
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
# shellcheck source-path=SCRIPTDIR
# shellcheck source=../scripts/ci-repair-reset.sh
. "$here/../scripts/ci-repair-reset.sh"

pass=0; fail=0
want() {
  local desc="$1" expected="$2" got="$3"
  case "$got" in
    "$expected"*) pass=$((pass + 1)); printf '  ok    %s\n' "$desc" ;;
    *) fail=$((fail + 1))
       printf '  FAIL  %s\n         wanted "%s..."\n            got "%s"\n' "$desc" "$expected" "$got" ;;
  esac
}

echo "A person with write access, saying the command and nothing else"

want "the whole comment is the command" reset \
     "$(attadipa_ci_reset_decision hleserg admin true '/ci-repair reset')"
want "maintain is enough" reset \
     "$(attadipa_ci_reset_decision someone maintain true '/ci-repair reset')"
want "write is enough" reset \
     "$(attadipa_ci_reset_decision someone write true '/ci-repair reset')"
want "the command on its own line, with an explanation under it" reset \
     "$(attadipa_ci_reset_decision hleserg admin true '/ci-repair reset

The CMake version pin was the cause; fixed in a1b2c3d.')"
want "leading and trailing whitespace on the line does not matter" reset \
     "$(attadipa_ci_reset_decision hleserg admin true '   /ci-repair reset   ')"
want "and neither does where in the comment the line is" reset \
     "$(attadipa_ci_reset_decision hleserg admin true 'Root cause was the pinned toolchain.

/ci-repair reset')"

echo
echo "Nobody else, and no other wording"

want "read access is not write access" "reject: actor stranger has permission 'read'" \
     "$(attadipa_ci_reset_decision stranger read true '/ci-repair reset')"
want "and a failed permission lookup reads as none, which refuses" \
     "reject: actor stranger has permission 'none'" \
     "$(attadipa_ci_reset_decision stranger none true '/ci-repair reset')"

# THE ONE THAT WOULD HAVE FIRED. This is the give-up comment's own last
# sentence, which sits on every escalated pull request, written by a bot.
# shellcheck disable=SC2016  # comment text, quoted exactly as it is written
GIVE_UP='**Automatic CI repair has stopped.**

Two attempts have already been made on this problem chain and CI is
still failing in: `build`

A third automatic attempt would be guessing rather than diagnosing, so
this needs a look. When the cause is understood, comment
`/ci-repair reset` to clear the counter, or push a fix directly.'
want "the give-up comment does not reset the escalation it just raised" \
     "reject: no /ci-repair reset on a line of its own" \
     "$(attadipa_ci_reset_decision 'github-actions[bot]' none true "$GIVE_UP")"
# ...and it must be refused for the RIGHT reason. Leaning on the bot check to
# cover a wording rule that is wrong is how the wording rule stops being read.
# shellcheck disable=SC2016  # backticks are markdown here, not a subshell
want "a person quoting the same sentence is refused the same way" \
     "reject: no /ci-repair reset on a line of its own" \
     "$(attadipa_ci_reset_decision hleserg admin true 'When the cause is understood, comment `/ci-repair reset` to clear the counter.')"
# shellcheck disable=SC2016  # backticks are markdown here, not a subshell
want "a code span on its own line is still not the command" \
     "reject: no /ci-repair reset on a line of its own" \
     "$(attadipa_ci_reset_decision hleserg admin true '`/ci-repair reset`')"
want "and neither is a near miss" \
     "reject: no /ci-repair reset on a line of its own" \
     "$(attadipa_ci_reset_decision hleserg admin true '/ci-repair reset-all')"
want "nor the command with an argument after it" \
     "reject: no /ci-repair reset on a line of its own" \
     "$(attadipa_ci_reset_decision hleserg admin true '/ci-repair reset now please')"

echo
echo "Never our own output, whatever it says"

want "claude[bot] cannot clear an escalation raised for a person" \
     "reject: actor claude[bot] is a bot" \
     "$(attadipa_ci_reset_decision 'claude[bot]' write true '/ci-repair reset')"
want "github-actions[bot] cannot either" \
     "reject: actor github-actions[bot] is a bot" \
     "$(attadipa_ci_reset_decision 'github-actions[bot]' write true '/ci-repair reset')"
want "and the bare logins are refused as well" \
     "reject: actor claude is a bot" \
     "$(attadipa_ci_reset_decision claude admin true '/ci-repair reset')"
# A named producer is a producer, not an operator. ATTADIPA_TRUSTED_PRODUCERS
# buys an app the right to FILE a task -- issues events only, and never a
# comment. Nothing about that list reaches this command, and the check that
# keeps it that way is that this file does not read the list at all.
want "a trusted producing app is still a bot here" \
     "reject: actor chatgpt-codex-connector[bot] is a bot" \
     "$(attadipa_ci_reset_decision 'chatgpt-codex-connector[bot]' write true '/ci-repair reset')"

echo
echo "And only on a pull request"

want "an issue has no CI escalation to clear" "reject: not a pull request" \
     "$(attadipa_ci_reset_decision hleserg admin false '/ci-repair reset')"
# An unreadable answer is not a yes. The caller passes the literal string
# "true" and anything else -- an empty value from a lookup that failed
# included -- must refuse rather than fall through to the label edits.
want "and an unknown object is not a pull request" "reject: not a pull request" \
     "$(attadipa_ci_reset_decision hleserg admin '' '/ci-repair reset')"

echo
printf '  %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
