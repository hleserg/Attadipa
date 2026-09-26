# Draining a MeshCore offline queue that outlives this build's vocabulary

**Research report for [#624](https://github.com/hleserg/Attadipa/issues/624).
Research only: no production code changed with this report.** The decision it
reaches is [ADR-0024](../adr/0024-an-unknown-answer-is-not-a-malformed-one.md);
the implementation is [#709](https://github.com/hleserg/Attadipa/issues/709).

## 0. Provenance

| What | Revision | Read on | How |
|---|---|---|---|
| MeshCore PR [#3447](https://github.com/meshcore-dev/MeshCore/pull/3447) | head `0d7ba547`, base `dev` | 2026-09-26 | `gh pr view` / `gh pr diff`, and the three changed files fetched at that SHA |
| MeshCore tree around it | `0d7ba547` | 2026-09-26 | blobless clone, `git checkout FETCH_HEAD` |
| This client | `39898a11` (`main`) | 2026-09-26 | read, and **executed** — [`meshcore-offline-queue-drain/`](meshcore-offline-queue-drain/) |

**Nothing here is a hardware result.** The table in §3 is a host build of this
repository's own code driven by synthetic frames. Putting a device-originated
frame into a real node's queue and watching the wire is
**NOT EXECUTED — HARDWARE REQUIRED**, and §7 keeps it open.

**Licence.** MeshCore is MIT. No upstream code is copied into this repository
by this report; the two frame layouts in §2.2 are transcribed as *data*, which
is what a wire format is.

## 1. The question

Should this client issue the next `CMD_SYNC_NEXT_MESSAGE` after a response it
does not recognise — and if it should, what stops that from becoming the
session-long exchange with a broken node that
[#481](https://github.com/hleserg/Attadipa/pull/481) was written to prevent?

The issue predicted a stall. It reproduces. It is also, in one respect, worse
than the issue described, and §3 is where that shows up.

## 2. Upstream, verified

### 2.1 State of the pull request, today

`OPEN`, base `dev`, head `0d7ba5473f9185f1af24c696e806709ddfec18b0`, opened
2026-09-18T23:27:35Z and **not touched since 2026-09-18T23:27:44Z**. Three
files, +120 −1. `reviews` is empty: **no maintainer has responded.** It is
therefore `OPEN_PR` in the reuse ledger's vocabulary and may not be promoted to
`MERGED` or `RELEASED` on anything in this report.

### 2.2 What it adds

Two response codes, and the byte layouts are the ones the harness in
[`meshcore-offline-queue-drain/`](meshcore-offline-queue-drain/) uses:

- `RESP_CODE_CONTACT_MSG_SENT_V3` = **30** (`0x1E`) — reserved 1–3, *recipient*
  prefix 4–9, `path_len` 10 (always `0xFF`), text type 11, timestamp 12–15,
  text 16+.
- `RESP_CODE_CHANNEL_MSG_SENT_V3` = **31** (`0x1F`) — reserved 1–3, channel 4,
  `path_len` 5 (`0xFF`), text type 6, timestamp 7–10, text 11+.

Both are gated on `app_target_ver >= 3` and both are queued by
`MyMesh::queueSentMessage()` / `queueSentChannelMessage()` through
`addToOfflineQueue()` — the same queue, the same `PUSH_CODE_MSG_WAITING`
tickle, the same `CMD_SYNC_NEXT_MESSAGE` drain as an ordinary inbound message.

**Mainline calls neither.** The pull request says so in the header comment of
each function and in the protocol document, and a grep of the tree at
`0d7ba547` finds no caller. They exist for a custom firmware that
self-originates messages: a bot reply, or an on-device keyboard.

### 2.3 The invariant the pull request writes down

Added to `docs/companion_protocol.md` under `CMD_SYNC_NEXT_MESSAGE`:

> a host must advance its polling loop — issue the next `CMD_SYNC_NEXT_MESSAGE`,
> exactly as it would for a known type — on *any* response to this command,
> including packet types it does not recognize. New message-carrying packet
> types are added over time, and a host that only advances on the types it knows
> will stall on the first unknown one: the queue stops draining and the messages
> behind it stay undelivered until the next `PACKET_MESSAGES_WAITING`. Only
> `PACKET_NO_MORE_MSGS` (0x0A) ends the loop.

**Read this for what it is.** It is a paragraph a contributor added to a
document in the same unreviewed pull request that needs it to be true. It is
not a maintainer statement, it is not a released protocol guarantee, and it
carries no bound of any kind — "on *any* response" includes a response that is
one byte of noise. What it *is* good evidence for is the direction upstream
intends and the failure it names, both of which this client can verify
independently, and §3 does.

### 2.4 Three mechanics that decide how bad the stall is

Read at `0d7ba547`, in `examples/companion_radio/MyMesh.cpp`:

1. **The node pops on hand-over.** `getFromOfflineQueue()` copies
   `offline_queue[0]`, decrements the length and shifts the rest down, before
   the frame is written to the app. There is no acknowledgement and no way to
   ask for the same frame twice. So an unknown frame costs **one stalled drain,
   not permanent head-of-line blocking** — the node has already discarded it.
2. **`RESP_CODE_NO_MORE_MESSAGES` means exactly "the queue was empty".** The
   handler answers it only when `getFromOfflineQueue()` returned zero.
3. **The queue is bounded, and overflow is silent and lossy.** This repository
   already recorded that —
   `docs/research/MESHCORE_COMPANION_PROTOCOL.md:288` — "**Lossy when full**: `addToOfflineQueue` evicts the *oldest channel message*"
   — and the sizes at `0d7ba547` are worth naming: the header default is 16,
   and of the build environments that set it, **135 set 256 and 10 set 128**.
   `heltec_t114`, `heltec_v3` and `heltec_v4` — the bench fleet's own node
   types — are all 256.

Point 3 is what turns a stall into a loss. A drain that stops while the node
keeps accepting traffic is a queue filling toward an eviction the node reports
to nobody. Point 1 is what keeps it bounded: the stall costs one frame's worth
of delay per unknown frame, not a session.

**One thing the pull request adds that the issue did not mention**, and it
matters to §5: `queueSentMessage()` sends a `PUSH_CODE_MSG_WAITING` tickle
after queueing, but only `if (_serial->isConnected())`. Nothing in the tree
pushes on connect, so **a queue accumulated while the app was away announces
itself to nobody.** The only thing that drains it is the client asking.

## 3. This client, today — measured

`main` is forty commits past the `06dcbc4` the issue reviewed, and 111 files
moved. The two places the finding rests on did not:
`git diff 06dcbc4..main -- link/src/meshcore_companion.cpp` touches neither the
`default:` arm nor `drain_after()`. **The finding is live.**

### 3.1 The three arms that continue a drain, and the one that does not

`drain_after()` is the only caller of `request_next_message()` inside the
dispatcher —
`link/src/meshcore_companion.cpp:895` — "void MeshCoreCompanion::drain_after(bool accepted, core::MonotonicTime now)"
— and it is reached from exactly three cases: `RESP_CODE_CONTACT_MSG_RECV`
(`link/src/meshcore_companion.cpp:1787` — "    case kResponseContactMessage:"),
and the two V3 arms beside it. `RESP_CODE_NO_MORE_MESSAGES` ends the drain
(`link/src/meshcore_companion.cpp:1796` — "    case kResponseNoMoreMessages:").
Everything else falls to the default:

`link/src/meshcore_companion.cpp:1930` — "        // A response code this build does not know is a frame we did not"

which counts the frame in `malformed_frames_` and returns. It does not ask
again — and **it does not end the drain either**, which is the part the issue
did not predict and the part that costs the most.

### 3.2 What that costs, which is more than one missing request

`draining_` stays up until the deadline in `tick()` —
`link/src/meshcore_companion.cpp:550` — "    if (draining_ && core::elapsed(draining_since_, now) >= kMaxAckWait) {"
— and that budget is fifteen seconds
(`link/include/attadipa/link/meshcore_companion.h:272` — "    static constexpr core::Millis kMaxAckWait{15000};").
For those fifteen seconds:

- the swallowed-push repayment is withheld, because it is gated on the same
  flag — `link/src/meshcore_companion.cpp:576` — "    if (!wrong_node_ && !draining_) {";
- and **the battery poll is withheld too**, by the drain gate in `next_tx()` —
  `link/src/meshcore_companion.cpp:648` — "        (tx_size_ == 0 || drain_queued) && (!draining_ || drain_queued)) {".
  Nothing in the issue or in #481 anticipated an unknown message frame holding
  telemetry, and it does.

A *malformed known* frame does none of this: `drain_after(false)` clears
`draining_` at once, so the very next `tick()` repays the push
(`tests/test_meshcore_companion.cpp:413` — "void test_a_push_swallowed_by_a_drain_is_paid_back()").
So the client already has two different behaviours for two frames it equally
cannot read, and **the worse one is the one reserved for a node that is not
broken at all.**

### 3.3 The matrix

Executed at `39898a11`. The harness, how to re-run it and how to read the
columns are in [`meshcore-offline-queue-drain/README.md`](meshcore-offline-queue-drain/README.md);
the capture is [`trace-2026-09-26.md`](meshcore-offline-queue-drain/trace-2026-09-26.md).

| # | frame under test | next command | drain still open | unprompted recovery | malformed | cli |
|---|---|---|---|---|---|---|
| 1 | known `0x07` answering a sync | `10` | yes — properly waiting | none in 60 s | 0 | 0 |
| 2 | **unknown bounded `0x1E`** | `-` | **YES — waiting for nothing** | none in 60 s | 1 | 0 |
| 3 | **unknown bounded `0x1F`** | `-` | **YES — waiting for nothing** | none in 60 s | 1 | 0 |
| 4 | unknown `29` (`CLI_REPLY`) | `-` | **YES — waiting for nothing** | none in 60 s | 1 | 0 |
| 5 | structurally short known `0x10` | `-` | no | none in 60 s | 1 | 0 |
| 6 | unsolicited `0x1E`, no sync out | `-` | no | none in 60 s | 1 | 0 |
| 7a | oversize frame into `receive()` | `-` | **YES — waiting for nothing** | none in 60 s | 1 | 0 |
| 7b | oversize frame dropped by the transport | `-` | no | none in 60 s | 1 | 0 |
| 8 | eight unknown answers, one push each | `10` | yes | none in 60 s | 8 | 0 |
| 9 | unknown answer, then disconnect and reconnect | `-` | no | none in 60 s | 1 | 0 |
| 10 | push swallowed by the drain, then unknown answer | `-` | YES | **+15 s** | 1 | 0 |
| 11 | `TXT_TYPE_CLI_DATA` — understood, shown to nobody | `10` | yes | none in 60 s | 0 | 1 |

Availability is `Ready` on every row but 9, where the client is mid-handshake
after the reconnect. **The link never says anything is wrong**, which is the
part a wearer would experience.

What each row settles:

- **2, 3, 4** — the predicted stall, reproduced, and it is not specific to the
  two new codes: any response code this build does not define does it. Row 4
  matters because `RESP_CODE_CLI_REPLY` (29) is already upstream at `0d7ba547`
  and is not in this client's table either.
- **5 vs 2** — the two paths differ, and #481's own summary said they would
  not: *"a frame that decoded continues the drain, a frame that did not ends
  it"*. For a short known frame that is true. For an unknown opcode it is not;
  nothing ends it but the deadline.
- **6** — an unknown frame with no sync outstanding starts nothing. Correct
  today and must stay so: otherwise a node can drive the client into polling by
  volunteering frames.
- **7a vs 7b** — the transport's own drop is handled at the moment of loss
  (`link/include/attadipa/link/meshcore_companion.h:76` — "    void drop_oversize_frame() { ++malformed_frames_; draining_ = false; }"),
  and an oversize frame that somehow reaches `receive()` is not. Both are
  transport damage and should end a drain identically.
- **8** — there is no spin today, and the reason is not a budget: the client
  simply never asks twice. The budget is the node's pushes. Any contract that
  adds a continue must put a real bound where there is currently an accident.
- **9** — #481's reconnect property survives an unknown frame: a new session
  clears the drain and asks again. Nothing in §5 may weaken this.
- **10** — when a push *was* coalesced, recovery is the fifteen-second
  deadline. When it was not — rows 2–4 — there is no recovery at all, and §2.4
  established that nothing on the node will announce the backlog again.
- **11** — the counter-case, and the precedent §5 is built on. A
  `TXT_TYPE_CLI_DATA` message is understood, shown to nobody, counted
  separately, and **still advances the queue**
  (`link/src/meshcore_companion.cpp:1066` — "    if (data[text_type] == kTextCliData) { ++cli_frames_; return true; }").
  "Consumed" is already a class in this client that is neither "displayed" nor
  "malformed". An unknown response is the same kind of thing with less known
  about it.

### 3.4 Why a custom node would even send this to us

`kAppProtocolVersion = 3`
(`link/src/meshcore_companion.cpp:32` — "constexpr std::uint8_t kAppProtocolVersion = 3;")
and `CMD_DEVICE_QUERY` carries it, so a node running the pull request's code
records `app_target_ver == 3` for this client and considers it eligible for
both new frames. This watch is not an accidental recipient; it declares itself
one.

## 4. What is proven, and what is not

**Proven, at source level:**

- The stall, for any unknown response code, with no recovery except a later
  push that the node may never send (rows 2–4, §2.4 point 3's push rule).
- The fifteen-second hold on the swallowed-push repayment and on the battery
  poll (§3.2).
- The asymmetry between an unknown opcode and a malformed known one (rows 2, 5).

**Not proven, and not claimed:**

- **No misdelivery and no security consequence.** The unknown frame's bytes are
  never read: the default arm returns before anything is parsed, so no text,
  sender, SNR or coordinate can come out of one. This is a liveness defect.
- **Not reachable on the fleet today.** No firmware anywhere calls
  `queueSentMessage()`. The reachable form is the *general* one — row 4 — which
  needs only a node newer than this client's response table, and that is an
  ordinary future rather than a present fact.
- **Nothing measured on hardware.** §7.

## 5. The contract

The rule is not "continue on anything". It is: **a bounded answer to a question
this client actually asked advances the queue; nothing else does.** Six classes,
and each gets exactly one drain action.

| Class | Recognised by | Drain action | Counted in |
|---|---|---|---|
| queue-complete | `RESP_CODE_NO_MORE_MESSAGES`, size exactly 1 | end the drain, clear `pending_push_` | — |
| accepted | a known message opcode that parses | ask again | — |
| consumed | a known message opcode, `TXT_TYPE_CLI_DATA` | ask again | `cli_frames_` |
| **unknown answer** | a drain is outstanding, opcode `< 0x80`, not known to this build, and `1 ≤ size ≤ kMeshCoreFrameBytes` | **ask again while the per-drain budget lasts; when it is spent, end the drain** | a new counter |
| malformed known | a known opcode that fails its own length or shape guard | end the drain | `malformed_frames_` |
| unsolicited or damaged | no drain outstanding; or an unknown opcode `≥ 0x80`; or oversize, truncated or dropped | end the drain if one was open, and never ask | `malformed_frames_` |

Five things that are load-bearing in that table:

**a. Continuing is not accepting.** An unknown frame's payload is never read.
It may not set `last_message`, `last_sender`, `has_snr`, or adopt a coordinate,
and it is not a message the wearer was shown. The only thing it does is prove
the node answered, which is all the drain needs.

**b. `< 0x80` is the whole of the "is this an answer" test, and it is an
inference.** At `0d7ba547` the companion protocol allocates response codes 0–31
and push codes `0x80`–`0x90`, with nothing between. The split has to hold or a
client could not tell an unsolicited push from a response at all — the wire
carries both with no other distinction — but **upstream does not write it down
anywhere**, and this report will not claim it as a guarantee. It is M39 in
[OPEN_QUESTIONS](OPEN_QUESTIONS.md). Treating an unknown `0x8x` as a push that
never advances a drain is the conservative reading and costs nothing if the
inference is wrong: it fails back to today's behaviour.

**c. The budget is 256, and it is derived rather than chosen.** 256 is the
largest offline queue any upstream build environment configures at `0d7ba547`
(§2.4). A budget equal to it means one drain can always empty one queue, even
a queue consisting entirely of frames this build has never heard of — which is
the case the contract exists for. Anything larger is not a queue this client
can be asked to walk. The budget counts **consecutive** unknown answers, is
reset by any accepted or consumed frame, and is reset when a drain ends.

**d. The bound is per drain, and a drain still needs a push or a session to
start.** So the worst case against a node that answers every ask with an
unknown frame is 256 one-byte commands per push it sends — bounded, and bounded
by traffic the node could have generated anyway by queueing 256 real messages.
Compare the unbounded session-long exchange #481 refused: that is still
refused, by the budget and by the fifteen-second deadline that survives
untouched.

**e. The `default:` arm must end the drain in every case where it does not
continue it.** This half is a defect against #481's own stated intent and is
worth fixing whether or not the continue is ever implemented: rows 2–4 and 7a
leave a client waiting fifteen seconds for an answer it never asked for, and
holding telemetry while it waits.

And one thing the contract deliberately does **not** decide: what a
device-originated message *means* to this product — whether the wearer should
ever see a message the node sent on its own, and under whose name. Parsing
`0x1E`/`0x1F` as a feature is a separate question with no answer in the issue,
no caller upstream, and no reason to be settled here. §8 keeps it out of scope.

## 6. What #471 and #481 decided, and what survives

[#471](https://github.com/hleserg/Attadipa/issues/471) found that a reconnect
read one message and stranded the backlog; [#481](https://github.com/hleserg/Attadipa/pull/481)
fixed it with four mechanisms. **Three are untouched by this report and one is
narrowed:**

- **The drain itself** — one ask per accepted message — unchanged.
- **The fifteen-second bound on `draining_`** — unchanged, and §5 leans on it.
- **`pending_push_`, the bit that repays a swallowed push** — unchanged, and
  row 10 is the proof it still works on this path.
- **"a frame that did not decode ends the drain"** — narrowed to *a known frame
  that did not decode*, which is the only case it was ever measured against
  (`tests/test_meshcore_companion.cpp:307` — "void test_an_unreadable_message_does_not_spin_the_drain()"
  builds a short `RESP_CODE_CONTACT_MSG_RECV_V3`, not an unknown opcode). The
  anti-spin *purpose* is kept and given a real bound; what changes is the
  premise that a client cannot tell "I do not know this code" from "this frame
  is broken".

That distinction did not exist when #481 was written, and it is the whole of
what upstream #3447 adds to the argument.

## 7. What is `UNKNOWN`

- **Hardware.** Queueing `unknown → known → NO_MORE` on a real custom MeshCore
  node, capturing the wire and confirming neither a stall nor a spin:
  **NOT EXECUTED — HARDWARE REQUIRED.** It additionally needs a firmware that
  calls `queueSentMessage()`, and none exists (§2.2), so the honest bench
  substitute is a node patched to queue any frame with an unallocated code.
- **M39** — is the `< 0x80` response/push split a documented upstream
  guarantee, or only the current allocation?
- **M40** — does a node ever re-push `PACKET_MESSAGES_WAITING` for a message it
  has already announced? Open since #481 and still open; §2.4 narrows it (the
  tickle is emitted per queued message and only while connected) without
  closing it.
- **M41** — will #3447 merge, and does any maintainer accept the polling
  invariant as a *compatibility rule* rather than as this pull request's
  premise?
- **M42** — does any released or custom firmware actually emit `0x1E`/`0x1F`?

## 8. What the implementation issue must carry

Opened as [#709](https://github.com/hleserg/Attadipa/issues/709).

Narrow, and explicitly not a feature:

1. The six-class table of §5, in `receive()`.
2. A new counter for unknown answers, separate from `malformed_frames_` — a
   dropped-frame investigation cannot use a counter that also counts a peer
   speaking a newer dialect.
3. The `default:` arm ending the drain whenever it does not continue it (§5e).
4. An oversize frame reaching `receive()` ending the drain as `drop_oversize_frame()`
   already does (row 7a).
5. Tests that drive the production `MeshCoreCompanion`, one per row of §3.3,
   asserting the post-contract column rather than the measured one — and
   `test_an_unreadable_message_does_not_spin_the_drain` kept, because the class
   it pins is the one that still ends the drain.
6. No parsing of `0x1E`/`0x1F` payloads, no UI, no storage. The upstream
   producer's `strlen` + clamp carries its own `TODO: UTF-8` and is not
   evidence of a safe truncation boundary; this repository has its own
   (`core/src/mesh_service.cpp`), and neither is needed by anything above.
