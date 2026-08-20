# Architecture Decision Records

One decision per file, named `NNNN-short-title.md`, numbered in order.

Write an ADR when a decision is hard to reverse, when it constrains other
work, or when research shows the specification proposes something that does not
survive contact with the hardware. The specification explicitly permits
changing implementation details on evidence; product requirements need an
explicit reason.

## Template

```markdown
# NNNN — Title

Status: proposed | accepted | superseded by NNNN
Date: YYYY-MM-DD

## Context
What forced the decision. Cite evidence — facts from docs/research/, not
recollection.

## Decision
What was decided, stated plainly.

## Alternatives considered
Each with why it lost. An ADR with no rejected alternatives did not decide
anything.

## Consequences
What this makes easier, what it makes harder, and what it commits us to.
```

## Index

| # | Title | Status |
|---|---|---|
| 0001 | Capability model — variant and degree | planned (TASKS T-002) |
