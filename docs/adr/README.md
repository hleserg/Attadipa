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
| [0001](0001-capability-model.md) | Capability model: presence, variant, degree, availability | **superseded by 0007** |
| [0002](0002-companion-is-optional.md) | The phone companion is optional, and the watch never depends on it | **accepted** — scope corrected by 0004 |
| [0003](0003-radio-not-lora.md) | The part is a `Radio`. Whether it can do LoRa is a fact about it | **accepted** |
| [0004](0004-capability-sources.md) | Where a capability comes from, and what happens when it leaves | **accepted** |
| [0005](0005-node-protocol.md) | The watch↔node protocol | **provisional** — encoding pending benchmark; three further corrections open |
| [0006](0006-settings-and-bounded-values.md) | Settings, and values the law bounds | **accepted** |
| [0007](0007-two-capability-layers.md) | Two capability layers, and the end of `has()` | **accepted** |
| [0008](0008-mesh-service-providers.md) | One `MeshService`, two providers, and the local path is real | **accepted** for the shape; the local mechanism is an open spike |
| [0009](0009-heading.md) | Heading is three quantities, and one of them belongs to a different body | **accepted** |
| [0010](0010-localization.md) | English and Russian from the first screen | **accepted** |
| [0011](0011-gnss-integrity.md) | GNSS integrity: the receiver's own defences, and a trust state with reasons | **accepted** |
| [0012](0012-project-name-attadipa.md) | Project name is Attadipa | **accepted** |
| [0013](0013-node-motion.md) | The node's IMU is a power decision, and motion belongs to a body | **accepted** |

### What the statuses mean here

Final §74: *"Do not build dozens of layers on an allegedly provisional decision
while treating it as immutable. Before M1 core APIs rely on a decision, accept
it or state why it remains intentionally provisional."*

Everything M1 depends on is now **accepted**. Two documents are deliberately not:

- **0005** is `provisional` because its encoding choice compared a hypothetical
  Attadipa TLV against Meshtastic's entire `FromRadio` union, which is not a
  comparison. It stays provisional until benchmarked (T-016). Its *goals* —
  versioning, bounded parser, session reset, fragmentation, hostile-frame
  corpus — are endorsed by final §18 and are not in question.
- **0008** accepts the service shape and explicitly refuses to decide the local
  MeshCore integration mechanism, because final §14 forbids choosing between the
  options without a measured spike, and this project has already made that
  mistake once.

An ADR that amends another does not replace it. 0001 keeps its reasoning and
carries a supersession notice; 0007 carries the model that is actually in force.
Deleting the superseded text would delete the record of why the first answer
looked right, which is the part a later reader needs most.

### 0003, and the sentence that used to be here

This index said, for a day:

> *"the watch does not run mesh, because the radio is in the node"*

It was wrong, it was in the index rather than in an ADR where it would have been
argued, and final §13 corrects it. The reasoning behind it was sound as far as it
went — MeshCore keeps radio state in a file-static variable set from an ISR
(OPEN_QUESTIONS M9), so one image drives one radio; and a node with the radio in
it makes that stop mattering. The error was concluding that because the problem
disappears on *one* path, the other path does not exist.

0003 is now written, and reading MeshCore changed what it had to say. It was
reserved for "one radio interface across five chips". It turns out that at the
pinned revision MeshCore supports exactly **one** of those five, two of them
have no LoRa modulator at all, and there is no T-Watch variant upstream. So 0003
is less about abstracting five drivers than about not claiming a mesh the
hardware cannot join. [ADR-0008](0008-mesh-service-providers.md) carries the
service that consumes it.
