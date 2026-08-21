# Handoff: what a local session must do, and why a cloud session could not

**Written:** 2026-08-21 · **By:** the Claude Code cloud session that produced
pull request #9 · **For:** a Claude Code session running on the owner's own
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

Run them in order. **1 and 2 are the ones that matter** — until 1 is done the
agent queue does not close, and everything else is cosmetic by comparison.

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

### The decision

| | **Option A — recommended** | Option B |
|---|---|---|
| ChatGPT files through | a user account with `write`/`maintain`/`admin` | a GitHub App |
| Change needed | none, works as built | widen the gate with an allowlist |
| Cost | one GitHub account | configuration on the one boundary the security model rests on |

**Option A is recommended and needs no code.** Confirm which account ChatGPT's
GitHub connector authenticates as, and give it write access:

```bash
gh api user                                    # ...as ChatGPT, to learn the login
gh api -X PUT repos/hleserg/FireflyOS/collaborators/<login> -f permission=push
```

Then verify by filing one issue *as ChatGPT* and watching what happens. Either an
agent starts, or a refusal comment names the actual actor. Either way the answer
lands on the issue — that is what PR #9's refusal-comment change is for.

**If Option B turns out to be forced** — ChatGPT can only file as an App — then
the gate needs an allowlist, and it must be built to these constraints, which
are not negotiable:

- a new repository variable `FIREFLY_TRUSTED_PRODUCERS`, **empty by default**;
- it applies to **`issues` events only, never comments**. The loop this guards
  against lives in comments;
- `claude` and `github-actions` can **never** be listable, checked *after* the
  allowlist so the list cannot override them. Those are this repository's own
  output;
- `.github/tests/intake-gate-test.sh` gains cases for: an allowlisted app opening
  an issue (accept), the same app commenting (reject), `claude[bot]` present in
  the list anyway (reject), and an empty list behaving exactly as today (reject).
  It is currently 16/16; it must stay green and grow.

A cloud agent deliberately did **not** implement this. Widening the intake
gate's trust boundary is the owner's decision, and the recommendation above is A.

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

## 7. Review and merge pull request #9

<https://github.com/hleserg/FireflyOS/pull/9> — draft, `mergeable: true`, CI
green except where noted in its body.

It carries four fixes, three of which were found by running the pipeline rather
than by reading it:

1. **`--allowedTools` was missing from every Claude step.** This is the big one.
   Agent mode grants no tools by default and the headless SDK denies anything
   that would prompt — silently. That is why the reviewer ran 41 s and posted
   nothing, and why the agent on issue #5 finished green with no branch and no
   pull request. Both had read everything and had no way to say so.
2. **A claim that did not clear the other state labels**, so a retry left an
   issue on `agent:working` *and* `agent:review`, stuck forever, re-queued by the
   watchdog every two hours.
3. **`reviewed_head` was never checked** despite being in the protocol since the
   marker was defined.
4. **A refused task was silent**, which is what task 1 above is about.

**The `--allowedTools` fix cannot be verified until it is merged** — `issues` and
`pull_request` events run the workflow file from the default branch. After
merging, confirm on the next agent run that a branch and a pull request actually
appear, and that the reviewer leaves a comment carrying `<!-- firefly-ai-review -->`.

Issue #10's task — the missing reuse-ledger record for
`anthropics/claude-code-action`, which three workflows depend on — is real and
still open. Once task 1 is done, comment `@claude` on it and it becomes the
first end-to-end run of the repaired loop.

---

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
