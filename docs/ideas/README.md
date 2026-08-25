# The owner's idea catalogue

Two documents handed over by the owner on 2026-08-23 with one instruction:
*"на будущее — вот тебе список идей и задумок по улучшению позиции целиком.
потом надо будет все проработать."*

They are kept here **verbatim and unedited**, in the author's Russian, on the
same footing as the two specification documents: they are the owner's own text,
and correcting them would destroy the record of what was asked for.

| File | Sections | What it covers |
|---|---|---|
| [idei-i-dorabotki.md](idei-i-dorabotki.md) | A–L, S | Radio hardware and reception, deciding when the instrument is lying, track processing, differential corrections over LoRa, assisted start, map data, routing, phone and PWA, infrastructure on site, own data collection, interface, field diagnostics, web preparation of an area |
| [nosimye-ustroystva.md](nosimye-ustroystva.md) | M–R | The split node/watch architecture, the companion node, wearable interface, extra functions, what the newer measurements force a rethink of, and a stationary reference station |

## Status: not yet worked through

**Nothing here is a requirement yet, and nothing here has been checked.** The
catalogue is explicitly a catalogue — its own preamble says *"Ничего не
отброшено — отбор будет позже"* (nothing has been discarded; the selection comes
later). Several items carry the author's own ⚫ marker for *research task with an
unclear outcome*.

Three rules apply to anything taken from these files:

1. **A technical claim here is not a fact.** The same rule the specification
   states about itself applies with more force to a brainstorm. Anything acted
   on gets traced to a datasheet, a schematic for the specific board revision,
   or vendor source, and recorded in [`../research/`](../research/) — see
   [CLAUDE.md](../../CLAUDE.md).
2. **A product requirement here does not outrank
   [`master-prompt-final.md`](../master-prompt-final.md) or
   [OWNER_DECISIONS](../research/OWNER_DECISIONS.md)** until the owner says so.
   Where they disagree, the specification and the recorded decisions win, and
   the disagreement is worth raising rather than silently resolving.
3. **Working an item through means an issue, not a speculative commit.** The
   issue carries acceptance and blockers, with research done first. The
   catalogue is input to that, not a substitute for it.

Several sections overlap work already in flight — the split architecture in M is
the node/watch division the capability model was built around
([ADR-0001](../adr/0001-capability-model.md),
[ADR-0002](../adr/0002-companion-is-optional.md)), and parts of A and Q touch
questions already open in
[OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md). Check there before opening
anything new: solving the same finding twice is the failure the queue exists to
prevent.
