# Sending a message: the recipient, the delivery lifecycle, and a coordinate

Research for [#552](https://github.com/hleserg/Attadipa/issues/552), part of the
product sequence in [#488](https://github.com/hleserg/Attadipa/issues/488). It
changes no production code and proposes none; what it produces is a contract,
a compatibility matrix, a test plan, and one question for the owner.

The short version, and the rest of the document is the evidence for it:

1. **An ACK timeout is not a delivery failure, and this repository currently
   says it is.** The vanilla companion protocol has *no negative delivery
   event at all* — the node's own send-timeout hook is an empty function
   body — so the absence of a confirmation is the only signal, and it is
   indistinguishable from a confirmation that was lost on the way back.
2. **The node is the address book; the watch's sixteen retained contacts are a
   cache.** The bench node holds 350 contact slots and 233 contacts.
   `CMD_GET_CONTACT_BY_KEY` (30) resolves a full 32-byte key against all of
   them, and this repository does not use it.
3. **The payload budget is 160 bytes upstream and 128 here, and 128 is
   defensible** — but for a reason nobody has written down, and it is not the
   one the prose documentation gives.
4. **The outbound coordinate is an owner question, not a research one.** OD-30
   says so in its own words. The recommendation is written; the decision is not
   this document's to make.

---

## 0. Provenance — what was read, and at what revision

| Source | Revision | How it was read |
|---|---|---|
| MeshCore firmware | `d92964352441e53b93e8667b802e04f6e072b39e` (`companion-v1.17.1`, 2026-08-14) | eight files downloaded at that exact SHA and read locally, 2026-09-14 |
| MeshCore T114 build environment | same SHA, `variants/heltec_t114/platformio.ini` | same |
| MeshCore PR #2974 | head `3ae67848541500ceaf14820c279c03b39937a1b6`, base `dev` | GitHub API, 2026-09-14: `state` open, `merged` false, `mergeable` **false**, `mergeable_state` `dirty`, last updated 2026-08-09 |
| MeshCore issue #1834 | open since 2026-02-24, ten comments | GitHub API, 2026-09-14 |
| `meshcore_py` | `1bfd8385`, `src/meshcore/commands/messaging.py` | downloaded and read, 2026-09-14 |
| `meshcore.js` | `9e76c514`, `src/connection/connection.js` | downloaded and read, 2026-09-14 |
| Attadipa | `main` at `40271f5` | read in place |

**Nothing in this document is a hardware result.** Two numbers are `MEASURED`
and both are decodings of frames already captured and already committed in
[MESHCORE_T114_FIRST_CONTACT](MESHCORE_T114_FIRST_CONTACT.md) — §2.3 says which,
and says exactly what decoding them does and does not add. Every statement about
what two nodes do to each other in a product flow is
**NOT EXECUTED — HARDWARE REQUIRED**.

**One gap in an earlier document is narrowed here.** The open row
`docs/research/MESHCORE_COMPANION_PROTOCOL.md:764` — "Whether the first-party JS and Python clients agree with this reading"
stood because the two first-party clients had never been read against that
document. They are read in §2.2 and §2.3 below and they agree with the firmware
on both send frames. That settles `RESP_CODE_SENT` and
`PUSH_CODE_SEND_CONFIRMED`, and nothing else; the row has been updated in place
rather than struck through.

---

## 1. The five claims in #552, checked against `main`

The issue was written against `9f357a1`; `main` is nine commits further on and
two of them touched this file. All five claims hold.

| #552 says | On `main` at `40271f5` |
|---|---|
| `kMeshTextBytes = 128` | holds — `core/include/attadipa/core/mesh_service.h:16` — "inline constexpr std::size_t kMeshTextBytes = 128;" |
| `send_private()` returns only `bool` | holds — `core/include/attadipa/core/mesh_service.h:149` — "virtual bool send_private(const MeshPeerId& peer" |
| one global `delivery`, no message ID, no recipient | holds — `core/include/attadipa/core/mesh_service.h:121` — "MeshDelivery delivery = MeshDelivery::None;" |
| an expired ACK budget becomes `Failed` | holds — `link/src/meshcore_companion.cpp:324` — "status_.delivery = core::MeshDelivery::Failed;" |
| send resolves by 6-byte prefix in the retained window only | holds — `firmware/main/meshcore_ble.cpp:1366` — "requested contact prefix is not in retained chat contacts" |

What the nine commits did change nearby: [#478](https://github.com/hleserg/Attadipa/issues/478)
put a length floor on the `PUSH_CODE_SEND_CONFIRMED` arm, and
[#566](https://github.com/hleserg/Attadipa/issues/566) bounded the contacts
iteration. Neither touches the outbound vocabulary. **The finding is live, not
stale.**

---

## 2. The wire, as the pinned source writes it

### 2.1 `CMD_SEND_TXT_MSG` (2)

```
[2][txt_type][attempt][timestamp:4][pubkey_prefix:6][text…]
```

Thirteen bytes of header, guarded by `len >= 14`, so a one-byte text is the
shortest legal frame. The handler is `examples/companion_radio/MyMesh.cpp:1085`
at the pin — [permalink](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/MyMesh.cpp#L1085-L1130).
Four things in it decide most of this document:

1. **The recipient is looked up by six bytes.**
   `ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);` — the
   node searches *its own* table, which is much larger than the watch's cache.
   §8.
2. **The text is not length-checked here.** `int tlen = len - i; text[tlen] = 0;`
   — the frame's own length is the only bound at this layer, and the buffer is
   `cmd_frame[MAX_FRAME_SIZE + 1]`, so the terminator that is written one past
   the text is in range even for a maximal frame. The real bound is two calls
   down. §6.
3. **An unknown recipient and an unsupported `txt_type` are told apart**, which
   is unusual for this dispatch chain:
   `writeErrFrame(recipient == NULL ? ERR_CODE_NOT_FOUND : ERR_CODE_UNSUPPORTED_CMD);`.
4. **A text that is too long is reported as `ERR_CODE_TABLE_FULL`.**
   `composeMsgPacket` returns `NULL`, `sendMessage` returns `MSG_SEND_FAILED`,
   and the handler's only mapping for that is `ERR_CODE_TABLE_FULL` (3). A
   client that trusts the error word will tell its owner the node's tables are
   full when what happened is that the client sent 161 bytes.

### 2.2 `RESP_CODE_SENT` (6) — ten bytes, and one of them is a measurement

```
[6][flood?1:0][expected_ack:4][est_timeout:4]
```

Built at `MyMesh.cpp:1120`, ten bytes, `writeFrame(out_frame, 10)`.
`meshcore.js` parses the identical shape — `onSentResponse` reads
`result: readInt8(), expectedAckCrc: readUInt32LE(), estTimeout: readUInt32LE()`
— which is the independent corroboration §0 says was missing.

`est_timeout` is not a constant and not a guess at the client's convenience. It
is the node's arithmetic over the *airtime of the packet it just built*:

```
flood:   500 + 16.0 × airtime_ms
direct:  500 + (6.0 × airtime_ms + 250) × (path_hash_count + 1)
```

with `SEND_TIMEOUT_BASE_MILLIS 500`, `FLOOD_SEND_TIMEOUT_FACTOR 16.0f`,
`DIRECT_SEND_PERHOP_FACTOR 6.0f`, `DIRECT_SEND_PERHOP_EXTRA_MILLIS 250`.

So the estimate **grows with the payload and with the hop count**, and **neither
input is known for the one Attadipa observed**. The frame carries the product and
not its terms: the text's airtime is not in it and `path_hash_count` is not in
it. 2406 ms is consistent with a short text over several hops and equally with a
longer one over a single hop — two unknowns, one number, and nothing in the
capture to separate them. Both are `UNKNOWN`. That matters in §3, defect 2,
which is argued from the formulas and the product's own 128-byte maximum rather
than from this observation.

### 2.3 `PUSH_CODE_SEND_CONFIRMED` (0x82) — nine bytes, and the last four are the node's own round trip

```
[0x82][ack:4][trip_time_ms:4]
```

`MyMesh::processAck` writes it: `memcpy(&out_frame[1], data, 4)`, then
`uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;`,
`writeFrame(out_frame, 9)`. `meshcore.js` agrees: `onSendConfirmedPush` reads
`ackCode` and `roundTrip`, both `UInt32LE`.

This repository already holds one of these frames, captured on the bench and
committed. The four bytes it deliberately declined to interpret —
`link/src/meshcore_companion.cpp:1481` — "std::memcmp(&data[1], expected_ack_.data(), expected_ack_.size()) == 0) {"
reads the ack and stops — are a millisecond count:

`docs/research/MESHCORE_T114_FIRST_CONTACT.md:298` — "82 38 66 6c b8 1b 03 00 00"

`1b 03 00 00` little-endian is **795 ms**. The same report's `RESP_CODE_SENT`
two frames earlier,
`docs/research/MESHCORE_T114_FIRST_CONTACT.md:296` — "06 00 38 66 6c b8 66 09 00 00",
carries `66 09 00 00` = **2406 ms** of estimate, which
`link/include/attadipa/link/meshcore_companion.h:243` — "static constexpr core::Millis kMaxAckWait{15000};"
already records in its own comment.

**What decoding them adds, and what it does not.** The bytes were `MEASURED`
when they were captured; reading a field out of a frame this repository already
committed is not a new hardware claim, and nothing here was run on a board. What
it adds is that the node's own measurement of that round trip, **795 ms**, is
larger than the 720 ms the host saw between the two frames — which is the right
way round, because the node starts its clock when it handles the command and the
host starts it when the response arrives. Two clocks, one event, and they agree
to within the frame's own transit. It says nothing about any other node, any
other payload, or any other path.

### 2.4 The event that does not exist

```cpp
void MyMesh::onSendTimeout() {}
```

`BaseChatMesh::loop()` calls it when `txt_send_timeout` expires and then clears
the timer. The companion build's override is an **empty body**. There is no
frame, no push, no error, and no state a later command can read.

This is the load-bearing fact of the whole document. At the pinned revision the
companion protocol carries exactly two outbound delivery events — "the node
accepted it" and "an ACK matching it came back" — and **no third event that
means anything negative**. A client that says `Failed` has invented the word.

Two corollaries follow from the same source and are worth stating separately
because each is a test case later:

- **A confirmation can arrive arbitrarily late.** `expected_ack_table` is a
  circular table of `EXPECTED_ACK_TABLE_SIZE 8` entries, cleared **only** on a
  match. Nothing expires an entry. So an ACK that takes a minute still produces
  `PUSH_CODE_SEND_CONFIRMED`, as long as fewer than eight further sends have
  gone out behind it.
- **A confirmation that arrives while the app is away is lost for good.**
  `processAck` calls `writeFrame` with no `isConnected()` guard, and on the
  ESP32 BLE path `writeFrame()` drops everything while the link is down —
  `docs/research/MESHCORE_COMPANION_PROTOCOL.md:310` — "drops everything while it is false".
  The offline queue is for *messages*; `addToOfflineQueue` is reached from
  `queueMessage` and not from `processAck`. The node's ack table survives the
  disconnect (§3.1 of that report) — the push does not.

### 2.5 The ack tag is a hash, not an identifier

```cpp
mesh::Utils::sha256((uint8_t *)&expected_ack, 4, temp, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);
```

`expected_ack` is the first four bytes of a keyed SHA-256 over
`timestamp ‖ (attempt & 3) ‖ text`, keyed by the **sender's** public key. Three
consequences, none of them obvious from the wire:

1. **It is deterministic.** The same text to the same recipient at the same
   Unix second with the same attempt number produces the **same four bytes**.
   Two such sends are indistinguishable to any correlator, including this one.
   A built-in phrase is exactly the text most likely to be sent twice, and a
   one-second clock is exactly the resolution that makes it collide.
2. **`attempt` is masked to two bits** in the hashed blob, and the two extra
   bytes upstream appends for `attempt > 3` are appended *after* the hash is
   computed. So **attempt 4 and attempt 0 hash identically.** `meshcore_py`
   defaults to `max_attempts=3` and does not reach it; a caller that raises the
   default does.
3. **It does not identify the recipient.** The recipient's key is not in the
   hash input. Correlation to a person is the client's own bookkeeping, always.

`processAck` carries upstream's own warning in a comment —
`// NOTE: the same ACK can be received multiple times!` — and handles it by
zeroing the table entry on first match, so the second copy falls through to
`checkConnectionsAck` and raises no second push. A client must still be
duplicate-safe, because entry zeroing is per-entry and two entries can hold the
same tag by (1).

---

## 3. What the current vocabulary gets wrong

`MeshDelivery` has five values and one slot. Four defects, in order of how badly
they mislead:

**Defect 1 — `Failed` asserts a fact the wire cannot carry.** §2.4. The word
reaches the owner's screen through
`apps/src/mesh.cpp:241` — "put(text.delivery, sizeof(text.delivery)," and the
catalogue renders it `l10n/strings.toml:921` — "failed" in English and
`l10n/strings.toml:922` — "не доставлено" in Russian. The Russian string is
literally *"not delivered"*: a claim of non-delivery, made by a client that
cannot observe non-delivery. Upstream #1834 is eight months of people reporting
precisely this, including a MeshCore contributor's own diagnosis — *"the message
packet successfully made it's way from the sender to the recipient, but … the
ack packet didn't make it's way … back"* — and a user asking, in April, for the
word this document recommends: *"a third option akin to 'delivery
unconfirmed'"*.

**Defect 2 — the budget can expire before the node's own estimate does.**
`kMaxAckWait` is 15 s and the node's estimate is clamped to it. Against the
bench's 2406 ms there is a factor of six in hand, and the bench is the *easy*
case: a short text, over a path whose hop count the frame does not carry and
which is therefore `UNKNOWN` (§2.2). Putting those same formulas against the
128-byte maximum this product allows, on a three-hop direct path, produces
estimates
**above 15 s** — `500 + (6 × airtime + 250) × 4` passes 15 000 at an airtime of
about 563 ms. Whether a 128-byte MeshCore packet takes 563 ms of air is an
`ESTIMATED` arithmetic question this document does not answer, and it does not
have to: the point is that the clamp is *not* known to sit above the node's own
estimate, and where it does not, the watch declares an outcome while the node is
still legitimately waiting. The clamp is right — a peer's number must be bounded
— and 15 s is a chosen number that predates this reading.

**Defect 3 — a late confirmation is thrown away.** §2.4's first corollary:
upstream will still push the confirmation. `end_operation()` has already cleared
`awaiting_confirm_`, so the frame arrives, matches nothing, is not counted
malformed, and is dropped. **The node proves delivery and the watch, having
already said `Failed`, discards the proof.** Today this is the *correct* code
given today's vocabulary; it is the vocabulary that makes it a loss.

**Defect 4 — a disconnect erases the verdict instead of qualifying it.**
`link/src/meshcore_companion.cpp:180` — "status_.delivery = core::MeshDelivery::None;"
runs in `reset_session()`. `None` renders as *"not sent"* / *"не отправлено"*.
A message the node accepted, and may already have delivered, reads to the owner
as one that never left. And by §2.4's second corollary the confirmation that
would have settled it is dropped by the node while the link is down, so this is
not even recoverable by waiting.

---

## 4. The contract this recommends

### 4.1 The request

```
send(recipient: full 32-byte identity,
     body:      UTF-8 bytes, non-empty, ≤ budget,
     when:      WallTime)
  -> either a local refusal with a reason, or a non-zero local request id
```

- **Full 32-byte identity at the app/core boundary.** The six-byte prefix is
  what the adapter writes into `CMD_SEND_TXT_MSG` and must not be what an
  application holds — `link/src/meshcore_companion.cpp:1754` — "std::memcpy(&frame[7], peer.public_key.data(), kPeerPrefixBytes);"
  is where the narrowing belongs and is already where it happens.
- **A local request id, and the word "local" is the contract.** Non-zero so
  that zero means "no request"; monotonic within a session; explicitly **not**
  the node's ack tag, which §2.5 shows is a hash that can repeat. The id is what
  a result is delivered against.
- **A refusal is not a delivery state.** "The link is down", "a send is already
  in flight", "the body is over budget", "the recipient is not resolvable" are
  answers to the *call*, and none of them is a statement about a message,
  because no message exists. Today all four arrive as `false` and, for one of
  them, as a `Failed` written by
  `link/include/attadipa/link/meshcore_companion.h:219` — "void send_abandoned() { status_.delivery = core::MeshDelivery::Failed; }".

### 4.2 The result states

| State | What the wire had to do to produce it | What it does **not** mean |
|---|---|---|
| `Queued` | the frame is in this client's transmit ring | the node has it |
| `Accepted` | `RESP_CODE_SENT` arrived for this request | it was transmitted, or received |
| `Confirmed` | `PUSH_CODE_SEND_CONFIRMED` matched this request's ack tag | anybody read it |
| `Unconfirmed` | the ACK budget expired with no match, **after** `Accepted` | it failed. **This is the change.** |
| `Refused` | the node answered `RESP_CODE_ERR`, a login for a room failed, or this client could not put the frame on the radio **after** it had already published `Queued` | that the node declined *this text*, in the third case |
| `Unknown` | the request was made and no verdict followed: the session ended, or the budget expired **before** `Accepted` — the node having answered neither the text nor the room login | it was not sent |

`Failed` leaves the vocabulary. Nothing that can be observed on this protocol
licenses it: the only two frames that could are `RESP_CODE_ERR` before the send
is accepted, which is `Refused`, and nothing at all, which is `Unconfirmed`.

**`Refused` covers the room path's second phase, and this is the one producer
of `Failed` an earlier draft of this report did not enumerate.** A room send is
one call in two phases: `send_room()` publishes `Queued` and returns `true`
while `CMD_SEND_LOGIN` is outstanding, and the text frame is enqueued later,
from the `PUSH_CODE_LOGIN_SUCCESS` arm —
`link/src/meshcore_companion.cpp:1505` — "        if (!enqueue_private(room_peer_, std::string_view(room_text_.data()),".
If the four-deep ring is full at that moment the enqueue fails, and the call
that would have reported it returned `true` a second ago. So decision 3's rule
— a local refusal is not a delivery state, because no message exists — does not
reach this case: a message was published as `Queued` and an owner is looking at
it. It is `Refused`, which is exactly what it is from the owner's side: nothing
went to the radio, and a resend cannot duplicate anything. Removing `Failed`
without naming this would leave the path with **no** terminal state at all —
`awaiting_login_` is already false and `awaiting_send_` was never set, so
`send_busy()` is false and `tick()`'s expiry arm never runs, and the screen
would hold `Queued` for the rest of the session.

**One budget, three phases, and only the last of them has an acceptance to be
unsure about.** `op_budget_` is armed for anything `send_busy()` covers —
`link/src/meshcore_companion.cpp:316` — "    if (!send_busy()) {" — which is a
room login outstanding, a text awaiting `RESP_CODE_SENT`, and a text awaiting
its acknowledgement. `Unconfirmed` is a claim about the third: *the node
accepted this message and this product cannot tell whether it arrived.* In the
first two the node answered nothing at all, and saying "accepted, unconfirmed"
there would invent an acceptance the wire never gave. Those expire to `Unknown`,
which is the same epistemic position §4.3 describes with a different boundary —
a timeout rather than a disconnect — and it is the state that leaves an owner
free to send again, which is right when nothing is known to have reached the
radio.

**And `Refused` has two producers, which the owner reads the same way and an
implementer must not.** A verdict the node gave is one. A frame this client
could not put on the radio after publishing `Queued` is the other, and on the
room path it lands **after** `Accepted` — which is the defect underneath it:
`RESP_CODE_SENT` on that path answers `CMD_SEND_LOGIN`, and publishing
`Accepted` from it claims the node accepted a text it has not been sent yet.
Only the text's own `RESP_CODE_SENT` means `Accepted`. With that corrected the
second producer lands on `Queued`, where `Refused` says what it is from the
owner's side: nothing reached the radio, and a resend cannot duplicate anything.

**A confirmation that arrives after the budget expired upgrades `Unconfirmed`
to `Confirmed`, while the request is still this session's current one.** §2.4
is why the case is real rather than theoretical: the node pushes the
confirmation whenever `processAck` runs, with no notion of our budget, so a
late match is ordinary traffic and not a protocol violation. The ack is
positive evidence and the budget's expiry is the absence of it, so the later
frame is the stronger claim and discarding it would make the watch say less
than it knows — and say it in the direction that invites the duplicate
decision 7 exists to prevent. The bound is the request, not the clock: once the
request has been replaced or the session has ended, a late ack has nothing to
attach to and is discarded, because §2.5's tag can repeat and an unattached
match would be evidence about some other message.

**`Confirmed` means a delivery ACK and never a read.** The ACK is generated by
the recipient *node*, not by a person and not by an application: nothing in
`TxtDataHelpers.h` carries a read receipt and nothing in this product should
imply one.

**`Unconfirmed` is not a softer `Failed`; it is a different claim**, and the two
differ in what the owner should do next. `Failed` invites a resend. `Unconfirmed`
says a resend may duplicate — which is the failure mode #1834's own participants
describe, and which the workaround offered there (*"keep sending the message
until you get an ack"*) causes by construction. **No automatic retry.** A retry
after a lost return ACK delivers a second copy of a message that arrived, and
nothing on this wire can tell the two apart. If a resend is offered at all it is
a person's decision, on a screen that says the first one may have arrived.

### 4.3 No new persistent state

The whole contract above is session state. One in-flight request, one id, one
result, cleared when the session ends — which the existing object already does
for everything except the vocabulary. **No chat database, no history, no durable
queue, no stored phrases.** Two consequences worth naming so that a later
implementer does not quietly reintroduce them:

- A request **still in flight** when the session ends yields `Unknown` (§4.2,
  decision 5): that is what the boundary establishes about it. A request the
  session already settled keeps what it settled on — a `Confirmed` is a verdict
  the wire gave, and a disconnect afterwards is not evidence against it — and is
  then simply not retained past the session, like everything else here.
  `Unknown` is the state of an unfinished request, not a solvent poured over
  finished ones.
- Nothing is versioned, so nothing needs migrating. The first slice adds no
  field to any stored structure.

---

## 5. Compatibility: three protocols, one contract

| | pinned `v1.17.1 / d929643` | stock companion today | PR #2974's proposed v14 |
|---|---|---|---|
| `RESP_CODE_SENT` | yes, 10 bytes | yes | unchanged |
| `PUSH_CODE_SEND_CONFIRMED` | yes, 9 bytes | yes | unchanged |
| local TX completed | **absent** | absent | `PACKET_SEND_TX_STATUS` (0x91) status 0 |
| local TX rejected before start | **absent** | absent | 0x91 status 1 |
| local TX outcome unknown | **absent** | absent | 0x91 status 2 |
| any negative delivery event | **absent** — `onSendTimeout` is empty | absent | still absent, by design |
| version gate | `CMD_DEVICE_QUERY` carries the client's version; node stores `app_target_ver` | same | new frames gated on v14 |

**Which states are honest on the current protocol:** every one in §4.2.
`Unconfirmed` is a client-side conclusion drawn from a budget the *node* sized,
which is the strongest form available, and it is exactly the conclusion #2974's
own author proposes clients draw.

**What v14 would add, and what it would not.** It would let the client start the
ACK timer at *transmission* rather than at *acceptance*, removing CAD, duty-cycle
and queue delay from a budget that currently absorbs them; and it would separate
"the radio refused this" from "nothing came back". It adds **no** delivery
evidence: 0x91 is about the local radio, and status 2 is explicitly "unknown".
So v14 makes `Unconfirmed` *narrower* and never turns it into `Failed`. **The
app-facing contract in §4 does not change if v14 lands** — which is the test of
whether the contract was drawn in the right place, and it passes.

**Why v14 cannot be planned against.** `mergeable: false`, `mergeable_state:
dirty`, base `dev`, no maintainer has answered the design proposal in #1834 in
the two months since it was made, and the last two comments on that issue are
both the proposer's. Its validation numbers — 20/20 native tests, three
representative builds — are the author's own statement about somebody else's
tree, and none of it is Attadipa hardware. Separately and decisively: the fleet
is pinned on `v1.17.1-d929643` by owner decision, so **no node here would
receive it even if it merged today**.

**How detection degrades.** The client already sends `CMD_DEVICE_QUERY` on every
connection and already reads a version out of the reply. A v14-aware client asks
for v14, gets whatever the node supports, and treats 0x91 as an *optional
refinement of its own timer* — never as a precondition. It must not probe: a
defined command that fails its guard returns `ERR_CODE_UNSUPPORTED_CMD`,
indistinguishable from an unknown opcode, so an error can never be read as
"your node is too old" —
`docs/research/MESHCORE_COMPANION_PROTOCOL.md:525` — "**indistinguishable from a".

---

## 6. Payload: 160 bytes upstream, 128 here, and bytes are not characters

### 6.1 The chain of bounds, each one real

| Bound | Value | Where |
|---|---|---|
| mesh payload | `MAX_PACKET_PAYLOAD 184` | `src/MeshCore.h` |
| text, as the chat layer enforces it | `MAX_TEXT_LEN = 10 × CIPHER_BLOCK_SIZE` = **160** | `src/helpers/BaseChatMesh.h`, with `CIPHER_BLOCK_SIZE 16` in `src/MeshCore.h` |
| companion frame | `MAX_FRAME_SIZE 176`, minus 13 of header ⇒ **163** | `BaseSerialInterface.h`; §2.1 |
| BLE notification at MTU 176 | 173, minus 13 ⇒ **160** | [MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md) §2 |
| Attadipa | **128** | `core/include/attadipa/core/mesh_service.h:16` — "inline constexpr std::size_t kMeshTextBytes = 128;" |

So on a link whose MTU leaves room for a full 176-byte frame, a companion can
put 161–163 bytes on the wire and be refused with the wrong error word (§2.1,
point 4). At MTU 176 the two ceilings coincide at exactly 160 and the mistake is
unreachable. A client that never exceeds 160 is correct on both.

**`MAX_TEXT_LEN` is a byte count and the check is `strlen`.** Not code points,
not characters. `if (text_len > MAX_TEXT_LEN) return NULL;`. The prose companion
documentation's "133 characters" figure matches nothing in this chain and is
not a bound any code enforces; where prose and source disagree, source wins and
prose is noted as wrong.

**The node is not UTF-8-aware on this path.** `src/helpers/UTF8Helpers.h` exists
at the pin and defines `validUtf8PrefixLength`, a correct scanner with the
surrogate and overlong exclusions in it — and **nothing at the pinned revision
calls it.** The inbound path a few lines from where it would be needed carries
`int tlen = strlen(text); // TODO: UTF-8 ??` and truncates on a byte boundary.
So a sender that hands the node a body cut mid-character gets exactly that on
the air.

### 6.2 The recommendation: keep 128

**Keep the 128-byte cap**, and record the reason, because the issue is right
that it has never been written down.

The reason is not radio efficiency and not frame capacity; at 160 both are fine.
It is that **128 is the buffer this product already sizes every inbound message
against**, and the inbound side has a documented failure that a smaller number
does not fix and a larger number makes worse:
`docs/research/REMOTE_TARGET_POSITION_FROM_MESHCORE.md:811` — "told to put it, and the cut lands on the coordinate: 131 bytes of text ending"
is the paragraph that shows a 131-byte message arriving into 128 and losing
three characters of longitude, about 650 m worth. (The figures here were 132
and five characters until #570 corrected the arithmetic by one; the citation
moved with it.) Raising the outbound
cap to 160 while the inbound buffer stays 128 would mean **this product emits
messages its own receiver truncates**, which is a worse property than a smaller
cap. Raising both is a second change with its own memory cost on a watch, and
nothing in the product asks for 32 more bytes.

So: 128 out, 128 in, one number, and the asymmetry with upstream's 160 is
recorded rather than hidden. If the cap is ever raised it is raised on both
sides in one change.

### 6.3 What the cap obliges, whatever its value

1. **Count UTF-8 bytes, not characters.** A Russian phrase is about two bytes a
   letter; 128 bytes is roughly 64 Cyrillic characters and 128 Latin ones, and
   a single emoji is four. A character counter would over-promise by a factor
   of two in the product's second language.
2. **Never split a code point.** If a body must be shortened, shorten it at a
   code-point boundary. `validUtf8PrefixLength` is the algorithm; it is MIT and
   it is unused upstream, which makes it a reuse candidate rather than a
   dependency (§10).
3. **Never truncate silently and never chunk.** Over budget is a *refusal the
   owner sees before sending*, not a repair. Chunking would turn one press into
   several messages, several ack tags and several verdicts, and nothing in §4
   could describe the result.
4. **The preview is the budget.** Whatever is counted is what the preview shows,
   including a coordinate if one is ever attached (§7). Two numbers — one on the
   screen, one on the wire — is the defect this rule exists to prevent.

---

## 7. A coordinate in an outbound message

### 7.1 There is no protocol slot, and this is enumerated rather than assumed

`TxtDataHelpers.h` defines three text types — `TXT_TYPE_PLAIN` 0,
`TXT_TYPE_CLI_DATA` 1, `TXT_TYPE_SIGNED_PLAIN` 2 — and the two constants beside
them are `DATA_TYPE_RESERVED` and `DATA_TYPE_DEV`, a developer namespace for
*group/channel* datagrams. `CMD_SEND_TXT_MSG` accepts 0 or 1 and nothing else.
There is no private-message position type, and
`docs/research/MESHCORE_COMPANION_PROTOCOL.md:696` — "This is an enumerated absence: all three definitions in" says so
from a reading of all three definitions rather than from a failure to find a
fourth.

**Do not invent a proprietary suffix.** Anything this product appends is text a
stock client renders verbatim to a person.

### 7.2 The candidates

| | What it is | Verdict |
|---|---|---|
| **A — visible canonical text** | `@55.9821,37.2104` appended to the body, exactly the grammar §14.2 of [REMOTE_TARGET_POSITION_FROM_MESHCORE](REMOTE_TARGET_POSITION_FROM_MESHCORE.md) already specifies for the **inbound** direction | the only interoperable option. A stock client shows it as text; an Attadipa receiver parses it; the two agree by construction because they are one grammar |
| **B — a typed channel or datagram** | `DATA_TYPE_DEV` group/channel datagrams | **reject.** It is a *channel* mechanism, not a private-message one; "developer namespace for experimenting" is an invitation to collide, not a contract; and no stock client renders it to a person |
| **C — Meshtastic `POSITION_APP=3`** | a structured position port | **reject**, and not on merit. Different wire protocol, GPL-3.0 against this tree's licence boundary, and OD-12 excludes Meshtastic support. Kept as architectural evidence that a structured slot is what a protocol designed for it looks like |
| **D — defer** | attach nothing yet | viable, and it is what today's code does |

**A is the only candidate that is not rejected on its own terms**, and it has a
property the others do not: this repository has already decided the *receiving*
half of it. [ADR-0021](../adr/0021-remote-target-from-a-message.md) is accepted,
and [#570](https://github.com/hleserg/Attadipa/pull/570) implements the parser
for exactly this grammar. Sending in that shape would make Attadipa's two halves
symmetric rather than adding a second convention.

### 7.3 Why the decision is still not this document's

`docs/research/OWNER_DECISIONS.md:2027` — "position is kept, and whether this product ever attaches a position to a message"
is OD-30 naming this as undecided and unasked, one sentence after deciding that
a position travels in a message rather than an advert. The remaining question is
not *which wire* — OD-30 answered that — but *whether this product ever composes
one*, and that is a privacy decision about the wearer, not a protocol question.

**Recommendation, for when it is asked:** adopt A, **default off, opt-in per
message, never standing.** With these conditions, each of which is a refusal:

- the preview shows the **exact bytes** that will be sent, coordinate included,
  counted against §6's budget;
- no fix, or a fix this product would not otherwise trust, means the option is
  **unavailable**, not silently omitted and not sent stale — the validity rules
  in [ADR-0011](../adr/0011-gnss-integrity.md) decide it, not the message
  screen;
- four decimal places, which is the observed grammar and about 11 m; more
  decimals is a precision claim the fix may not support and a privacy cost
  nobody asked for;
- **no per-contact standing setting in the first slice.** The owner's fork has
  one (§14.1 of the remote-target report, as the owner's report of a black box);
  a standing setting means a position leaves the wrist without a person deciding
  it that time, and that is a second decision, not a detail of this one.

**If the answer is "not yet", nothing is lost.** Defer costs one paragraph in
the UI plan and no wire compatibility, because A is additive text. That is why
this is written as a recommendation with a live alternative rather than as a
blocker.

---

## 8. Resolving a recipient

### 8.1 The window is a cache, and the node is the address book

`link/include/attadipa/link/meshcore_companion.h:222` — "static constexpr std::size_t kRetainedPeers = 16;"
is what the watch keeps. What the node holds, at the pin, on the T114 companion
environment, is **350 slots** — `variants/heltec_t114/platformio.ini` sets
`-D MAX_CONTACTS=350` for all four `Heltec_t114*_companion_radio_*` envs,
overriding the source default of 100 in `MyMesh.h`. And the bench node is not
hypothetically full:
`docs/research/MESHCORE_T114_FIRST_CONTACT.md:637` — "7b, its contact list grown to 233. 20 `RESP_CODE_CONTACTS_START`, 19 complete".

**Sixteen of two hundred and thirty-three.** The retained window is not an
address book with a small limit; it is a seven-per-cent sample of one, and
today it is also the only thing a send can address:
`firmware/main/meshcore_ble.cpp:1366` — "requested contact prefix is not in retained chat contacts".

This resolves the #552 requirement directly: **a runtime retention policy must
not become a product address-book limit**, and at present it is one.

### 8.2 `CMD_GET_CONTACT_BY_KEY` (30) is the right instrument and is not free

The handler is eight lines:
`lookupContactByPubKey(pub_key, PUB_KEY_SIZE)` over the node's whole table,
answering `RESP_CODE_CONTACT` or `ERR_CODE_NOT_FOUND`. Full 32-byte key, no
prefix, no iteration, no ambiguity. It is exactly what §4.1's full-identity
boundary was drawn for.

**Three hazards, all in this client rather than upstream**, and all of them
reasons the command is a design task and not a one-line addition:

1. **Its reply is the iteration's frame.** `RESP_CODE_CONTACT` (3) is emitted
   both by the walk and by this command. This client accepts the code
   unconditionally and folds it into the retained list. A fetch-by-key reply
   would be appended to the cache — or, once sixteen slots are full, **dropped
   in silence**, which is the case that matters, because the whole point of the
   fetch is that the contact is not in the cache.
2. **The chat-type filter drops it too.**
   `link/src/meshcore_companion.cpp:594` — "if (size < 148 || data[33] != kAdvertTypeChat) {"
   refuses any advert type that is not chat, correctly for a contact list and
   incorrectly for a targeted fetch, where the refusal must be *reported* rather
   than absorbed.
3. **A reply arriving mid-walk perturbs the walk.** The quiet-window timer that
   decides a stream has ended is refreshed by any `RESP_CODE_CONTACT`.

So the resolution path is: a full identity from wherever the UI got it →
`CMD_GET_CONTACT_BY_KEY` → the six-byte prefix for the send → refuse if the node
says `ERR_CODE_NOT_FOUND`. The correlation the fetch needs is a *pending-fetch
flag* the `RESP_CODE_CONTACT` arm consults, which does not exist yet.

### 8.3 Three refusals

- **Never send by display name.** Two bench nodes' names differed by an emoji —
  `core/include/attadipa/core/mesh_service.h:90` — "whose names differed by an emoji and" — and a
  name is not an identity.
- **Never resolve a prefix collision by picking one.** Six bytes over 233
  contacts is comfortable and over an unbounded network is not; the first-match
  loops in this client and in `meshcore.js` alike return *a* contact, not *the*
  contact. With a full key available there is no reason to be in that position.
- **Never silently widen the search.** Not in the cache and not on the node is
  an answer to give the owner, not a reason to fall back to something looser.

---

## 9. Privacy and logging

Constraints, not preferences, and each one is already somewhere in this tree:

- **Never log message text.** Not at any level, not truncated, not hashed. The
  existing outbound redaction stops the frame dump at the header for exactly
  this reason.
- **Never log a passkey or a room password.** `send_room` already zeroes its
  stack frame with `secure_zero()` rather than a fill a compiler may drop.
- **Never commit a capture carrying a real position or a contact name.** OD-27
  is the rule and §14.4 of the remote-target report is the worked example: the
  evidence is cited by SHA-256 and not published, because `docs/` is this
  repository's Pages root.
- **A coordinate is a location, in a log as much as on the air.** If §7's option
  A is ever adopted, a preview string containing it is message text by this
  section's first rule.

---

## 10. Reuse

The full record is in [REUSE_LEDGER](REUSE_LEDGER.md) under *"Sending a message
and finding out what became of it"*. The summary:

| Candidate | Decision |
|---|---|
| MeshCore firmware at `d929643` — `MyMesh.cpp`, `BaseChatMesh.*`, `TxtDataHelpers.h`, `MeshCore.h` | **canonical wire reference.** No code taken; this is what the client speaks to |
| `mesh::validUtf8PrefixLength` — `src/helpers/UTF8Helpers.h`, MIT, **unused at the pin** | `EXTRACT ALGORITHM` for §6.3's boundary rule. Not a dependency: it is 40 lines inside a firmware this product does not link |
| `meshcore_py` `1bfd8385` | `ADAPT` the matching-ACK oracle for tests. **Do not copy the retry policy** — §4.2 |
| `meshcore.js` `9e76c514` | corroboration of two frame layouts (§2.2, §2.3). Not a dependency |
| MeshCore PR #2974 | `MONITOR`. Compatibility input only — §5 |
| Meshtastic protobufs | `REJECT` — §7.2, and OD-12 |
| InfiniTime notification model | `REJECT` — inbound phone notifications do not solve an autonomous mesh send |

---

## 11. The test plan

### 11.1 Host, golden vectors — runnable now, no board

Driven through `tests/test_meshcore_companion.cpp`'s existing frame-by-frame
harness, which delivers bytes to `receive()` rather than calling internals.

| # | Case | What it proves |
|---|---|---|
| 1 | bodies of 127, 128, 129 bytes | the cap refuses at the byte, not near it |
| 2 | a two-byte code point straddling byte 128 | no split code point; refusal, not repair |
| 3 | a four-byte code point straddling byte 128 | the same for the case that costs four |
| 4 | a 160-byte body | upstream's boundary is documented and *refused here*, so the asymmetry is asserted rather than assumed |
| 5 | over budget with the coordinate off, under budget with it on, over with it on | §6.3 rule 4 — one budget, counted whole |
| 6 | full 32-byte recipient in → frame bytes 7..12 are its first six | the narrowing happens once, in the adapter |
| 7 | request ids: non-zero, distinct across sends, wraparound | no id aliases another live one |
| 8 | `RESP_CODE_SENT` → `Accepted`, then a matching ack → `Confirmed` | the happy path, by frame |
| 9 | a mismatched ack | no state change, not counted malformed |
| 10 | a duplicate of a matching ack | idempotent; the second changes nothing |
| 11 | an ack arriving **after** the budget expired | `Unconfirmed` is **upgraded to `Confirmed`** while the request is still the current one (§4.2); and a match arriving after the request was replaced changes nothing |
| 12 | budget expiry | `Unconfirmed`, and specifically **not** `Failed` |
| 13 | a second send while one is in flight | refused as a *call*, and the first request's state is untouched |
| 14 | disconnect after `Queued`, before `RESP_CODE_SENT` | `Unknown`, not `None` — the frame may have been written to the characteristic before the link went, and this client cannot tell that from one still in its ring |
| 15 | disconnect after `Accepted` | `Unknown`; and a reconnect does not resurrect the request |
| 16 | `RESP_CODE_ERR` with `ERR_CODE_NOT_FOUND` after a send | `Refused`, distinguishable from a timeout |
| 17 | `RESP_CODE_ERR` with `ERR_CODE_TABLE_FULL` | not reported to the owner as "the node is full" — §2.1 point 4 |
| 18 | two `RESP_CODE_SENT` frames carrying an **identical** ack tag, in sequence | each is attributed to the request that was in flight when it arrived, and the second does not confirm the first. The aliasing of §2.5 is asserted through the seam the host has: this client never computes a tag — `link/src/meshcore_companion.cpp:1434` — "            std::memcpy(expected_ack_.data(), &data[2], expected_ack_.size());" — it copies one, so a host test states the collision rather than reproducing upstream's keyed hash to manufacture it |

Rows 11, 12 and 18 are the ones this research exists to produce. A test suite
without them can pass while the product lies.

### 11.2 Simulator and replay

- recipient chosen by full identity, including one **not** in the retained
  sixteen — the §8.1 case, which has no path today;
- a built-in localized phrase, EN and RU, with no persistent storage behind it;
- a bounded parameterized phrase;
- the exact preview, coordinate off and on, cancel, and an explicit Send;
- offline, degraded and `NeedsAttention` — Send unavailable with a stated
  reason, never a silent no-op;
- replay: `RESP_CODE_SENT` + matching ack; `RESP_CODE_SENT` + nothing; a
  lost-return-ACK model where the recipient *did* receive; a mismatched ack.

The lost-return-ACK row is the one that cannot be observed on the bench and can
be modelled exactly: it is #1834's whole content, and the point of modelling it
is that the screen must read `Unconfirmed` while the message is, in the model,
delivered.

### 11.3 Hardware — **NOT EXECUTED — HARDWARE REQUIRED**

Not run, not partially run, not simulated and then called run. When it is run,
the record must fix: Attadipa firmware SHA, node firmware SHA and version string
as read from `RESP_CODE_DEVICE_INFO`, both device identities, radio
configuration, the recipient's public key, the exact payload bytes, timestamps,
and the raw serial capture — under OD-27's redaction rules.

| Case | Notes |
|---|---|
| stock sender and stock recipient, both directions | establishes interoperability before anything product-shaped is claimed |
| the product journey: recipient → phrase or text → preview → Send | the thing being built |
| EN and RU at the byte boundary | §6.3 rule 1, on a real panel and a real radio |
| a recipient **outside** the retained window | §8.2, the reason `CMD_GET_CONTACT_BY_KEY` is in scope |
| confirmation, no-confirmation, and reconnect across each | §3 defects 1, 3 and 4 |
| coordinate off and on | **only after §7 is decided.** Not before |
| duplicate detection with an intentionally delayed return ACK | only if it can be provoked safely and reproducibly; otherwise it stays modelled |

**The existing Room send does not count, and it was a Room send on both
units.** It is `MEASURED` and it is a debug path:
`docs/research/MESHCORE_T114_FIRST_CONTACT.md:11` — "Handshake, Receive and the send path to `Accepted` are on the Heltec T114; the"
says which halves ran on which unit, and every attempt in §6 went to the same
Room Server behind a login —
`docs/research/MESHCORE_T114_FIRST_CONTACT.md:275` — "Target in every attempt: the "Beta Room" Room Server, public key".
A room send is a different path with a different first phase, so nothing in §6
is evidence about a private one.

**And that case does not carry the argument an earlier draft of this section
made with it.** The T114's attempt reached `Accepted` and no confirmation in
120 s, and the receiving end says why that was not a lost return ACK:
`docs/research/MESHCORE_T114_FIRST_CONTACT.md:367` — "**And the message is absent from the room.** The operator's screenshot above"
— `MEASURED` by a person on the recipient, which is a stronger negative than
the watch could produce. **On that evidence the message really did not arrive,
and a screen that had said `Failed` would have been right.** What the case
shows is narrower and is what this report actually rests on: the *client* had
no way to know either way, and a label is a claim about what was observed, not
about what happened. The cause of the non-delivery is `UNKNOWN` and is filed as
M36.

---

## 12. What remains UNKNOWN

| Question | Why this document does not answer it |
|---|---|
| Why the T114's room send never confirmed, and never reached the room, in 120 s | the outcome is not what is unknown — the message's absence from the room is `MEASURED` on the recipient (§6b). The *cause* is: one occurrence, no air capture, and §2.4 means the node had nothing to say about it either. Filed as M36 |
| Whether a 128-byte MeshCore packet's airtime can push `est_timeout` past `kMaxAckWait` on a multi-hop path | §3 defect 2 is arithmetic over source formulas; the airtime term needs a radio configuration and a measurement. Filed as M37 |
| Whether `PUSH_CODE_SEND_CONFIRMED` is really dropped while the app is disconnected | read from source (§2.4). Nobody has disconnected a watch mid-flight and reconnected to see. Filed as M38 |
| Whether any upstream maintainer intends to take v14 | unanswered on #1834 for two months; not ours to predict |
| Whether the owner wants an outbound coordinate at all | OD-30 says it has not been asked. §7.3 |

---

## 13. The scope this justifies

One finite executable issue, and deliberately not more:

**In:** the `MeshDelivery` vocabulary of §4.2 including the removal of `Failed`,
the local request id, the full-identity boundary, `CMD_GET_CONTACT_BY_KEY` with
its three hazards handled, the byte-accurate UTF-8 budget and its refusal, and
rows 1–18 of §11.1.

**Out, and each for a stated reason:** the coordinate (§7.3, owner), a chat
database (§4.3), a retry queue (§4.2), v14 support (§5), chunking (§6.3), and
any UI beyond what the vocabulary needs — the screens belong to the navigation
and app-shell work, not here.
