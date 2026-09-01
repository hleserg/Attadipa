#!/usr/bin/env bash
# Every third-party action runs at a commit, not at a name somebody else can move.
#
# `anthropics/claude-code-action@v1` resolves fresh on every run, and the code it
# resolves to receives the Anthropic credential, a GitHub token context, OIDC and
# repository write permissions. A tag moved upstream -- routinely, mistakenly, or
# after a compromise -- changes what executes here with no change to any workflow,
# pull request, review or required check. No upstream compromise is claimed; the
# execution path is the finding.
#
# This is a static scan and it is honest about being one: it proves nothing about
# what is AT those commits. What it proves is that the set of things that can
# change under us without a commit here is empty.
#
# `^\s*uses:` does NOT match `- uses:`, and an inventory built with that pattern
# reported 9 refs where there were 32. The pattern below is dash-aware, and the
# count assertion is here so a future regex bug shows up as a red test rather
# than as a smaller number nobody questions.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1

pass=0
fail=0
ok() { printf '  ok    %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL  %s\n     %s\n' "$1" "$2"; fail=$((fail + 1)); }

# A COMMENT IS NOT A CONDITION, and this pattern could not tell them apart. It
# is not anchored -- it cannot be, because `- uses:` is the common form -- so a
# comment containing the word `uses:` produced a phantom reference and reported
# it as unpinned. That happened the moment a comment in ci.yml explained why the
# container is pinned "for the same reason every `uses:` here is". The scan reads
# the workflows with full-line comments removed; trailing `# vX` provenance is
# already excluded by the character class.
workflow_body() { grep -rh '' "$@" | sed 's/^[[:space:]]*#.*//'; }
refs=$(workflow_body .github/workflows/ | grep -oP 'uses:\s*\K[^\s#]+' | sort)
total=$(printf '%s\n' "$refs" | grep -c .)

if [ "$total" -eq 0 ]; then
  no "the scan finds any action references at all" \
     "no 'uses:' found under .github/workflows -- the pattern or the layout changed, and this test is asserting nothing"
  printf '\n%d passed, %d failed\n' "$pass" "$fail"
  exit 1
fi
ok "the scan finds action references to check ($total)"

# A local action (./.github/actions/...) and a reusable workflow in this
# repository are not third-party and are not pinned by SHA.
external=$(printf '%s\n' "$refs" | grep -v '^\./' || true)

unpinned=$(printf '%s\n' "$external" | grep -vP '@[0-9a-f]{40}$' || true)
if [ -n "$unpinned" ]; then
  no "every third-party action is pinned to a 40-hex commit" \
     "$(printf '%s' "$unpinned" | tr '\n' ' ')"
else
  ok "every third-party action is pinned to a 40-hex commit"
fi

# A pin nobody can read is a pin nobody will bump. Each pinned line carries the
# human version it was resolved from, as a trailing comment.
undocumented=$(grep -rhnP 'uses:\s*[^\s@#]+@[0-9a-f]{40}\s*$' .github/workflows/ || true)
if [ -n "$undocumented" ]; then
  no "every pin says which version it is" \
     "no trailing '# vX' comment on: $(printf '%s' "$undocumented" | tr '\n' ' ')"
else
  ok "every pin says which version it is"
fi

# THE TRAP THIS REPOSITORY HAS FALLEN INTO TWICE. `git/ref/tags/<tag>` returns
# the object the ref points at, and for an ANNOTATED tag that object is a tag,
# not a commit -- its SHA is not a commit SHA and pinning to it is pinning to
# nothing GitHub will check out. Two of the six actions here are annotated
# (anthropics/claude-code-action, github/codeql-action), and DEPENDENCIES.md
# recorded ESP-IDF's tag object as its commit for the same reason.
#
# A tag object SHA cannot be told from a commit SHA by looking, so this asserts
# the fact rather than the shape: each pinned SHA must exist as a COMMIT in the
# action's repository. Skipped without network or credentials, and skipping is
# reported rather than counted as a pass.
if [ "${ATTADIPA_PIN_CHECK_NETWORK:-}" = "1" ] && command -v gh >/dev/null 2>&1; then
  while IFS= read -r ref; do
    [ -n "$ref" ] || continue
    path=${ref%@*}; sha=${ref##*@}
    repo=$(printf '%s' "$path" | cut -d/ -f1,2)
    if gh api "repos/$repo/commits/$sha" --jq '.sha' >/dev/null 2>&1; then
      ok "$repo@${sha:0:8} is a commit in that repository"
    else
      no "$repo@${sha:0:8} is a commit in that repository" \
         "GitHub does not resolve it as a commit -- an annotated tag object SHA looks identical and is not one"
    fi
  done < <(printf '%s\n' "$external" | sort -u)
else
  printf '  skip  each pin resolves to a real commit (set ATTADIPA_PIN_CHECK_NETWORK=1 with gh authenticated)\n'
fi

# A CONTAINER IS AN ACTION BY ANOTHER NAME, and this test did not look at one.
# `container: espressif/idf:v5.5.5` is an image whose owner can move the tag, and
# the job it runs builds the firmware that ships -- the same execution path the
# scan above exists for, reached through a different key. Acceptance item 4 of
# #294 names it explicitly; the first version of this suite met the other five
# and left this one, because `uses:` was the whole pattern.
#
# The trailing `# vX` is provenance and deliberately NOT the anchor: the digest
# is, exactly as with an action pin.
#
# One function, so the regression cases at the bottom of this file go through
# the parser that scans the shipping workflows rather than a second copy of the
# same regex written to agree with it.
container_refs() { workflow_body "$@" | grep -oP '^\s*container:\s*\K[^\s#]+'; }
container_keys() { workflow_body "$@" | grep -cP '^\s*container:' || true; }

# AN EMPTY SCAN IS NOT THE SAME ANSWER AS "NOTHING TO CHECK", and the pattern
# above cannot tell them apart on its own. `container:` also takes a MAP --
# `container:` on one line with `image:` indented under it -- and against that
# form this grep captures nothing after the colon, the list comes back empty,
# and the arm below reports that a workflow with a container in it has none.
# The same vacuous-pass shape that made the deny-list pairing rule in
# bot-actor-test.sh silently stop asserting. So the parsed count is checked
# against the raw count of `container:` keys, and a disagreement is the failure.
#
# TWO JOBS ON ONE IMAGE IS NOT A PARSE FAILURE, and counting the deduplicated
# list said it was. `sort -u` answers a different question -- which distinct
# images to check a digest on -- and when its output fed this count, a second
# job on the same correctly pinned `espressif/idf@sha256:...` gave
# `declared=2, parsed=1` and failed a workflow with nothing wrong in it. The
# completeness check counts OCCURRENCES; deduplication happens after it, for
# the digest check, where repeating an identical ref proves nothing.
#
# ONE FUNCTION FOR THE THREE ASSIGNMENTS, because the defect was in neither
# regex: it was the ORDER of these statements, with the deduplicated list
# feeding the completeness count. A regression case that calls the two helpers
# and writes the comparison out a second time exercises the parser and leaves
# that order untested -- put `sort -u` back on `found` here and such a case
# stays green while the workflow it stands for goes red. The cases at the
# bottom of this file therefore run THIS. It sets three globals, exactly as
# the shipping scan does, so after a fixture call they describe the FIXTURE:
# anything appended below scans again before it asserts.
container_scan() {
  local found
  found=$(container_refs "$@")
  declared=$(container_keys "$@")
  parsed=$(printf '%s\n' "$found" | grep -c . || true)
  containers=$(printf '%s\n' "$found" | sort -u)
}

container_scan .github/workflows/
if [ "$declared" -ne "$parsed" ]; then
  no "the container scan sees every container key" \
     "$declared 'container:' keys, $parsed parsed -- the map form (container:/image:) parses to nothing and would report 'no container image runs'"
elif [ -z "$containers" ]; then
  ok "no container image runs in a workflow"
else
  ok "the scan finds container images to check ($(printf '%s\n' "$containers" | grep -c .))"
  loose=$(printf '%s\n' "$containers" | grep -vP '@sha256:[0-9a-f]{64}$' || true)
  if [ -n "$loose" ]; then
    no "every container image is pinned to a digest" \
       "movable image reference: $(printf '%s' "$loose" | tr '\n' ' ')"
  else
    ok "every container image is pinned to a digest"
  fi

  # The digest binds the bytes and says nothing about what is in them. The job
  # itself asserts the ESP-IDF commit against DEPENDENCIES.md at run time; this
  # only checks that the assertion is still there to run, because deleting it is
  # invisible -- the build keeps passing on whatever the image happens to be.
  if grep -q 'documented=b774170ff46c393eeb5e495ea37936038d3f4f4f' .github/workflows/ci.yml; then
    ok "the firmware job still checks the image against the documented commit"
  else
    no "the firmware job still checks the image against the documented commit" \
       "no commit assertion found in ci.yml -- a digest proves the image is unchanged, not that it was ever right"
  fi
fi

# THE PARSER, AGAINST THE TWO SHAPES THE REAL TREE DOES NOT CURRENTLY HAVE.
# Both go through `container_refs`/`container_keys` above, so a change to the
# regex that breaks either is a red test rather than a silent one.
fixture=$(mktemp -d) || exit 1
trap 'rm -rf "$fixture"' EXIT

# Two jobs, one correctly pinned image -- production and HIL on the same
# toolchain. Nothing is wrong here and the count must say so.
cat >"$fixture/duplicate.yml" <<'YAML'
jobs:
  firmware:
    container: espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2  # v5.5.5
  hil:
    container: espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2  # v5.5.5
YAML
container_scan "$fixture"
unique=$(printf '%s\n' "$containers" | grep -c .)
# Two keys, two parsed, ONE image to check a digest on. The third conjunct is
# the invariant #364 is actually about, and no assertion in this file stated it
# before: deduplication belongs to the digest check and not to the count.
if [ "$declared" -eq 2 ] && [ "$parsed" -eq 2 ] && [ "$unique" -eq 1 ]; then
  ok "two jobs on one pinned image are both parsed"
else
  no "two jobs on one pinned image are both parsed" \
     "$declared 'container:' keys, $parsed parsed, $unique distinct -- a correct workflow is being rejected"
fi

# The map form, which this parser does NOT support. It must stay a disagreement
# between the two counts -- the arm above turns that into a red test -- and not
# become an empty list reported as "no container image runs".
cat >"$fixture/map-form.yml" <<'YAML'
jobs:
  firmware:
    container:
      image: espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2
YAML
rm "$fixture/duplicate.yml"
container_scan "$fixture"
# The exact counts, not merely that they disagree: "disagree" also holds when
# the fixture is empty for an unrelated reason -- if a third case added later
# leaves a file behind, or the `rm` above stops matching.
if [ "$declared" -eq 1 ] && [ "$parsed" -eq 0 ]; then
  ok "the map form is a parse failure, not a vacuous pass"
else
  no "the map form is a parse failure, not a vacuous pass" \
     "$declared 'container:' keys, $parsed parsed -- container:/image: parsed to nothing and the counts still agreed"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
