# 0022 — A finished contact stream and a true contact snapshot are two observations

Status: **accepted**
Date: 2026-09-14

Rests on [MESHCORE_CONTACT_SNAPSHOT_CONSISTENCY](../research/MESHCORE_CONTACT_SNAPSHOT_CONSISTENCY.md),
which is the evidence for every claim below, and on
[ADR-0008](0008-mesh-service-providers.md), which owns the service this decision
changes the vocabulary of. Decided by research on
[#563](https://github.com/hleserg/Attadipa/issues/563); the implementation is a
separate issue and no production code changed with this ADR.

## Context

`CMD_GET_CONTACTS` is answered by a stream the node emits one frame per `loop()`
pass, while the packet callbacks in the same `loop()` write unsolicited pushes to
the same transport. The node's contact iterator is a raw index into the live
`contacts[]` array, so the table can be appended to, rewritten in place, or
compacted underneath the cursor while the walk runs — research report §2.

Attadipa converts the end of that stream into a claim about its content:
`link/src/meshcore_companion.cpp:1506` — "status_.peers_complete = true;" is set
unconditionally on `RESP_CODE_END_OF_CONTACTS`. The stream ending is a syntactic
fact. That the list matches the node's table is a semantic one, and the wire does
not carry it.

Three findings shaped the decision rather than merely motivating it:

1. **Not every invalidating mutation has a push.** `CMD_REMOVE_CONTACT` compacts
   the array and answers `writeOKFrame()`; a contact that shifts into an
   already-consumed slot is skipped with nothing announced to anybody. So a
   push-driven client is structurally incomplete, and the contract must promise
   only what it saw — research report §3.2.
2. **Two of the push codes that look invalidating are not.** `0x8A NEW_ADVERT`
   announces a contact the node did **not** store, and `0x90 CONTACTS_FULL` is
   raised exactly where the allocation returned `NULL` — nothing stored, nothing
   overwritten, table unchanged. Upstream's own patch comment says otherwise about
   `0x90`, and it is wrong at both revisions read — §3.1.
3. **`peers_complete` already has a consumer, and that consumer wants
   "finished".** `apps/src/mesh.cpp:284` — "if (status.peers_complete && retained < reported) {"
   gates the `retained/reported` pair on it, because the two numbers are only
   comparable once the iteration has ended.

## Decision

**1. `peers_complete` keeps its present meaning: the stream finished.** It is not
repurposed. Its consumer needs exactly that, and a flag whose name fits both
readings is the worst possible place to change one silently.

**1a. Since #567, "finished" also means "went quiet".** A stream that stops for
three seconds is ended by a sweep in `tick()`, because a bounded transport queue
drops the tail of a burst and `RESP_CODE_END_OF_CONTACTS` is systematically the
frame it loses — `link/src/meshcore_companion.cpp:528` — "            status_.peers_complete = true;".
That is a third rung below the one this ADR is about, not a competing answer to
it: *the node stopped sending* is weaker than *the node said it was done*, which
is weaker than *this is the node's list*. A walk closed by the sweep with no
invalidating push inside it is **consistent** — a lost boundary frame is not
evidence the table moved.

**1b. That last sentence is about a first walk, and a re-read is not one.** A
first walk the sweep closes publishes whatever it staged, because that is the
only list there is. A re-read has decision 7's proven list standing behind it,
and needs the node's own `RESP_CODE_END_OF_CONTACTS` before it may replace it:
swept, it abandons what it staged and the snapshot goes back to being dirty —
another attempt, then `degraded` with the older list. That is not a second
rule. It is decision 7 read on the one end the sweep can produce, and the
implementation that read it the other way is
[#586](https://github.com/hleserg/Attadipa/issues/586).

**2. Snapshot consistency is a separate observation**, carried alongside it:
consistent, dirty, retry pending, or degraded. A snapshot is *consistent* when the
stream finished and no invalidating push arrived inside it, and *dirty* when one
did.

**3. The invalidating codes are `0x80`, `0x81`, `0x8D` and `0x8F`, and only
between `START` and `END`.** `0x8A`, `0x90`, `0x82` and `0x83` are not: `0x83`
moves `lastmod` alone, and letting an ordinary incoming message cancel a sync
would be a self-inflicted outage. The same code outside the stream is staleness,
which a re-read does not repair.

**4. Every one of those codes stops being counted as a malformed frame.** A push
this build understands and deliberately does not act on is not a parse failure,
and putting protocol-correct traffic in the parser-fault counter destroys the
evidence anybody debugging this will need. The `default:` arm's refusal to tear
the link down is unchanged.

**5. `Availability::Ready` stays gated on the stream finishing, never on
consistency.** Gating it on consistency would make contact churn look like an
unreachable mesh and would re-create, one layer up, the defect §6c of
[MESHCORE_T114_FIRST_CONTACT](../research/MESHCORE_T114_FIRST_CONTACT.md)
measured — a room login that landed mid-sync dropping its message.

**6. A dirty snapshot is re-read at most twice per session, after `END` and never
during a stream.** A second `CMD_GET_CONTACTS` while the node is iterating is
refused `ERR_CODE_BAD_STATE`, and that error carries no correlation field, so it
would be charged to whatever command is still owed an answer and would fail an
innocent send.

**7. While a re-read is pending the last proven snapshot is what is published**,
and when the budget is spent the newest snapshot *the node ended* is published as
degraded rather than withheld — which may be the one that was already standing,
because a re-read the quiet sweep closed produces no snapshot at all (1b). A peer list that is probably right serves the wearer; one that claims to
be proven and is not does not.

**7a. Publishing it costs a shadow copy and a latch, and that is part of this
decision, not an implementation detail.** A re-read opens with a second
`RESP_CODE_CONTACTS_START`, whose handler empties the retained set and clears
both completion flags — `link/src/meshcore_companion.cpp:1425` — "        peer_count_ = 0;".
Left alone, a retry therefore drops `Availability::Ready`, closes the battery
poll gate, restarts the `retained/reported` pair at zero, and — the one that
matters — leaves an incoming message with no sender name, because
`find_peer_prefix()` walks only the live count. Decision 7 is implemented by
holding sixteen slots of the retained set across the re-read and latching the
flags until it completes or is abandoned; an implementation without that has not
implemented decision 7. The same latch covers the message drain, which
`end_contacts()` arms and which a second walk would otherwise arm again.

**8. Retention truncation, inconsistency and staleness stay three observations.**
A snapshot can be atomic and deliberately truncated at the watch's 16 slots,
full-capacity and torn, both, or neither.

**9. Upstream [MeshCore #3403](https://github.com/meshcore-dev/MeshCore/pull/3403)
is `ADAPT`, not a dependency.** It holds pushes in a bounded FIFO until the
response ends. It does not make a snapshot consistent — it reorders the telling,
not the doing — a full FIFO drops the very push the client detects dirt with, and
a deferred push is byte-identical to a fresh one, so the client cannot tell which
read it belongs to. The contract above must therefore behave correctly on a node
that has it and on one that does not.

## Alternatives considered

- **Repurpose `peers_complete` to mean "consistent".** One consumer exists and it
  needs "finished"; the change would be invisible at the call site and would alter
  the mesh face.
- **Keep setting it on `END` and ignore the pushes.** The defect itself.
- **Invalidate on any push.** `0x83` would let an incoming message cancel a sync;
  `0x8A` and `0x90` would discard reads that are correct.
- **Tear the session down on an unknown opcode, as a strict reader would.**
  Breaks forward compatibility and loses unrelated ACK and message events.
- **Re-read until clean.** A node under advert load never goes quiet: a live-lock
  with a radio attached.
- **Wait for #3403, or copy it.** Open, unmerged, unreleased, hardware-untested by
  its author, against `dev`; the fleet is pinned on `v1.17.1-d929643`; and it does
  not deliver consistency.
- **One flag for truncation and inconsistency.** Different causes, different
  remedies, different things to tell the wearer.

## Consequences

- A caller can, for the first time, distinguish *"the node finished talking"* from
  *"this is the node's contact list"*. #552's recipient resolution needs the
  second and would otherwise have inherited the first under a name that reads like
  it.
- The mesh face and `Availability` are unchanged by construction, which is what
  makes this implementable without a UI decision — and `snapshot_degraded` is
  therefore **exported and read by nobody on the face**. Telling the wearer a
  peer list is unproven is a UI state with a localised string and two panel
  geometries behind it; it is argued under `ui/AGENTS.md`, in its own issue,
  after this. The first real consumer of the observation is #552's recipient
  resolution, which refuses rather than displays.
- The client gains a bounded retry, and therefore a new way to spend commands and
  radio time. The cost in latency and power is unmeasured and stays `UNKNOWN`
  until someone measures it.
- **On the one node this project has measured, the upper bound of that cost is
  the normal case, not the worst one.** Decision 1b lets a re-read commit only
  on the node's own `RESP_CODE_END_OF_CONTACTS`, and that is the frame the
  measured bench drops — `firmware/main/meshcore_ble.cpp:1125` — "            // sessions out of three -- it is the last frame of the burst, so it"
  — because it is the last frame of a 234-frame burst and the overrun reaches
  the end of a burst first. A re-read is the same burst again. So on that node
  a dirty walk spends attempt one, spends attempt two, and ends `degraded` with
  the list it already had, every time, by construction: both attempts are
  always spent and neither can ever commit. **Inferred, not measured** — the
  bench dropped that frame on a *first* walk on 2026-09-14, four days before a
  re-read existed, and no session has yet run one. The burst is the same burst,
  which is why the inference is worth recording; it is still an inference. Before 1b the sweep committed and
  a session usually stopped after one.

  The stale list is still the safe direction and this does not change it. What
  is recorded here is the price, so that the next reader finds it written down
  rather than derives it: decision 6's second `CMD_GET_CONTACTS` always goes
  out and always re-streams ~234 frames through the queue that is dropping
  them. It is a reason to raise `kSnapshotRetries` toward zero rather than away
  from it, and a reason the airtime above is `UNKNOWN` about a case that is
  routine rather than rare.
- Detection remains incomplete on purpose: a second client's
  `CMD_REMOVE_CONTACT` or `CMD_ADD_UPDATE_CONTACT` mutates the table silently, and
  no client-side contract can see it. What is promised is that nothing is ever
  claimed beyond what was observed.
- Nothing here is confirmed on hardware. The replay matrix is host-only and the
  bench plan is **NOT EXECUTED — HARDWARE REQUIRED**.
