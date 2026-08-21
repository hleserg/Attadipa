# Handoff: what a local session must do, and why a cloud session could not

**Written:** 2026-08-21 · **Updated:** after #9 merged as `b1a3dca` ·
**By:** the Claude Code cloud session that produced pull request #9 · **For:** a Claude Code session running on the owner's own
machine, with the owner's own `gh` login.

Everything in this file is blocked by one thing: **the cloud session's GitHub
credential is a `claude[bot]` App installation token, and its egress proxy
refuses GraphQL and repository-settings writes.** Not permissions — the proxy
itself:

```
$ curl -X POST .../graphql -d '{"query":"{repository(...){issueCreationPolicy}}"}'
{"message":"This GraphQL query is not enabled for this session — only the pinned
set of PR-review operations is served."}

$ curl -X PATCH .../repos/hleserg/FireflyOS -d '{"has_discussions":true}'
403  Repository settings writes are not permitted through this proxy.
```

A local session has none of those limits. Everything below is one command or
one file away there.

Run them in order. **1 and 7 are the ones that matter** — until 1 is done the
agent queue does not close, and until 7 is observed nobody knows whether the
biggest fix in #9 actually works. The rest is housekeeping.

---

## 0. Before anything

```bash
gh auth status          # must be a USER account, not an app
gh api user --jq .login # expect: hleserg
cd /path/to/FireflyOS && git fetch origin && git checkout main && git pull
```

If `gh api user` reports anything ending in `[bot]`, stop — the whole point of
task 1 is that bots and users are different here.

---

## 1. Decide how ChatGPT files issues — the one real blocker

**Nothing else on this list changes whether the automation works. This does.**

### What was proven, not guessed

The intake gate (`.github/scripts/intake-decision.sh`) trusts the **actor**, not
the marker, and refuses any login ending in `[bot]`. That guard is correct: a
Claude comment mentioning `@claude` would otherwise start a Claude run that
comments, and the bill grows until somebody notices.

On 2026-08-21 that guard was demonstrated against a real, correctly-marked task.
[Issue #10](https://github.com/hleserg/FireflyOS/issues/10), gate log from run
`32475652479`:

```
EVENT_NAME: issues
ACTOR: claude[bot]
ACTION: opened
##[notice]#10 actor claude[bot] is a bot
```

Worse than the refusal: that issue's `author_association` is `NONE`, so
`agent-queue-watchdog.yml` skips it too — it filters on `OWNER`, `MEMBER` or
`COLLABORATOR`. **The task was invisible to every part of the pipeline at once
and the workflow run went green.** Issue #5, filed by `hleserg` as a `User`, was
accepted the same day with the same shape of marker. The route decides this, not
the content.

### The answer, and the one command it needs

**ChatGPT's identity in this repository is now known.** It reaches GitHub through
its App and acts as `chatgpt-codex-connector[bot]` — a `Bot`, association `NONE`.
That is the login that reviewed pull request #11. Running the real gate function
against it:

```
$ .github/scripts/intake-decision.sh 'chatgpt-codex-connector[bot]' issues opened ...
reject: actor chatgpt-codex-connector[bot] is a bot
```

So there is no user account to grant write to — the earlier "Option A" is not
available for the connector. The owner chose the allowlist, and it is built,
tested and merged. What remains is to switch it on:

```bash
gh variable set FIREFLY_TRUSTED_PRODUCERS \
  --body 'chatgpt-codex-connector[bot]' --repo hleserg/FireflyOS
gh variable list --repo hleserg/FireflyOS
```

Then have ChatGPT file one issue and watch it be accepted rather than refused.

**Confirm the login first.** `chatgpt-codex-connector[bot]` is what reviews pull
requests here; that it is the same identity used to *create issues* is a strong
inference, not an observation. If the first task is still refused, the gate's
refusal comment now names the actual actor on the issue — put that login in the
variable instead. That is the whole diagnostic loop, and it needs no code change.

The variable is empty by default, applies to `issues` events only, can never
list `claude` or `github-actions`, and matches logins exactly. Thirteen tests in
`.github/tests/intake-gate-test.sh` cover those properties, including a comment
from a listed producer (refused) and a login that merely contains a listed one
(refused).

---

## 2. Set the repository variable

`vars.CLAUDE_AUTOMATION_ENABLED` is read by all four Claude workflows. **Unset
reads as enabled**, so the pipeline is live right now on an unset variable. The
kill switch works either way; setting it explicitly is what makes
`docs/automation/RECOVERY.md`'s first command meaningful.

```bash
gh variable set CLAUDE_AUTOMATION_ENABLED --body true --repo hleserg/FireflyOS
gh variable list --repo hleserg/FireflyOS
```

Set it to `false` instead if you want the loop parked while PR #9 is reviewed.
`false` stops every Anthropic-billed step and leaves ordinary CI running.

---

## 3. Confirm the credentials, and prune one

Two facts already established from run logs, so do not re-derive them:

- an Anthropic credential **is** configured and works — `Say what is missing` is
  skipped and `Run Claude` succeeds;
- `ANTHROPIC_API_KEY` is **empty** — it prints as blank in the review job's env.
  So the live path is `CLAUDE_CODE_OAUTH_TOKEN`, the subscription one, which is
  the intended path.

```bash
gh secret list --repo hleserg/FireflyOS
```

Expect `CLAUDE_CODE_OAUTH_TOKEN`. If `ANTHROPIC_API_KEY` is also present, it is
an unused metered fallback — deleting it removes a way to bill an API account by
accident:

```bash
gh secret delete ANTHROPIC_API_KEY --repo hleserg/FireflyOS
```

The workflows accept whichever exists, so deleting the unused one changes no
behaviour. **Do not remove the `ANTHROPIC_API_KEY` lines from the workflow
files** — they are the documented fallback.

`FIREFLY_AGENT_TOKEN` is intentionally unset. Empty means the action
authenticates as the Claude GitHub App, whose installation token *does* trigger
workflow runs. `secrets.GITHUB_TOKEN` would not, which is why it appears nowhere.
The app is installed — `claude[bot]` has been posting all day.

---

## 4. Close issue creation to the public

Requested state: Issues stay **enabled**; only collaborators may create them.
This is a **GraphQL-only** field — there is no REST equivalent, which is why the
cloud session could not do it.

```bash
# Read first
gh api graphql -f query='
{ repository(owner:"hleserg", name:"FireflyOS") {
    id hasIssuesEnabled issueCreationPolicy viewerCanCreateIssues
    hasDiscussionsEnabled } }'

# Then set, using the id from above
gh api graphql -f query='
mutation($id:ID!) {
  updateRepository(input:{repositoryId:$id, issueCreationPolicy:COLLABORATORS_ONLY}) {
    repository { issueCreationPolicy viewerCanCreateIssues } } }' -f id='<repository.id>'
```

Re-read afterwards and confirm both:

```
issueCreationPolicy == COLLABORATORS_ONLY
viewerCanCreateIssues == true
```

Known good already, from REST: `has_issues: true`, `has_discussions: true`,
`visibility: public`.

**Do not** disable Issues. **Do not** use repository interaction limits — a
different, temporary mechanism that expires and restricts more than issue
creation. **Do not** change anything about pull request creation.

---

## 5. Check the Discussions categories

Discussions are enabled (REST confirms it). Which categories exist could not be
read, because that is GraphQL too.

```bash
gh api graphql -f query='
{ repository(owner:"hleserg", name:"FireflyOS") {
    discussionCategories(first:30) {
      nodes { id name slug description emoji isAnswerable } } } }'
```

The form committed at `.github/DISCUSSION_TEMPLATE/ideas.yml` binds to the
category whose **slug is `ideas`**. If the default `Ideas` category exists,
nothing to do.

If it does not: **creating a discussion category is not in the GitHub API at
all**, in REST or GraphQL. That one is genuinely a settings click —
`Settings → Discussions → Categories → New category`, named `Ideas` so the slug
comes out as `ideas`. Do not reach for undocumented endpoints; there is no
supported programmatic route.

---

## 6. Post the seed discussions

Three are written and committed, filled in exactly against the form a visitor
will see, English first and Russian below:

- `docs/community/seed-discussions/1-offline-friend-location.md`
- `docs/community/seed-discussions/2-find-my-camp.md`
- `docs/community/seed-discussions/3-group-radar.md`

Each file's first lines give its title and category. `createDiscussion` is
GraphQL-only, hence this handoff.

```bash
REPO_ID=$(gh api graphql -f query='{repository(owner:"hleserg",name:"FireflyOS"){id}}' --jq .data.repository.id)
CAT_ID=$(gh api graphql -f query='{repository(owner:"hleserg",name:"FireflyOS"){discussionCategories(first:30){nodes{id slug}}}}' \
           --jq '.data.repository.discussionCategories.nodes[]|select(.slug=="ideas")|.id')

# per file: strip the four header lines, post the rest as the body
gh api graphql -F repositoryId="$REPO_ID" -F categoryId="$CAT_ID" \
  -F title='Offline friend location — find your group without cell service / Координаты друзей офлайн — найти свою группу без мобильной связи' \
  -F body=@<(tail -n +6 docs/community/seed-discussions/1-offline-friend-location.md) \
  -f query='mutation($repositoryId:ID!,$categoryId:ID!,$title:String!,$body:String!){
    createDiscussion(input:{repositoryId:$repositoryId,categoryId:$categoryId,title:$title,body:$body}){
      discussion{number url} } }'
```

Check the rendering of the first one before posting the other two. Afterwards
the source files can be deleted or kept as a style reference — they are the
seed, not a second copy.

Three is the intended number. A fourth is allowed only if it shows a genuinely
different scenario; do not pad the section.

---

## 7. Verify the merged fix actually works

**#9 is merged** (`b1a3dca`, 2026-08-21) and so is #11. Everything is on `main`.

An earlier draft of this file said `pull_request` events run the workflow file
from the default branch. **That was wrong and is corrected here**, because this
repository's own record runs the other way: `docs/automation/CI_AND_REVIEW_PIPELINE.md`
records, as an observed result, that the action refuses to run when the workflow
file differs from the default branch's copy — a guard that would be meaningless
if `pull_request` did not source the workflow from the pull request's own ref.
`pull_request_target` is the one that runs from the base branch, which is exactly
why `claude-pr-review.yml` avoids it.

`issues` and `issue_comment` **do** run from the default branch, and those are
the triggers that matter for item 7.

The single most important fix in it was that **every Claude step was running with
no tools at all**. Agent mode grants no default `--allowedTools`, and the headless
SDK denies anything that would prompt, silently. That is why the reviewer ran 41 s
and posted nothing, and why the agent on issue #5 finished green with no branch
and no pull request.

**The reviewer half is now observed working.** On
[#11](https://github.com/hleserg/FireflyOS/pull/11) the independent reviewer
posted a full review carrying `<!-- firefly-ai-review -->` and set
`ai-review:blocking` — the first time that has happened in this repository.

**The writer half has not been.** No agent run has yet produced a branch or a
pull request. That is the job:

```bash
gh issue comment 10 --repo hleserg/FireflyOS --body "@claude"
gh run watch   # or: gh run list --workflow=claude-agent.yml --limit 3
```

Issue #10's task is real and unfinished — three workflows depend on
`anthropics/claude-code-action` and `docs/research/REUSE_LEDGER.md` has no record
of it, which `CLAUDE.md` requires. So this is a genuine first task, not a test
fixture.

Watch for:

- [ ] a branch `claude/issue-10-*` appears;
- [ ] a **draft pull request** appears, whose body carries a closing reference
      back to this issue (the `Fixes` keyword and the issue number);
- [ ] the independent reviewer posts a comment on it carrying
      `<!-- firefly-ai-review -->`, and sets exactly one of `ai-review:pass` or
      `ai-review:blocking`.

The third one now depends on a fix made after #11: the agent opens its pull
request as `claude[bot]`, and the review workflow excluded every `[bot]` actor,
so it would have skipped the agent's own pull requests — the exact case it exists
for. `claude[bot]` is now exempted and every other bot still excluded.

**Two cases where the reviewer is silent by design, so the silence is not a
failure:** a pull request from a fork (no secrets, hence no credential), and a
pull request that edits `.github/workflows/claude-*.yml` (the action refuses to
run a version of itself that a pull request has modified). Issue #10 is neither.

If a branch and a PR appear, the loop closes end to end for the first time.

If the run still finishes green with nothing to show, the tool list is still
wrong — read the run's **Step Summary**, which now carries Claude's own report
(`display_report` is on), and check the `Run Claude` step for denied tools. Do not
re-run hoping for a different result.

> **A trap this document walked into once.** An earlier revision spelled that
> closing reference out literally, as an example of what the agent's *future*
> pull request should say. GitHub does not distinguish a quoted closing keyword
> from a real one: the text reached a commit message and a pull request body, and
> merging #11 closed issue #10 at the same second. The intake gate refuses a
> closed issue — `reject: issue is closed` — so the verification below would have
> done nothing, and looked like yet another silent failure with an entirely
> different cause. Issue #10 has been reopened. **Never write a live closing
> keyword into prose that describes one.**

**Note:** commenting `@claude` yourself works because you have write access. It
does not answer task 1 — that is still about ChatGPT's own account.

## Do not do these

- Do not disable Issues, or use interaction limits.
- Do not change pull request creation policy.
- Do not weaken `intake-decision.sh` beyond the constrained Option B above.
- Do not enable GitHub **step-debug logging** (`ACTIONS_STEP_DEBUG`) while the
  agent workflows are live. It overrides `show_full_output: "false"` and writes
  every tool result — which can contain tokens — into a world-readable log.
  Verified in the action's source, not assumed.
- Do not build anything that turns community Discussions into Issues
  automatically. The step across is a maintainer deciding an idea is actionable,
  and that is the whole reason there are two places.

## When you are done

Update `STATUS.md` and `docs/research/OPEN_QUESTIONS.md` — the producer-identity
question there is written as open and should record the answer once task 1 has
one. Same commit as the change it describes.
