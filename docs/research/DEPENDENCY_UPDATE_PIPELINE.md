# Dependency update pipeline — what a Dependabot pull request meets here

Research for #562. Reviewed at `main@8e16c1af`, 2026-09-23. No automation,
workflow or Dependabot configuration is changed by this document; the one change
it argues for is filed as #641.

Hardware: not applicable. Nothing here is a hardware result.

## The finding in one paragraph

A weekly Dependabot run opens one pull request per action, and **every one of
them is red by design**. The inventory gate refuses any pin that
`docs/research/DEPENDENCIES.md` does not name, and Dependabot never edits that
file. Each of those pull requests counts against the four-slot writer queue. None
of them is reviewed, repaired, updated or merged by any automation here. So a
burst holds queue slots until a person with the owner's token finishes it by
hand. This has happened twice: #556–#558 on 2026-09-14 and #631–#633 on
2026-09-21. Grouping the CodeQL sub-actions makes the burst smaller and fixes
the split `init`/`analyze` bump. It does **not** make the pull requests green.
The hand-finishing step is the actual process, and this document names it
instead of hiding it.

## Snapshot

| Fact | Value | Source |
|---|---|---|
| Writer queue width | 4 | repository variable `ATTADIPA_WIP_LIMIT` = `4` (`gh variable list`, 2026-09-23) |
| What counts against it | every same-repository open PR without `queue:parked` or `queue:emergency`; **no author filter** | `.github/scripts/wip-limit.sh:39` — "queue:emergency" |
| Dependabot version-update ceiling | 5 open PRs; 2 since #641 | `.github/dependabot.yml:20` — "open-pull-requests-limit: 2" |
| Dependabot security updates | **enabled**, not paused | `gh api repos/hleserg/Attadipa/automated-security-fixes` → `{"enabled":true,"paused":false}`; vulnerability alerts → `204` |
| Groups | none; one CodeQL group since #641 | `.github/dependabot.yml:30` — "github/codeql-action/*" |
| Second burst | #631 (claude-code-action), #632 (codeql `analyze`), #633 (codeql `init`), opened 2026-09-21 05:14–05:16Z | `gh pr view` |
| Its effect | the writer lease for this issue was refused: "Active pull requests: 5 (incident) — #633 #632 #631 #622 #610" | `writer-start.sh start`, 2026-09-23 |
| Workaround applied | 631–633 labelled `queue:parked`, with a comment linking here | PR comments, 2026-09-23 |

### Why every Dependabot PR is red, twice over

1. **Our gate.** Required CI run 35563914149 on #632:
   "1 of 1 occurrences of github/codeql-action/analyze execute a commit the table
   does not name: github/codeql-action/analyze@1c5b675…". The check is
   `ledger_check` in `.github/tests/action-pin-test.sh:128` — "ledger_check() {".
   It is doing its job. Weakening it is out of scope for this issue and has
   no owner decision behind it.
2. **CodeQL's own check, which has nothing to do with our gate.** CodeQL run
   35563914143 on the split bump: "Loaded a configuration file for version
   '4.38.0', but running version '4.38.1'". A pull request that moves only one of
   `init` and `analyze` cannot pass however the inventory is edited. The
   inventory already says so:
   `docs/research/DEPENDENCIES.md:114` — "both sub-paths share one repository and must move together".
   Both are pinned to the same commit today:
   `.github/workflows/codeql.yml:42` — "github/codeql-action/init@1c5b675" and
   `.github/workflows/codeql.yml:55` — "github/codeql-action/analyze@1c5b675".

## The state machine a Dependabot PR actually goes through

Each guard below was read at source. None of them was assumed.

| Stage | What happens to a `dependabot/*` PR | Source |
|---|---|---|
| Opened | counts against the queue immediately; `pr-wip-limit.yml` adds `queue:over-limit` (add-only, read by nothing) | `.github/scripts/wip-limit.sh:39` — "queue:emergency" |
| Labels | gets `source:owner`, which is defined as filed by the owner. **A bot PR is labelled as owner-filed.** Until #641 it also got `dependencies`, a label that does not exist here. | `.github/dependabot.yml:22` — "- source:owner"; `.github/scripts/setup-labels.sh:63` — "Filed by the owner, per the task marker." |
| Required CI | red: inventory gate; for a split CodeQL bump, also CodeQL | above |
| Independent review | **never runs** while Dependabot is the actor | `.github/workflows/claude-pr-review.yml:90` — "github.actor != 'dependabot[bot]'" |
| CI repair | **refused**: only `claude/*` branches | `.github/workflows/claude-ci-repair.yml:126` — "claude/*) ;;" |
| Branch update | **nothing runs for anyone.** `pr-branch-update.sh` has no workflow caller on `main`; only its test runs, in CI. The decision would not exclude a bot anyway, only a held PR. | `git grep pr-branch-update.sh` finds no workflow; `.github/scripts/pr-branch-update-decision.sh:125` — "held=yes ;;" |
| Merge sweep | **holds forever**: it needs `ai-review:pass`, which only the review sets, and the review never runs | `.github/scripts/merge-candidate.sh:314` — "HOLD no ai-review:pass" |
| Exit | a person with owner-capable credentials pushes the inventory row onto the Dependabot branch (which also makes that person the actor, so review now runs), closes the twin, and merges | #556: `hleserg` commit "both codeql-action sub-paths move together…", #557 closed; #558: `hleserg` commit adding the ledger row |

The exit row is the whole process today. The next section makes it explicit.

## Answers to the eight questions

**1. How Dependabot names the CodeQL sub-actions.** It uses two dependency
names that include the sub-path. The evidence is Dependabot's own
machine-readable commit trailer, not the PR title:
`dependency-name: github/codeql-action/analyze` on #632 and
`dependency-name: github/codeql-action/init` on #633, both at
`dependency-version: 4.38.1`. The branch names follow the same pattern.
GitHub's options reference (read 2026-09-23) says `patterns` "include
dependencies with matching names" and that `*` works as a wildcard. So
`github/codeql-action/*` matches both. **What was not run:** a disposable
repository that shows one grouped PR. Creating one is an external write that
nobody authorised. Until the first grouped run on this repository, the grouped
result is **ESTIMATED, not observed**.

**2. Grouping CodeQL without the Claude action.** A group whose only pattern is
`github/codeql-action/*` cannot match `anthropics/claude-code-action`, because
the names differ from the first character. Only version updates are grouped by
default (`applies-to` defaults to `version-updates`). A security update for one
CodeQL sub-action would still arrive alone, unless a second group with
`applies-to: security-updates` is added.

**3. The limit that leaves room for product work.** With width 4 and one
grouped CodeQL PR plus one Claude-action PR, a normal burst is 2. Set
`open-pull-requests-limit: 2`. The worst version-update burst then takes half
the queue instead of all of it. Security PRs do not count toward this limit, per
GitHub's reference: "*Security update* pull requests are not subject to this
limit and do not count toward it."

**A parked PR is still an open PR.** `queue:parked` removes a PR from our queue
count. It does not remove it from Dependabot's `open-pull-requests-limit`. With
#631–#633 parked and a limit of 2, Dependabot would open nothing and give no
signal here: a silent stall. So the limit only works if a burst that meets a full
queue is **closed, not parked**, and #631–#633 are closed before the limit
changes (#641).

**4. How Dependabot PRs should count.**

| Option | Failure mode |
|---|---|
| Count as ordinary WIP (today) | a burst fills the queue; it is released only by hand-finishing or parking |
| Auto-`queue:parked` on open | the queue stays free, but parked PRs pile up unseen, still fill Dependabot's own limit, and the branch-update decision skips them once it is wired |
| Separate bounded intake | new write-capable automation, which the issue's non-goals rule out |
| One PR at a time | `open-pull-requests-limit: 1` makes the Claude and CodeQL updates wait on each other week to week; security PRs still ignore it |

Recommendation: keep counting them (no hidden exemption), and bound the count
with question 3. Parking worked on 2026-09-23 only because the Dependabot limit
was 5. Under limit 2 the manual step is closing (§3). Whether to exempt bot PRs from WIP is a question for the owner
(question 8).

**5. Who writes the second half.** Today, and under every candidate here, the
second half is written by **the owner or an agent session running on the owner's
token**. It covers the `DEPENDENCIES.md` row, the pinned-commit date, the
upstream diff read for the Claude action -- a record `action-pin-test.sh` refuses
unless it ends at the new pin -- and the check that every occurrence
moved. Workflow files need owner-capable credentials, so no hosted agent can
push them. Nothing is automated, and nothing should be until a trust boundary
for it is proven. That was ruled out in #562's non-goals too. This is the courier
step the issue's FAIL clause names. It is named here, and the separate issue
makes it visible on the PR itself.

**6. One group or two classes.** Two classes. `github/codeql-action/*` is grouped
because it *must* be atomic. `anthropics/claude-code-action` stays alone because
`docs/research/DEPENDENCIES.md:112` — "Read the upstream diff before bumping". It
is the highest-privilege dependency, and one rollback unit per concern is
simpler. The other GitHub-owned actions (`checkout`, `upload-artifact`, `cache`)
do not need grouping. They rarely move together, and each one is a one-row edit.

**7. Whether current automation can converge a Dependabot PR.** It cannot. See
the state machine above: no review, no repair, no branch update, no sweep. The
branch-update script is not stopped by a bot exclusion; it is not wired to any
workflow at all. Something **not verified here**: whether Dependabot keeps
rebasing its branch after a person pushes to it. The options reference read for
this document does not say. Both past hand-finished PRs merged within hours, so
it did not matter then.

**8. What only the owner can decide.** Asked in #643 (`needs-owner`).
- Auto-merge of dependency PRs. It is rejected here as a default and needs an
  owner decision to enable.
- Whether Dependabot PRs are exempt from the writer queue.
- Security-update policy: keep it enabled (it is on), and whether a security
  group is wanted.
- Whether `source:owner` on bot PRs is intended. It asserts owner filing.

## Decision matrix

| Candidate | Max open version-update PRs | Takes all 4 slots? | CodeQL atomic? | Inventory by | Security PRs |
|---|---|---|---|---|---|
| Today | 5 | yes | no (split) | owner/agent by hand, unannounced | unbounded |
| Group `github/codeql-action/*` only | 5 | yes (if 5 distinct updates) | yes | same | unbounded |
| Group + limit 2 **(recommended)** | 2 | no | yes | same, now stated | unbounded, not counted by Dependabot, **counted** by our queue |
| Group + limit 2 + auto-park | 2, then **0**: parked PRs stay open and fill it | no | yes | same | silent stall |
| Exempt bots from WIP | 5 | no | no | same | unbounded and invisible to the queue |

**Research verdict: PARTIAL.** The recommended process keeps full-SHA pins
and the two-way gate. It makes CodeQL atomic and stops a version-update burst
from taking all four slots. It names who finishes the inventory and review, and
adds no auto-merge. Two parts are still open:
- the grouped outcome is ESTIMATED until the first Monday after the change;
- security PRs remain unbounded by Dependabot. Here they are **bounded only by
  our own queue**, which counts them. That is the right failure: loud, not
  silent.

## Failure, retry and rollback

- **Grouped run produces two PRs anyway.** The first Monday after the change
  shows it. Close both, revert the `groups` entry, record the observed names.
  The inventory gate still refuses any partial bump, so no split can merge.
- **Burst meets a full queue.** Close the Dependabot PRs with a comment. Do
  not park them under limit 2: a parked PR still fills Dependabot's limit (§3).
  Whether Dependabot re-proposes a version whose PR was closed is **UNKNOWN**.
  If it does not, the owner-token session makes that bump by hand.
- **Limit too low, updates starve.** Dependabot waits until a PR closes, and
  reports it only in its own job log, which nothing here reads. Check the open
  Dependabot PRs whenever a weekly run brings none. Raise the limit by a one-line
  revert.
