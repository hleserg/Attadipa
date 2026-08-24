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

## Two things the guard does on purpose, and only the owner may change either

Both were found in the fourth review round of
[#180](https://github.com/hleserg/Attadipa/pull/180) and both are written down
rather than quietly decided, because each puts a red on `main` **and every open
pull request at once** — and widening or narrowing what the guard fails on is
the same class of decision as the unattended-merge allowlist, which
[`CLAUDE.md`](../../../CLAUDE.md) already reserves to the owner.

**1. Two parked patches that both edit `ci.yml` will red the queue when the
first one lands.** The fatal arm covers `.github/workflows/`, and that is
correct as far as it goes: nobody but the owner can move workflow context, so a
patch whose workflow half has drifted really is wrong now. But *landing another
parked patch* moves that context too, and two patches each inserting a step into
`ci.yml` is the natural shape — a new test script needs a line there, which is
the stated reason patches get parked at all. Land the first and the second goes
`stale-workflow`, which is red on `main`, on every open pull request, and
therefore on the orchestrator merge and on
[`pr-merge-sweep.yml`](../../../.github/workflows/pr-merge-sweep.yml), both of
which are gated on green.

It is not a deadlock — the pull request that rebuilds the second patch has a
green tree of its own — but everything else is red while somebody does it.
"Rare" is a claim nobody has established; "small audience" is the one that is
true, and they are different sentences. The docs half was made a warning to
avoid exactly this radius. **The options are: leave it fatal; make it a warning
like the other half; or keep it fatal and require that only one patch at a time
may carry a `ci.yml` hunk.** No agent picks between those.

**2. A placeholder file in this directory reds the whole queue.** Anything here
that is not a `*.patch` and not a `README.md` fails the scan — `.gitkeep`,
`.gitignore`, `NOTES.md`, a stray `README.ru.md`. That is deliberate: the
enumeration's promise is that it opens everything parked here, and passing over
a file it does not recognise is how the guard turns itself off. It is also a
trap, because this file says the directory is empty in normal operation and git
cannot track an empty directory, so `.gitkeep` is the obvious reach. Worse, the
apply half returns *silently* while that red is being diagnosed, so half the
guard is off and nothing says so.

The behaviour is now pinned by a fixture either way
(`.github/tests/gh-api-usage-test.sh`, *"a .gitkeep beside a valid patch fails
the queue"*), so a change to it is a change somebody chose. **The options are:
leave it; allow a named placeholder such as `.gitkeep` explicitly; or warn on an
unrecognised file rather than failing.**

> **For the owner — two questions, and both can wait.**
> Neither is urgent: nothing is broken today, both are recorded so that the day
> one of them fires, nobody has to reconstruct why. Answer them whenever the
> queue next comes up.
>
> **По-русски.** Два вопроса к тебе, оба не срочные. Ничего сейчас не сломано —
> это записано, чтобы в день, когда оно выстрелит, никто не разбирался заново.
> Первый: два припаркованных патча, оба правящие `ci.yml`, — когда садится
> первый, второй краснеет, и краснеет он на `main` и на всех открытых PR сразу.
> Оставить как есть (это честно: патч действительно устарел), сделать
> предупреждением, или запретить больше одного патча с правкой `ci.yml`
> одновременно? Второй: любой посторонний файл в `pending/` (например
> `.gitkeep`) роняет всю проверку. Оставить, разрешить `.gitkeep` явно, или
> сделать предупреждением? Ответ «оставить как есть» — полный ответ на оба.

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
