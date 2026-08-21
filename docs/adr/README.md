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
| [0005](0005-node-protocol.md) | The watch↔node protocol | proposed |
| [0006](0006-settings-and-bounded-values.md) | Settings, and values the law bounds | proposed |
| [0010](0010-localization.md) | English and Russian from the first screen | **accepted** |

0003 was reserved for the radio abstraction across the five possible T-Watch
chips ([TASKS](../../TASKS.md) T-013), blocked on reading MeshCore. MeshCore has
now been read, and the answer changed the question: its RadioLib wrapper keeps
radio state in a file-static variable set from an ISR, so one firmware image
drives one radio. That would have been a problem for a watch running mesh — and
the watch does not run mesh, because the radio is in the node. 0003 is still
needed, but it is now about the *node's* radio and about the five chips only in
so far as a T-Watch with its own LoRa is also a valid configuration. See
OPEN_QUESTIONS M9.

An ADR that amends another does not replace it. 0001 keeps its reasoning and
carries an amendment notice; 0004 carries the enum that is actually in force.
Deleting the superseded text would delete the record of why the first answer
looked right, which is the part a later reader needs most.
