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
containers=$(workflow_body .github/workflows/ | grep -oP '^\s*container:\s*\K[^\s#]+' | sort -u)
if [ -z "$containers" ]; then
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

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
