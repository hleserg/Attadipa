# What a contact snapshot says after the node lost its contact store

Research for [#654](https://github.com/hleserg/Attadipa/issues/654). It changes
no production code. It answers one question: after the connected MeshCore node
rebooted, or recovered its filesystem, what may this product claim about the
contact list it then enumerates?

**The answer:** only that one enumeration of the node's **in-RAM** contact
table ended. `peers_complete` and `MeshSnapshot::Consistent` say nothing about
whether that table is the node's durable store, whether the store was intact,
or whether it is the newest one the node ever held. At the fleet pin, an
interrupted save, a filesystem reformat that kept the node's identity, and a
legitimately empty table produce **byte-identical** walks as far as LittleFS's
own bookkeeping goes (what a cut does to the flash page beneath it is
**UNKNOWN**, M51/M52), and no frame, CLI answer, boot flag or statistic tells
them apart. A reformat that also regenerated the node's identity is visible as
a different node, but only while this watch holds a pin (§3), and a table of
garbage records shows only weakly (§4). Confidence: **high**, read from source; the field frequency is
**UNKNOWN** and the power-cut experiment is **NOT EXECUTED — HARDWARE
REQUIRED** (§8).

---

## 0. Provenance

| Source | Revision | Licence | How it was read |
|---|---|---|---|
| MeshCore, `meshcore-dev/MeshCore` | `companion-v1.17.1@d92964352441e53b93e8667b802e04f6e072b39e` (the fleet pin) | MIT (`license.txt`) | cloned, read at that commit on 2026-10-01; no code taken |
| nRF52 Arduino core, `meshcore-dev/Adafruit_nRF52_Arduino` | `d541301665b40959682252911e57b11df3ee651a` (pinned by MeshCore `platformio.ini:88`) | MIT / BSD (per file) | files fetched at that commit, 2026-10-01 |
| CustomLFS, `oltaco/CustomLFS` | tag `0.2.3`, `b3928ea2d0f46c2533e901c43f471a081c503a3c` | MIT | cloned, read 2026-10-01 |
| MeshCore [#2964](https://github.com/meshcore-dev/MeshCore/pull/2964) | head `06423621f9911285b900520da25e755f3bded092` | MIT | open draft, read 2026-10-01 |
| MeshCore [#3499](https://github.com/meshcore-dev/MeshCore/pull/3499) | head `f8ff81ec4a7f7968dc13643946d4554410e7b6df` | MIT | open, `DataStore.cpp` read in full 2026-10-01 |
| Atta-dipa | `main@04dca51a` | this repository | source read; one host test run (§7) |

Every MeshCore citation below was read **at the fleet pin itself**, so no
byte-identity bridge to another revision is needed. Unprefixed `MyMesh.cpp`,
`DataStore.cpp`, `DataStore.h` and `main.cpp` are under
`examples/companion_radio/` of that tree; unprefixed `lfs.c` is the core's
`libraries/Adafruit_LittleFS/src/littlefs/lfs.c`, and `platformio.ini` is
MeshCore's top-level file.

## 1. Where the contacts live on a T114

The Heltec T114 companion environment is
`[env:Heltec_t114_companion_radio_ble]`, the build this project's bench node
runs (`docs/research/VERIFIED_FACTS.md:724` — "[env:Heltec_t114_companion_radio_ble]").
Its board is `heltec_t114` (`variants/heltec_t114/platformio.ini:6`), and the
companion environment overrides the base linker script with
`boards/nrf52840_s140_v6_extrafs.ld` (`variants/heltec_t114/platformio.ini:212`),
so the nRF52840 branches of the core apply. `platformio.ini:93` sets `-D EXTRAFS=1` for the nRF52 family.

| Store | Holds | Geometry | Source |
|---|---|---|---|
| `InternalFS` | the node identity `/_main` | `0xED000`, 7 × 4 KiB = 28 KiB, **128-byte** LittleFS blocks | core `libraries/InternalFileSytem/src/InternalFileSystem.cpp:29`, `:34`, `:35`; MeshCore `DataStore.cpp:10-12`, `:184-185` |
| `ExtraFS` (`CustomLFS`) | `/contacts3`, channels | `0xD4000`, `0x19000` = 100 KiB, 128-byte blocks | MeshCore `examples/companion_radio/main.cpp:76-77` — `CustomLFS ExtraFS(0xD4000, 0x19000, 128);` |

`0xD4000 + 0x19000 = 0xED000`: ExtraFS ends exactly where InternalFS begins,
which is the consistency check on the two addresses. Contacts go to ExtraFS
whenever it exists (`DataStore.h:54`, `_getContactsChannelsFS()`).

Both stores are LittleFS **v1.7** (core
`libraries/Adafruit_LittleFS/src/littlefs/lfs.h:24` — `#define LFS_VERSION 0x00010007`).
The flash beneath them is erased a **4 KiB page** at a time through a
read-modify-write cache (core `libraries/InternalFileSytem/src/flash/flash_cache.h:31`
— `#define FLASH_CACHE_SIZE 4096`, and `flash_cache.c:89-90`, erase then
program the whole page). One page holds 32 LittleFS blocks, so an interrupted
page rewrite can reach up to 32 blocks that LittleFS believes are independent:
**ESTIMATED** from the two constants; the real blast radius is **UNKNOWN**
(M52). That is #2964's diagnosis, and it is now traced to the T114 build rather
than borrowed from the Wio or T-Echo.

v1.7 checksums **directory metadata only** — `lfs.c:470` in `lfs_dir_fetch`
and `lfs.c:603` in `lfs_dir_commit` are the only callers of `lfs_bd_crc`. File
data is read back unchecked.

## 2. The trace

**Save.** Every contact change arms a five-second lazy write
(`MyMesh.cpp:107` — `LAZY_CONTACTS_WRITE_DELAY 5000`; setters at
`MyMesh.cpp` lines 389, 411, 541, 1276, 1288, 1296 and 1307; the write at
`MyMesh.cpp:2239-2240`; `CMD_REBOOT` flushes it at `MyMesh.cpp:1468-1469`).
`saveContacts` opens the file with `openWrite`, which is
`fs->remove(filename); return fs->open(filename, FILE_O_WRITE);`
(`DataStore.cpp:36-37`, called at `DataStore.cpp:291`), writes 152-byte
records, and on a short write stops with `if (!success) break; // write failed`
(`DataStore.cpp:315`). Nothing reports the failure.

In LittleFS v1.7 the `remove` is committed to the directory at once
(`lfs.c:1895`), and creating the file commits an entry of **size 0**
(`lfs.c:1319-1325`). The size is written back only when the file is synced or
closed. So a power cut anywhere between the `remove` and the `close` leaves
`/contacts3` either absent or zero bytes long — the old table is already gone
and the new one is not yet visible. Read from source; whether flash damage from
§1 adds anything to that is M52.

**Boot.** `InternalFS.begin()` and `ExtraFS.begin()` are called with their
results ignored (`examples/companion_radio/main.cpp:142`, `examples/companion_radio/main.cpp:152`). A mount that fails is
answered inside `begin()` by erasing the whole region and formatting it
(core `InternalFileSystem.cpp:133-145`; CustomLFS `src/CustomLFS.cpp:218-228`).
If the identity cannot be loaded afterwards a new one is generated and saved
(`MyMesh.cpp:904-911`). Then `resetContacts(); _store->loadContacts(this);
bootstrapRTCfromContacts();` (`MyMesh.cpp:970-972`).

**Load.** `loadContacts` (`DataStore.cpp:257-288`) does nothing if the file
does not open (`DataStore.cpp:261`), reads 152-byte records with no header,
checksum or generation, and stops at the first short read with
`if (!success) break; // EOF` (`DataStore.cpp:281`). A partial last record is
dropped silently; a full record of garbage is loaded as a contact.

**Clock.** `bootstrapRTCfromContacts` (`src/helpers/BaseChatMesh.cpp:58-68`)
sets the clock to the newest contact `lastmod` plus one. With no contacts it
does nothing, and the `lastmod` values it reads were stamped on a clock that
was itself bootstrapped from the store, so `lastmod` cannot audit the store.

**Wire.** `CMD_GET_CONTACTS` answers `RESP_CODE_CONTACTS_START` carrying
`getNumContacts()` — "total, NOT filtered count" (`MyMesh.cpp:1202-1204`) —
zeroes `_most_recent_lastmod` (`MyMesh.cpp:1210`), sends one
`RESP_CODE_CONTACT` per in-RAM entry newer than the optional `since`
(`MyMesh.cpp:1195-1196`, `:2211`), and ends with
`RESP_CODE_END_OF_CONTACTS` carrying the newest `lastmod` it sent
(`MyMesh.cpp:2216-2221`). Every one of those reads the RAM table that
`loadContacts` filled.

**Atta-dipa.** START records the total and clears completeness
(`link/src/meshcore_companion.cpp:1435` — "        status_.peers_reported = reported;",
`link/src/meshcore_companion.cpp:1438` — "        status_.peers_complete = false;").
END, or three quiet seconds standing in for a lost END
(`link/src/meshcore_companion.cpp:529` — "    if (contacts_open_ && !wrong_node_ &&"),
sets it again
(`link/src/meshcore_companion.cpp:1546` — "        status_.peers_complete = true;")
and calls `settle_snapshot`
(`link/src/meshcore_companion.cpp:1547` — "        settle_snapshot(now);").
That function reads exactly two members, `snapshot_dirty_` and `retries_left_`,
and publishes `Consistent` when the first is clear
(`link/src/meshcore_companion.cpp:813` — "        status_.snapshot = core::MeshSnapshot::Consistent;").
Neither the START count nor any frame's content is an input. The one consumer
that compares counts, the mesh screen's `retained/reported` pair, waits only on
completeness (`apps/src/mesh.cpp:284` — "        if (status.peers_complete && retained < reported) {").

## 3. The seven states

| State (the issue's list) | Can it happen at `d929643` on a T114? | What the watch receives | Distinguishable? |
|---|---|---|---|
| Legitimately empty table | yes | `START 0`, `END lastmod=0` | — (the reference) |
| Zero-length truncation | **yes**: a cut between `remove` and `close` (§2) | `START 0`, `END lastmod=0` | **no** — identical bytes |
| Partial final record | not from an interrupted save in v1.7, whose dir entry stays at size 0 until close; possible only if a block is damaged after a successful save (**UNKNOWN**, M52) | `START n`, n CONTACTs, END | **no** — a shorter table looks like fewer contacts |
| Same-length, content-corrupt | possible: file data carries no CRC (§1); rate **UNKNOWN** | CONTACT frames with garbage keys and names | **weakly** — a record whose byte 33 is not the chat type is not retained (`link/src/meshcore_companion.cpp:686` — "    if (size < 148 || data[33] != kAdvertTypeChat) {"), so the face can show `retained < reported` (§4); repeaters, rooms and any table above 16 contacts show the same |
| Older backup restored | **cannot occur**: the pin keeps no backup. It becomes possible only with #3499 (§5) | — | — |
| Reformat, identity kept | yes: ExtraFS fails to mount and is erased, InternalFS mounts | `START 0`, `END lastmod=0` | **no** — same node, empty table |
| Reformat, identity regenerated | yes: InternalFS fails to mount and the identity is lost | a different public key in `SELF_INFO` | **yes, while this watch holds a pin** — the check fires (`link/src/meshcore_companion.cpp:1358` — "        if (pinned_set_ && !(status_.node_id == pinned_)) {") and the walk is refused (`link/src/meshcore_companion.cpp:1366` — "            wrong_node_ = true;"). With the pin unreadable (`firmware/main/meshcore_ble.cpp:2578` — "    case PinRead::Unreadable:"), unfinished (`firmware/main/meshcore_ble.cpp:2586` — "    case PinRead::Unfinished:") or never adopted (`firmware/main/meshcore_node_pin.h:211` — "            return PinOutcome::AdoptFailed;") the watch attaches to the reformatted node as its own, and this row is as invisible as the others |

The host replay the issue asks for holds by construction:
`MeshCoreCompanion` is a function of the frame bytes it is fed, and the three
indistinguishable rows above feed it the same bytes.

## 4. Is there a signal? The survey

| Candidate | What it carries at the pin | Why it does not separate the states |
|---|---|---|
| START count / END `lastmod` | RAM table size; newest `lastmod` sent | Both describe the table after the loss. `lastmod` falling across a reconnect also happens when the newest contact is deleted on purpose, and the clock that stamps it is bootstrapped from the store (§2) |
| `RESP_CODE_BATT_AND_STORAGE` | used / total KB (`MyMesh.cpp:1476-1482`) | `_getLfsUsedBlockCount` answers 0 on a traverse error (`DataStore.cpp:92-97`), so a broken store and an empty one read alike; an empty healthy store is small too |
| `DEVICE_INFO`, `SELF_INFO` | firmware, radio, identity (`MyMesh.cpp:1027-1041`, `:1052-1083`) | no storage or boot field |
| Error events | radio only — `ERR_EVENT_FULL`, `CAD_TIMEOUT`, `STARTRX_TIMEOUT` (`src/Dispatcher.h:110-112`) | nothing for the filesystem |
| Reset reason | captured into `g_nrf52_reset_reason` (`src/helpers/NRF52Board.cpp:46`) | never put on the wire |
| CLI rescue `ls` | file listing (`MyMesh.cpp:2074-2097`) | serial console only, not the BLE companion link |
| Mount failure | handled and swallowed inside `begin()` (§2) | never reported anywhere |
| `retained / reported` on the face | chat contacts this client kept against START's total (`apps/src/mesh.cpp:284` — "        if (status.peers_complete && retained < reported) {") | the empty-table rows all give `0 == 0`. It fires on a garbage table only weakly: repeaters and rooms are dropped the same way, and `kRetainedPeers` (`link/include/attadipa/link/meshcore_companion.h:251` — "    static constexpr std::size_t kRetainedPeers = 16;") makes it fire on every table above sixteen |

**No signal separates the empty-table rows, and none is invented here.** The one wire hint — END
`lastmod` going backwards between two sessions — is ambiguous by design and
would need state this product does not keep across a session: `reset_session`
clears the walk (`link/src/meshcore_companion.cpp:225` — "    status_.peers_complete = false;",
`link/src/meshcore_companion.cpp:246` — "    status_.snapshot = core::MeshSnapshot::None;").

One more consequence, read from source: a direct message is decrypted only by
trying contacts whose hash matches its source (`src/Mesh.cpp:147-160`), so after
a contact loss the node cannot read messages from former contacts until they
are re-added — by advert, unless `manual_add_contacts` is set
(`MyMesh.cpp:299-305`). The watch sees silence, not an error.

## 5. The upstream proposals, modelled

**#3499** stages `/contacts3.tmp`, keeps `/contacts3.bak` and loads the backup
when the primary is not valid. Its validity test is `file.size() %
CONTACT_RECORD_SIZE == 0` (its `DataStore.cpp:62`), so:

- a **zero-length** primary is valid and is loaded as an empty table — the
  backup is never consulted for the very case the issue opened with;
- a **same-length corrupt** file is valid;
- a cut **between** the two renames (its `DataStore.cpp:374` primary → backup,
  then its `DataStore.cpp:385` temp → primary) leaves no primary, and the next boot loads the
  backup: an **older generation**, reported only through `MESH_DEBUG_PRINTLN`.
  This is the "older backup restored" state, and #3499 is what creates it.

It is compile-only on a Wio Tracker L1. **MONITOR; do not copy.**

**#1447** (open, head `ffebb64b`, read 2026-10-01) is the same mechanism without
that hole: it falls back to the backup when the primary is missing **or empty**
([its `DataStore.cpp:271`](https://github.com/meshcore-dev/MeshCore/blob/ffebb64b31fa3b951c82886caa2169e53c75af9e/examples/companion_radio/DataStore.cpp#L271) — "if (!file || file.size() == 0) {")
and deletes the backup after a successful promotion
([its `DataStore.cpp:356`](https://github.com/meshcore-dev/MeshCore/blob/ffebb64b31fa3b951c82886caa2169e53c75af9e/examples/companion_radio/DataStore.cpp#L356)).
A cut between its two renames still loads the previous generation. This
repository already decided `ADOPT` for its pattern
(`docs/upstream/meshcore-1.17-review.md:445` — "**Status: `ADOPT` the pattern from #1447, and apply it more widely than upstream"),
and that stands; the `REJECT` here is of #3499's validity test.

**#2964** moves to LittleFS v2 with 4 KiB blocks and chunked contact files with
checksums, which addresses the page-erase problem at its root, and warns that a
downgrade "will wipe your filesystems!". It is a draft, large and not mergeable.
**MONITOR**; flashing it to a fleet node is not a research result.

Neither adds a wire field, so neither changes §4: even with #2964 merged, a
store that recovered by formatting would still enumerate as an ordinary empty
table.

## 6. What the public states mean, narrowly

- **`peers_complete`** — one enumeration of the node's in-RAM table ended in
  this session, by `RESP_CODE_END_OF_CONTACTS` or by the quiet sweep that
  stands in for a lost one. It does not even promise the node sent END, and it
  carries nothing about the durable store.
- **`MeshSnapshot::Consistent`** — that enumeration ended and no invalidating
  push this client recognises arrived inside it. It is reachable with
  `peers_reported == 0`, and it is reachable after any of the indistinguishable
  rows of §3. It is a statement about the walk, never about the store's
  integrity, currency or provenance.
- **"Contact not found"** — `CMD_GET_CONTACT_BY_KEY` answers
  `ERR_CODE_NOT_FOUND` from `lookupContactByPubKey` over the RAM table *now*
  (`MyMesh.cpp:1324-1331`), and this client publishes `Refused` for it
  (`link/src/meshcore_companion.cpp:381` — "    // -- which is also what the same fetch answered `ERR_CODE_NOT_FOUND`").
  Absence from a fetch or a walk means **absent from the node's table at that
  moment**. It is not evidence that anybody deleted the contact.

**ADR-0022 needs no amendment.** Its decision 2 already defines the state by
the stream — `docs/adr/0022-contact-snapshot-consistency.md:85` — "A snapshot is *consistent* when the" —
and decision 5 keeps readiness on the stream as well. What this report adds is
the scope those words already had: a stream over the RAM table, which a
recovered store fills like any other.

**ADR-0021 needs no amendment either.** A target is named by a full 32-byte key,
and a message whose sender resolves to no contact carries no target
(`docs/adr/0021-remote-target-from-a-message.md:79` — "**2. A target is named by the full 32-byte public key of the contact the").
A contact lost with the store therefore resolves to no target by its key. A
sender is matched on a six-byte prefix
(`link/src/meshcore_companion.cpp:729` — "        if (std::memcmp(peers_[i].id.public_key.data(), prefix, 6) == 0) {"),
so a collision with a remaining or garbage contact is not excluded, and
ADR-0021 already declines to call that risk zero. The selection itself is
not implemented yet; when it is, the rule this report adds for it is the third
bullet above: a selected key that a fresh walk no longer contains has become
**unresolved**, not deleted.

## 7. Evidence run here

**Run, 2026-10-01:** the `meshcore_companion` host test
(`tests/test_meshcore_companion.cpp`) on `main@04dca51a`:
`100% tests passed, 0 tests failed out of 1`. It pins the client's behaviour
over frame bytes; §3 rests on the client being deterministic over those bytes
and on the source trace of §2. No test is added — this issue is research.

**Not run:**

- An upstream host harness for `DataStore` with injected short writes, zero
  files and failed renames. MeshCore has no native test target at this
  revision; §2 and §5 are source traces, not executions.
- **HIL power-cut series** on an owner-authorised expendable T114: record the
  board revision, firmware SHA and environment, export the contacts, force a
  real save, cut power at controlled offsets across the save window, and after
  every boot capture the serial log, identity, raw START/CONTACT/END frames and
  the watch's snapshot state. Classify each trial as *no boot*, *identity
  changed*, *same identity + empty*, *partial*, *older*; publish numerator and
  denominator, including failed trials. Compare the pin with #3499/#2964 only
  in a bench image. **NOT EXECUTED — HARDWARE REQUIRED** (M51).

## 8. Open

- [OPEN_QUESTIONS](OPEN_QUESTIONS.md) **M51** — the HIL series above.
- [OPEN_QUESTIONS](OPEN_QUESTIONS.md) **M52** — what LittleFS v1.7 over a 4 KiB
  read-modify-write page actually leaves after a cut, and whether a partial or
  garbage table, rather than an empty one, is a real outcome on a T114.

No production change follows from this report: every product state already
means only what §6 says, and nothing on the wire would let the product say
more. If the owner ever wants the watch to *warn* that a node's table may have
been lost, that needs a signal the node does not send, and the place to ask
for one is upstream.

## Rejected

- END as proof of durable integrity — it terminates one walk.
- An empty list after a reboot as evidence of intentional deletion — the wire
  cannot tell it from storage loss (§3).
- Copying #3499 — unmerged, compile-only, accepts a zero-length file, and
  introduces the older-generation state through a two-rename promotion with no
  proven power-loss contract.
- Flashing #2964 as the result — a draft migration that wipes on downgrade.
- A client heuristic on `lastmod` or counts now — ambiguous (§4) and needs
  persistent state the contract does not have.
