# MeshCore parser bounds at the pinned revision

Research for [issue #142](https://github.com/hleserg/Attadipa/issues/142).
Read on 2026-08-23. **Research only — no Attadipa production code changed, and
none should on the strength of this document alone.**

Three upstream pull requests filed on 2026-08-21 and 2026-08-22 claim missing
length checks in MeshCore's frame and advertisement parsers, found by fuzzing.
The question this answers is not "are they right" — they are — but the three
that actually decide anything here:

1. do the findings hold **at the revision this project pinned**, rather than at
   the `dev` commits the pull requests are based on;
2. what each one can actually do, traced from the byte that triggers it to the
   buffer it lands in and the caller that supplied it;
3. what any of it costs Attadipa, which today links no MeshCore code at all.

The short answers are yes; less than the word "out-of-bounds" suggests in three
cases and rather more in a fourth that none of the pull requests mention; and
nothing today, but it moves two decisions that are not yet made.

Everything below was executed on a host. **Nothing here was run on a radio, a
node or any physical board — NOT EXECUTED — HARDWARE REQUIRED.** Where a claim
could not be executed at all it says so in its own row rather than in a footnote.

---

## 1. The revision, and why the pull request bases do not get in the way

| | |
|---|---|
| Attadipa's pin | `d92964352441e53b93e8667b802e04f6e072b39e` |
| What that is upstream | tag `companion-v1.17.1` (also `repeater-`, `room-server-`), released 2026-08-14 |
| `meshcore-dev/MeshCore` `main`, 2026-08-23 | `d92964352441e53b93e8667b802e04f6e072b39e` — **the same commit** |
| `meshcore-dev/MeshCore` `dev`, 2026-08-23 | `9d7cee66394fffd6e8c6e9f39fe03660cb314f64`, 2026-08-22 |
| Licence | **MIT**, `license.txt` in the clone |

So the pin is not lagging: it *is* the current release and the current `main`.
Nothing has been released that contains any of these guards.

The pull requests are based on `dev`, which made the issue's own framing cautious
about comparing them to our pin. That caution turns out to be unnecessary, and it
is worth saying why rather than asserting it, because "the base is a different
branch" is normally a real obstacle:

```
$ git diff --quiet d929643 <pr-base> -- <file>     # for each affected file
src/Packet.cpp                    vs e0031870 (base of #3267)      : identical
src/Packet.cpp                    vs 9d7cee66 (base of #3269/#3270): identical
src/Dispatcher.cpp                vs e0031870                      : identical
src/Dispatcher.cpp                vs 9d7cee66                      : identical
src/Mesh.cpp                      vs e0031870                      : identical
src/Mesh.cpp                      vs 9d7cee66                      : identical
src/helpers/AdvertDataHelpers.cpp vs e0031870                      : identical
src/helpers/AdvertDataHelpers.cpp vs 9d7cee66                      : identical
src/Utils.cpp                     vs e0031870                      : identical
src/Utils.cpp                     vs 9d7cee66                      : identical
```

Every file any of these pull requests touches is byte-identical between our pin
and both of their bases. The 29 commits `dev` is ahead by are elsewhere. So each
diff applies to `d929643` unchanged, and a measurement taken on a pull request's
head tree is a measurement of our pin plus that pull request's guards.

### The supersession graph, checked rather than repeated

| PR | State on 2026-08-23 | Base | Head | Files | What it is |
|---|---|---|---|---|---|
| [#3266](https://github.com/meshcore-dev/MeshCore/pull/3266) | **closed, unmerged** | `main@d929643` | `d87dd32f` | 30 (+804/−38) | the parser fix buried in board variants, `platformio.ini`, UI drivers and `armstubs.cpp` |
| [#3267](https://github.com/meshcore-dev/MeshCore/pull/3267) | **open** | `dev@e003187` | `05da523e` | 2 (+10/−0) | #3266's `Dispatcher.cpp` and `Packet.cpp` hunks, **byte-identical**, with the other 28 files dropped |
| [#3269](https://github.com/meshcore-dev/MeshCore/pull/3269) | **open** | `dev@9d7cee6` | `5ebf8ef9` | 1 (+4/−0) | a `MESH_DEBUG_PRINTLN` on the PATH length mismatch, and nothing else |
| [#3270](https://github.com/meshcore-dev/MeshCore/pull/3270) | **open** | `dev@9d7cee6` | `f80d805e` | 1 (+6/−0) | three `return` guards in `AdvertDataParser` |
| [#3271](https://github.com/meshcore-dev/MeshCore/pull/3271) | **closed, unmerged** | `dev@9d7cee6` | `f80d805e` | 1 | **the same head commit as #3270**, not merely equivalent |

None is merged. None is in a release. Nothing below may be written up anywhere as
"upstream fixed it".

---

## 2. The harness

`docs/research/meshcore-parser-bounds/` holds it, with the shims, the build
script and the runner. It is **not** part of any Attadipa build: no CMake target
references it, and it is under `docs/` deliberately so that it cannot become one
by accident.

What it compiles is upstream's own code. `src/Packet.cpp`, `src/Dispatcher.cpp`,
`src/helpers/AdvertDataHelpers.cpp` and `src/Utils.cpp` are taken unmodified from
a `git archive` of the revision under test and compiled against four small shim
headers — `Arduino.h`, `Stream.h`, `SHA256.h`, `AES.h` — that supply just enough
of the Arduino and Crypto surface to link on a desktop. **The `SHA256` and
`AES128` shims are not ciphers and are labelled as such in their own files**;
nothing measured here depends on what they compute, only on how many bytes the
code around them moves.

Two independent boundary mechanisms, because one of them quietly lied:

- **AddressSanitizer**, which names the offending source line — that is what
  makes the evidence quotable;
- **a `PROT_NONE` guard page** with the input placed so that its last declared
  byte is the last addressable byte before the wall.

The guard page is not belt-and-braces. The first version of the harness used a
tight `malloc(len)` alone, and **ASan does not report a read at offset 0 of a
zero-size allocation** — which silently turned the two `len == 0` cases green on
a build that had no guard whatsoever. Both of those cases are real, and both were
recovered only when the wall replaced the redzone. It is recorded here because
the failure mode is not obvious and the next person to build a harness like this
will hit it.

The buffer being exactly `len` bytes is the whole design. It separates *"the
parser reads past the length it was given"*, which is a property of the parser
and is what the harness measures, from *"the read left the allocation"*, which is
a property of the caller and is answered by reading the call sites. Conflating
the two is how a parser bug gets written up as a crash it cannot cause — or, as
in P4 below, how a real one gets missed.

---

## 3. Findings

Five, of which the pull requests describe three. Each row states what has to be
on the wire, where the parser is reached from, what buffer actually backs the
read at that call site, and what happens. Nothing is inferred from a pull request
description.

### P1 · `Dispatcher::tryParsePacket` reads three fields it has not proved are there

**Where:** `src/Dispatcher.cpp:149-189` at `d929643`.

```cpp
pkt->header = raw[i++];                              // :152  no len check
if (pkt->hasTransportCodes()) {
  memcpy(&pkt->transport_codes[0], &raw[i], 2); i += 2;   // :159  no len check
  memcpy(&pkt->transport_codes[1], &raw[i], 2); i += 2;   // :160
}
pkt->path_len = raw[i++];                            // :165  no len check
...
if (path_byte_len > MAX_PATH_SIZE || i + path_byte_len > len) return false;  // :173 present
```

The path-bytes check at `:173` is there. The three reads before it are not.

**Precondition:** `len < 6` with `header & 0x03 == 0x00` or `0x03` (a transport
route, four extra bytes), or `len < 2` otherwise. Maximum index reached before
the first bound check is `raw[5]`.

**Reachable from: two callers, not one, and the second is the interesting one.**

| Caller | Direction | Buffer behind `raw` |
|---|---|---|
| `Dispatcher::checkRecv`, `src/Dispatcher.cpp:205` | the radio | `uint8_t raw[MAX_TRANS_UNIT+1]` — a **256-byte stack array**, filled by `_radio->recvRaw(raw, 255)` |
| `MyMesh::handleCmdFrame`, `examples/companion_radio/MyMesh.cpp:2000` | **the companion link** — `CMD_SEND_RAW_PACKET`, `tryParsePacket(pkt, &cmd_frame[2], len - 2)` | `cmd_frame[MAX_FRAME_SIZE+1]` — a **177-byte member array** of `MyMesh` |

The second is the one place in this whole document where **a client hands bytes
to a MeshCore parser**, and a client is what Attadipa is. Its guard is `len >= 4`,
so the parser can be called with a declared length of 2, and with a transport
route in the header it then reads `cmd_frame[4..7]` having been given
`cmd_frame[2..3]`. That is reachable by any connected app, ours included, by
sending four bytes.

**What actually backs `raw`, either way:** a fixed array far larger than this
parser's furthest reach, which is `raw[5]`. **The read never leaves the
allocation** on either caller. It picks up bytes the current frame did not write
— the tail of an earlier frame on the radio path, the tail of an earlier command
on the companion path.

**Outcome:** `reject`, in every case. Follow the arithmetic: whatever garbage
`path_len` picks up, `i` is already at least 2, so `i + path_byte_len > len`
holds for any `len` short enough to have triggered the over-read, and
`tryParsePacket` returns `false`. No crash, no disclosure — the parsed packet is
freed. Both callers also gate on a positive length (`len > 0` in `checkRecv`,
`len >= 4` on the command), so the missing `len < 1` guard that #3267 adds is
unreachable through either; case A3 exists to characterise the function, not a
reachable state.

**Status:** confirmed by execution (A1, A2, A3). Fixed on `#3267`'s head.
**Severity for a stock node: cosmetic — a use of uninitialised memory whose
result is discarded.** It is worth fixing because MSan-class tooling will flag it
forever and because the next caller might not have a 256-byte buffer, not because
a packet can do harm through it today.

### P2 · `Packet::readFrom` is missing the check `tryParsePacket` has

**Where:** `src/Packet.cpp:65-84` at `d929643`.

```cpp
uint8_t bl = getPathByteLen();
memcpy(path, &src[i], bl); i += bl;   // :78  bl up to 64, and nothing compares i+bl to len
if (i >= len) return false;           // :80  too late
```

Same three missing checks as P1, **plus** the path-bytes bound that
`tryParsePacket` does have. `isValidPathLen` has already capped `bl` at
`MAX_PATH_SIZE`, so `path[64]` cannot be overflowed on the write side; the read
side is unbounded against `len`.

**Precondition:** `len < 2 + bl`, where `bl = (path_len & 63) * ((path_len >> 6) + 1)`
and `isValidPathLen` accepts it. Largest over-read 64 bytes, at `path_len == 0x40`
(32 hashes × 2) or `0x3F` (63 × 1).

**Reachable from:** four callers, and they do not behave alike.

| Caller | Buffer behind `src` | Can the over-read be triggered? |
|---|---|---|
| `helpers/bridges/RS232Bridge.cpp:87` | `_rx_buffer[MAX_TRANS_UNIT+1+6]` = 262 B, `src = _rx_buffer+4` | **yes** — `len` comes from the frame's own length field. Furthest reach `_rx_buffer[70]`: **inside the allocation**, stale bytes |
| `helpers/bridges/ESPNowBridge.cpp:147` | `decrypted[MAX_ESPNOW_PACKET_SIZE]` = 250 B on the stack | **yes**, same shape, same in-allocation reach |
| `helpers/BaseChatMesh.cpp:560` `importContact` | the companion command frame | **no** — see below |
| `helpers/BaseChatMesh.cpp:546` `shareContactZeroHop` | `temp_buf`, from our own stored blob | not attacker-supplied |

**`importContact` is the one that matters to us, and it is not reachable.** It is
the companion-protocol path — `CMD_IMPORT_CONTACT`, `examples/companion_radio/MyMesh.cpp:1362-1363`
— so it is the one place a *client*, which is what Attadipa is, hands bytes to
this parser. The guard on that branch is `len > 2 + 32 + 64`, i.e. at least 99
bytes, and the parser's furthest reach is `i + bl ≤ 6 + 64 = 70`. The minimum the
caller enforces exceeds the maximum the parser can want, so the over-read cannot
happen through it. That is luck rather than design — the constant is there to
reject a truncated identity, not to bound the parser — but it holds at this
revision, and it means **a malformed contact blob from a companion client cannot
reach P2**.

Both bridges are optional builds and neither is on the companion path.

**Outcome:** `reject`, again by arithmetic: after the over-read `i = 2 + bl`
exceeds the `len` that caused it, so `i >= len` returns `false`.

**Status:** confirmed by execution (B1, B2, B3). Fixed on `#3267`'s head.

### P3 · `PAYLOAD_TYPE_PATH` underflows `extra_len`, and #3269 does not stop it

**Where:** `src/Mesh.cpp:161-172` at `d929643`.

```cpp
uint8_t data[MAX_PACKET_PAYLOAD];                          // :157   184 bytes, stack
int len = Utils::MACThenDecrypt(secret, data, macAndData, pkt->payload_len - i);
if (len > 0) {
  if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH) {
    int k = 0;
    uint8_t path_len = data[k++];
    if (!Packet::isValidPathLen(path_len)) break;
    uint8_t hash_size = (path_len >> 6) + 1;
    uint8_t hash_count = path_len & 63;
    uint8_t* path = &data[k]; k += hash_size*hash_count;   // k up to 65
    uint8_t extra_type = data[k++] & 0x0F;                 // k up to 66
    uint8_t* extra = &data[k];
    uint8_t extra_len = len - k;                           // :172  underflows when k > len
```

`isValidPathLen` bounds `k` at 66 but never compares it to `len`. When `k > len`
the subtraction is done in `int` and truncated into a `uint8_t`, so a small
negative becomes a large positive, and `extra`/`extra_len` are then handed to
`onPeerPathRecv` as a window that runs off the end of `data`.

**The domain, computed rather than asserted.** `Utils::decrypt` returns whole
16-byte blocks — *"will always be multiple of 16"*, `src/Utils.cpp:81` — so `len`
is a multiple of 16. Over every `(len, path_len)` pair that reaches this code
with `len` in 16…176:

```
accepted (len, path_len) pairs            : 1309
pairs where k > len (extra_len underflows):  187
  of those, &data[k] + extra_len > 184    :  187      <- all of them
smallest underflowing input               : len=16 path_len=0x0F
                                            -> k=17, extra_len=255,
                                               window data[17..271] against data[0..183]
                                               (88 bytes past the end)
```

Every underflow produces a window past the end of the 184-byte stack buffer.
There is no benign corner of this one.

**Reachable from:** the radio, addressed to this node, from a source hash that
matches a contact, **and past the MAC check**. That last gate is `CIPHER_MAC_SIZE`
= **2 bytes** (`src/MeshCore.h:17`) — already recorded as
[M11](OPEN_QUESTIONS.md#meshcore). So it is not "authenticated peers only": a
party holding the shared secret passes it always, and a party holding nothing
passes it with probability ~1/65536 per candidate peer per packet, with up to
four candidates tried and no rate limit in this loop. `_tables->wasSeen` dedupes
identical packets, which costs an attacker one varied byte per attempt.

**What the consumer then does — analysed, not executed.** In `companion_radio`,
the stock firmware Attadipa's node path talks to, the chain is
`BaseChatMesh::onPeerPathRecv` (`BaseChatMesh.cpp:316-326`) →
`MyMesh::onContactPathRecv` (`MyMesh.cpp:750`) → `BaseChatMesh::onContactPathRecv`
(`:328-345`) → `MyMesh::onContactResponse` (`MyMesh.cpp:676-747`), whose three
`pending_*` branches each do

```cpp
memcpy(&out_frame[i], &data[4], len - 4);     // MyMesh.cpp:722, :733, :744
```

with `out_frame[MAX_FRAME_SIZE + 1]` = **177 bytes** and `i` already at 8 in the
status and telemetry branches, 6 in the binary-response one. With `len`
underflowed to 255 that writes some 80 bytes past a member array — a write, not
a read. Two things stop this being called a proven memory-corruption path and
both are load-bearing:

- `extra_type` and the four `tag` bytes the branches match on are read from
  `data[k..]`, which is **past `len`** — so they are stale stack bytes, not
  anything the attacker put in this packet. Steering them means grooming the
  same stack region with an earlier packet. Plausible; **not demonstrated here**;
- the branches additionally require the local app to have a matching `pending_status`,
  `pending_telemetry` or `pending_req` outstanding.

**Status of the underflow: confirmed** (exhaustive, `path_arith.cpp`).
**Status of the write overflow behind it: NOT REPRODUCED** — reaching it needs
the full node build with real AES, SHA-256 and ed25519, which this harness does
not have. See §6.

**#3269 does not fix this.** Its entire diff is

```cpp
if (k >= len) {
  MESH_DEBUG_PRINTLN("%s PAYLOAD_TYPE_PATH, set path_len %u exceeds ...");
}
uint8_t extra_type = data[k++] & 0x0F;      // unchanged — still executes
```

— a log line with no `break`, no `return`, and no change to control flow, in a
build where `MESH_DEBUG_PRINTLN` expands to `{}` unless `MESH_DEBUG` is set. The
issue's reading of it is right, and this is the precise reason: it reports the
condition and then does the thing.

### P4 · `Utils::decrypt` rounds up past the destination — not from any of the pull requests

**Where:** `src/Utils.cpp:70-83`, reached from `src/Mesh.cpp:158`.

```cpp
// Utils.h: "'src_len' should be multiple of block size, as returned by 'encrypt()'"
while (sp - src < src_len) {
  aes.decryptBlock(dp, sp);       // :77
  dp += 16; sp += 16;
}
return sp - src;  // will always be multiple of 16
```

A documented precondition with no enforcement, and an on-air caller that does not
honour it. `Mesh::onRecvPacket` passes `pkt->payload_len - 2` into
`MACThenDecrypt`, which passes `src_len - 2` into `decrypt`, into a **184-byte**
stack destination. `payload_len` is attacker-chosen up to 184 (`tryParsePacket`
rejects more). For `payload_len` in **181…184** the source length is 177…180, the
loop runs twelve times, and it writes **192 bytes into 184**.

**Reproduced.** Against the real `src/Utils.cpp` from the pinned tree, with a
stub block cipher — the bound belongs to the loop, not to the cipher — and a
destination of exactly `MAX_PACKET_PAYLOAD` bytes against a guard page:

```
src_len = 176 : decrypt() returned 176 — wrote dest[0..175]        clean
src_len = 177 : SEGV in mesh::Utils::decrypt at src/Utils.cpp:77
src_len = 180 : SEGV in mesh::Utils::decrypt at src/Utils.cpp:77
src_len = 182 : SEGV in mesh::Utils::decrypt at src/Utils.cpp:77
```

**Reachable from:** the radio, for `PAYLOAD_TYPE_PATH`, `REQ`, `RESPONSE` and
`TXT_MSG`, behind the same 2-byte MAC as P3 — see the note there about what that
gate is and is not worth.

**What lands where: an 8-byte stack write past `uint8_t data[184]` in
`Mesh::onRecvPacket`.** That is qualitatively different from P1, P2 and P5: it
leaves the allocation, and it is a write. On a desktop it faulted. On an
ESP32-S3 with no stack canary between `data` and its neighbours, what those eight
bytes hit is a property of the frame layout the compiler chose, and this project
has not compiled that firmware for that target — **UNKNOWN, and it stays UNKNOWN
until somebody does.**

**Status: reproduced at function level; NOT REPRODUCED end-to-end** through
`Mesh::onRecvPacket`, for the same reason as P3. The precondition chain is read
from source and every link is quoted above.

Not fixed, not filed, not known upstream as far as the pull request queue shows.
**Attadipa has not reported it and should not do so unilaterally** — see §5.

### P5 · `AdvertDataParser` reads on flag bits, and #3270 misses its own first byte

**Where:** `src/helpers/AdvertDataHelpers.cpp:31-61` at `d929643`.

```cpp
_flags = app_data[0];                                    // :34  no app_data_len check
int i = 1;
if (_flags & ADV_LATLON_MASK) { memcpy(&_lat, &app_data[i], 4); i += 4;   // :40
                                memcpy(&_lon, &app_data[i], 4); i += 4; } // :41
if (_flags & ADV_FEAT1_MASK)  { memcpy(&_extra1, &app_data[i], 2); i += 2; } // :44
if (_flags & ADV_FEAT2_MASK)  { memcpy(&_extra2, &app_data[i], 2); i += 2; } // :47
if (app_data_len >= i) { ... _valid = true; }            // :49  the only length test
```

Up to `app_data[12]` is read before `app_data_len` is consulted at all.

**Precondition:** `app_data_len < 1 + 8·[LATLON] + 2·[FEAT1] + 2·[FEAT2]`, i.e.
any advert whose flags promise fields its length does not contain.

**Reachable from:** the radio, `src/Mesh.cpp:267-269` →
`Mesh::onAdvertRecv` → `BaseChatMesh.cpp:121`, **after** the Ed25519 signature
over `pub_key ‖ timestamp ‖ app_data` has verified. That is a weaker gate than it
sounds: the attacker signs their own advert with their own key, which anyone can
generate. Also `examples/simple_repeater/MyMesh.cpp:656`, same data.

**What actually backs `app_data`:** `&pkt->payload[100]`, inside
`uint8_t payload[184]`, and `app_data_len` is clamped to `MAX_ADVERT_DATA_SIZE`
(32) one line earlier at `Mesh.cpp:269`. So the twelve-byte reach stays inside
the `Packet` object — **the read never leaves the allocation on this path**.

That clamp is also what keeps a much worse bug from existing. `_name` is
`char[MAX_ADVERT_DATA_SIZE]` = 32 and the parser ends with
`memcpy(_name, &app_data[i], app_data_len - i); _name[nlen] = 0;` with no bound
of its own. Were `app_data_len` allowed past 32 — the payload has room for 84 —
that would be a straightforward stack write overflow. `Mesh.cpp:269` is the only
thing preventing it, and it is one line in one of the two callers. Worth knowing
before anyone refactors it; not a defect today.

**Outcome:** `reject`. `_valid` requires `app_data_len >= i`, which is exactly
the condition that failed, so the parser returns an object the callers discard —
`BaseChatMesh.cpp:122` and `MyMesh.cpp:657` both test `isValid()`.

**Status: confirmed by execution (C1, C2, C3, C4), and #3270 does not close it.**
On `#3270`'s own head `f80d805e`, case C3 — `app_data_len == 0` — still reads
`app_data[0]` at `AdvertDataHelpers.cpp:34`. The diff guards the lat/lon and
feature reads and leaves the flags byte in front of them exactly as it was. Since
`#3271` is the same commit, both of the advert pull requests are incomplete in
the same place.

Is `app_data_len == 0` reachable? Yes: `Mesh.cpp:261` tests `i > pkt->payload_len`,
not `>=`, so a correctly signed advert with `payload_len == 100` and no app data
gives `app_data_len == 0`. It lands on a stale in-allocation byte and is rejected,
so the consequence is nil — but a fix that leaves the case it was written for
still reading out of bounds is not a fix, and this is why the harness's `len == 0`
cases had to be made to work.

---

## 4. The corpus

Ten sequences. Each is the whole input; each is fed as a buffer of exactly its own
length with a guard page behind it. `base` is `d929643`.

| # | Parser | Bytes (hex) | `len` | Reads, and where the tool stops it | base | `#3267` head | `#3270` head |
|---|---|---|---|---|---|---|---|
| A1 | `tryParsePacket` | `01` | 1 | 1 B at `Dispatcher.cpp:165` | **OOB** | clean, `false` | **OOB** |
| A2 | `tryParsePacket` | `00` | 1 | 2 B at `Dispatcher.cpp:159` | **OOB** | clean, `false` | **OOB** |
| A3 | `tryParsePacket` | *(empty)* | 0 | 1 B at `Dispatcher.cpp:152` | **OOB** | clean, `false` | **OOB** |
| B1 | `Packet::readFrom` | `01 3F` | 2 | **63 B** at `Packet.cpp:78` | **OOB** | clean, `false` | **OOB** |
| B2 | `Packet::readFrom` | `01` | 1 | 1 B at `Packet.cpp:74` | **OOB** | clean, `false` | **OOB** |
| B3 | `Packet::readFrom` | `00` | 1 | 2 B at `Packet.cpp:69` | **OOB** | clean, `false` | **OOB** |
| C1 | `AdvertDataParser` | `91` | 1 | 4 B at `AdvertDataHelpers.cpp:40` | **OOB** | **OOB** | clean, `invalid` |
| C2 | `AdvertDataParser` | `F1` | 1 | 4 B at `AdvertDataHelpers.cpp:40` | **OOB** | **OOB** | clean, `invalid` |
| C3 | `AdvertDataParser` | *(empty)* | 0 | 1 B at `AdvertDataHelpers.cpp:34` | **OOB** | **OOB** | **OOB** |
| C4 | `AdvertDataParser` | `21` | 1 | 2 B at `AdvertDataHelpers.cpp:44` | **OOB** | **OOB** | clean, `invalid` |

The read widths are the `memcpy` and subscript widths in the source; a
multi-field read is attributed to the statement that first crosses the boundary,
so A2 is the first of two `memcpy`s and C1 the first of two. The line numbers are
the tool's. With the guard page in place ASan words all ten as `SEGV` at those
lines; an earlier build of the same corpus against a tight `malloc(len)` instead
worded the eight non-zero-length cases as `heap-buffer-overflow` with
`READ of size N` matching the widths above — and reported nothing at all for the
two `len == 0` cases, which is why the guard page is the mechanism of record.

Plus two experiments that are not single byte sequences:

| # | What | Result |
|---|---|---|
| P3 | every `(len, path_len)` reaching `Mesh.cpp:161-172`, `len` ∈ 16…176 | 187 of 1309 underflow `extra_len`; **all 187** give a window past `data[184]`; smallest `len=16, path_len=0x0F` |
| P4 | `Utils::decrypt` into a 184-byte destination | clean at `src_len=176`; faults at `src_len` 177, 180, 182 at `Utils.cpp:77` |

**Nine of ten cases over-read on the pinned revision. No head fixes all of them:**
`#3267` closes A and B and leaves C untouched; `#3270` closes C1, C2, C4 and
leaves A, B and **C3** untouched. Vendoring any one of them would buy part of the
problem.

---

## 5. What this costs Attadipa

**Today: nothing, and that is a fact about the repository rather than an
opinion.** Attadipa links no MeshCore code. There is no local provider, and the
stock-node path is a protocol client behind
[ADR-0008](../adr/0008-mesh-service-providers.md). Every one of P1–P5 executes on
the node, on the far side of the companion link.

It changes four things anyway.

**One of them is reachable from our side of the link, and it is the only one.**
`CMD_SEND_RAW_PACKET` calls `tryParsePacket` on a client-supplied buffer
(`examples/companion_radio/MyMesh.cpp:2000`), gated only by `len >= 4`, so a
four-byte command from any connected app makes the node read four bytes of its
own previous command frame. It stays inside `cmd_frame` and ends in
`ERR_CODE_ILLEGAL_ARG`, so the consequence is nil — but it is the one place where
Attadipa is the party handing bytes to a MeshCore parser rather than the party
receiving the result, and that is worth knowing before anyone writes a client
that emits raw packets. Two practical consequences, neither of them urgent: if
Attadipa ever uses that opcode it should send a complete frame or none, and a
node shared with another client is a node whose parser another client can poke.
It does **not** follow that the companion link is dangerous — P2's
`CMD_IMPORT_CONTACT` path was checked and cannot be reached at all.

**The node's output is a peer's output, not a trusted source.** P3 and P4 are
memory-safety defects on the device that supplies Attadipa's mesh capability, and
P4 can be provoked by a third party on the air who holds nothing but a 1-in-65536
chance per packet. The realistic consequence for a watch is that its node
misbehaves or reboots — which is capability withdrawal, which
[ADR-0008](../adr/0008-mesh-service-providers.md) and
[ADR-0002](../adr/0002-companion-is-optional.md) already require us to survive.
The unrealistic-but-not-excluded consequence is a node whose memory has been
corrupted and which then sends us plausible frames. Our side of that boundary
must keep treating positions, headings and messages arriving over the link as
peer claims: validated on arrival, never promoted to trusted because the node
sounded confident. That is the existing rule, and this is evidence for it rather
than a change to it — no ADR is amended by this document.

**A local MeshCore provider inherits all five.** The moment `LocalMeshProvider`
becomes real, P1–P5 stop being facts about somebody else's firmware. A pin or
upgrade decision taken then must be made against the state of these five, not
against a version number. That is the criterion this research was asked to
establish, and it is now written down: **do not pin a MeshCore revision for the
local provider without re-running the corpus in
`docs/research/meshcore-parser-bounds/` against it.**

**Our own decoder is not analogous, and it was checked rather than assumed.**
`link/src/frame_codec.cpp` validates the declared length *before* reading —
`if (declared > kMaxPayload)` at `:139`, counted as its own error class, and
`if (size_ < needed) return 0;` at `:147` before any payload is touched — behind
a length-check byte at `:123` and a CRC. The issue's caution about not
transplanting the finding onto it is right. **No change to `attadipa_link` is
proposed by this document.** Its boundary fixtures may still be worth extending
with the shape these findings share — a declared length that outruns the frame —
and that is a separate, executable task, not this one.

### Decision

**ADAPT the rejection cases, MONITOR the pull requests, vendor nothing.**

- **Do not vendor** `#3267`, `#3269` or `#3270`. Unmerged, unreleased, and two of
  the three are incomplete against their own findings. Copying a guard from an
  open pull request into a project that does not yet compile the file it guards
  buys nothing and dates immediately.
- **Monitor** all three plus `dev`. The re-check is cheap: `build.sh <tag> <ref>`
  then `run.sh`.
- **Keep the corpus.** It is the executable form of this document and the entry
  condition for any future pin.
- **P4 should be reported upstream, and that is the owner's call, not an agent's.**
  It is a memory-safety defect in a third-party project, it is a write rather
  than a read, and opening an issue on `meshcore-dev/MeshCore` is an outward-facing
  act this run did not have authority for. The evidence needed is in §3 P4 and
  reproduces in one command. Recorded as a recommendation, deliberately not acted
  on.

No ADR changes. The trust boundary these findings press on is
[ADR-0008](../adr/0008-mesh-service-providers.md)'s and it already holds; nothing
here moved it, and an ADR edited to say what it already said is churn.

---

## 6. What was not established

| Claim | Status |
|---|---|
| Any of this on a radio, a node, or a board | **NOT EXECUTED — HARDWARE REQUIRED.** Nothing here touched hardware, and no host sanitizer result may be presented as radio or HIL validation |
| The pull request authors' own claim of verification on a Heltec V4 | **not independently checked.** No crash trace, corpus or sanitizer output is attached to any of the three pull requests; taken as an unverified author statement |
| P3's write overflow in `companion_radio` | **NOT REPRODUCED.** Needs the full node build with real AES-128, SHA-256 and ed25519, which this harness does not have. The underflow it depends on is proven; the path from there to `out_frame` is read from source |
| Whether `extra_type` and `tag` in P3 can be steered by grooming the stack across packets | **UNKNOWN.** Plausible, untested, and the difference between "conditional" and "controllable" for that finding |
| P4 end-to-end through `Mesh::onRecvPacket` | **NOT REPRODUCED**, same reason. Reproduced at `Utils::decrypt` |
| What P4's eight bytes overwrite on an ESP32-S3 | **UNKNOWN.** Depends on a stack frame layout nobody here has compiled |
| Whether any of this is exploitable rather than merely wrong | **UNKNOWN, and deliberately not claimed.** P1, P2 and P5 end in a rejected packet. P3 and P4 leave a buffer, which is a necessary and not a sufficient condition for anything worse |

The harness has no fuzzer behind it. It runs a hand-built corpus derived from
reading the parsers, so it demonstrates the findings and does not search for
more. A real fuzzing pass over the pinned tree would be a separate task and would
want the genuine crypto libraries, not the stubs used here.

---

## 7. Reproducing this

```bash
git clone --filter=blob:none https://github.com/meshcore-dev/MeshCore /tmp/meshcore-src
cd docs/research/meshcore-parser-bounds
./build.sh base   d92964352441e53b93e8667b802e04f6e072b39e
./build.sh pr3267 05da523ebd32980a1c28b11f2928d351796b9737
./build.sh pr3270 f80d805ee8b20f77ff5b3ca6bc3a9021989aafd2
./run.sh                      # the ten-case matrix in §4
./build-extras.sh
./build/path_arith            # P3
./build/decrypt_bounds 180    # P4; 176 is clean
```

The two pull request heads have to be fetched by SHA before they resolve —
`git -C /tmp/meshcore-src fetch origin <sha>`; `build.sh` says so if they have not
been. **`run.sh` does not cover P3 or P4**, so a green matrix is not a clean
revision.

Needs `clang++` with AddressSanitizer. Measured on clang 18.1.3, Ubuntu 24.04,
2026-08-23. It reads the upstream clone and writes only inside its own directory.

---

## References

- Upstream: `meshcore-dev/MeshCore`, MIT, pinned `d92964352441e53b93e8667b802e04f6e072b39e`
- [`REUSE_LEDGER.md`](REUSE_LEDGER.md) — the pin and the monitored deltas
- [`OPEN_QUESTIONS.md`](OPEN_QUESTIONS.md) — M10–M14 from the first reading; M15–M17 from this one
- [`VERIFIED_FACTS.md`](VERIFIED_FACTS.md) — what is now traced to executed evidence
- [`../upstream/meshcore-1.17-review.md`](../upstream/meshcore-1.17-review.md) — T-041, the review this continues
- [`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md) — the wire format on the client side
- [ADR-0008](../adr/0008-mesh-service-providers.md) — the two providers and the trust boundary this presses on
