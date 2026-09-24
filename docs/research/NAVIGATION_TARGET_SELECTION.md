# Which contact the arrow points at, who chooses it, and when it stops

Research for [#488](https://github.com/hleserg/Attadipa/issues/488), on the
owner's research-only follow-up of 2026-09-17. **No production code is proposed
here and none was written.** The wire half is decided —
[ADR-0021](../adr/0021-remote-target-from-a-message.md) takes a coordinate out
of a message and names whose it is. What is open is the half that ADR-0021
deliberately refused to settle by implication: *which* of those coordinates the
wearer is walking to.

The reading behind this is
[REMOTE_TARGET_POSITION_FROM_MESHCORE](REMOTE_TARGET_POSITION_FROM_MESHCORE.md)
for the wire, [MESHCORE_CONTACT_SNAPSHOT_CONSISTENCY](MESHCORE_CONTACT_SNAPSHOT_CONSISTENCY.md)
for what a contact list may be trusted to say, and
[OUTBOUND_MESHCORE_MESSAGES](OUTBOUND_MESHCORE_MESSAGES.md) for what the send
path already does with a full key. None of it is restated; this report cites it.

---

## 0. Provenance

- **Reviewed:** `main@ca4b64d` (2026-09-24). The owner's brief was written
  against `main@40271f5` (2026-09-17) and §1 records what moved between them.
- **Method:** repository source read at that head; three upstream trees fetched
  at the exact commits the brief names and read at the exact lines it cites.
- **Hardware:** nothing here was run on a board. Every claim about two nodes
  talking to each other is `NOT EXECUTED — HARDWARE REQUIRED`.
- **Not decided here:** anything this report marks as the owner's. §9 separates
  the two lists on purpose, because a research document that answers a product
  question by implication is the failure mode ADR-0021 decision 2 was written
  against.

---

## 1. Was the brief still true

Mostly, and one premise is now narrower than when it was written.

| Brief's premise | State at `ca4b64d` |
|---|---|
| "PR #570 carries an incoming coordinate to `MeshCoreCompanion::remote_position()`, but a consumer is still missing" | **Still true.** #570 merged at `0c7a3ac`. Nothing outside `tests/` calls `remote_position()`, and the header says so itself — `link/include/attadipa/link/meshcore_companion.h:179` — "    // NOTHING READS THIS YET, AND THAT IS THE STATE THE ISSUE ASKS FOR. #450" |
| ADR-0021 forbids `last sender wins`; promotion belongs to the wearer | **Still true**, decision 2, unchanged |
| ADR-0020 leaves target selection undecided | **Still true**, §9.3 of the research and OD-30's *what it does not decide* |
| "`kRetainedPeers = 16` limits the list that can be offered, though the T114 table allows 350" | **Half of it moved.** The cap still limits the list. It no longer limits *reachability*: #600 (`e1872aa`, 2026-09-18) made the sixteen a cache and the node the address book for sends. §2.2 is what that does and does not buy this task |

Two findings deferred out of #570's review and recorded on
[#576](https://github.com/hleserg/Attadipa/issues/576) are both paid at this
head: a grammar failure now clears the answer rather than falling back to a
quoted earlier coordinate, and the comment naming the unimplemented refusal
calls the frame `PUSH_CODE_CONTACT_DELETED`. Neither is a reason to hold
anything here; they are noted because a reader arriving from #576 would
otherwise re-check them.

**Nothing in the brief was found to be wrong.** The finding has not gone stale
and it has not been fixed.

---

## 2. What the code does today, which is not what #488 describes

### 2.1 The three identities #488 asks to separate are currently one

The brief asks the product to distinguish a connected companion, a contact, and
a selected navigation target. On the shipping board there is one of them:

`firmware/main/waveshare_board.cpp:1010` — "  nav.target = meshcore_ble_location();"

That is the **connected companion's own** coordinate — `RESP_CODE_SELF_INFO`
bytes 36–43, through `NodePositionProvider` over `node_position()` — and not any
contact's. So the readout's `target` slot, whose header defines it as what a
node said about itself — `apps/include/attadipa/apps/navigation.h:19` — "// **own** position comes from a receiver on this body, **target** position is" —
is filled by the node the watch is *attached to*, which is the one node a wearer
demonstrably is not walking towards on the split arrangement: ADR-0019 exists
because that node is on the wearer's own body.

This is not presented as a defect to fix in isolation. It is the state a
consumer of `remote_position()` will replace, and it is why the question "where
does the selected identity live" cannot be answered by pointing at an existing
slot: the existing slot has different contents and a different meaning.

### 2.2 What #600 bought, and the asymmetry it created

Before #600, a recipient outside the retained sixteen could not be sent to. Now
`send_private()` takes a full 32-byte key, and a key the window does not hold is
fetched from the node by `CMD_GET_CONTACT_BY_KEY` (30). The reply is taken
above the list walk, and **deliberately does not enter the window**:
`link/src/meshcore_companion.cpp:2000` — "// 1. It must not enter the cache. Sixteen slots, and the fetch exists precisely"

The incoming side did not move with it. A coordinate is attributed by resolving
the message's six-byte sender prefix against the retained window and nothing
else — `link/src/meshcore_companion.cpp:720` — "        if (std::memcmp(peers_[i].id.public_key.data(), prefix, 6) == 0) {" —
and a seventeenth contact never enters that window —
`link/src/meshcore_companion.cpp:710` — "    // A seventeenth distinct contact is dropped and nothing is flagged for it."

**So the two directions now disagree about who exists.** The watch can send a
message to contact 200 of 233; a coordinate arriving *from* contact 200 resolves
to no peer, ADR-0021 decision 2 says no target, and nothing is flagged. Fetching
that contact in order to send to it does not change the answer, because the
fetch is excluded from the window by design and for a good reason — folding it
in would silently evict a retained peer.

This asymmetry is new with #600 and is recorded here because no document carries
it: ADR-0021's Consequences names the cap as gating the coordinate, which is
still true, and was written before the send path stopped being gated by it.

### 2.3 What the single slot already denies, in the tree's own words

One coordinate is retained, against one sender key, as session state:
`link/include/attadipa/link/meshcore_companion.h:160` — "    // One slot is the known ceiling, not an oversight: it becomes a table"

Two denial paths follow from the single slot, both documented and one of them
pinned by a test — `tests/test_meshcore_companion.cpp:4028` — "void test_a_second_peer_restarts_the_first_peers_arrival()":

1. **B evicts A.** A wearer walking to A loses A's coordinate the moment any
   other contact sends a coordinate. The arrow does not turn towards B —
   decision 2 forbids that — it goes *blank*, which is a different failure and
   is the one a keyed table removes.
2. **A's identical re-send is stamped fresh** after B has evicted it, because
   the "nothing moved" test can only compare against whatever the slot holds
   now. That publishes an arrival that reports no new observation.

Neither is a defect in #570. Both are the ceiling the header names, and both are
reasons the consumer cannot be written on top of one slot unchanged. §6 prices
the alternatives.

### 2.4 One refusal of ADR-0021 decision 7 is unpaid

The clause requires that a contact the node has deleted is discarded rather than
aged. On this branch it is not implemented, deliberately and with the reason
recorded — `link/src/meshcore_companion.cpp:1118` — "// `remote_position_id_`, clear `has_remote_position_` -- and it is deliberately"

So `remote_position()` can publish a coordinate held against a key the node has
since deleted. It is latent today because nothing reads it. It stops being
latent on the first line of consumer code, which is why it belongs in the
*prerequisites* of this work and not in its follow-up list.

### 2.5 A pointer in the header that will mislead the next reader

The keyed table is cited to #304 — `link/include/attadipa/link/meshcore_companion.h:161` — "    // keyed by full public key when #304 lets a wearer pick a contact and more"

[#304](https://github.com/hleserg/Attadipa/issues/304) is about which **node**
the Companion transport attaches to; its Definition of Done is that "the watch
connects to a node the owner selected". Picking a **contact** is #488 and #450.
Both are "picking", they are not the same pick, and an implementer following
that comment lands on the wrong issue. Noted, not fixed: this report changes no
code.

---

## 3. Q1 — where the canonical selected identity lives

**What research closes.** The selected target and its coordinate have different
owners and different lifetimes, and the one thing that must not happen is
storing them together.

- **The identity is a full 32-byte public key and nothing else.** ADR-0020
  decision 2 and ADR-0021 decision 2 already require this of attribution; §7 of
  the wire research gives the four candidates it refuses — advertised name,
  six-byte prefix, arrival order, correlation tag — and every one of those
  arguments is *stronger* for a selection than for a label, because the cost of
  being wrong is an arrow pointing at somebody else rather than a wrong name
  over a message that already arrived.
- **The coordinate is session state and must stay there.** It is attributed
  through `peers_`, which belongs to whichever node filled it, and
  `remote_position()` already refuses on a disowned node —
  `link/src/meshcore_companion.cpp:1171` — "    if (wrong_node_ || !has_remote_position_) return false;"
- **The selection is not session state.** "I am walking to Anna" does not stop
  being true because BLE dropped. A selection cleared by a reconnect is a
  wearer's decision undone by a transport event, and the wearer did not undo it.
- **This is not ADR-0019's confirmation and must not share its rule.** A
  confirmed companion is RAM-only because the claim it encodes — this node is on
  my body *now* — is falsified by time passing. A selection is not: nothing
  about a target key becomes untrue while the watch sleeps. Copying ADR-0019's
  lifetime onto it would be reasoning from a superficial similarity between two
  opposite claims.

**The recommended shape**, which is three states in three places rather than one
state in one:

| What | Where | Lifetime |
|---|---|---|
| the selected key (32 bytes) | application/settings layer | survives app exit; §9 owns whether it survives a reboot |
| the coordinate held against a key | `link/`, session state | cleared by `reset_session()` and refused on `wrong_node_` |
| what the readout says about the pair | `apps/navigation` | recomputed every frame, stored nowhere |

**If it is persisted, it is re-validated on load.** ADR-0006 decision 1 is
explicit that validation happens on read as well as on write, and the failure it
was written against is exactly this one: a stored value carried forward
"wearing the authority of 'the user chose this'". A stored key whose contact no
longer exists is a selection that is still *named* and no longer *resolvable*,
and §5 gives it a state rather than a silent clear.

**There is a precedent for the storage and it is the right size.** The pinned
node key is already a 32-byte NVS blob — `firmware/main/meshcore_ble.cpp:349` — "    const esp_err_t err = nvs_get_blob(handle, kNodeKeyNvsKey," —
and the brightness store is the shape for a read that can fail in three
distinguishable ways — `firmware/main/brightness_nvs.h:18` — "  err = nvs_get_u8(handle, "brightness", &percent);" —
`Present`, `Missing`, `Failed`. The three-way answer is load-bearing here:
**`Failed` must not be rendered as `Missing`**, or a flash fault reads on the
wrist as "you have not chosen anybody".

---

## 4. Q2 — reaching a key the retained window does not hold

**The brief's constraint is right and the honest answer is that no mechanism
exists yet.** The four candidates, each measured against the code:

| Candidate | What it actually gives |
|---|---|
| **the retained window** | up to 16 chat contacts, `peers_retained` against `peers_reported`, with completeness already published — `core/include/attadipa/core/mesh_service.h:216` — "    bool peers_complete = false;". On the bench node that is 16 of 233 |
| **recent message senders** | **nothing the window does not already contain.** A sender is resolved by `find_peer_prefix` against `peers_`, so a message from outside the window has no sender at all — `link/src/meshcore_companion.cpp:1065` — "    const core::MeshPeer* sender = find_peer_prefix(&data[prefix]);" — and cannot appear in a "recent senders" list, because nothing knows who it was. This candidate looks like a second source and is a subset of the first |
| **exact-key entry or search** | the *transport* exists after #600 and the *interaction* does not. 32 bytes is 64 hex characters, on a 2.06-inch touch screen, with no keyboard in the tree. A key the wearer cannot type is a key they cannot select |
| **enumerating the node's table on demand** | the only candidate that reaches contact 200. It costs a walk: at the T114 build's 350-slot capacity, ~350 records × 148 bytes ≈ 52 kB over a link whose notifications carry 173 bytes — about 34 kB for the 233 the bench node actually holds — under ADR-0022's snapshot rules, and the result does not fit in RAM as a list. The byte count is the easy half; **M41** is the stall it has to survive |

**So Q2's real answer is that the gap is enumeration, and #600 did not close
it.** #600 lets the watch act on a key *it already holds*; the wearer holding a
key is the part that does not exist. Two ways out, and both are bounded:

- **widen the window.** `MeshPeer` is a 32-byte key plus a 33-byte name, so 65
  bytes before padding, and the client holds **two** such arrays —
  `peers_` and `incoming_peers_`. Sixteen costs about 2.1 kB of the session
  object; 64 would cost about 8.3 kB; the full 350 would cost about 45 kB, which
  is not a window any more, it is a mirror of the node's table on a device that
  should not hold one. A wider window is a *bigger list*, never a complete one,
  and `peers_retained < peers_reported` stays the honest statement either way.
- **browse the node, page by page, while the picker is open.** The frames exist
  and the cost is paid only while a person is looking. It needs a bounded
  cursor, and it inherits every consistency rule ADR-0022 wrote for a walk —
  including that a walk is not a snapshot.

**What is refused outright, on evidence rather than on taste:** offering a list
and letting a contact past the sixteenth be silently absent from it. That is the
owner's own constraint in the brief, it is what `peers_complete` exists to
prevent on the mesh face, and the same pair must reach the picker.

---

## 5. Q3 — the honest state machine, and one state that should not exist

The brief proposes `None`, `SelectedNoCoordinate`,
`SelectedCoordinateUnknownSourceAge`, `SelectedStale`, `Deleted/Unavailable`.
Four of those survive contact with the code. One does not, and collapsing it is
the finding.

**`SelectedCoordinateUnknownSourceAge` is not a state, because it is every
state.** No remote coordinate on any wire ever carries an age at the source:
ADR-0020 decision 4 forbids the advert timestamps from reaching
`age_at_source_ms`, and ADR-0021 decision 5 extends the ban to the message wire,
which carries no timestamp this repository reads at all. A state named for an
unknown source age implies a sibling state where the source age is known. There
is none, there is no wire on which there could be one, and naming it would
invite an interface to promise the distinction.

**Three of the remaining four already exist in `NavStatus` and must not be
duplicated.** The enum's own header is explicit that each value is a different
thing being wrong — `apps/include/attadipa/apps/navigation.h:57` — "    NodePositionUnknown,  // the node is reachable and has stated no coordinate":

| Brief's state | Where it belongs |
|---|---|
| `None` | **new.** No `NavStatus` value says "the wearer has chosen nobody", because until now there was nothing to choose. It is the honest resting state of a watch out of the box and it is not an error |
| `SelectedNoCoordinate` | `NodePositionUnknown`, unchanged. The contact exists and has stated no place |
| `SelectedStale` | `NodePositionStale`, unchanged, against `target_stale_after` — `apps/include/attadipa/apps/navigation.h:77` — "    core::Millis target_stale_after{120000};". ADR-0021 decision 5 notes this is the *resting* state and not a fault: a person sends a message when they have something to say, which is a cadence, not a refresh |
| `Deleted/Unavailable` | **new, and it is two things.** The node cannot be asked (`NodeUnavailable`, exists) is not the same as the node was asked and this contact is gone. The second has no value and needs one |

**The invariant under all of them: losing data never changes the identity.** A
failed NVS read, an emptied window, a reconnect and a deleted contact are four
different reasons the coordinate is absent, and none of them is a reason the
*selection* became somebody else. The selection changes when the wearer changes
it. Everything else changes what the readout says about it.

---

## 6. Q4 — is one sender-keyed slot enough, and what the alternatives cost

**No, and the tree already says why** (§2.3): with A selected and B talking, A's
coordinate is evicted and the arrow goes blank. Three shapes, priced:

| Shape | Cost | What it buys | What it still gets wrong |
|---|---|---|---|
| **bounded per-key cache** | ~48 bytes per entry (32 key + 8 coordinate + 8 stamp); 16 entries ≈ 0.8 kB | ADR-0021 decision 5 becomes true per key rather than approximately: "the first arrival since these bytes were last held **for this sender**" stops being falsified by a second sender | eviction still exists, it is just rarer; an entry evicted and re-arriving is stamped fresh, so the eviction policy is part of the honesty claim and must be written down |
| **selected-key-only slot** | one slot, near zero | the cheapest correct thing: adopt only when the resolved sender's full key equals the selected key, so nobody else can touch the slot at all | changing target throws away a coordinate the watch had already received and cannot ask for again on this wire. Every switch starts blank until that contact next speaks |
| **on-demand fallback** | one command per selection and per `0x80` whose key is the selected target's | ADR-0020 decision 3, already specified, already the fallback | it reads the contact record, which any BLE client of the companion can write — **M31** — so it is a weaker claim than a message, not a fresher one |

**Recommendation: the bounded per-key cache, with the on-demand fallback behind
it.** The selected-key-only slot is defensible as a first step and it is the one
to take if the cache does not fit, but it should be taken *knowingly*: it makes
switching target destructive, which is a property a wearer will notice long
before they notice a slot count.

**Whichever is taken, the comparison is on the full key.** The prefix resolves a
message to a contact; the *contact's* 32-byte key is then compared against the
selection. This matters for the collision case and it makes it fail closed: a
prefix collision resolves to the wrong contact, whose full key does not equal
the selected one, so the coordinate is dropped rather than attributed to the
wearer's target. That is the whole 48-bit risk ADR-0021 decision 2 declines to
price, turned into a refusal.

---

## 7. Q5 — message coordinate against contact-record fallback

**Precedence is by source, never by time, and the reason is that there is no
time.** Neither wire carries an age; arrival is a fact about our own receiver
(ADR-0020 decision 6, ADR-0021 decision 5). "Prefer the fresher" is therefore
not a rule that can be evaluated, and any implementation of it would be
comparing two arrival stamps and calling the result freshness.

The ranking that follows from the evidence:

1. **A message coordinate for the selected key wins**, because it arrived with a
   named sender and was composed for a recipient. ADR-0021 decision 1 makes it
   primary.
2. **The contact record fills the gap and does not overwrite the message**, for
   a reason stronger than ADR-0021 decision 3's demotion: `CMD_ADD_UPDATE_CONTACT`
   lets any BLE client of our companion write a contact's coordinate, persisted,
   with nothing marking it client-written — the owner's own phone is such a
   client (**M31**). A record silently replacing a message coordinate would let
   a second app on the owner's phone move the arrow.
3. **A message coordinate that goes wrong must not demote to the record**, and
   **"clears" is true of the parser and not yet of the slot.** The distinction
   is load-bearing and an earlier draft of this clause blurred it. *Inside one
   message*, `parse_trailing_coordinate()` already does the right thing: a
   failed bound and a failed grammar both reset the answer, so an earlier
   quoted match in the same text cannot win, and only a later well-formed match
   restores. *At the session slot* nothing of the kind happens, because the
   parser's verdict arrives as one boolean that means two different things —
   **no coordinate in this message** and **a coordinate that failed** — and the
   caller returns early on both:
   `link/src/meshcore_companion.cpp:1128` — "if (!parse_trailing_coordinate(status_.last_message.data(), position))"

   That is correct today and it cannot carry the rule. A rule that cleared the
   slot on every `false` would blank the arrow when the selected contact sent
   "on my way" — an ordinary message that says nothing about a place, and
   nothing about the coordinate already held. So the seam needs three answers
   where it has two: **absent**, **failed**, **ok**. Absent leaves the slot
   alone; failed clears it and does *not* fall through to the contact record,
   because a sender-side truncation would otherwise swap the wearer onto a
   coordinate from a different wire with no visible change — precisely the
   substitution ADR-0021 decision 7 was written to stop. State the cleared
   case, do not fill it. The tri-state is a prerequisite (§13), not a
   preference: no owner answer changes it.
4. **A fallback read is never presented as a refresh.** ADR-0020 decision 6
   forbids re-stamping unchanged bytes; reading the record after a message
   failed is a different source answering, not the same source updating.

---

## 8. Q6 — the lifecycle events, one row each

| Event | What happens to the coordinate | What must happen to the selection |
|---|---|---|
| **reboot** | gone: session state, nothing survives | resolved by §9 O1. If persisted: reload, re-validate, show "no coordinate yet" — never a blank identity |
| **reconnect to the same node** | `reset_session()` clears it; the window rebuilds from the walk | unchanged. A transport event is not a wearer's decision |
| **unpin / rebind to another companion** | must be dropped. `peers_` belongs to whichever node filled it, and `wrong_node_` already refuses the read | the key is a mesh identity, not a per-node one, so it stays valid as a *name*. Whether that contact exists on the new node is a question to ask the node, not to assume either way |
| **contact deleted on the node** | must be discarded rather than aged — ADR-0021 decision 7, **unpaid today** (§2.4) | selection survives as named-but-unresolvable. Deleting a contact is somebody else's action; it must not silently repoint the wearer |
| **duplicate display name** | irrelevant: names are not identity. Two bench nodes differ by an emoji, and the 32-byte name field truncates without complaint | irrelevant by construction |
| **prefix collision** | fails closed if §6's full-key comparison is implemented; mis-attributes if it is not | unchanged |
| **A and B reordered** | arrival order is the only order there is; no message carries a sequence number this repository reads | unchanged. The readout may not describe the survivor as "newest", only as the last that arrived |
| **window truncation past 16** | a coordinate from an unretained contact is dropped with nothing flagged (§2.2) | a selected contact can therefore be silent *because of our cap*, not because they said nothing. The picker must not be able to produce a selection the attribution path cannot resolve — or if it can, the state must say so |
| **offline / no link** | the last held coordinate is still the last thing that contact said | unchanged. Whether a last-known place is drawn with no link is §9 O3 |
| **Navigation exited, screen off, sleep** | nothing about the coordinate changes | the selection is unchanged; what must stop is the *work* — §10 |

---

## 9. Q7 — what is an owner decision, and what research closes

### Invariants — closed here, no owner input needed

1. Identity is the full 32-byte key. Never a name, a six-byte prefix, an array
   index, arrival order, or a correlation tag.
2. Attribution compares the **resolved contact's full key** against the
   selection, so a prefix collision fails closed.
3. No coordinate promotes itself. ADR-0021 decision 2; nothing arriving on any
   wire may change which contact is selected.
4. No age is claimed, on any wire, ever. `SelectedCoordinateUnknownSourceAge` is
   every state and therefore not one (§5).
5. Losing data never changes the identity: a failed read, an emptied window, a
   reconnect and a deletion are four reasons a coordinate is absent and none is
   a reason the selection moved.
6. A deleted contact's coordinate is discarded, not aged (ADR-0021 decision 7),
   and this is a prerequisite rather than a follow-up.
7. Precedence is by source rank, not by arrival; a **failed** message
   coordinate clears rather than demoting to the record, and a message that
   simply carries none leaves the slot untouched. Today's seam cannot tell
   those two apart, which is why the tri-state is in §13 rather than assumed.
8. The selected key is re-validated on load, and a storage **failure** is
   distinguishable from a storage **absence** (ADR-0006 decision 1).
9. Leaving the readout stops sensor and location work without changing identity
   (§10).

### Owner decisions — four, with options and a recommendation

**O1 — does a selection survive a reboot?**
(a) transient, cleared at boot; (b) persisted in NVS and restored; (c)
persisted, restored, and *confirmed* on the first Navigation entry after boot.
**Recommend (b).** (a) makes the wearer re-pick after every battery event, which
is the case a watch has most often. (c) buys safety against a stale intent at
the cost of a dialog nobody asked for.

**O2 — what may the wearer pick from?**
(a) the retained window only, with `k of N` visible; (b) the window plus an
on-demand paged browse of the node's table; (c) a wider window (a number, and
the RAM in §4); (d) (b) and (c).
**Recommend (a) first and (b) next**, because (a) is shippable against the code
that exists and is honest as long as the pair is shown, and (b) is the only one
that ever reaches contact 200. (c) alone buys a bigger incomplete list for
kilobytes of RAM.

**O3 — what does the readout show for a selected target with no link?**
(a) nothing but the state; (b) the last known place, labelled as last known,
with a bearing; (c) the last known place with no bearing.
**Recommend (b).** A distance to where somebody was an hour ago is useful and
the label is what keeps it honest; ADR-0020's ban is on claiming an *age*, not
on showing a place with its state.

**O4 — is selecting one action or two (browse, then confirm)?**
(a) one tap commits; (b) browse with a preview and a separate commit, as
`gpsnav` does; (c) one tap plus an undo.
**Recommend (b)** on the evidence in §10: it is the interaction two independent
watch-side implementations converged on, and on a wrist a single tap is the
input most easily made by accident.

**None of these four blocks writing the prerequisites** — §2.4's deletion arm,
§6's keyed cache and §7's tri-state parser verdict are needed under every answer.

---

## 10. Reuse — three upstreams, read at the exact commits, verified here

Full records go to [REUSE_LEDGER](REUSE_LEDGER.md). What is new is that the
brief's citations were **checked against the source rather than accepted**, and
two of them yielded a correction the brief did not carry.

**`meshtastic/Meshtastic-Android@6499b0f` · GPL-3.0 · `INSPIRE ARCHITECTURE`.**
The identity travels in the route and the overlay, never inferred:
`NodesNavigation.kt:100` reads `val destNum = args.destNum ?: 0`, and
`NodeDetailScreens.kt:59` declares `data class Compass(val nodeNum: Int, …)`.
The lifecycle seam is `NodeDetailScreens.kt:199-205`, where
`val targetNode = node?.takeIf { it.num == overlay.nodeNum }` re-checks identity
before the compass is started at all, and `ActiveWhileStarted` owns start and
stop. `CompassViewModel.start()` snapshots the target and cancels the previous
job before launching a new one; `stop()` cancels it.

- **ADAPT:** one owner of start/stop; identity re-checked at the point of use
  rather than trusted from the moment of selection; explicit cancellation of the
  previous job on target change.
- **Two things the brief did not name, found by reading it:**
  `args.destNum ?: 0` turns a missing route argument into node number **0** — a
  default identity, which is the failure §9 invariant 1 forbids, and it is a
  `REJECT` rather than an adapt. And `CompassViewModel` derives its freshness
  from `node.position.timestamp`, **a field our wire does not have**; copying
  its readout would copy a promise this product cannot keep.
- **DO NOT COPY:** phone location providers, Compose, and the Meshtastic node
  model.

**`espruino/BangleApps@6061fb4` · MIT · `INSPIRE ARCHITECTURE`.** `gpsnav`'s
README describes exactly the interaction O4 recommends: a visible `NONE`
waypoint, a press to enter selection, browse, and a second press to commit —
"The waypoint choice is fixed by pressing BTN2 [touch / BTN] again."
`waypointer/app.js` separates browse (`nextwp`) from commit (`doselect`) behind
one `selected` flag, and powers both sensors down on exit:
`Bangle.on('kill',()=>{ Bangle.setCompassPower(0); Bangle.setGPSPower(0); })`.

- **ADAPT:** the `None → browsing → committed` progression, a visible "nobody
  selected", and sensor shutdown as an explicit lifecycle action.
- **REJECT:** identity. A waypoint is `waypoints[wpindex]` — an index into a
  RAM-loaded array whose entries the app also renames in place — with no
  deletion, no truncation, no sender and no source age. Persisting a selection
  by index or by name is the defect class this product cannot afford.

**`meshcore-dev/meshcore.js@9e76c51` · MIT · `REJECT` for this purpose.**
`connection.js` offers `findContactByName`, which returns the **first** exact
`advName` match out of a full `getContacts()`, and `findContactByPublicKeyPrefix`,
the first prefix match. Both are reasonable host conveniences and both are
refused as target identity: display names are not unique, a prefix can collide,
and the full-table read is the ~52 kB walk §4 prices. Canonical identity stays
the full public key. This is confirmation of an existing ledger finding, not a
new candidate.

---

## 11. Tests the implementation will owe

**Host replay, runnable with no board.** Each of these is a frame sequence into
the client and an assertion out of the selection seam:

- A selected, message from B arrives → A's coordinate is **not** replaced by B's
  and the arrow does not turn; with the keyed cache, A's coordinate survives.
- A and B reordered → the survivor is the last that arrived and is not described
  as newest.
- Duplicate display names across two contacts → selection unaffected; duplicate
  six-byte prefix → the mismatched full key drops the coordinate rather than
  attributing it.
- Malformed, out-of-bounds, `(0,0)` and grammar-failing coordinates → the held
  coordinate is cleared and the contact record does **not** fill in behind it.
  **Paired with its opposite, because the two are one test:** a message from the
  selected contact carrying *no* coordinate at all — "on my way" — leaves the
  held coordinate and its arrival stamp untouched. A suite that asserts only the
  first half passes on an implementation that clears on every parser `false`,
  and that implementation blanks the arrow on an ordinary message (§7).
- `PUSH_CODE_CONTACT_DELETED` for the selected key → coordinate discarded, state
  says unavailable, **selection retained**.
- Retention truncation past 16 with a selected contact beyond it → the state is
  honest about why nothing arrives (§2.2), and nothing silently selects
  somebody else.
- Reconnect and `wrong_node_` → coordinate refused, selection intact.
- Reboot with a persisted key → restored, re-validated, `Missing` and `Failed`
  distinguished (§3).

**Simulator, rendered and inspected.** `None` with no contact chosen; browsing
without committing; cancel; a selected target with no coordinate; a selected
target the node deleted; `k of N` visible in the picker; EN and RU; both display
sizes. Per `AGENTS.md`, this needs the `watch-ui-testing` skill and an inspected
image, not a build that compiles.

**Power and lifecycle.** Leaving Navigation, the screen going off and the device
sleeping must each stop heading and location work while leaving the identity
untouched, under ADR-0016's one power owner. Meshtastic
[PR #6620](https://github.com/meshtastic/Meshtastic-Android/pull/6620) is the
bug precedent — listeners left running in the background — and it is a
*precedent*, not code to copy.

**Physical.** Two nodes, a real message carrying a coordinate, a real finger on
the picker, real sleep and wake, and end-to-end message → chosen target → arrow:
**NOT EXECUTED — HARDWARE REQUIRED.**

---

## 12. What remains UNKNOWN

Filed in [OPEN_QUESTIONS](OPEN_QUESTIONS.md) as **M39–M41**. Everything M28–M31
and M35 already says about this wire still applies and is not restated.

- **M39** — how many distinct contacts actually send coordinates to one watch in
  a session, which is what sizes the keyed cache of §6.
- **M40** — whether the sixteen-contact window ever excludes a contact the
  wearer wants, on a real fleet, which decides whether §4's browse is needed at
  all or is a hypothetical.
- **M41** — what a paged browse of the node's table costs in **power**, and how
  often it meets the 3850 ms stall the one long capture caught inside a walk.
  The bytes and the nominal wall time are already `MEASURED`; the outlier is
  what O2 option (b) is really being priced on, because it is the one that
  drops rows.

---

## 13. Recommendation, and confidence

**Build the three prerequisites, which no owner answer changes:** the deletion
arm of ADR-0021 decision 7 (§2.4); a coordinate held per key rather than in one
slot (§6); and a parser verdict of **absent / failed / ok** where there is one
boolean today (§7), because the rule that a failed coordinate clears is not
expressible at that seam and the plausible shortcut blanks the arrow on an
ordinary message. Then take O1(b) and O2(a) as the smallest honest selection — a
persisted 32-byte key, a picker over the retained window with `k of N` visible,
explicit commit — and leave the node-wide browse to O2(b) when M40 says it is
needed.

**No ADR is written by this report.** The invariants in §9 are consequences of
ADR-0020, ADR-0021 and ADR-0006 applied to a new question, not new decisions;
and the four questions that *are* decisions are the owner's, which is what
ADR-0021 decision 2 and OD-30 both say in the same words. An ADR recording an
answer the owner has not given would be the exact failure this task was told to
avoid.

**Confidence.**

- **HIGH** — that a separate target-selection contract is needed; that one slot
  is insufficient; that the deletion arm is unpaid; that "recent senders" is a
  subset of the retained window rather than a second source; that the send and
  receive paths disagree about who exists after #600. All five are read directly
  off the code cited above.
- **MEDIUM** — the cache size, the persistence shape, and whether the browse is
  needed. These rest on M39–M41, which are measurements nobody has taken.
- **UNKNOWN / NOT EXECUTED — HARDWARE REQUIRED** — every claim about what two
  physical nodes do to each other, including whether a coordinate-bearing
  message arrives often enough for any of this to be useful (**M29**).
