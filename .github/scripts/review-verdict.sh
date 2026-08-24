#!/usr/bin/env bash
# May this round of the independent review still hold the pull request?
#
# WHAT THIS IS FOR. `claude-pr-review.yml` reviews the head commit. Answer every
# finding, push, and the next run reviews the *new* head — and finds new
# findings, necessarily, because the diff changed. Observed on 2026-08-24 across
# four branches at rounds five, eight, ten and two: the findings shrink each
# round (a `§4.1` that should read `§2.4`; a line number that drifted three
# lines) and the label stays `ai-review:blocking`, so the orchestrator's rule —
# merge once CI is green — never fires. There was no convergence rule at all:
# no round counter, no severity floor, no way for a finding to become a
# follow-up instead of a blocker. Issue #169.
#
# THE RULE, and it is one line rather than two regimes:
#
#   an open finding holds the pull request iff
#       it is FLOOR-category, or it was first raised BEFORE the floor round.
#
# Floor-category is the list the acceptance in #169 refuses to weaken: a
# hardware fact with no source, a `PASS` for a test that did not run on a board,
# an application-layer hardware access, an architecture-boundary violation. Those
# hold at **any** round, however late they are found. Everything else is
# published, marked deferred, and filed rather than held.
#
# WHY ONE FORMULA AND NOT A SWITCH. Below the floor every open finding was by
# construction first raised before the floor, so the same expression gives the
# old behaviour for rounds 1..floor-1 without a second code path to get wrong.
#
# WHY DATING IS NOT THE MODEL'S. #169 proposed "carry-over only": a re-review may
# block on a finding it already raised and the push did not fix. Taken literally
# that does not converge — a prose defect first raised at round 7 is a carry-over
# at round 8 and blocks, so each round mints next round's blockers and the queue
# is unbounded again. The fix is that a deferred finding never ages into a
# blocker: `first_round` is read from the ledger this script itself wrote last
# round, never from what the reviewer says this round, so a finding cannot be
# re-dated into blocking or out of it.
#
# THE SAFE DIRECTION, every time there is a choice:
#
#   - a finding in the ledger that this round does not mention stays OPEN. To
#     close one the reviewer must say `fixed` about it by name. "It did not come
#     up again" is not evidence that a push fixed it.
#   - a category that is neither `floor` nor `normal` is read as `floor`.
#   - a status that is neither `open` nor `fixed` is read as `open`.
#   - an unreadable `first_round` is read as 1, which is below every floor.
#   - a corrupt or absent round number restarts at round 1, which is the open
#     regime rather than the converged one.
#   - no findings block at all prints `label=unknown`, and the caller is expected
#     to leave whatever the reviewer set itself. Silence never invents a verdict.
#
# No network, no `gh`, no environment: every input is a file path or a value, so
# .github/tests/review-verdict-test.sh can execute it. Same reason as
# promote-decision.sh, handover-decision.sh, intake-decision.sh and
# merge-candidate.sh.

# attadipa_review_verdict PREV_LEDGER FINDINGS FLOOR LEDGER_OUT DEFERRED_OUT PR
#
# PREV_LEDGER   path to the body of the ledger comment this script wrote last
#               round. Missing or empty means round 1.
# FINDINGS      path to the body of the reviewer's own comment this round. The
#               block is read out of it; the surrounding prose is ignored.
# FLOOR         the convergence floor, a positive integer round number.
# LEDGER_OUT    where to write the new ledger comment body. Empty to skip.
# DEFERRED_OUT  where to write the follow-up issue body. Empty to skip. Written
#               only when there is at least one deferred finding.
# PR            the pull request number, used only in rendered text.
#
# Prints `key=value` lines on stdout and nothing else:
#
#   round=      the round this review is, counting only rounds that published
#   floor=      the floor it was judged against
#   label=      ai-review:blocking | ai-review:pass | unknown
#   reason=     one token: floor | pre-floor | floor-and-pre-floor |
#               nothing-holding | no-findings-block. `pre-floor` rather than
#               `carry-over` because at round 1 every finding satisfies the same
#               clause and calling that a carry-over would be false.
#   open=       open findings after reconciliation
#   blocking=   of those, the ones that hold the pull request
#   deferred=   of those, the ones that do not
#   deferred_title=  the title for the follow-up issue, when there is one
#   deferred_issue=  the issue the deferred findings were filed as, when the
#                    ledger already records one. Carried forward, never invented:
#                    the caller appends one `deferred_issue=` line to the state
#                    block after it creates the issue, and every round after that
#                    reads it back rather than searching issue bodies for a
#                    marker, because GitHub's issue search is an index with lag
#                    and two rounds minutes apart would file the follow-up twice.
#
# THE FINDINGS BLOCK the reviewer writes, inside its own comment:
#
#   <!-- attadipa-review-findings
#   gnss-trust-source | open  | floor  | The trust state is claimed with no source
#   adr-section-number | fixed | normal | ADR-0007 §4.1 should read §2.4
#   -->
#
# THE LEDGER STATE BLOCK this script writes, inside the ledger comment:
#
#   <!-- attadipa-review-ledger-state
#   round=7
#   floor=4
#   deferred_issue=170
#   gnss-trust-source | 2 | floor | open | The trust state is claimed with no source
#   -->

# shellcheck disable=SC2016  # The single-quoted printf formats below carry
# markdown backticks and literal `%s` placeholders, not shell expansions.
# Double-quoting them would turn every backtick into a command substitution.

ATTADIPA_FINDINGS_OPEN='<!-- attadipa-review-findings'
ATTADIPA_LEDGER_MARK='<!-- attadipa-review-ledger -->'
ATTADIPA_LEDGER_OPEN='<!-- attadipa-review-ledger-state'

# Everything between the FIRST opening marker line and the next `-->`, exclusive
# of both. Three properties that a sed range address does not give and that the
# inputs here need, since one of the two is written by a model:
#
#   - an opening marker that closes on its own line (`<!-- ... -->`) is an EMPTY
#     block, not the start of one. A sed range would take it as the start and
#     swallow every line up to the next `-->`, which on the ledger comment is
#     the rest of the comment.
#   - a block that is never closed prints to end of file WITHOUT losing its last
#     line, which `sed -e '$d'` would eat.
#   - only the first block is read, so a second copy pasted below cannot add
#     findings to the first.
_attadipa_block() {
  local file="$1" open="$2"
  [ -n "$file" ] && [ -f "$file" ] || return 0
  # \r first: a comment fetched from the API can carry CRLF, and a trailing \r
  # turns `open` into `open\r`, which then reads as an unknown status and is
  # silently promoted to blocking. That is the safe direction, but it is the
  # safe direction for the wrong reason and it would never converge.
  tr -d '\r' < "$file" | awk -v open="$open" '
    !inblock {
      line = $0
      sub(/^[ \t]+/, "", line)
      if (index(line, open) == 1) {
        rest = substr(line, length(open) + 1)
        if (index(rest, "-->") > 0) exit
        inblock = 1
      }
      next
    }
    index($0, "-->") { exit }
    { print }
  '
}

# A finding id is a slug and nothing else. Anything with a pipe, a bracket, a
# space or a newline in it would either break the ledger's own table or let a
# title reach a place a title should not; such a line is dropped rather than
# repaired, and the count says how many.
_attadipa_id_ok() {
  case "$1" in
    "" ) return 1 ;;
    *[!a-z0-9-]* ) return 1 ;;
    -* ) return 1 ;;
    * ) [ "${#1}" -le 60 ] ;;
  esac
}

_attadipa_trim() {
  local s="$1"
  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"
  printf '%s' "$s"
}

_attadipa_is_uint() {
  case "$1" in
    "" ) return 1 ;;
    *[!0-9]* ) return 1 ;;
    * ) return 0 ;;
  esac
}

attadipa_review_verdict() {
  local prev="${1:-}" findings="${2:-}" floor="${3:-}" \
        ledger_out="${4:-}" deferred_out="${5:-}" pr="${6:-}"

  _attadipa_is_uint "$floor" && [ "$floor" -ge 1 ] || floor=1

  declare -A first_round=() category=() status=() title=()
  local order=() id fr cat st ti line rest

  # ---- the ledger this script wrote last round -------------------------------
  local prev_round=0 prev_block deferred_issue=""
  prev_block="$(_attadipa_block "$prev" "$ATTADIPA_LEDGER_OPEN")"
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    case "$line" in
      round=*)
        rest="${line#round=}"
        _attadipa_is_uint "$rest" && prev_round="$rest"
        continue ;;
      floor=*) continue ;;
      deferred_issue=*)
        # Which issue the deferred findings were filed as, carried in the ledger
        # rather than looked up. The alternative is a search over issue bodies,
        # and GitHub's issue search is an index with lag — two review rounds
        # minutes apart would file the same follow-up twice. The caller appends
        # this line once, after it creates the issue; this script only carries
        # it forward.
        rest="${line#deferred_issue=}"
        _attadipa_is_uint "$rest" && deferred_issue="$rest"
        continue ;;
    esac
    case "$line" in *"|"*) ;; *) continue ;; esac
    id="$(_attadipa_trim "${line%%|*}")"; rest="${line#*|}"
    fr="$(_attadipa_trim "${rest%%|*}")"; rest="${rest#*|}"
    cat="$(_attadipa_trim "${rest%%|*}")"; rest="${rest#*|}"
    st="$(_attadipa_trim "${rest%%|*}")"
    case "$rest" in *"|"*) ti="$(_attadipa_trim "${rest#*|}")" ;; *) ti="" ;; esac
    _attadipa_id_ok "$id" || continue
    _attadipa_is_uint "$fr" && [ "$fr" -ge 1 ] || fr=1
    [ "$cat" = "normal" ] || cat=floor
    [ "$st" = "fixed" ] || st=open
    if [ -z "${first_round[$id]+set}" ]; then order+=("$id"); fi
    first_round["$id"]="$fr"; category["$id"]="$cat"
    status["$id"]="$st";     title["$id"]="$ti"
  done <<< "$prev_block"

  local round=$((prev_round + 1))

  # ---- what the reviewer said this round -------------------------------------
  local have_block=no findings_block
  if [ -n "$findings" ] && [ -f "$findings" ] \
     && tr -d '\r' < "$findings" | grep -qF "$ATTADIPA_FINDINGS_OPEN"; then
    have_block=yes
  fi
  findings_block="$(_attadipa_block "$findings" "$ATTADIPA_FINDINGS_OPEN")"

  local dropped=0
  if [ "$have_block" = yes ]; then
    while IFS= read -r line; do
      line="$(_attadipa_trim "$line")"
      [ -n "$line" ] || continue
      case "$line" in \#*) continue ;; esac
      case "$line" in *"|"*) ;; *) dropped=$((dropped + 1)); continue ;; esac
      id="$(_attadipa_trim "${line%%|*}")"; rest="${line#*|}"
      st="$(_attadipa_trim "${rest%%|*}")"; rest="${rest#*|}"
      cat="$(_attadipa_trim "${rest%%|*}")"
      case "$rest" in *"|"*) ti="$(_attadipa_trim "${rest#*|}")" ;; *) ti="" ;; esac
      if ! _attadipa_id_ok "$id"; then dropped=$((dropped + 1)); continue; fi
      [ "$st" = "fixed" ] || st=open
      [ "$cat" = "normal" ] || cat=floor
      # Sanitising the title, not trusting it. It is model text and it lands in
      # a markdown table cell: a pipe would add a column, a newline cannot get
      # here but a backtick run can still swallow the rest of the row.
      ti="${ti//|/ }"; ti="${ti//\`/\'}"
      [ "${#ti}" -le 160 ] || ti="${ti:0:157}..."
      if [ -z "${first_round[$id]+set}" ]; then
        order+=("$id"); first_round["$id"]="$round"
      fi
      # Category upgrades and never downgrades. A finding the reviewer called
      # floor in round 2 does not stop being floor because round 9 typed
      # `normal`; the acceptance in #169 is explicit that nothing weakens what
      # blocks, and a downgrade is exactly that.
      [ "${category[$id]:-normal}" = floor ] && cat=floor
      category["$id"]="$cat"; status["$id"]="$st"
      [ -n "$ti" ] && title["$id"]="$ti"
      [ -n "${title[$id]:-}" ] || title["$id"]="(no description given)"
    done <<< "$findings_block"
  fi

  # ---- the verdict -----------------------------------------------------------
  local open_n=0 blocking_n=0 deferred_n=0 floor_hits=0 pre_hits=0
  local defers=()
  for id in ${order[@]+"${order[@]}"}; do
    [ "${status[$id]}" = open ] || continue
    open_n=$((open_n + 1))
    if [ "${category[$id]}" = floor ]; then
      blocking_n=$((blocking_n + 1)); floor_hits=$((floor_hits + 1))
    elif [ "${first_round[$id]}" -lt "$floor" ]; then
      blocking_n=$((blocking_n + 1)); pre_hits=$((pre_hits + 1))
    else
      deferred_n=$((deferred_n + 1)); defers+=("$id")
    fi
  done

  local label reason
  if [ "$have_block" = no ]; then
    label=unknown; reason='no-findings-block'
  elif [ "$blocking_n" -eq 0 ]; then
    label=ai-review:pass; reason='nothing-holding'
  elif [ "$floor_hits" -gt 0 ] && [ "$pre_hits" -gt 0 ]; then
    label=ai-review:blocking; reason='floor-and-pre-floor'
  elif [ "$floor_hits" -gt 0 ]; then
    label=ai-review:blocking; reason='floor'
  else
    label=ai-review:blocking; reason='pre-floor'
  fi

  printf 'round=%s\n'     "$round"
  printf 'floor=%s\n'     "$floor"
  printf 'label=%s\n'     "$label"
  printf 'reason=%s\n'    "$reason"
  printf 'open=%s\n'      "$open_n"
  printf 'blocking=%s\n'  "$blocking_n"
  printf 'deferred=%s\n'  "$deferred_n"
  printf 'dropped=%s\n'   "$dropped"
  [ -n "$deferred_issue" ] && printf 'deferred_issue=%s\n' "$deferred_issue"
  [ "$deferred_n" -gt 0 ] && \
    printf 'deferred_title=Deferred review findings from #%s\n' "$pr"

  [ -n "$ledger_out" ] && _attadipa_render_ledger \
    "$ledger_out" "$round" "$floor" "$label" "$reason" \
    "$open_n" "$blocking_n" "$deferred_n" "$dropped"
  if [ -n "$deferred_out" ] && [ "$deferred_n" -gt 0 ]; then
    _attadipa_render_deferred "$deferred_out" "$pr" "$floor" "${defers[@]}"
  fi
  return 0
}

# The floor list, written once and quoted in three places, because three
# paraphrases of a rule are three rules.
ATTADIPA_FLOOR_LIST='a hardware fact with no source, a `PASS` for a test that did not run on a board, an application-layer hardware access, or an architecture-boundary violation'

_attadipa_render_ledger() {
  local out="$1" round="$2" floor="$3" label="$4" reason="$5"
  local open_n="$6" blocking_n="$7" deferred_n="$8" dropped="$9"
  local id holds
  {
    printf '%s\n' "$ATTADIPA_LEDGER_MARK"
    printf '### Independent review — round %s\n\n' "$round"
    printf 'The convergence floor is **round %s**. From that round on, an open finding holds this pull request only if it is *floor* — %s — or if it was first raised **before** round %s and the pushes since have not fixed it. Everything else is published, marked *deferred*, and filed rather than held.\n\n' \
      "$floor" "$ATTADIPA_FLOOR_LIST" "$floor"
    printf 'A deferred finding never becomes blocking by ageing, and the round a finding was first seen comes from this ledger rather than from the review that is running. The rule is `.github/scripts/review-verdict.sh`; what it is for is in `docs/automation/CI_AND_REVIEW_PIPELINE.md`.\n\n'
    if [ "$label" = unknown ]; then
      printf '**This round published no findings block**, so no verdict was computed from it and the label the reviewer set itself stands. The findings below are carried from earlier rounds unchanged.\n\n'
    fi
    if [ "${#order[@]}" -eq 0 ]; then
      printf 'No findings have been recorded on this pull request.\n\n'
    else
      printf '| finding | first seen | kind | state | holds the merge |\n'
      printf '|---|---|---|---|---|\n'
      for id in ${order[@]+"${order[@]}"}; do
        if [ "${status[$id]}" != open ]; then
          holds='no — fixed'
        elif [ "${category[$id]}" = floor ]; then
          holds='**yes** — floor'
        elif [ "${first_round[$id]}" -lt "$floor" ]; then
          holds='**yes** — raised before the floor'
        else
          holds='no — deferred'
        fi
        printf '| `%s` | round %s | %s | %s | %s |\n' \
          "$id" "${first_round[$id]}" "${category[$id]}" "${status[$id]}" "$holds"
      done
      printf '\n'
    fi
    printf '%s open · %s holding · %s deferred' "$open_n" "$blocking_n" "$deferred_n"
    if [ "$dropped" -gt 0 ]; then
      printf ' · %s line(s) of the findings block were unreadable and dropped' "$dropped"
    fi
    printf '\n\n'
    [ -n "$deferred_issue" ] && \
      printf 'The deferred findings are filed as #%s.\n\n' "$deferred_issue"
    case "$label" in
      unknown) printf 'Verdict: **not computed this round** (`%s`).\n' "$reason" ;;
      *)       printf 'Verdict: **`%s`** (`%s`).\n' "$label" "$reason" ;;
    esac
    printf '\n%s\n' "$ATTADIPA_LEDGER_OPEN"
    printf 'round=%s\n' "$round"
    printf 'floor=%s\n' "$floor"
    [ -n "$deferred_issue" ] && printf 'deferred_issue=%s\n' "$deferred_issue"
    for id in ${order[@]+"${order[@]}"}; do
      printf '%s | %s | %s | %s | %s\n' \
        "$id" "${first_round[$id]}" "${category[$id]}" "${status[$id]}" "${title[$id]:-}"
    done
    printf -- '-->\n'
  } > "$out"
}

_attadipa_render_deferred() {
  local out="$1" pr="$2" floor="$3"; shift 3
  local id
  {
    printf '<!-- attadipa-review-deferred:%s -->\n' "$pr"
    printf 'The independent review of #%s raised these findings **at or after the convergence floor** (round %s). They are real and unfixed. They do not hold that pull request, and they are recorded here so that not holding it does not mean losing them.\n\n' \
      "$pr" "$floor"
    printf '| finding | first seen | what it is |\n'
    printf '|---|---|---|\n'
    for id in "$@"; do
      printf '| `%s` | round %s | %s |\n' \
        "$id" "${first_round[$id]}" "${title[$id]:-}"
    done
    printf '\n'
    printf 'None of these is %s. Those hold the pull request at any round and never appear in this list.\n\n' \
      "$ATTADIPA_FLOOR_LIST"
    printf 'The reasoning is in the review comment on #%s; this is an index, not a copy. This issue carries no `agent:*` label on purpose — nothing here starts a billable run until somebody decides it should.\n' "$pr"
  } > "$out"
}

# Callable as a script as well as sourceable.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  attadipa_review_verdict "${1:-}" "${2:-}" "${3:-}" "${4:-}" "${5:-}" "${6:-}"
fi
