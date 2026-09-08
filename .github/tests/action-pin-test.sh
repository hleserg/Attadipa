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
workflow_refs() { workflow_body "$@" | grep -oP 'uses:\s*\K[^\s#]+' | sort; }
refs=$(workflow_refs .github/workflows/)
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

# A PIN OF THE RIGHT SHAPE POINTING AT A COMMIT NOBODY WROTE DOWN.
#
# Everything above reads the workflows and nothing else, and that is exactly how
# far it got. Dependabot's #464 (merged 2026-09-07) moved all three
# `anthropics/claude-code-action` invocations from `a60f3e1…` to `d75b94d…` and
# changed no other file, so `docs/research/DEPENDENCIES.md` -- the canonical
# record of which third-party code receives the Anthropic credential, a GitHub
# token context and repository write permissions -- named a commit that no job
# executed, and every check in this repository stayed green over it. The pin was
# immutable the whole time; the inventory describing it was fiction. That is not
# the P1 exposure back, it is the thing an incident response, a licence review or
# the next bump would start from. A person reading the diff caught it inside a
# day. Nothing here would have caught it at all, which is the part that scales.
#
# So the two are compared, in BOTH directions. A row for an action the tree does
# not use is as much a lie as an action the tree uses and no row names, and only
# the second direction makes an empty parse fail: if the table's shape drifts and
# nothing parses, every ref in the tree becomes uncovered and this goes red
# rather than quietly asserting nothing over zero rows.
#
# The row shape this reads:
#
#   | **`owner/action`** | `<40-hex>`, <date> | ... |
#
# Column 2 opening with a backticked 40-hex commit is what makes a row an
# inventory row. That is not enough on its own: the `Decided` table above uses
# the same shape for minmea, MeshCore and RadioLib, and the LVGL sub-section
# further down uses it for the vendored converter -- fetched sources rather than
# actions, appearing in no workflow. So the scan is bounded by the inventory
# table itself and not by a heading: it starts after that table's header row and
# stops at the first line that is not a table row. A heading bound is far too
# wide, because the table does not own its heading -- it sits inside
# `### The container image`, and `## GitHub Actions` runs on for six more
# sub-sections of prose and tables, any pin-shaped row in which would be read
# here as an action. The container sub-table drops out on its own as well --
# `sha256:…` is 64 hex behind a prefix, not 40 -- but it no longer has to.
ledger_section() {  # ledger file -> the rows of the action inventory table
  awk '/^\| *Action *\| *Pinned at *\|/ { inside = 1; next }
       inside && !/^\|/ { exit }
       inside' "$1"
}

# shellcheck disable=SC2016  # the backticks are Markdown code spans in a PCRE.
ledger_rows() {  # ledger file -> sha \t path[ path...]
  local line c1 c2 sha paths
  while IFS= read -r line; do
    case "$line" in '|'*) ;; *) continue ;; esac
    c1=$(printf '%s' "$line" | cut -d'|' -f2)
    c2=$(printf '%s' "$line" | cut -d'|' -f3)
    sha=$(printf '%s' "$c2" | grep -oP '^\s*`\K[0-9a-f]{40}(?=`)') || continue
    paths=$(printf '%s' "$c1" | grep -oP '`\K[^`]+/[^`]+(?=`)' | tr '\n' ' ')
    printf '%s\t%s\n' "$sha" "${paths% }"
  done < <(ledger_section "$1")
}

# Prints one line per disagreement and returns non-zero if it printed any. Takes
# the workflow root and the ledger as arguments so the regression cases below run
# THIS, against a planted tree, rather than a second copy of the rule.
ledger_check() {  # workflows root, ledger file
  local tree_refs rows sha paths path at_any at_row wrong covered rc=0
  tree_refs=$(workflow_refs "$1" | grep -v '^\./' || true)
  rows=$(ledger_rows "$2")
  covered=""

  while IFS=$'\t' read -r sha paths; do
    [ -n "$sha" ] || continue
    if [ -z "$paths" ]; then
      printf 'row %s names no action path\n' "${sha:0:8}"
      rc=1
      continue
    fi
    for path in $paths; do
      at_any=$(printf '%s\n' "$tree_refs" | awk -v p="$path@" 'index($0, p) == 1' | grep -c . || true)
      at_row=$(printf '%s\n' "$tree_refs" | awk -v r="$path@$sha" '$0 == r' | grep -c . || true)
      covered="$covered $path"
      if [ "$at_any" -eq 0 ]; then
        printf 'the table pins %s at %s and no workflow uses it\n' "$path" "${sha:0:8}"
        rc=1
      elif [ "$at_row" -ne "$at_any" ]; then
        wrong=$(printf '%s\n' "$tree_refs" | awk -v p="$path@" -v r="$path@$sha" \
          'index($0, p) == 1 && $0 != r' | sort -u | tr '\n' ' ')
        printf '%d of %d occurrences of %s execute a commit the table does not name: %s\n' \
          "$((at_any - at_row))" "$at_any" "$path" "${wrong% }"
        rc=1
      fi
    done
  done <<EOF
$rows
EOF

  while IFS= read -r path; do
    [ -n "$path" ] || continue
    case " $covered " in
      *" $path "*) ;;
      *) printf 'a workflow uses %s and no inventory row names it\n' "$path"; rc=1 ;;
    esac
  done < <(printf '%s\n' "$tree_refs" | sed 's/@.*//' | sort -u)

  return "$rc"
}

LEDGER=docs/research/DEPENDENCIES.md
disagreement=$(ledger_check .github/workflows/ "$LEDGER") && ledger_rc=0 || ledger_rc=$?
if [ "$ledger_rc" -eq 0 ]; then
  ok "the dependency inventory names the commits the workflows execute"
else
  no "the dependency inventory names the commits the workflows execute" \
     "$LEDGER and .github/workflows/ disagree: $(printf '%s' "$disagreement" | tr '\n' ';' | sed 's/;/; /g')"
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

# THE LEDGER BINDING, AGAINST THE DIVERGENCE THAT ACTUALLY HAPPENED AND THE
# ONES NEXT DOOR. Every case calls `ledger_check` -- the same function the
# shipping assertion above calls -- with a planted workflow tree and a planted
# ledger, so a rule that stops holding is red here rather than merely different.
#
# The two SHAs are the real ones from #464: `a60f3e1…` is what the table said and
# `d75b94d…` is what the three privileged jobs ran. Nothing compares them with
# the tree, so they cannot rot when the action is next bumped -- they are the
# incident, written down.
lfix=$(mktemp -d) || exit 1
trap 'rm -rf "$fixture" "$lfix"' EXIT
mkdir -p "$lfix/wf"

LEDGER_SHA=a60f3e1db3edbceed2b1e6c6a9d34c36b8a15eba
RUN_SHA=d75b94d5ad426cb8546e6628b6f5f19b84e5cce1
CHECKOUT_SHA=3d3c42e5aac5ba805825da76410c181273ba90b1

# A tree with three privileged steps, one checkout, and a local action that no
# inventory row names and never should.
plant_workflows() {  # sha for the three claude steps, sha for the third alone
  {
    printf 'jobs:\n  agent:\n    steps:\n'
    printf '      - uses: actions/checkout@%s # v7\n' "$CHECKOUT_SHA"
    printf '      - uses: anthropics/claude-code-action@%s # v1\n' "$1"
    printf '      - uses: anthropics/claude-code-action@%s # v1\n' "$1"
    printf '      - uses: anthropics/claude-code-action@%s # v1\n' "${2:-$1}"
    printf '      - uses: ./.github/actions/say\n'
  } >"$lfix/wf/agent.yml"
}

# A ledger with two unrelated rows in the same 40-hex shape, one on each side of
# the inventory table, so the scoping stays asserted in every case below rather
# than in one of them: MeshCore and the vendored LVGL converter are fetched
# sources, appear in no workflow, and must not be read as actions. The LVGL row
# is the one a heading bound gets wrong -- `### Where the resolved graph lives`
# is a *sub*-section, so a scan that stops at the next `## ` swallows it whole.
plant_ledger() {  # sha recorded for the action
  cat >"$lfix/ledger.md" <<LEDGER
## Decided

| Dependency | Pinned at | Licence |
|---|---|---|
| **MeshCore** | \`d92964352441e53b93e8667b802e04f6e072b39e\`, 2026-08-14 | MIT |

## GitHub Actions

| Action | Pinned at | Tag it came from | Licence | Upgrade strategy |
|---|---|---|---|---|
| **\`actions/checkout\`** | \`$CHECKOUT_SHA\`, 2026-07-17 | \`v7\` | MIT | as below |
| **\`anthropics/claude-code-action\`** | \`$1\`, 2026-09-04 | \`v1\` | MIT | read the diff |

### Where the resolved graph lives

| Library | Pinned at | Licence |
|---|---|---|
| **LVGL** | \`85aa60d18b3d5e5588d7b247abf90198f07c8a63\`, 2026-08-20 | MIT |
LEDGER
}

ledger_case() {  # name, expected rc (0 pass / 1 reject), needle the output must carry
  local out rc
  out=$(ledger_check "$lfix/wf" "$lfix/ledger.md") && rc=0 || rc=1
  if [ "$rc" -ne "$2" ]; then
    no "$1" "expected $([ "$2" = 0 ] && echo accept || echo reject), got the opposite: ${out:-no disagreement reported}"
  elif [ -n "${3:-}" ] && [ "${out#*"$3"}" = "$out" ]; then
    no "$1" "rejected for the wrong reason -- \"$3\" is not in: $out"
  else
    ok "$1"
  fi
}

# 1. Agreement. Also case 5 of the issue's list: the local `uses: ./…` is
#    covered by no row and must not be demanded to be.
plant_workflows "$RUN_SHA"
plant_ledger "$RUN_SHA"
ledger_case "a ledger naming the executed commit is accepted" 0

# 2. The regression as it shipped: Dependabot moved the workflows, the table
#    stayed. The message has to name the commit that actually runs, because that
#    is the only thing the next reader needs.
plant_ledger "$LEDGER_SHA"
ledger_case "a SHA-only bump that leaves the table behind is rejected" 1 "$RUN_SHA"

# 3. One occurrence moved alone -- a half-applied bump, which reads as correct
#    at two of the three call sites.
plant_workflows "$LEDGER_SHA" "$RUN_SHA"
ledger_case "one occurrence out of three at another commit is rejected" 1 "1 of 3 occurrences"

# 4. Occupancy is not asserted, and must not be. A second job takes a checkout
#    at the commit the table already names: the dependency did not move, nothing
#    executes a byte the table does not name, and a required check that reddened
#    here would charge every job added anywhere in the tree with a document edit.
plant_workflows "$RUN_SHA"
printf '  extra:\n    steps:\n      - uses: actions/checkout@%s # v7\n' \
  "$CHECKOUT_SHA" >>"$lfix/wf/agent.yml"
plant_ledger "$RUN_SHA"
ledger_case "a second occurrence at the commit the table names is accepted" 0

# 5. The other direction. A row for an action nothing uses is as stale as a
#    missing one, and it is how a removed workflow leaves the table lying.
plant_workflows "$LEDGER_SHA"
plant_ledger "$LEDGER_SHA"
sed -i "/anthropics\/claude-code-action/a | **\`actions/stale-action\`** | \`$CHECKOUT_SHA\`, 2026-01-01 | \`v1\` | MIT | gone |" \
  "$lfix/ledger.md"
ledger_case "a row for an action no workflow uses is rejected" 1 "no workflow uses it"

# 6. THE VACUOUS PASS, which is the failure mode a check like this really dies
#    of. Rename a column of the header row the scan anchors on and it matches
#    nothing; without the second direction that is zero rows, zero
#    disagreements, green. Every ref in the tree becoming uncovered is what
#    makes it red instead.
plant_ledger "$RUN_SHA"
sed -i 's/^| Action | Pinned at |/| Action name | Pinned at |/' "$lfix/ledger.md"
ledger_case "a table the parser can no longer find is rejected, not passed over" 1 \
  "no inventory row names it"

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
