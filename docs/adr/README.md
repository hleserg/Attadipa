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
| [0001](0001-capability-model.md) | Capability model: presence, variant, degree, availability | proposed — amended by 0004 |
| [0002](0002-companion-is-optional.md) | The phone companion is optional, and the watch never depends on it | proposed — scope corrected by 0004 |
| [0004](0004-capability-sources.md) | Where a capability comes from, and what happens when it leaves | proposed |

0003 is reserved for the radio abstraction across the five possible T-Watch
chips ([TASKS](../../TASKS.md) T-013). It is blocked on reading MeshCore, not on
a decision — writing it before knowing whether MeshCore assumes exclusive
ownership of the radio would be guessing.

An ADR that amends another does not replace it. 0001 keeps its reasoning and
carries an amendment notice; 0004 carries the enum that is actually in force.
Deleting the superseded text would delete the record of why the first answer
looked right, which is the part a later reader needs most.
