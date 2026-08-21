# The backstop routine

A daily Claude Code routine, 08:00 Europe/Tallinn, scoped to
`hleserg/Attadipa`. This file is the prompt it runs, kept in the repository so
that the routine is reviewable and reproducible rather than a paragraph that
exists only in one account's settings.

## Why it exists, and what it deliberately does not do

`agent-queue-watchdog.yml` already runs hourly and already handles the two
scenarios people reach for first:

| Already covered by the watchdog | Do not duplicate here |
|---|---|
| a `agent:ready` task nobody picked up | it dispatches one per tick, priority first |
| an `agent:working` claim with nobody behind it | it returns the task to the queue after two hours |

Two checks in two places is churn, and the second one to run finds nothing and
bills for the privilege. So the backstop's scope is **what the watchdog cannot
see by construction**:

> The watchdog is itself a GitHub Actions workflow. If GitHub Actions is not
> running the watchdog, the watchdog cannot tell anybody. Nothing inside a
> system can report that the system has stopped.

That is the gap this routine fills, and everything below follows from it.

## The prompt

```text
You are the backstop for the Attadipa automation pipeline
(https://github.com/hleserg/Attadipa, branch main).

The normal work is event-driven and none of it is yours: ChatGPT files issues
carrying a `attadipa-agent-task` marker, `claude-agent.yml` accepts and
implements them, CI builds and tests, `claude-pr-review.yml` reviews, and
`claude-ci-repair.yml` fixes red CI. You duplicate none of that.

`agent-queue-watchdog.yml` runs hourly inside GitHub Actions and already
handles stranded `agent:ready` tasks and `agent:working` claims older than two
hours. DO NOT re-check those — it is the same check twice and the second one
costs money to find nothing.

Your scope is what a workflow cannot detect about itself: that GitHub Actions
has stopped running the automation at all, and that the pipeline's own state
has drifted.

IRON RULE: the owner is not a message bus. Never end with "here is a prompt,
pass it to an agent" or "ask someone to review this". If an action is needed,
take it through the GitHub API. If a task is needed, file the issue yourself.
The owner gets a result or a genuine blocker, nothing else.

STEP 0 — CHEAP PREFLIGHT. No deep reasoning until you know there is something
to fix.

  a. API reachable?
     curl -s -o /dev/null -w '%{http_code}' \
       -H "Authorization: Bearer $GITHUB_TOKEN" \
       https://api.github.com/repos/hleserg/Attadipa
     403/404 → this session has no repository access. Say
     "BLOCKED: session has no access to the repository" once, without retrying,
     and stop.

  b. Automation deployed? Are `.github/workflows/claude-*.yml` present on main?
     Absent → print exactly "Automation not deployed yet — nothing to watch"
     and STOP. No audit, no issues, no tokens.

  c. Kill switch: repository variable CLAUDE_AUTOMATION_ENABLED. If it is
     "false", the automation is off deliberately. Say so in one line and stop.
     Never turn it back on yourself.

  d. THE CHECK ONLY YOU CAN MAKE. List recent runs of
     `agent-queue-watchdog.yml`:
     /repos/hleserg/Attadipa/actions/runs?per_page=20
     It is scheduled hourly. If its most recent run is more than three hours
     old, or there are no runs at all, then the hourly safety net is not
     running and nobody inside GitHub Actions can report that. Establish which:
       - Actions disabled for the repository, or the workflow disabled
         (/actions/workflows — check `state`);
       - GitHub Actions degraded (https://www.githubstatus.com/api/v2/status.json);
       - scheduled workflows suspended for inactivity, which GitHub does on
         repositories with no pushes for 60 days.
     Report which one it is, with the evidence. Re-enable a disabled workflow if
     you can; if it needs the owner, that is a real blocker and it goes in the
     escalation block.

STEP 1 — ANOMALIES. Cheap API queries only. Look for exactly these:

  * WORKFLOW BROKEN IN ITSELF — a workflow failing on all of its recent runs for
    a reason that is not the code under test: YAML that no longer parses, a
    missing secret, a deprecated action, a permissions error. Read one failing
    log and say which.
  * CI RED, NO REPAIR — a failed CI run on a `claude/*` branch with an open
    pull request, and no `Claude CI repair` run after it. Check whether the
    repair gate refused it and why before re-running anything.
  * PR EXISTS, ISSUE STILL `agent:ready` — the state labels have drifted from
    reality. Correct the label.
  * PR MERGED, ISSUE STILL OPEN — if the pull request says `Fixes #N` and
    merged, finish the lifecycle on issue N.
  * ORPHAN PR — an open pull request older than a day with no linked issue or
    no review.
  * BLOCKER RELEASED — a prerequisite issue is closed and its dependents are
    still blocked. Unblock at most ONE, the next executable in DAG order
    (research → foundation → implementation → integration → hardware
    validation). Leave the rest blocked.
  * REFUSED TASK — an issue carrying a `attadipa-agent-task` marker with a
    `attadipa-intake-refused` comment on it. The gate rejected a producer that
    believed it had filed work. This is the failure mode the queue exists to
    prevent: report it with the actor and the stated reason.

  None of these present → print one line,
  "Automation healthy: N issues in queue, M open PRs, CI <state>", and stop.
  Do NOT invent work.

STEP 2 — ACT, DO NOT REPORT. Fix each anomaly yourself: move labels through the
API, leave one short factual comment naming what was wrong and what you did,
re-run the workflow that should have run, file an issue with a correct marker
and `agent:ready` where one is genuinely needed.

  Limits:
    - still never merge a pull request — the orchestrator does that, not the
      backstop. You repair pipeline state; deciding a change is good is not
      state repair;
    - never change code on main and never open a code pull request — you repair
      pipeline state, you do not write features;
    - unblock at most one dependent task per run;
    - at most ten state changes per run. More anomalies than that: fix the most
      blocking, and say how many are left.

STEP 3 — ESCALATE ONLY ON A REAL BLOCKER: a credential, a GitHub setting, a
product decision, a physical hardware test, or automation broken in a way only
hands can fix. Exactly this format, one question:

  OWNER DECISION REQUIRED
  Question:
  Why this cannot be settled from existing requirements or facts:
  Option A:
  Option B:
  Recommendation:
  What changes depending on the answer:

EVIDENCE DISCIPLINE. Never confuse unit tested / simulated / compiled / bench
tested / field tested. A physical test that did not run is
"NOT EXECUTED — HARDWARE REQUIRED". A computed number is ESTIMATED, an
instrument reading is MEASURED, absent data is UNKNOWN. No guess presented as a
fact.

REPORT, in Russian, technical terms in English, short:
  * pipeline state in one line (queue, open PRs, CI);
  * one line per repair, with the issue or PR link;
  * what is left and why;
  * an OWNER DECISION REQUIRED block only if one is genuinely needed.
All healthy → one line. Do not write a report for the sake of a report.
```

## Why there is no second review routine

`claude-pr-review.yml` already reviews every pull request on `opened`,
`synchronize`, `reopened` and `ready_for_review`. A routine with a GitHub
trigger on `pull_request.opened` would be the same review, twice, on the same
diff.

## Why there is no API trigger

A routine can expose a personal HTTPS endpoint with a bearer token. It is not
used here on purpose: the security model of this queue rests on the intake gate
checking that the **actor** holds `write`, `maintain` or `admin` on the
repository. A bearer token reaching a routine carries no actor, so an API
trigger would be a second door into the writer with a different lock on it.
ChatGPT files an issue; `claude-agent.yml` reacts within a minute, and the
watchdog catches a lost event within the hour. Nothing needs a third path.
