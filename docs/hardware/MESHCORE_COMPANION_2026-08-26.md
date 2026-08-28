# MeshCore Companion bench session — 2026-08-26

> **Status: partially `MEASURED`.** This report separates the physical watch
> and node result from host simulation and from the inbound-message result that
> has not yet been observed. It covers only the Waveshare unit identified in
> [BENCH_DEVICES](../research/BENCH_DEVICES.md), not other boards.

This is the physical evidence for the minimal MeshCore companion slice. The
factory recovery image and its device-wide verification remain the precondition
for every flash in this session; see
[BRINGUP_2026-08-25](BRINGUP_2026-08-25.md) §2.

## 1. Physical equipment and firmware

| Item | Evidence | Classification |
| --- | --- | --- |
| Watch | Waveshare ESP32-S3-Touch-AMOLED-2.06, USB serial `28:84:85:B2:18:A4` | `MEASURED` |
| Companion node | separately enumerated MeshCore node; never flashed by Attadipa tooling | `MEASURED` |
| Firmware image | ESP-IDF 5.5.5 build, `attadipa.bin` `0x1a5f10` B; bootloader, partition table and app each returned `Hash of data verified` when flashed | `MEASURED` |
| Recovery | complete 32 MiB factory image exists locally and was previously `verify-flash` clean | `MEASURED`, source above |

The board reported ESP32-S3 QFN56 revision 0.2, 8 MiB PSRAM and MAC
`28:84:85:b2:18:a4` during the flash handshake. Those are device observations,
not assumptions about another Waveshare unit.

## 2. Companion connection and rendered status

After the operator supplied the node's BLE passkey through the existing debug
channel, the physical watch discovered and connected to the MeshCore Companion
GATT service. The rendered 410 × 502 AMOLED status screen showed:

```
MESH
CONNECTED
Node: Beta Serega
Last message: —
SNR: —  Peers: 350  MTU: 247
```

The screen stayed `CONNECTED` after the old 15-second synthetic liveness
window. The provider now relies on the real BLE disconnect callback rather than
expiring an otherwise idle connection.

- **Connection and status:** `MEASURED` on the physical watch and node.
- **Rendered screen:** `MEASURED`; captures were inspected from the physical
  debug endpoint. The node-name glyph preceding `Beta Serega` is still an
  encoding/rendering defect and is not treated as a hardware fact.
- **RSSI:** `UNKNOWN`. The companion API exposed SNR only after an inbound
  message, so this session did not fabricate an RSSI value.
- **Wake check:** a later black framebuffer capture was followed by a physical
  debug-injected centre tap and an inspected capture of the same `CONNECTED`
  screen. This proves the rendered Mesh view remained live after wake; it does
  not independently classify the preceding blank state as light-sleep.

### Clean-node repeat

After the operator clean-flashed the companion node, a separate one-attempt
watch trace again observed advertisement match, BLE security, service and
characteristic discovery, notification subscription and `Companion GATT ready,
MTU 176`. An inspected physical framebuffer showed `CONNECTED`, node `Beta
Serega`, `Peers: 171` and `MTU: 176`. This is `MEASURED` on the watch and the
advertised node; the node firmware build and board revision remain `UNKNOWN`.

The repeat exposed two Attadipa defects, both corrected before the final
connection:

- a notification delivered before the new connection reached GATT-ready was
  treated as fatal; it is now ignored unless it belongs to the current,
  subscribed TX characteristic;
- Room Server login unnecessarily waited for completion of the full contact
  sync, although it addresses the server by the supplied full public key. It
  now waits only for the connected self/device handshake.

The corrected watch accepted Room Server send requests at its debug boundary,
but a later physical protocol trace recorded `MeshCore delivery failed` from
the companion. No password is recorded here. This is `MEASURED` failure after
the request entered the watch-to-node link; its cause — Room Server reachability,
Room identity, login policy or another node-side condition — is `UNKNOWN`.
External room receipt is therefore `NOT OBSERVED`. The final framebuffer in an
earlier wait window was black; it is `MEASURED` as a black capture only, not
classified as sleep.

### Subsequent non-discovery observation

In a later controlled attempt, the watch waited for the preceding scan
cancellation to settle, then performed a fresh 18-second active BLE discovery.
The inspected physical screen showed `MESH / Attached / Node: — / MTU: 0`.
The operator reported that the T114 was otherwise working and free; that is
`OPERATOR REPORTED`, not evidence that it was advertising a Companion service
at that moment. No Room Server command was queued in this state, and the watch
was explicitly stopped afterwards. This is a `MEASURED` non-discovery from the
watch; the current T114 advertising state, PHY and advertisement layout are
`UNKNOWN`.

### Inbound queue drain

The watch now sends `CMD_SYNC_NEXT_MESSAGE` after `CONTACTS_END`, as the
Companion protocol requires for messages queued while the client was away. In a
physical 45-second connected session, the inspected AMOLED framebuffer showed
`CONNECTED`, the node name, an external text in `Last message`, and
`SNR: 11.50 dB`. This is `MEASURED` receive-and-render evidence from the
physical watch and node. It proves an external message was retrieved from the
node queue; whether it was a reply to the current outbound Room message remains
`UNKNOWN`.

## 3. Room Server outbound message

The operator selected an existing MeshCore Room Server and supplied its public
identity and temporary login password interactively. Neither credential is
recorded in this repository. The debug-only sequence is:

```
watch → BLE Companion → CMD_SEND_LOGIN → LOGIN_SUCCESS
      → CMD_SEND_TXT_MSG → Room Server
```

The Room Server received the first physical `Hello from Attadipa`. Its visible
garbled prefix was a diagnostic result: Attadipa had serialized a 32-byte public
key in a command whose destination field is a 6-byte key prefix. The remaining
26 key bytes appeared before the text. This proves that the transmission did
reach the Room Server, but it was **not** accepted as clean-message evidence.

The defect was corrected in the shared `MeshCoreCompanion::enqueue_private`
serializer, host-tested, built and flashed again. The subsequent corrected
Room Server request was accepted by the physical watch debug boundary. Its
independent appearance in the room is **PENDING OPERATOR OBSERVATION**; do not
claim delivery until it is observed there.

## 4. What is and is not proven

| Requirement | Result | Evidence |
| --- | --- | --- |
| Discover available node | `MEASURED` | §2 physical `CONNECTED` status |
| Establish link state | `MEASURED` | §2 physical status persists while idle |
| Send through MeshCore to a compatible peer | `MEASURED`, first payload malformed | §3 Room Server observed the text and the encoding fault |
| Send corrected payload | `MEASURED FAILED` | T114 answered `MeshCore delivery failed`; external Room receipt was not observed |
| Receive external message and show it on watch | `MEASURED` | §2 inbound queue-drain screenshot: text in `Last message`, SNR `11.50 dB` |
| Receive a reply to the current outbound Room message | `PENDING OPERATOR OBSERVATION` | inbound text was retrieved, but its causal relation to the current send is not established |
| Host protocol logic | `SIMULATED` | `test_meshcore_companion`, `test_debug`, and `tools/watch/selftest.py` passed; they are not HIL proof |

The smallest next physical step is a short reply from a different Room Server
client while the watch remains connected, followed by a physical screenshot
showing it in `Last message` (and SNR if supplied by the node).
