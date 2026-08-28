#!/usr/bin/env bash
# Create (or correct) every label the agent queue depends on.
#
# The labels ARE the state — docs/automation/AI_TASK_PROTOCOL.md#lifecycle. There
# is no separate database, so a label that does not exist is a state a task
# cannot reach: `gh issue edit --add-label` fails, the workflow's `|| true`
# swallows it, and the task sits in a state nobody set. That failure is silent,
# which is why this is a file that can be re-run rather than a paragraph telling
# somebody to click sixteen times.
#
# Idempotent: `gh label create --force` updates colour and description in place
# when the label already exists, so running this against a repository that is
# already correct changes nothing and says so.
#
# Usage:  .github/scripts/setup-labels.sh [owner/repo]
#         .github/scripts/setup-labels.sh --dry-run [owner/repo]
#
# Requires: gh, authenticated with a token that can write labels.

set -euo pipefail

DRY_RUN=false
if [ "${1:-}" = "--dry-run" ]; then
  DRY_RUN=true
  shift
fi

REPO="${1:-}"
if [ -z "$REPO" ]; then
  REPO=$(gh repo view --json nameWithOwner --jq .nameWithOwner)
fi

# name|colour|description
#
# Grouped exactly as AI_TASK_PROTOCOL.md groups them. Colour carries meaning at a
# glance in the issue list: state is blue, refusals are red, humans are amber.
#
# The three `queue:` labels were live on the repository and absent from this file
# until #239 -- created by hand, so a fresh setup would not have had them, and
# the header above says exactly what that costs: `pr-wip-limit.yml` adds
# `queue:over-limit` under `|| true`, so on a repository set up from this script
# the label would silently never appear. Their colours and descriptions here are
# transcribed from the deployed ones rather than invented, so re-running this
# with `--force` over the live repository changes nothing.
#
# `queue:over-limit`'s deployed description ends *"See CLAUDE.md, OD-23"* and
# BOTH HALVES OF THAT POINTER ARE DANGLING: OWNER_DECISIONS.md runs OD-1..OD-20
# and then OD-24, and CLAUDE.md does not discuss the queue's width at all. It is
# transcribed unchanged rather than corrected here, because rewriting it would
# edit the live label the next time anybody runs this, and an owner decision is
# not an agent's to invent or to renumber. The policy itself is written down in
# CLAUDE_AUTOMATION.md's cost-control table as of #239. Naming a decision record
# that does not exist is the owner's to resolve.
LABELS=$(cat <<'EOF'
agent:ready|1d76db|Queued for an agent. The watchdog may hand this to the writer.
agent:working|0e8a16|An agent holds this. One writer at a time, repository-wide.
agent:review|5319e7|Work is done and waiting on review or a merge decision.
agent:blocked|b60205|Cannot proceed. Pair with needs-owner or needs-hardware.
agent:failed|b60205|The agent run ended without a conclusion. Comment @claude to retry.
agent:claude|8a2be2|Claude is the writer on this task.
source:chatgpt|c5def5|Filed by ChatGPT, per the task marker.
source:claude|c5def5|Filed by Claude, per the task marker.
source:owner|c5def5|Filed by the owner, per the task marker.
type:review|bfd4f2|Review of a commit range.
type:upstream|bfd4f2|What changed in a dependency and what it costs us. Research only.
type:quality|bfd4f2|Existing code or documents held against a standard.
type:research|bfd4f2|Find out what the next task needs before it starts. Research only.
type:readiness|bfd4f2|Are we where a milestone says we are. Research only.
priority:P0|b60205|Drop everything.
priority:P1|d93f0b|Next.
priority:P2|fbca04|Normal. The default when the marker does not say.
priority:P3|0e8a16|When there is room.
needs-owner|fbca04|Needs a human decision, a credential, or a settings change.
needs-hardware|fbca04|Needs a physical board, an instrument or a measurement.
needs-rebase|fbca04|The branch conflicts with the base and only hands can fix it. Set and cleared by pr-branch-update.sh.
ci:repairing|fef2c0|Automatic CI repair is in flight.
ci:failed|b60205|CI is red and automatic repair has stopped.
ai-review:pass|0e8a16|The independent reviewer found nothing blocking.
ai-review:blocking|b60205|The independent reviewer found something that must be fixed before merge.
queue:parked|d4c5f9|Held open deliberately, with the owner's agreement. Not counted against the WIP limit.
queue:emergency|b60205|Security, data loss, broken CI or a regression in main. May be opened over the WIP limit.
queue:over-limit|fbca04|Opened while the queue was at or over its width. See CLAUDE.md, OD-23.
EOF
)

created=0
printf 'Repository: %s\n\n' "$REPO"

while IFS='|' read -r name colour description; do
  [ -n "$name" ] || continue
  if [ "$DRY_RUN" = true ]; then
    printf '  would ensure  %-22s #%s\n' "$name" "$colour"
    continue
  fi
  # --force is what makes this idempotent: it creates when absent and updates
  # colour and description when present, rather than failing on "already exists"
  # and taking the whole script down under `set -e`.
  if gh label create "$name" --repo "$REPO" --color "$colour" \
       --description "$description" --force >/dev/null 2>&1; then
    printf '  ok            %-22s #%s\n' "$name" "$colour"
    created=$((created + 1))
  else
    printf '  FAILED        %-22s (check the token can write labels)\n' "$name"
    exit 1
  fi
done <<< "$LABELS"

if [ "$DRY_RUN" = true ]; then
  printf '\nDry run. Nothing was changed.\n'
else
  printf '\n%d labels ensured.\n' "$created"
fi
