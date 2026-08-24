# Changes a cloud session wrote and could not push

Everything here is a `.patch` against `.github/workflows/`, and it is here for
one reason: **GitHub refuses to let a GitHub App create or update a workflow
file** unless the installation holds the `workflows` permission, and the cloud
agent's `claude[bot]` token does not.

```
! [remote rejected] claude/... -> claude/...
  (refusing to allow a GitHub App to create or update workflow
   `.github/workflows/agent-queue-watchdog.yml` without `workflows` permission)
```

The alternative to a directory like this is an agent that either abandons the
half of its task it can do, or reports success over a change that was never
pushed. Neither is better.

## Applying one

Three commands from a local session with the owner's own `gh` login, from the
repository root:

```
git apply docs/automation/pending/<name>.patch
git rm docs/automation/pending/<name>.patch
git commit -m "Land the workflow half of <what>"
```

**Delete the patch in the same commit that applies it.** A patch that outlives
its application is a second copy of a workflow file, and the second copy is the
one that goes stale — silently, because nothing lints it and nothing runs it.

**A patch carries every edit its own landing forces**, so that those three
commands produce a commit CI accepts. Two kinds are easy to miss: its row in
*Waiting now* below, which otherwise advertises a patch that no longer exists;
and any fingerprinted citation that the insertion pushes down the file, which
`tools/docs/check_docs.py` reports on the landing commit — naming the document
that holds the citation rather than the workflow that moved it. `git apply`
then `python3 tools/docs/check_docs.py .` before parking a patch says which.

**And a citation in a `.sh` or a `.yml` will never be reported at all**, which
is the case that has to be carried by hand. `check_docs.py` walks `*.md` and
nothing else, so a `ci.yml:NNN` written in a shell comment is invisible to the
one tool that would otherwise catch it: the landing commit is green and the
citation is wrong. `75-approval-stall.patch` carries exactly one of these —
`.github/tests/gh-api-usage-test.sh`'s own comment citing the `ci.yml` line that
runs it, bumped from `:360` to `:371` in the same patch that inserts eleven
lines above it. Found in the fourth review round of
[#180](https://github.com/hleserg/Attadipa/pull/180), where the patch already
bumped all three of its `.md` citations correctly and had missed the one nothing
would report. **Before parking a patch, grep the non-Markdown files it touches
for a citation into a file it moves.**

**CI checks that it still applies**, on every push, in
`.github/tests/gh-api-usage-test.sh`
([`.github/workflows/ci.yml:360`](../../../.github/workflows/ci.yml) "bash .github/tests/gh-api-usage-test.sh") — `git apply --check` over
every `*.patch` in this directory. Until T-158 that sentence read *"check it
before trusting it"* and nothing did, which is the shape of every defect this
directory's own patches were written to fix.

**It is loud in two different ways, and which one depends on the half of the
patch that moved.** A patch has two, and they rot for different reasons.

| what moved | CI | why |
|---|---|---|
| a hunk under `.github/workflows/` | **fails** | nobody but the owner or another landing patch can move that context, and a stale workflow hunk means the parked change itself is now wrong |
| a hunk under `.github/scripts/` or `.github/tests/` | **warns** | the `workflows` permission gates `.github/workflows/` and nothing else: agent branches write scripts and tests constantly, so this half rots under ordinary work like the docs half does |
| anything else the patch carries | **warns** — a job-summary line and a `::warning file=` annotation, naming the file that moved | those pins move under ordinary work, by people who did not choose to and cannot rebuild a workflow patch either |

The second row is not caution, it is arithmetic. `check_docs.py` enforces the
`ci.yml` citation fingerprints, so inserting a line into `ci.yml` *forces* a
citation edit that moves this patch's own pinned context. Fail on that and the
red lands on `main` and every open pull request over a file none of them
touched, and one stale patch stops the whole queue.

**Either way there are exactly two answers, and doing nothing is not one.** If
the patch has already been landed, it is a leftover copy: `git rm` it, which is
the same rule as *delete the patch in the same commit that applies it*, one
commit late. If it has not been landed, rebuild it against the current tree —
never hand-edit the hunk headers, and note that a patch written against a
`main` that has since moved may want `git apply -3` when it does land.

The same suite also reads the shell a parked patch would deploy — its
**post-image**, context plus added lines — for the `gh api --slurp` with `--jq`
pair that `gh` rejects before making a request: three of those shipped in
`pr-merge-sweep.yml`, and the scan that catches them globbed
`.github/workflows/` only, so it could not see this directory at all. It reads
the post-image and not the added lines alone because every `--slurp` call in
this repository puts `gh api` on one line and its flags on the next; a patch
touching only the flag line has no `gh api` among its added lines. Removing a
bad call is still not an offence — take `--jq` out and the post-image no longer
holds one — and only hunks targeting `.github/workflows/`, `.github/scripts/`
or `.github/tests/` are read, so a patch that *documents* the rule in prose is
left alone.

That scan filter is deliberately **wider** than the fail-versus-warn split
above: a forbidden `gh` call is forbidden wherever it is parked, and breadth
costs nothing there, while the split needs precision because a wrong fatal reds
every open pull request at once. Two globs, on purpose.

**What is parked here must be one flat directory of `*.patch` files**, and CI
now refuses anything else rather than skipping it. A patch in a subdirectory, a
file saved as `.diff`, a directory that has been renamed away, a file with no
diff target in it at all — each of those used to leave *both* halves of the
guard reading nothing while each printed its strongest green line. Group a
multi-file change into one patch, not into a folder. And a **missing**
directory is a failure where an **empty** one is a pass: empty is a state
somebody chose, missing is one nobody noticed.

**A patch `git` cannot parse is reported separately, and its remedy is never
`git rm`.** Rebuild it. Deleting a patch nobody can read is deleting work
nobody has read.

What CI still cannot judge: a patch that applies cleanly onto a job that has
been rewritten underneath it. `--check` says the context matches, not that the
change still makes sense.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

## Two rules that came out of one review round, and the owner's answers

Both were raised in the fourth review round of
[#180](https://github.com/hleserg/Attadipa/pull/180), because each put a red on
`main` **and every open pull request at once** — and that radius is the same
class of decision as the unattended-merge allowlist, which
[`CLAUDE.md`](../../../CLAUDE.md) reserves to the owner. Both were answered on
2026-08-24, and both answers are now enforced by
`.github/tests/gh-api-usage-test.sh` rather than left as prose.

### At most one parked patch may carry a hunk for any one workflow file

The apply half is fatal on a `.github/workflows/` hunk because nobody but the
owner can move workflow context. That is right as far as it goes, and it has one
failure mode nothing else covers: **landing a parked patch moves that context
too.** Two patches each inserting a step into `ci.yml` is the natural shape — a
new test script needs a line there, which is the stated reason patches get parked
at all. Land the first and the second goes `stale-workflow`: red on `main`, on
every open pull request, and therefore on the orchestrator merge and on
[`pr-merge-sweep.yml`](../../../.github/workflows/pr-merge-sweep.yml), both of
which gate on green. Not a deadlock — the pull request that rebuilds the second
patch has a green tree of its own — but everything else is red while somebody
does it.

Three options were put to the owner: leave it fatal, soften it to a warning like
the docs half, or forbid the collision. **The answer was to forbid the
collision**, and the reasoning is worth keeping: softening loses the only hard
barrier over files an agent token cannot write, while forbidding costs one
patch's parking and fails **here, when the second patch is written**, instead of
on `main` after the first one lands. It fails at the cheap moment rather than the
expensive one.

So: if a patch you are about to park carries a hunk for a workflow file another
parked patch already carries, **land the parked one first, or fold your workflow
hunk into it.** Never `git rm` either — both are work nobody has landed. A patch
that renames a workflow counts for both names.

### An empty `.gitkeep` is allowed; one with content in it is not

Anything here that is not a `*.patch`, not a `README.md` and not a `.gitkeep`
fails the scan — and that failure then turns the apply half off as well, because
it returns silently on `UNREADABLE`. Half the guard is off while somebody works
out why.

This file says the directory is empty in normal operation, and git cannot track
an empty directory, so a placeholder is the reach anybody would make. Refusing it
was a trap. The owner's answer was to **allow `.gitkeep`, and only when it is
empty** — the emptiness test is what keeps the exemption from becoming a hiding
place. A `.gitkeep` with anything in it is a file somebody put content in, and
this guard cannot judge content. Both directions are pinned by fixtures.

Every other unrecognised name still fails, and that stays deliberate: the
enumeration's promise is that it opens everything parked here, and passing over a
file it does not recognise is how a guard turns itself off.

## Waiting now

| Patch | For | Written |
|---|---|---|
| `75-approval-stall.patch` | [#75](https://github.com/hleserg/Attadipa/issues/75) — the writer checkout's `token:`, the watchdog's `approvals` job, and the test's line in `ci.yml`. See [APPROVAL_STALLS.md](../APPROVAL_STALLS.md) | 2026-08-23 |

Verified before it was parked: `actionlint` clean over all seven workflows with
the patch applied, `shellcheck -x` clean, and the `approvals` job's body
dry-run against the live repository — the pagination, the jq, the marker
written and read back, and the rendered comment. What that does **not** prove
is the job running on a schedule under its own permissions, which no local run
can prove and which is therefore `NOT EXECUTED` until it is deployed.
