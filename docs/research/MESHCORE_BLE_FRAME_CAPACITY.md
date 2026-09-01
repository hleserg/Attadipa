# What a MeshCore companion frame actually fits over BLE

**One number was doing the work of four**, here and upstream, and this document
separates them. It answers issue
[#143](https://github.com/hleserg/Attadipa/issues/143) and corrects
[MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md) §2, which said
*"176 is the number for every transport"*.

It is research. **No Attadipa code changed and none should have** — this
repository links no MeshCore, and the client this bears on does not exist yet.
The executable half is [`meshcore-ble-frame-capacity/`](meshcore-ble-frame-capacity/).

---

## 0. Provenance

| What | How it was established |
|---|---|
| every upstream commit, PR state, merge SHA and file list | GitHub API against `OffbandMesh/meshcore-firmware` and `meshcore-dev/MeshCore`, 2026-08-23 |
| the corrected invariant, and the defect it replaced | upstream's own headers compiled and executed at both revisions — [`meshcore-ble-frame-capacity/`](meshcore-ble-frame-capacity/) |
| every claim about vanilla MeshCore | read from source at the pinned `d929643`, file and line cited inline |
| the chunk arithmetic | re-derived here, not copied from a commit message; it agrees with all four upstream figures |
| **anything about a physical board** | **`NOT EXECUTED — HARDWARE REQUIRED`.** No Attadipa device has been connected to any MeshCore node over BLE. Every field measurement below is *upstream's*, on *their* hardware, on *their* BLE stack |

---

## 1. Four numbers, and they are not the same number

| # | Name | Value | Set by | Whose |
|---|---|---|---|---|
| 1 | **Protocol / buffer maximum** | **176** | `#define MAX_FRAME_SIZE 176`, `src/helpers/BaseSerialInterface.h:5` — a bare `#define` with no `#ifndef`, so no build flag moves it | the protocol's. Both peers must agree; it is not negotiable |
| 2 | **ATT notification payload** | **negotiated ATT MTU − 3** | the Bluetooth Core specification: a Handle Value Notification spends 3 octets on opcode and handle | the *link's*, settled per connection |
| 3 | **Effective frame ceiling** | the **smaller of rows 1 and 2**, and on a fan-out wrapper the **minimum across the sinks a write actually reaches** | upstream `deliverableFrame()` and `MultiSerialInterface::maxFrameSize()` | the transport set's, and it changes when an interface is enabled or disabled |
| 4 | **Application chunk payload** | **row 3 minus the builder's own header** (2 bytes for the chunked paths measured upstream) | each frame builder | the application's |

On an ESP32 companion whose link negotiates MTU 176 those are **176, 173, 173**
and, in the derivative that measured them, **171** — four quantities, and the one
a client must size its chunks against is the last.

**Row 4 is 171 upstream and 173 here, and the difference is not arithmetic.**
The 2-byte header is a *chunk* header, belonging to `caplog`, the config stream
and the other chunked downloads — and **vanilla has none of those commands**
(§4.1). At our pin the builder header is zero and row 4 collapses onto row 3. The
171 is quoted throughout this document because it is what upstream's evidence is
denominated in; it is **not** a fact about the protocol a vanilla node speaks,
and carrying it across that boundary would be the same move as the defect being
corrected — a number taken out of the context that produced it.

**Why 176 negotiates at all**: `begin()` asks for exactly `MAX_FRAME_SIZE`
(`BLEDevice::setMTU(MAX_FRAME_SIZE)`,
`src/helpers/esp32/SerialBLEInterface.cpp:29`). So the buffer size becomes the
requested MTU, and then the ATT header is taken out of it — the frame buffer was
sized as though it were the payload budget, and it is three bytes larger than
the link it asked for. That is the whole defect in one sentence.

nRF52 is unaffected in the observed configuration for an arithmetic reason and
not a lucky one: at MTU 247 the link delivers 244, which is above 176, so
bound 1 binds instead of bound 2 and the ceiling is the buffer.

---

## 2. The correction chain, so the first wrong theory does not set as fact

Five upstream changes over two releases. **The first four all edited code the
companion path never called** — upstream says so itself in `4f5e8b7a`: *"#450,
#454 and both previous #711 attempts all changed code that is not reached on the
companion path."* Recorded in order, because the *shape* of this mistake is the
transferable part and the last row alone does not show it.

| # | Change | What it claimed | Verdict |
|---|---|---|---|
| **#450** | a hardcoded constant capping the BLE frame | a full frame is clipped to MTU−3 over BLE | right about the symptom; brittle |
| **#454** | generalised it to a computed cap | trust the reported MTU | **wrong**: trusted the *peer's* MTU as the connection's, reintroducing the bug the same day |
| **#711 attempts, beta2–beta4** | further computed replacements | fixed | **wrong, and green.** Shipped in three betas; a tester on beta4 still saw the exact clipping signature |
| **#937** `fda4cdd8` | `deliverableFrame() = min(mtu−3, max_frame−3)`, a hard ceiling from our own pinned preference | a stack may over-report its MTU — `NimBLEDevice::getMTU()` returns the *preferred* value, NimBLE's default 256 | **the theory was refuted**; the ceiling was reverted. Two parts survived: the pure testable `BleFrameSizing.h` unit, and checking `setMTU()`'s discarded return |
| **#939** `4f5e8b7a` | `MultiSerialInterface` overrides `maxFrameSize()`, delegating to the enabled sinks | **this is the defect** | **correct**, and it explains all four predecessors |

### What #939 found, and why nobody found it for months

`companion_radio` holds a `MultiSerialInterface` as its `_serial`. That wrapper
overrode **eight** `BaseSerialInterface` methods — `enable`, `disable`,
`isEnabled`, `isConnected`, `loop`, `isWriteBusy`, `writeFrame`,
`checkRecvFrame` — and **not `maxFrameSize()`**. So every caller got
`BaseSerialInterface`'s default, 176, and the BLE interface's MTU-aware answer
was never reached on the companion path, on any board, on any link.

> Upstream's own commit message says *nine*, and lists those eight. Counted here
> at both revisions: eight `override`s at `fda4cdd8`, nine at `4f5e8b7a` — the
> ninth being the `maxFrameSize()` the same commit adds. The message describes
> the state before its change with the count from after it. Recorded because this
> document is meant to be checkable, and a number transcribed from a commit
> message is not a number that was checked. **Vanilla's wrapper at `d929643` has
> the same eight**, which is the shape without the capacity concept at all.

**Every earlier fix was to code that was not called.** They were reviewed,
tested, merged and shipped, and their tests went green, because the tests called
the leaf directly and the firmware called the wrapper.

> **This is the general lesson, and it is the one that survives the specific
> number.** A correct leaf implementation behind a wrapper that does not delegate
> is indistinguishable from no implementation at all — and it is *worse* than no
> implementation, because the leaf's tests testify that the behaviour exists. Any
> Attadipa transport abstraction that puts a wrapper between an application and a
> concrete transport inherits this hazard the moment it adds one capability query
> the wrapper does not forward.

### The evidence that settled it

Three field reports, one signature. Each is a whole number of full frames
multiplied by exactly 3 bytes:

| Reporter | Announced | Received | Chunks | Short by |
|---|---|---|---|---|
| `madmax_2069` | 8 608 B | 8 461 B | 50 | 147 = 3 × 49 |
| `schill` (beta4, *with* the earlier fix) | 12 973 B | 12 751 B | 75 | 222 = 3 × 74 |
| `hv4-bench-1` | 14 495 B | 14 246 B | 84 | 249 = 3 × 83 |

The bench client log for the third read `MTU set to: 176` and measured
`83 × 171 received + 1 × 53` — the MTU was reported honestly, which is what
refuted #937's over-reporting theory.

### Reproduced here, both sides

The harness compiles upstream's real headers at `fda4cdd8` and `4f5e8b7a` and
drives them. Recorded 2026-08-23, g++ 13.3.0:

```
post   31 checks, 0 failed, 2 hazards recorded
pre    31 checks, 4 failed, 2 hazards recorded   <- expected; the failure IS the defect
```

Two things it shows that reading the diffs does not:

- **Both revisions answer 173 for ESP32 at MTU 176.** A test that checked only
  the ESP32 number could not tell them apart. The revisions differ on nRF52 —
  173 at `pre`, 176 at `post` — which is the reverted ceiling, and on the wrapper.
- At `pre` the wrapper reports **176** and a 2-byte-header payload of **174**,
  which is precisely the 174 that produced 75 chunks for `schill` instead of 76.

---

## 3. The matrix

`d929643` is simultaneously our pin, upstream vanilla's `main` tip and its newest
release, verified 2026-08-23 — so "vanilla" and "our pin" are the same column,
and there is no superseding upstream fix to wait for.

| | vanilla MeshCore `d929643` | Offband `4f5e8b7a` |
|---|---|---|
| **BLE stack, ESP32** | the Arduino-ESP32 core's bundled **Bluedroid** (`BLEDevice`, `BLE2902`, `esp_ble_gatts_cb_param_t`, `BLESecurityCallbacks`), no BLE library in `lib_deps` | **NimBLE** — `h2zero/NimBLE-Arduino @ ^2.0.0` |
| **Is there MTU-aware sizing?** | **No, anywhere.** `maxFrameSize` does not exist in the tree — not in `BaseSerialInterface`, not as a concept. `onMtuChanged()` only logs (`esp32/SerialBLEInterface.cpp:100-101`) | yes: `BleFrameSizing.h` + per-interface override |
| **Does the wrapper delegate capacity?** | not applicable — there is nothing to delegate | yes, minimum across enabled sinks |
| **ESP32 effective frame ceiling** | **176 always**, i.e. 3 above what an MTU-176 link carries | **173** at MTU 176 |
| **nRF52 effective frame ceiling** | 176 always | **176** (link delivers 244; the buffer binds) |
| **nRF52 exposure** | none — a 176-byte frame fits a 244-byte deliverable | none, same reason |
| **Field-measured?** | **no** | yes, ESP32/NimBLE, three reports plus a bench capture |

**The vanilla ESP32 cell is the one that matters to us, and it is the one nobody
has measured.** Upstream's measurement was taken on NimBLE; vanilla is on
Bluedroid. Truncation of an over-long notification to `ATT_MTU − 3` is a
requirement of the Bluetooth Core specification rather than a quirk of either
library, so the expectation is that both behave the same — but *expectation* is
the word. See §7.

---

## 4. What vanilla actually does at our pin

Verified by reading `d929643`. This is the part that decides whether the finding
costs Attadipa anything, and the issue asked for it as an open question — most of
it turned out to be answerable from source.

### 4.1 Vanilla has no fan-out defect, because it has no capacity query

`MultiSerialInterface` exists at `d929643` and holds up to `MAX_INTERFACES` (4)
sinks, but `BaseSerialInterface` declares no `maxFrameSize()` and nothing in the
tree calls one. Upstream's #939 defect **cannot exist here in that form**. What
exists instead is the condition the defect was hiding: every producer sizes
against `MAX_FRAME_SIZE` directly, and no code anywhere asks the link what it can
carry.

Two mitigating facts, both already established in
[MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md) §1: no shipped
vanilla companion build enables two transports at once, so the fan-out has no
real users; and vanilla has no chunked-download command at all — `caplog`, the
config stream, observer views and the block list are Offband additions. So
vanilla's exposure is **per-frame**, not per-transfer.

### 4.2 Four vanilla producers size against the buffer, and one fills it exactly

| Producer | Bound in the code | Largest frame | Reachable? |
|---|---|---|---|
| `logRxRaw` → `PUSH_CODE_LOG_RX_DATA` (0x88) | `len + 3 <= MAX_FRAME_SIZE` (`MyMesh.cpp:287`) | **exactly 176** | **yes.** Called unconditionally for every received raw packet (`Dispatcher.cpp:199`, hook at `Dispatcher.h:159`) with up to `MAX_TRANS_UNIT` = 255 bytes (`MeshCore.h:23`); a 173-byte on-air packet is ordinary |
| `onRawDataRecv` → `PUSH_CODE_RAW_DATA` (0x84) | `payload_len + 4 > sizeof(out_frame)` (`MyMesh.cpp:802`) | **177** | `payload_len` is bounded by `MAX_PACKET_PAYLOAD` = 184 (`MeshCore.h:20`), so 173 is within range |
| `onControlDataRecv` → `PUSH_CODE_CONTROL_DATA` (0x8E) | same shape (`MyMesh.cpp:782`) | **177** | same |
| `onTraceRecv` → `PUSH_CODE_TRACE_DATA` (0x89) | `12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)` (`MyMesh.cpp:824`) | **177** by the guard | only if `path_len` can exceed `MAX_PATH_SIZE` = 64; that is a parser-bounds question and belongs to [#142](https://github.com/hleserg/Attadipa/issues/142), not here |

Everything else fits comfortably: `RESP_CODE_CONTACT` is **148** bytes
(`MyMesh.cpp:166-186`) and `RESP_CODE_DEVICE_INFO` is 82.

`RESP_CODE_SELF_INFO` is 58 plus the node name, and the name **is** bounded:
`char node_name[32]` (`examples/companion_radio/NodePrefs.h:15`), copied out with
`strlen`, so at most 31 characters and a **89-byte** frame. Worth stating with
its source rather than as "fits comfortably", because the field is written
unterminated to the end of the frame (§3 of
[MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md)) — a *client* has
no length to check and cannot tell a truncated name from a short one, so this is
a bound on **this** firmware's sender and not a rule a parser may assume.

**Two distinct failures, and both are silent.**

- A **176-byte** frame is accepted by every `writeFrame()` and handed to the
  link. Over BLE at MTU 176 the last 3 bytes do not arrive.
- A **177-byte** frame is refused outright. `sizeof(out_frame)` is
  `MAX_FRAME_SIZE + 1` = 177 — one byte more than any transport will take. BLE's
  `writeFrame()` returns 0 with a `MESH_DEBUG_PRINTLN`
  (`esp32/SerialBLEInterface.cpp:169-172`); `ArduinoSerialInterface::writeFrame()`
  returns 0 **with no message at all** (`ArduinoSerialInterface.cpp:25-28`). And
  `MESH_DEBUG_PRINTLN` expands to `{}` unless `MESH_DEBUG` is defined
  (`MeshCore.h:29-32`), which the root `platformio.ini` does not define. On a
  stock build the frame simply never appears.

So the guard `+ 4 > sizeof(out_frame)` reads as a bounds check and behaves as a
frame-dropper — **for exactly one input value.** It admits `payload_len <= 173`;
`writeFrame()` accepts frames up to 176, i.e. `payload_len <= 172`. Only
`payload_len == 173` falls in the gap. Neither producer's caller checks the
return value.

> **Corrected after review.** This said *"the top four bytes of its range"*,
> which is three values too many and mixes the two failures the paragraph above
> exists to separate. At 174–176 bytes a frame is **accepted and truncated over
> BLE**; only at 177 is it **dropped, on every transport including the serial one
> that has no MTU at all**. §7 step 5 provokes the drop with a single 173-byte
> payload, which is the whole of it.

### 4.3 What that costs a client

`PUSH_CODE_LOG_RX_DATA` is the one to care about. It carries the **raw on-air
bytes of a received LoRa packet** for a client to log or decode. A client that
reads it over BLE on a vanilla ESP32 node gets, for the largest packets, a buffer
three bytes shorter than the frame said — and the natural conclusion is a
corrupted radio packet.

That is the diagnostic inversion the issue names, arrived at from vanilla source
rather than from Offband's caplog: **a transport defect that presents as a radio
fault.** A client must not treat a short or unparseable `0x88` payload as
evidence about the radio until it has ruled out its own link.

---

## 5. Reuse: adapt the invariant, do not adopt the fork

Recorded in full in [REUSE_LEDGER](REUSE_LEDGER.md). In short:

**Take** — the rule, restated in our own terms, not the code: *a wrapper's
capacity is the minimum over exactly the sinks its write reaches*, and *a
capacity is a property of the link, floored by the buffer, never raised by it*.
And the two test suites as **specifications**: `test/test_frame_size/` and
`test/test_multiserial/test_multiserial_framesize.cpp`, MIT.

> The issue named the first of those as `test/test_ble_frame_sizing/`. That
> directory does not exist. The file is
> `test/test_frame_size/test_ble_frame_sizing.cpp`, verified in both commits'
> file lists — worth correcting because the wrong path is the kind of thing that
> gets a reuse candidate written off as missing.

**Do not take** — the fork. Offband is a MeshCore derivative carrying its own
features (caplog, observer views, block lists), it is on a different BLE stack
from the node Attadipa will actually talk to, and none of that is needed to speak
a protocol.

**Do not take** — two behaviours of the code being borrowed from, both of which
the harness reproduces:

1. `writeFrame()` **returns full success when no sink is enabled.** The fan-out
   loop body never runs, so `allSuccessful` stays `true` and the caller is told
   every byte was written. Attadipa's [ADR-0005](../adr/0005-node-protocol.md)
   link already refuses this shape; an adapter above it must not reintroduce it.
2. `maxFrameSize()` returns the buffer size when nothing is enabled. Harmless
   upstream, but it means a capacity query can never express *"there is no
   sink"* — so in our model that has to be a state, not a number.

One more, for anyone porting the predicate: upstream's comment says the capacity
predicate is *"identical to `writeFrame()`'s"*. It is not quite —
`writeFrame()` also tests the wrapper's own `_enabled`, and `maxFrameSize()` does
not. The difference is conservative in every case and therefore not a defect
upstream, but it is not the invariant the comment claims, and a port that
inverted it would be.

---

## 6. What this means for Attadipa

Consequences only. Designs go in ADRs and tasks.

1. **A frame buffer is not a transport capacity, and a client must not allocate
   one and call it the other.** This is the correction to
   [MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md) §6.3, which read
   *"176 bytes is the packet budget"*. 176 is the budget the **protocol** agrees;
   what the **link** delivers is a separate quantity that has to be asked for.
2. **A capacity query must name the sinks it answers for.** The minimum across
   the enabled set, and the same set the write reaches. This is the transferable
   half of the upstream defect and it applies to any Attadipa transport
   abstraction with a wrapper in it — including the provider registry, which is a
   wrapper by construction.
3. **The rule reaches `link/` after all, and it lands on
   [ADR-0005](../adr/0005-node-protocol.md) §8.** The node link is a different
   protocol, with its own framing, checksum and refusal of over-long frames — but
   its size constant is *not* independent of MeshCore's, and an earlier revision
   of this section said it was. `kMaxPayload = 192` is derived **from
   `MAX_FRAME_SIZE`**. The source now explicitly records that ADR-0005 §4 is
   read here as a **10-byte** envelope and that this reading remains owner-owned
   (`link/include/attadipa/link/frame_codec.h:57-71` — "Derived, not copied").
   The conclusion does not
   depend on settling that reading: 184 + 10 = 194 still exceeds 192.

   So `kMaxFrame` is 192 + 7 = **199** (`frame_codec.h:51-71` —
   "kOverheadBytes = kHeaderBytes + kTrailerBytes"). At a negotiated
   MTU of 176 a BLE notification carries 173, so a 199-byte frame does not fit
   that link. **The Attadipa node link's MTU is not established and must not be
   inferred from MeshCore's configuration.** ADR-0005 §8 already calls fragmentation mandatory and names BLE
   as the presumed transport; what this document adds is the number it must be
   sized against — *the link's, not the buffer's*, which is the same mistake one
   layer down. Registered rather than closed.

   **Still no ADR from this task, and now for a better reason than "untouched".**
   ADR-0005 §8 already mandates the mechanism; nothing here changes the decision,
   only the quantity it will be built against, and there is no fragmentation code
   yet to be wrong. When a companion client or that fragmentation is actually
   specified, *then* an ADR decides whether capacity is an interface method, a
   state, or both. **No live bug today:** `link/` has no transport, its CRC makes
   truncation detectable rather than silent, and `FrameQueue::push` refuses an
   over-long frame (`link/include/attadipa/link/frame_queue.h:82-100` — "bool push(const std::uint8_t* data, std::size_t length)").
4. **Truncation must be detectable, not inferred.** Every upstream report here
   was diagnosed from arithmetic on a byte count — 147 = 3 × 49 — because
   nothing on the wire said the frame was short. A companion client cannot fix
   the peer, so it needs its own total-length or integrity signal above the
   protocol before it may report a transfer as complete.
5. **A short frame is not evidence about the radio.** §4.3.

---

## 7. The hardware plan, which has not been run

**`NOT EXECUTED — HARDWARE REQUIRED`.** No board has been connected. This is
what would have to be measured, in order, on a stock ESP32 MeshCore node and an
Attadipa central. Nothing below may be marked `PASS` from a simulation.

1. **Record the negotiated ATT MTU from both ends** and note that they agree. The
   peripheral requests 176; what an ESP32-S3 central settles on is unknown.
2. **Send payloads at 172, 173, 174, 175 and 176 bytes** and compare the exact
   received length and content, not that a command answered. The boundary is the
   measurement; a round trip is not.
3. **Confirm the failure mode**: over-long notification truncated, refused, or
   split. Bluedroid and NimBLE are both expected to truncate per the Core
   specification, and neither has been observed here.
4. **Provoke `PUSH_CODE_LOG_RX_DATA` at its maximum** — a ≥173-byte on-air packet
   from a second node — and check the 176-byte frame arrives whole. §4.2 says
   this is the vanilla path that reaches the ceiling.
5. **Verify the 177-byte drop** by provoking `PUSH_CODE_RAW_DATA` with a
   173-byte payload. Expect *nothing* to arrive, on any transport.
6. **Compare BLE against USB and Wi-Fi/TCP control** on the same node with the
   same payloads: same node, same command, three transports, one difference.
7. **Interrupt and reconnect mid-transfer** and check what the client can tell
   about what it missed.

Steps 1–3 need one node and one central. Steps 4–5 need a second radio-capable
node; [A3](OPEN_QUESTIONS.md) is resolved with five MeshCore nodes. Step 6 needs a Wi-Fi
companion build; the node behind Home Assistant on `doctor` is the cheapest
route and needs no BLE stack at all.

---

## 8. What is still `UNKNOWN`

| Question | Why it is not answered here |
|---|---|
| What ATT MTU does an Attadipa ESP32-S3 central negotiate with a MeshCore peripheral? | no board has been connected. §7 step 1 |
| Does vanilla's **Bluedroid** ESP32 path truncate an over-long notification, as Offband's NimBLE path was measured to? | the specification says truncate and nobody has measured it on Bluedroid. §3 |
| Is `PUSH_CODE_LOG_RX_DATA` at 176 bytes reached in practice, or only in principle? | needs a second node transmitting large packets. §7 step 4 |
| Can `onTraceRecv` reach 177? | depends on whether `path_len` can exceed `MAX_PATH_SIZE`; a parser-bounds question, [#142](https://github.com/hleserg/Attadipa/issues/142)'s |
| Does any transport **other** than BLE deliver less than it accepts? | serial and TCP have their own length prefix and never reach `deliverableFrame()`. Not verified against a real USB link |
| What detects a peer truncating **our** outbound data, and what recovers? | nothing in either protocol carries a total length. §6.4 states the requirement; the mechanism is a design question for the client |
| Does Offband's fix converge back into vanilla? | vanilla `main` is `d929643` and has no `maxFrameSize` concept at all. Nothing to converge yet — **MONITOR** |
