# Where a *remote* node's coordinate comes from, and what it may be said to mean

Research for [#467](https://github.com/hleserg/Attadipa/issues/467), which is the
one seam [#450](https://github.com/hleserg/Attadipa/issues/450) has left on the
wire half. **Nothing here was executed on hardware.** Every claim about MeshCore
is read from upstream source at named revisions; every claim about a physical
node is `NOT EXECUTED — HARDWARE REQUIRED` and says so where it stands.

[NODE_POSITION_FROM_MESHCORE](NODE_POSITION_FROM_MESHCORE.md) chose
`RESP_CODE_SELF_INFO` for the **connected** node and deliberately deferred both
remote paths — "*Deferred out of the first slice, deliberately*". This document
chooses between them. The answer is **path C, the contact record**, and the
reason is not the one the issue expected: it is not that path C is fresher, but
that **neither path can be fresher than the other**, because they read the same
two variables on the same node — and once freshness is off the table every
remaining axis goes the same way.

## 0. Provenance, and the two revisions

| What | How it was established |
|---|---|
| every MeshCore claim | read from a full checkout of `meshcore-dev/MeshCore` at `0679dbeffc504d562d2f09eb072fdc223f8ffc2a`, cloned 2026-09-07, whose commit date is **2026-08-24** — not the 2026-09-07 the issue implies. The pinned `d929643` is a *different* commit; what makes that harmless is the byte-identical diff in the row below, not an identity. (`git rev-list --count d929643..origin/main` being 0 says only that `origin/main` is not ahead of the pin — an ancestor and an equal both answer 0, so it was never evidence of sameness.) |
| the pin-versus-tip question | `git diff` over all eleven files this report rests on, between `d92964352441e53b93e8667b802e04f6e072b39e` and `0679dbe`: **byte-identical, every one**. §10 lists them |
| `meshcore.js` and `meshcore_py` | full clones at `9e76c51409c13c3ed0183ee1e9c1b380e671a038` (v1.15.0) and `837ac53e77ad75610ceeb0fde4ae318546a10ab9` (v2.3.9.1), both confirmed as their repository's tip on 2026-09-07 |
| the `dt267` fork | cloned. **It publishes no source** — §10.2, and it is the sharpest negative result here |
| anything about a physical node | **`NOT EXECUTED — HARDWARE REQUIRED`.** No Attadipa device has spoken to any MeshCore node over BLE |

Licences: MIT for MeshCore, `meshcore.js` (`LICENSE`, "Copyright (c) 2025-2026
Liam Cottle"), `meshcore_py`, and the `dt267` documentation repository.

## 1. Was the finding still true

**Yes, and it is unchanged.** Checked against `main` at `579bf08`, the revision
the issue was filed against.

- a contact is still a key and a name and nothing else —
  `core/include/attadipa/core/mesh_service.h:27` — "struct MeshPeer {" — with no
  position field to fill;
- `NODE_POSITION_FROM_MESHCORE.md` §6's deferral list named no chosen remote
  path, and the roadmap recorded the choice as owed — *"So what #450 owes on
  this half is which path to pay for"*, in the block that now reads
  `docs/ROADMAP.md:110` — "**That debt was which path to pay for, and it was paid on 2026-09-07.**" —
  because this document is what paid it;
- nothing in `link/` sends `CMD_SEND_BINARY_REQ`, `CMD_SEND_TELEMETRY_REQ` or
  `CMD_GET_CONTACT_BY_KEY`, and nothing handles `0x80`, `0x8A`, `0x8B` or `0x8C`.

**And one thing is more true than the issue knew.** The remote coordinate is
already inside a frame this repository parses and then throws away. §3.1.

## 2. Two of the issue's premises are inverted, and a third is missing

Recorded first because each of the three changes what the rest of the document
can rest on.

### 2.1 `0x8A NEW_ADVERT` is the frame for a contact the node did **not** store

The issue reads: *"новый contact приходит полным `0x8A NEW_ADVERT`; update
существующего — только `0x80 ADVERT`"*. Source says the opposite.
`BaseChatMesh::onAdvertRecv` declares

```cpp
bool is_new = false; // true = not in contacts[], false = exists in contacts[]
```

and **never assigns `true` to it.** The three calls that pass a literal `true`
are the three early returns where the contact is *not* placed in `contacts[]` —
`shouldAutoAddContactType()` refused the type, the hop limit refused the
distance, or `allocateContactSlot()` had nowhere to put it. The fall-through
path, which covers both a pre-existing contact **and** one allocated four lines
above, passes the `false`. `MyMesh::onDiscoveredContact` then branches on it:
`is_new` → `writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact)`, the 148-byte
frame with the coordinates; otherwise → `PUSH_CODE_ADVERT` and a bare 32-byte
key.

Upstream's own comment agrees with the source and not with the name:
`is_new` means *not in `contacts[]`*. So does the second independent client —
`meshcore.js/src/constants.js` annotates `Advert: 0x80, // when companion is set
to auto add contacts` and `NewAdvert: 0x8A, // when companion is set to manually
add contacts`.

**With stock defaults the coordinate-bearing push is close to unreachable.**
`manual_add_contacts` defaults to 0, `MyMesh::shouldAutoAddContactType` returns
`true` unconditionally in that case, and `autoadd_max_hops` defaults to 0, which
is *no limit*. So on a node nobody has reconfigured, every advert that is not
refused by a full table produces `0x80` and a key. A client that waits for
`0x8A` to bring it a coordinate waits forever, and the failure is silent.

### 2.2 A coordinate in a contact record is not evidence that an advert carried it

`CMD_ADD_UPDATE_CONTACT` (9) hands `updateContactFromFrame` the client's frame,
and that function writes `contact.last_advert_timestamp`, `contact.gps_lat` and
`contact.gps_lon` straight out of it, then `MyMesh` sets `lastmod` and schedules
a persist. **Any BLE client of the companion can write any coordinate into any
contact, with any advert timestamp, and it survives a reboot.** No field
distinguishes the result from a signed advert; `meshcore.js` exposes the write
as `sendCommandAddUpdateContact(publicKey, type, flags, outPathLen, outPath,
advName, lastAdvert, advLat, advLon)`.

This matters here because the owner's node is not exclusively ours: the phone
app is another client of the same companion. The advert signature and the replay
gate are real and they protect *adverts*; they do not protect the field this
document proposes to read. §7 is what that costs.

### 2.3 `lastmod` is a last-heard clock, not an advert clock

The issue says `lastmod` advances with the advert. It advances with more than
that. `BaseChatMesh` writes `from.lastmod = getRTCClock()->getCurrentTime()` on
a plain text message and on a signed one, and `onContactPathRecv` writes it
again when a path arrives. `ContactInfo` labels the two fields itself —
`uint32_t last_advert_timestamp;   // by THEIR clock` and
`uint32_t lastmod;  // by OUR clock` — which is upstream stating in a comment
exactly the thing §6 refuses to let a consumer forget.

**The stale-coordinate trap the issue found is real, and confirmed.**
`onAdvertRecv` writes the coordinate only under `if (parser.hasLatLon())` and
writes both timestamps unconditionally, four lines apart:

```cpp
  if (parser.hasLatLon()) {
    from->gps_lat = parser.getIntLat();
    from->gps_lon = parser.getIntLon();
  }
  from->last_advert_timestamp = timestamp;
  from->lastmod = getRTCClock()->getCurrentTime();
```

A node that shared a coordinate and then stopped leaves the old pair in place
under two fresh timestamps. Confidence **HIGH**, source read. The correction to
the issue is only that this is one of *three* reasons neither timestamp is a
coordinate age, not the only one.

## 3. The three coordinates are one coordinate

This is the finding the recommendation actually turns on, and it is one line of
source repeated in three places. Every path reads `sensors.node_lat` /
`sensors.node_lon`:

| Path | Where the bytes come from |
|---|---|
| A — `RESP_CODE_SELF_INFO` | `lat = (sensors.node_lat * 1000000.0)` in the `CMD_APP_START` branch |
| B — `LPP_GPS` in a telemetry reply | `telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude)` in `EnvironmentSensorManager::querySensors` |
| C — the advert | `createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon)`, in all three of `MyMesh::advert()`, `CMD_SEND_SELF_ADVERT` and `CMD_EXPORT_CONTACT` |

And that variable is written in exactly one place from the receiver —
`EnvironmentSensorManager::loop()`, under `if (_location->isValid())`, which
[NODE_POSITION_FROM_MESHCORE](NODE_POSITION_FROM_MESHCORE.md) §2 already
established is a **write** gate and not a **publish** gate. So:

> **No remote path can be fresher at the source than any other, because there is
> no other source.** The freshness argument that would normally decide between a
> pull and a push does not exist here. What differs between B and C is who
> initiates, how the bytes are scaled, which gate is in front, and whether the
> answer says who sent it — and every one of those goes the same way.

`gps_update_interval_sec` is 1 s by default and the setter clamps a zero to 1,
so on a node with a running receiver the variable tracks the last *valid* fix at
1 Hz and holds it indefinitely when the fix goes. `_prefs.gps_enabled` is 0 by
default — the loop above does not run at all until somebody turns the receiver
on.

### 3.1 And path C's bytes are already inside a frame this repository parses

`writeContactRespFrame` builds 148 bytes:

```text
offset   0        RESP_CODE_CONTACT (3) | PUSH_CODE_NEW_ADVERT (0x8A)
         1 ..  32 pub_key                          32 bytes
        33        type    (ADV_TYPE_*)
        34        flags   (bit 0 = favourite; upper bits = telemetry perms)
        35        out_path_len                     0xFF = OUT_PATH_UNKNOWN
        36 ..  99 out_path                         MAX_PATH_SIZE = 64
       100 .. 131 name                             32, zero-padded
       132 .. 135 uint32 last_advert_timestamp     THEIR clock
       136 .. 139 int32  gps_lat  = degrees x 1e6  little-endian
       140 .. 143 int32  gps_lon  = degrees x 1e6  little-endian
       144 .. 147 uint32 lastmod                   OUR companion's clock
```

The arithmetic is confirmed from two independent directions, exactly as path A's
was. `meshcore.js`'s `onContactResponse` reads the same ten fields in the same
order and calls the last three `advLat`, `advLon`, `lastMod`; and this
repository already demands all 148 bytes before it will look at one —
`link/src/meshcore_companion.cpp:769` — "        if (size < 148) { ++malformed_frames_; return false; }".

`accept_contact` then reads the key at 1, filters on
`link/src/meshcore_companion.cpp:416` — "    if (size < 148 || data[33] != kAdvertTypeChat) {" —
and copies the name from 100. **Bytes 132–147 are present in every contact frame
this watch has ever parsed, and they are discarded.** That is the same shape as
#412's discovery about bytes 36–43 of `RESP_CODE_SELF_INFO`, one frame over.

`MESHCORE_BLE_FRAME_CAPACITY.md:198` — "Everything else fits comfortably: `RESP_CODE_CONTACT` is **148** bytes" —
already settled that the frame fits the link.

## 4. The decision table

| | **B — binary telemetry** (`CMD_SEND_BINARY_REQ` → `0x8C`) | **C — the contact record** (`0x80`/`0x8A`/`RESP_CODE_CONTACT`) |
|---|---|---|
| **Who initiates** | the watch | the target node |
| **Scaling** | ×10⁴ — ≈ **11.1 m**, truncated toward zero | ×10⁶ — ≈ **0.11 m** |
| **New wire work here** | a command builder, `RESP_CODE_SENT` correlation, a `0x8C` handler, a timeout policy, a serialisation lock, **and an LPP decoder for untrusted bytes** | a `0x80` handler, one `CMD_GET_CONTACT_BY_KEY`, and 16 bytes of a frame already parsed |
| **Gate on the target** | `telemetry_mode_loc` ≠ `TELEM_MODE_DENY` ∧ contact flags ∧ requester's inverse mask ∧ `gps_active` | `advert_loc_policy` = `ADVERT_LOC_SHARE` |
| **Default state of that gate** | **shut** — `telemetry_mode_loc` is never assigned in the defaults block, so it is `NodePrefs`'s 0 = `TELEM_MODE_DENY`; `gps_enabled` is explicitly 0 | **shut** — `advert_loc_policy` is never assigned either, so it is 0 = `ADVERT_LOC_NONE` and the node's adverts carry no coordinate at all |
| **Gate on our companion** | the target must be a contact (full-key `lookupContactByPubKey`, else `ERR_CODE_NOT_FOUND`) | the target must be a contact. The watch's 16-peer retention is **not** a second gate: it caps storage into `peers_` after the frame is already parsed — `link/include/attadipa/link/meshcore_companion.h:176` — "    static constexpr std::size_t kRetainedPeers = 16;" — so it limits enumeration, not delivery |
| **Radio cost per read** | a request packet and a response packet, plus flood or a direct path | **none** — the advert was sent for its own reasons |
| **Node-side concurrency** | takes the node's **single global pending slot**: `clearPendingReqs()` zeroes `pending_login`, `pending_status`, `pending_telemetry`, `pending_discovery` and `pending_req` together, and each send sets exactly one | none |
| **Identity in the answer** | **none.** `0x8C` is `[0x8C][0][tag×4][payload…]`. The legacy `0x8B` carries a 6-byte key prefix and drops the tag | **the full 32-byte public key**, in every contact frame and in the `0x80` push |
| **Failure that looks like success** | permission off, `gps_active` off, and a node with no receiver all return a well-formed reply **with no `LPP_GPS` record** | a location-omitting advert leaves the previous coordinate under two fresh timestamps (§2.3) |
| **Cadence** | the watch's, bounded, paid in airtime | **the target's, and unbounded** — §5.3 |
| **Altitude** | carried, datum `UNKNOWN` (M26) | not carried |

Nothing in the table is close except the last row, and §9 is where it is paid
for rather than argued away.

## 5. Path C's lifecycle, in full

### 5.1 What the node does when an advert arrives

1. `AdvertDataParser` runs on the app data. `ADV_LATLON_MASK` is `0x10` in the
   first byte; the coordinate is two `int32` at offsets 1 and 5, **the node's
   native byte order** through `memcpy`, little-endian on every board in the
   fleet. `hasLatLon()` reports the flag, not the value.
2. **The parser range-checks nothing.** `getIntLat()` can return any `int32` —
   ±2 147 483 647, which is ±2147°. A decoder that trusts it and multiplies has
   already overflowed. §9's bound is on the raw integer.
3. The parser also reads before it validates: `_lat`, `_lon`, `_extra1` and
   `_extra2` are `memcpy`'d out of `app_data` and only then is `app_data_len >= i`
   tested. Node-side, not ours, and recorded because it is the same defect class
   as `LPPReader` (`MESHCORE_PARSER_BOUNDS.md`) in a second file.
4. A contact already known is subject to the replay gate —
   `if (timestamp <= from->last_advert_timestamp) return;` — which is a
   monotonicity check **on the sender's clock**. It proves ordering. It does not
   prove the coordinate changed, or that it is current, or that it came from a
   receiver.
5. Then §2.3's write: coordinate conditionally, both timestamps unconditionally.
6. `onDiscoveredContact` emits `0x80` or `0x8A` per §2.1, and schedules a lazy
   persist 5 s out for stored contacts.

### 5.2 What a client must do about it

`0x80` carries the key and nothing else, so a client that wants the coordinate
must ask. Two commands can answer, and the reference clients take the wrong one
for this job.

- `CMD_GET_CONTACTS` (4) with the optional 4-byte `since` returns
  `RESP_CODE_CONTACT` for **every** contact whose `lastmod` exceeds it, then
  `RESP_CODE_END_OF_CONTACTS` carrying `_most_recent_lastmod`. This is what
  `meshcore_py` does: `ADVERTISEMENT` and `PATH_UPDATE` both mark the table
  dirty and `ensure_contacts(follow=True)` calls
  `get_contacts(lastmod=self._lastmod)`.
- `CMD_GET_CONTACT_BY_KEY` (30) takes a full 32-byte key, answers with one
  `RESP_CODE_CONTACT` or `ERR_CODE_NOT_FOUND`, and touches no iterator state.

**`CMD_GET_CONTACT_BY_KEY` is the right one and the reference clients do not
implement it** — `meshcore.js`'s `CommandCodes` has no 30 at all. Three reasons
it wins for a navigation target:

- it is **bounded by construction**: one command, one answer. `CMD_GET_CONTACTS`
  on a `MAX_CONTACTS=350` T114 build can answer with 350 frames of 148 bytes,
  which is ~52 kB over a link whose notifications carry 173;
- it is **narrow**: we want one contact, and asking for the table to get it is
  the shape a privacy review objects to;
- it **cannot abort a contacts iteration**, which is the cost `CMD_APP_START`
  carries and which is filed as **M27**.

And `CMD_GET_CONTACTS`'s cursor has a trap worth writing down even though we are
not taking it: `_most_recent_lastmod` is reset to 0 at the start of each
iteration and updated **only inside the `since` filter**, so a delta that matches
nothing reports 0. A client that stores the reported value verbatim — which
`meshcore_py` does — resets its cursor and does a full table sync next time. If
anything here ever uses `since`, it advances it as `max(previous, reported)`.

### 5.3 The cadence, which is path C's one real defect

**A companion node has no periodic advert.** `advert_interval` and
`flood_advert_interval` exist in `CommonCLI`'s prefs and are used by
`simple_repeater` and `simple_room_server`; in the companion's `NodePrefs` they
are commented out:

```cpp
    //def("adv_int", advert_interval);
    //def("f_adv_int", flood_advert_interval);
```

`MyMesh::advert()` has exactly three callers, all of them a user action on the
node's own screen — `ui-new/UITask.cpp` on `KEY_ENTER` at the `ADVERT` page,
`ui-tiny` likewise, and `ui-orig`'s `handleButtonDoublePress`. It sends
`sendZeroHop(pkt)`, so it does not even flood. The only other transmit is
`CMD_SEND_SELF_ADVERT` (7) from the target's *own* client, which floods when its
second byte is 1.

So the refresh rate of a remote target's coordinate is **a human pressing a
button on the target device**, and no reading, no polling and no configuration
on this side changes it. That is not a number to be estimated; it is the absence
of a number.

Two consequences, and both are load-bearing:

- **the honest readout for a path-C target is `NodePositionStale` most of the
  time.** `apps/include/attadipa/apps/navigation.h:77` — "    core::Millis target_stale_after{120000};" —
  is two minutes of *arrival* age, and
  `apps/src/navigation.cpp:191` — "state.target.position.age_at_us_ms >= state.target_stale_after" —
  is the line that says so. **That is the correct answer and must not be
  engineered away** by re-stamping arrival on every resync;
- **path B is the only thing that can pull.** Which is why §9 defers it with a
  named trigger rather than rejecting it.

### 5.4 The rest of the lifecycle

| Event | What the source says, and what a consumer must do |
|---|---|
| **new contact, auto-add on** | stored, announced by `0x80` + key. Resync by key |
| **new contact, auto-add off for the type** | **not** stored, announced by `0x8A` with the full record. Usable directly; there is nothing to resync against, and a later `CMD_GET_CONTACT_BY_KEY` for it answers `ERR_CODE_NOT_FOUND` |
| **hop limit refused it** | same as above — `0x8A`, not stored |
| **table full** | `0x8A`, then `PUSH_CODE_CONTACTS_FULL` (0x90). `shouldOverwriteWhenFull()` is `(autoadd_config & AUTO_ADD_OVERWRITE_OLDEST)`, and `autoadd_config` defaults to 0, so the default node **drops** rather than evicts |
| **eviction, when it is enabled** | `allocateContactSlot` picks the oldest non-favourite by `lastmod` and the node pushes `PUSH_CODE_CONTACT_DELETED` (0x8F) + full key. A target that is evicted stops being answerable and the client is told which key went |
| **client deletes the contact** | `CMD_REMOVE_CONTACT` (15) — another client of the same node can do this to our target |
| **replayed or reordered advert** | dropped by `timestamp <= last_advert_timestamp`. Nothing reaches the client, so a target whose clock ran backwards goes quiet rather than wrong |
| **target reboots with a bad clock** | its `last_advert_timestamp` can go backwards, and then the replay gate silences it against our companion's stored value **until its clock passes the old one**. This is a real, source-visible way for a target to become permanently unheard |
| **our companion reboots** | `bootstrapRTCfromContacts()` sets its RTC to `max(lastmod) + 1`. So `lastmod` after a reboot is derived from the contact table it is describing |
| **BLE detach / reconnect** | contacts are persisted on the node; a new session re-reads them. Nothing about the coordinate is per-session |
| **identity changes** | a new key is a new node. The pinned-key machinery already refuses a wrong node's `RESP_CODE_SELF_INFO`, and a target is named by full key, so a regenerated key is a target that no longer exists rather than a target that moved |

## 6. Four ages, and none of them is the one that matters

| # | Age | Where it comes from | What it bounds |
|---|---|---|---|
| 1 | **remote observation age** — how long ago the target's receiver solved this fix | nowhere. No wire carries it, at either revision | the thing a navigator actually needs. **`UNKNOWN`** |
| 2 | **advert sender timestamp** — `last_advert_timestamp`, bytes 132–135 | the target's own clock, unauthenticated as a clock and freely writable by any client of our companion (§2.2) | when the target *sent an advert*. Not when the coordinate in it was measured, and not even that the advert carried a coordinate (§2.3) |
| 3 | **companion update time** — `lastmod`, bytes 144–147 | our companion's RTC, which is itself bootstrapped from `lastmod` at boot | when our companion last **heard anything at all** from that contact — including a text message and a path update |
| 4 | **watch arrival time** | our own monotonic, at the frame | how long the *watch* has held these bytes. Exact, and the only honest one |

Ages 2 and 3 are on two different clocks and upstream's own comments say so.
Age 1 is `UNKNOWN` and no combination of requests changes it, which
`NODE_POSITION_FROM_MESHCORE.md` §2 established for paths A and B and which §3
above extends to C by the same variable.

**So the mapping is the one this tree already has.** `age_at_us_ms` from arrival;
`age_at_source_ms` has no defensible value and the `Timed<T>` it rides in has no
representation for that —
`core/include/attadipa/core/availability.h:72` — "std::uint32_t age_at_source_ms = 0;  // how old it was when the" —
so the published `validity` is `Validity::Unknown` and a consumer reads it
first. Ages 2 and 3 are shown *as themselves*, labelled with whose clock they
are on, or not shown at all. They are never summed, never differenced against
our clock, and never mapped onto `age_at_source_ms`.

`PositionValidity` is `NoFix` at every age, unchanged, for the same reason as
path A: `link/src/node_position_provider.cpp:38` — "    out.observation.fix_type = core::FixType::Unknown;" —
and the classifier answers `NoFix` before it looks at the coordinate or the
clock. `Valid` is not reachable and no provider may set it.

## 7. Identity: what may name a target, and what may not

**A target is named by its full 32-byte public key and by nothing else.** The
key is explicit in every frame path C uses — the `0x80` push is
`[0x80][pub_key×32]`, `0x8A` and `RESP_CODE_CONTACT` carry it at offset 1, and
`CMD_GET_CONTACT_BY_KEY` is answered by full-key `lookupContactByPubKey`.

Refused, each for its own reason:

- **the advertised name.** Two nodes in this fleet differ by an emoji —
  `TEST_FLEET.md:92` — "the T114 answers `Beta test companion` and the" — and
  `StrHelper::strncpy` into a 32-byte field truncates without complaint;
- **a 6-byte prefix.** `find_peer_prefix` already matches on six bytes for
  message attribution — `link/src/meshcore_companion.cpp:444` — "        if (std::memcmp(peers_[i].id.public_key.data(), prefix, 6) == 0) {" —
  and that is defensible for *labelling a message that already arrived*. It is
  not defensible for *choosing which coordinate is the destination*: 48 bits is
  a birthday collision at ~16 M keys and a deliberate collision at far less, and
  the consequence of getting it wrong is an arrow pointing at somebody else;
- **arrival order.** `advertises_meshcore()` connects to whichever
  advertisement arrives first — filed as
  [#304](https://github.com/hleserg/Attadipa/issues/304) — so order is not a
  choice this firmware makes;
- **a `0x8C` tag.** `tag = getRTCClock()->getCurrentTimeUnique()` — a wall-clock
  second on a clock that `bootstrapRTCfromContacts()` can move backwards at
  boot. It is a correlation handle chosen by the companion, not a nonce, and it
  is the *only* thing a `0x8C` frame carries about who answered.

**And the identity path C gives is an identity of the record, not of the
coordinate.** §2.2 is why: the record is writable by another client. What the
full key buys is that the coordinate we read is the one *our companion holds for
that key* — which is exactly the claim §9 lets the readout make and no more.

## 8. Privacy, airtime and who owns the cadence

- **Path C is a broadcast.** Turning `advert_loc_policy` to `ADVERT_LOC_SHARE`
  on a node publishes its coordinate to every receiver in range, signed, with no
  recipient list — it is not a grant to *us*. That is a decision for whoever owns
  the target node and it is bench configuration, not firmware. This watch never
  sets it: nothing here sends `CMD_SET_OTHER_PARAMS`, and nothing should.
- **Path C costs this watch no airtime at all.** It reads a record the companion
  built from a packet it received anyway.
- **Path B is per-contact and revocable**, which is the one axis where it is the
  better citizen: `telemetry_mode_loc = TELEM_MODE_ALLOW_FLAGS` plus the
  contact's own flag bit grants location to one requester. Its cost is two
  packets per read plus the node's single pending slot, which the messaging path
  also wants.
- **No polling, on either path.** A coordinate whose source age is `UNKNOWN`
  does not become fresher by being asked for more often — the same argument
  `NODE_POSITION_FROM_MESHCORE.md` §5 makes, and it is stronger here because on
  path C there is nothing to ask.
- **Cadence belongs to the existing owners.** The session is ADR-0015's, the
  power budget is ADR-0016's, and the only event-driven work path C adds is one
  `CMD_GET_CONTACT_BY_KEY` per `0x80` **whose key equals the selected target** —
  a filter that is exact and free, because the push carries the full key.

## 9. The recommendation

**Take path C. Read the remote target's coordinate out of the companion's
contact record, keyed by full public key. Defer path B behind a named trigger.**

Confidence **HIGH** on everything read from source and on the comparison;
**`UNKNOWN`** on every number that needs a node in a room, and the trigger in
the last bullet is one of them.

The slice, at its smallest:

1. handle `PUSH_CODE_ADVERT` (0x80) and compare its 32-byte key to the selected
   target key. Ignore every other key;
2. on a match, send `CMD_GET_CONTACT_BY_KEY` (30) with the full key;
3. handle `PUSH_CODE_NEW_ADVERT` (0x8A) as a contact frame that needs no round
   trip — the record is inline;
4. read bytes 136–143 of the contact frame the session already parses, under
   §9.1's rules, and publish it as the target position;
5. handle `PUSH_CODE_CONTACT_DELETED` (0x8F) for the target key and discard the
   retained coordinate. Decision 7 refuses ageing it and §12.1 tests it, so this
   is part of the slice and not a later refinement;
6. handle `PUSH_CODE_CONTACTS_FULL` (0x90) and surface it, because a target that
   never appears is otherwise indistinguishable from one that does not exist.

**Implementation note that will otherwise be found late:** `RESP_CODE_CONTACT`
(3) is *also* the contact-iteration frame, and `accept_contact` populates a peer
array that `kResponseContactsStart` resets and `peers_retained` counts. A
targeted read's answer arrives on the same code and must not be appended to that
array or the retained count is wrong. The two readers are told apart by whether
an iteration is in progress, which the session already tracks.

### 9.1 Fail-closed rules, and each one is a way this goes wrong

| Rule | Why |
|---|---|
| The coordinate is admitted **only** from a contact frame whose full 32-byte key equals the selected target key | §7 |
| A `0x80` shorter than **33 bytes** is refused and counted malformed, in the new arm's **own** guard | `0x80` is `[0x80][pub_key×32]` (§7). The dispatcher owns no shared bound — `link/src/meshcore_companion.cpp:659` — "size > kMeshCoreFrameBytes" — rejects only an empty or over-long frame, and **no** arm after it inherits a bound — each one that reads a fixed-size field carries its own check. An arm that inherits a guard it does not have reads 32 bytes off the end of a one-byte frame |
| A `0x8A` shorter than **148 bytes** is refused and counted malformed, in the new arm's **own** guard | It is §3.1's contact layout under a different opcode, and the coordinate is at its far end, bytes 136–143. The 148-byte guard that exists today is one `case` arm and covers `RESP_CODE_CONTACT` alone — `link/src/meshcore_companion.cpp:769` — "        if (size < 148) { ++malformed_frames_; return false; }". A new opcode does not inherit it |
| A `0x8F` shorter than **33 bytes** is refused and counted malformed, in the new arm's **own** guard | It carries `0x80`'s `[opcode][pub_key×32]` shape and is compared against the target key the same way, so it over-reads a one-byte frame by the same 32 bytes. Nothing handles `0x8F` today — it falls to the dispatcher's catch-all — `link/src/meshcore_companion.cpp:1005` — "    default:" — so it is a new arm on exactly `0x80`'s footing, inheriting exactly as little |
| A `0x90` carries nothing past its opcode, and the arm reads nothing past it | `PUSH_CODE_CONTACTS_FULL` is a bare notification. The rule is written down so the arm is implemented that way rather than reaching for a payload that is not there; the dispatcher's own `size == 0` rejection is the only bound it needs |
| Exactly `(0, 0)` is **refused** and the target slot stays empty | `populateContactFromAdvert` `memset`s the record and writes the coordinate only under `hasLatLon()`, so a contact that has never shared one reads exactly `(0,0)`. ADR-0019 already refuses the same value for `own`, for the same reason, one slot over |
| `\|raw_lat\| > 90 000 000` or `\|raw_lon\| > 180 000 000` ⇒ the coordinate is refused, **checked on the raw `int32` before any scaling** | The wire is degrees × 10⁶ (§3.1), so ±90° is 90 000 000 and ±180° is 180 000 000 — a bound of 900 000 would refuse everything outside 0.9° of the equator and 1.8° of Greenwich, silently, because an absent coordinate is deliberately not an error. `AdvertDataParser` range-checks nothing and `CMD_ADD_UPDATE_CONTACT` range-checks nothing. `raw × 10` overflows `int32` above 214 748 364, and `core/include/attadipa/core/position.h:55` — "constexpr bool in_range(Position p)" — cannot save a value that already overflowed |
| Scaling is exact integer arithmetic: `latitude_e7 = raw_e6 × 10` | `Position` is `e7`, the wire is `e6`, the ratio is 10. No floating point, no rounding decision to get wrong |
| `fix_type` is `FixType::Unknown`, `source` is `PositionSource::NodeGnss`, every optional stays empty, `PositionValidity` is `NoFix` at every age | §6, and it is exactly what the path-A provider already does |
| `age_at_source_ms` is meaningless and the published `Timed` carries `Validity::Unknown`; **a consumer reads `validity` before either age** | §6 |
| `last_advert_timestamp` and `lastmod` are displayed as themselves with their clock named, or not at all | §6 |
| A target coordinate older than `target_stale_after` reads `NodePositionStale`, and the resync **does not re-stamp arrival** unless the bytes changed | §5.3. Re-stamping an unchanged coordinate would manufacture the freshness the whole document says does not exist |
| An unchanged coordinate read twice is evidence **against** a live fix | `NODE_POSITION_FROM_MESHCORE.md` §6.1, unchanged |
| `ERR_CODE_NOT_FOUND` to `CMD_GET_CONTACT_BY_KEY` ⇒ the target is not on this companion. `Availability::Ready`, coordinate absent, `NavStatus::NodePositionUnknown` — **not** an error and not `Failed` | The node answered correctly. Nothing is broken |
| The answer to `CMD_GET_CONTACT_BY_KEY`, `RESP_CODE_CONTACT` or `RESP_CODE_ERR` alike, is attributed to **that** command by the sequence this session already stamps, and is **never charged to the messaging operation** | A `RESP_CODE_ERR` is two bytes and carries nothing saying which command it answers, so attribution here is by order and not by content: each asynchronous claimant stamps `tx_seq_` as it sends — `link/src/meshcore_companion.cpp:798` — "                custom_vars_seq_ = tx_seq_;" — and the error goes to the oldest claimant still owed one — `link/src/meshcore_companion.cpp:994` — "custom_vars_seq_ < op_seq_". That ladder has exactly **two** claimants today, and everything that does not enter it falls to the send — `link/src/meshcore_companion.cpp:999` — "        if (send_busy()) {". A third claimant left outside it fails a message the node **accepted**: `send_busy()` stays true through `awaiting_confirm_`, where what is outstanding is a radio round trip rather than a response, and the arm's own comment records this exact defect being found and fixed once already for opcode 40 |
| `ERR_CODE_UNSUPPORTED_CMD` (1) to `CMD_GET_CONTACT_BY_KEY` ⇒ the read did not happen: `Availability::Ready`, coordinate absent, `NavStatus::NodePositionUnknown`, no malformed-frame count, no retry — and **no conclusion about the node's firmware** | It is *not* proof the node is too old. A defined command whose frame fails the node's own guard returns the same code — `docs/research/MESHCORE_COMPANION_PROTOCOL.md:525` — "`ERR_CODE_UNSUPPORTED_CMD` (1)," — so a bug in our own frame and an old node are the same two bytes on the wire. Reporting "your node is too old" from it would state as fact the one thing this error cannot establish |
| A `RESP_CODE_ERR` attributed to any **other** command leaves the target coordinate untouched | The clearing rules above are rules about opcode 30's answer, not about the code. A `CMD_SEND_LOGIN` failure — which the same arm's comment says arrives "here and nowhere else" — must not discard a coordinate it knows nothing about |
| `PUSH_CODE_CONTACT_DELETED` (0x8F) for the target key ⇒ the retained coordinate is **discarded**, not aged | The record it came from is gone. Ageing it would present a coordinate whose provenance no longer exists |
| The 16-peer retention caps **enumeration**, not the read, and `peers_truncated` must reach the operator as a statement about the list they choose from | `kRetainedPeers = 16` against `MAX_CONTACTS=350` on the T114 build. A target beyond the sixteenth is still readable by key — `accept_contact` copies key and name out of the frame *before* the cap is consulted, and the cap then decides storage alone — so what goes missing is the target's appearance in a list, not its coordinate |

### 9.2 What would make path B the answer instead

Not "if the LPP decoder gets written". Three things, and they are measurements:

- the target's advert cadence is measured and is unusable for the wearer's task
  (§5.3 predicts it, from source, but predicts nothing about how a real person
  uses the button);
- the owner of the target node is willing to set `telemetry_mode_loc` and
  `gps` on it, which is a per-contact grant rather than a broadcast;
- 11.1 m is enough for the readout, which for a `742 m` walk-to-a-node it plainly
  is.

If it is taken, the request body is
`[0x03][~(TELEM_PERM_BASE | TELEM_PERM_LOCATION)][0][0][0][rand×4]` through
`CMD_SEND_BINARY_REQ` (50) — the shape upstream itself uses for
`CMD_SEND_PATH_DISCOVERY_REQ` — and **not** the one-byte body `meshcore_py`
sends. `onContactRequest` reads `data[1]` as the inverse mask with no length
check; a one-byte body leaves that byte to AES's zero padding, which happens to
give `0xFF` and therefore happens to work. Relying on the padding is relying on
an accident, and the explicit mask costs one byte and asks for less.
Correlation is by tag **held against the full target key in our own state**,
strictly serialised because the node has one pending slot anyway, with any
response arriving while nothing is pending dropped. The legacy `0x8B` path is a
compatibility fallback only, and it is worse on the axis that matters: it drops
the tag.

### 9.3 What this does not decide

Which slot the coordinate fills is ADR-0019's, already decided, and unchanged:
a confirmed companion fills `own`, everything else is `target`. The heading, the
magnetometer and the haptics are #450's other stages and gated elsewhere. No
fusion, no map, no second HAL, and no change to any node's firmware.

## 10. Compatibility

### 10.1 The pin and upstream's tip are the same bytes

`git diff d92964352441e53b93e8667b802e04f6e072b39e 0679dbe` over every file this
report rests on, run 2026-09-07:

```text
IDENTICAL  src/helpers/AdvertDataHelpers.h        src/helpers/AdvertDataHelpers.cpp
IDENTICAL  src/helpers/BaseChatMesh.cpp           src/helpers/BaseChatMesh.h
IDENTICAL  src/helpers/ContactInfo.h              src/helpers/SensorManager.h
IDENTICAL  src/helpers/sensors/LPPDataHelpers.h
IDENTICAL  src/helpers/sensors/EnvironmentSensorManager.cpp
IDENTICAL  examples/companion_radio/MyMesh.cpp    examples/companion_radio/MyMesh.h
IDENTICAL  examples/companion_radio/NodePrefs.h
```

So the free bench T114 on `v1.17.1-d929643` and upstream `main` speak the same
protocol here, and a bench result on that node is evidence about both.

### 10.2 The V4.3's firmware cannot be read, and that is a stronger statement than "UNKNOWN commit"

The issue records the fork's exact commit as `UNKNOWN`. It is worse than that.
**`dt267/MeshCore-Low-Power-Firmware` publishes no source.** Its repository, at
every one of its commits and at `origin/main` (`5048e00`, 2026-09-06), contains
nine Markdown files and a `LICENSE` — no `src/`, no `examples/`, no
`platformio.ini`. Firmware is distributed as release binaries.

The release `MeshCore-low-power-v1.17.dev_0809`, published 2026-08-09, matches
the version string measured on the V4.3 — `v1.17.dev`, `9 Aug dt267`,
`MESHCORE_T114_FIRST_CONTACT.md:61` — "9 Aug dt267" — so that release is the
**candidate**, and matching a version string to a release name is not reading a
firmware. The corresponding source commit `cac4769` contains documentation only.

Consequences, and they are decisions rather than caveats:

- **no claim in this document is asserted about the V4.3.** It is not a
  compatibility gap to be closed by reading; there is nothing to read;
- the fork's own release notes describe a companion display UI with its own
  advert controls, so §5.3's "no periodic advert" is a claim about **vanilla**
  and is not carried across;
- **the free T114 is the node any path-C bench run uses**, and the V4.3 is the
  remote target only if the run is willing to have one end be a black box. A run
  that wants both ends readable uses two pin-matched nodes, which this fleet
  does not currently have.

Filed as **M28**.

## 11. Reuse

| Candidate | Revision · licence | What it gives | Decision |
|---|---|---|---|
| MeshCore companion firmware | `0679dbe` (= the pin, §10.1) · MIT | the wire truth: advert encoding, the contact frame, the gates, the lifecycle, `CMD_GET_CONTACT_BY_KEY` | **Protocol evidence.** Not a dependency; this repository links no MeshCore |
| `meshcore.js` | `9e76c51` v1.15.0 · MIT · active 2026-09-07 | an **independent** parse of the 148-byte contact frame and of `0x80`/`0x8A`, agreeing field for field; and its constants annotate the auto-add/manual-add split §2.1 corrects the issue with | **Read, do not depend.** JavaScript. `package.json`'s `test` script is `echo "Error: no test specified" && exit 1` — there is no suite behind it. It has no `CMD_GET_CONTACT_BY_KEY` |
| `meshcore_py` | `837ac53` v2.3.9.1 · MIT · active 2026-09-03 | the reference resync (`ADVERTISEMENT` → dirty → `get_contacts(lastmod)`), the single `_mesh_request_lock`, and a tag-to-request table that keeps the peer beside the tag | **Read, do not depend.** Python cannot enter an ESP-IDF image. Take the lock idea and the tag-plus-key pairing; **do not** take `get_contacts` as the resync (§5.2), the verbatim `lastmod` cursor (§5.2), the `/800` timeout constant, or the one-byte telemetry body (§9.2) |
| ElectronicCats CayenneLPP | `a83f3e4` 1.6.1 · MIT | the writer of record for path B's bytes | Unchanged from `NODE_POSITION_FROM_MESHCORE.md` §7: **reject as a dependency**, keep as the golden-vector writer. Only relevant if §9.2's trigger fires |
| MeshCore in-tree `LPPReader` | pinned · MIT | layout evidence | **Do not copy.** Known OOB read and a wrapping `uint8_t` cursor |
| `dt267/MeshCore-Low-Power-Firmware` | `5048e00` · MIT (documentation) | nothing readable | **Not a source.** §10.2 |

No new production dependency is proposed. Path C adds no parser that does not
already exist: the contact frame is parsed today and the change is which of its
bytes are read.

## 12. Tests

### 12.1 Host and replay, runnable now

Frame level. For `RESP_CODE_CONTACT` the length check the companion already
owns is above these: the `size < 148` guard drops a short contact frame before
any consumer sees it, and `tests/test_meshcore_companion.cpp` already covers
that case, so a short-frame test in the consumer would go green without the
shipping path reaching it. **That holds for that one opcode and no other.** The
guard is a single `case` arm, the four arms §9 adds inherit nothing from it, and
so their length rules are tested here rather than assumed:

- `{0x80}` alone and `{0x8A}` alone ⇒ counted malformed; no key compared, no
  command sent, no coordinate read. These tests build frames as exact-sized
  stack arrays — `tests/test_meshcore_companion.cpp:906` — "    const std::uint8_t short_contact[] = {3};" — so an arm that trusts its length over-reads that array by 32 bytes and by 143;
- one byte short of each bound ⇒ still refused; exactly at it ⇒ accepted;
- `{0x8F}` alone ⇒ counted malformed, with no key compared and **no coordinate
  discarded** — a short delete must not become a delete;
- a contact frame whose coordinate is exactly `(0, 0)` ⇒ refused, slot empty;
- ±90 / ±180 ×10⁶ **accepted at the boundary**; one LSB beyond ⇒ refused;
- `INT32_MIN` and `INT32_MAX` in either field ⇒ refused **before** scaling, and
  the test asserts no intermediate overflow (a raw value is checked, not a
  scaled one);
- a coordinate at the equator, at the prime meridian, and negative in each field;
- `data[33] != ADV_TYPE_CHAT` ⇒ the frame is not a target candidate.

Lifecycle:

- `0x80` for the target key ⇒ exactly one `CMD_GET_CONTACT_BY_KEY`, carrying the
  full 32 bytes;
- `0x80` for **any other** key ⇒ no command at all. This is the rate-limit and
  it is the test that stops a busy mesh from becoming a command storm;
- two `0x80`s for the target before the first answer ⇒ still one outstanding
  read;
- `0x8A` for the target ⇒ the coordinate is taken inline and **no** round trip;
- `ERR_CODE_NOT_FOUND` ⇒ `Ready`, no coordinate, `NodePositionUnknown`, no
  malformed-frame count;
- `ERR_CODE_NOT_FOUND` for the targeted read arriving **while a mesh message is
  in `awaiting_confirm_`** ⇒ the read reports `NodePositionUnknown` and the
  message stays outstanding: `MeshDelivery::Failed` is **not** set and the
  confirmation that lands afterwards is still counted. Without this test the
  slice reintroduces, for opcode 30, the defect the `RESP_CODE_ERR` arm's
  comment records having been fixed for opcode 40;
- `ERR_CODE_UNSUPPORTED_CMD` for the targeted read ⇒ `Ready`, no coordinate, no
  malformed-frame count, no retry, and nothing recorded about the node's
  firmware version;
- `PUSH_CODE_CONTACT_DELETED` for the target ⇒ the retained coordinate is
  discarded, not aged;
- `PUSH_CODE_CONTACTS_FULL` ⇒ surfaced, and the target may simply never appear;
- a targeted `RESP_CODE_CONTACT` arriving **outside** an iteration does not
  enter `peers_` and does not change `peers_retained` — the §9 implementation
  note, as a test;
- a targeted answer arriving **during** an iteration is not double-counted;
- 17 chat contacts ⇒ `peers_truncated` is set, **and the 17th is still readable
  by key**: the retention caps the list, not the read, so a test asserting the
  17th is absent would pin the opposite of what the code does.

Semantics, which are the tests that matter:

- **every target observation classifies `NoFix`**, at age 0 and at an hour;
- **no input produces `PositionValidity::Valid`**, none `Degraded`, none `Stale`;
- every published `Timed<Position>` carries `Validity::Unknown`, and the fixture
  asserts *that* rather than the value of `age_at_source_ms`;
- an identical coordinate delivered twice refreshes neither age;
- `last_advert_timestamp` and `lastmod` never reach `age_at_source_ms`, and the
  test asserts the field is untouched rather than asserting a value;
- a target whose `last_advert_timestamp` moves forward while `gps_lat`/`gps_lon`
  do not — the §2.3 trap — produces **no** change in either age. This is the
  single most important test in the list, because it is the one that fails if
  somebody later "improves" freshness by reading the timestamp;
- a coordinate written by another client (§2.2) is **indistinguishable** from an
  advert-derived one, and the test asserts the indistinguishability rather than
  papering over it.

Replay, through the existing rig: a plausible target coordinate, then the target
stops sharing location while continuing to advert, for an hour. The fixture must
end `NoFix`, must never have been `Valid`, and the readout must have reached
`NodePositionStale` by 2 minutes of arrival age.

### 12.2 Simulator

A fake target provider drives the availabilities a node-backed target can
produce — `Unprovisioned` (no target selected), `Unreachable`, `Failed` (the
transport fault) and `Ready` — and, under `Ready`, both "coordinate present,
`NoFix`" and "no coordinate", so the seven-sentence discipline of ADR-0004 §3 is
exercised. The consumer never learns which frame answered. `Off` has no producer
here for the same reason it has none on path A. If UI changes, the
`watch-ui-testing` skill and a look at the rendered image, per `AGENTS.md`.

### 12.3 Physical, two nodes — `NOT EXECUTED — HARDWARE REQUIRED`

Bench-authorised nodes only, per [TEST_FLEET](TEST_FLEET.md). **Do not flash the
Home Assistant node, the Room Server or the repeater**, and do not write to the
MeshCore node on the host — `BENCH_DEVICES.md:113` — "is not ours to write to.** It is a MeshCore node somebody".
The free T114 stays on `v1.17.1-d929643`.

Roles: the **free T114** as the connected companion (§10.1 makes it the readable
end), the **V4.3** as the remote target, with §10.2's caveat that the target end
is a black box. Swap the roles only if the owner authorises it; the T114's pin is
the reason it is not to be reconfigured casually.

Record, per run: the Attadipa full SHA, build profile, manifest and toolchain;
both nodes' full public keys, hardware revision, band and radio parameters, and
**the exact firmware SHA and build environment** where one can be read at all;
the starting `advert_loc_policy`, `telemetry_mode_*`, contact flags,
`manual_add_contacts`, `autoadd_config`, `gps` and `gps_interval` — and every
change and its restoration; raw redacted `0x80`, `0x8A`, `RESP_CODE_CONTACT` and
`RESP_CODE_END_OF_CONTACTS` frames with monotonic arrival stamps; every parse and
state transition; RSSI, SNR, packet loss and advert counts; battery voltage and
reset reason.

The experiments, in order, each of which decides something written above:

1. **Which push arrives.** With `manual_add_contacts = 0` on the companion, make
   the target advert. **Prediction: `0x80` and a bare key, never `0x8A`** (§2.1).
   If `0x8A` arrives, §2.1 is wrong and the whole resync design is unnecessary.
2. **The coordinate is there and is `e6`.** Set `advert_loc_policy` on the
   target, advert, `CMD_GET_CONTACT_BY_KEY`, and check bytes 136–143 against the
   target's own displayed coordinate.
3. **The `(0,0)` case.** A target that has never shared a location: the record's
   coordinate reads exactly zero, and the readout must show no distance.
4. **The trap, and it is the run that matters.** Advert with location, then set
   `advert_loc_policy = ADVERT_LOC_NONE` and advert again. **Prediction: bytes
   136–143 are unchanged and bytes 132–135 and 144–147 both advance.** If that
   is what happens, §2.3 is confirmed on hardware and §6's whole discipline is
   justified by measurement.
5. **The cadence.** Leave both nodes alone for an hour with nobody touching the
   target. **Prediction: no advert at all** (§5.3), the coordinate's arrival age
   grows past two minutes, and the readout says `NodePositionStale` and keeps
   saying it. On the V4.3 this prediction is not made — §10.2.
6. **Replay and clock rollback.** Reboot the target and re-advert before its RTC
   catches up; check whether the companion silences it.
7. **Table and eviction.** Fill the contact table if it is safe to; observe
   `0x90` and, with `AUTO_ADD_OVERWRITE_OLDEST` set, `0x8F`.
8. **Detach and reconnect** mid-read, and a companion reboot, to confirm the
   coordinate returns from persisted contacts and that nothing claims it is new.

Node-side fix age, C/N0, HDOP and TTFF are recorded **only** from the node's own
display or an independent interface. They are never inferred from a coordinate.

**Pass** for the research: experiment 4 behaves as predicted, and the frames
match §3.1's layout. **Fail:** any design that presents a retained coordinate as
current, that cannot say which node a coordinate belongs to, that needs a poller,
or that needs a change to node firmware.

## 13. What remains UNKNOWN

Filed as [OPEN_QUESTIONS](OPEN_QUESTIONS.md) **M28–M31**, plus everything in
§12.3, none of which is claimed here.

- **M28** — the V4.3's firmware, which is unreadable rather than merely
  unidentified (§10.2);
- **M29** — the real advert cadence of a companion in use, which §5.3 predicts
  from source is "when somebody presses the button" and which decides §9.2's
  first trigger;
- **M30** — whether `bootstrapRTCfromContacts()` plus the replay gate can
  permanently silence a rebooted target against our companion (§5.4);
- **M31** — how a second client of the same companion (the owner's phone)
  interacts with a target's contact record in practice, given §2.2. **This is
  also where `PositionSource::NodeGnss` overstates what it knows:** the label
  says a node's receiver, the bytes are a record another client can write, and
  ADR-0020 decision 5 publishes them under that label anyway rather than amend
  ADR-0011 inside this change. `PositionValidity` staying `NoFix` at every age
  is what keeps the readout honest meanwhile; a source value that says
  "relayed record, provenance unproven" is the fix, and it is not this ADR's.
