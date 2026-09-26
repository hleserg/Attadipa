# A contact snapshot that ended is not a contact snapshot that is true

Issue [#563](https://github.com/hleserg/Attadipa/issues/563). `CMD_GET_CONTACTS`
is not answered by one frame. It is answered by a stream that the node emits one
frame per `loop()` pass, while the same `loop()` runs the packet callbacks that
write unsolicited pushes to the same transport. So a push can land between
`RESP_CODE_CONTACTS_START` and `RESP_CODE_END_OF_CONTACTS`, and some of those
pushes describe a change to the very table being read.

This report answers what a client may conclude when the stream ends. The short
answer is that `RESP_CODE_END_OF_CONTACTS` proves the node finished walking its
array and proves nothing else, and that Attadipa currently converts that syntactic
fact into a semantic claim — `link/src/meshcore_companion.cpp:1511` —
"status_.peers_complete = true;" — that the evidence does not support.

It is a research document. No production code changed for it, and the contract in
§7 is a decision recorded for an executable issue to implement, not an
implementation.

---

## 0. Provenance — what was read, what was measured, what was not

| What | How it was established |
|---|---|
| the node's iteration, its push sites and its contact table | read at the **pinned** revision `d92964352441e53b93e8667b802e04f6e072b39e` (`companion-v1.17.1`, 2026-08-14), the revision [MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md) pins and the one the bench T114 reports running |
| the upstream patch | `meshcore-dev/MeshCore` [PR #3403](https://github.com/meshcore-dev/MeshCore/pull/3403), head `fefc150005d19b03ac5f302ea93a6e684ecd3308`, read as its own diff and against its base; **open**, unmerged, base `dev`, 2 files, +73/−21, created 2026-09-13 |
| Attadipa's client behaviour | read at `main@8e597d8`, the revision #563 was filed against, which is still the tip |
| **that a push really does interleave a contact stream** | **`MEASURED`** on this project's own bench — [§2.4](#24-it-is-measured-here-not-only-reported-upstream) |
| everything about *how often* it interleaves, and about node preferences | **`UNKNOWN`**. `NOT EXECUTED — HARDWARE REQUIRED` — §10 |

**What is not claimed.** No frequency, no failure rate, no measurement of any
node's contact churn, and nothing at all about the V4.3's unpublished fork (M28).
The upstream author's "about 25 times out of 30 reads" is quoted in §8 as **an
author report**, not as a result: the host, transport and radio pair behind it are
not published, and the author says plainly that the patch is compile-tested only.

**Licence.** MeshCore is MIT ([REUSE_LEDGER](REUSE_LEDGER.md)). Upstream source is
read and cited here; none of it is vendored, and the decision on #3403 is `ADAPT`
— the failure model and the compatibility tests, not the code.

---

## 1. The two questions a client confuses

A client that asks for contacts and reads until the stream ends learns exactly
one thing: **the stream finished.** It tends to record a second: **the list it
now holds is what the node holds.** They are different claims and only the first
is on the wire.

The gap between them is not theoretical. The node's table can change while the
node is reading it out, and §3 shows that the mutations are of three different
kinds with three different consequences — a row appended, a row's fields
rewritten in place, and a row removed with the array compacted underneath the
cursor. The last of those is the one with no push at all.

---

## 2. What the node actually does

### 2.1 One frame per pass, and only when nothing else asked

`MyMesh::checkSerialInterface()` reads a command frame if one arrived; **only if
none did** does it advance the iterator, and only when the transport is not
already busy writing:

```cpp
void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    handleCmdFrame(len);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    if (_iter.hasNext(this, contact)) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
```

— `examples/companion_radio/MyMesh.cpp:2206-2214` at the pin. `MyMesh::loop()`
calls `BaseChatMesh::loop()` **first**, and the packet callbacks under it write
their push frames straight to the same interface. There is no wire-level
transaction and no mutual exclusion; the response and the pushes share one
stream by construction.

Two consequences that are easy to miss:

- **A client's own command also interleaves.** `handleCmdFrame` runs in
  preference to the iterator, and its reply is written immediately. A
  `CMD_SYNC_NEXT_MESSAGE` sent mid-stream is answered mid-stream. This is not
  fixed by #3403, which deliberately leaves command responses alone.
- **The iterator is paused, not cancelled, by that.** `_iter_started` survives;
  the stream resumes on the next pass with nothing marking the interruption.

### 2.2 The iterator is a raw index into the live array

```cpp
bool ContactsIterator::hasNext(const BaseChatMesh* mesh, ContactInfo& dest) {
  if (next_idx >= mesh->getTotalContactSlots()) return false;

  dest = mesh->contacts[next_idx++];
  return true;
}
```

— `src/helpers/BaseChatMesh.cpp:948-952`. `contacts[]` is
`ContactInfo contacts[MAX_CONTACTS+MAX_ANON_CONTACTS]` (32 + 8), `num_contacts`
is seeded to `MAX_ANON_CONTACTS` so slots 0–7 are the anonymous-request scratch
space, and `startContactsIterator()` returns `ContactsIterator(MAX_ANON_CONTACTS)`
— the walk is over `[8, num_contacts)`, live, with no copy and no generation
counter.

That is the whole of the consistency story on the node side. Every mutation
during a walk is a mutation **under the cursor**:

| Mutation | Where it writes | Effect on the walk in progress |
|---|---|---|
| advert from a node already in the table | the matching slot, in place | rewritten row: emitted **old** if the cursor passed it, **new** if it has not. A torn read, never a skip |
| advert from a new node, auto-added | `contacts[num_contacts++]`, appended | appended above the cursor, so it **is** emitted — and `RESP_CODE_CONTACTS_START`'s count, taken before, is one short |
| advert when the table is full and the node overwrites | the oldest non-favourite slot, **in place** | the old contact is emitted and is gone; the new one is never emitted — if the slot is below the cursor. Above it, the reverse, which is correct |
| advert when the table is full and the node does not overwrite | **nothing** | none. §3 corrects the upstream claim about this case |
| a message or a path return from a contact | the matching slot's `lastmod`, `out_path`, `sync_since`, in place | a torn read of those fields only |
| `CMD_REMOVE_CONTACT` from *a* client | `num_contacts--` then every later row copied down one | **a row is skipped entirely.** The contact that shifts into the just-consumed slot is never emitted, and **no push is raised for it** |

The last row is the sharpest and it is worth seeing:

```cpp
  // remove from contacts array
  num_contacts--;
  while (idx < num_contacts) {
    contacts[idx] = contacts[idx + 1];
    idx++;
  }
```

— `src/helpers/BaseChatMesh.cpp:868-872`. `removeContact` has exactly one caller
in the companion, `CMD_REMOVE_CONTACT` at `examples/companion_radio/MyMesh.cpp:1305`,
and that handler answers `writeOKFrame()` — no push, to anybody. **Attadipa never
sends that command**; its whole command set is opcodes 1, 2, 4, 10, 20, 22, 26 and
40, none of which writes the contact table. So for this product the compaction
hazard needs a *second* client on the same node — which is not hypothetical, it is
the shape M31 already records, and `CMD_ADD_UPDATE_CONTACT` from that same second
client appends a row with no push either.

### 2.3 Nothing frames the stream except the codes themselves

Each frame is separately framed by the transport, so an interleaved push does not
corrupt the contact frames around it: a reader that dispatches on `data[0]` sees
three well-formed frames, not one mangled one. What it does not see is any marker
saying which of them belong to the response. There is no request id, no sequence
number and no "response continues" bit anywhere in this protocol.

### 2.4 It is `MEASURED` here, not only reported upstream

This repository already captured an interleave, five weeks before #3403 was
opened, on the T114 running the pinned firmware:

```
I (130258) TX op=0x1A len=42      CMD_SEND_LOGIN
I (130668) RX op=0x06 len=10      RESP_CODE_SENT
I (131698) RX op=0x85 len=14      PUSH_CODE_LOGIN_SUCCESS
I (134228) RX op=0x04 len=5       RESP_CODE_END_OF_CONTACTS
```

— [`MESHCORE_T114_FIRST_CONTACT.md:396`](MESHCORE_T114_FIRST_CONTACT.md) —
"PUSH_CODE_LOGIN_SUCCESS", §6c, run `send5`, 2026-08-28, `MEASURED`. The
`END_OF_CONTACTS` at 134228 ms is what makes it evidence: the enumeration was
still open when `0x85` arrived at 131698 ms, and the `RESP_CODE_SENT` at
130668 ms is a **command response** landing in the same window. Both kinds of
interleaving this report is about were on the wire, on this bench, on this
product's own hardware.

What that capture does **not** establish is the mutating case. `0x85` changes no
contact row. Nothing here has yet observed a `0x80`, `0x8F` or `0x90` inside a
contact stream, and §10 is the plan that would.

---

## 3. Every push code, and whether it can move the table

Classified from the pinned source, by what the callback behind the code writes
before it pushes. "In the contact frame" means the field is one of the eleven
`writeContactRespFrame` puts on the wire: public key, `type`, `flags`,
`out_path_len`, `out_path`, `name`, `last_advert_timestamp`, `gps_lat`,
`gps_lon`, `lastmod` — `examples/companion_radio/MyMesh.cpp:166-187`.

| Code | Raised by | Table change | Invalidates a walk in progress? |
|---|---|---|---|
| `0x80` `ADVERT` | `onDiscoveredContact(is_new=false)` | **yes** — a row rewritten in place, *or a row appended*: `is_new` is never assigned `true` on the storing path, so an auto-added contact announces itself here. Already recorded in [VERIFIED_FACTS](VERIFIED_FACTS.md) | **YES.** The one code that covers both "a field you already read changed" and "a row you may not reach appeared" |
| `0x81` `PATH_UPDATED` | `onContactPathRecv` | **yes** — `out_path`, `out_path_len`, `lastmod`, all three in the contact frame | **YES**, for routing fields only. No row appears or disappears |
| `0x82` `SEND_CONFIRMED` | `processAck` | no | no |
| `0x83` `MSG_WAITING` | `queueMessage`, `onChannelMessageRecv`, `onChannelDataRecv` | **yes, narrowly** — a plain or signed text message sets `from.lastmod` (and `sync_since`) before the tickle: `src/helpers/BaseChatMesh.cpp:238` — "from.lastmod = getRTCClock()->getCurrentTime(); // update last heard time" | **freshness only.** Identity, name, coordinate, path and membership are untouched. Treating it as invalidating would make an ordinary incoming message cancel every sync |
| `0x84` `RAW_DATA` | `onRawDataRecv` | no | no |
| `0x85` / `0x86` `LOGIN_SUCCESS` / `LOGIN_FAIL` | `onContactResponse` | no | no |
| `0x87` `STATUS_RESPONSE` | `onContactResponse` | no | no |
| `0x88` `LOG_RX_DATA` | `logRxRaw` | no | no |
| `0x89` `TRACE_DATA` | `onTraceRecv` | no | no |
| `0x8A` `NEW_ADVERT` | `onDiscoveredContact(is_new=true)` | **no** — the three literal-`true` call sites are the early returns where the contact was *refused* a slot | **NO.** The opposite of the intuitive reading, and this repository already verified it |
| `0x8B` `TELEMETRY_RESPONSE` | `onContactResponse`, `handleCmdFrame` | no | no |
| `0x8C` `BINARY_RESPONSE` | `onContactResponse` | no | no |
| `0x8D` `PATH_DISCOVERY_RESPONSE` | `onContactPathRecv` | see `0x81` — the same callback writes the path first | **YES**, same as `0x81` |
| `0x8E` `CONTROL_DATA` | `onControlDataRecv` | no | no |
| `0x8F` `CONTACT_DELETED` | `allocateContactSlot`, overwrite branch | **yes** — a row's identity is replaced in place, and the stored blob is deleted | **YES.** The strongest signal available: a contact that existed does not any more |
| `0x90` `CONTACTS_FULL` | `onAdvertRecv`, after `allocateContactSlot()` returned `NULL` | **NO** | **NO** — see below |

### 3.1 `PUSH_CODE_CONTACTS_FULL` does not mean the table changed

Both #563 and upstream #3403 state that it does. Upstream's own patch comment
says `CONTACT_DELETED` / `CONTACTS_FULL` "describes a table change that
invalidates what is being read". Read at the pin, and unchanged at the PR's own
head, `onContactsFull()` is called from exactly one place:

```cpp
    from = allocateContactSlot();
    if (from == NULL) {
      ContactInfo ci;
      populateContactFromAdvert(ci, id, parser, timestamp);
      onDiscoveredContact(ci, true, packet->path_len, packet->path);
      onContactsFull();
```

— `src/helpers/BaseChatMesh.cpp:172-177`. `allocateContactSlot()` returned
`NULL`, which is precisely the case where **nothing was allocated and nothing was
overwritten**: the table is unchanged and an advert was dropped. The push means
"I could not store a contact", not "I changed the one you are reading". A client
that invalidates a snapshot on `0x90` throws away a correct read, and on a node
whose table is genuinely full it would do so repeatedly.

The two outcomes are a **node preference**, not a constant:
`shouldOverwriteWhenFull()` reads `_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST`
— `examples/companion_radio/MyMesh.cpp:329-331`. Which way the bench nodes are
configured is `UNKNOWN` and is filed as **M33**.

### 3.2 What has no push at all

`CMD_REMOVE_CONTACT` (§2.2) and `CMD_ADD_UPDATE_CONTACT` mutate the table and
announce nothing. **A push-driven client therefore cannot detect every
invalidating mutation**, and no amount of push classification fixes that. It is
the reason §7 does not build the contract out of pushes alone.

---

## 4. What `RESP_CODE_END_OF_CONTACTS` actually carries

```cpp
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
```

— `examples/companion_radio/MyMesh.cpp:2217-2220`. `_most_recent_lastmod` is
accumulated as the maximum `lastmod` **of the contacts the walk actually
emitted** — after the `since` filter, and therefore not the table's maximum and
not the table's state.

**It is a freshness watermark for incremental sync. It is not a generation
counter, a transaction id, a checksum or a consistency proof**, and four
properties say so:

1. **It is a maximum over emitted rows.** A row rewritten *after* it was emitted
   contributes its old value; the watermark cannot see the new one.
2. **The filter is strict and the clock is coarse.** `contact.lastmod > _iter_filter_since`
   against `getRTCClock()->getCurrentTime()`, which is **seconds**. A mutation in
   the same second as the watermark, to a row already emitted, is invisible to
   the *next* incremental read as well — it is filtered out, and stays filtered
   until that contact changes again. A one-second hole, structurally.
3. **It says nothing about deletions.** A `since`-filtered read returns changed
   rows; a removed row is simply absent, and absence is indistinguishable from
   "unchanged".
4. **A full read is not unfiltered.** Attadipa sends `CMD_GET_CONTACTS` with no
   `since` — `link/src/meshcore_companion.cpp:25` — "constexpr std::uint8_t kGetContacts = 4;",
   sent as a one-byte frame, so the node sets `_iter_filter_since = 0` and the
   filter becomes `lastmod > 0`. **A contact whose `lastmod` is zero is never
   enumerated** while still being counted in `RESP_CODE_CONTACTS_START`, whose
   count is `getNumContacts()` — "total, NOT filtered count", in upstream's own
   comment. Whether a node in the field can hold such a contact depends on its
   RTC having been unset when the contact was created, and is `UNKNOWN` (**M34**).

Attadipa does not use the watermark today and §7 does not propose starting. If a
future incremental sync wants it, property 2 is the trap to design against.

---

## 5. Three different kinds of incomplete, and they need three different words

Conflating any two of these is how a client ends up with one flag that cannot be
acted on.

| Kind | Cause | Whose limit | Detectable how |
|---|---|---|---|
| **Retention truncation** | the watch keeps 16 — `link/include/attadipa/link/meshcore_companion.h:251` — "static constexpr std::size_t kRetainedPeers = 16;" — and drops every contact whose advert type is not `ADV_TYPE_CHAT` — `link/src/meshcore_companion.cpp:681` — "if (size < 148 || data[33] != kAdvertTypeChat) {" | **ours** | `peers_retained < peers_reported`, already rendered — `apps/src/mesh.cpp:284` — "if (status.peers_complete && retained < reported) {" |
| **Snapshot inconsistency** | the table moved under the cursor (§2.2) | **the node's** | an invalidating push inside the stream — and *not always*, per §3.2 |
| **Staleness** | the snapshot was true and the world moved on | nobody's | only a re-read |

A snapshot can be atomic and deliberately truncated; it can be full-capacity and
torn; it can be both, or neither. One boolean cannot carry that, and today one
boolean is asked to: `core/include/attadipa/core/mesh_service.h:216` —
"bool peers_complete = false;".

---

## 6. What Attadipa does today, at `main@1531cee`

1. `RESP_CODE_CONTACTS_START` records the node's count, clears the retained
   peers and clears both completion flags — `link/src/meshcore_companion.cpp:1432` —
   "status_.peers_complete = false;".
2. Each `RESP_CODE_CONTACT` is length-checked and accepted into the 16-entry
   window, de-duplicated by public key so a repeated row updates rather than
   doubles — `link/src/meshcore_companion.cpp:708` — "    if (count < table.size()) {".
3. `RESP_CODE_END_OF_CONTACTS` sets both flags —
   `link/src/meshcore_companion.cpp:773` — "contacts_complete_ = true;" — and
   that is the defect: the syntactic end of the stream is converted into the
   semantic claim that the list is the node's list.
4. **Since #567 the boundary frame is no longer the only way that happens.** A
   contact stream that falls quiet for three seconds is ended by a sweep in
   `tick()` — `link/src/meshcore_companion.cpp:528` — "            status_.peers_complete = true;" — because a
   bounded transport queue drops the *tail* of a burst and `END_OF_CONTACTS` is
   systematically the frame it loses. So *finished* now means "the node stopped
   sending", which is weaker than "the node said it was done" and strictly
   weaker again than "this is the node's list". **That widens the gap this
   report is about rather than narrowing it**, and the contract below is written
   against the post-#567 tree: every place that says `END` arrived should be
   read as *the stream ended, by either route*.
5. The client knows four push codes, `0x82`, `0x83`, `0x85`, `0x86` —
   `link/src/meshcore_companion.cpp:60` — "constexpr std::uint8_t kPushSendConfirmed = 0x82;".
6. Every other valid push — including all four invalidating ones — reaches the
   `default:` arm, where it is counted and refused —
   `link/src/meshcore_companion.cpp:1872` — "// A response code this build does not know is a frame we did not".
   The link is deliberately left up, which is right and is why this is a
   correctness gap rather than an outage.
7. `contacts_complete_` also gates `Availability::Ready` and the battery poll.

**Two things to keep.** The `default:` arm's refusal to tear down the link is
forward compatibility working as designed. And `enqueue_private()`'s refusal to
wait for the contact sync is a `MEASURED` fix — §6c of
[MESHCORE_T114_FIRST_CONTACT](MESHCORE_T114_FIRST_CONTACT.md), pinned by
`tests/test_meshcore_companion.cpp:881` — "void test_room_login_success_during_a_contact_burst_still_sends()".
Neither may be traded away for consistency.

**One thing to correct beyond the flag.** A valid push is not a malformed frame.
Counting `0x80`/`0x81`/`0x8A`/`0x8D`/`0x8F`/`0x90` as malformed puts
protocol-correct traffic into the counter the firmware logs as a parser fault,
which is the opposite of the evidence anybody debugging this will need. **All
four invalidating codes are in that list** — `0x8D` included, which an earlier
draft of this paragraph dropped while §3 and the contract both carry it.

---

## 7. The contract

Chosen here, to be implemented by a separate executable issue. It is recorded as
a decision in [ADR-0022](../adr/0022-contact-snapshot-consistency.md).

### 7.1 The observations, and why they are separate

| Observation | Means | Consumer today |
|---|---|---|
| `stream_finished` | `END_OF_CONTACTS` arrived; the node stopped walking | the `retained/reported` pair, `Availability::Ready`, the battery poll gate — all three want exactly this and nothing more |
| `snapshot_consistent` | finished, **and** no invalidating push was seen between START and END | nobody yet; it is what a recipient resolver (#552) will need |
| `snapshot_dirty` | finished, and an invalidating push was seen inside it | a retry, and an honest readout |
| `retry_pending` | dirty, and a bounded re-read is in flight | — |
| `snapshot_degraded` | the retry budget is spent and the newest snapshot is still dirty | **nobody yet, deliberately** — see below |

**`peers_complete` keeps its meaning and loses its name's ambiguity.** Its one
consumer needs `stream_finished` — the pair of numbers is only comparable once
the node's iteration ended, which is what that flag was written for. Moving it to
mean "consistent" would silently change the mesh face. Consistency is therefore a
**new, separate observation**, and this is the single most important line in this
report: *do not repurpose the existing flag*.

**Availability does not move either.** `Ready` stays on `stream_finished`. Gating
it on consistency would make contact churn look like an unreachable mesh and
would re-create, one layer up, the exact defect §6c measured.

**And `snapshot_degraded` is not given to the face here.** An earlier draft of
the table above named "the readout" as its consumer, which contradicts this
report's own claim — and ADR-0022's — that the mesh face needs no decision to
make this implementable: telling the wearer a peer list is unproven is a UI
state with a Russian string, a place on two panel geometries and a design token
behind it, and `ui/AGENTS.md` is where that is argued. So the observation is
**recorded and exported, and read by nobody on the face yet**. Its first real
consumer is #552's recipient resolution, which needs to refuse rather than to
display. A readout is a separate, later issue, and this report does not pre-empt
its design.

### 7.2 Which pushes set `dirty`

Invalidating, from §3: **`0x80`, `0x81`, `0x8D`, `0x8F`**. Not invalidating:
`0x8A`, `0x90`, `0x82`, `0x83`, and every non-contact push. `0x83` is called out
because it is tempting and wrong: it moves `lastmod` only, and treating it as
invalidating would let an incoming message cancel a sync.

A push is only invalidating **between START and END**. The same code outside that
window means the world moved on, which is staleness, and staleness is not
repaired by a re-read of a snapshot that was true.

**Expect dirty to be the normal outcome, not the exception.** The test that
demotes `0x83` — it moves `last_advert_timestamp` and `lastmod` and nothing else
— is one a routine `0x80` also fails: `docs/research/VERIFIED_FACTS.md:343` — "An advert without a coordinate advances a contact's timestamps"
— and `0x80` is 33 bytes of bare public key, so the client has nothing in the
frame to tell an advert that changed the table from one that did not. Repeaters
and room servers advert on a timer. On a busy channel the likely steady state is
therefore: every stream dirty, both retries spent, `snapshot_degraded` published
every session. **That is a design input, not an objection** — the contract is
built to degrade rather than withhold precisely for it — but it inverts which
row of §7.5 is the common one, and it means the retry budget buys latency and
radio time in exchange for a consistency that may never be reached on such a
node. **How often a `0x80` actually arrives inside a stream is `UNKNOWN` (M32)**,
and it is the number that decides whether the retry is worth issuing at all.

Every one of those codes must first stop being counted as malformed. A push the
build understands and deliberately ignores is not a parse failure.

### 7.3 Bounded retry

- **At most two re-reads per session**, and only for a dirty snapshot.
- **Never mid-stream.** A second `CMD_GET_CONTACTS` while the node is iterating
  is answered `ERR_CODE_BAD_STATE` — `examples/companion_radio/MyMesh.cpp:1192-1193` —
  and that error carries no correlation field, so Attadipa's existing attribution
  would charge it to whatever command is still owed an answer, failing an
  innocent send. The re-read is issued after `END_OF_CONTACTS`, never before.
- **A delay between attempts**, so a node under advert load is not re-read at
  loop speed. The value is a policy choice, not a measurement; it belongs in the
  implementing issue with its reasoning, and this report does not invent a
  number for it.
- **No abort, ever.** `CMD_APP_START` would clear `_iter_started` and restart the
  handshake, and what that costs in practice is unmeasured (**M27**).
- **The re-read must not re-arm the message drain.** Ending a contact walk is
  not only bookkeeping: `end_contacts()` also spends the session's one
  `CMD_SYNC_NEXT_MESSAGE` — `link/src/meshcore_companion.cpp:772` — "    if (!request_next_message(now)) return false;"
  — and a failed enqueue there charges a malformed frame against a valid frame
  and returns before `update_availability()`. It is idempotent through
  `contacts_complete_`, but the re-read's own `CONTACTS_START` clears that flag,
  so a second walk re-enters the arm: another sync into a four-deep ring, and
  `draining_since_` reset, which extends the stall guard. **The implementation
  owes a latch that survives the re-read** — the drain is a session-level thing
  and the retry is a contacts-level thing, and this is where the two were
  accidentally tied together.

### 7.4 What is published while a retry is pending

**The last proven snapshot is kept and the dirty one is not published.** The
client's accumulator is already idempotent for updates — a re-read of the same
contact overwrites its row — but it never removes a row, so a snapshot that
dropped a contact cannot be repaired by merging; only a completed re-read
replaces it wholesale. If the budget runs out, the newest snapshot is published
as `snapshot_degraded` rather than withheld: a peer list that is probably right
serves the wearer better than an empty one, provided it does not claim to be
proven. **Newest of the ones the node ended**, which is the qualifier #586 added:
an attempt the quiet sweep closed produced a fragment and not a snapshot, so
after two of those `snapshot_degraded` carries the list that was already
standing.

**Keeping it is not the same as doing nothing, and this is the part that was
missing.** A re-read is a second `CMD_GET_CONTACTS`, so it opens with a second
`RESP_CODE_CONTACTS_START`, and that handler is not inert — it wipes the live
set: `link/src/meshcore_companion.cpp:1430` — "        peer_count_ = 0;" — and with
it `peers_retained`, `peers_complete` and `contacts_complete_` on the three lines
below. For the whole duration of a retry the contract claims changes nothing,
four things change:

| what moves | why | who notices |
|---|---|---|
| `Availability::Ready` is lost | it is gated on `contacts_complete_` | the mesh face reads not-ready; §6c's defect, one layer up |
| the battery poll gate closes | same flag | no battery reading for the length of the re-read |
| the `retained/reported` pair restarts at zero | `peers_retained = 0` | the face counts up through `3/40`, which `peers_complete` exists to stop |
| **an incoming message loses its sender's name** | `find_peer_prefix()` walks only `peer_count_` — `link/src/meshcore_companion.cpp:723` — "    for (std::size_t i = 0; i < peer_count_; ++i) {" — and an unresolved prefix blanks the field | the wearer is shown a message from nobody |

The last one is the serious one: it is exactly §6c of
[MESHCORE_T114_FIRST_CONTACT](MESHCORE_T114_FIRST_CONTACT.md) re-created by a
mechanism this report proposes to add.

**So decision 7 has a cost and it must be paid explicitly.** Publishing the last
proven snapshot across a re-read requires the implementation to hold a shadow
copy of the retained set and to latch the three flags until the re-read either
completes or is abandoned — not to rely on the accumulator being left alone,
because it is not. The cost is bounded and small: sixteen slots of
`core::MeshPeer`, the same array the client already carries —
`link/include/attadipa/link/meshcore_companion.h:251` — "    static constexpr std::size_t kRetainedPeers = 16;".
**An implementation that skips the shadow copy does not implement decision 7**,
and §7.5's `previous kept` row is the line that would silently be untrue.

### 7.5 The failure matrix

| Situation | `peers_complete` / stream | snapshot | Published | Retry |
|---|---|---|---|---|
| clean stream | finished | consistent | yes | no |
| stream ended by the #567 quiet sweep, no push seen | finished | consistent | yes | no — a lost boundary is not dirt |
| `0x83` inside the stream | finished | consistent | yes | no |
| `0x8A` or `0x90` inside the stream | finished | consistent | yes | no |
| `0x80`/`0x81`/`0x8D`/`0x8F` inside the stream | finished | dirty | previous kept | yes, bounded |
| **`0x8D` specifically** (path discovery) | finished | dirty | previous kept | yes — the code §3 classifies and §6 used to leave out |
| **while a re-read is in flight** | re-opened by its `CONTACTS_START` | **previous, from the shadow copy** | previous kept | in progress |
| **a message arrives during a re-read** | as above | as above | its sender resolves **against the shadow copy**, not against the emptied live set | — |
| **the re-read's `END` arrives** | finished again | as its pushes decide | new one, or previous if dirty | — |
| **the drain during a re-read** | — | — | — | **no second `CMD_SYNC_NEXT_MESSAGE`**; the latch holds across the retry |
| retry returns clean | finished | consistent | new one | — |
| budget spent, still dirty | finished | degraded | newest the node ended | no |
| disconnect mid-stream | not finished | none | previous kept until session reset | the reconnect's own sync |
| 17th contact, clean stream | finished | consistent | yes, truncated | **no** — retention, not consistency |
| unknown opcode | unchanged | unchanged | unchanged | no; counted as malformed, link kept |

---

## 8. Compatibility — three firmwares, one client

| | pinned `d929643` (`v1.17.1`, the bench T114) | upstream `dev` today | with #3403 merged |
|---|---|---|---|
| pushes inside the response | yes | yes | **no** — held in a FIFO and flushed after `END` |
| command responses inside the response | yes | yes | **yes, still** — deliberately untouched |
| table still mutates during the walk | yes | yes | **yes** — the patch reorders the telling, not the doing |
| a dirty snapshot is still dirty | yes | yes | **yes** |
| client can tell *which* read a post-`END` push refers to | n/a — position says it | n/a | **no.** A deferred push is byte-identical to a fresh one; nothing marks it as held |
| invalidating push can be lost | no | no | **yes** — a full FIFO drops it silently, 8 frames deep by default |
| `0x82`/`0x85`/`0x86` timing | immediate | immediate | **deferred**, so an operation's terminal push can arrive after the contact stream, and a dropped one leaves Attadipa's bounded deadline to fail an operation the node actually completed |

**Read that column downward.** #3403 makes the stream contiguous, which is a real
improvement for a strict reader, and it makes the client's evidence *weaker*: the
signal that says "your snapshot is torn" becomes droppable and loses the one
property — its position inside the stream — that told the client which read it
belonged to. A contract that depends on seeing the push therefore gets worse when
the fix lands, unless it is written to treat a post-`END` mutating push
conservatively.

**So #3403 is `ADAPT`, not a dependency.** It is open, unmerged, unreleased and
hardware-untested by its own author; the installed fleet is pinned on
`v1.17.1-d929643` by the owner; and none of it is needed for a correct client.
What is taken is its failure model and its compatibility cases.

The author's "about 25 of 30 reads" is an **author report**: no host, transport,
node or contact-table size is published with it, and it describes a strict driver
that fails closed. It is consistent with §2.4 and it is not a measurement this
repository may quote as one.

---

## 9. Replay matrix — the host tests the contract needs

`tests/test_meshcore_companion.cpp` already drives the provider frame by frame
and already contains the push-inside-a-stream shape (§6). These are the cases to
add with the implementation, not fixtures to check in ahead of it. Every one
asserts the **observation vocabulary**, not only a peer count, and every one
asserts that unrelated events survived.

| # | Trace | Expected |
|---|---|---|
| 1 | `START → A → 0x8F(A) → END` | dirty; previous snapshot kept; one bounded retry; `malformed_frames` unchanged |
| 2 | `START → A → 0x80(A) → END` | dirty; retry |
| 3 | `START → A → 0x8A(B) → END` | **consistent**; no retry — B was never stored |
| 4 | `START → A → 0x90 → END` | **consistent**; no retry — §3.1 |
| 5 | `START → A → 0x81(A) → END` | dirty; retry |
| 6 | `START → A → 0x83 → B → END` | consistent; **and the drain still happens** — the message must not be lost to the sync |
| 7 | `START → A → 0x82(match) → END`, and a mismatched ack | consistent; delivery still reaches `Confirmed`; a mismatch still does not release the slot |
| 8 | a mutating push at each boundary: before `START`, immediately after it, immediately before `END`, immediately after `END` | inside ⇒ dirty; outside ⇒ staleness, no retry |
| 9 | dirty → retry → clean `END` | consistent; new snapshot published; exactly one extra `CMD_GET_CONTACTS` on the wire |
| 10 | a mutating push in every retry | degraded after the budget; **no live-lock**, and a bounded number of commands |
| 11 | disconnect mid-stream; disconnect mid-retry | no false completion; session state cleared; the reconnect's sync is a fresh one |
| 12 | 17+ contacts, clean stream | consistent **and** truncated — the two observations must be independently assertable |
| 13 | a `0x8F` arriving *after* `END`, as #3403 would deliver it | not a retry trigger for the read that already ended; staleness only |
| 14 | a `RESP_CODE_ERR` arriving while a retry is outstanding | attributed by the existing order rule; **a send in flight must not be failed by it** |
| 15 | `0x80` while no iteration is running | consistent; no retry; not malformed |
| 16 | `START → A → 0x8D(A) → END` | dirty; retry — the fourth invalidating code, which §3 classifies and no row above exercised |
| 17 | dirty → retry `START` → `RESP_CODE_CONTACT_MSG_RECV` from A → retry `END` | **`last_sender` still names A**, from the shadow copy; `Availability::Ready` never drops; the `retained/reported` pair never counts up from zero |
| 18 | dirty → retry `START` → retry `END`, with the TX ring full | **no second `CMD_SYNC_NEXT_MESSAGE`**; `malformed_frames` unchanged; `draining_since_` not reset |
| 19 | a stream ended by the #567 quiet sweep, with no invalidating push | consistent, no retry — the sweep's `end_contacts()` is the only end that arrived, and a lost boundary frame is not evidence the table moved |
| 20 | dirty → retry `START` → one contact → **no `END`**, swept quiet | **the first walk's list still published**, `peers_retained` unchanged, snapshot back to dirty and a second attempt ten seconds later; the second sweep ends the session degraded with that same older list. On the measured node this is the only outcome a dirty walk has — see below the table |
| 21 | the swept walk's `END` arriving late, with a budget left | the next attempt is still ten seconds from the **sweep**, not from the stale frame: a walk this client wrote off may not move the clock the walk that replaces it is measured from |
| 22 | the swept walk's `END` arriving after the last attempt is armed and before the node answers it | the snapshot stays `retry pending`; `degraded` is the right end for the session and not the right end *yet*, and the attempt still in the air may still commit |
| 23 | the node volunteers a `START` after the budget is spent | that walk owns its own frames: the swept bit is cleared by any `START`, so its rows and its `END` are a first walk's in every sense |

Case 14 is the regression risk the whole design has to be checked against: the
retry adds a command to a queue whose error attribution is order-based, and
#315's fail-closed direction must not be softened by it.

**Cases 17 and 18 are the ones that fail on a naive implementation**, because
both pass trivially if the retry is never exercised and both fail the moment it
is: 17 asserts the shadow copy exists, 18 asserts the drain latch does. Neither
is a fixture test — both drive the production client through `receive()`.

**Case 20 is where rows 9 and 19 disagree**, and #586 is the defect of taking
19's answer. The quiet sweep ends a re-read too, and on its weakest rung — *the
node stopped sending*. For the first walk that rung is the best available and a
partial list beats none. For a re-read it is not: decision 7 keeps the last
proven snapshot published precisely while the attempt is in flight, so a
truncated staging committed over it converts a suspicion of staleness into a
certainty of loss, and publishes the result as `consistent`. The swept re-read
therefore abandons its staging and restores the dirty bit its own
`RESP_CODE_CONTACTS_START` optimistically cleared.

**And on the measured node, row 20 is not a case — it is the only outcome.**
The frame a re-read must have before it may commit is the one the bench dropped
**on a first walk**, and the distinction is the evidence boundary here: the
re-read landed on 2026-09-18 and no bench session has ever contained a second
`CMD_GET_CONTACTS`, so that an attempt two loses the same frame is **inferred**
from the burst being identical, not measured. The measurement is:
`firmware/main/meshcore_ble.cpp:1110` — "            // sessions out of three -- it is the last frame of the burst, so it".
A re-read is the same 234-frame burst, so a dirty walk there spends both
attempts and ends `degraded` with the older list every time. Row 20's "a second
attempt ten seconds later" is therefore not the unlucky branch on that node; it
is the branch, and the full two-attempt budget is always spent. The safe
direction is unchanged, and the cost — two full bursts through a queue already
overrunning — is what row 20 did not say and now does.

---

## 10. Physical HIL — **NOT EXECUTED — HARDWARE REQUIRED**

Nothing in this section has been run and none of it may be cited as a result.

**Fleet.** The pinned bench T114 (`v1.17.1-d929643`, held pinned by the owner)
and, if anyone builds it, a node carrying #3403. The V4.3's fork is out of scope
— no source, M28.

**Record for every run:** the Attadipa commit, the node's `RESP_CODE_DEVICE_INFO`
version string, the transport, the node's identity, its `autoadd_config` (M33)
and its contact table before and after.

**Procedure.** Deterministically mutate the table during `CMD_GET_CONTACTS` —
a second node advertising on cue for `0x80`, a second client issuing
`CMD_REMOVE_CONTACT` for the no-push compaction case, and a filled table for
`0x8F`/`0x90` — capture every frame with timestamps from the watch's own
`log_frame()`, and repeat at least 30 times so the interleave rate can be
compared with the upstream report rather than assumed from it.

**Pass/fail:** snapshot observation correct in every iteration; no ACK, message
or login push lost; retry count within budget; a reconnect mid-stream recovers.
Old firmware and any #3403 build are separate runs and separate tables — one must
never stand in for the other.

**No current, battery or radio claim** comes out of this. The retry changes
traffic; whether it changes power is a measurement nobody has taken, and until
someone does the answer is `UNKNOWN`.

---

## 11. Rejected alternatives

- **Wait for #3403, or copy it.** Open, unreleased, hardware-untested by its
  author, against `dev`; the fleet is pinned; and §8 shows it does not make a
  snapshot consistent. Copying firmware code into a client would also be the
  wrong tree.
- **Tear the session down on an unknown opcode.** Breaks forward compatibility
  and discards unrelated ACK and message events. The `default:` arm is right.
- **Keep setting `peers_complete` on `END` and ignore the pushes.** The defect
  itself.
- **Invalidate on every push.** `0x83` alone would make an ordinary incoming
  message cancel a sync; `0x8A` and `0x90` would invalidate reads that are
  correct.
- **Repurpose `peers_complete` to mean "consistent".** One consumer exists and it
  needs "finished" — §7.1. A silent change of meaning under a flag whose name
  fits both is exactly the kind of defect this repository writes reports about.
- **Unbounded re-read until clean.** A node under advert load never goes quiet;
  that is a live-lock with a radio attached.
- **One flag for truncation and inconsistency.** §5.
- **Block `Availability::Ready` on consistency.** Re-creates §6c one layer up.
- **Implement any of this here.** Out of scope by task type; the contract is a
  decision, and the decision is what a research issue may produce.

---

## 12. Residual risk

- **Detection is incomplete by construction.** `CMD_REMOVE_CONTACT` and
  `CMD_ADD_UPDATE_CONTACT` from a second client mutate the table silently
  (§3.2). No client-side contract can see them. What the contract can promise is
  that it never claims more than it saw.
- **Under #3403 the evidence is droppable** (§8), and a dropped `0x8F` reads as a
  clean snapshot.
- **The whole push classification is read from source**, at one revision, on a
  protocol that is a flat `if/else` chain over `#define`s rather than a versioned
  schema. It is `VERIFIED` in this repository's sense and it is not `MEASURED`.
- **The retry's cost is unmeasured**, in commands, in latency and in power.
