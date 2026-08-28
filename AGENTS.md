# Attadipa — working agreement

Keep this file short: it is the repository-wide entry point for people and
agents. Before changing a scoped area, also read its nearest `AGENTS.md`.

## Start with the work, not the repository memory

1. Read the GitHub issue and its comments. The issue is the task.
2. Check open issues and pull requests before editing. An assignee or draft PR
   means the work is claimed; avoid a competing implementation.
3. Use `graft` before source-code discovery and prefix shell commands with
   `rtk`. The tools' own help is the source for their detailed usage.
4. Read only the product requirements, ADRs and scoped rules relevant to the
   files being changed.

## Non-negotiable invariants

- Do not use a hardware fact until it is traced to a datasheet, the schematic
  for the relevant board revision, vendor source, or a reproducible bench
  result. If it cannot be established, write `UNKNOWN` and record the evidence
  or blocker under `docs/research/`.
- A test that did not run on physical hardware is never a hardware `PASS`.
  Write `NOT EXECUTED — HARDWARE REQUIRED`. Label physical numbers `MEASURED`,
  `ESTIMATED`, or `UNKNOWN`.
- Do not burn eFuses, enable irreversible security settings, destroy keys, or
  commit secrets. Reversible flashing is allowed when the factory image is
  backed up and verified.
- Applications ask what a device can do, not which board it is. Board-specific
  code belongs in `boards/` or `platform/`; no `#ifdef BOARD_X` in `core/` or
  `apps/`.
- Research verifies claims; it does not quietly grow a production subsystem.

## One fact, one home

| Information | Canonical source |
| --- | --- |
| Work to do, acceptance criteria, blockers | GitHub Issue |
| Work in progress and result | assignee, linked PR, checks and PR review |
| Durable product direction | `docs/ROADMAP.md` |
| Product requirements | relevant section of `docs/master-prompt-final.md` |
| Architecture decision | `docs/adr/` |
| Owner-only decision | `docs/research/OWNER_DECISIONS.md` |
| Verified hardware fact and provenance | `docs/research/VERIFIED_FACTS.md` or a linked evidence report |
| What changed and how it was tested | Pull request |

`STATUS.md` and `TASKS.md` are compatibility pointers, not ledgers. Do not edit
them for individual tasks. Git and closed issues already preserve history.

## Automation is maintenance

Agent automation is maintenance infrastructure, not a permanent product
workstream. There is no standing automation roadmap task. Automation may
preempt device work only for a demonstrated P0/P1 security, corruption or
data-loss defect, a demonstrated queue stall blocking product development, or
an explicit owner request. New automation implementation needs one finite issue
with explicit scope and Definition of Done; convenience, elegance, extra
orchestration, recovery, retries, observability and hypothetical robustness do
not enter the queue automatically.

## Delivery

- Before creating a branch or editing, run
  `.github/scripts/writer-start.sh start REPO ISSUE AGENT_ID` from current `main`;
  `AGENT_ID` is an opaque label for the holder, such as `agent-<run>-<attempt>` —
  **never a credential**: it is published in a tag anyone can read, and the tag
  object survives deleting the tag;
  its repository lease and atomic claim are the machine-enforced writer gate.
  Run `writer-start.sh finish ...` on hand-off. Keep commits logical and never
  push directly to `main`.
- A PR links its issue with `Fixes #<issue>` and states what was tested and what
  was not. Update durable documentation only when the durable fact changed.
- A blocker comment states reason, evidence, impact and one recommended next
  action; add `needs-owner` or `needs-hardware` only when that dependency is real.
- Run the smallest meaningful checks that exercise the shipping seam. A test
  of a fixture, copied implementation, generated patch, or isolated decision
  helper does not prove the production caller works.
- If UI, navigation, themes, widgets, touch, buttons or screen geometry changed,
  use the `watch-ui-testing` skill and inspect the rendered image.
- Do not create a follow-up issue for debt that reasonably fits the current
  change. Do not add a mechanism unless it lets the repository remove an old
  one.

## Scoped instructions

- Firmware and board work: `firmware/AGENTS.md`
- GitHub automation: `.github/AGENTS.md`
- Hardware and upstream research: `docs/research/AGENTS.md`

Repository artefacts are English. Owner-facing chat is Russian; public issue
and PR text is English first and Russian second. `README.md` and `README.ru.md`
are one document and change together.
