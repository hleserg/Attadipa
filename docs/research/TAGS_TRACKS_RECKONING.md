# Smart tags, tracks and dead reckoning — what the sources say

**Written:** 2026-08-21 · **Status:** research record. No code came out of it,
and none should until the questions in §5 have owner answers.

## Why this file exists, and the thing to read first

The owner asked for three capabilities in conversation on 2026-08-21:

1. the watch can pretend to be a smart tag — AirTag, Google's, Samsung's —
   **any of them, in any combination, not one or the other**;
2. it records a breadcrumb track, sends it and receives someone else's, over
   mesh and BLE directly and over the internet as well;
3. the track can be reconstructed from the accelerometer when GNSS is lost —
   *"прошёл столько-то, повернул, прошёл столько-то"* — so that a way back
   exists without a map, and so that an approximate position can be computed
   from the last GNSS fix. Heading by compass points where a magnetometer
   exists.

**None of the three is in the specification.** `docs/master-prompt-final.md`
contains nothing about tag emulation, nothing about track recording, and
nothing about dead reckoning — §23 FIND MY PHONE is the watch ringing the
phone, which is the opposite direction. The pedometer these depend on exists
only as [OD-6](OWNER_DECISIONS.md), not as spec either.

That matters more than it looks. Every sizing decision below is parameterised
by a number nobody has given: how many ecosystems at once, how long a track is,
how far a reckoned path has to stay useful. They compete for one antenna, one
coexistence arbiter and one 940 mAh cell, so they have to be answered together.
The questions are in §5 and they are the owner's.

Evidence labels are the project's own: `PUBLISHED-SPEC`, `VENDOR-STATED`,
`THIRD-PARTY-DOCUMENTED`, `OBSERVED`, `MEASURED`, `ESTIMATED`, `UNKNOWN`.
Nothing here has been near a board.

---

## 1. Smart tags

### 1.1 The short answer: three ecosystems, three different answers

| | Broadcast | Retrieval | Where it stops |
|---|---|---|---|
| **Apple Find My** | `PROVEN` | `THIRD-PARTY-DOCUMENTED`, with a 2026 hardware prerequisite | reachable — but not through Apple's own app, and never as a finder |
| **Google Find Hub** (ex Find My Device) | `LIKELY`, gated | `LIKELY`, gated | Google's approval, an allowlist and a third-party lab |
| **Samsung Find** | `NOT_FEASIBLE` | `NOT_FEASIBLE` | a partner-only SDK that ships for no Espressif part |

"In any combination" is not the binding constraint. **Two of the three are shut
before the radio is involved**, and the radio has room for more sets than there
are ecosystems to fill.

### 1.2 Apple Find My

The advertisement is unauthenticated, and that is the whole reason this works.
A finder iPhone parses a public key out of the payload, encrypts its own
position to that key and uploads it; nothing in the packet says the advertiser
is an Apple product. Heinrich, Stute, Kornhuber and Hollick state it plainly —
*"advertisements only contain the public part of an advertisement key and are
not authenticated"* — and note that Apple confirmed their reverse-engineered
specification afterwards.

- The cryptography is `VENDOR-STATED`, from Apple's own Platform Security
  guide: an EC **P-224** key pair, a 256-bit secret `SK0`, a counter, and
  *"approximately every 15 minutes, the public key is replaced by a new one
  using an incremented value of the counter"*. Derivation
  `SKi = KDF(SKi-1,"update",32)`, `(ui,vi) = KDF(SKi,"diversify",72)`,
  `di = (d0·ui)+vi`, `pi = di·G`, ANSI X9.63 KDF with SHA-256.
- The **byte layout** is `THIRD-PARTY-DOCUMENTED` (PoPETs 2021 §6.2, Table 2):
  37 bytes, company ID `0x004C`, type `0x12`, one status byte, 22 bytes of key,
  non-connectable undirected. Apple does not publish it.
- The **Find My Network Accessory Specification is not obtainable.** The
  developer page returns only "enroll in the MFi Program". It was downloadable
  in 2020 and is not now. Every accessory-specific requirement — the pairing
  flow, the GATT services, the separated/near-owner bits — is therefore
  `UNKNOWN` from a primary source. *Do not accept a quoted service UUID for it
  without a document.*

Three things it can never do, and each kills a plausible product line:

1. **It can never be a finder.** Uploading a report needs a device identity
   certificate and an ECDSA signature whose key lives in Apple's Secure Enclave.
   Attadipa can be found by the network and can never contribute to it. "The
   watch helps find other people's things" is dead on arrival.
2. **It can never appear in Apple's Find My app.** That is the MFi pairing
   flow. An OpenHaystack-shaped beacon is invisible there; the user needs *our*
   server and *our* interface. That is a support burden and a trust story.
3. **It can never be licensed as constituted.** MFi is open to *"companies,
   organizations, government entities and educational institutions"* and
   explicitly excludes *"individuals creating accessories for personal use"*.

Retrieval was originally reported as `PROVEN` and the refutation downgraded it,
correctly. Apple cannot link a key to an account, so any *provisioned* Apple ID
can fetch any report given the public key — but provisioning is no longer
hardware-free. macless-haystack's own FAQ tells users to *"register your account
with a real Apple device"*, and its open PR #240 (2026-07-05) states that a new
Apple ID *"must be initialized on a physical Apple device (iPhone, iPad, or Mac)
by opening the 'Find My' app and accepting its Terms & Conditions. Web or iTunes
login is no longer sufficient"*. `THIRD-PARTY-REPORTED` — a community pull
request, unmerged, not vendor-stated. **The honest cost line is "an Apple ID
bootstrapped on Apple hardware, a self-hosted endpoint and SMS-only 2FA", not
"an Apple ID".**

And the latency is structural, not an implementation detail: median 26 minutes
from generating a report to it being uploaded, *"several hours if the finder
device is in a low power mode"*, seven-day retention, ~10 m urban accuracy. This
is **recovery, not live tracking**, and an empty report list means nothing at
all — not "it is not there".

### 1.3 Google Find Hub

The specification is public and the door is not. A compliant device advertises
Eddystone service data under `0xFEAA` at least once every 2 s carrying a 20- or
32-byte ephemeral identifier. Getting to be compliant requires, in order: a
device proposal form approved by Google, a Developmental NDA, onboarding, and
third-party lab certification. **There is no self-certification path.**

The finding that closes the hobbyist route comes from the silicon vendor's own
working sample rather than from speculation: provisioning *"will fail at the FHN
provisioning stage for the default debug (uncertified) device model"* unless the
tester's account is on Google's allowlist — obtained through the same form.

Rotation here is stricter than "rotate your identifier": the EID rotates on a
period exponent of 10 — every 1024 s on average, randomised by 1–204 s so that a
population does not rotate in lockstep — and *"Fast Pair advertisement, FHN
advertisement and the corresponding BLE address(es) should rotate at the same
time."* Rotating one without the other defeats the scheme entirely.

Licence, and it is decisive: Nordic's nRF Connect SDK is the only complete,
vendor-maintained, openly readable FMDN + DULT implementation. Its headers carry
`LicenseRef-Nordic-5-Clause`, whose clause 4 reads *"This software, with or
without modification, must only be used with a Nordic Semiconductor ASA
integrated circuit."* **Unusable on an ESP32-S3, at any distance.**

### 1.4 Samsung Find

Closed, and closed further than the other two. The legitimate path exists — the
"SmartThings Find Device" product type — behind four gates: a partnership
programme *"available only to registered partners who wish to commercialize"*, an
organisation in the Developer Center, the partner-only Find Device SDK
(published for Nordic nRF52833/52840/54L10/54L15 and Atmosic ATM33 — **no
Espressif part**), and Works With SmartThings certification.

There is no published advertisement specification. And even a byte-perfect
`0xFD5A` advertisement would be inert: the privacy ID derives from key material
the server issues **only at registration**, and registration requires the server
to already hold that device's public key against its hashed serial number. A
privacy ID Samsung cannot map to a registered device maps to no owner and
produces no position anybody can query. Emitting it anyway would be advertising
under another company's SIG-assigned UUID while failing the signature check.

### 1.5 Licences, checked through the GitHub API rather than README badges

Attadipa is `GPL-3.0-or-later`, and nothing incompatible with that licence enters
the repository.

| Project | Licence | Verdict |
|---|---|---|
| `seemoo-lab/OpenHaystack` | AGPL-3.0 | **blocked** — read it, copy nothing |
| `dchristl/macless-haystack` | AGPL-3.0 | **blocked**, firmware and server both |
| Nordic FHN / Fast Pair | `LicenseRef-Nordic-5-Clause` | **blocked** — Nordic silicon only |
| Samsung Find Device SDK | not published | **unknowable** — partnership-delivered |
| `seemoo-lab/AirGuard` | Apache-2.0 | **compatible** — see §1.7 |
| `xioTechnologies/Fusion` | MIT | **compatible** — see §2.5 |
| `Dadoum/anisette-v3-server` | no SPDX, no LICENSE at root | `UNKNOWN` |

### 1.6 Anti-stalking is a requirement, and it points back at us

The IETF DULT accessory protocol — co-authored by Apple and Google, shipped as
unwanted-tracking alerts in iOS 17.5 and Android 6.0+ — is normative in tone and
readable, and is simultaneously an **expired** Internet-Draft that was never
replaced and never became an RFC. What it demands of a compliant accessory
(`PUBLISHED-SPEC`, draft-ietf-dult-accessory-protocol-00):

- separated state: rotate the address **every 24 hours**; near-owner state:
  **every 15 minutes**; and on every state transition (§3.5.1);
- *"The accessory MUST include a sound maker … to play sound when in separated
  state"*, minimum **60 phon** peak loudness per ISO 532-1:2017 (§3.13.3), for
  5–30 s (§3.13.4.1);
- a serial number printed on the accessory, unique per product ID (§3.15.1);
- internal state tracking whether its position is available to its owner (§3.3).

Two things follow, and the second is the uncomfortable one.

**First: fast rotation may defeat the detectors.** A 2022 observation (iOS
15.3.1) tracked a person for five days using 2000 pre-generated keys at one
beacon per key every 30 s with **no alert** to the tracked user; Apple's Tracker
Detect showed nothing, and AirGuard's background scan did not alert because it
*"requires multiple detections of the same public key"*. Whether that still
evades detection in 2026 — after DULT alerts shipped, after Apple's December
2024 patches, after four more years of AirGuard — is **`UNKNOWN` and untested**.
The mechanism makes evasion plausible; plausible is not tested. **A tag that
rotates faster than the detectors sample is a stalking device with a compliance
story, and this firmware has a Child Mode.**

**Second, and nobody in this repository has asked it before:** Attadipa is
itself a device DULT exists to detect. `grep -rni "stalk|tracker detect" docs/`
returns nothing. The product as specified is a wearable that reports a person's
position to a remote party over a mesh, DULT's own scope enumerates "Watch" as
an accessory category, and the track exchange in §3 is a location-sharing
channel that has never been read against a threat model. Filed as **T-069**.

Neither has the law: a six-year-old's position leaving the device engages GDPR
Article 8, the UK Age Appropriate Design Code and COPPA. Google scoping Find Hub
to "age-eligible users" was read as a feasibility signal; it is a hint that the
law here is specific. Also **T-069**.

### 1.7 Two things worth more than emulation, and neither needs anybody's permission

**The cheap baseline nobody costed.** The alternative to being found by hundreds
of millions of iPhones is: *the companion phone remembers where it last saw the
watch over BLE, and the watch remembers the phone.* No Apple ID, no MFi, no
Google proposal form, no Samsung partnership, no reverse-engineered protocol,
no other company's SIG identifier, no server. It is also the only variant that
works with the companion this repository has already specified
([COMPANION_PROTOCOL](../mobile/COMPANION_PROTOCOL.md)). **It should be
evaluated before any of §1.2–§1.4.** — **T-063**.

**The inverse feature.** Attadipa as a tracker *detector*: scanning for an
unknown BLE identifier that has been near the wearer all afternoon. For a
child-worn device this protects the wearer rather than exposing them, needs no
ecosystem's approval, uses a radio the watch certainly has, and AirGuard is
Apache-2.0 and actively maintained. **T-070**.

### 1.8 The radio, if it ever comes to that

| Quantity | Value | Label |
|---|---|---|
| Advertising instances, NimBLE (IDF 5.5.5 / 6.0.2) | **5** — `CONFIG_BT_NIMBLE_MAX_EXT_ADV_INSTANCES` 0–4, plus the always-present one | `VENDOR-STATED` |
| Advertising instances, Bluedroid | **10**, fixed at compile time | `VENDOR-STATED` |
| Controller activity pool (shared with connections, scan, periodic sync) | default **6**, max **10**, 828 B each | `VENDOR-STATED` |
| Per-set interval range, extended advertising | 20 ms … ~10 485 s | `VENDOR-STATED` |
| ESP32-S3 peak current, BLE TX @ +9 dBm | 193 mA **peak** | `PUBLISHED-SPEC` |
| ESP32-S3 light sleep | 240 µA typ; +140 µA with 8 MB octal PSRAM @ 3.3 V | `PUBLISHED-SPEC` |
| NimBLE advertising, PM enabled | 17.9 mA modem sleep · 3.3 mA light sleep on main XTAL · **230 µA** light sleep on 32 kHz XTAL | `VENDOR-STATED` |
| Radio-only average, four sets at 1 s, +9 dBm | ~0.87 mA — **lower bound only** | `ESTIMATED` |
| Actual average for N sets on either board | — | **`UNKNOWN`** |
| Wi-Fi / BLE RF time split when both are active | 50 % each | `VENDOR-STATED` |

Read the last two rows together. The instance count is not the problem; the
`UNKNOWN` is. The ~0.87 mA figure excludes PLL settling, controller wake, host
CPU time and the sleep floor — **which dominates**. The ~14× lever between
3.3 mA and 230 µA is whether the SoC can sleep on a 32 kHz clock. T-068 / #268
resolved the documented designs: neither cited vendor schematic connects one.
On the T-Watch drawing, PCF8563 `RTC_CLKOUT` terminates at test point `TP66`; on
the Waveshare drawing, PCF85063ATL `CLKOUT` is unconnected. The old `R126`
statement was unrelated to the RTC. External RTC clock modes therefore remain
unauthorized and T-167 keeps `INT_RC`; physical continuity and actual board
current remain `UNKNOWN`. See [RTC_SLOW_CLOCK](RTC_SLOW_CLOCK.md).

Two further constraints that would bite a compliant implementation: Google's
mandated *"at least once every 2 s"* cadence against ESP-IDF's own statement
that during Wi-Fi activity *"in every N BLE Advertising events, there is always
one event with high priority"* — N unpublished; and key storage, which both
working reference implementations solve by pre-generating keys on a host and
flashing them into a dedicated partition (2000 keys in one build; Apple's own
retention implies 672). No byte figure exists, and the partition table is
`RESOURCE_BUDGET.md`'s *"an ADR, not an accident"*.

---

## 2. Dead reckoning

### 2.1 The number that decides the whole question

An **uncalibrated** QMI8658 gyroscope has a board-level offset tolerance of
**±10 dps** (`PUBLISHED-SPEC`). That is 600°/minute: **a full 360° of heading
error in 36 seconds** (`ESTIMATED`, arithmetic). Thermal drift alone, over a
10 °C swing at ±0.05 dps/°C, is 30°/minute. The vendor's own system-level figure
for unreferenced yaw drift is 5–25°/h *"from Allan Variance bias instability"*.

Time to accumulate just **5°** of heading error, by residual bias
(`ESTIMATED`): 0.1 dps → 50 s · 0.05 dps → 100 s · 25°/h → 12 min · 5°/h → 60 min.
Cross-track position error at 1.4 m/s with 0.1 dps residual: 4.4 m at 60 s,
**110 m at 300 s**.

For scale, the best case in the no-magnetometer literature uses hardware this
product does not have — foot-mounted, eight IMUs, a zero-velocity update **every
step** — and still reports 14° of heading error over 1018 m
(`THIRD-PARTY-DOCUMENTED`).

There is one wrist-specific primary source, and it is better than expected: Park
et al., IEEE Access 2024, report mean **walking-direction** errors of **5.58°
and 6.07°** against INS/GNSS RTK ground truth, with the authors stating the
method *"can be equally effective indoors as the inertial sensor itself does not
rely on external infrastructure"* (`THIRD-PARTY-DOCUMENTED`). That is a result
worth taking seriously — and it is one paper, on their subjects, with their
mounting, and it is not a shipped product.

**Conclusion, and it is the honest answer to *"повернул"*:** on the T-Watch
there is no gyroscope, so a turn is not merely inaccurate — it is **not
observable at all**, and the reckoned track is a *length, not a shape*. On the
Waveshare a turn is observable for a bounded window whose length is set by
calibration quality and thermal stability, and that window is minutes, not
hours. Neither board can hold a shape without an absolute reference, and neither
board has one.

This is the same conclusion [ADR-0009](../adr/0009-heading.md) reached about
heading and that [#21](https://github.com/hleserg/Attadipa/issues/21) has just
removed from the capability registry. **Dead reckoning must not bring it back
through a side door: DR consumes odometry, an anchor and — where it exists —
`Heading`. It never manufactures one.**

### 2.2 Steps, per part

**BMA423** (T-Watch), all `PUBLISHED-SPEC` from the datasheet:

- 32-bit step counter, `STEP_COUNTER_0..3`, POR default 0;
- watermark has an implicit ×20 factor, range 0–20460, resolution 20 steps;
- feature engine wants **50 Hz**; below it, `INTERNAL_STATUS.odr_50hz_error`;
- a **6144-byte** configuration blob must be uploaded after **every** POR or
  soft reset, with a 140–150 ms settling time;
- **14 µA** typical in low-power mode at 50 Hz — the always-on case;
- FIFO 1024 B ≈ 170 tri-axial samples ≈ 3.4 s at 50 Hz;
- **no published accuracy figure** — rev 1.0 offers only the prose *"optimized
  on high accuracy"*. `UNKNOWN`.

**QMI8658C** (Waveshare) — and the variant matters. The schematic names
`QMI8658C` twice, and the vendor wiki's datasheet link is byte-identical to the
C document. **The `QMI8658A`'s pedometer registers `Pedo_EN` / `STEP_CNT_*`
apply to a part that is not on this board.** The C's `CTRL8` is *"Reserved: Not
Used"*, so **no hardware pedometer is documented**: `UNKNOWN`, leaning absent —
not "provably absent", because the only obtainable C datasheet is Rev 0.6 of
January 2021 marked *"ADVANCE INFORMATION"*.

Correspondingly, C figures only: noise density **15 mdps/√Hz** (not the A's 13),
TCO **±0.05 dps/°C on all axes** (not per-axis), ARW ≈ 0.90°/√h
(`ESTIMATED` from the noise density).

**The power asymmetry is the design constraint.** Accelerometer-only low power:
30 µA at 3 Hz to 55 µA at 128 Hz. Gyroscope: **651 µA at 28 Hz rising to
908 µA**. Running the gyroscope for heading costs roughly **46× the BMA423's
always-on step counting**. A DR mode that leaves the gyroscope on is a different
power product, and it must be a mode the user enters, not a background service.

### 2.3 How well steps and stride actually work

Wrist step detection, published algorithms on identical free-living data
(`THIRD-PARTY-DOCUMENTED`): from **12.5 % MAPE** at best (recall 98.7 %,
r = 0.98) through 63.5 % to **231 %** at worst. Laboratory walking: 16.5 %
overall, 9.2 % during regular walking. *The spread between algorithms is larger
than the error of the good ones* — which is to say the algorithm choice is the
whole result, and "we count steps" is not a specification.

Stride length has three published models: constant-height `s = k·h`
(k = 0.415 / 0.413), Weinberg `s = k_w·(f_max − f_min)^¼`, and a three-gain
adaptive model. Distance error, **calibrated**: **< 2 %** of distance travelled.
**Uncalibrated** height model, same worked example: −20 %.

That 10× gap is the argument for calibrating stride against GNSS while GNSS is
good — which is free, continuous and per-user. Until enough good GNSS distance
has been seen, the stride is `Uncalibrated` and says so, rather than reporting an
invented number.

### 2.4 The honest output is a disk, and something has to expire it

With no heading, the position extrapolated from an anchor by odometry alone is

> **a disk** centred on the last trusted fix, radius `R = r₀ + d̂·(1 + ε)`,
> where `r₀` is the anchor's own uncertainty, `d̂` is steps × stride and `ε` is
> the stride error bound — ~2 % calibrated, ~20 % uncalibrated.

Walk 300 m with nothing measuring direction and the honest statement is
"somewhere within 300 m of the anchor". That is not a failure; it is the only
non-lying thing available, and it can be drawn.

Two things the research did **not** settle and that must be decided before this
is built:

- **What expires the disk.** [ADR-0011](../adr/0011-gnss-integrity.md) already
  requires that *"a position that was good sixty seconds ago is a circle, not a
  point"* — but nothing says when the circle stops being drawn at all. The bound
  above is silently false the moment the wearer boards a bus: steps stop, the
  distance travelled does not.
- **It is two devices, not one.** Per [OD-1](OWNER_DECISIONS.md) the GNSS is on
  the node, the IMU is on the wrist, and the Waveshare — the board that most
  needs a node — has no GNSS at all. So the anchor and the step count come from
  two bodies joined by a link that can drop, and the stride-calibration path
  crosses it. A node in a pocket and a watch on a swinging wrist are not
  co-located and do not move identically.

### 2.5 `xioTechnologies/Fusion`

`CLAUDE.md` requires reading it before anyone writes a Madgwick filter, so it
was read. **MIT, © 2021 x-io Technologies — compatible.** Bias algorithm
defaults: stationary threshold 3 dps, stationary period 3 s, sample rate 100 Hz,
offset persistable through `FusionBiasGetOffset` / `FusionBiasSetOffset`. Gain
ramps from 10 to the configured value over the first 3 s; overrange recovery at
98 % of `gyroscopeRange` **restarts the algorithm**.

It is a competent AHRS and it does not solve this problem: without a
magnetometer, yaw is unobservable, and no filter makes it observable. What it
*does* offer that is directly useful is the stationary-bias machinery — the
lever that turns ±10 dps of uncalibrated offset into a residual small enough for
the table in §2.1 to be survivable. **Reuse ledger record: T-067.**

---

## 3. Tracks

### 3.1 The number that decides this one

MeshCore, all `PUBLISHED-SPEC` from its source:

| Field | Bytes |
|---|---|
| `MAX_PACKET_PAYLOAD` | **184** |
| `MAX_TRANS_UNIT` (on air) | 255; worst case actually used 254 |
| `GRP_DATA` net application bytes | **165** |
| `TXT_MSG` text | 160 |
| `RAW_CUSTOM` | 184 — **unencrypted, unauthenticated, direct-route only** |
| companion frame (USB / BLE / UART) | 176 |
| BLE notification payload | 173 (ATT_MTU 176 − 3) |
| BLE frame queue depth | **4** each direction — *ESP32 implementation*; the nRF52 one is 12 |
| duplicate-suppression ring | 160 entries × 8 B |

Attadipa's own node link is already decided at `kMaxPayload = 192` with 7 bytes
of framing and `kDefaultQueueDepth = 4`.

Time on air at the project's stated LoRa parameters (868.731 MHz, BW 62.5 kHz,
SF 7, CR 4/5), reproduced from RadioLib's own integer implementation and
therefore `ESTIMATED, NOT EXECUTED — HARDWARE REQUIRED`: symbol 2.048 ms, a
181-byte packet **633.3 ms**, a 255-byte packet 848.4 ms.

Which gives the sentence that decides the design:

> A **1000-point** track costs **26 packets and 16.5 s of originator airtime**
> at 4 bytes per point, or 53 packets and 33.6 s at 8. A **3600-point** track:
> 93 packets / 58.9 s, or 190 packets / 120.3 s.

MeshCore's companion build sets `airtime_factor = 1.0`, i.e. a **50 %** duty
cycle over a one-hour window — looser than any sub-GHz regulatory limit, so
Attadipa must still enforce whatever [OPEN_QUESTIONS](OPEN_QUESTIONS.md) A4
resolves to. It cannot claim the node imposes nothing.

**Caveat that outranks the table:** every LoRa figure describes a radio that is
not established. [ADR-0003](../adr/0003-radio-not-lora.md) says the Waveshare
has no LoRa and the T-Watch ships one of five chips of which the pinned
MeshCore supports one; the node's hardware is entirely `UNKNOWN`
(NODE_PROFILE N1).

### 3.2 Encoding

| Format | Bytes per point | Label |
|---|---|---|
| varint + zigzag @ 1e-7° | 1 if \|Δ\| ≤ 0.70 m; 2 if ≤ 91 m | `PUBLISHED-SPEC` |
| varint + zigzag @ 1e-5° | 1 if \|Δ\| ≤ 70 m; 2 if ≤ 9.1 km | `PUBLISHED-SPEC` |
| Google encoded polyline | 1 char if ≤ ~16.7 m; 2 if ≤ ~568 m | `PUBLISHED-SPEC` |
| FIT record | 9 B (13 with time, 15 with altitude); resolution ~9.3 mm | `VENDOR-STATED` |
| CayenneLPP GPS | 11 B, resolution ~11.1 m | `VENDOR-STATED` |
| GPX 1.1 `trkpt` | 38 B; 93 B with elevation and time | `ESTIMATED` |
| TCX `Trackpoint` | 204 B | `ESTIMATED` |

Walking at 1.4 m/s sampled every second puts consecutive points ~1.4 m apart,
which is *just* outside the 1-byte window at 1e-7° and comfortably inside it at
1e-5°. **The resolution choice and the sampling rule are the same decision**,
and neither should be made without the track-length number from §5.

Base64 costs +33 % on the IP carrier. GPX and TCX are interchange formats for
the far end, not wire formats for a mesh; the ratio to varint is 20–100×.

### 3.3 Simplification, and the one that fits a microcontroller

Douglas-Peucker is O(n²) worst case (O(n log n) with path hulls) and **needs the
whole track in memory**: 3600 points is 28.8–43.2 kB plus recursion. On a device
that is recording continuously, that is the wrong shape.

**Zhao-Saalfeld sleeve-fitting is linear time and explicitly does not require
storing all the original data at once** — per-sleeve state is a two-angle sector
bound plus the anchor. That is the microcontroller answer, and it is *online*,
so the same routine can decimate while recording rather than only before
sending. Opening-Window is the other online candidate, at O(N²).

Licences **unchecked** for `psimpl`, `simplify-js` and Zhao-Saalfeld's reference
code. Nothing may be depended on until they are — **T-067**.

### 3.4 What the research did not settle

- **A half-received track.** Nothing says what one looks like or whether the
  receiver knows it is half. A track that arrives simplified must record *that
  it was simplified*, or the map lies quietly — the same failure class as a
  confident arrow.
- **Timebase and datum.** A track is points plus time and nobody said what
  either means: two different RTC parts, GNSS supplying a different and better
  time, `COMPANION_PROTOCOL` §7 listing *"max clock step without confirmation —
  to be set"*, and no statement of which datum the coordinates are in or whether
  altitude is ellipsoidal or geoidal.
- **The internet leg**, which was researched least and has the most unbuilt
  infrastructure behind it: a server, an account model, TLS and certificate
  storage on a constrained device, a retention policy, an operator, a privacy
  policy. None exists, and [ADR-0002](../adr/0002-companion-is-optional.md)
  makes the phone optional, so the watch cannot assume the phone is the internet.
- **A track at rest is a movement record**, and nobody treated it as sensitive.
  What happens when the watch is lost, stolen, sold or reset — which is
  pointedly the same scenario §1 exists to serve.
- **Resumption.** A transfer in progress when the node goes out of range is one
  of the two most concrete instances this project has produced of ADR-0004's
  seven availability states, and neither survey mentioned partial state or what
  the user is told.

---

## 4. What is testable without a board, and what is not

The Definition of Done requires host tests and honestly-marked hardware tests,
and these three features divide cleanly. Nobody had drawn the line, so:

**Host, today, deterministically** — the track codec; the simplifier; the
chunking and resumption state machine; the confidence-disk arithmetic; the step
detector against a recorded acceleration trace. The replay rig already has the
right shape for the last two and is the highest-leverage piece here: extend it
to carry IMU samples and the whole reckoning path becomes testable without
hardware, the same way `classify()` already is.

**Golden test vectors** are the only way to test a wire format without a radio,
and none have been proposed.

**Hardware only, and stays `NOT EXECUTED — HARDWARE REQUIRED`** — the beacon
itself; the advertising-set count in practice; cadence under Wi-Fi coexistence;
and every power figure without exception.

---

## 5. What the owner has to answer before any of this is built

These are one question in three parts, because the three features compete for
one antenna, one coexistence arbiter and one 940 mAh cell — and nobody has
summed them. In combination: N tag ecosystems each want an advertising set, the
node link wants a standing GATT connection, a track transfer wants the mesh or
BLE, and all of it shares a controller activity pool that defaults to 6 and
hard-caps at 10.

1. **Is "the watch can be found by a crowd-sourced network" a product
   requirement?** If yes: which network, and how many at once? Note that only
   Apple is reachable, that it costs an Apple ID bootstrapped on Apple hardware
   plus a self-hosted endpoint, that the watch will never appear in Apple's own
   app, and that §1.7's companion-remembers-last-sighting baseline delivers much
   of the value for none of that.
2. **How long is a track?** An hour, a day, a multi-day route? It decides
   whether the mesh carrier is 13 packets or 190, whether the simplifier can be
   batch or must be online, and whether coordinates are 1e-5 or 1e-7.
3. **How far does a reckoned path have to stay useful, and for what?** "Get me
   back to the tent" and "reconstruct my route" have different answers, and only
   the first survives §2.1.

Also open and recorded here so they are not rediscovered: the battery-life
acceptance criterion does not exist ([OPEN_QUESTIONS](OPEN_QUESTIONS.md) Q3), the
Waveshare's capacity is `UNKNOWN` (D2), and Child Mode §49's *"direction toward
parent/known point where data permits"* has never been reconciled with
ADR-0009's honest fallback — which, for a six-year-old, is an adult
map-reading affordance.

---

## Sources

Primary sources are cited inline at the claim they support. The full research
run, including the adversarial refutation of every claim that would become a
design commitment and the four claims it downgraded, is reproducible from this
file's tasks; where a refutation changed a number, **this file carries the
corrected one** and says so.
