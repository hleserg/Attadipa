# 0013 — Motion evidence belongs to a physical body

Status: **accepted** — [ADR-0019](0019-confirmed-companion-body.md) adds one application-level exception on top of it, a companion the wearer has confirmed is on their body: the mapping below is unchanged and so are its two consumers, the trust engine and GNSS power gating; what a navigation readout may do with a `Node`-body coordinate is decided there rather than here
Date: 2026-08-25

Extends [ADR-0009](0009-heading.md) and [ADR-0011](0011-gnss-integrity.md).

## Context

The watch and an Attadipa node can each have a receiver and motion sensor. A
motion sample shaped only as `{known, moving}` allowed evidence measured on a
watch wrist to gate or judge a node receiver. Both readings can be correct:
the watch can be still while the node moves in a bag, and the inverse can be
true. Treating them as one body either reports a false motion disagreement or
misses a real one.

The old GNSS power context had the related failure: `false` meant both "not
moving" and "no sample", so it could put a receiver to sleep without evidence.

## Decision

- `MotionEvidence` names its `SensorBody`: `Watch`, `Node`, `Companion`, or
  `Unknown`.
- A consumer asks whether evidence speaks for the body it is deciding about.
  Unknown-body and other-body evidence are neutral, not an approximation.
- `PositionSource` maps local GNSS to `Watch`, node GNSS to `Node`, companion
  positions to `Companion`; manual, simulated, and unknown sources name no
  body.
- Trust's `MotionDisagreement` detector uses only same-body stillness.
- GNSS power gating uses only known same-body rest or motion. Unknown evidence
  moves neither direction; a future motion producer must expire stale stillness
  before presenting it as known.

This is not an application capability and does not add a motion service. It is
provenance for the existing trust and receiver-power decisions.

## Consequences

The node's motion can optimise its own receiver without becoming wearer motion,
and the watch cannot make a decision about hardware on another body. Tests keep
both directions explicit: a wrist sample does not affect node GNSS, while a
node's own sample still does.
