# First physical contact with a MeshCore node

Issue [#296](https://github.com/hleserg/Attadipa/issues/296) (T-169). This is
the bench record for the first time an Attadipa device exchanged Companion
protocol frames with a real MeshCore node over the path it will use in the
field: a Waveshare ESP32-S3 Touch AMOLED acting as BLE central against Heltec
boards running vanilla MeshCore.

Two MeshCore nodes turned out to be in range and the firmware has no way to
choose between them (section 1a), so each section names the node it measured.
Handshake, Receive and the send path to `Accepted` are on the Heltec T114; the
`Confirmed` ack round trip is on a Heltec V4.3 OLED.

It is an evidence report. Every physical claim carries a label, and the
transcripts are the ones the firmware printed, not a reconstruction.

**This document does not claim the link or MeshCore is secure.** Section 8
records the authentication and cryptography state exactly as observed.

---

## 0. Provenance

| What | How it was established |
| --- | --- |
| every frame in section 4 | printed by `firmware/main/meshcore_ble.cpp` `log_frame()` on the watch, byte for byte |
| node model, firmware build, node identity | read out of `RESP_CODE_DEVICE_INFO` and `RESP_CODE_SELF_INFO` on the wire, not from the operator |
| opcode names and frame layouts | [`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md) §§2.2, 3 and the command set at §5 |
| frame size bound | [`MESHCORE_BLE_FRAME_CAPACITY.md`](MESHCORE_BLE_FRAME_CAPACITY.md) §1 and the matrix at §3 |
| drop-and-count rule for over-size frames | [`MESHCORE_PARSER_BOUNDS.md`](MESHCORE_PARSER_BOUNDS.md) §5 |
| watch screenshots | captured over the USB debug channel (ADR-0005) into `tools/ui/out/`, framebuffer as rendered |
| **BLE air traffic** | **`NOT CAPTURED`.** There is no BLE sniffer on this bench and the host is not a party to the link. The firmware transcript is the only view of the wire, and it therefore shows what the ESP32-S3 host stack accepted, not what was radiated |
| **T114 serial log** | **`NOT CAPTURED`.** The node's USB is not attached to this host |

No credentials appear in this document. The BLE passkey and the Room Server
guest password were supplied by the operator at run time and are redacted where
they occurred in a frame.

---

## 1. Bench configuration

| Role | Identity | Label |
| --- | --- | --- |
| Watch (BLE central) | Waveshare ESP32-S3 Touch AMOLED 2.06", USB serial `28:84:85:B2:18:A4`, BLE MAC `28:84:85:B2:18:A6` | `MEASURED` |
| Watch firmware | this branch, `board_id=waveshare-amoled-206`, ESP-IDF v5.5.5, NimBLE central | `MEASURED` |
| Node (BLE peripheral) | Heltec T114, BLE address `f7:f3:33:6b:9b:61` (random, `peer_addr_type=1`) | `MEASURED` |
| Node firmware | `v1.17.1-d929643`, built `14-Aug-2026` | `MEASURED` — read from `RESP_CODE_DEVICE_INFO` |
| Node self name | `Beta test comp`, then `Beta test companion` after the 2026-08-28 factory reset | `MEASURED` — read from `RESP_CODE_SELF_INFO` |
| Node public key | `5c62d9bc82e530fc…` after the reset | `MEASURED` — a factory reset regenerates it, so it does not identify the unit across the reset |
| Node board revision | `UNKNOWN` | the Companion protocol does not carry it |
| Pairing | static passkey, supplied by the operator at run time and injected by the watch (section 8) | `MEASURED` |

### 1a. There are two MeshCore nodes in range, and the transport picks either

`MEASURED`, 2026-08-28, and it governs how every run below must be read.

| | node A | node B |
| --- | --- | --- |
| `RESP_CODE_DEVICE_INFO` model | `Heltec T114` | `Heltec V4.3 OLED` |
| firmware | `v1.17.1-d929643`, `14-Aug-2026` | `v1.17.dev`, `9 Aug dt267` |
| `RESP_CODE_SELF_INFO` name | `Beta test companion` | `✂️Beta Serega` |
| public key | `5c62d9bc82e530fc…` | `044e2de8068447d3…` |
| contacts announced | 4–5 | 90 |
| negotiated ATT MTU | 247 | 176 |
| answers for the Beta Room | login yes, ack no (§6b) | login and ack (§6a) |

Both advertise the Companion service and both pair with the same operator
passkey. `advertises_meshcore()` matches the service UUID or the name substring
`MeshCore` and connects to whichever advertisement arrives first, so **the node
a run talked to is not a choice this firmware makes**. Across nine runs the
watch reached node A five times and node B four, and after 03:56 it reached node
B on eight consecutive attempts. Node selection is out of scope for T-169 and is
filed separately as
[#304](https://github.com/hleserg/Attadipa/issues/304).

Every claim below therefore names its node. The identity is read from
`RESP_CODE_SELF_INFO` and `RESP_CODE_DEVICE_INFO` in that run's own log, never
assumed from the previous run.

The earlier revision of this report recorded `MeshCore-🤘Beta Serega` as a former
advertised name of the same unit. That reading is withdrawn: the two names
belong to two different boards, seen in the same run 108 s apart.

`v1.17.1-d929643` is the same upstream revision
[`MESHCORE_BLE_FRAME_CAPACITY.md`](MESHCORE_BLE_FRAME_CAPACITY.md) pinned its
source reading to. The bench node runs the code that research was written
against; the frame-capacity numbers below are being checked against their own
source revision, not a neighbouring one.

**Anonymisation.** The node's contact list and the mesh traffic it forwards
belong to third parties. Contact records are reported as counts and sync
boundaries only, and the payloads of forwarded-packet pushes are elided. The
node's own public key and the Beta Room's public key are public by design and
are kept.

The screenshots in section 5 do show one broadcast message each, with its
sender's mesh name. They are the Receive evidence and cannot be redacted without
destroying it. These are unencrypted broadcasts on an open radio network,
readable by anyone in range, and no contact record, key or private message is
shown.

---

## 2. What changed in the firmware for this

Before this task the composition root logged

```
Link model : Absent (no transport adapter)
```

because `MeshCoreCompanion` had no transport. It now logs

```
I (1307) attadipa: Link model : Absent (MeshCore companion over BLE)
```

`MEASURED`. That line is the whole point of the task: mesh availability now
follows the live BLE link phase (ADR-0004) instead of a placeholder.

The mesh status screen also gained the last sender and the delivery state. It
had been rendering the message alone, which cannot evidence a send — the screen
now reads `Sent: confirmed` in [`t169-ui-2.png`](meshcore-t114-first-contact/t169-ui-2.png)
and that is the screenshot section 6a rests on. The node name is rendered from
`RESP_CODE_SELF_INFO` verbatim; where a node's name begins with an emoji the
watch's font has no glyph and draws two boxes, which is a font limitation and
not a parse error — the bytes `e2 9c 8c ef b8 8f` are U+2704 plus a variation
selector, checked against the frame.

---

## 3. Frame bounds, as implemented

| Rule | Source | Where it lives |
| --- | --- | --- |
| one GATT operation is one whole Companion frame; there is no length prefix and no fragmentation layer | [`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md) §2.2 | the notification callback copies one frame per callback and never concatenates |
| first byte is the opcode | ibid. §2.2 | `log_frame()` prints it; `receive()` dispatches on it |
| a frame is at most 176 bytes (`kMeshCoreFrameBytes`) | [`MESHCORE_BLE_FRAME_CAPACITY.md`](MESHCORE_BLE_FRAME_CAPACITY.md) §3 | `MeshCoreFrame::bytes` is exactly that array |
| an over-size notification is dropped and counted, never buffered | [`MESHCORE_PARSER_BOUNDS.md`](MESHCORE_PARSER_BOUNDS.md) §5 | `drop_oversize_frame()`; the frame never reaches `receive()` |
| a frame larger than the negotiated ATT payload is refused on the TX side | ibid. | `disconnect_fault("frame exceeds negotiated ATT payload", …)` |
| a disconnect resets the session fail-closed | ADR-0002 | `reset_session()` via `begin()` — see section 7 |

Negotiated ATT MTU on this bench: **247** (`MEASURED`, every run), so the
176-byte buffer, not the MTU, is the binding limit — as
[`MESHCORE_BLE_FRAME_CAPACITY.md`](MESHCORE_BLE_FRAME_CAPACITY.md) §3 says it
would be.

**The bound came within 2 bytes of being exercised in real traffic.** The
largest frame observed across every run is a 174-byte `PUSH_CODE_LOG_RX_DATA`
(`I (229929) RX op=0x88 len=174`, soak run) — `MEASURED`. Contact records are a
fixed 148; `PUSH_CODE_LOG_RX_DATA` was seen at 152, 153, 160, 164, 168 and 174
bytes, and one `op=0x11` frame at 157.
Nothing over 176 arrived: `malformed_frames` is 0 in every run and
`drop_oversize_frame()` was never called, so the drop path itself is still
`SIMULATED` — `tests/test_meshcore_companion.cpp`. That a real forwarded-packet
push reaches 174 bytes is worth stating plainly: the margin on this bound is two
bytes of somebody else's mesh traffic, not a comfortable one.

---

## 4. Evidence 1 — Handshake · `MEASURED`

Session of 2026-08-27, watch uptime in milliseconds as printed.

```
I (4767) attadipa_mesh_ble: scanning for MeshCore Companion service
I (4797) attadipa_mesh_ble: received BLE advertising report
I (4817) attadipa_mesh_ble: matched MeshCore advertisement
I (4827) NimBLE: GAP procedure initiated: connect; peer_addr_type=1
I (4827) NimBLE: f7:f3:33:6b:9b:61
I (5187) attadipa_mesh_ble: MeshCore connected; starting BLE security
I (5337) NimBLE: static passkey injected
I (5987) NimBLE: GATT procedure initiated: exchange mtu
I (7357) attadipa_mesh_ble: Companion GATT ready, MTU 247
```

Then the Companion exchange. `TX` is watch → node, `RX` is node → watch.

```
TX op=0x01 len=16                       CMD_APP_START
01 00 00 00 00 00 00 00 41 74 74 61 64 69 70 61      "....Attadipa"

RX op=0x05 len=72                       RESP_CODE_SELF_INFO
05 01 16 16 f7 c7 dc ef 09 31 54 f5 ce 58 7d 3d
15 5d 1b 41 99 00 77 7c b4 8b 5d ca d8 f6 b8 55
a0 69 5e bf 00 00 00 00 00 00 00 00 00 01 00 00
7b 41 0d 00 24 f4 00 00 07 07 42 65 74 61 20 74
65 73 74 20 63 6f 6d 70                              "Beta test comp"

TX op=0x16 len=2                        CMD_DEVICE_QUERY, app protocol 3
16 03

RX op=0x0D len=82                       RESP_CODE_DEVICE_INFO
0d 0d af 28 47 39 08 00 31 34 2d 41 75 67 2d 32      "14-Aug-2"
30 32 36 00 48 65 6c 74 65 63 20 54 31 31 34 00      "026.Heltec T114."
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 76 31 2e 31      "v1.1"
37 2e 31 2d 64 39 32 39 36 34 33 00 00 00 00 00      "7.1-d929643"
00 01

TX op=0x04 len=1                        CMD_GET_CONTACTS
04

RX op=0x02 len=5                        RESP_CODE_CONTACTS_START, count 0
02 00 00 00 00

RX op=0x04 len=5                        RESP_CODE_END_OF_CONTACTS
04 00 00 00 00

TX op=0x0A len=1                        CMD_SYNC_NEXT_MESSAGE
0a

RX op=0x0A len=1                        (offline queue empty)
0a
```

What this establishes:

- The initialisation order required by
  [`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md) §3.2 is
  respected on the wire: `CMD_APP_START` first, `CMD_DEVICE_QUERY` only after
  `RESP_CODE_SELF_INFO`, `CMD_GET_CONTACTS` only after
  `RESP_CODE_DEVICE_INFO`, one command outstanding at a time.
- `RESP_CODE_DEVICE_INFO` is **82 bytes**, exactly the length that document
  records at §3. The layout matched: build date, model string, firmware version
  string at their documented offsets.
- The node reported **0 contacts** in this run. That is genuine, not an
  anonymisation artefact. It did not stay that way: on 2026-08-28 the same node
  reported **37**, and that change is what exposed the defect in section 7b.
- Handshake to `Ready` took **2.6 s** from advertisement match, of which 1.4 s
  was pairing and MTU exchange.

Full raw capture: not committed. It contains third-party mesh traffic; the
frames above are the complete Companion exchange with nothing elided.

---

## 5. Evidence 2 — Receive · `MEASURED`

Two independent captures, both spontaneous — the messages are real traffic on
the operator's mesh, not injected for the test.

| Screenshot | Watch shows | Peers | SNR |
| --- | --- | --- | --- |
| [`t169-mesh-status.png`](meshcore-t114-first-contact/t169-mesh-status.png) | `CONNECTED`, node `Beta test comp`, last message `КЕРЫ4: @[Vault13] У меня тоже)` | 6 | 12.00 dB |
| [`t169-ctl-60.png`](meshcore-t114-first-contact/t169-ctl-60.png) | `CONNECTED`, node `Beta test comp`, last message `Spawn: Да` | 8 | 12.75 dB |

The sender name is rendered from the frame, not from a contact record — the
node has no contacts. Both messages arrived as `PUSH_CODE_LOG_RX_DATA` (`0x88`)
pushes, [`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md) §5.

Alongside them the watch received a continuous stream of `0x88` and
`PUSH_CODE_ADVERT` (`0x80`) pushes carrying advertisements from real regional
nodes. Payloads elided; the observation is that the watch stayed synchronised
with a live mesh for the whole session without a malformed-frame drop
(`malformed_frames() == 0` in every run).

**Observed limitation, not fixed here:** a tofu glyph renders in place of one
character of one sender's name. The font has no coverage for that codepoint.
This is a font-coverage issue in the UI layer, unrelated to the transport, and
is out of scope for T-169.

---

## 6. Evidence 3 — Send · `MEASURED` through `Confirmed`

The criterion was a message from the watch reaching a real MeshCore node and
reporting `Confirmed` through the ack path. **It does.** Read the node identity
in this section carefully: two MeshCore Companion nodes are in BLE range of this
bench and the transport connects to whichever advertises first (§1a), so which
node carried a given run is a fact to be read off `RESP_CODE_SELF_INFO`, never
assumed.

Target in every attempt: the "Beta Room" Room Server, public key
`ba4c1ca4d6463a23e99b5440b6206723116bf78713a1b45b266f837bcde3149e`. A Room
Server requires `CMD_SEND_LOGIN` before a message, so the watch sends that first
and continues on `PUSH_CODE_LOGIN_SUCCESS`.

### 6a. The full path, `Confirmed` · `MEASURED`

Node `044e2de8…`, the one MeshCore Companion node of the two that reports the
room as reachable. Run `ui2`, 2026-08-28, on the build at
[`51a210a`](https://github.com/hleserg/Attadipa/commit/51a210a).

```
I (24927) TX op=0x1A len=42     CMD_SEND_LOGIN
1a ba 4c 1c a4 d6 46 3a 23 e9 9b 54 40 b6 20 67
23 11 6b f7 87 13 a1 b4 5b 26 6f 83 7b cd e3 14
9e ** ** ** ** ** ** ** ** **                     ← guest password, REDACTED
I (25287) RX op=0x06 len=10     RESP_CODE_SENT   06 00 ba 4c 1c a4 72 0c 00 00
I (27177) RX op=0x85 len=14     PUSH_CODE_LOGIN_SUCCESS
85 00 ba 4c 1c a4 d6 46 77 c6 90 6a 02 01
I (27177) TX op=0x02 len=29     CMD_SEND_TXT_MSG
02 00 00 79 c6 90 6a ba 4c 1c a4 d6 46 "T-169 acceptance"
I (27757) RX op=0x06 len=10     RESP_CODE_SENT   06 00 38 66 6c b8 66 09 00 00
I (28477) RX op=0x88 len=10     PUSH_CODE_LOG_RX_DATA  88 31 b5 2a 40 13 38 66 6c b8
I (28477) RX op=0x82 len=9      PUSH_CODE_SEND_CONFIRMED  82 38 66 6c b8 1b 03 00 00
```

**The `**` above were applied by hand, after the capture.** The firmware on
`51a210a` printed the guest password's real bytes to the USB console, and this
transcript is the reason #316 exists. It is left as captured — a capture is the
run — but nothing after this build reproduces it: `log_frame()` now asks
`attadipa::link::meshcore_loggable_prefix()` how much of each frame may be
printed, and `CMD_SEND_LOGIN` stops after the public room key. The same run on
the current tree prints the header and the first 33 bytes:

```
I (24927) TX op=0x1A len=42 (9 bytes redacted: credential)
1a ba 4c 1c a4 d6 46 3a 23 e9 9b 54 40 b6 20 67
23 11 6b f7 87 13 a1 b4 5b 26 6f 83 7b cd e3 14
9e
```

That line is `SIMULATED`: it is what the host test asserts the redaction leaves
(`tests/test_meshcore_companion.cpp`,
`test_a_room_password_never_reaches_the_transcript`), not a second bench run.
**NOT EXECUTED — HARDWARE REQUIRED.**

Opcodes and error codes read from
[`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md) §9: `6`
`SENT`, `0x82` `SEND_CONFIRMED`, `0x85` `LOGIN_SUCCESS`, `0x88` `LOG_RX_DATA`.

The chain closes on itself, which is why it is worth reading rather than
summarising. `RESP_CODE_SENT` for the text carries the expected ack
`38 66 6c b8` and an estimated round trip of `0x0966` = 2406 ms. The same four
bytes then appear as the tail of a raw LoRa packet the node logged off the air,
and again as the body of `PUSH_CODE_SEND_CONFIRMED`. The provider compares them
(`meshcore_companion.cpp` `kPushSendConfirmed`) and only then sets
`MeshDelivery::Confirmed`. Measured round trip **720 ms** (27757 → 28477),
inside the node's own estimate.

The watch's mesh screen reads `Sent: confirmed` —
[`t169-ui-2.png`](meshcore-t114-first-contact/t169-ui-2.png).

**Independently confirmed by a human reading the room.** The operator's MeshCore
client shows three `T-169 acceptance` messages in Beta Room, all from
`<044e2de8…>`, at the times these runs sent them —
[`beta-room-received.jpg`](meshcore-t114-first-contact/beta-room-received.jpg).
The node's ack path establishes that some destination acknowledged the packet;
this establishes that the room server stored it and a person read it.

### 6b. The same path on the T114, `Accepted` · `MEASURED`; `Confirmed` `NOT OBSERVED`

Node `5c62d9bc…`, name `Beta test companion` — the Heltec T114 this report is
named for. Run `send7`, same evening, on the build carrying the §6c fix.

```
I (26927) TX op=0x1A len=42     CMD_SEND_LOGIN
I (27587) RX op=0x06 len=10     RESP_CODE_SENT
I (28517) RX op=0x85 len=14     PUSH_CODE_LOGIN_SUCCESS
I (28517) TX op=0x02 len=29     CMD_SEND_TXT_MSG
I (28667) RX op=0x06 len=10     RESP_CODE_SENT  → MeshDelivery::Accepted
```

Reproduced on run `t114s1` after the second node was switched off, so it is not
an artefact of node contention: `LOGIN_SUCCESS` at 31107, `CMD_SEND_TXT_MSG` at
31117, `RESP_CODE_SENT` at 31227. The watch reads `Sent: accepted`,
`Node: Beta test companion`, 5 peers, MTU 247 —
[`t169-t114-sent.png`](meshcore-t114-first-contact/t169-t114-sent.png).

Everything up to `Accepted` is identical and `MEASURED`. No
`PUSH_CODE_SEND_CONFIRMED` arrived in the following **120 s**, against a node
estimate of ~2 s.

**And the message is absent from the room.** The operator's screenshot above
carries three messages from node B and none from `<5c62d9bc…>`. So this is not
an ack that went missing on the way back: nothing the T114 sent reached the Beta
Room. `MEASURED` on the receiving end, by a human, which is a stronger negative
than the watch could produce on its own. The room server did ack the *login*
from the T114, so the two reached each other at least once in at least one
direction. Whether the T114's radio placement, its route to the room server, or
something else accounts for the missing message is `UNKNOWN` from this bench and
is not attributed to either firmware.

An earlier run (`send3`, before the operator added the room to the T114's
contact list) is the control for the cause: the node answered `CMD_SEND_LOGIN`
with `RX op=0x01 len=2` — `01 02`, `ERR` / `NOT_FOUND` — 240 ms later. With the
contact present the same command is answered by the room. The contact record is
visible in the sync as `03 ba 4c 1c a4 …` with contact type `3`.

### 6c. A room login that landed mid-sync dropped its message · `MEASURED`, fixed

Found by this acceptance run, in code this task wrote.

`send_room()` deliberately does not wait for the contact sync — there is a test
named for it. Its continuation went through `enqueue_private()`, which required
`Availability::Ready`, and `update_availability()` grants `Ready` only when
`contacts_complete_` is also set. So a room login that succeeded while a burst
was still arriving was answered by silently dropping the message.

```
I (130258) TX op=0x1A len=42      CMD_SEND_LOGIN
I (130668) RX op=0x06 len=10      RESP_CODE_SENT
I (131698) RX op=0x85 len=14      PUSH_CODE_LOGIN_SUCCESS
I (134228) RX op=0x04 len=5       RESP_CODE_END_OF_CONTACTS
                                  -- no CMD_SEND_TXT_MSG, ever
```

`MEASURED`, run `send5`. The two halves each had a passing test and the
combination had none. `enqueue_private()` now requires exactly what
`send_room()` requires — the link up, `SELF_INFO` and `DEVICE_INFO` seen — and
not a completed contact sync, which has nothing to do with whether a text frame
can be written. `test_room_login_success_during_a_contact_burst_still_sends()`
fails without the change: verified by reverting the source alone and re-running.

---

## 7. Evidence 4 — Recovery

Three distinct things live under this heading. They are separated because they
have different labels, and only two of them were run.

### 7a. Recovery from losing the peer · `MEASURED`; power-cycle `NOT EXECUTED`

Two different things live under "recovery" and only one of them was executed.

**Losing the peer mid-session and coming back · `MEASURED`, three times in one
boot.** Run `recovery`, 2026-08-28, on the T114. Every drop reported reason
`520` — a supervision timeout, i.e. the node stopped answering — and every one
was followed by a complete new session, not merely a reconnect:

| link lost | advert matched | connected | GATT ready, MTU 247 | `CMD_APP_START` … `END_OF_CONTACTS` |
| --- | --- | --- | --- | --- |
| 175402 | 177392 | 177862 | 183802 | 183802 … 185582 |
| 227332 | 229322 | 230262 | 236692 | 236692 … 238202 |
| 303862 | 305762 | 306262 | 311972 | 311972 … 313712 |

Four full Companion sessions in one boot, three of them recovered without
intervention, 8–9 s from loss to a synced session. The scan restarts in the same
millisecond as the disconnect.
[`t169-rec-post.png`](meshcore-t114-first-contact/t169-rec-post.png) is the watch
after the second recovery: `CONNECTED`, `Beta test companion`, 6 peers, MTU 247.

This is the behaviour 7b and 7c were fixed to produce, now observed against a
peer that genuinely went away rather than a simulated fault.

**Power-cycle recovery · `NOT EXECUTED — HARDWARE REQUIRED`.** Cutting power to
the T114 mid-session is a different stimulus and was not performed. The node is
on battery inside its case on a short charging lead; the operator was unwilling
to open it, and confirmed afterwards that no reset was pressed during the
`recovery` run — so none of the three drops above may be attributed to an
operator action. It is not claimed and not inferred from the drops above.

It used to say this was an open acceptance item for
[#296](https://github.com/hleserg/Attadipa/issues/296); #296 and its follow-up
[#307](https://github.com/hleserg/Attadipa/issues/307) are both closed, so the
sentence pointed at nothing. **Nothing is waiting on this test.** The stimulus is
still unperformed and the classification above is still correct — if a future
plan wants it, it needs the node out of its case and an owner willing to open
it, which is why #307 was closed as not-now rather than done.

**Why the link was fragile that evening.** Three of five connection attempts to
the T114 collapsed with `520` before discovery finished, a rate not seen earlier
in the session. `OPERATOR REPORTED`: the node had been moved further away and
lower down, limited by the length of its charging lead. That is a plausible
cause and is not measured here — no RSSI was recorded on the watch side and
`SNR` reads `—` on every screenshot.

### 7b. A contact-list burst killed mesh for the whole boot · `MEASURED`, fixed

This is the most consequential finding of the session, and it only appeared
because the node's contact list grew over the evening as advertisements
accumulated: empty on 2026-08-27, then 6, 19, 22, 33 and 37 across the
2026-08-28 runs. It is size-dependent, and that is `MEASURED`, not inferred: on
the *same* binary a 22-record burst (the 672 s soak) came through with zero
queue failures, and the 33-record burst 5 minutes later did not.

`CMD_GET_CONTACTS` is answered with an unpaced burst of 148-byte
`RESP_CODE_CONTACT` records, ten milliseconds apart. The transport's FreeRTOS
event queue was 16 deep, the burst outran the worker task, and:

```
I (7719) attadipa_mesh_ble: TX op=0x04 len=1          CMD_GET_CONTACTS
I (7959) attadipa_mesh_ble: RX op=0x02 len=5          02 21 00 00 00 -- 33 contacts follow
I (7959) attadipa_mesh_ble: RX op=0x03 len=148        contact record
I (7969) attadipa_mesh_ble: RX op=0x03 len=148        contact record
E (7999) attadipa_mesh_ble: queue notification failed: 6
E (8009) attadipa_mesh_ble: queue notification failed: 6
I (8189) attadipa_mesh_ble: RX op=0x03 len=148        20th and last record
W (8289) attadipa_mesh_ble: MeshCore disconnected: 534
```

`6` is `errQUEUE_FULL`. The node announced 33 records. Twenty-two notifications
reached the callback between 7959 ms and the teardown at 8289 ms: twenty were
queued and logged, two were refused by a queue sixteen deep. The remaining
eleven have no evidence of arriving at all — the link went down first, and
whether the node had sent them is `UNKNOWN` from this side.

Two dropped frames took the link down at 8.3 s, and **nothing rescanned for the
remaining four minutes of that boot** — no advertisement match, no GAP
procedure, nothing. The room-send issued 30 s later was refused locally:

```
W (39929) attadipa_mesh_ble: Room Server message rejected by provider
```

The queue-full path called `disconnect_fault()`, which clears
`reconnect_allowed` — by design, so a genuinely broken subsystem is not retried
forever. A transient burst is not a broken subsystem, and the code one branch
above already said so, for the over-size case: *"tearing the link down here
would let one malformed notification end the session — and `disconnect_fault()`
also clears `reconnect_allowed`, so it would end mesh for the whole boot."* The
same reasoning had simply not been applied to a full queue.

**Fix, two parts:** the queue is 48 deep rather than 16, sized for a contact
burst; and a frame that cannot be queued is logged and dropped, never faulted.
The Companion protocol tolerates that — a contact record is re-sent by the next
`CMD_GET_CONTACTS` and the sync boundary still arrives; a lost push is one
message.

**MEASURED before and after, same node, same 37-contact list, 40 minutes
apart:**

| | before (`send2`, 00:29) | after (`send3`, 00:35) |
| --- | --- | --- |
| contacts announced by the node | 33 | 37 |
| contact records delivered | 20 | 37 |
| `queue notification failed` | 2 | 0 |
| link at 8.3 s | terminated | up |
| Companion frames after 8.3 s | none, for the rest of the boot | continuous for 170 s |
| watch display at 245 s / 170 s | `Attached`, no node, 0 peers, MTU 0 | `CONNECTED`, `Beta test comp`, 37 peers |

The list grew by four contacts between the two runs, so this is not a controlled
A/B on identical input; the burst got *larger* and still succeeded.
[`t169-send2-245.png`](meshcore-t114-first-contact/t169-send2-245.png) is the before, 245 s into a boot whose
link died at 8.3 s: `MESH / Attached / Node — / Peers 0 / MTU 0`. It reads
`Attached` rather than `Faulted` because `provider.begin()` had already re-armed
the link model (7c); it is `reconnect_allowed`, cleared by `disconnect_fault()`,
that stopped the scan from ever restarting.

[`t169-send3-170.png`](meshcore-t114-first-contact/t169-send3-170.png) is the after: `MESH / CONNECTED / Node Beta
test comp / SNR 12.75 dB / Peers 37`.

### 7c. A faulted session could not survive a reconnect · fix `SIMULATED`

Observed three times on 2026-08-27, before 7b was understood. Roughly 30 s after
a Companion write the link dropped:

```
W (75008) attadipa_mesh_ble: MeshCore disconnected: 534
I (77648) attadipa_mesh_ble: Companion GATT ready, MTU 247
```

`534` is `0x216`: HCI reason `0x16`, *connection terminated by local host*. The
transport rescanned, reconnected, re-paired, re-subscribed and reported MTU 247 —
**and then sent nothing, ever again.** `MEASURED`, control run, on a build that
predates the fix: the link dropped at 111168, the scan restarted in the same
millisecond, the connection came back at 111388, and GATT was ready with MTU 247
at 113628 — and the last `TX` line in the whole log is at 80998. `0x88` pushes
kept arriving and the display showed no node and zero peers, which is
`receive()` refusing frames while the link is not ready.
[`t169-ctl-130.png`](meshcore-t114-first-contact/t169-ctl-130.png) shows `MESH / Faulted / Node — / Peers 0 / MTU
247`; `t169-ctl-60.png` from earlier in that same boot shows `CONNECTED / Beta
test comp / Peers 8`.

**Root cause**, from `link/src/link_state.cpp`: the provider was already
`Faulted` when the disconnect arrived. From `Faulted`, `LinkEvent::PeerGone` is
`Ignored`, so `disconnected()` was a no-op; `PeerArriving` requires `Attached`
and `PeerEstablished` requires `Connecting` or `Attached`, so both were
`Ignored` too, and `connected()` returned early without enqueueing
`CMD_APP_START`. `begin()` is the only call that invokes `link_.reset()`, and
nothing on the reconnect path called it. The reconnect machinery worked
perfectly; the session model was dead behind it.

**The 30 seconds is a diagnosis, not a coincidence.** Companion writes are
write-with-response, and ATT mandates a 30 s transaction timeout after which the
host must terminate the link. The measured intervals — 30090 ms in the send run,
30170 ms in the control run — are that timeout to within 0.3%. In the handshake
run the `0x0A` at 8287 got its response at 8497 and that session ran 180 s
without a disconnect. So: the node stopped acknowledging a write at the ATT
layer, the 30 s timeout fired, the host terminated, the write callback faulted
the provider, and the reconnect that followed could never re-establish the
session.

**Fix, two parts:** the disconnect path calls `provider.begin()` whenever a
reconnect is actually going to be attempted — `reconnect_allowed` is the
existing interlock and `start_scan()` already honours it, so a hard fault is
still reported as failed. And a failed write no longer faults the provider by
itself: it logs the error code, which was silent before, and terminates the
link, routing every write failure through that single recovery path.

**Label discipline.** The fix is `SIMULATED`, not `MEASURED`. Host coverage is
`test_a_fault_survives_reconnect_until_begin_restarts_the_session` in
`tests/test_meshcore_companion.cpp`, which pins the provider contract the
transport now depends on — a fault survives the whole reconnect sequence, and
only `begin()` re-arms the handshake. It proves the contract, not the wiring.
The wiring could not be bench-verified, because the ATT stall stopped recurring
on its own: the 672 s soak, which ran on a build carrying this fix and not 7b,
completed without a single disconnect, and so did the 170 s run after 7b. **A defect that stops reproducing is not a defect that is
proven fixed**, and it is recorded that way here.

Whether the ATT stall was ever independent of 7b is `UNKNOWN`. The two are
plausibly the same failure — a worker starved by a burst also stops draining
write results — but the 2026-08-27 runs show no `queue notification failed`
line, so on the evidence they are distinct.

### 7d. Stability, incidentally · `MEASURED`

Two clean sessions, on two different builds — and the distinction matters,
because only the second one carries both fixes.

| run | build | duration | contacts | disconnects | dropped | malformed |
| --- | --- | --- | --- | --- | --- | --- |
| `soak`, 00:12–00:24 | 7c fix only | 672 s | 22 of 22 | 0 | 0 | 0 |
| `send3`, 00:32–00:35 | 7c + 7b | 170 s | 37 of 37 | 0 | 0 | 0 |

The soak binary predates the 7b fix by construction: the soak ended at 00:23:56
and the queue fix was not built until 00:31:08. Its 22-record burst simply never
reached the queue bound — which is the point of 7b, and the reason the soak is
not evidence for it. Only the 170 s `send3` run exercises both fixes together.

Both ran with a live message feed throughout.
[`t169-soak-240.png`](meshcore-t114-first-contact/t169-soak-240.png) is mid-soak: `CONNECTED`, 22 peers, SNR
12.50 dB. Peer count rose from 6 to 37 across the evening's sessions as
advertisements accumulated.

## 8. Authentication and cryptography, as observed

**Nothing in this section says the link or MeshCore is secure.** It records
state.

| Layer | Observed state | Label |
| --- | --- | --- |
| BLE pairing | static passkey, injected by the watch; the node accepted it and the link was encrypted by the BLE link layer | `MEASURED` |
| BLE bonding | `UNKNOWN` — not exercised; every session in this report re-paired from scratch. Bonds do persist (`CONFIG_BT_NIMBLE_NVS_PERSIST=y`), and what happens when the *node's* half is gone is #325 — see section 8.1 |  |
| Passkey handling | the 6-digit passkey is **not** in the firmware image. It is supplied at runtime by the operator over the USB debug channel, reaches NimBLE through `configure_meshcore_ble()` -> `ble_sm_configure_static_passkey()` ([`meshcore_ble.cpp:1256`](../../firmware/main/meshcore_ble.cpp) "bool configure_meshcore_ble", [`meshcore_ble.cpp:1058`](../../firmware/main/meshcore_ble.cpp) "ble_sm_configure_static_passkey(event.passkey"), lives only in RAM and is gone on reset. `CONFIG_BT_NIMBLE_STATIC_PASSKEY=y` enables the mechanism, not a value | `MEASURED` |
| Passkey strength | 6 decimal digits, static for the session, not per-device and not rotated. Whoever holds it can pair | structural, from the mechanism |
| Companion frame integrity | none at the Companion layer. Frames carry no MAC, no sequence number and no replay counter. Their only protection is whatever the BLE link layer provides | `MEASURED` — every frame in section 4 is plaintext on the wire |
| Mesh payload encryption | the `0x88` push payloads are ciphertext the watch does not decrypt; the node does the mesh crypto | `MEASURED` |
| Room Server login | the guest password is sent **in the clear inside the Companion frame** (section 6) — the wire cannot change, the protocol requires it there. It is protected only by BLE link encryption between watch and node, and by whatever MeshCore does beyond the node. It is **no longer printed to the USB console**: the frame transcript is truncated at the public room key (#316, section 12) | `MEASURED` on the wire; the transcript truncation is `SIMULATED` |
| Trust model | the watch trusts the node completely. A node — or anything that can present itself as one and satisfy the passkey — chooses every sender name and message body the watch displays | structural, from the protocol |

### 8.1 When the node's keys are gone · `NOT EXECUTED — HARDWARE REQUIRED`

The node in this report was factory-reset once, and it came back with a
different self name and public key. A node that is reset or reflashed after a
bond exists loses its half of that bond.

**How that reaches the watch depends on which side asks.** NimBLE raises
`BLE_GAP_EVENT_REPEAT_PAIRING` from `ble_sm_chk_repeat_pairing()`
([`ble_sm.c:990`](https://github.com/apache/mynewt-nimble/blob/master/nimble/host/src/ble_sm.c)),
whose only call site is `ble_sm_pair_req_rx()` — the handler for an *inbound*
Pairing Request (`ble_sm.c:1956`, call at `:2079`; read in the ESP-IDF v5.5.5
tree at `components/bt/host/nimble/nimble/nimble/host/src/ble_sm.c`). The watch
is the central: it originates pairing and receives a Pairing *Response*, and a
peripheral cannot make it receive a request — a peripheral sends a Security
Request, which makes the central originate. So on this device that event does
not fire.

What the watch sees instead is its own encryption attempt refused. It connects
with a bond in the store, calls `ble_gap_security_initiate()`, the node has no
key for the LTK, and the failure arrives as `BLE_GAP_EVENT_ENC_CHANGE` with
status `PIN or Key Missing` (HCI `0x06`). That is the trigger this firmware
records on. The repeat-pairing handler is kept — the callback owes NimBLE an
answer, and a role that does receive a Pairing Request must not fall through to
the default — but it is not the path the watch takes.

The watch does not delete the bond on either path. Returning
`BLE_GAP_REPEAT_PAIRING_RETRY` would mean deleting it first, before Phase 2
authentication — [Apache NimBLE
issue #2206](https://github.com/apache/mynewt-nimble/issues/2206), open as of
2026-08-28 — so any peer in radio range could evict the bond with one Pairing
Request. Instead the watch records which peer conflicted, faults the transport
and stops reconnecting, and says so on the console:

```
MeshCore bond is stale (the node has no key for it): type=0
addr=xx:xx:xx:AA:BB:CC. The bond is kept. Mesh stays down until the owner
forgets it (mesh-forget-bond).
```

Recovery is an operator action:

```
tools/watch_control.py mesh-forget-bond
```

It deletes only the bond that the conflict recorded — it takes no peer to name
— and arms one fresh pairing. With no conflict recorded it refuses, and says
so in those words rather than as a malformed-request error, so the ordinary
state is distinguishable from a fault; it is not a way to walk the bond store.
If the store refuses the deletion the record goes back and nothing is re-armed:
the operator is told to run the command again, because the reply that said the
bond was gone had already been sent when the request was accepted.
`mesh-configure` is still needed afterwards if the watch has been reset since,
because the passkey lives only in RAM.

Everything in this subsection is `SIMULATED` on a host
([`../../tests/test_session_owner.cpp`](../../tests/test_session_owner.cpp),
[`../../tests/test_debug.cpp`](../../tests/test_debug.cpp)). No bond has been
made stale on this bench and no recovery has run on hardware: the row above
stays `UNKNOWN` until one does.

See [`docs/upstream/meshcore-1.17-review.md`](../upstream/meshcore-1.17-review.md)
for the upstream review of what MeshCore does and does not guarantee at this
revision. Nothing observed on this bench contradicts it, and nothing observed
here upgrades any of its findings.

The practical consequence for Attadipa: **treat every mesh-sourced string as
hostile input.** That is why the parser bounds
([`MESHCORE_PARSER_BOUNDS.md`](MESHCORE_PARSER_BOUNDS.md)) exist and why a
malformed frame is dropped and counted rather than allowed to reach the
recovery path.

---

## 9. Power · `ESTIMATED`

A full power budget is out of scope for T-169 and was not measured — no
inline current measurement was taken, so every number here is an estimate, not
a bench figure.

What is `MEASURED` is the duty cycle, which is the input a budget would need.
In the steady 672 s soak the watch's BLE central sent 4 frames in the first
900 ms and then one `CMD_SYNC_NEXT_MESSAGE` roughly every 160 s. Everything
else was receive. The connection interval NimBLE negotiated was
`itvl_min=24 itvl_max=40` (30–50 ms) with `latency=0` and a 2.56 s supervision
timeout — a connection kept continuously awake at a 30–50 ms interval with no
slave latency.

`ESTIMATED`: a continuously connected BLE central at that interval is the
dominant term over an almost entirely idle Companion protocol, and the
protocol's own traffic is negligible against it. Raising slave latency is the
obvious lever, and it is a separate task with its own measurement.
`UNKNOWN`: the actual figure, in mA and in hours.

---

## 10. What this closes in the earlier defect report

[`T114_BLE_COMPANION_DEFECTS_2026-08-26.md`](T114_BLE_COMPANION_DEFECTS_2026-08-26.md)
set a filing threshold before any upstream MeshCore issue could be raised, and
left four items `UNKNOWN`. This session ran that clean reproduction. **It
succeeded**, so the threshold is not met and no upstream issue should be filed
on that evidence.

| That report said | Now |
| --- | --- |
| node model `OPERATOR REPORTED`, revision and build `UNKNOWN` | model and build `MEASURED` off the wire: Heltec T114, `v1.17.1-d929643`, built `14-Aug-2026`. Board revision still `UNKNOWN` |
| current BLE PIN configuration `UNKNOWN` | `MEASURED`: the operator-supplied static passkey pairs successfully and repeatably |
| pairing correlated with the node becoming unavailable; causation `UNKNOWN` | not reproduced. Pairing succeeded in every run of this session. The earlier absence of advertisements has a simpler explanation observed directly this session: the node accepts one BLE connection, and while the operator's phone held it, the node did not advertise and no scan could match it |
| status `261` (`0x105`) on the CCCD write, mapping `UNKNOWN` | not reproduced; the CCCD write succeeded in every run. The mapping remains `UNKNOWN` |
| `NOT CAPTURED`: T114 serial log | still `NOT CAPTURED` |

The one open node-side question this session *adds* is the ATT write stall in
section 7c, and its cause is `UNKNOWN`. The two Attadipa defects this session
found — sections 7b and 7c — are both fixed on this branch; neither is an
upstream allegation.

---

## 11. What this does not establish

- **`Confirmed` was measured through one node, not through the T114.** Section
  6. The end-to-end ack path is `MEASURED` on a `Heltec V4.3 OLED` running
  `v1.17.dev`; on the `Heltec T114` running `v1.17.1-d929643` the same sequence
  reaches `Accepted` and its ack was `NOT OBSERVED` in 120 s. Why the two differ
  is `UNKNOWN` and is attributed to neither firmware.
- **Nobody has confirmed the message appeared in the room.** The evidence is the
  node's own ack path, which establishes that a destination acknowledged the
  packet — not that a human read it in the Beta Room. That check is
  `NOT EXECUTED — OPERATOR REQUIRED`.
- **Which physical board is which is read per run, not assumed.** Section 1a.
- **Power-cycle recovery is not tested.** Section 7a.
- **The reconnect fix in 7c is not bench-verified.** Section 7c. It is
  host-tested against the provider contract, and the failure it repairs stopped
  reproducing on its own, before 7b was fixed.
- **No BLE air capture exists.** Everything is the ESP32-S3 host stack's view.
- **Two nodes, two boards, two firmware revisions, and no way to choose.**
  Section 1a and [#304](https://github.com/hleserg/Attadipa/issues/304). Nothing
  here generalises to a third MeshCore build or peripheral, and no run below
  chose the node it talked to.
- **The 176-byte frame bound was approached but never crossed in traffic**
  (section 3): the largest real frame was 174 bytes, and no over-size frame was
  ever observed, so the drop-and-count path is `SIMULATED` only. Whether a
  MeshCore push can exceed 176 bytes on this revision is `UNKNOWN` from this
  bench.
- **Nothing here is a security claim.** Section 8.

## 12. Running this again without publishing a credential

Two credentials appear in this procedure: the node's six-digit BLE passkey and
the Room Server's guest password. Neither is a command-line argument any more
(#316) — an argument is readable by every other process on the host while the
command runs, and the shell writes it to history besides.

**At a terminal**, both are prompted for and neither is echoed:

```
tools/watch_control.py mesh-configure
BLE passkey:
tools/watch_control.py mesh-room-send --room <64-hex> --text "..."
Room Server password:
```

**Unattended**, both are one line on stdin, which keeps them out of `ps` and out
of history:

```
printf '%s\n' "$BLE_PASSKEY"    | tools/watch_control.py mesh-configure
printf '%s\n' "$ROOM_PASSWORD"  | tools/watch_control.py mesh-room-send \
    --room <64-hex> --text "..."
```

`mesh-configure --unpaired-probe` is the old `--passkey 0`: a diagnostic that
carries no secret, so it stays a flag and stays scriptable. The host refuses an
empty password, one longer than the protocol's fifteen bytes, one carrying a
NUL, and a passkey that is not six digits — refusing here rather than sending a
half credential to the watch. It also refuses `000000`, which *is* six digits:
the firmware reads that value as **do not pair** (`secure_pairing = passkey
!= 0`) and skips `ble_gap_security_initiate()`, so typing it would bring the
link up unencrypted while the host reported success — and the Room Server
password below would then cross the air in the clear. The unpaired probe is
reachable only through its own flag.

**Publishing a capture.** The transcript is truncated at the source, so a raw
capture taken on the current tree does not contain the password and does not
need editing before it is committed. Two things still hold:

- Check any capture taken on an **older build** — `51a210a` and anything before
  the #316 fix printed the password's real bytes. Section 6's transcript is one
  of those, and its `**` were applied by hand.
- The truncation covers `CMD_SEND_LOGIN` on the Companion link. It says nothing
  about a credential typed into a screen recording, a scrollback buffer opened
  before the fix, or a `sdkconfig` — and `CONFIG_BT_NIMBLE_STATIC_PASSKEY=y`
  enables the mechanism, never a value (section 8).

**Two residuals this does not close**, stated rather than implied. The password
crosses the USB debug channel from host to watch and is read in place from the
receive buffer the transport owns (`debug/src/bridge.cpp`, `handle` takes a
`const` payload), so that buffer holds it until the transport overwrites it. And
a Python `str` on the host cannot be zeroized. Both are bounded and neither
reaches a console or a file; closing either means changing who owns those
buffers, which #316 does not ask for.
