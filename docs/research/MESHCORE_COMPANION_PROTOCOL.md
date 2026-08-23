# The vanilla MeshCore companion protocol

**Revision pinned:** `d92964352441e53b93e8667b802e04f6e072b39e` — upstream
`meshcore-dev/MeshCore`, tagged `companion-v1.17.1` / `repeater-v1.17.1` /
`room-server-v1.17.1`, dated 2026-08-14. Licence MIT; the ledger record is
[REUSE_LEDGER](REUSE_LEDGER.md).

**Every file and line number in this document is meaningless at any other
revision.** MeshCore's companion protocol is a flat `if/else if` chain over
`#define`s in one `.cpp` file, not a versioned schema, and the numbering has
already moved once (see §7). Re-verify before trusting any of it against a newer
tag.

This answers T-072. §1 of
[COMPANION_AND_POSITION_SOURCES](COMPANION_AND_POSITION_SOURCES.md) carries the
one-line summaries; this file is the detail a client cannot be written without.

---

## 0. Provenance — what was verified, and by whom

This is a research document and the project's rule about not letting an estimate
read as a measurement applies to it as much as to a power number.

**How it was produced.** Three research agents read the pinned clone at
`/root/upstream/MeshCore` and returned answers with quoted evidence, each with a
file and a line range. The adversarial verification stage of that run **never
executed** — every verify agent and the write agent died on upstream API errors
(529 Overloaded, and two mid-response server errors). Nothing here has been
through an independent second reading by another agent.

**What the author verified personally**, by opening the pinned clone and reading
the cited lines:

| Claim | Verified at |
|---|---|
| `MAX_FRAME_SIZE 176`, unguarded `#define`, therefore not overridable | `src/helpers/BaseSerialInterface.h:5` |
| the 3-byte byte-stream header, `'>'`/`'<'` plus little-endian `uint16` length | `src/helpers/ArduinoSerialInterface.cpp:24-37` |
| `FIRMWARE_VER_CODE 13`, `FIRMWARE_VERSION "v1.17.1"`, build date `14 Aug 2026` | `examples/companion_radio/MyMesh.h:8-16` |
| `LPP_GPS 136`, 3-byte fields, lat/lon ×10 000, altitude ×100 | `src/helpers/sensors/LPPDataHelpers.h:29,56-57,105-110` |
| `ADV_LATLON_MASK 0x10`, advert lat/lon `int32` ×1e6 | `src/helpers/AdvertDataHelpers.h:14,29-30,61-65` |
| `app_target_ver` has exactly two write sites and neither is a disconnect path | `examples/companion_radio/MyMesh.cpp:869,1024`; reads at `:435,:548` |
| command code **53 does not exist** — the defines jump 52 → 54 | `examples/companion_radio/MyMesh.cpp:44-65` |
| `OFFLINE_QUEUE_SIZE` defaults to 16 | `examples/companion_radio/MyMesh.h:62-63` |
| the telemetry permission gate and its requester-supplied inverse mask | `examples/companion_radio/MyMesh.cpp:628-672` |
| `PUSH_CODE_TELEMETRY_RESPONSE` frame layout | `examples/companion_radio/MyMesh.cpp:728-735` |

Ten spot-checks, ten agreements — including one place where the prose needed
correcting (§4.3, the reserved byte). Everything **not** in that table rests on
the agents' quoted evidence and has not been independently audited. It is
sourced, which is the project's bar for a fact; it is not double-read, which is
the bar this document was originally meant to clear.

**Not observed on hardware.** `NOT EXECUTED — HARDWARE REQUIRED`. Every
statement here is read from source. A vanilla node exists on the LAN behind Home
Assistant on `doctor` and a USB node is coming, and confirming any of this
against one of them would be the first honest `OBSERVED` in this area — a
separate task, T-072a below.

---

## 1. Transports — five, and each build has exactly one

All five are compile-time gated in `examples/companion_radio/main.cpp` and all
register into a single `MultiSerialInterface`.

| # | Transport | Gate | Implementation |
|---|---|---|---|
| 1 | **BLE** | `BLE_PIN_CODE` | ESP32: `helpers/esp32/SerialBLEInterface.h`. nRF52: `helpers/nrf52/SerialBLEInterface.h`. Any other platform is a hard `#error` |
| 2 | **Wi-Fi / TCP** | `WIFI_SSID` | ESP32 only, `helpers/esp32/SerialWifiInterface.h`, TCP server on `TCP_PORT`, default **5000** |
| 3 | **USB serial** | `ENABLE_USB_INTERFACE` | `ArduinoSerialInterface` bound to the Arduino `Serial` object |
| 4 | **Ethernet / TCP** | `ETHERNET_ENABLED` | `ETHERNET_CLASS` picks `CH390EthernetInterface` (ESP32) or `RAK13800EthernetInterface` (nRF52), default port **5000** |
| 5 | **Hardware UART** | `SERIAL_RX` / `SERIAL_TX` | a second, non-USB UART at 115 200 baud |

`MultiSerialInterface` can hold `MAX_INTERFACES` (default 4) at once and fans
`writeFrame()` out to every enabled one. **No shipped build uses that**: all 202
`[env:*companion*]` sections were resolved through their `extends` and `${...}`
inheritance and none defines two of the five flags.

**The LAN question, which is the one OD-7 actually turns on: yes, LAN/TCP exists
at this revision**, in two independent forms — Wi-Fi and Ethernet — both raw TCP
servers on the device, both port 5000 by default, neither overridden anywhere in
the tree. 25 companion envs carry `-D WIFI_SSID` (named `*_companion_radio_wifi`);
exactly two carry Ethernet (`ThinkNode_M7_companion_radio_ethernet`,
`RAK_4631_companion_radio_ethernet`).

Two traps worth carrying:

- **The env name does not tell you the transport.** `Heltec_E290_companion_usb`
  and `meshnology_w12_companion_radio_usb` are named "usb" and define none of the
  five flags, so they register no companion interface at all.
- **One client at a time on Wi-Fi.** A new `accept` stops the previous client
  (`SerialWifiInterface.cpp:56-70`). A watch and a phone cannot both hold the LAN
  link; whoever connects last wins, silently.

There is **no ESP-NOW companion transport**. The only ESP-NOW environments in the
tree are repeater bridges, and `examples/companion_radio` contains no reference
to either.

---

## 2. Framing — one buffer size, four different capacities, no checksum

`#define MAX_FRAME_SIZE 176` in `src/helpers/BaseSerialInterface.h:5`, a bare
`#define` with **no `#ifndef` guard**, so it is not overridable by a build flag.
The first payload byte is always the command or response code.

> **Corrected 2026-08-23.** What stood here said *"176 is the number for every
> transport"*. It is not. 176 is what the **protocol** agrees and what the
> **buffer** holds; what a **link** delivers is a separate quantity, and on BLE it
> is smaller. Upstream lost three bytes per full frame for months on exactly this
> conflation, and the arithmetic is now derived and executed rather than assumed
> — though **nothing here was measured**; the losses are upstream's, on their
> boards and their BLE stack —
> [MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md), issue
> [#143](https://github.com/hleserg/Attadipa/issues/143).

Four numbers, and a client that treats any of them as the others will be wrong:

| # | Name | On an ESP32 at MTU 176 | Set by |
|---|---|---|---|
| 1 | **Protocol / buffer maximum** | **176** | `MAX_FRAME_SIZE`. Both peers must agree; not negotiable |
| 2 | **ATT notification payload** | **173** | the Bluetooth Core specification: negotiated ATT MTU − 3 |
| 3 | **Effective frame ceiling** | **173** | `min(1, 2)`, and across a fan-out wrapper the minimum over the sinks the write reaches |
| 4 | **Application chunk payload** | **173 at this revision**; 171 in the derivative that measured it | 3 minus the frame builder's own header — and **vanilla has no chunked builder at all**, so its header is 0 and rows 3 and 4 coincide. The 171 that appears throughout the upstream evidence is 173 minus a 2-byte chunk header belonging to `caplog` and the config stream, which are *not* commands of this protocol |

Numbers 2 to 4 apply to **BLE only**. On the four byte-stream transports the
frame carries its own 3-byte length prefix outside the payload (§2.1), so
number 1 is number 3 and there is nothing to subtract. That is the whole of what
*"176 is the number for every transport"* was ever true about.

**No checksum anywhere.** A case-insensitive search for `crc|checksum` across
every interface implementation and all of `examples/companion_radio/` returns
nothing. Integrity is whatever the transport provides — which for BLE and TCP is
real, and for a raw UART is nothing.

### 2.1 Byte-stream transports — USB, UART, Wi-Fi TCP, Ethernet TCP

Identical 3-byte header, then the payload, nothing after it:

```
byte 0     direction: '>' (0x3E) device→app, '<' (0x3C) app→device
bytes 1-2  uint16 payload length, LITTLE-endian (LSB first)
bytes 3..  payload, `len` bytes
```

The three implementations differ in how forgiving they are, and the difference
matters:

- **USB / UART** (`ArduinoSerialInterface`): a 4-state machine hunting for `'<'`.
  An over-long frame is **not rejected** — it is silently `// truncate`d to 176
  and delivered. `isConnected()` returns `true` unconditionally: *"no way of
  knowing, so assume yes"*.
- **Wi-Fi TCP**: stricter. Waits for the full length to arrive, **drains** frames
  over 176 rather than truncating, and drops frames whose type byte is not `'<'`.
  One real quirk: `hasReceivedFrameHeader()` requires `length != 0`, so a
  zero-length frame never completes the header state.
- **Ethernet TCP**: byte-for-byte the same state machine as the serial one, same
  truncate-to-176 behaviour. An alternate `ETHERNET_RAW_LINE` mode exists that
  swaps framing for CRLF-delimited lines — **no `.ini` in the tree enables it**.

Outbound frames queue in a 4-slot ring on Wi-Fi and Ethernet, one pushed per
poll. There is no fragmentation: a frame is always a single `write()` of
`3 + len` bytes. TCP may split that across segments, which is exactly why the
length prefix exists.

### 2.2 BLE — no in-band framing at all

No length prefix, no delimiter, no checksum, no chunking and no reassembly code
anywhere in the repository. **One GATT operation carries one whole companion
frame**, and the payload's own first byte is the command or response code.

ESP32 uses a Nordic-UART-style GATT service:

```
service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX char  6E400002-…  WRITE
TX char  6E400003-…  READ | NOTIFY
```

both with `ESP_GATT_PERM_*_ENC_MITM` and static-PIN MITM bonding. TX is
`setValue(buf, len); notify();` — the frame verbatim. Writes are throttled to one
per **60 ms** (`BLE_WRITE_MIN_INTERVAL`), with a 4-deep send queue and a 4-deep
FreeRTOS receive queue.

nRF52 uses Bluefruit `BLEUart` with `SECMODE_ENC_WITH_MITM` and a static PIN. A
partial write is treated as a corrupted frame and **dropped rather than resumed**.
12-deep queues both ways, no inter-write delay, a 250 ms retry throttle instead.

> **The caveat a watch-side client will hit first, and it cannot be settled from
> this repository.** BLE framing relies entirely on the underlying stack
> preserving message boundaries per ATT operation. For nRF52 that is a property
> of Adafruit's `BLEUart` FIFO, whose source is not in this repository — *"one RX
> callback equals one frame"* is what MeshCore assumes, not something these files
> prove. Likewise, whether a 176-byte payload survives in a single notification
> depends on the negotiated ATT MTU. `BLEDevice::setMTU(MAX_FRAME_SIZE)` **requests**
> 176 (`esp32/SerialBLEInterface.cpp:29`); `onMtuChanged()` only logs it (`:100`).
> **The code never adapts to the negotiated MTU and never splits a frame if the
> peer negotiates less.** A client that negotiates a smaller MTU than 176 is in
> undefined territory, and an ESP32-S3 central negotiating conservatively is not a
> hypothetical.
>
> **Partly answered 2026-08-23, and the answer is worse than "undefined".** The
> request *succeeding* is the problem, not the request failing: MTU 176 delivers
> 173, so a full frame is three bytes over the link that the buffer size itself
> asked for. Nobody has to negotiate conservatively for this to bite. A MeshCore
> derivative measured exactly that on ESP32 hardware — three field reports, each
> short by a whole number of full frames times three — and four vanilla producers
> at this revision size against the buffer rather than the link, one of them
> filling it exactly (§2.3). Full chain, matrix and evidence in
> [MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md). Still
> `NOT EXECUTED — HARDWARE REQUIRED` here: the measurement is upstream's, on
> their boards and on a **different BLE stack** — they are on NimBLE, this
> revision is on the Arduino core's Bluedroid.

### 2.3 Which vanilla frames reach the ceiling

Read from source at this revision. `sizeof(out_frame)` is `MAX_FRAME_SIZE + 1`,
which is one byte more than any transport accepts, so the top of each range is
not a large frame but a dropped one.

| Producer | Bound in the code | Largest frame |
|---|---|---|
| `logRxRaw` → `PUSH_CODE_LOG_RX_DATA` (0x88) | `len + 3 <= MAX_FRAME_SIZE` (`MyMesh.cpp:287`) | **exactly 176** — over BLE at MTU 176, three bytes do not arrive |
| `onRawDataRecv` → `PUSH_CODE_RAW_DATA` (0x84) | `payload_len + 4 > sizeof(out_frame)` (`:802`) | **177** — refused by every `writeFrame()`, silently |
| `onControlDataRecv` → `PUSH_CODE_CONTROL_DATA` (0x8E) | same shape (`:782`) | **177**, same |
| `onTraceRecv` → `PUSH_CODE_TRACE_DATA` (0x89) | `12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)` (`:824`) | **177** by the guard; whether the inputs reach it is [#142](https://github.com/hleserg/Attadipa/issues/142)'s question |

Everything else fits: `RESP_CODE_CONTACT` is 148 bytes (`:166-186`),
`RESP_CODE_SELF_INFO` is 58 plus the node name. Both drop paths are silent —
`ArduinoSerialInterface::writeFrame()` returns 0 with no message at all
(`ArduinoSerialInterface.cpp:25-28`), and `MESH_DEBUG_PRINTLN` expands to `{}`
unless `MESH_DEBUG` is defined (`MeshCore.h:29-32`), which no stock build does.

---

## 3. The handshake, and what a reconnect does *not* clear

Two frames, both commented *"sent when app establishes connection"*, in either
order — neither gates the other.

**`CMD_DEVICE_QUERY` (22) — the version exchange.** Request `[22, app_ver]`,
`len >= 2`. Byte 1 is stored verbatim into `app_target_ver`. Reply is
`RESP_CODE_DEVICE_INFO` (13), **82** bytes:

```
[13][FIRMWARE_VER_CODE][MAX_CONTACTS/2][MAX_GROUP_CHANNELS][ble_pin:4]
[build_date:12][manufacturer:40][firmware_version:20][repeat_enabled:1][path_hash_mode:1]
```

`FIRMWARE_VER_CODE` is **13** at this commit. This is a mutual declaration, not a
negotiation: each side states a number and adapts unilaterally. Note the device
hands out its own `ble_pin` in this reply.

> **Corrected 2026-08-23.** This said 81 bytes. The ten fields listed above sum
> to **82** — `1+1+1+1+4+12+40+20+1+1`, and the builder at `MyMesh.cpp:1024-1044`
> writes exactly those ten. The layout was right and the total was not. It
> matters because §7 tells a client to key off the length rather than assume it,
> and an off-by-one in the number it keys against defeats that.

**`CMD_APP_START` (1) — the app start.** Request `[1][7 reserved][app_name…]`,
`len >= 8`; the name is only logged. Reply is `RESP_CODE_SELF_INFO` (5),
variable length, `node_name` unterminated to the end of the frame:

```
[5][ADV_TYPE_CHAT][tx_power_dbm][MAX_LORA_TX_POWER][pub_key:32][lat:4][lon:4]
[multi_acks][advert_loc_policy][telemetry_modes][manual_add_contacts]
[freq_hz:4][bw_hz:4][sf][cr][node_name…]
```

### 3.1 What survives a disconnect

Nothing in the disconnect path touches `MyMesh` state. `onDisconnect()` clears
`deviceConnected` and re-advertises; `clearBuffers()` resets **transport** queues
only. There is no `onDisconnect` hook into `MyMesh` at all. The following are
initialised in the constructor — which runs once, at power-on — and therefore
survive every reconnect until reboot:

| Retained | Why it matters to a client |
|---|---|
| `app_target_ver` | **Both directions are hazards.** Skip `CMD_DEVICE_QUERY` on a fresh boot and you get `0`, so incoming messages arrive as legacy `RESP_CODE_CONTACT_MSG_RECV` (7) with no SNR. Skip it after a v3+ app has been connected and you get `…_V3` (16/17), *three bytes longer*, though you never asked. A client must send `CMD_DEVICE_QUERY` on **every** connection, not on first pairing |
| the offline message queue | 16 frames by default, 256 on some envs. Drained with `CMD_SYNC_NEXT_MESSAGE` (10), one per command, until `RESP_CODE_NO_MORE_MESSAGES`. **Lossy when full**: `addToOfflineQueue` evicts the *oldest channel message*, and if there is no channel message to evict it drops the arriving frame entirely |
| `send_scope` / `send_unscoped` | a flood-scope override set by one session silently applies to the next |
| `sign_data` | an **8 KB malloc** from `CMD_SIGN_START` survives a disconnect, freed only by the next `SIGN_START`/`SIGN_FINISH` or a reboot. A client that starts signing and drops the link leaks it until then |

**The one thing the handshake does clear** is `_iter_started = false` in the
`CMD_APP_START` handler, abandoning a half-streamed contacts sync. Note it is
`APP_START` that does this and **not** `DEVICE_QUERY` — a client that reconnects,
sends only `DEVICE_QUERY`, then `CMD_GET_CONTACTS`, gets `ERR_CODE_BAD_STATE`
from a leftover iterator.

### 3.2 What is actually mandatory

**At the protocol layer, nothing.** There is no session flag, no
`app_target_ver` precondition and no ordering check on entry to the dispatch
chain. `CMD_SEND_TXT_MSG` as the very first frame is executed.

Real requirements live in three other places:

1. **Transport-level pairing, below the protocol.** On ESP32 the GATT
   characteristics are `*_ENC_MITM` with `ESP_LE_AUTH_REQ_SC_MITM_BOND` and a
   static PIN, so an unbonded client cannot write a frame at all; `deviceConnected`
   is set **only** in `onAuthenticationComplete()` on success, and `writeFrame()`
   drops everything while it is false. The PIN comes from `_prefs.ble_pin` or the
   `BLE_PIN_CODE` build flag and can be changed at runtime by
   `CMD_SET_DEVICE_PIN` (37).
2. **De facto: `CMD_DEVICE_QUERY` first**, per §3.1.
3. **Two genuine ordering rules**, both enforced with `ERR_CODE_BAD_STATE` (4):
   `SIGN_START` → `SIGN_DATA`* → `SIGN_FINISH`, and `CMD_GET_CONTACTS` is not
   re-entrant while its iterator is still streaming.

`CMD_APP_START` is **not** a precondition for anything, and `CMD_SEND_LOGIN` (26)
is about logging in to a *remote* repeater over the mesh, not authenticating the
local client.

---

## 4. Position — three different scalings, and no fix validity anywhere

### 4.1 The three scalings, which are easy to conflate

| Where | Type | Scale | Byte order | Size |
|---|---|---|---|---|
| **Advert, on air** | `int32` lat, `int32` lon | ×10⁶ | **native** (little-endian on every supported target) — raw `memcpy` | 4 + 4 |
| **Telemetry `LPP_GPS`, on air** | 3-byte signed lat, lon, alt | lat/lon ×10⁴ (≈ 11 m), alt ×10² | **big-endian**, MSB first | 9 |
| **Companion serial frames** | `int32` | ×10⁶ | native | 4 |

The advert builder **truncates toward zero** (`double`→`int32`), it does not
round.

> **Caveat on the LPP encoder.** The call is `telemetry.addGPS(...)` on a
> `CayenneLPP` object, and CayenneLPP is an **external dependency** —
> `electroniccats/CayenneLPP @ 1.6.1`, declared in `platformio.ini:27` and **not
> vendored** here (only a stub in `test/mocks/` with no `addGPS`). The 9-byte
> layout above is established from MeshCore's own reader, writer and size tables,
> which agree with each other three ways, but **not** from the `addGPS` source,
> which is not in this repository.

### 4.2 Does telemetry carry a position? Yes — conditionally

A telemetry request is `REQ_TYPE_GET_TELEMETRY_DATA = 0x03`; the reply body is
4 bytes of reflected `sender_timestamp` (a tag) followed by a raw CayenneLPP
buffer. Position rides as one `LPP_GPS` record, always on
`TELEM_CHANNEL_SELF = 1`.

Every one of these must hold:

- the whole reply exists only if `permissions & TELEM_PERM_BASE` (0x01);
- position specifically needs `TELEM_PERM_LOCATION` (0x02);
- permission is computed from three owner prefs — `telemetry_mode_base/_loc/_env`,
  each `ALLOW_ALL` / `ALLOW_FLAGS` / off — where `ALLOW_FLAGS` ANDs against that
  contact's flag bits (`contact.flags >> 1`, the LSB being the *favourite* bit);
- **the requester can narrow it further**: `perm_mask = ~(data[1]); permissions &= perm_mask;`
  — the first reserved byte is an inverse mask;
- on a repeater or room server an ACL **guest** is forced to base-only. Note
  `simple_sensor` does **not** apply that narrowing;
- `EnvironmentSensorManager` additionally requires `gps_active`. The per-variant
  managers (t1000-e, heltec_tracker, thinknode_m1, nano_g2_ultra, meshtracker_x1,
  meshadventurer, heltec_mesh_solar) gate on the permission bit **alone** — no
  check of any kind at query time.

### 4.3 Does the node distinguish its own fix from a relayed one?

**There is no relaying to be distinct from, and the more useful answer is worse
than that.**

*Every position a node emits is its own.* All emission paths read the same two
doubles, `SensorManager::node_lat` / `node_lon`. No code path in this repository
puts a third party's coordinates into a telemetry response or an advert.

*Received telemetry is an opaque blob.* `onContactResponse` copies `&data[4]`
straight into a push frame and hands it to the client. The frame is:

```
[0x8B PUSH_CODE_TELEMETRY_RESPONSE][0x00 reserved][pub_key_prefix:6][LPP bytes…]
```

The firmware never parses a peer's LPP for position. The only in-repo callers of
`LPPReader::readGPS` are the two on-device UIs, and both read the node's **own**
freshly built buffer.

> ### The finding that matters most to us
>
> `node_lat` is **a single slot fed from three sources with no marker of which**.
> It is loaded from saved prefs at boot, can be written by the client over the
> companion link, and is overwritten by the GNSS loop **only inside an
> `isValid()` branch**. When the fix goes away the last value simply stays —
> nothing clears or invalidates it.
>
> No fix-valid flag, no satellite count, no fix timestamp and no HDOP is ever
> transmitted, in an advert or in a telemetry record. `LocationProvider` does
> expose `isValid()` and `satellitesCount()`, but they are used for local gating
> and the CLI and are never encoded into a packet.
>
> **A receiver cannot tell, from any field on the wire, whether a reported
> position is a current fix, a fix from six hours ago, or a hand-typed
> `set lat`.**

That is a direct input to
[OD-8](OWNER_DECISIONS.md#od-8--every-source-of-position-and-the-watch-as-the-instrument)
and [ADR-0011](../adr/0011-gnss-integrity.md), not trivia: a position out of a
vanilla MeshCore node arrives with **no provenance and no age**, so Attadipa must
supply both from the outside — the arrival time is the only age we will ever
have, and `PositionValidity` for such a coordinate can never be better than
whatever a coordinate of unknown vintage deserves. It also means the
motion-gated-GNSS work (OD-10, T-080) cannot lean on a companion's fix to decide
whether the wearer moved.

### 4.4 Position fields by packet type

- **`PAYLOAD_TYPE_ADVERT` (0x04)** — the only radio packet type with dedicated
  position fields. `app_data` is `flags`, then optional `lat(4) + lon(4)`, then
  optional `feat1(2)`, `feat2(2)`, then the name. Presence is signalled by
  `ADV_LATLON_MASK = 0x10`.
- **`PAYLOAD_TYPE_TXT_MSG` (0x02) / `GRP_TXT` (0x05) / `GRP_DATA` (0x06)** — **no
  position fields.** The text sub-types are `PLAIN` / `CLI_DATA` / `SIGNED_PLAIN`
  only; group datagrams carry an opaque blob with no location member defined in
  this repository. *A coordinate inside an incoming message is not a MeshCore
  protocol feature at this revision* — if we want one, it is our payload inside
  their datagram, and that is a design decision, not a reading of theirs.
- **`PAYLOAD_TYPE_RESPONSE` (0x01)** — position only as the `LPP_GPS` record of
  §4.2.

`ContactInfo` stores `int32_t gps_lat, gps_lon; // 6 dec places`, populated only
from that contact's own advert and only when the flag bit is set — **but the
client can also write it** via `CMD_ADD_UPDATE_CONTACT`, so a stored contact
position is not necessarily advert-derived once an app has touched it.

### 4.5 Attribution is cryptographic, not a field

There is no "whose position is this" field anywhere. The mechanism is:

1. every advert is **Ed25519-signed** over `pub_key ‖ timestamp ‖ app_data`, and
   lat/lon live inside `app_data`, so they are covered. A failed verify is
   discarded (*"received advertisement with forged signature!"*);
2. replaying an old advert is rejected by a **monotonic timestamp check per
   contact**;
3. a node ignores adverts bearing its own key;
4. re-sharing a third party's location is possible **only** by re-broadcasting
   that party's original signed advert verbatim — the raw packet is stashed by
   pub-key for exactly that purpose. The signature travels with it, so
   attribution survives the relay.

Whether a node puts its position in its advert at all is a three-way policy:
`ADVERT_LOC_NONE` (0), `ADVERT_LOC_SHARE` (1, the live value),
`ADVERT_LOC_PREFS` (2, the configured value). **The receiver sees identical bytes
for 1 and 2** — §4.3 again, from the other end.

---

## 5. The command set

58 codes, all with a handler, no orphans in either direction. Dispatch is one
flat `if / else if` chain on `cmd_frame[0]` in
`MyMesh::handleCmdFrame()` (`MyMesh.cpp:1022-2014`); the `#define`s are at
`MyMesh.cpp:6-64`.

| Code | Name | Code | Name |
|---|---|---|---|
| 1 | `CMD_APP_START` | 31 | `CMD_GET_CHANNEL` |
| 2 | `CMD_SEND_TXT_MSG` | 32 | `CMD_SET_CHANNEL` |
| 3 | `CMD_SEND_CHANNEL_TXT_MSG` | 33 | `CMD_SIGN_START` |
| 4 | `CMD_GET_CONTACTS` | 34 | `CMD_SIGN_DATA` |
| 5 | `CMD_GET_DEVICE_TIME` | 35 | `CMD_SIGN_FINISH` |
| 6 | `CMD_SET_DEVICE_TIME` | 36 | `CMD_SEND_TRACE_PATH` |
| 7 | `CMD_SEND_SELF_ADVERT` | 37 | `CMD_SET_DEVICE_PIN` |
| 8 | `CMD_SET_ADVERT_NAME` | 38 | `CMD_SET_OTHER_PARAMS` |
| 9 | `CMD_ADD_UPDATE_CONTACT` | 39 | `CMD_SEND_TELEMETRY_REQ` |
| 10 | `CMD_SYNC_NEXT_MESSAGE` | 40 | `CMD_GET_CUSTOM_VARS` |
| 11 | `CMD_SET_RADIO_PARAMS` | 41 | `CMD_SET_CUSTOM_VAR` |
| 12 | `CMD_SET_RADIO_TX_POWER` | 42 | `CMD_GET_ADVERT_PATH` |
| 13 | `CMD_RESET_PATH` | 43 | `CMD_GET_TUNING_PARAMS` |
| 14 | `CMD_SET_ADVERT_LATLON` | 50 | `CMD_SEND_BINARY_REQ` |
| 15 | `CMD_REMOVE_CONTACT` | 51 | `CMD_FACTORY_RESET` |
| 16 | `CMD_SHARE_CONTACT` | 52 | `CMD_SEND_PATH_DISCOVERY_REQ` |
| 17 | `CMD_EXPORT_CONTACT` | 54 | `CMD_SET_FLOOD_SCOPE_KEY` |
| 18 | `CMD_IMPORT_CONTACT` | 55 | `CMD_SEND_CONTROL_DATA` |
| 19 | `CMD_REBOOT` | 56 | `CMD_GET_STATS` |
| 20 | `CMD_GET_BATT_AND_STORAGE` | 57 | `CMD_SEND_ANON_REQ` |
| 21 | `CMD_SET_TUNING_PARAMS` | 58 | `CMD_SET_AUTOADD_CONFIG` |
| 22 | `CMD_DEVICE_QUERY` | 59 | `CMD_GET_AUTOADD_CONFIG` |
| 23 | `CMD_EXPORT_PRIVATE_KEY` † | 60 | `CMD_GET_ALLOWED_REPEAT_FREQ` |
| 24 | `CMD_IMPORT_PRIVATE_KEY` † | 61 | `CMD_SET_PATH_HASH_MODE` |
| 25 | `CMD_SEND_RAW_DATA` | 62 | `CMD_SEND_CHANNEL_DATA` |
| 26 | `CMD_SEND_LOGIN` | 63 | `CMD_SET_DEFAULT_FLOOD_SCOPE` |
| 27 | `CMD_SEND_STATUS_REQ` | 64 | `CMD_GET_DEFAULT_FLOOD_SCOPE` |
| 28 | `CMD_HAS_CONNECTION` | 65 | `CMD_SEND_RAW_PACKET` |
| 29 | `CMD_LOGOUT` | | |
| 30 | `CMD_GET_CONTACT_BY_KEY` | | |

**The numbering is not contiguous and must not be assumed to be.** 44–49 are
parked (*"potentially for WiFi operations"*) and **53 is not defined at all** —
the defines jump straight from 52 to 54.

† `CMD_EXPORT_PRIVATE_KEY` and `CMD_IMPORT_PRIVATE_KEY` are the only two commands
behind build flags (`ENABLE_PRIVATE_KEY_EXPORT` / `_IMPORT`), and both flags are
set in the shared `[arduino_base]` build flags that every companion env inherits.
**A stock build answers both** — a vanilla node on a LAN will hand out its
private key to anything that can open the socket, which is a fact about the
threat model rather than about the protocol, and belongs in T-069.

> **The landmine for a client implementer.** Every branch carries its length and
> argument guard *in the same condition* — `len >= 14`, `cmd_frame[1] == 0`, and
> so on. A **defined** command whose frame fails its guard does not get a specific
> error: it falls off the end of the chain to the catch-all and comes back as
> `RESP_CODE_ERR` / `ERR_CODE_UNSUPPORTED_CMD` (1), **indistinguishable from a
> genuinely unknown opcode**. A client cannot use that error to probe which
> commands a node supports, and must not report "your node is too old" when the
> real fault is its own malformed frame.

**Response codes** (0–28): 0 `OK`, 1 `ERR`, 2 `CONTACTS_START`, 3 `CONTACT`,
4 `END_OF_CONTACTS`, 5 `SELF_INFO`, 6 `SENT`, 7 `CONTACT_MSG_RECV`,
8 `CHANNEL_MSG_RECV`, 9 `CURR_TIME`, 10 `NO_MORE_MESSAGES`, 11 `EXPORT_CONTACT`,
12 `BATT_AND_STORAGE`, 13 `DEVICE_INFO`, 14 `PRIVATE_KEY`, 15 `DISABLED`,
16 `CONTACT_MSG_RECV_V3`, 17 `CHANNEL_MSG_RECV_V3`, 18 `CHANNEL_INFO`,
19 `SIGN_START`, 20 `SIGNATURE`, 21 `CUSTOM_VARS`, 22 `ADVERT_PATH`,
23 `TUNING_PARAMS`, 24 `STATS`, 25 `AUTOADD_CONFIG`, 26 `ALLOWED_REPEAT_FREQ`,
27 `CHANNEL_DATA_RECV`, 28 `DEFAULT_FLOOD_SCOPE`.

**Unsolicited pushes**, which arrive at any time and are the reason a client
needs a receive path independent of its request path: `0x80` `ADVERT`,
`0x81` `PATH_UPDATED`, `0x82` `SEND_CONFIRMED`, `0x83` `MSG_WAITING`,
`0x84` `RAW_DATA`, `0x85` `LOGIN_SUCCESS`, `0x86` `LOGIN_FAIL`,
`0x87` `STATUS_RESPONSE`, `0x88` `LOG_RX_DATA`, `0x89` `TRACE_DATA`,
`0x8A` `NEW_ADVERT`, `0x8B` `TELEMETRY_RESPONSE`, `0x8C` `BINARY_RESPONSE`,
`0x8D` `PATH_DISCOVERY_RESPONSE`, `0x8E` `CONTROL_DATA`, `0x8F` `CONTACT_DELETED`,
`0x90` `CONTACTS_FULL`.

**Errors**: 1 `UNSUPPORTED_CMD`, 2 `NOT_FOUND`, 3 `TABLE_FULL`, 4 `BAD_STATE`,
5 `FILE_IO_ERROR`, 6 `ILLEGAL_ARG`. `OK` is one byte, `ERR` is two,
`DISABLED` is one.

---

## 6. What this means for Attadipa

Consequences only. Designs go in ADRs and tasks, not here.

1. **A companion client is viable and is a client, not a port.** MIT, a flat
   command set, framing that fits in a page. Nothing here argues for vendoring
   MeshCore into the firmware — [ADR-0008](../adr/0008-mesh-service-providers.md)'s
   provider shape holds, and the provider is a protocol client.
2. **LAN is real, and it is the cheapest bring-up we have.** A Wi-Fi companion on
   port 5000 needs no BLE stack, no pairing and no radio, and the node behind
   Home Assistant on `doctor` is reachable today. That makes a **host-side** test
   client the first honest step: the framing can be exercised from a laptop long
   before an ESP32 is involved.
3. **176 bytes is the packet budget, and it is not ours to change — but it is
   not the transport's capacity either.** Every queue and buffer on our side is
   bounded by 176; every *chunking* decision is bounded by what the link
   delivers, which on BLE is **173** — less any header a chunking builder of our
   own adds, and vanilla has no such builder, so 173 is the whole of it here. The
   research prompt's §6 — *sizes come from the real transport* — asks for the
   second number, and this document gave it the first until 2026-08-23. §2 and
   [MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md).
4. **A companion position arrives with no provenance and no age.** §4.3. This is
   the single most consequential finding in the document and it lands on
   ADR-0011, OD-8 and OD-10 at once.
5. **Re-send `CMD_DEVICE_QUERY` on every connection.** Not on pairing, not once.
   §3.1.
6. **Never infer capability from an error.** §5's landmine.
7. **The BLE MTU question is ours to answer, not theirs — and it is no longer
   hypothetical.** §2.2. Still the first thing to test against real hardware, and
   it is now a *confirmed* cause, upstream and on someone else's boards, of a
   class of bug that looks like corruption. Specifically it looks like a **radio**
   fault: the vanilla path that reaches the ceiling is `PUSH_CODE_LOG_RX_DATA`,
   whose payload is the raw bytes of a received LoRa packet, so a truncated frame
   presents as a malformed packet off the air. A client must rule out its own
   link before it says anything about the radio.
8. **A stock node exports its private key on request.** T-069's threat model
   gains a section: a vanilla companion on a shared LAN is not a trusted
   peripheral.

## 7. What is still `UNKNOWN`

| Question | Why it is not answered here |
|---|---|
| Does any of this behave as read on a real vanilla node? | source only. `NOT EXECUTED — HARDWARE REQUIRED` |
| What ATT MTU does a real ESP32-S3 central negotiate with a MeshCore peripheral? | still open, and it needs a board. §2.2 |
| ~~What happens to a 176-byte frame if the MTU is smaller?~~ | **narrowed 2026-08-23.** It is short by `176 − (MTU − 3)`, silently — measured upstream on ESP32/NimBLE, and at MTU 176 that is three bytes. **Not** measured on this revision's Bluedroid path, and not on any Attadipa hardware. [MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md) |
| The exact `addGPS` byte layout as CayenneLPP 1.6.1 writes it | §4.1 — external, not vendored |
| Whether the first-party JS and Python clients agree with this reading | not cross-checked; they are the obvious second source and were not consulted |
| How the numbering differs at other tags | 53's absence proves the numbering has already moved. Any statement about another revision is `UNKNOWN` |
| Whether a `RESP_CODE_DEVICE_INFO` from a *newer* node is safe to parse at 81 bytes | the reply has grown before; a client must key off length, and no compatibility rule is documented upstream |
