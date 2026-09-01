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

# A DELETE the token may not make and a DELETE that worked answer the same way:
# `gh api` writes its own error and the callers below used to swallow both with
# `|| true`. Confirm the absence instead, and confirm it as a 404 -- a 500 or a
# rate limit says nothing about whether the ref is still there, and reading
# "gone" out of an unknown answer is how a claim gets reported cleared while it
# still holds the queue (#254). Every caller inherits this: release, break, reap.
delete_ref() {
  local err
  gh api --method DELETE "repos/$1/git/refs/tags/attadipa-claims/$2" >/dev/null 2>&1
  err="$(gh api "repos/$1/git/ref/tags/attadipa-claims/$2" 2>&1 >/dev/null)"
  case "$err" in
    *'HTTP 404'*) return 0 ;;
    '') printf '%s still exists after DELETE\n' "$(claim_ref "$2")" >&2 ;;
    *) printf 'cannot confirm %s is gone: %s\n' "$(claim_ref "$2")" \
         "$(printf '%s' "$err" | tr '\n' ' ')" >&2 ;;
  esac
  return 2
}

# Undo this attempt's own create, and only this attempt's. A ref carrying some
# other tag is another writer's claim, and deleting it would hand two writers
# the same task. A ref this attempt cannot remove is named on stderr, because a
# claim nobody can see is exactly the stall #254 reports.
discard_own_ref() {
  local repo="$1" number="$2" tag_sha="$3" live
  live="$(attadipa_claim_tag_sha "$repo" "$number")" || live=""
  [ -z "$live" ] || [ "$live" = "$tag_sha" ] || return 0
  delete_ref "$repo" "$number" && return 0
  printf 'left behind: %s -- clear it with: claim.sh break %s %s\n' \
    "$(claim_ref "$number")" "$repo" "$number" >&2
}

# The holder id is published: claim.sh puts it in the tag message and in the tag
# name, and this repository is public. Refuse anything credential-shaped, and
# refuse it before the first network call -- a tag object outlives the deletion
# of its ref, so not sending the POST is the only protection that holds.
reject_unsafe_holder() {
  case "$1" in
    ghp_* | gho_* | ghu_* | ghs_* | ghr_* | github_pat_*)
      printf 'holder looks like a GitHub credential, not an agent id\n' >&2; return 1 ;;
  esac
  case "$1" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]\
[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]\
[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]\
[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])
      printf 'holder looks like a GitHub credential, not an agent id\n' >&2; return 1 ;;
  esac
  if ! printf '%s' "$1" | grep -Eq '^[A-Za-z0-9._-]{1,64}$'; then
    printf 'holder must match ^[A-Za-z0-9._-]{1,64}$\n' >&2; return 1
  fi
}

acquire() {
  local repo="$1" number="$2" holder="$3" branch head tag_json tag_sha winner existing now
  reject_unsafe_holder "$holder" || {
    printf 'pass an agent id such as agent-<run>-<attempt>; see AGENTS.md\n' >&2
    return 64
  }
  if existing="$(attadipa_claim_owner "$repo" "$number")"; then
    printf 'held by %s\n' "$existing" >&2
    return 3
  fi

  branch="$(gh api "repos/$repo" | jq -er '.default_branch')" || return 2
  head="$(gh api "repos/$repo/git/ref/heads/$branch" | jq -er '.object.sha')" || return 2
  now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  tag_json="$(gh api --method POST "repos/$repo/git/tags" \
    -f "tag=attadipa-claim-$number-${holder//[^A-Za-z0-9._-]/-}" \
    -f "message=$holder" -f "object=$head" -f type=commit \
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
  if [ "$winner" != "$holder" ]; then
    printf 'claim verification failed\n' >&2
    discard_own_ref "$repo" "$number" "$tag_sha"
    return 2
  fi
  if [ "$number" != writer ] && ! edit_label "$repo" "$number" --add-label agent:working; then
    discard_own_ref "$repo" "$number" "$tag_sha"
    return 2
  fi
  printf 'acquired by %s\n' "$holder"
}

release() {
  local repo="$1" number="$2" holder="$3" winner
  winner="$(attadipa_claim_owner "$repo" "$number")" || return 3
  if [ "$winner" != "$holder" ]; then
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
  printf 'released by %s\n' "$holder"
}

break_claim() {
  local repo="$1" number="$2"
  # The ref first, the label second. The label is only the visible half; taking
  # it off while the lock survives is what leaves a queue held by nobody -- the
  # ref refuses every acquire and no issue says who holds it.
  if ! delete_ref "$repo" "$number"; then
    printf 'claim NOT cleared; %s still holds it\n' "$(claim_ref "$number")" >&2
    return 2
  fi
  [ "$number" = writer ] || edit_label "$repo" "$number" --remove-label agent:working || true
  printf 'claim cleared\n'
}

# The only completion evidence this repository has is the Actions run named by a
# hosted holder id, `agent-<run_id>-<attempt>` (claude-agent.yml, claude-ci-repair.yml).
# A local holder -- `agent-i254-r1`, a person at a terminal -- has none: the
# process runs on a machine Actions cannot see, and the tag's creation time says
# only when the work started, never whether it stopped. So age alone reaps
# nothing now; a local lease is released by `writer-start.sh finish`, or by hand
# with `claim.sh break` (#254).
holder_finished() {
  local repo="$1" holder="$2" run status
  if ! printf '%s' "$holder" | grep -Eq '^agent-[0-9]+-[0-9]+$'; then
    printf 'holder %s is not a hosted run; nothing proves it ended\n' "$holder" >&2
    return 1
  fi
  run="${holder#agent-}"; run="${run%-*}"
  # A local holder can still be shaped like a hosted one. A run that cannot be
  # read is not a run that finished, so it falls into the same bucket.
  status="$(gh api "repos/$repo/actions/runs/$run" 2>/dev/null | jq -er '.status' 2>/dev/null)" || {
    printf 'no readable Actions run %s behind holder %s\n' "$run" "$holder" >&2
    return 1
  }
  [ "$status" = completed ] || {
    printf 'Actions run %s is %s\n' "$run" "$status" >&2
    return 1
  }
}

reap() {
  local repo="$1" number="$2" max_age="$3" tag_sha tag_json holder date claimed_epoch now
  if tag_sha="$(attadipa_claim_tag_sha "$repo" "$number")"; then
    tag_json="$(gh api "repos/$repo/git/tags/$tag_sha")" || return 2
    date="$(printf '%s' "$tag_json" | jq -er '.tagger.date')" || return 2
    holder="$(printf '%s' "$tag_json" | jq -er '.message')" || return 2
    if ! holder_finished "$repo" "$holder"; then
      printf '%s is held by %s and stays held; release it by hand with: claim.sh break %s %s\n' \
        "$(claim_ref "$number")" "$holder" "$repo" "$number" >&2
      return 3
    fi
  else
    # Migration path for labels created before repository refs became the lock.
    # Age still decides here, and may: every claim a live writer holds, hosted
    # or local, creates the ref this branch did not find.
    date="$(gh api "repos/$repo/issues/$number/timeline?per_page=100" --paginate --slurp \
      | jq -er 'if (length > 0 and (.[0] | type) == "array") then add else . end
                  | [.[] | select(.event == "labeled" and .label.name == "agent:working")]
                  | last.created_at')" || return 3
  fi
  claimed_epoch="$(date -u -d "$date" +%s 2>/dev/null)" || return 2
  now="$(date -u +%s)"
  [ $((now - claimed_epoch)) -ge "$max_age" ] || return 3
  # A break that could not clear the ref is not a reap. Saying so is the point:
  # the caller must not go on to hand the task back to the queue behind a lock
  # that is still there.
  break_claim "$repo" "$number" || return 2
  if [ "$number" != writer ] && [ "$(target_kind "$repo" "$number")" = issue ]; then
    edit_label "$repo" "$number" --add-label agent:ready
  fi
}

case "${1-}" in
  acquire) [ "$#" -eq 4 ] || { echo 'usage: claim.sh acquire REPO NUMBER AGENT_ID' >&2; exit 64; }; acquire "$2" "$3" "$4" ;;
  release) [ "$#" -eq 4 ] || { echo 'usage: claim.sh release REPO NUMBER AGENT_ID' >&2; exit 64; }; release "$2" "$3" "$4" ;;
  reap) [ "$#" -eq 4 ] || { echo 'usage: claim.sh reap REPO NUMBER SECONDS' >&2; exit 64; }; reap "$2" "$3" "$4" ;;
  break) [ "$#" -eq 3 ] || { echo 'usage: claim.sh break REPO NUMBER' >&2; exit 64; }; break_claim "$2" "$3" ;;
  owner) [ "$#" -eq 3 ] || { echo 'usage: claim.sh owner REPO NUMBER' >&2; exit 64; }; attadipa_claim_owner "$2" "$3" ;;
  *) echo 'usage: claim.sh {acquire|release|reap|break|owner} ...' >&2; exit 64 ;;
esac
