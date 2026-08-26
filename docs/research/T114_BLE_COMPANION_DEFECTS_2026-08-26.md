# Heltec T114 MeshCore BLE Companion — defect evidence

**Status:** active bench report, not an upstream issue yet. The MeshCore build,
T114 board revision, current BLE PIN and a clean single-client reproduction are
not yet captured together. No credentials are recorded here.

This report separates Attadipa defects from the candidate T114 firmware defect
seen while bringing up the MeshCore Companion client.

## Evidence boundary

| Item | Classification |
| --- | --- |
| Node identity | `MEASURED`: advertised as `MeshCore-🤘Beta Serega` over BLE. |
| Node model | `OPERATOR REPORTED`: Heltec T114. Revision and MeshCore build are `UNKNOWN`. |
| Watch | `MEASURED`: Waveshare ESP32-S3 Touch AMOLED, USB serial `28:84:85:B2:18:A4`. |
| T114 firmware writes | `NOT PERFORMED`: Attadipa did not flash, erase or configure the node. |
| T114 serial log | `NOT CAPTURED`: observations below come from watch USB trace and host BLE scans. |

## Measured observations

### Unpaired GATT reaches notification subscription

With BLE pairing disabled, the watch trace recorded: MeshCore advertisement
match, BLE connection, MTU exchange, MeshCore service/RX/TX/CCCD discovery, and
then a failure writing `0x0001` to the TX CCCD with status `261` (`0x105`). The
watch deliberately terminated that link. A later host BLE scan observed
the same T114 advertising.

Therefore an unpaired GATT attempt did **not** make the T114 unresponsive in
this run. The mapping of `261` to an ATT error is `UNKNOWN` until captured from
the matching ESP-IDF/NimBLE headers; interpreting it as insufficient
authentication is an inference only.

MeshCore documents both a no-PIN BLE connection and an explicit PIN pairing
path. [meshcore_py README](https://github.com/meshcore-dev/meshcore_py)

### The reconnect storm was Attadipa's defect

Before the correction, a failed CCCD subscription left the watch configured.
Its disconnect callback immediately scanned and connected again; the trace
recorded several complete connect → MTU → discovery → failed-subscription
cycles in roughly ten seconds.

That is not an upstream MeshCore allegation. Attadipa now disables reconnection
before fault teardown, and `mesh-disconnect` stops both scanning and the active
link. A failed session is one attempt, not a connection storm.

### Earlier forced-pairing runs correlated with node unavailability

Earlier, the T114 advertised before a watch connection and was absent from
later host BLE scans. Upstream causation is **UNKNOWN** because those runs
were confounded by all of the following Attadipa defects:

- the reconnect storm above;
- forced pairing using a PIN supplied before a factory reset, whose continued
  validity was not established;
- incorrect Companion initialisation: `DEVICE_QUERY`, `APP_START` and
  `GET_CONTACTS` were sent in that order without waiting for responses.

The initialization order is now corrected: `APP_START` first; `DEVICE_QUERY`
only after `SELF_INFO`; `GET_CONTACTS` only after `DEVICE_INFO`. MeshCore's
protocol requires `APP_START` first and one command at a time.
[Companion protocol](https://github.com/meshcore-dev/MeshCore/blob/main/docs/companion_protocol.md#command-sequencing)

### Later single-attempt pairing observation is inconclusive

After the corrections above, a host BLE scan observed the T114 advertising. The
watch was configured for one pairing attempt and automatic retry was disabled.
The next host BLE scan did not observe the node. However, the watch log reader
was opened after the configure acknowledgement and captured neither an
advertisement match nor a connection event. This is therefore **not evidence
that pairing caused the node to stop advertising**; it only records a temporal
correlation.

### Single-attempt post-reboot pairing observation (2026-08-26)

The next capture held the watch USB endpoint continuously from configuration
through teardown. After an operator reboot, the corrected client recorded one
MeshCore advertisement match, one GAP connect, and `MeshCore connected;
starting BLE security`. It injected its configured static passkey, then logged
a pairing error and deliberately terminated the connection (`hci_reason=19`).
No service discovery, notification subscription, or MeshCore command followed.
Automatic retry remained disabled; `mesh-disconnect` then stopped the watch.

An eight-second host BLE scan immediately after that explicit stop did not
observe the T114. The operator reported the node was hard-hung. This is a
`MEASURED` temporal sequence from the watch/host side plus an
`OPERATOR REPORTED` hang; it does **not** establish causation. The exact
pairing-error text was not recoverable from the interleaved debug stream, and
the T114 build, revision, current PIN configuration, and serial log remain
`UNKNOWN`/`NOT CAPTURED`.

### Later watch-side non-discovery (2026-08-26)

After an explicit watch-side stop and a settling delay, the watch performed one
fresh 18-second active discovery. Its inspected display remained `Attached`
with no node, peer count zero and MTU zero. The operator reported the T114
working and free, but no matching node advertising trace or T114 serial log was
captured. The result is therefore `MEASURED` non-discovery on the watch plus an
`OPERATOR REPORTED` node state; it does not establish that the T114 was absent,
stuck or caused by the watch.

An independent, non-connecting 10-second host BLE discovery immediately after
that test observed two unrelated devices but neither the previously observed
T114 address nor its MeshCore name. At the same time USB enumeration exposed
only the Waveshare watch, with no T114 serial device. These are `MEASURED` host
observations, not proof of a T114 fault: its current address, advertising
policy and USB-data path are `UNKNOWN`.

A follow-up instrumented watch scan received a BLE advertising report within
the first second, proving that the ESP32-S3 scanner was receiving local BLE
traffic. During the remaining 12-second window it found no report advertising
the MeshCore service UUID or a name containing `MeshCore`, and therefore did
not initiate a connection. This is `MEASURED` evidence that the watch-side
radio was scanning but had no eligible MeshCore Companion candidate; it does
not identify the T114's current BLE configuration.

## Candidate upstream issue — filing threshold

Do not file an upstream issue until this clean reproduction still leaves the
node unavailable:

1. Record the exact T114 revision, MeshCore Companion build and current BLE PIN
   configuration.
2. Confirm advertising in a host BLE scan or matching T114 serial log.
3. Run one connection from the corrected Attadipa client with the current PIN;
   no retry on failure.
4. Record watch trace, T114 serial log if available, and whether advertising
   returns after deliberate disconnect.

Expected behaviour: a Companion rejects an invalid or unauthenticated client
without becoming unresponsive. A failure of this clean case is a candidate T114
firmware defect. MeshCore has a T114-specific BLE state report, but it concerns
Bluetooth toggle/reconnect and does not establish this as the same fault.
[MeshCore issue #1933](https://github.com/meshcore-dev/MeshCore/issues/1933)

## Safe next action

Do not reconnect the watch while the node is unavailable. On the next bench
session, first capture the exact node build/PIN state and T114 serial output;
then one corrected pairing attempt with automatic retry disabled can decide
whether the filing threshold is met.
