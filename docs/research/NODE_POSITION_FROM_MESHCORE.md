# A position out of a MeshCore node — what the wire actually carries

Research for [#412](https://github.com/hleserg/Attadipa/issues/412). **Nothing
here was executed on hardware.** Every claim about MeshCore is read from
upstream source at two named revisions; every claim about a physical node is
`NOT EXECUTED — HARDWARE REQUIRED` and says so where it stands.

The question the issue asked was *how do we decode the Cayenne LPP GPS record a
telemetry response carries.* The answer is that we probably should not start
there, and §2 is why.

## 0. Was the finding still true

Yes. Checked against `main` at `0a51f2c`, which is the revision the issue was
filed against:

- no `LocationService`, no `PositionProvider`, no `NodePositionProvider` and no
  LPP decoder exists anywhere in `core/`, `link/`, `firmware/`, `apps/`, `sim/`
  or `tests/`. `grep -rn "LPPReader\|LPP_GPS\|TELEM_PERM\|telemetry"` over all
  six returns nothing;
- `core/include/attadipa/core/position.h:134` — "struct GnssObservation {" — has
  no producer. It is constructed only by tests;
- `docs/architecture/PLATFORM_AUDIT.md:239` — "or provider registry is wired" —
  stands as written.

Upstream had not moved either, and this was checked by fetching files rather
than by reading a compare page. The five files every claim below rests on —
`src/helpers/sensors/LPPDataHelpers.h`, `examples/companion_radio/MyMesh.cpp`,
`src/helpers/sensors/EnvironmentSensorManager.cpp`, `src/helpers/SensorManager.h`
and `variants/heltec_t114/platformio.ini` — are **byte-identical** between the
Attadipa pin `d92964352441e53b93e8667b802e04f6e072b39e` and upstream `main` at
`0679dbeffc504d562d2f09eb072fdc223f8ffc2a`, compared with `diff` on 2026-09-02.
MIT throughout.

That matters more than the usual "the pin is current" note. Two of those files
are the ones the bench node runs — [`TEST_FLEET.md:54`](TEST_FLEET.md)
"It stays on `v1.17.1-d929643` — owner decision," — so a bench result and a
source reading are about the same bytes here, which is not true for every node
in the fleet.

## 1. Three wires carry a coordinate, and they are not equivalent

A MeshCore node emits its position in three places. The existing research
([`MESHCORE_COMPANION_PROTOCOL.md:331`](MESHCORE_COMPANION_PROTOCOL.md)
"The caveat on the LPP encoder is closed") documented the second — that line now
reads as closed because §3.1 below is what closed it. The first path is the one
this work found, and it is the cheapest by a wide margin.

| | A. `RESP_CODE_SELF_INFO` | B. LPP `LPP_GPS` in a telemetry reply | C. the advert |
|---|---|---|---|
| **Scaling** | ×10⁶ — **≈ 0.11 m** | ×10⁴ — ≈ **11.1 m** | ×10⁶ |
| **Width** | `int32` ×2, little-endian | signed 24-bit ×3, big-endian | `int32` ×2 |
| **Altitude** | not carried | yes, ×10², signed 24-bit | not carried |
| **Costs** | nothing — already in the handshake | a request, a permission gate, a radio round trip for a remote node | nothing, but it is a broadcast |
| **Gated by** | nothing | 3 owner prefs ∧ per-contact flags ∧ requester mask ∧ `gps_active` | `advert_loc_policy` |
| **Present when the node has no GNSS** | **yes** — it is whatever is in prefs | **no** | policy-dependent |

### 1.1 Path A — the coordinate is already in a frame this repository parses

`CMD_APP_START` is the first thing an Attadipa companion session sends, and the
node answers `RESP_CODE_SELF_INFO` (code `5`). Upstream builds that frame field
by field in `examples/companion_radio/MyMesh.cpp`, and the arithmetic is
unambiguous:

```text
offset  0   RESP_CODE_SELF_INFO (5)
        1   ADV_TYPE_CHAT
        2   tx_power_dbm
        3   MAX_LORA_TX_POWER
        4  .. 35   self_id.pub_key            32 bytes
       36  .. 39   int32 lat = node_lat  * 1e6   little-endian
       40  .. 43   int32 lon = node_lon  * 1e6   little-endian
       44   multi_acks
       45   advert_loc_policy
       46   (telemetry_mode_env << 4) | (telemetry_mode_loc << 2) | telemetry_mode_base
       47   manual_add_contacts
       48  .. 51   freq  (Hz)
       52  .. 55   bw    (Hz)
       56   sf
       57   cr
       58  ..      node_name, not null-terminated, length is the frame's
```

The upstream lines are `lat = (sensors.node_lat * 1000000.0);` and the two
`memcpy(&out_frame[i], &lat, 4)` calls that follow it, inside the
`cmd_frame[0] == CMD_APP_START` branch.

**The offsets are confirmed twice, from two independent directions.** The
arithmetic above puts the name at 58; the bench capture already in the tree put
it at 58 in a 72-byte frame, and this repository's parser reads it from there —
`link/src/meshcore_companion.cpp:524` —
"(void)copy_text(status_.node_name, &data[58], size - 58);" — with the public
key at 4, `link/src/meshcore_companion.cpp:521` — "std::memcpy(status_.node_id.public_key.data(), &data[4],".
Bytes 36–43 sit between two fields we already read correctly, and we discard
them.

`memcpy` of an `int32` means **the node's native byte order**, which is
little-endian on both the nRF52840 and the ESP32 — every board in the fleet.
A big-endian node would emit big-endian here and nothing in the protocol says
so; recorded because the field is not self-describing.

Two properties that make path A better than it looks:

- it is **not** gated by `advert_loc_policy`, which only governs the advert, and
  it is **not** gated by the three `telemetry_mode_*` prefs, which only govern
  path B. A connected client sees the node's stored coordinate unconditionally.
  That is a privacy fact as much as a convenience one, and it is handed to
  T-069 the way the protocol report hands over the private-key export:
  [MESHCORE_COMPANION_PROTOCOL.md](MESHCORE_COMPANION_PROTOCOL.md) §6, item 9;
- byte 46 hands us the telemetry permission configuration in the same frame, so
  a client can tell *before asking* whether a path-B request would be refused.

And one that makes it worse: `RESP_CODE_SELF_INFO` is only sent in reply to
`CMD_APP_START`, and that handler also does `_iter_started = false`, which
**aborts a contacts iteration in progress**. Re-sending `CMD_APP_START` to
re-read a coordinate is therefore not free, and a provider that polls it must
not do so while contacts are being synced.

### 1.2 Path B — the LPP GPS record, and the two gates in front of it

The reply body to `REQ_TYPE_GET_TELEMETRY_DATA` (`0x03`) is four bytes of
reflected `sender_timestamp` followed by a raw CayenneLPP buffer. The position
rides as one `LPP_GPS` (136) record on `TELEM_CHANNEL_SELF = 1`.

Two independent gates, and the second is new here:

1. **Permission.** `onContactRequest` computes `permissions` from
   `telemetry_mode_base`/`_loc`/`_env` ANDed with the contact's flags, then
   applies the requester's own inverse mask: `uint8_t perm_mask = ~(data[1]);`.
   Nothing is returned at all unless `TELEM_PERM_BASE`; the position needs
   `TELEM_PERM_LOCATION` as well.
2. **`gps_active`.** `EnvironmentSensorManager::querySensors` reads
   `if (requester_permissions & TELEM_PERM_LOCATION && gps_active)` before it
   calls `addGPS`. A node with permission granted and its receiver switched off
   returns a well-formed telemetry reply **with no GPS record in it**, and that
   is a normal outcome rather than an error — the reuse ledger already required
   a test for the permission half of this; the `gps_active` half is a second
   way to reach the same silence, from a different cause.

`gps_active` is **off by default** — `_prefs.gps_enabled = 0;` in `MyMesh.cpp`,
commented "GPS disabled by default" — and it is a persisted user preference.

### 1.3 The one thing path B tells you that path A does not

Because of gate 2, **the presence of an `LPP_GPS` record is evidence that a
receiver exists on the node and is switched on.** That is the only origin signal
either path carries, and it is weak: it says a receiver is running, not that it
has a fix, and not that the coordinate in the record came from it.

It is also **only true of `EnvironmentSensorManager`**, which is what the T114
builds. [`MESHCORE_COMPANION_PROTOCOL.md:370`](MESHCORE_COMPANION_PROTOCOL.md)
"additionally requires `gps_active`" — records seven per-variant managers that
gate on the permission bit alone. On one of those the record's presence says
nothing at all, so this inference is a property of the node's variant and not of
the protocol. A provider must not depend on it.

There is a stronger and cheaper version of the same signal, and the issue
assumed it did not exist. `CMD_GET_CUSTOM_VARS` (40) → `RESP_CODE_CUSTOM_VARS`
(21) returns `name:value` pairs comma-separated, and the sensor manager
publishes exactly one name, `gps`, and publishes it **only when a receiver was
detected**:

| `RESP_CODE_CUSTOM_VARS` | What it establishes | What the coordinate can be |
|---|---|---|
| no `gps` key | `gps_detected == false` — no receiver answered at boot | prefs, or typed by a client. Never a fix from this boot |
| `gps:0` | receiver detected, switched off | a fix from earlier in this boot, a restored pref, or typed |
| `gps:1` | receiver detected, running | any of the above, **or** a current fix. Still indistinguishable |

Three states rather than one is a real improvement on "a coordinate exists", and
it costs one command. It is still not a fix flag.

`gps_detected` deserves its own warning, because it is not a board fact:
`EnvironmentSensorManager` opens the GPS UART, waits `delay(1000)`, and sets
`gps_detected = (Serial1.available() > 0)`. A receiver that has not started
emitting NMEA inside that second is *not detected for the whole session*. So
`no gps key` means "nothing answered in one second at boot", not "this board has
no receiver", and a node can disagree with itself across two power cycles.

## 2. What the node knows and does not send

This is the crux, it was already the crux —
[`COMPANION_AND_POSITION_SOURCES.md:47`](COMPANION_AND_POSITION_SOURCES.md)
"No fix flag, no satellite count, no timestamp, no HDOP is ever transmitted" —
and reading the current source makes it sharper rather than softer.

`LocationProvider`, MeshCore's own GNSS interface, declares `isValid()`,
`satellitesCount()` and `getTimestamp()` as pure virtuals, and
`MicroNMEALocationProvider` implements all three. **The node has the fix flag,
the satellite count and the receiver's own UTC in hand.** No wire in §1 carries
any of them.

Worse, the one place `isValid()` is consulted is a write gate, not a publish
gate:

```cpp
if (_location->isValid()) {
  node_lat = ((double)_location->getLatitude())/1000000.;
  node_lon = ((double)_location->getLongitude())/1000000.;
  node_altitude = ((double)_location->getAltitude()) / 1000.0;
}
```

`EnvironmentSensorManager::loop()`, unchanged at both revisions. **On loss of
fix the last valid coordinate stays in `node_lat` and keeps being transmitted.**
There is no transition, no marker, nothing that changes on the wire when the sky
view goes. Path A and path B are equally affected; they read the same two
doubles.

`node_lat` is also written by `CMD_SET_ADVERT_LATLON` from a client (range
checked to ±90/±180 ×10⁶ and nothing else) and restored from prefs at boot. And
`savePrefs()` copies `sensors.node_lat` into `_prefs` — so **toggling the `gps`
setting persists whatever coordinate is currently in the slot**, which is the
mechanism by which a stale fix becomes a permanent one across a reboot.

`gps_update_interval_sec` defaults to 1 s and is settable to 86400. So the age
of the value in the slot, measured against the receiver, is bounded by a
node-side setting the client can read only by asking for it — and only when the
receiver is on.

Upstream [issue #2179](https://github.com/meshcore-dev/MeshCore/issues/2179),
open since 2026-03-28, asks for GPS accuracy in telemetry. An open request for a
field is decent evidence the field is not there; it is not evidence about when
it will be.

**Conclusion for the type mapping:** fix status, observation age, accuracy,
HDOP, satellite count and GNSS-versus-manual origin are `UNKNOWN` on every path,
at both revisions, and no combination of requests changes that. §4 maps that
honestly rather than working around it.

## 3. A bounded LPP GPS decoding contract

Specified here whether or not §6's recommendation defers it, because the
specification is most of the work and it does not rot.

### 3.1 The layout is now established from the writer, not inferred

`MESHCORE_COMPANION_PROTOCOL.md` recorded that the encoder was an external,
non-vendored dependency and that the 9-byte layout therefore rested on the
reader and the format documentation. **That caveat is closed.** MeshCore pins
`electroniccats/CayenneLPP @ 1.6.1`; at `ElectronicCats/CayenneLPP@a83f3e4`,
`src/CayenneLPP.cpp`, `addGPS` writes:

```text
[channel][136]
[lat>>16][lat>>8][lat]      int32_t lat = latitude  * 10000
[lon>>16][lon>>8][lon]      int32_t lon = longitude * 10000
[alt>>16][alt>>8][alt]      int32_t alt = altitude  * 100
```

Big-endian, signed 24-bit, `LPP_GPS_SIZE 9`. This is byte-for-byte what
MeshCore's *own* `LPPWriter::writeGPS` in `LPPDataHelpers.h` produces, and what
`LPPReader::readGPS` expects. Three agreeing sources, one of which is the code
that actually runs.

### 3.2 Two arithmetic properties a golden vector must respect

**The encoder truncates toward zero; it does not round.** `int32_t lat =
latitude * 10000;` is a C float-to-integer conversion. So the transmitted value
is never larger in magnitude than the node's stored one, and can be up to one
LSB smaller — **1e-4° ≈ 11.1 m north–south**, biased consistently toward the
equator and the prime meridian. A test vector generated with round-to-nearest
will disagree with a real device by one LSB near half the time, and the
disagreement is not a decoder bug.

**The value is narrowed to `float` before that.** `node_lat` is a `double`;
`addGPS` takes `float`. The 24-bit mantissa costs nothing at these magnitudes
compared with the ×10⁴ quantisation, but it means the pipeline is
`double → float → truncate`, and a bit-exact model must apply all three steps in
that order.

### 3.3 The conversion to Attadipa's types needs no floating point at all

`core/include/attadipa/core/position.h:42` — "struct Position {" — is `e7`, and
LPP is `e4`. The ratio is exactly 1000, so:

```text
latitude_e7 = raw_i24_lat * 1000        exact, integer
longitude_e7 = raw_i24_lon * 1000
altitude_msl_mm = raw_i24_alt * 10      LPP centimetres to millimetres
```

**Range-check before multiplying, not after.** The 24-bit field spans
±8 388 607, which is ±838.8607° — so a hostile or corrupt frame can carry a
latitude nine times the legal maximum, and `raw × 1000` overflows `int32` above
2 147 483. `core/include/attadipa/core/position.h:55` —
"constexpr bool in_range(Position p)" — cannot save a value that already
overflowed on the way in. The decoder's own bounds are therefore
`|raw_lat| ≤ 900 000` and `|raw_lon| ≤ 1 800 000`, applied to the raw 24-bit
integer.

Altitude is safe by comparison: ±8 388 607 cm × 10 = ±83 886 070 mm, inside
`int32`. Its **datum is `UNKNOWN`**: Cayenne LPP says "meters" and nothing more,
and MicroNMEA's `getAltitude` reads NMEA GGA field 9, which is orthometric
height above mean sea level — so `altitude_msl_mm` is the better guess and it is
a guess. Record it as MSL with the provenance, or leave both altitude fields
empty; do not populate `altitude_ellipsoid_mm`.

### 3.4 Do not copy `LPPReader`, and the reason is arithmetic

`LPPReader::readGPS` is protocol evidence. As a decoder for untrusted bytes it
has two defects, and the second is worse than the one the issue names.

```cpp
bool readGPS(float& lat, float& lon, float& alt) {
  lat = getFloat(&_buf[_pos], 3, 10000, true); _pos += 3;
  lon = getFloat(&_buf[_pos], 3, 10000, true); _pos += 3;
  alt = getFloat(&_buf[_pos], 3, 100, true); _pos += 3;
  return _pos <= _len;
}
```

1. **It reads nine bytes before checking anything.** `readHeader` guarantees
   only `_pos + 2 < _len` on entry, so after the header `_pos ≤ _len − 1`: one
   valid byte may remain and nine are read. **Up to 8 bytes past the end of the
   buffer**, on every truncated GPS record, before the bounds test the function
   does have.
2. **`_pos` is a `uint8_t`, so `_pos += 9` wraps.** At `_pos = 250` it becomes
   3, and `return _pos <= _len` is then **true** — the function reports success
   on a record it just read out of bounds, hands the caller three garbage floats
   and leaves the parse cursor pointing into the middle of the frame. The frame
   budget makes 250 unreachable over a stock BLE link today
   ([`MESHCORE_BLE_FRAME_CAPACITY.md`](MESHCORE_BLE_FRAME_CAPACITY.md)), which
   is a property of the transport and not of the function.

Two smaller ones worth carrying into the contract: `skipData`'s `default: _pos++`
advances one byte for an unknown type, which **desynchronises** the parse rather
than ending it; and its `LPP_POLYLINE` case carries upstream's own
`// TODO: this is MINIMUM`, so a frame containing a polyline cannot be skipped
correctly at all.

`readHeader`'s `if (_pos + 2 < _len)` is one byte conservative rather than one
byte short — it refuses a header with no data byte behind it, which is the safe
direction. Written down because it looks like an off-by-one and is not.

### 3.5 The contract, then

Attadipa's decoder is a pure function over `(const uint8_t*, size_t)` returning
a small result type. `size_t` offsets, never `uint8_t`. Every field bounds-checked
**before** the read. Unknown type ⇒ stop, do not skip. Channel 0 ⇒ end of data.
A record whose declared size runs past the buffer ⇒ the whole frame is rejected,
not the record. Duplicate `LPP_GPS` on channel 1 ⇒ reject the frame rather than
pick one, because upstream emits exactly one and two means something we do not
understand. `LPP_GPS` on a channel other than 1 ⇒ ignored, not treated as the
node's own position. No allocation, no float, total, deterministic — the same
properties that `position.h:214` — "PositionValidity classify" — already relies
on for the replay rig.

## 4. Mapping the wire onto the types that already exist

No new position model. `GnssObservation` is wide enough, and the width is the
point — most of it stays empty, and empty is the correct answer.

| Field | Value from a MeshCore node | Why |
|---|---|---|
| `observed_at` | monotonic time the **frame arrived** | it is the only clock we have. It is an arrival time and must never be presented as an observation time |
| `position` | set, after §3.3's checks | |
| `altitude_msl_mm` | path B only, provenance recorded | §3.3; `UNKNOWN` datum |
| `fix_type` | **`FixType::Unknown`** | never `TwoD`, never `ThreeD`. The node did not say, and `NoFix` in this *field* would be the node saying "no", which it never says. The *verdict* on the observation is another matter: `classify()` maps an unstated fix type to `PositionValidity::NoFix` before it looks at the coordinate or the clock (§4.1) |
| `source` | **`PositionSource::NodeGnss`** | `core/include/attadipa/core/position.h:81` — "enum class PositionSource : std::uint8_t {" — already has it. Note the name overstates: it is *a node's coordinate*, whose origin may be manual |
| `receiver_time`, `receiver_time_valid` | absent / false | the node has it and does not send it |
| `horizontal_accuracy_mm`, `hdop_centi`, `satellites_used`, everything in "what it can see" | **absent** | not transmitted, at either revision |
| `jamming`, `spoofing` | `ReceiverIndication::Unknown` | not `Unsupported` — the node's receiver may well support it. We cannot see the answer, which is a different fact from there being none |
| `native` | the raw wire values, unnormalised | `vendor` identifies the path; keeps the evidence for our own bugs |

### 4.1 Validity, and why `Valid` is not reachable

`core/include/attadipa/core/position.h:185` — "enum class PositionValidity" —
has four values, and through the tree's `classify()` a stock MeshCore node
reaches exactly one of them: **`NoFix`, at every age.** The classifier returns
it for `FixType::Unknown` before the coordinate or the clock is consulted —
`core/src/position.cpp:13` — "observation.fix_type == FixType::Unknown) {" —
into `core/src/position.cpp:14` — "return PositionValidity::NoFix;" — and
ADR-0011 makes that verdict the caller's with a policy, not something a
provider may hold its own opinion about
(`docs/adr/0011-gnss-integrity.md:423` — "that is a `classify()` verdict a caller reaches with a").

The consequence is downstream, and it is conservative. A second provider is
comparable only at `Valid` or `Degraded` —
`core/src/trust.cpp:706` — "other_validity == PositionValidity::Valid ||" —
`core/src/trust.cpp:707` — "other_validity == PositionValidity::Degraded;" —
so a node provider built to this document is never one: ADR-0011's
cross-provider check does not run against a node, and
`core/src/trust.cpp:404` — "set(engine_, TrustReason::FixLost, validity == PositionValidity::NoFix, now);"
— holds `FixLost` while the node is the source. Nothing the first consumer
(§6) needs is lost by that: the fix type, both ages and the `gps` key are on
the same surface as the coordinate. The ladder the rest of this section argues
— **`Stale` as the resting state, `Degraded` only on a changing coordinate from
a running receiver** — is what a classifier *would* say about a coordinate
whose fix type was never stated. That is a new `classify()` case, an amendment
to ADR-0011, and it is deferred with §6's list rather than assumed; the
argument is kept here so it is not derived twice, and §8.1 pins the current
verdict so the amendment cannot land without this report changing.

The argument is short. `classify()` needs freshness, and the freshest thing we
know is when the frame arrived. `ValidityPolicy`'s `stale_after` is 30 s. A
coordinate that arrived 2 s ago is 2 s old *as a message* and of unknown age *as
an observation* — those are exactly the two ages
`core/include/attadipa/core/availability.h:70` — "struct Timed {" — was built
for, and ADR-0004 §3 already rules that the interface shows the larger. Here the
larger is unbounded. So:

- **`age_at_us_ms`** — known, exact, from the arrival monotonic;
- **`age_at_source_ms`** — `UNKNOWN`, and there is no defensible number.
  `Timed<T>` cannot say so: the field is
  `core/include/attadipa/core/availability.h:72` — "std::uint32_t age_at_source_ms = 0;"
  — and the tree's one consumer adds whatever is there to transit as a real
  number, `core/src/time_service.cpp:31` — ": transit + observation.age_at_source_ms;".
  So the zero the field carries is not a claim of freshness; it is a field with
  no meaning, and nothing about the number makes it safe;
- therefore **`Validity::Unknown`**, not `Valid`, on the `Timed<Position>` a
  Location owner publishes — and that is a **precondition on every consumer,
  not a property of the provider**: a consumer reads `validity` first and treats
  both ages as undefined under `Unknown`. The first consumer §6 names must show
  the ages as unknown, not as `0 ms`. This is the only thing that makes the
  zero safe, and it is the contract §8.1 tests.

Under that amendment, a `PositionValidity` of `Degraded` is defensible *only* when
`RESP_CODE_CUSTOM_VARS` said `gps:1` and the value changed since the last read —
a changing coordinate from a running receiver is weak evidence of a live fix.
`gps:0`, no `gps` key, or an unchanged value all mean `Stale`. **`Valid` is not
reachable from a stock node and no provider may set it**, which is the strongest
single rule this research produces.

**And on path A alone there is no second read, so `Degraded` is not reachable
either.** The coordinate arrives once per session, in the `RESP_CODE_SELF_INFO`
that answers `CMD_APP_START`; the only way to ask again is to send
`CMD_APP_START` again, which is M27 — `UNKNOWN`, and it aborts a contacts
iteration in progress (§5) — and §5 argues against the poller that would
produce a second read anyway. So even under the amendment the first slice's
ladder **tops out at `Stale`**: `Degraded` becomes reachable only when a safe
re-read exists — M27 measured, or path B's request/response decoded — and until
then a provider that publishes `Degraded` has read something this document has
not shown it. Until the amendment lands, §6.1's rows and §8.1's tests say
`NoFix`, which is what the tree says today.

### 4.2 Provenance is the node, and the node is not the wrist

`PositionSource::NodeGnss` maps through `body_of()` to a node body, not a watch
body, and that is already how the tree treats a companion's heading —
ADR-0009 refuses to present a companion's heading as the wearer's without a
known transform. The same refusal applies here and needs no new mechanism: a
node in a rucksack, on a windowsill or bolted to a wall reports its own
position, and a detached node reports a place the wearer is not. Any consumer
that wants "where am I" rather than "where is my node" must ask a different
question, and the first consumer in §6 deliberately asks the second.

## 5. Correlation, timeouts and cadence

For the **directly connected** node — path A, and path B's `len == 4` "self"
form — none of this applies. There is no radio, no tag and no timeout beyond the
link's own.

For a **remote** node it does, and the framing has a trap.

| | legacy `CMD_SEND_TELEMETRY_REQ` (39) | `CMD_SEND_BINARY_REQ` |
|---|---|---|
| Push code | `PUSH_CODE_TELEMETRY_RESPONSE` `0x8B` | `PUSH_CODE_BINARY_RESPONSE` `0x8C` |
| Frame | `[0x8B][0][pub_key_prefix×6][LPP…]` | `[0x8C][0][tag×4][payload…]` |
| Tells you **who** answered | yes, 6-byte prefix | **no** |
| Tells you **which request** | **no — the tag is stripped** | yes |

Neither carries both. `onContactResponse` matches on `tag == pending_telemetry`
internally and then does `memcpy(&out_frame[i], &data[4], len - 4)`, dropping the
tag it just matched. So a client on the legacy path correlates by peer identity
only, and a client on the binary path by tag only.

That is survivable because the node keeps **one pending request of any kind**:
`clearPendingReqs()` zeroes `pending_login`, `pending_status`,
`pending_telemetry`, `pending_discovery` and `pending_req` together, and every
send calls it. `meshcore_py` mirrors this with a single `_mesh_request_lock`.
**An Attadipa provider must serialise the same way**; it does not need a request
table, and building one would model a concurrency the node does not have.

`RESP_CODE_SENT` returns `est_timeout` at bytes 6–9. It is **milliseconds** —
`calcFloodTimeoutMillisFor` / `calcDirectTimeoutMillisFor`, both `uint32_t` and
both named for the unit. `meshcore_py` converts it with
`suggested_timeout / 800`, which is milliseconds-to-seconds with a 25 % margin
folded in, and elsewhere in the same package with `/ 1000 * 1.2`. Two different
margins on one field. Take the unit from the firmware and choose our own margin
deliberately; do not copy either constant.

`CMD_SEND_PATH_DISCOVERY_REQ` is a telemetry request with
`req_data[1] = ~(TELEM_PERM_BASE)`, so it asks for base telemetry only and
**never returns a position**. Worth knowing before somebody reaches for it as a
cheap poll.

The legacy path is not broken and the issue's caution about it can be relaxed
slightly: `sendRequest(recipient, req_type, tag, est_timeout)` builds a 13-byte
body — tag, type, **four zero reserved bytes**, four random — so the responder's
`perm_mask = ~(data[1])` reads a real zero and narrows nothing. `data[1]` is in
bounds. What the legacy path actually costs is the tag, per the table above.

**Cadence and power.** Nothing here justifies a task, a queue or a timer of its
own. A Location owner asks through the existing MeshCore session, under
ADR-0015's transport ownership and ADR-0016's power owner, at a cadence the
consumer sets. A coordinate whose source age is `UNKNOWN` does not get fresher
by being asked for more often — that is the honest argument against a poller,
and it is stronger than the power one.

## 6. The smallest first slice, and what it should be

PLATFORM_AUDIT is explicit about the shape:
`docs/architecture/PLATFORM_AUDIT.md:259` — "One provider contract is enough" —
and `docs/architecture/PLATFORM_AUDIT.md:556` — "There is no second forwarding HAL".

**Recommendation: build the first `PositionProvider` on path A.**

The reasoning is not that LPP is hard. It is that path B is strictly worse on
every axis that matters for a first slice — 100× coarser, four gates in front of
it, a radio round trip for a remote node, a decoder whose upstream reference is
unsafe — and the only thing it adds is the "a receiver is running" signal, which
`CMD_GET_CUSTOM_VARS` gives directly and more cheaply. A slice built on path A
is a handful of bytes in a frame this repository already parses, plus the owner
seam that P0.3 actually asks for. The owner seam is the deliverable; the decoder
is not.

The shape, following `core/include/attadipa/core/mesh_service.h:84` —
"class MeshProvider {" — which is the pattern the tree has already accepted:

```text
    the first consumer (engineering/diagnostics only)
                    |
             LocationService          owns effective source, availability,
                    |                 validity, trust, provenance, both ages
             PositionProvider         one contract, no second HAL
                    |
         NodePositionProvider         reads SELF_INFO bytes 36-43 and
                    |                 CUSTOM_VARS; builds GnssObservation
          MeshCoreCompanion           already exists, already parses the frame
```

`NodePositionProvider` lives beside the companion client in `link/`, not in
`core/`, because it knows a wire format. `LocationService` is `core/` and knows
none. Applications see neither; they see availability, validity and two ages,
exactly as `TimeService` already publishes them.

**Deferred out of the first slice, deliberately:** an ADR-0011 amendment
giving `classify()` a case for a coordinate whose fix type was never stated
(§4.1 — without it every node observation is `NoFix`), the LPP decoder
(specified in §3, buildable any time), local GNSS, source fusion or any estimator, GNSS power
policy, Navigator, and provider selection between two live providers — ADR-0008
§3's table needs more rows before that is a decision rather than a coin toss.

**The first consumer must not be a map.** A diagnostics or engineering-status
surface that shows the coordinate together with *both* ages, the validity, the
`gps` key and the node's public key is the right first consumer, because it
makes the uncertainty the subject rather than a footnote. Putting this behind
Navigator first would ship the exact failure §4.1 is written to prevent.

### 6.1 Lifecycle

Semantics for the events that will otherwise be decided by accident:

| Event | Required behaviour |
|---|---|
| link disconnects | availability → `Unreachable`. The last observation is **retained** and its age keeps growing; its validity is `NoFix` already (§4.1) and stays so. It is not *cleared* — that would be the node saying "no", and it never did |
| session reconnects | the coordinate is re-read from the new `RESP_CODE_SELF_INFO`. Nothing survives the session except as an aged observation |
| node identity changes | the pinned-key machinery already refuses this; the retained observation is **discarded**, not re-attributed. A new key is a new node |
| node reboots | invisible to us. Covered by the retained-value rules, not by a reboot signal we do not get |
| permission denied / no `LPP_GPS` record | a normal outcome. Availability stays `Ready`; validity is unaffected; **not** an error to the user |
| timeout on a remote request | one outstanding request, so: fail it, do not retry inside the provider, surface it. Retry policy belongs to the caller |
| identical coordinate read twice | evidence *against* a live fix, not for one. Never refreshes `age_at_source_ms` |
| `gps:0`, or no `gps` key | availability stays `Ready` — the node handed over a coordinate regardless, and the watch cannot bring the node's receiver up. It is **not** `Off`: `core/include/attadipa/core/availability.h:22` — "Off,            // deliberately powered down; can be brought up" — is a remedy this device can perform, and a node provider has none. The receiver's state is a fact about the coordinate; the verdict is `NoFix` regardless (§4.1), so the first consumer (§6) shows the `gps` key itself |
| mid-session refresh | **none in the first slice.** Path A is read once per session and there is no safe re-read (M27); validity is `NoFix` (§4.1) and neither `Stale` nor `Degraded` is produced. A second read happens only at reconnect, and that is a new session, not a refresh |
| two providers disagree | out of scope. Recorded so that the first slice's shape does not foreclose ADR-0011 §5.2 |

## 7. Reuse comparison

| Candidate | Licence | Maintained | What it would give us | Decision |
|---|---|---|---|---|
| MeshCore `LPPDataHelpers.h` | MIT | yes, unchanged since the pin | the authoritative layout | **Protocol evidence only.** §3.4: OOB read and a `uint8_t` cursor that wraps into a false success |
| `electroniccats/CayenneLPP` 1.6.1 (`a83f3e4`) | check `LICENSE.md` | yes, 2026-05-01 | a full encoder/decoder with tests | **Reject as a dependency.** Arduino/ESP-IDF component depending on **ArduinoJson**, and a heap `_buffer` — against Attadipa's no-hot-path-allocation contract. Keep as the **writer of record** for golden vectors: it is the code that produces the bytes |
| `myDevicesIoT/CayenneLPP` | — | **no**, last commit 2018-12-07 | format lineage | **Reject.** Eight years unmaintained |
| `meshcore_py` `664ba0c9`, 2.3.9.1 | MIT | yes, 2026-08-30 | request serialisation, correlation, host fixtures | **Read, do not depend.** Python cannot enter an ESP-IDF image. The single request lock is the idea worth taking; the `/800` timeout constant is not (§5) |
| Zephyr `2f0bc112` GNSS subsystem | Apache-2.0 | yes | one acquisition struct joining navigation and receiver state; a publish seam; `gnss_emul` as a test pattern | **Inspire architecture.** `GnssObservation` already is the joined struct. Take the emulator/replay pattern. **Do not** take `drivers/modem/vendor_standalone/hl78xx/hl78xx_gnss.c`'s suppress-on-no-fix behaviour — it guards `gnss_publish_data()` with `if (fix_status != GNSS_FIX_STATUS_NO_FIX)`, and silence with no explicit transition preserves a stale valid observation, which is precisely §2's defect |
| Meshtastic | GPL-3.0 | — | — | **Rejected by OD-12**, not by licence alone |

## 8. Tests

### 8.1 Host, runnable now

Decoder, if and when §3 is built: golden `LPP_GPS` vectors generated from the
pinned MeshCore writer *and* from ElectronicCats 1.6.1, asserted equal;
truncation at every byte offset from 0 to 10; a record claiming to run past the
buffer; unknown type; channel 0; duplicate `LPP_GPS` on channel 1; `LPP_GPS` on
channel 2; a trailing partial header; the 24-bit extremes ±8 388 607 rejected
before multiplication; ±90/±180 accepted at the boundary; negative coordinates,
the equator and the prime meridian; the truncate-toward-zero bias of §3.2 as an
explicit case rather than an accident of rounding.

Frame parsing, buildable in the first slice — and *above* the length check,
which the companion owns: `link/src/meshcore_companion.cpp:512` — "if (size < 58) { ++malformed_frames_; return false; }"
— drops a `RESP_CODE_SELF_INFO` shorter than the name offset before any
provider sees it. The companion's suite fails closed on a short *contact* frame
(`tests/test_meshcore_companion.cpp:548` — "CHECK(client.malformed_frames() == 1);")
and has no short `SELF_INFO` case; that missing case is the one length test this
plan names, and it belongs in that file, not in the provider's. Bytes 36–43 are
therefore present in every frame the provider is handed, and a 44-byte case in
the provider's tests would go green without the shipping path reaching it
(`../../AGENTS.md:87` — "of a fixture, copied implementation, generated patch, or isolated decision").
The provider's frame cases are value cases: a coordinate of exactly (0, 0), which is legal, plausible and almost
certainly an unset pref; ±90/±180 ×10⁶ at the boundary; a value beyond it, which
`CMD_SET_ADVERT_LATLON`'s own check should make unreachable and which must be
rejected anyway.

Semantics, which are the tests that matter: **every observation classifies
`NoFix`** — §4's observation handed to `classify()` at age 0 and at an hour
returns it both times (`core/src/position.cpp:14` — "return PositionValidity::NoFix;"),
and the fixture asserts that verdict rather than the ladder §4.1 argues for, so
the ADR-0011 amendment cannot land without turning this test red and this
report with it; **no input produces `PositionValidity::Valid`**, none
`Degraded`, none `Stale`; every published `Timed<Position>` carries
`Validity::Unknown`, and the fixture asserts *that* rather than the value of
`age_at_source_ms`, which has no unknown representation (§4.1) — a consumer
that reads the age before the validity is the bug the test exists to catch;
`FixType::Unknown` survives to the consumer; an unchanged coordinate read twice
does not refresh either age; `gps:0` and a missing `gps` key change nothing in
the verdict and reach the consumer as the key itself (§6.1); disconnect retains
and ages rather than clearing.

Replay: a plausible coordinate, then loss of fix, then the same coordinate for
an hour — the fixture must end `NoFix`, must never have been `Valid`, and is
judged by the same classifier (`tests/replay/replay.cpp:484` — "validity = classify(step.observation, step.at, validity_policy);"). A
manually typed coordinate and a GNSS one must produce **identical** observations
except for the `gps` key, and the test asserts that indistinguishability rather
than papering over it.

### 8.2 Simulator

A fake provider drives every `Availability` a node provider can produce —
`Unprovisioned` (no node pinned), `Unreachable`, `Incompatible` (the version
handshake fails), `Failed` (the transport fault: `firmware/main/meshcore_ble.cpp:1274` — "provider.fault(now());"
lands as `link/src/meshcore_companion.cpp:238` — "status_.availability = core::Availability::Failed;")
and `Ready` — and, under `Ready`, `PositionValidity::NoFix` (the first slice's
only verdict, §4.1), plus disconnect and recovery, without the consumer learning which provider
answered. `Degraded` is a validity, not an availability —
`core/include/attadipa/core/position.h:188` — "Degraded,  // usable, with a caveat the interface must show"
— and the two are never folded into one another (ADR-0004 §3); the seven
availabilities must render as seven different sentences, and the fake drives
the five that have a producer. `Unsupported` and `Off` are the two values a node
provider cannot produce: the first is left to the board that has no node at
all; the second has no producer because a node hands over its coordinate with
the receiver off (§6.1, `gps:0`) and the watch cannot bring that receiver up.
`Failed` stays, but its producer is the transport fault named above, not a
short frame: a frame the companion cannot parse is counted at
`link/src/meshcore_companion.cpp:512` — "if (size < 58) { ++malformed_frames_; return false; }"
and never reaches the provider, and a
provider that came up over a link that then sent garbage has not failed to come
up — `Failed` is
`core/include/attadipa/core/availability.h:21` — "Failed,         // bound and reachable; it did not come up".

### 8.3 Physical — `NOT EXECUTED — HARDWARE REQUIRED`

The free bench T114 only — [`TEST_FLEET.md:72`](TEST_FLEET.md)
"Assistant one carries no screen and no GNSS; the free bench one has both;". Do
not disturb the Home Assistant, Room Server or repeater nodes.

Record, per run: the Attadipa full SHA, product/HIL config and toolchain; the
node's hardware revision, band, BLE identity, public key and **the exact
firmware SHA and build environment** — the env matters and the version string
does not encode it; raw redacted `RESP_CODE_SELF_INFO`, `RESP_CODE_CUSTOM_VARS`
and any telemetry frames, with monotonic send and arrival timestamps; every
parse and state transition; RSSI/SNR where offered; battery voltage and reset
reason.

The experiment that decides everything else: open sky until the node's own UI
shows a fix, capture; move indoors until the fix is lost, capture repeatedly for
an hour. **The prediction is that the captures are byte-identical in bytes
36–43.** If they are, §2 is confirmed on hardware and every conservative rule in
§4 is justified by measurement rather than by reading. If they are not, this
document is wrong in a way worth knowing immediately.

Then: `gps:0` versus `gps:1` versus a node with the receiver unplugged, to
confirm §1.3's three states; BLE detach and reconnect; a node reboot, to confirm
that the pre-reboot coordinate returns from prefs.

Node-side fix age, C/N0, HDOP and TTFF are recorded **only** if the node's own
display or an independent interface shows them. They are never inferred from a
coordinate.

**Pass** for the research: the frames match the layouts in §1, and the
loss-of-fix capture is identical to the fix capture. **Fail:** any plan that
labels a remote value current, any decoder that cannot bound a malformed frame,
or any provider that needs a poller.

## 9. What remains UNKNOWN

Filed as [OPEN_QUESTIONS](OPEN_QUESTIONS.md) **M24–M27** so that they are in the
register rather than only in this file.

- **M24** — the bench node's **build environment**. `v1.17.1-d929643` does not
  encode it, and `gps_detected` is a boot-time probe rather than a board fact,
  so nothing read from source predicts whether that unit answers `gps:1`,
  `gps:0` or nothing. One `CMD_GET_CUSTOM_VARS` on the bench settles it.
- **M25** — whether losing the fix really leaves bytes 36–43 unchanged. It is
  the experiment §8.3 is built around and the one every conservative rule in §4
  rests on. Read from source, never observed.
- **M26** — the **altitude datum** in `LPP_GPS`. §3.3.
- **M27** — whether `RESP_CODE_SELF_INFO` can be re-requested safely mid-session
  in practice. The `_iter_started = false` interaction is read from source; its
  practical cost is unmeasured.
- And every number in §8.3, none of which is claimed here.
