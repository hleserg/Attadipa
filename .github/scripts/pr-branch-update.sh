#!/usr/bin/env bash
# Keep every open pull request building against `main` as it is now, and say so
# where that cannot be done.
#
# The workflow supplies a trusted default-branch checkout, REPO, GH_TOKEN and
# UPDATE_TOKEN. The rule is in pr-branch-update-decision.sh and the path list is
# in pr-conflict-paths.jq; both are executed directly by their test. This file
# is the transport: what to read, in what order, and what to do with the answer.
#
# ==========================================================================
# THE CREDENTIAL, WHICH IS THE WHOLE POINT AND NOT A DETAIL
# ==========================================================================
#
# Updating a branch is only worth doing because the update makes CI run against
# the base as it is now. If the push does not start a workflow run, this job
# replaces **dirty, no CI** with **clean, no CI** -- which is worse, because it
# looks fine. Issue #171 says exactly that, and it is why the job was filed and
# then deliberately left unstarted.
#
#   "GitHub does not start workflow runs for events created with the built-in
#    GITHUB_TOKEN (except workflow_dispatch and repository_dispatch)."
#   -- quoted in docs/automation/CLAUDE_AUTOMATION.md, "The GitHub credential
#      -- and why it is *not* GITHUB_TOKEN"
#
#   "When a pull request is created or updated by a workflow using GITHUB_TOKEN,
#    pull_request events with the opened, synchronize, or reopened activity
#    types create workflow runs that require approval."
#   -- GitHub documentation, quoted on issue #171
#
# Either way the branch ends up clean and unbuilt. So the update-branch write is
# made with UPDATE_TOKEN, which the workflow sets to the credential this
# repository already documents as the one that does start runs
# (ATTADIPA_AGENT_TOKEN, or the Claude App installation token it falls back to).
# Reads, labels and comments keep using GH_TOKEN, because -- in the words of the
# same document -- "a label change is not supposed to start a workflow".
#
# WHEN UPDATE_TOKEN IS EMPTY THIS JOB UPDATES NOTHING. It still does the half
# that needs no CI: labelling what conflicts and unlabelling what no longer
# does. Pushing anyway with a credential that cannot start a build is the
# documented failure this job exists to avoid, and doing it quietly would be the
# version nobody notices.
#
# ==========================================================================
# THE BOUND
# ==========================================================================
#
# MAX_PULLS_EXAMINED caps the reads and MAX_UPDATES_PER_RUN caps the writes.
# The repository's own WIP limit is two active pull requests
# (.github/scripts/wip-limit.sh: "normal limit: 2") and thirteen open at once is
# the observed high-water mark, so ten updates per run is a bound that does not
# truncate in practice. It exists so that a queue nobody expected cannot turn
# one merge into a hundred pushes. When it does truncate it says so once, and
# the next merge to the base picks up the rest; nothing is lost, because the
# decision is recomputed from GitHub's state every run and holds no memory.
#
# IDEMPOTENCE. Every write here is gated on a fact read in the same run:
# update-branch only when `behind_by > 0`, the label only when it is absent, the
# comment only when no comment carries its marker for this head commit. A second
# run over an unchanged repository issues no write at all.

set -uo pipefail
: "${REPO:?REPO is required}"

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1

readonly MAX_PULLS_EXAMINED=50
readonly MAX_UPDATES_PER_RUN=10

UPDATE_TOKEN="${UPDATE_TOKEN:-}"
BASE="${BASE:-}"
if [ -z "$BASE" ]; then
  BASE="$(gh api "repos/$REPO" --jq '.default_branch' 2>/dev/null)" || BASE=""
fi
if [ -z "$BASE" ]; then
  echo "::error::could not determine the default branch; this run decided nothing"
  exit 1
fi

if [ -z "$UPDATE_TOKEN" ]; then
  echo "::warning::no UPDATE_TOKEN: nothing will be updated this run. A push made with the built-in token does not start a workflow run, and a clean branch that nothing has built is worse than a dirty one. Conflict labels are still maintained."
fi

BASE_SHA="$(gh api "repos/$REPO/commits/$BASE" --jq '.sha' 2>/dev/null)" || BASE_SHA=""
BASE_AT="$BASE"
[ -z "$BASE_SHA" ] || BASE_AT="$BASE at \`${BASE_SHA:0:8}\`"

# `mergeable` is not in the LIST response -- only in the single-pull-request one
# -- so the list is used for nothing but the numbers, filtered to this base by
# the API rather than by us. A pull request stacked on another branch is not
# this job's business: updating it would merge the default branch into one whose
# author deliberately based it elsewhere.
if ! PRS="$(gh api --paginate --slurp \
  "repos/$REPO/pulls?state=open&base=$BASE&per_page=100" 2>/dev/null \
  | jq -r 'if type=="array" and (.[0]|type)=="array" then add else . end
            | map(.number) | .[]' 2>/dev/null)"; then
  echo "::error::could not list open pull requests; this run decided nothing"
  exit 1
fi

if [ -z "$PRS" ]; then
  echo "::notice::no open pull requests based on $BASE"
  exit 0
fi

UPDATED=0
FLAGGED=0
CLEARED=0
EXAMINED=0
CAP_SAID=no

# Prints the facts and NOTHING ELSE, for the reason the watchdog's fetch_facts
# gives: its caller uses $(...), so a line meant for a human would be captured
# as data instead of reaching the log.
fetch_facts() {
  local n="$1" raw
  raw="$(gh api "repos/$REPO/pulls/$n" 2>/dev/null)" || return 1
  # -e so a response that is not the promised shape fails here rather than
  # producing the string "null" for the decision to guess at. `gh` writes error
  # documents to stdout, and #71's outcome comment went out as a JSON blob
  # because something downstream trusted one.
  #
  # The `!= null` in the third field is not decoration. A fork whose repository
  # has since been deleted reports `head.repo: null`, and comparing two absent
  # names for equality would answer "same repository" for the one case where
  # there is no head repository to push to at all.
  printf '%s' "$raw" | jq -e -r '
    [ (.mergeable | tostring),
      (.head.sha // ""),
      (if (.head.repo.full_name != null)
          and (.head.repo.full_name == .base.repo.full_name)
       then "yes" else "no" end),
      ([.labels[]?.name] | join(",")) ] | @tsv' 2>/dev/null
}

for PR in $PRS; do
  if [ "$EXAMINED" -ge "$MAX_PULLS_EXAMINED" ]; then
    echo "::notice::reached the cap of $MAX_PULLS_EXAMINED pull requests examined; the rest wait for the next merge to $BASE"
    break
  fi
  EXAMINED=$((EXAMINED + 1))

  FACTS="$(fetch_facts "$PR")" || FACTS=""
  # One retry, and only for the pull request that needs it. A push landing
  # seconds before this run reads `mergeable: null` -- and the read itself is
  # what asks GitHub to compute it, so the second attempt usually answers. The
  # sleep is paid per undetermined pull request rather than per run because the
  # merge that triggered this touched at most one of them.
  case "$FACTS" in
    null*|'')
      sleep 3
      FACTS="$(fetch_facts "$PR")" || FACTS="" ;;
  esac
  if [ -z "$FACTS" ]; then
    echo "::notice::#$PR: could not read its state, leaving it alone"
    continue
  fi
  IFS=$'\t' read -r MERGEABLE HEAD_SHA SAME_REPO LABELS <<<"$FACTS"

  # One compare per pull request, always: it answers `behind_by` for the update
  # decision and carries the merge base and the branch's own file list, which is
  # what the conflict comment needs. Asking for them separately would be two
  # requests for one answer.
  COMPARE="$(gh api "repos/$REPO/compare/$BASE...$HEAD_SHA" 2>/dev/null)" || COMPARE=""
  BEHIND=""
  MERGE_BASE=""
  if [ -n "$COMPARE" ]; then
    BEHIND="$(printf '%s' "$COMPARE" | jq -r '.behind_by | tostring' 2>/dev/null)" || BEHIND=""
    MERGE_BASE="$(printf '%s' "$COMPARE" | jq -r '.merge_base_commit.sha // ""' 2>/dev/null)" || MERGE_BASE=""
  fi

  DECISION="$(bash "$here/pr-branch-update-decision.sh" \
    "$MERGEABLE" "$BEHIND" "$SAME_REPO" "$LABELS")"
  ACTION="$(printf '%s' "$DECISION" | sed -n 1p)"
  REASON="$(printf '%s' "$DECISION" | sed -n 2p)"

  case "$ACTION" in
    quiet)
      echo "::notice::#$PR: nothing to do ($REASON)"
      continue ;;

    clear)
      if gh pr edit "$PR" --repo "$REPO" --remove-label needs-rebase >/dev/null; then
        CLEARED=$((CLEARED + 1))
        echo "::notice::#$PR: merges cleanly again, needs-rebase removed"
      else
        echo "::warning::#$PR: could not remove needs-rebase"
      fi
      continue ;;

    flag)
      : ;;

    update)
      if [ -z "$UPDATE_TOKEN" ]; then
        echo "::notice::#$PR: $BEHIND commit(s) behind $BASE, not updated (no UPDATE_TOKEN)"
        continue
      fi
      # `continue` rather than `break`: the cap is on WRITES, and the pull
      # requests after this one may need only a label added or removed, which
      # costs nothing that the cap is protecting.
      if [ "$UPDATED" -ge "$MAX_UPDATES_PER_RUN" ]; then
        if [ "$CAP_SAID" = no ]; then
          echo "::notice::reached the cap of $MAX_UPDATES_PER_RUN updates; the rest wait for the next merge to $BASE"
          CAP_SAID=yes
        fi
        continue
      fi
      # Bind the write to the commit every fact above was read for, the way
      # pr-merge-sweep.sh binds its merge (#322). The window is this loop body:
      # a push landing inside it would otherwise be merged into under a decision
      # earned by the head it replaced. GitHub refuses on mismatch, and the
      # refusal is handled below like any other.
      if GH_TOKEN="$UPDATE_TOKEN" gh api --method PUT \
           "repos/$REPO/pulls/$PR/update-branch" \
           -f "expected_head_sha=$HEAD_SHA" >/dev/null 2>&1; then
        UPDATED=$((UPDATED + 1))
        echo "::notice::#$PR: merged $BASE_AT into it; it was $BEHIND commit(s) behind"
        # The 202 is the evidence that the branch merges cleanly, which is
        # exactly what `needs-rebase` claims it does not.
        case ",$LABELS," in
          *",needs-rebase,"*)
            if gh pr edit "$PR" --repo "$REPO" --remove-label needs-rebase >/dev/null; then
              CLEARED=$((CLEARED + 1))
            fi ;;
        esac
        continue
      fi
      # update-branch answers 422 for a merge conflict AND for an
      # expected_head_sha that no longer matches, with nothing but prose to tell
      # them apart. Rather than pattern-matching GitHub's wording -- which is
      # not part of any contract and has changed before -- re-read the fact and
      # let the same rule speak. A stale head still reads mergeable=true and is
      # picked up by the next run; a real conflict reads false and is flagged
      # now, on facts re-gathered for the head that is actually there.
      RECHECK="$(fetch_facts "$PR")" || RECHECK=""
      if [ "$(printf '%s' "$RECHECK" | cut -f1)" != false ]; then
        echo "::notice::#$PR: update-branch refused and it does not report a conflict; leaving it for the next run"
        continue
      fi
      REASON=conflicted-on-update
      IFS=$'\t' read -r _ HEAD_SHA _ LABELS <<<"$RECHECK"
      # The head moved under us, so the compare read at the top of this loop
      # describes a commit that is no longer there. Read it again rather than
      # naming paths from the branch as it was: a stale path list is exactly the
      # kind of nearly-right that costs somebody an afternoon.
      COMPARE="$(gh api "repos/$REPO/compare/$BASE...$HEAD_SHA" 2>/dev/null)" || COMPARE=""
      MERGE_BASE=""
      [ -z "$COMPARE" ] || MERGE_BASE="$(printf '%s' "$COMPARE" \
        | jq -r '.merge_base_commit.sha // ""' 2>/dev/null)" || MERGE_BASE=""
      ;;

    *)
      # The decision printed something this transport does not know. Saying
      # nothing is the only safe reading: every remaining action writes.
      echo "::warning::#$PR: unrecognised decision '$ACTION'; leaving it alone"
      continue ;;
  esac

  # Only `flag` reaches here, from the decision or from a refused update.
  case ",$LABELS," in
    *",needs-rebase,"*) : ;;
    *)
      gh pr edit "$PR" --repo "$REPO" --add-label needs-rebase >/dev/null \
        || echo "::warning::#$PR: could not add needs-rebase (does the label exist? .github/scripts/setup-labels.sh creates it)" ;;
  esac

  # Keyed on the head commit, not on the pull request: a branch left conflicted
  # for a week gets one comment, and this job does not speak again until its
  # author pushes. #119's watchdog owns the "a conflict means no CI at all"
  # sentence and says it once per head too; this one names a label and the
  # paths, and the marker is what keeps the two from becoming an hourly duet.
  MARKER="<!-- attadipa-needs-rebase:$HEAD_SHA -->"
  SAID="$(gh api --paginate --slurp "repos/$REPO/issues/$PR/comments?per_page=100" 2>/dev/null \
          | jq -r --arg m "$MARKER" \
              'if type=="array" and (.[0]|type)=="array" then add else . end
               | any(.[]; (.body // "") | contains($m))' 2>/dev/null)" || SAID=""
  # An unreadable comment list counts as "already said". A guard that cannot
  # tell whether it has spoken must not speak: a missed comment costs one merge
  # cycle, a duplicated one costs the channel.
  if [ "$SAID" != false ]; then
    FLAGGED=$((FLAGGED + 1))
    echo "::notice::#$PR: labelled needs-rebase ($REASON); not commenting (already said for this head, or its comments could not be read)"
    continue
  fi

  PATHS=""
  if [ -n "$COMPARE" ] && [ -n "$MERGE_BASE" ]; then
    BASE_MOVED="$(gh api "repos/$REPO/compare/$MERGE_BASE...$BASE" 2>/dev/null)" || BASE_MOVED=""
    if [ -n "$BASE_MOVED" ]; then
      PATHS="$(jq -n --argjson pr "$COMPARE" --argjson base "$BASE_MOVED" \
                 '{pr: $pr, base: $base}' 2>/dev/null \
               | jq -r -f "$here/pr-conflict-paths.jq" 2>/dev/null)" || PATHS=""
    fi
  fi
  if [ -z "$PATHS" ]; then
    PATHS="The paths cannot be named this run: one of the two comparisons against the merge base did not answer. The label stands on \`mergeable: false\`, which did."
  fi

  BODY="${RUNNER_TEMP:-/tmp}/needs-rebase-$PR.md"
  {
    printf '%s\n' "$MARKER"
    printf '%s\n\n' "### This branch conflicts with \`$BASE\` and cannot be updated automatically"
    printf '%s\n\n' "$BASE has moved on — this run is looking at $BASE_AT — and merging it into this branch fails. An automatic update would be refused, so none was attempted."
    printf '%s\n\n' "$PATHS"
    printf '%s\n' "Labelled \`needs-rebase\`. Rebase, or merge \`$BASE\` in by hand; the label comes off automatically on the first run after the branch merges cleanly again, and this comment is not repeated until you push."
  } > "$BODY"

  if gh pr comment "$PR" --repo "$REPO" --body-file "$BODY" >/dev/null </dev/null; then
    FLAGGED=$((FLAGGED + 1))
    echo "::warning::#$PR: conflicts with $BASE, labelled needs-rebase and named the paths ($REASON)"
  else
    echo "::warning::#$PR: could not comment; the label is on and the paths were not named"
  fi
done

echo "::notice::examined $EXAMINED pull request(s) on $BASE: $UPDATED updated, $FLAGGED flagged, $CLEARED cleared"
