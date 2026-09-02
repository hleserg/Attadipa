#!/usr/bin/env bash
# Repository-wide compare-and-set claim for one issue/PR writer.
set -uo pipefail

claim_ref() { printf 'refs/tags/attadipa-claims/%s' "$1"; }

# Three answers, not two. 0: the tag sha, on stdout. 1: there is no such ref
# (GitHub said 404). 2: the answer is unknown -- a 5xx, a rate limit, a body
# jq cannot read. Only the first two are knowledge. Collapsing the third into
# the second is how a lease that was legitimately won got deleted by its own
# winner: the read-back of a ref created milliseconds earlier lags, and the
# caller undid a claim that was there (#392).
attadipa_claim_tag_sha() {
  local err_file body rc err
  err_file="$(mktemp)" || return 2
  body="$(gh api "repos/$1/git/ref/tags/attadipa-claims/$2" 2>"$err_file")"
  rc=$?
  err="$(cat "$err_file")"
  rm -f "$err_file"
  if [ "$rc" -eq 0 ]; then
    printf '%s' "$body" | jq -er '.object.sha' 2>/dev/null && return 0
    return 2
  fi
  case "$err" in
    *'HTTP 404'*) return 1 ;;
    *) return 2 ;;
  esac
}

# Same three answers. The tag object behind a readable ref is as new as the
# ref, so a failure to read it is "unknown", never "no claim".
attadipa_claim_message() {
  local tag_sha body
  tag_sha="$(attadipa_claim_tag_sha "$1" "$2")" || return $?
  body="$(gh api "repos/$1/git/tags/$tag_sha" 2>/dev/null)" || return 2
  printf '%s' "$body" | jq -er '.message' 2>/dev/null || return 2
}

# The holder is the first line of the tag message; the lines under it are this
# claim's provenance (see claim_message). A claim written before provenance
# existed is one line and nothing else, and reads back the same way here.
attadipa_claim_owner() {
  local message
  message="$(attadipa_claim_message "$1" "$2")" || return $?
  printf '%s\n' "${message%%$'\n'*}"
}

# One value or nothing. Two `kind=` lines would answer with both and match no
# case below, which is the direction a claim of unclear provenance should fail.
claim_field() { printf '%s\n' "$1" | sed -n "s/^$2=//p"; }

# WHAT KIND OF CLAIM THIS IS, RECORDED WHERE IT HAPPENS, ONCE.
#
# The holder id is a label the caller picks and AGENTS.md publishes an example
# of. Reading provenance back out of that string is reading the caller's
# preference as evidence: a person at a terminal who copied `agent-<run>-<attempt>`
# lands on some finished run's number, the watchdog calls their live lease a
# dead hosted one and a second writer follows it in (#369). The same guess fails
# the other way -- claude-ci-repair.yml claims as `ci-repair-<run>-<attempt>`,
# which no `agent-` regex ever matched, so a genuinely dead repair run was never
# recoverable at all.
#
# So the tag records it instead, at acquire, from the only context that is not a
# guess: `kind=hosted` and the run behind it are written when and only when the
# acquire itself ran on the workflow path. Everything else is `kind=local`, and
# local is never reaped by the clock (#254).
claim_message() {
  printf '%s\n' "$1"
  if [ "${GITHUB_ACTIONS-}" = true ] && printf '%s' "${GITHUB_RUN_ID-}" | grep -Eq '^[0-9]+$'; then
    printf 'kind=hosted\nrun=%s\nattempt=%s\n' "$GITHUB_RUN_ID" "${GITHUB_RUN_ATTEMPT:-1}"
  else
    printf 'kind=local\n'
  fi
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
  local err attempt
  gh api --method DELETE "repos/$1/git/refs/tags/attadipa-claims/$2" >/dev/null 2>&1
  # The confirmation reads a ref deleted milliseconds ago, and GitHub's reads
  # lag its writes: a replica still serving it answers "exists" for a DELETE
  # that worked. Look again before saying so, because release re-adds the label
  # and reap abandons its sweep on that answer (#392, round 2).
  for attempt in 1 2 3; do
    err="$(gh api "repos/$1/git/ref/tags/attadipa-claims/$2" 2>&1 >/dev/null)"
    case "$err" in *'HTTP 404'*) return 0 ;; esac
    [ "$attempt" -eq 3 ] || sleep 2
  done
  case "$err" in
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
  local repo="$1" number="$2" tag_sha="$3" live rc attempt
  # Delete only what is positively this attempt's ref. Both callers run after
  # the create returned 201, so the ref IS there: a 404 seconds later is the
  # read lagging the write, so look again, and three of them are still not
  # proof of absence -- they are an answer that never settled, and the ref is
  # left where it is and named, like any other unreadable answer. Deleting on
  # "unknown" is the bug this file had (#392); returning quietly on "not
  # found" would be its silent twin, a lock with no holder and no label.
  for attempt in 1 2 3; do
    live="$(attadipa_claim_tag_sha "$repo" "$number")"
    rc=$?
    if [ "$rc" -ne 1 ] || [ "$attempt" -eq 3 ]; then break; fi
    sleep 2
  done
  if [ "$rc" -eq 0 ] && [ "$live" = "$tag_sha" ] && delete_ref "$repo" "$number"; then
    return 0
  fi
  # Every other answer leaves the ref where it is and names it -- including a
  # read that names some other tag. After a 201 the ref is this attempt's, so
  # that read is the same lagging replica as a 404, or a writer that took the
  # ref since; either way it is not ours to delete unseen, and a lock nobody
  # names is the invisible one. The operator confirms before breaking.
  printf 'left behind: %s -- clear it with: claim.sh break %s %s\n' \
    "$(claim_ref "$number")" "$repo" "$number" >&2
  [ "$rc" -ne 0 ] || [ "$live" = "$tag_sha" ] || printf \
    '  (it read back as tag %s, not this attempt'"'"'s %s: a replica behind the create, or a later writer -- check claim.sh owner %s %s first)\n' \
    "$live" "$tag_sha" "$repo" "$number" >&2
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
  local repo="$1" number="$2" holder="$3" branch head tag_json tag_sha winner existing now attempt
  reject_unsafe_holder "$holder" || {
    printf 'pass an agent id such as local-<who>-<n>; see AGENTS.md\n' >&2
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
    -f "message=$(claim_message "$holder")" -f "object=$head" -f type=commit \
    -f 'tagger[name]=Attadipa automation' -f 'tagger[email]=actions@users.noreply.github.com' \
    -f "tagger[date]=$now")" || return 2
  tag_sha="$(printf '%s' "$tag_json" | jq -er '.sha')" || return 2

  if ! attadipa_claim_create_ref "$repo" "$number" "$tag_sha"; then
    # The ref that refused this create is the winner's, milliseconds old, and
    # a read of it lags the same way the read-back below does. Held is exit 3
    # and every caller exits clean on it; unknown is 2 and goes red (#392).
    for attempt in 1 2 3; do
      if existing="$(attadipa_claim_owner "$repo" "$number")" && [ -n "$existing" ]; then
        printf 'held by %s\n' "$existing" >&2
        return 3
      fi
      [ "$attempt" -eq 3 ] || sleep 2
    done
    printf 'claim creation failed and ownership is unknown\n' >&2
    return 2
  fi

  # The create above is the compare-and-set: GitHub refuses a ref that exists,
  # so its success is the claim won. The read-back confirms it and can only
  # add one fact -- that another holder is there after all. It cannot un-win
  # the claim by failing: the ref is milliseconds old and GitHub's reads lag
  # its writes, so an unreadable answer is retried and, if it never settles,
  # reported and trusted (#392). A read naming somebody else is retried the
  # same way: the writer lease is released and re-acquired seconds apart, and
  # a replica behind that DELETE still serves the previous holder's tag. Only
  # a different holder that survives every read is a claim to undo.
  local read_rc
  attempt=1
  while :; do
    winner="$(attadipa_claim_owner "$repo" "$number")"
    read_rc=$?
    if { [ "$read_rc" -eq 0 ] && [ "$winner" = "$holder" ]; } || [ "$attempt" -eq 4 ]; then break; fi
    attempt=$((attempt + 1))
    sleep 2
  done
  if [ "$read_rc" -ne 0 ]; then
    printf 'claim created but not readable back after %s attempts; trusting the create -- confirm with: claim.sh owner %s %s\n' \
      "$attempt" "$repo" "$number" >&2
  elif [ "$winner" != "$holder" ]; then
    printf 'claim verification failed: held by %s\n' "$winner" >&2
    discard_own_ref "$repo" "$number" "$tag_sha"
    return 2
  elif [ "$attempt" -gt 1 ]; then
    printf 'claim read back after %s attempts\n' "$attempt" >&2
  fi
  if [ "$number" != writer ] && ! edit_label "$repo" "$number" --add-label agent:working; then
    discard_own_ref "$repo" "$number" "$tag_sha"
    return 2
  fi
  printf 'acquired by %s\n' "$holder"
}

release() {
  local repo="$1" number="$2" holder="$3" winner rc attempt
  # The read is retried as acquire's is: a release seconds after the create
  # meets the same lag, and a replica behind the previous DELETE serves the
  # previous holder's tag, so a read naming somebody else is retried too. An
  # answer that never settles is said out loud, because every caller writes
  # `|| true` -- this line is the whole signal that a lease trusted at its
  # create is still there (#392). A settled 404 says only what it knows: no
  # readable claim, nothing deleted -- acquire may have trusted a create this
  # read has not caught up with yet.
  for attempt in 1 2 3; do
    winner="$(attadipa_claim_owner "$repo" "$number")"
    rc=$?
    if { [ "$rc" -eq 0 ] && [ "$winner" = "$holder" ]; } || [ "$attempt" -eq 3 ]; then break; fi
    sleep 2
  done
  case "$rc" in
    0) ;;
    1) printf 'no readable claim at %s after %s attempts; nothing deleted -- confirm with: claim.sh owner %s %s\n' \
         "$(claim_ref "$number")" "$attempt" "$repo" "$number" >&2
       return 3 ;;
    *) printf 'cannot read who holds %s; left behind -- clear it with: claim.sh break %s %s\n' \
         "$(claim_ref "$number")" "$repo" "$number" >&2
       return 3 ;;
  esac
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

# The only completion evidence this repository has is the Actions run this claim
# recorded when it was acquired. A local claim -- a person at a terminal -- has
# none: the process runs on a machine Actions cannot see, and the tag's creation
# time says only when the work started, never whether it stopped. So age alone
# reaps nothing; a local lease is released by `writer-start.sh finish`, or by
# hand with `claim.sh break` (#254).
#
# Read the recorded `kind`, never the holder's spelling (#369). A claim from
# before provenance was written carries neither, and unattributed is not
# finished: it holds until someone breaks it, which is the safe direction.
claim_finished() {
  local repo="$1" message="$2" holder kind run status
  holder="${message%%$'\n'*}"
  kind="$(claim_field "$message" kind)"
  case "$kind" in
    hosted) ;;
    local)
      printf 'claim by %s was made off Actions; nothing proves it ended\n' "$holder" >&2
      return 1 ;;
    *)
      printf 'claim by %s records no provenance; it cannot be read as a finished run\n' "$holder" >&2
      return 1 ;;
  esac
  run="$(claim_field "$message" run)"
  if ! printf '%s' "$run" | grep -Eq '^[0-9]+$'; then
    printf 'hosted claim by %s names no Actions run\n' "$holder" >&2
    return 1
  fi
  # A run that cannot be read is not a run that finished, so it falls into the
  # same bucket as one that is still going.
  status="$(gh api "repos/$repo/actions/runs/$run" 2>/dev/null | jq -er '.status' 2>/dev/null)" || {
    printf 'no readable Actions run %s behind the claim by %s\n' "$run" "$holder" >&2
    return 1
  }
  [ "$status" = completed ] || {
    printf 'Actions run %s is %s\n' "$run" "$status" >&2
    return 1
  }
}

reap() {
  local repo="$1" number="$2" max_age="$3" tag_sha tag_json message holder date claimed_epoch now rc
  tag_sha="$(attadipa_claim_tag_sha "$repo" "$number")"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    tag_json="$(gh api "repos/$repo/git/tags/$tag_sha")" || return 2
    date="$(printf '%s' "$tag_json" | jq -er '.tagger.date')" || return 2
    message="$(printf '%s' "$tag_json" | jq -er '.message')" || return 2
    holder="${message%%$'\n'*}"
    if ! claim_finished "$repo" "$message"; then
      printf '%s is held by %s and stays held; release it by hand with: claim.sh break %s %s\n' \
        "$(claim_ref "$number")" "$holder" "$repo" "$number" >&2
      return 3
    fi
  elif [ "$rc" -eq 1 ]; then
    # Migration path for labels created before repository refs became the lock.
    # Age still decides here, and may: every claim a live writer holds, hosted
    # or local, creates the ref this branch did not find.
    date="$(gh api "repos/$repo/issues/$number/timeline?per_page=100" --paginate --slurp \
      | jq -er 'if (length > 0 and (.[0] | type) == "array") then add else . end
                  | [.[] | select(.event == "labeled" and .label.name == "agent:working")]
                  | last.created_at')" || return 3
  else
    # "Could not read" is not "not there". The path above reaps by age and
    # `break_claim` deletes before it confirms, so an unreadable ref has to stop
    # here or a live local writer's lock goes with it (#392).
    printf '%s could not be read; nothing is reaped behind that answer\n' "$(claim_ref "$number")" >&2
    return 3
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
