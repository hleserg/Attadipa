# GitHub automation

- Keep workflow YAML declarative: trigger, permissions, checkout, call a
  repository script, publish the result. Put reusable decisions in
  `.github/scripts/` and execute that shipping script directly in tests.
- A test must cross the seam where production can fail. Do not award `PASS`
  from a fixture, copied implementation, parked patch or generated post-image.
- Use least privilege, pin third-party actions, treat issue/PR text as hostile
  input, and never execute untrusted pull-request code with write credentials.
- Labels are views of GitHub state, not an independent ledger. Prefer the
  platform's issue, PR, review and check state before adding a label or marker.
- Workflow files require owner-capable credentials. Do not weaken that boundary
  and do not build a permanent patch-management subsystem around it; keep rare
  workflow-only handoffs explicit and implement/test the script portion first.

Security and recovery details are in `docs/automation/`. Update those documents
only when the operating contract changes, not for each run or task.
