#!/usr/bin/env bash
# Repository-wide compare-and-set claim for one issue/PR writer.
set -uo pipefail

claim_ref() { printf 'refs/tags/attadipa-claims/%s' "$1"; }

attadipa_claim_tag_sha() {
  gh api "repos/$1/git/ref/tags/attadipa-claims/$2" 2>/dev/null |
    jq -er '.object.sha' 2>/dev/null
}

attadipa_claim_owner() {
  local tag_sha
  tag_sha="$(attadipa_claim_tag_sha "$1" "$2")" || return 1
  gh api "repos/$1/git/tags/$tag_sha" 2>/dev/null | jq -er '.message' 2>/dev/null
}

attadipa_claim_create_ref() {
  gh api --method POST "repos/$1/git/refs" -f "ref=$(claim_ref "$2")" -f "sha=$3" >/dev/null
}

target_kind() {
  local target
  target="$(gh api "repos/$1/issues/$2")" || return 1
  printf '%s' "$target" | jq -er 'if .pull_request != null then "pr" else "issue" end'
}

edit_label() {
  local kind
  kind="$(target_kind "$1" "$2")" || return 1
  gh "$kind" edit "$2" --repo "$1" "$3" "$4" >/dev/null
}

delete_ref() {
  gh api --method DELETE "repos/$1/git/refs/tags/attadipa-claims/$2" >/dev/null 2>&1
}

acquire() {
  local repo="$1" number="$2" token="$3" branch head tag_json tag_sha winner existing now
  if existing="$(attadipa_claim_owner "$repo" "$number")"; then
    printf 'held by %s\n' "$existing" >&2
    return 3
  fi

  branch="$(gh api "repos/$repo" | jq -er '.default_branch')" || return 2
  head="$(gh api "repos/$repo/git/ref/heads/$branch" | jq -er '.object.sha')" || return 2
  now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  tag_json="$(gh api --method POST "repos/$repo/git/tags" \
    -f "tag=attadipa-claim-$number-${token//[^A-Za-z0-9._-]/-}" \
    -f "message=$token" -f "object=$head" -f type=commit \
    -f 'tagger[name]=Attadipa automation' -f 'tagger[email]=actions@users.noreply.github.com' \
    -f "tagger[date]=$now")" || return 2
  tag_sha="$(printf '%s' "$tag_json" | jq -er '.sha')" || return 2

  if ! attadipa_claim_create_ref "$repo" "$number" "$tag_sha"; then
    existing="$(attadipa_claim_owner "$repo" "$number")"
    if [ -n "$existing" ]; then
      printf 'held by %s\n' "$existing" >&2
      return 3
    fi
    printf 'claim creation failed and ownership is unknown\n' >&2
    return 2
  fi

  winner="$(attadipa_claim_owner "$repo" "$number")"
  if [ "$winner" != "$token" ]; then
    printf 'claim verification failed\n' >&2
    return 2
  fi
  if [ "$number" != writer ] && ! edit_label "$repo" "$number" --add-label agent:working; then
    delete_ref "$repo" "$number" || true
    return 2
  fi
  printf 'acquired by %s\n' "$token"
}

release() {
  local repo="$1" number="$2" token="$3" winner
  winner="$(attadipa_claim_owner "$repo" "$number")" || return 3
  if [ "$winner" != "$token" ]; then
    printf 'held by %s\n' "$winner" >&2
    return 3
  fi
  if [ "$number" != writer ]; then
    edit_label "$repo" "$number" --remove-label agent:working || return 2
  fi
  if ! delete_ref "$repo" "$number"; then
    [ "$number" = writer ] || edit_label "$repo" "$number" --add-label agent:working || true
    return 2
  fi
  printf 'released by %s\n' "$token"
}

break_claim() {
  local repo="$1" number="$2"
  [ "$number" = writer ] || edit_label "$repo" "$number" --remove-label agent:working || true
  delete_ref "$repo" "$number" || true
  printf 'claim cleared\n'
}

reap() {
  local repo="$1" number="$2" max_age="$3" tag_sha date then now
  if tag_sha="$(attadipa_claim_tag_sha "$repo" "$number")"; then
    date="$(gh api "repos/$repo/git/tags/$tag_sha" | jq -er '.tagger.date')" || return 2
  else
    # Migration path for labels created before repository refs became the lock.
    date="$(gh api "repos/$repo/issues/$number/timeline?per_page=100" --paginate --slurp \
      | jq -er 'if (length > 0 and (.[0] | type) == "array") then add else . end
                  | [.[] | select(.event == "labeled" and .label.name == "agent:working")]
                  | last.created_at')" || return 3
  fi
  then="$(date -u -d "$date" +%s 2>/dev/null)" || return 2
  now="$(date -u +%s)"
  [ $((now - then)) -ge "$max_age" ] || return 3
  break_claim "$repo" "$number"
  if [ "$number" != writer ] && [ "$(target_kind "$repo" "$number")" = issue ]; then
    edit_label "$repo" "$number" --add-label agent:ready
  fi
}

case "${1-}" in
  acquire) [ "$#" -eq 4 ] || { echo 'usage: claim.sh acquire REPO NUMBER TOKEN' >&2; exit 64; }; acquire "$2" "$3" "$4" ;;
  release) [ "$#" -eq 4 ] || { echo 'usage: claim.sh release REPO NUMBER TOKEN' >&2; exit 64; }; release "$2" "$3" "$4" ;;
  reap) [ "$#" -eq 4 ] || { echo 'usage: claim.sh reap REPO NUMBER SECONDS' >&2; exit 64; }; reap "$2" "$3" "$4" ;;
  break) [ "$#" -eq 3 ] || { echo 'usage: claim.sh break REPO NUMBER' >&2; exit 64; }; break_claim "$2" "$3" ;;
  owner) [ "$#" -eq 3 ] || { echo 'usage: claim.sh owner REPO NUMBER' >&2; exit 64; }; attadipa_claim_owner "$2" "$3" ;;
  *) echo 'usage: claim.sh {acquire|release|reap|break|owner} ...' >&2; exit 64 ;;
esac
