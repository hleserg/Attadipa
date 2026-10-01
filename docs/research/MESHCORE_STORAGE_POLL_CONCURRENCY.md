# Does the battery poll race the flash on a T114?

Research for [#700](https://github.com/hleserg/Attadipa/issues/700). It changes
no production code, polling cadence, wire protocol, UI or node firmware. It
answers one question: can this product's periodic battery request
(`CMD_GET_BATT_AND_STORAGE`, command 20) reach unsafe concurrent access to the
nRF52 flash cache on the fleet's Heltec T114 build, and what may the product
claim about it?

**The answer:** the request reaches an unlocked `lfs_traverse()` over the
contact filesystem on every poll, exactly as the issue says, but **no write
path from the poll was found in source**. The traversal and the flash-cache
read it ends in are read-only; the contact filesystem is touched by one task
only, the one that runs the command; and the one unsynchronised write/write
race found — a contact save against a bond save on the shared 4 KiB page cache
— exists with the poll switched off. Why removing the poll stopped the
upstream failure is therefore **unexplained**: the upstream 5–10 versus 100
connection result is a correlation whose mechanism is **UNKNOWN**, and the
patch that produced it changes three things at once (§5), so it cannot isolate
the traversal. The poll is **not shown harmless** either; only a hardware run
discriminates, and that run is **NOT EXECUTED — HARDWARE REQUIRED** (§8).
Confidence: **high** for the trace (§1–§3), read at the fleet pin; **INFERRED**
for every interleaving claim (§4), which no test exercises.

---

## 0. Provenance

| Source | Revision | Licence | How it was read |
|---|---|---|---|
| MeshCore, `meshcore-dev/MeshCore` | `companion-v1.17.1@d92964352441e53b93e8667b802e04f6e072b39e` (the fleet pin) | MIT (`license.txt`) | cloned, read at that commit on 2026-10-01; no code taken |
| nRF52 Arduino core, `meshcore-dev/Adafruit_nRF52_Arduino` | `d541301665b40959682252911e57b11df3ee651a` (pinned by MeshCore `platformio.ini:88`) | LGPL-2.1 at the root; the `InternalFileSytem` and `Adafruit_LittleFS` files read carry MIT headers; `Bluefruit52Lib` carries its own MIT `LICENSE` | cloned, read at that commit 2026-10-01; no code taken |
| LittleFS as vendored in that core | v1.7 (`libraries/Adafruit_LittleFS/src/littlefs/lfs.c`) | BSD-3-Clause | read in the clone above |
| CustomLFS, `oltaco/CustomLFS` | tag `0.2.3`, `b3928ea2d0f46c2533e901c43f471a081c503a3c` | MIT (file headers) | cloned, read 2026-10-01 |
| MeshCore [#3503](https://github.com/meshcore-dev/MeshCore/pull/3503) | head `a845319d5890f08b9c2def5dfe8c5296c8d25c0b` | MIT | open, unmerged; diff and description read 2026-10-01 |
| Atta-dipa | `main@ee0c9a63` | this repository | source read; existing host test cited (§2) |

Every MeshCore citation below was read **at the fleet pin itself**. Paths
without a prefix are under `examples/companion_radio/` of that tree; core paths
are under the nRF52 core clone at `d541301`.

## 1. Where the traversal runs on a T114

The bench node's build is `[env:Heltec_t114_companion_radio_ble]`
(`docs/research/VERIFIED_FACTS.md:724` — "`variants/heltec_t114/platformio.ini`, `[env:Heltec_t114_companion_radio_ble]`").
That environment **overrides** the board linker script:
[`variants/heltec_t114/platformio.ini:212`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/variants/heltec_t114/platformio.ini#L212)
— "board_build.ldscript = boards/nrf52840_s140_v6_extrafs.ld", which ends
application flash at `0xD4000`
([`boards/nrf52840_s140_v6_extrafs.ld:8`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/boards/nrf52840_s140_v6_extrafs.ld#L8)
— "FLASH (rx) : ORIGIN = 0x26000, LENGTH = 0xD4000 - 0x26000"). The nRF52
family builds with `-D EXTRAFS=1`
([`platformio.ini:93`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/platformio.ini#L93))
and `-D LFS_NO_ASSERT=1` (`platformio.ini:92`).

| Filesystem | Region | Geometry | Holds | Lock |
|---|---|---|---|---|
| `ExtraFS` (CustomLFS 0.2.3) | `0xD4000`–`0xED000` | 800 × 128 B blocks | contacts, channels | its own `Adafruit_LittleFS` mutex |
| `InternalFS` (core) | `0xED000`–`0xF4000` | 7 × 4 KiB pages, 128 B blocks | prefs, identity, **Bluetooth bonds** | its own `Adafruit_LittleFS` mutex |

- `ExtraFS` is
  [`main.cpp:76`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/main.cpp#L76)
  — "CustomLFS ExtraFS(0xD4000, 0x19000, 128);", passed beside `InternalFS` at
  [`main.cpp:77`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/main.cpp#L77), and selected for contacts and channels by
  [`DataStore.h:54`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/DataStore.h#L54)
  — "FILESYSTEM* _getContactsChannelsFS() const { if (_fsExtra) return _fsExtra; return _fs;};".
- `InternalFS` is
  [`InternalFileSystem.cpp:29`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/InternalFileSytem/src/InternalFileSystem.cpp#L29)
  — "#define LFS_FLASH_ADDR        0xED000", seven pages
  (`InternalFileSystem.cpp:34`), 128-byte blocks (`InternalFileSystem.cpp:35`).
- Each filesystem object creates **its own** mutex,
  [`Adafruit_LittleFS.cpp:49`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/Adafruit_LittleFS/src/Adafruit_LittleFS.cpp#L49)
  — "_mutex = xSemaphoreCreateMutexStatic(&this->_MutexStorageSpace);", and
  `CustomLFS` is a subclass of `Adafruit_LittleFS`. Both route every block
  operation into **one** static 4 KiB page cache,
  [`flash_nrf5x.c:57`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/InternalFileSytem/src/flash/flash_nrf5x.c#L57)
  — "static uint8_t _cache_buffer[FLASH_CACHE_SIZE] __attribute__((aligned(4)));".
  No path of `flash_cache.c` takes a lock.

So the issue's premise holds: two filesystems, two different mutexes, one
unprotected cache. What the traversal *does* with that cache is §3.

## 2. The trace, end to end

**Atta-dipa side.** The request is built once the handshake has finished and
nothing else is queued:

- `link/src/meshcore_companion.cpp:647` — "self_info_seen_ && device_info_seen_ && contacts_complete_ &&"
  is part of the enqueue gate, and
  `link/src/meshcore_companion.cpp:653` — "const std::uint8_t request[] = {kGetBatteryAndStorage};"
  is the frame.
- It is due when it has never been sent this session, or when 60 s have passed
  since the **start** of the last one:
  `link/src/meshcore_companion.cpp:349` — "battery_due_ = !battery_polled_ ||"
  and `link/src/meshcore_companion.cpp:350` — "core::elapsed(battery_started_, now) >= kBatteryPollPeriod;",
  with `link/src/meshcore_companion.cpp:72` — "constexpr core::Millis kBatteryPollPeriod{60000};".
- Sending stamps the start (`link/src/meshcore_companion.cpp:676` — "battery_started_ = poll_now_;")
  and a five-second budget fails the request without retrying early
  (`link/src/meshcore_companion.cpp:73` — "constexpr core::Millis kBatteryReplyBudget{5000};").
- A new session clears the flag (`link/src/meshcore_companion.cpp:180` — "battery_polled_ = false;"),
  so **every completed handshake makes one request due**, after the contact
  walk. It is sent on the first tick that may send, so a link that drops first
  sends none.
- The host test pins the cadence: `tests/test_meshcore_companion.cpp:2026` —
  "void test_attached_node_battery_uses_the_live_queue_and_public_status()"
  polls at 10 ms, then `tests/test_meshcore_companion.cpp:2060` — "client.tick(at(60010));"
  and every 60 s after.

The bound this gives, **INFERRED from source and the host test**: at most one
request in flight; at most one per 60 s while connected; plus one per completed
handshake. A reconnect loop that never completes the contact walk sends none; a
loop that completes it sends one per connection.

**Node side, at the fleet pin.**

1. [`MyMesh.cpp:1472`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/MyMesh.cpp#L1472)
   — "} else if (cmd_frame[0] == CMD_GET_BATT_AND_STORAGE) {", reached from
   `handleCmdFrame` inside `checkSerialInterface`, which `MyMesh::loop` runs
   on the Arduino **loop task**.
2. `MyMesh.cpp:1476` reads the battery, then
   [`MyMesh.cpp:1477`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/MyMesh.cpp#L1477)
   — "uint32_t used = _store->getStorageUsedKb();".
3. [`DataStore.cpp:113`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/DataStore.cpp#L113)
   — "int usedBlockCount = _getLfsUsedBlockCount(_getContactsChannelsFS());", i.e. on `ExtraFS`.
4. [`DataStore.cpp:94`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/DataStore.cpp#L94)
   — "int err = lfs_traverse(fs->_getFS(), _countLfsBlock, &size);" — on the
   raw `lfs_t`, without `_lockFS()`.
5. LittleFS reads every metadata pair and every file's CTZ index through
   `CustomLFS::_flash_read`, which is
   [`CustomLFS.cpp:41`](https://github.com/oltaco/CustomLFS/blob/b3928ea2d0f46c2533e901c43f471a081c503a3c/src/CustomLFS.cpp#L41)
   — "VERIFY(flash_nrf5x_read(buffer, addr, size) > 0, -1);", which is
   `flash_cache_read` on the shared cache.

Any traversal error is swallowed: `DataStore.cpp:95`–`:97` log under
`MESH_DEBUG_PRINTLN` and return 0, and the T114 companion builds without
`MESH_DEBUG` (`variants/heltec_t114/platformio.ini:224` — ";  -D MESH_DEBUG=1"), so a corrupt traversal is invisible on the wire except as
"0 KiB used" — which Atta-dipa never reads: the reply layout is
`link/src/meshcore_companion.cpp:1614` — "// Pinned Companion producer: [12][u16 mV][u32 storage][u32 storage].",
and the parse takes only bytes 1–2 (`link/src/meshcore_companion.cpp:1636` — "static_cast<unsigned>(data[1]) |").

## 3. What the traversal can and cannot do

**Read-only.** `lfs_traverse` in v1.7 calls the callback and `lfs_bd_read`;
it allocates and programs nothing. Its last arm walks every open file's block
list, and there the file's own cache is passed only as the `const` program
cache, beside the filesystem's read cache
([`lfs.c:2307`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/Adafruit_LittleFS/src/littlefs/lfs.c#L2307)
— "int err = lfs_ctz_traverse(lfs, &lfs->rcache, &f->cache,"), so a pending
write in an open file is read through, never overwritten. `flash_cache_read` overlays cached bytes
when the range overlaps the cached page and otherwise reads flash; it never
changes `cache_addr` or the buffer
([`flash_cache.c:98`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/InternalFileSytem/src/flash/flash_cache.c#L98)
— "int flash_cache_read (flash_cache_t* fc, void* dst, uint32_t addr, uint32_t count)").
The command handler writes nothing either. **No write path from the poll was
found in source.**

**Unlocked, but alone on `ExtraFS`.** Every `ExtraFS` operation found — the
command, contact and channel loads, the lazy contact save at
[`MyMesh.cpp:2239`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/MyMesh.cpp#L2239)
— "if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {" — runs on the loop task. A missing lock on a structure only one task touches
races nothing. *Finding a second `ExtraFS` caller would change this answer.*

**The off-by-one is real and narrow.**
[`DataStore.cpp:83`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/DataStore.cpp#L83)
— "if (block > _ContactsChannelsTotalBlocks) {" accepts block 800, one past the
end. Only a pointer that is **already** corrupt can name block 800, and the
consequence is a read at `0xD4000 + 800 × 128 = 0xED000`, the first block of
`InternalFS`. It is a read of the wrong filesystem, not a write; LittleFS's own
bound check is compiled out by `LFS_NO_ASSERT`.

**Two read-side effects remain, both transient.** A traversal can read through
the cache while another task holds an `InternalFS` page in it; `ExtraFS`
addresses never overlap that page, so they miss it and read flash. The single
exception is the block-800 read above, which can see an `InternalFS` page
mid-update. Either way the worst outcome is a wrong count or an error that the
handler turns into 0 — nothing is stored.

**What source cannot rule out.** v1.7's `lfs_traverse` follows each metadata
pair's `tail` until a null pair and bounds nothing, so a corrupt but
checksum-valid tail cycle would never return, and the loop task would stop
answering while the BLE link stays up. The T114 build defines no
`HAS_EXTERNAL_WATCHDOG`, and no source read here arms the nRF52's internal one
(no `WDT` start in MeshCore's `src/` or the core at `d541301`), so a hang is
not turned into a reboot. That is a way for the poll to **expose** earlier corruption, not to
cause it; it is listed as a hypothesis in §5, not a finding, and the same
traversal runs from `lfs_alloc` on any write that needs a block
(`lfs.c:325` of the vendored v1.7 — "int err = lfs_traverse(lfs, lfs_alloc_lookahead, lfs);").

## 4. Every task that touches the flash, and the race that does exist

| Task (priority) | Filesystem | Operations | Trigger |
|---|---|---|---|
| loop (1) | `ExtraFS` | traverse (poll), read, **save** contacts and channels | command 20, boot, `dirty_contacts_expiry` |
| loop (1) | `InternalFS` | prefs, identity, other `DataStore` files | commands and boot |
| Callback (2) | `InternalFS` | **bond key save** (remove + create + write) | pairing |
| Callback (2) | `InternalFS` | **CCCD save**, written only if the stored attributes differ | a secured CCCD write |
| BLE (3) | `InternalFS` | bond and CCCD **load** | connection |

Priorities are the core's
(`TASK_PRIO_LOW = 1` loop, `TASK_PRIO_NORMAL = 2` Callback,
`TASK_PRIO_HIGH = 3` BLE in `cores/nRF5/rtos.h:58`–`:60`). The CCCD save is
queued, not run inline:
[`BLEGatt.cpp:143`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/Bluefruit52Lib/src/BLEGatt.cpp#L143)
— "if ( conn->secured() && (evt_id == BLE_GATTS_EVT_WRITE) && (req_handle == chr->handles().cccd_handle) )"
and
[`bonding.cpp:262`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/Bluefruit52Lib/src/utility/bonding.cpp#L262)
— "return ada_callback(id_addr, sizeof(ble_gap_addr_t), bond_save_cccd_dfr, role, conn_hdl, id_addr);",
and it skips the write when nothing changed
([`bonding.cpp:238`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/Bluefruit52Lib/src/utility/bonding.cpp#L238)
— "if ( 0 == memcmp(sys_attr, old_data, len) )"). Atta-dipa writes the CCCD on
every connection, after encryption
(`firmware/main/meshcore_ble.cpp:890` — "const int rc = ble_gattc_write_flat(conn, session.cccd_handle, enable,"),
so each connection **queues** a CCCD save; whether any byte is programmed
depends on whether the stored attributes differ, which is **UNKNOWN** for the
fleet node.

**The write/write race (INFERRED, untested).** `flash_cache_write` loads a
page, then copies into it
([`flash_cache.c:61`](https://github.com/meshcore-dev/Adafruit_nRF52_Arduino/blob/d541301665b40959682252911e57b11df3ee651a/libraries/InternalFileSytem/src/flash/flash_cache.c#L61)
— "flash_cache_flush(fc);", then `flash_cache.c:65` reads the new page into the buffer and `flash_cache.c:68` — "memcpy(fc->cache_buf + offset, src8, wr_bytes);"),
and the flush erases and programs whatever the buffer holds at that moment
(`flash_cache.c:89`–`:90`). With no lock, a loop-task contact save
preempted by a Callback-task bond or CCCD write can have its half-copied page
flushed, or can resume and copy contact bytes into a buffer that now holds an
`InternalFS` page while that page's flush is blocked on the SoftDevice — which
would program contact bytes into the bond filesystem. That outcome would fit
"BLE unusable until reflash" better than anything the traversal can do. **It
needs a contact save and a bond-side write at the same moment, and it exists
with the poll switched off.**

## 5. MeshCore #3503, reviewed

What it changes at `a845319`: it locks `lfs_traverse` in `DataStore`; wraps
CustomLFS's read, prog, erase and sync with `InternalFS`'s lock
(`LockedLFS.h:16`–`:42` at that head); and fixes the bound to `>=`.

| Question | Answer |
|---|---|
| Lock order | `ExtraFS` → `InternalFS`, taken in that order wherever both are held. No reverse order was found, so no lock-order deadlock in source. **INFERRED** |
| Recursion | The wrapped callbacks run inside LittleFS while the caller holds its own filesystem mutex; the added lock is the *other* filesystem's, a plain mutex, never re-entered on the same path read. **INFERRED** |
| Error exits | The wrappers release on every return path of the four block callbacks read |
| Coverage gaps | CustomLFS's mount-time and format-time erases (`CustomLFS.cpp:224`, `:252`) call the flash layer directly and stay unlocked; `setFlashRegion` (`:176`) re-runs `_configure_lfs()` and drops the wrapper |
| Does it isolate the traversal? | **No.** It serialises the write/write race of §4 as well, so the 5–10 versus 100 result cannot say which change mattered |

Status: **OPEN_PR**, non-draft, unmerged. Verdict: **MONITOR**; take no code.

**What the upstream result supports.** The author's hardware report (RAK
WisMesh 1W Booster, a custom client) is a correlation — removing the battery
request stopped the recurrence; the patch then ran 100 connections clean. Three
explanations fit it, none established:

1. the §4 write/write race, independent of the poll, made rarer by fewer
   connections or by lock-induced timing;
2. the traversal **exposing** corruption that already existed (§3, last paragraph);
3. a client- or timing-level effect of one more exchange inside the handshake
   window.

Mechanism: **UNKNOWN**. Only the HIL matrix of §8 can tell them apart.

## 6. What the product can observe

| Failure class | What Atta-dipa observes today | What tells it apart (needs node serial, flash image or HIL) |
|---|---|---|
| Ordinary BLE negotiation failure | `MeshCore disconnected: %d` with the reason (`firmware/main/meshcore_ble.cpp:1223` — "MeshCore disconnected: %d"); next connection succeeds | nothing more needed |
| Controller or host failure, flash intact | repeated disconnects or timeouts; same identity once it answers | node serial log, reset reason — **UNKNOWN** until HIL |
| Filesystem corruption, identity intact | storage figures are discarded; a short or empty contact walk is indistinguishable from a real one (#654) | traversal error under `MESH_DEBUG`, a flash image — **UNKNOWN** |
| Corrupted bond or config state | `pairing failed` or encryption failure on every attempt | the bond file on `InternalFS` — **UNKNOWN** |
| Recoverable by power cycle | the node answers again after it | the order of recovery steps in §8 |
| Recoverable only by erase or reflash | nothing ever answers, or every pairing fails | the order of recovery steps in §8 |

Atta-dipa keeps **no** record of the last battery request before a disconnect:
the request is never logged, and `received_at`
(`link/src/meshcore_companion.cpp:1645` — "battery.received_at = now;") lives
only in memory. A post-mortem cannot say whether a poll was in flight.

## 7. Is battery separable from storage?

Within command 20, **no**: its one 11-byte reply computes both (§2), and it is
the only battery command this client sends. At the protocol level, **yes**:
`CMD_GET_STATS` with `STATS_TYPE_CORE`
([`MyMesh.cpp:1865`](https://github.com/meshcore-dev/MeshCore/blob/d92964352441e53b93e8667b802e04f6e072b39e/examples/companion_radio/MyMesh.cpp#L1865)
— "} else if (cmd_frame[0] == CMD_GET_STATS && len >= 2) {"), whose reply
carries battery millivolts, uptime, error flags and queue length and touches no
filesystem (`MyMesh.cpp:1871` — "uint16_t battery_mv = board.getBattMilliVolts();").
It is recorded here as an option only. Whether to switch is a production change
and needs evidence from §8 first; no issue is filed for it.

## 8. Open — NOT EXECUTED — HARDWARE REQUIRED

On an owner-authorised expendable T114 only, after exporting identity,
contacts, channels and prefs:

1. **Baseline:** the unmodified fleet image, the poll suppressed by a test
   harness, N reconnect cycles; numerator, denominator, time to failure.
2. **Poll on:** the same matrix with the shipping 60 s cadence and one poll due
   per handshake.
3. **Write race isolated:** poll off, and both sides of §4's race forced into
   the same window at each connection — a contact change that marks the table
   dirty, so the loop task saves it, and a fresh pairing, so the Callback task
   saves the bond key. A run that forces only one side cannot test explanation
   1 and is no evidence against it.
4. **#3503 head** on a separate image, watching for deadlocks in contact and
   channel operations.
5. **After a failure:** capture node serial, reset reason, identity, counts and
   a flash image, then recover in order — client reconnect, watch restart, node
   power cycle, bond removal, filesystem erase, reflash — never erasing before
   capture.

Power-cut trials stay with #654 (M51, M52); they are not this experiment.

## 9. Against #654

`MESHCORE_CONTACT_STORE_RECOVERY.md`
([#744](https://github.com/hleserg/Attadipa/pull/744)) and this report share one mechanism — the nRF52 core rewrites a whole 4 KiB
page per write, through one cache (M52) — and keep separate root causes. #654
is a **power cut** during a save; this is **two tasks** sharing the cache with
the power on. Both end in the same blind spot: a contact walk that completes
says nothing about whether the store under it is intact.

## Rejected

- **Calling the poll harmless.** No write path was found; that is not proof of
  no effect, and the upstream correlation stands unexplained.
- **Calling the poll the cause.** The traversal is read-only and alone on its
  filesystem; the race that writes exists without it.
- **Copying `LockedLFS.h`.** Unmerged, leaves mount and format unlocked, and
  its benchmark does not isolate the traversal.
- **Treating 100 clean upstream connections as proof for a T114.** Different
  board, different client.
