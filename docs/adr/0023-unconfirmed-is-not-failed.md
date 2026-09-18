# 0023 — A message nobody acknowledged is unconfirmed, and this product will not call it failed

Status: **accepted**
Date: 2026-09-14

Rests on [OUTBOUND_MESHCORE_MESSAGES](../research/OUTBOUND_MESHCORE_MESSAGES.md),
which is the evidence for every claim below, and on
[ADR-0008](0008-mesh-service-providers.md), which owns the service whose
vocabulary this changes. Decided by research on
[#552](https://github.com/hleserg/Attadipa/issues/552); the implementation is a
separate issue and **no production code changed with this ADR**.

## Context

`MeshDelivery` has five values and `MeshStatus` has one slot to hold them:
`core/include/attadipa/core/mesh_service.h:121` — "MeshDelivery delivery = MeshDelivery::None;".
One of the five is `Failed`, and it is written when an acknowledgement budget
expires: `link/src/meshcore_companion.cpp:324` — "status_.delivery = core::MeshDelivery::Failed;".
It reaches the owner as *"failed"* in English and, in Russian, as
*"не доставлено"* — **not delivered**, a claim about what happened on the air.

The wire cannot carry that claim. At the pinned MeshCore revision a companion
client receives exactly two outbound delivery events — the node accepted the
message, and a matching acknowledgement came back — and **no third event that
means anything negative**. `MyMesh::onSendTimeout()` is an empty function body;
the base class calls it when the send timer expires and then clears the timer.
Nothing is sent to the app, ever. Research report §2.4.

So "no confirmation arrived" is produced by two different worlds, and this
protocol does not distinguish them:

- the message never reached the recipient; or
- the message reached the recipient and the returning acknowledgement was lost.

The second is not hypothetical. Upstream
[issue #1834](https://github.com/meshcore-dev/MeshCore/issues/1834) has been open
since 2026-02-24 with six independent reports of exactly it, a MeshCore
contributor's diagnosis that the acknowledgement rather than the message was
lost, and a user asking — in April — for the word this ADR adopts.

Three further consequences of the same absence shaped the decision rather than
merely motivating it:

1. **A confirmation can arrive after the budget.** The node's
   `expected_ack_table` is cleared only on a match; nothing expires an entry. So
   the node can still prove delivery after this client has already published
   `Failed`, and the proof is dropped because the operation was ended.
2. **A confirmation that arrives while the link is down is lost outright.** The
   push is written with no connection guard, and the BLE transport drops
   everything while disconnected — the offline queue holds messages, not
   confirmations. Waiting does not recover it.
3. **A disconnect currently erases the verdict rather than qualifying it.**
   `link/src/meshcore_companion.cpp:180` — "status_.delivery = core::MeshDelivery::None;"
   runs in `reset_session()`, and `None` renders as *"not sent"*. A message the
   node accepted, and may have delivered, reads as one that never left.

## Decision

1. **`Failed` leaves the vocabulary.** Nothing observable on this protocol
   licenses it. The two frames that could carry a negative outcome are
   `RESP_CODE_ERR` *before* a send is accepted, which is a refusal, and nothing
   at all, which is not evidence of failure.
2. **An expired acknowledgement budget is `Unconfirmed` — once, and only once,
   the node has accepted the message.** It means: the node accepted this, and
   this product cannot tell whether it arrived. It is not a softer `Failed`; it
   is a different claim, and it is the strongest one the wire supports. The same
   budget covers two earlier phases — a room login outstanding, and a text
   awaiting `RESP_CODE_SENT` — where the node has answered nothing. Those expire
   to `Unknown` by decision 5's reasoning rather than to `Unconfirmed`: there is
   no acceptance there to be unsure about, and inventing one points the owner
   away from the resend that is safe.
   **2a. And a confirmation that arrives after the budget expired upgrades it
   to `Confirmed`**, for as long as the request it matches is still the session's
   current one. The node has no notion of this client's budget and pushes the
   confirmation whenever its own acknowledgement arrives, so a late match is
   ordinary traffic. A positive proof outranks the absence of one, and
   discarding it would leave the owner deciding whether to risk the duplicate
   decision 7 exists to prevent, about a message this client had since learned
   was delivered. The bound is the request rather than the clock: once the
   request has been replaced or the session has ended there is nothing for the
   match to attach to, and the tag of decision 8 can repeat, so a late ack is
   discarded there instead.
3. **A local refusal is not a delivery state.** "The link is down", "a send is
   already in flight", "the body is over budget", "the recipient does not
   resolve" are answers to the *call*. No message exists, so no message has a
   state. In particular `send_abandoned()` must stop writing a delivery verdict
   for a request that never became an operation.
4. **A verdict the node gave is `Refused`, and so is a frame this client
   could not put on the radio after it had already published `Queued`.**
   `RESP_CODE_ERR` for an accepted command, and a room login that failed, are
   the node declining — and are not statements about the radio.
   `ERR_CODE_TABLE_FULL` in particular must not be shown to an owner as "the
   node is full": it is also what a too-long text returns. The second half is
   the room path, which is one call in two phases: `send_room()` publishes
   `Queued` and returns `true` while the login is outstanding, and the text is
   enqueued from the login's answer, where a full transmit ring can still
   refuse it. Decision 3 does not reach that case — a message *was* published
   and an owner is looking at it — and without this clause removing `Failed`
   leaves the path with no terminal state at all, holding `Queued` for the rest
   of the session. From the owner's side `Refused` is exactly right: nothing
   reached the radio, and a resend cannot duplicate anything.
5. **A session that ends mid-flight yields `Unknown`, not `None`** — from the
   moment the request is made, not from `Accepted`. A frame that has left this
   client's ring may already have been written to the characteristic, and
   nothing distinguishes that from one still queued behind it. The
   difference between "never sent" and "sent, outcome unknowable" is the
   difference between an owner who will send again and one who must decide
   whether to risk a duplicate.
6. **`Confirmed` means a delivery acknowledgement and never a read.** The
   acknowledgement is generated by the recipient's *node*. Nothing in this
   protocol carries a read receipt and nothing in this product may imply one.
7. **No automatic retry, ever, at any layer.** A resend after a lost return
   acknowledgement delivers a second copy of a message that arrived, and nothing
   on this wire can tell the two apart. The workaround offered on #1834 — keep
   sending until an acknowledgement comes — causes the duplicate by
   construction. If a resend is offered it is a person's decision, made on a
   screen that says the first attempt may already have arrived.
8. **A result is carried against a local, non-zero request identifier, and that
   identifier is this product's own.** The node's four-byte acknowledgement tag
   is a keyed hash of timestamp, attempt and text; identical messages in the
   same second produce identical tags, and the recipient's key is not an input.
   It is a correlation hint that can repeat, and it is never a message id.

## Consequences

**The catalogue changes and one string is retired.** `mesh_delivery_failed`
loses its only producer. The two strings it is replaced by have to be written so
that neither reads as the other: *"delivered"* must not be reachable by anything
but a matching acknowledgement, and the `Unconfirmed` string must say what this
product does not know rather than apologise for it.

**The contract can be described without new persistent state.** One in-flight
request, one identifier, one result, cleared with the session. No chat database,
no history, no durable queue. Nothing is versioned, so nothing needs migrating.

**It does not change if MeshCore's proposed protocol v14 lands.**
[PR #2974](https://github.com/meshcore-dev/MeshCore/pull/2974) would add a local
transmit-status event, letting a client start its timer at transmission rather
than at acceptance. That makes `Unconfirmed` *narrower*; it adds no delivery
evidence and cannot turn it into `Failed`. That the app-facing vocabulary is
unaffected either way is the test of whether it was drawn in the right place.
The PR is open, `mergeable: false`, unanswered by any maintainer, and against a
branch the pinned fleet does not run — so it is monitored and depended on by
nothing.

**What this ADR does not decide.** Whether this product ever attaches a
coordinate to a message it sends — that is the owner's, per
[OD-30](../research/OWNER_DECISIONS.md), and the research report's §7 is the
recommendation for when it is asked. How a recipient outside the retained
sixteen is chosen in the interface. Whether the 15-second acknowledgement clamp
should be raised, which needs an airtime measurement this project does not have.
