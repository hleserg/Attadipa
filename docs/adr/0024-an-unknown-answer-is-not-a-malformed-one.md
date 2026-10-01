# 0024 — An answer this build cannot read is not a broken one, and the queue behind it is not the node's to keep

Status: **accepted**, implemented by [#709](https://github.com/hleserg/Attadipa/issues/709)
Date: 2026-09-26

Rests on [MESHCORE_OFFLINE_QUEUE_FORWARD_COMPAT](../research/MESHCORE_OFFLINE_QUEUE_FORWARD_COMPAT.md),
which is the evidence for every claim below, and narrows one decision of
[#481](https://github.com/hleserg/Attadipa/pull/481). Decided by research on
[#624](https://github.com/hleserg/Attadipa/issues/624); **no production code
changed with this ADR.**

## Context

A MeshCore node hands over one queued message per `CMD_SYNC_NEXT_MESSAGE` and
keeps the rest until asked again. #481 built the drain that asks: a frame that
decoded continues it, a frame that did not ends it, and a fifteen-second
deadline bounds a node that answers nothing at all. That design has one premise
— **that a frame this client cannot read is a frame that is broken** — and the
premise was complete only while the client's response table was the whole
protocol.

It is not. Upstream [PR #3447](https://github.com/meshcore-dev/MeshCore/pull/3447)
adds two message-carrying response codes, `0x1E` and `0x1F`, delivered through
the same offline queue, gated on the `app_target_ver >= 3` this client
declares; and it documents a host rule that the queue must be advanced on any
response. Research report §2.

And the client's behaviour for an unknown code turns out not to be the one #481
described. Measured at `39898a11` against the shipping class — report §3.3 —
an unknown response does not end the drain at all: it is counted in
`malformed_frames_`, nothing is asked, and `draining_` stays up for the full
fifteen seconds. During those seconds the swallowed-push repayment is withheld
and so is the battery poll. A *short known* frame, by contrast, ends the drain
immediately and recovers on the next tick. Two frames the client equally cannot
read, two different outcomes, and the worse one belongs to the node that is not
broken.

The consequence is a queue that stops draining behind a link reporting `Ready`,
with no announcement to recover on — the node's `PUSH_CODE_MSG_WAITING` tickle
is emitted per queued message and only while the app is connected, so nothing
re-announces a backlog — and an offline queue that the bench fleet's own node
types size at 256 frames and overflow **silently**, evicting the oldest channel
message or dropping the arrival. Report §2.4.

## Decision

**A response is classified before the drain is, and the classes are six.** A
frame arriving at `receive()` is exactly one of:

1. **queue-complete** — `RESP_CODE_NO_MORE_MESSAGES`, one byte. Ends the drain
   and clears the coalesced push. Unchanged.
2. **accepted** — a known message opcode that parses. Asks again. Unchanged.
3. **consumed** — a known message opcode carrying `TXT_TYPE_CLI_DATA`:
   understood, shown to nobody, counted apart, asks again. Unchanged, #627.
4. **unknown answer** — a drain is outstanding, the opcode is below `0x80` and
   not in this build's table, and the frame arrived whole
   (`1 ≤ size ≤ kMeshCoreFrameBytes`). **Asks again**, spending one unit of a
   per-drain budget; when the budget is spent it ends the drain as today. Its
   payload is never read, and it is counted in a new counter of its own.
5. **malformed known** — a known opcode failing its own guard. Ends the drain.
   Unchanged.
6. **unsolicited or damaged** — no drain outstanding, or an unknown opcode at
   `0x80` or above, or oversize, truncated or dropped. Ends any drain that was
   open and never asks.

**The budget is 256 consecutive unknown answers per drain**, reset by any
accepted or consumed frame and by the end of a drain. 256 is not a taste: it is
the largest offline queue any upstream build environment configures at
`0d7ba547`, so one drain can always empty one queue even when every frame in it
is unknown, and nothing larger is a queue this client can be asked to walk.

**Continuing a drain is not accepting a message.** Class 4 may not write
`last_message`, `last_sender` or `has_snr`, may not adopt a coordinate, and is
not shown to the wearer. It advances the queue and nothing else.

**And the `default:` arm ends the drain whenever it does not continue it.**
That half is a defect against #481's own intent and holds independently of
everything above.

## Alternatives considered

**Leave it until #3447 merges.** Rejected. The reachable form of the stall does
not need #3447 at all — `RESP_CODE_CLI_REPLY` (29) is already upstream and
already absent from this client's table (report row 4), and any node newer than
this build reproduces it. What #3447 supplies is the argument, not the bug.

**Treat every unrecognised frame as a message and always ask again.** Rejected.
It is upstream's literal wording and it has no bound: one byte of noise,
repeated, becomes the session-long exchange #481 removed. The wording is a
contributor's paragraph in an unreviewed pull request, not a maintainer rule.

**Bound it by time instead of by count** — keep asking until the fifteen-second
deadline. Rejected. It ties the number of exchanges to link latency, so the
same broken node costs a handful of frames over BLE and hundreds over a fast
transport, and the bound then has nothing to do with the thing being drained.

**Bound it at 16, the header default.** Rejected: 135 of the build environments
at `0d7ba547` set 256 and 10 set 128, including every node type on this bench.
A budget below the queue it is draining strands the tail for exactly the
scenario the decision exists for.

**Treat an unknown answer as malformed but end the drain immediately** — the
minimal fix, §5e alone. Rejected as the whole answer, kept as part of it. It
removes the fifteen-second telemetry hold and makes recovery-on-next-push
reliable, but it still leaves everything behind an unknown frame waiting for an
announcement the node will not repeat.

**Parse `0x1E` and `0x1F` as a product feature.** Rejected, and out of scope.
Mainline emits neither, the pull request is unreviewed, and what a
device-originated message means on a wrist — whose name it carries, whether the
wearer should see it at all — is a product question nobody has asked.

## Consequences

**Easier.** A node that grows a response code stops costing this watch its
message queue. The distinction between "I do not know this" and "this is
broken" becomes visible in diagnostics instead of being averaged into
`malformed_frames_`, which is the counter a dropped-frame investigation reads.

**Harder.** `receive()` gains a classification step where it had a `default:`
arm, and the budget is state with a lifetime — per drain, reset on progress —
that a test has to pin at both ends. Six classes is more than the three the
dispatcher implies today, and the two that look alike (4 and 5) are told apart
by a table lookup rather than by a length check.

**Committed to.** A bound derived from an upstream constant: if upstream ever
configures an offline queue larger than 256, this budget is wrong and the
research report's §2.4 is where that is checked. And to *not* showing the
wearer a message this build cannot parse — the drain advances, the screen does
not change, and the frame is counted. Anything else needs its own decision.

**Not committed to.** Any claim that this has been seen on hardware. It has
not: every row behind it is a host build of this repository's own code, and the
bench confirmation is **NOT EXECUTED — HARDWARE REQUIRED**.
