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

**Applying one is step 8 of
[HANDOFF_LOCAL_CODER](../HANDOFF_LOCAL_CODER.md#8-land-the-workflow-half-of-a-change-a-cloud-session-could-not-push).**
It is three commands from a local session with the owner's own `gh` login.

**Delete the patch in the same commit that applies it.** A patch that outlives
its application is a second copy of a workflow file, and the second copy is the
one that goes stale — silently, because nothing lints it and nothing runs it.

This directory being empty is the normal state. If it is not empty, something is
waiting on a person.

| Patch | For | Written |
|---|---|---|
| `74-watchdog-conflicts-job.patch` | [#74](https://github.com/hleserg/Attadipa/issues/74) — the watchdog's `conflicts` job, plus its test's line in `ci.yml` | 2026-08-23 |
