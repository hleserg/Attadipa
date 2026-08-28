# 0015 — A transport session has one owner, a generation, and no lifecycle queue

Status: **accepted**
Date: 2026-08-28
Constrains: [ADR-0005](0005-node-protocol.md) §5 · [ADR-0008](0008-mesh-service-providers.md) §5

## Context

The MeshCore Companion transport is the first thing Attadipa runs that has two
execution contexts touching one link. On the ESP32-S3, `nimble_port_freertos_init()`
starts the NimBLE host in its own FreeRTOS task and `firmware/main/meshcore_ble.cpp`
creates a second task to drive the node protocol; ESP32-S3 is dual-core SMP, so
the two can run at the same instant on different cores.

Issue #317 traced two defects to that, both from source rather than from a
bench:

1. **Two writers, no owner.** The connection handle, three GATT handles, the
   negotiated MTU, `gatt_ready` and `write_in_flight` were plain globals written
   by NimBLE callbacks and read by the worker between its guard and its write. A
   disconnect between those two points left the worker writing to a handle that
   had been cleared, and a write completion belonging to a torn-down connection
   cleared the in-flight flag of a live one — which is a transport that never
   sends again until it is rebooted.

2. **The lifecycle travelled as data.** One 48-deep queue carried both bulk
   Companion frames and the session lifecycle. Dropping a frame under
   backpressure is deliberate and safe, and is
   [MEASURED](../research/MESHCORE_BLE_FRAME_CAPACITY.md) — a contact burst
   arrives faster than the worker runs, and the node re-sends a contact record.
   Dropping the *disconnect* is not safe, and `post()` was zero-wait with its
   result discarded, so a burst that filled the queue could silently delete it.

Neither is a hardware claim and neither needed one: both are properties of the
source. How often they fire on a board is **UNKNOWN**, and the stress that would
measure it is **NOT EXECUTED — HARDWARE REQUIRED**.

## Decision

**1. One record, one lock, one owner.** A transport session is a single record
(`attadipa::link::SessionOwner`, `link/include/attadipa/link/session_owner.h`),
not a set of variables. Every context that touches it does so under the same
critical section, and reads copy the whole record in one piece. A reader
therefore sees a session that existed, rather than fields that were each
individually current.

**2. Every session has a generation, and stale generations are refused.** A
generation is allocated once when a session begins, is never reused, and is
never zero. Every fact recorded about a session names the generation it belongs
to; the owner applies nothing that names any other one. The generation rides to
the transport and back in the transport's own callback argument — for NimBLE,
`cb_arg` — so an answer always knows which question it belongs to without a
second lookup that would have to be raced for in its turn.

**3. No transport call is made under the lock.** The sequence is snapshot,
call, conditional commit: take a coherent copy, decide and call outside the
critical section, and commit only through a mutator that re-checks the
generation. This is what makes "the peer went away between the guard and the
write" a refused commit rather than a write to a dead handle, and it is also
what keeps a stack that can run a callback before its own call has returned from
deadlocking against us.

**4. The lifecycle is state, not a message.** Session transitions do not travel
through the frame queue, or any queue. The worker holds a mark of what it has
already applied, and `reconcile()` returns the ordered steps it still owes the
link model. A worker that missed three transitions is told where the session
actually got to. A queue may still be used as a doorbell to shorten the wait,
and losing that doorbell may cost latency and can never cost a transition.

**5. Bulk data stays droppable, and both losses are counted.** Frames may still
be dropped under backpressure, and a frame whose session ended before the worker
reached it is dropped rather than mixed into the next session's handshake.
Dropped frames and coalesced lifecycle transitions are counted and published, so
"this happened" is answerable without a debugger.

The rule is transport-agnostic and lives in `link/`, beside `LinkState`.
`LinkState::epoch()` is the same idea one layer up and stays where it is: it
stamps what the *link model* must discard on reconnect (ADR-0005 §5), and it is
owned entirely by the worker. The generation here is the cross-context one, and
only it can be safely compared inside a callback.

## Alternatives considered

**One atomic per field.** Removes each individual data race and leaves the
expensive one. Six atomics that are each current can still collectively describe
no connection that ever existed — a live `gatt_ready` with a cleared connection
handle, or a handle from the session before this one. Torn multi-field state and
ABA across connections are exactly the failures that cost the transport, and
atomics do not address either.

**The host task owns everything and the worker asks it to write.** This is the
cleanest ownership story and it was the issue's first preference. It was
rejected for cost, not correctness: the NimBLE host task's mailbox is
`nimble_port`'s, not ours, so it needs a second request path and a second
completion path, and the worker still has to reason about a session it cannot
see. The record-with-a-lock is the issue's stated second option, and it makes
the same guarantee with a fraction of the moving parts.

**A second, reserved queue for control events.** Bounded, so it can still fill;
fail-closed behaviour then has to be designed for a queue that is not supposed
to be able to fill, which is how the first version of this bug was written. It
also keeps the ordering problem: two queues have no order between them. Sticky
state has neither failure, because there is nothing to overflow.

**Pin both tasks to one core.** Removes simultaneity, not preemption, and does
nothing about compiler visibility. It also spends the second core to hide a bug
rather than fix it, on a device that will want it for the UI.

## Consequences

- Every future transport in `link/` — USB, UART, a Wi-Fi socket — gets the
  ownership rule for free, and gets it tested on a host with no radio.
  `tests/test_session_owner.cpp` covers the interleavings, including two real
  threads behind one lock so `-fsanitize=thread` has something to look at.
- A stale callback now does nothing instead of something wrong. That includes
  doing nothing visible, so the counters are not decoration: a link that is
  quietly discarding a generation's worth of callbacks looks healthy without
  them.
- Sessions the worker never learned had begun are folded into one disconnect
  rather than replayed. The link model's session count can therefore undercount
  under sustained starvation; the amount is reported as `coalesced`, and the
  alternative — replaying a session from a record that no longer describes it —
  would be a fabricated `Ready`.
- Making the transport call outside the lock leaves one window this does not
  close, and it is named rather than left to be discovered: the peer can go away
  between the conditional commit and the call, so the call is made against a
  handle the stack has already invalidated. The stack rejects it, which lands in
  the existing failure path. Reaching a *different* session would additionally
  require the stack to reissue the same connection handle within those few
  instructions. Closing it properly would mean holding a lock across a call into
  a stack that can re-enter, which is a deadlock traded for a race.
- The firmware keeps two spinlocks, for the status snapshot and for the session.
  They are never nested and nothing calls into a radio stack under either. That
  is a rule a future change can break silently, which is why it is written at
  the top of `meshcore_ble.cpp` as well as here.
