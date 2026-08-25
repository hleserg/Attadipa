#!/usr/bin/env bash
# Scheduled unattended merge transport. The workflow supplies a trusted default-
# branch checkout plus GH_TOKEN and REPO; the decision rules live beside this
# script and are executed directly by their tests.

set -uo pipefail
: "${REPO:?REPO is required}"

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 1
error_file="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/attadipa-pulls.err"

if ! PRS="$(gh api --paginate --slurp \
  "repos/$REPO/pulls?state=open&per_page=100" 2>"$error_file" \
  | jq -r 'if type=="array" and (.[0]|type)=="array" then add else . end
            | map(.number) | .[]' 2>>"$error_file")"; then
  echo "::error::could not list open pull requests ($(head -c 400 "$error_file" 2>/dev/null)); this sweep decided nothing"
  exit 1
fi

if [ -z "$PRS" ]; then
  echo "::notice::no open pull requests"
  exit 0
fi

readonly MAX_PER_RUN=3
readonly MAX_PERMISSION_LOOKUPS=25
MERGED=0
REFUSED=0

for PR in $PRS; do
  if [ "$MERGED" -ge "$MAX_PER_RUN" ]; then
    echo "::notice::reached the cap of $MAX_PER_RUN merges; the rest wait for the next sweep"
    break
  fi

  FACTS="$(gh api graphql -F query=@"$here/merge-facts.graphql" \
    -F owner="${REPO%%/*}" -F name="${REPO##*/}" -F number="$PR" \
    2>/dev/null)" || FACTS=""
  if [ -z "$FACTS" ] \
     || ! printf '%s' "$FACTS" | jq -e '.data.repository.pullRequest' >/dev/null 2>&1; then
    echo "::notice::#$PR: could not read its state, leaving it alone"
    continue
  fi

  COMPLETE="$(printf '%s' "$FACTS" | bash "$here/merge-facts.sh")"
  case "$COMPLETE" in
    COMPLETE) COMPLETE=true ;;
    HOLD*) echo "::notice::#$PR: ${COMPLETE#HOLD }"; continue ;;
    *)
      echo "::warning::#$PR: unrecognised completeness result '$COMPLETE'; leaving it alone"
      continue ;;
  esac

  IS_DRAFT="$(printf '%s' "$FACTS" | jq -r '.data.repository.pullRequest.isDraft')"
  MERGEABLE="$(printf '%s' "$FACTS" | jq -r '.data.repository.pullRequest.mergeStateStatus // "" | ascii_downcase')"
  LABELS="$(printf '%s' "$FACTS" | jq -r '.data.repository.pullRequest.labels.nodes[].name')"
  UNRESOLVED="$(printf '%s' "$FACTS" | jq -r '[.data.repository.pullRequest.reviewThreads.nodes[] | select(.isResolved | not)] | length')"

  TRUST="$(printf '%s' "$FACTS" | bash "$here/merge-head-trust.sh")"
  case "$TRUST" in
    "TRUSTED "*) : ;;
    HOLD*) echo "::notice::#$PR: ${TRUST#HOLD }"; continue ;;
    *)
      echo "::warning::#$PR: unrecognised head-trust result '$TRUST'; leaving it alone"
      continue ;;
  esac
  IFS=' ' read -r _ HEAD_OID PASS_AFTER_HEAD HEAD_AGE <<<"$TRUST"

  PATHS="$(printf '%s' "$FACTS" | jq -r '.data.repository.pullRequest.files.nodes[]?.path')"
  CHECKS="$(printf '%s' "$FACTS" | jq -r '
    [ .data.repository.pullRequest.commits.nodes[0].commit.statusCheckRollup.contexts.nodes[]?
      | if .__typename == "CheckRun"
        then (if .status == "COMPLETED" then (.conclusion // "pending") else "pending" end)
        else "status:" + (.state // "pending") end
      | ascii_downcase ] | join(" ")')"

  # Codex findings can arrive as an issue comment, review body or inline thread.
  # Keep the three API shapes in one normalised record stream.
  CODEX_ISSUE="$(gh api --paginate --slurp \
    "repos/$REPO/issues/$PR/comments?per_page=100" 2>/dev/null \
    | jq 'if type=="array" and (.[0]|type)=="array" then add else . end
          | map({kind: "issue", login: .user.login, at: .created_at,
                 bot: (.user.type == "Bot"), thread: null, commit_oid: null,
                 body: (.body // "")})' 2>/dev/null)" || CODEX_ISSUE=""
  CODEX_REVIEW="$(gh api --paginate --slurp \
    "repos/$REPO/pulls/$PR/reviews?per_page=100" 2>/dev/null \
    | jq 'if type=="array" and (.[0]|type)=="array" then add else . end
          | map(select(.body != "" and .body != null)
                | {kind: "review", login: .user.login, at: .submitted_at,
                   bot: (.user.type == "Bot"), thread: null, commit_oid: null,
                   body: (.body // "")})' 2>/dev/null)" || CODEX_REVIEW=""
  CODEX_INLINE="$(gh api --paginate --slurp \
    "repos/$REPO/pulls/$PR/comments?per_page=100" 2>/dev/null \
    | jq 'if type=="array" and (.[0]|type)=="array" then add else . end
          | map({kind: "review-comment", login: .user.login, at: .created_at,
                 bot: (.user.type == "Bot"), thread: (.in_reply_to_id // .id),
                 commit_oid: (.original_commit_id // null),
                 body: (.body // "")})' 2>/dev/null)" || CODEX_INLINE=""

  if [ -z "$CODEX_ISSUE" ] || [ -z "$CODEX_REVIEW" ] || [ -z "$CODEX_INLINE" ]; then
    echo "::notice::#$PR: could not read its comments, leaving it alone"
    continue
  fi
  RECORDS="$(jq -n --argjson a "$CODEX_ISSUE" --argjson b "$CODEX_REVIEW" \
                   --argjson c "$CODEX_INLINE" '$a + $b + $c' 2>/dev/null)" || RECORDS=""

  CODEX=unknown
  if [ -n "$RECORDS" ]; then
    CODEX="$(printf '%s' "$RECORDS" \
             | bash "$here/codex-answered.sh" "$HEAD_OID" 2>/dev/null)" || CODEX=unknown
  fi

  if [ "$CODEX" != "0" ] && [ -n "$RECORDS" ]; then
    PERMS='{}'
    LOOKED=0
    # shellcheck disable=SC2046  # GitHub logins cannot contain whitespace.
    for LOGIN in $(printf '%s' "$RECORDS" \
                   | jq -r '[.[] | select(.bot == false) | .login] | unique | .[]' 2>/dev/null); do
      if [ "$LOOKED" -ge "$MAX_PERMISSION_LOOKUPS" ]; then
        echo "::notice::#$PR: more than $MAX_PERMISSION_LOOKUPS commenters; the rest count as unverified"
        break
      fi
      LOOKED=$((LOOKED + 1))
      PERM="$(gh api "repos/$REPO/collaborators/$LOGIN/permission" \
              --jq .permission 2>/dev/null)" || PERM=""
      case "$PERM" in
        admin|maintain|write|triage|read|none) : ;;
        *) PERM=unknown ;;
      esac
      PERMS="$(jq -n --argjson m "$PERMS" --arg l "$LOGIN" --arg p "$PERM" \
               '$m + {($l): $p}' 2>/dev/null)" || PERMS='{}'
    done

    CODEX=unknown
    RECORDS="$(jq -n --argjson r "$RECORDS" --argjson p "$PERMS" \
      '[ $r[] | . + {permission: ($p[.login] // "unknown")} ]' 2>/dev/null)" || RECORDS=""
    if [ -n "$RECORDS" ]; then
      CODEX="$(printf '%s' "$RECORDS" \
               | bash "$here/codex-answered.sh" "$HEAD_OID" 2>/dev/null)" || CODEX=unknown
    fi
  fi
  case "$CODEX" in
    unknown) : ;;
    ''|*[!0-9]*) CODEX=unknown ;;
  esac

  VERDICT="$(bash "$here/merge-candidate.sh" \
    "$CHECKS" "$LABELS" "$UNRESOLVED" "$CODEX" \
    "$MERGEABLE" "$IS_DRAFT" "$HEAD_AGE" "$PATHS" "$PASS_AFTER_HEAD" \
    "$COMPLETE" "$HEAD_OID")"

  case "$VERDICT" in
    HOLD*) echo "::notice::#$PR: ${VERDICT#HOLD }"; continue ;;
    READY)
      if gh pr ready "$PR" --repo "$REPO"; then
        echo "::notice::#$PR: taken out of draft; the next sweep re-gathers every fact"
      else
        echo "::warning::#$PR: could not take it out of draft"
      fi
      continue ;;
    MERGE) : ;;
    *)
      echo "::warning::#$PR: unrecognised verdict '$VERDICT', leaving it alone"
      continue ;;
  esac

  if gh pr merge "$PR" --repo "$REPO" --squash --delete-branch; then
    MERGED=$((MERGED + 1))
    echo "::notice::#$PR: merged at ${HEAD_OID:0:8}"
  else
    REFUSED=$((REFUSED + 1))
    echo "::warning::#$PR: merge refused by GitHub; nothing changed"
  fi
done

echo "::notice::sweep finished, $MERGED merged"
if [ "$REFUSED" -gt 0 ]; then
  echo "::error::$REFUSED merge(s) were refused by GitHub; check branch protection and squash-merge settings"
  exit 1
fi

