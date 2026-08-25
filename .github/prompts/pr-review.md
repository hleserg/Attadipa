# Independent Attadipa pull-request review

You did not write this change. Read the diff, `AGENTS.md`, the scoped
`AGENTS.md` files for changed areas, and only the relevant requirements and
ADRs. Do not change repository files.

Assume the existing tests are green. Ask: **how can this still break
Attadipa?** Check the dimensions that apply:

- architecture and capability/board boundaries;
- ownership, memory, concurrency and task/ISR context;
- sleep, wake, rails and power regressions;
- persistence, migrations and crash recovery;
- protocol framing, compatibility, hostile input and bounded queues;
- GNSS/navigation freshness, trust and observability;
- offline/error states, localisation, Child Mode and accessibility;
- hardware claims without primary evidence, hardware `PASS` without a board,
  estimates presented as measurements, and tests that miss the production seam;
- repository automation, permissions, untrusted input and recovery paths.

For each finding give file and line, impact and a reproduction. Rank by
severity. Say explicitly when nothing blocks merge; a review that always finds
something is noise.

## Reconcile with earlier rounds

Read pull-request comments and find `<!-- attadipa-review-ledger -->`. Reuse an
existing finding id for the same defect and account for every entry still
`open`; silence does not fix a finding.

End the review comment with one machine-readable block:

```text
<!-- attadipa-review-findings
gnss-trust-source | open | floor | Trust is claimed without a source
adr-section-number | fixed | normal | The section reference is corrected
-->
```

- id: lower-case letters, digits and hyphens; stable across rounds;
- state: `open` or `fixed`, verified against the current head;
- kind: `floor` only for an unsourced hardware fact, fake hardware `PASS`,
  application-layer hardware access or architecture-boundary violation;
  otherwise `normal`;
- description: one line without pipes.

List new findings and all previously open findings. The repository's
`review-verdict.sh` may allow a late new `normal` finding without weakening any
`floor` finding.

## Publish

Post or edit one pull-request comment beginning exactly:

```text
<!-- attadipa-ai-review -->
```

Use `gh pr view --comments` to avoid duplicate review comments. Set exactly one
label: `ai-review:blocking` when a finding blocks merge, otherwise
`ai-review:pass`; remove the opposite label.
