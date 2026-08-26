# The watch as a tracker detector — what the sources say

**Written:** 2026-08-22 · **Status:** research record for T-070. No detector
code came out of this task, and none should until T-070 is picked up as
implementation. Feeds [T-069](#relationship-to-t-069), which is the other
direction of the same question.

## Why this file exists

[`TAGS_TRACKS_RECKONING.md`](TAGS_TRACKS_RECKONING.md) §1 already declined
smart-tag emulation — the watch will not pretend to be an AirTag, a Find Hub
tag or a SmartTag — and named the opposite feature in §1.7: scan for an
unknown BLE identifier that has stayed near the wearer for an implausibly long
time, and say so. The owner asked for that feature directly on 2026-08-22
(issue #45), naming `seemoo-lab/AirGuard` as prior art and requiring the
honest limit — a detector keyed on a repeated identifier can be evaded by
rotation — to be established rather than assumed either way.

This file is that establishment. Six questions, each with sources and this
project's evidence-label vocabulary: `PUBLISHED-SPEC`, `VENDOR-STATED`,
`THIRD-PARTY-DOCUMENTED`, `OBSERVED`, `MEASURED`, `ESTIMATED`, `UNKNOWN`. As
elsewhere in this project, an unsourced `SUPPORTED` is not an answer and
`UNKNOWN` is a correct one.

---

## 1. DULT as it actually stands in 2026

### 1.1 Still not an RFC, and the accessory-protocol draft is still expired

`draft-ietf-dult-accessory-protocol-00` is the only revision ever published,
dated 2024-11-03, and IETF Datatracker marks it *"Expired & archived"*. It has
never been replaced or published as an RFC. This confirms
`TAGS_TRACKS_RECKONING.md` §1.6 and adds nothing new to it.
`PUBLISHED-SPEC` — https://datatracker.ietf.org/doc/draft-ietf-dult-accessory-protocol/

The working group is not dormant, though: `draft-ietf-dult-threat-model-05` is
an **active** document, latest revision 2026-08-06. It is the document most
directly relevant to T-069 (Attadipa is itself a device DULT exists to
detect) and has not yet been read in full — that reading belongs to T-069,
not to this file. `draft-ietf-dult-finding-01` is separately expired.
`PUBLISHED-SPEC` — https://datatracker.ietf.org/wg/dult/documents/

### 1.2 The editors' working copy is alive, and materially newer than the draft

The working group's GitHub repository `ietf-wg-dult/accessory-protocol` has
commits after the draft expired — most recently 2026-08-21, adding a
"ProductInfo network interface" section, and a March 2025 commit copying in
NFC requirements from the Apple+Google specification. `PUBLISHED-SPEC`
(it is the WG's own repository, named in the draft's front matter) — GitHub
API against `ietf-wg-dult/accessory-protocol`, branch `main`. The declared
`latest:` rendered-HTML URL in the draft 404s; the Markdown source is the only
readable current text.

**This is an editors' copy, not WG consensus and not a published spec.**
Treat it as the current intent of the Apple and Google authors (Brent Ledvina
and Ben Detwiler for Apple, David Lazarov and Siddika Parlak Polatkan for
Google), not as normative.

### 1.3 What a compliant accessory broadcasts, and it is unchanged since the expired draft

From the editors' copy, cross-checked byte-for-byte against the expired `-00`
text (identical):

| Bytes | Content | Requirement |
|---|---|---|
| 0–5 | MAC address | REQUIRED |
| 6–8 | Flags TLV | OPTIONAL |
| 9–12 | Service Data TLV, type `0x16`, value **`0xFCB2`** | **REQUIRED** |
| 13 | Network ID (per-manufacturer) | REQUIRED |
| 14 | **Near-owner bit** (LSB) + 7 reserved | REQUIRED |
| 15–36 | Proprietary company payload | OPTIONAL |

`0xFCB2` is a Bluetooth SIG member UUID assigned to **Apple Inc.**
(`PUBLISHED-SPEC`, SIG assigned-numbers registry) — the "cross-industry"
service-data value sits inside Apple's own SIG allocation. Bit 0 of byte 14 is
`1` for near-owner, `0` for separated; a scanner keys on `== 0` for
"separated, therefore possibly stalking".

Other normative numbers, all `PUBLISHED-SPEC` from the same document and
unchanged from the expired draft:

| Requirement | Value |
|---|---|
| Address rotation, near-owner | every **15 minutes**, SHALL |
| Address rotation, separated | every **24 hours**, SHALL |
| Rotation on state transition | SHALL, both directions |
| Advertising interval | **≤4 s SHALL, ≤2 s SHOULD** |
| Advertise if owner had location in past 24 h | SHALL |
| Sound maker | MUST, ≥60 phon peak (ISO 532-1:2017), 25 cm free field |
| Motion detector | SHOULD; if accelerometer, MUST detect ±10° change on two axes |
| `T(SEPARATED_UT_TIMEOUT)` | random 8–24 h before enabling motion detector |
| `T(SEPARATED_UT_SAMPLING_RATE1 / RATE2)` | 10 s / 0.5 s |
| `T(SEPARATED_UT_BACKOFF)` | 6 h |

Directly relevant to T-069: the Accessory Category table assigns **`Watch` =
value 146** and `Location Tracker` = 1. A wearable is explicitly in the
protocol's own scope.

Added since the expired draft (2026-07-23): an AccessoryInfo HTTP API with
real endpoints — `https://findmyut.apple.com/productinfo` and
`https://spot-pa.googleapis.com/lookup`. `PUBLISHED-SPEC` for the text;
**`UNKNOWN`** whether these are reachable by an unauthenticated third-party
client — not probed, and the draft does not say.

### 1.4 What is actually deployed in 2026 is not `0xFCB2` — the gap this project must not paper over

Google's own support page: *"Unknown tracker alerts currently work with Find
Hub network compatible tags, headphones, and Apple AirTags."* `VENDOR-STATED`
— https://support.google.com/android/answer/13658562

Apple's own support page scopes alerts to AirTag, Find My network accessory
program members, and specific AirPods models, requiring iOS/iPadOS 17.5+.
`VENDOR-STATED` — https://support.apple.com/en-us/119874

Apple's 2024 announcement of the joint capability names the manufacturers
that *"have committed that future tags will be compatible"*: Chipolo, eufy,
Jio, Motorola, Pebblebee. `VENDOR-STATED` —
https://www.apple.com/newsroom/2024/05/apple-and-google-deliver-support-for-unwanted-tracking-alerts-in-ios-and-android/

Google's separately-published Find Hub Network (FMDN) accessory
specification requires service UUID `0xFEAA`, EIDs rotating on a 1024 s
period, advertising at least every 2 s, and states FHN devices must
additionally meet DULT. `PUBLISHED-SPEC` —
https://developers.google.com/nearby/fast-pair/specifications/extensions/fmdn

**What a third-party scanner can key on today is therefore two different
answers, and only the second is useful in 2026:**

1. Future DULT-compliant accessories: `0xFCB2` service data, near-owner bit
   `== 0`. Clean, single filter. `PUBLISHED-SPEC`.
2. Everything actually shipping: ecosystem-specific identifiers, because no
   primary source shows a shipping product advertising `0xFCB2` today.
   **`UNKNOWN`** — what would settle it is a BLE sniffer capture of a
   2025/2026-manufactured Chipolo, Pebblebee or Motorola tag. That is an
   `OBSERVED`-grade test needing a device, not a document.

### 1.5 Apple did not open a non-MFi accessory route

`developer.apple.com/find-my/` still directs manufacturers to *"enroll in the
MFi Program"*; there is no downloadable accessory specification and no
non-MFi route. `VENDOR-STATED` (absence, checked on the vendor's own page,
2026-08-22). No primary source was found for reports that Apple opened this
up in 2024–2025 — the only thing that opened up is the DULT accessory
protocol itself, which is a detection-side specification, not an
accessory-authoring one. `TAGS_TRACKS_RECKONING.md` §1.2's conclusion stands
unchanged.

---

## 2. AirGuard's method, read from its own source

Repository `seemoo-lab/AirGuard`, HEAD `7f71a37d0776acc5f0e8d3046d3daaf8b71ad58d`
("AirGuard 3.1.1", 2026-07-20), not archived, actively pushed.

### 2.1 Licence, read from the file

`LICENSE` at the repository root is the verbatim Apache License 2.0; GitHub's
licence API agrees (`spdx_id: Apache-2.0`). `PUBLISHED-SPEC` — the ledger
rule of reading the file rather than a badge, satisfied. **Compatible with
Attadipa's `GPL-3.0-or-later`** — permissive and GPLv3-compatible, but requires
retaining the Apache notice for anything actually taken. Copyright holders per `CITATION.cff`:
Niklas Bittner, Alexander Matern, Dennis Arndt, Matthias Hollick (SEEMOO, TU
Darmstadt).

**Caveat that matters for reuse:** AirGuard is Android/Kotlin. Nothing in it
is firmware-reusable code. What is reusable is the algorithm and its
constants — facts about a detection policy, not code to link.

### 2.2 What it scans for

Ten `ScanFilter`s, one per ecosystem, read from `DeviceManager.kt` and each
type's file: Apple manufacturer ID `0x4C` with byte masks distinguishing
AirTag / Find My / AirPods / a lost Apple device by the status byte; Google
Find My Device Network via service data UUID `0xFEAA`; Samsung SmartTag
(`0xFD5A`) and Find My Mobile (`0xFD69`); Tile (`0xFEED`); Chipolo (`0xFE33`);
Pebblebee (`0xFA25`, **not present in the Bluetooth SIG member registry** —
`UNKNOWN` provenance, used anyway). `OBSERVED` (read from the pinned source),
not `PUBLISHED-SPEC` — Apple's byte layout in particular is reverse-engineered
by SEEMOO, not vendor-published.

**There is no `0xFCB2` filter anywhere in AirGuard 3.1.1.** The Apache-2.0
reference implementation of tracker detection does not yet scan for the DULT
service-data UUID, which is indirect but consistent evidence that nothing
ships with it (§1.4).

### 2.3 Thresholds — the full rule, from `RiskLevelEvaluator.kt`

A device is flagged **MEDIUM** when all hold: first discovery older than
30/60/120 min (high/medium/low sensitivity); at least **3 sightings** within
14 days; at least **2–4 distinct locations** (Tile overridden upward to 3–5,
because its MAC is static and false positives are likelier); no user-marked
false alarm for that address in the window; and the time span between first
and last sighting is at least 30 min (Apple lost-device entries overridden to
2.5 h). **HIGH** additionally needs 2 notifications within 5 days and enough
locations with ≤100 m accuracy. `OBSERVED` —
`app/src/main/java/de/seemoo/at_tracking_detection/util/risk/RiskLevelEvaluator.kt`,
`device/DeviceContext.kt`, `device/types/AppleDevice.kt`,
`device/types/Tile.kt`.

### 2.4 How it avoids flagging a shop full of stationary beacons

Three mechanisms, each read from source:

1. **A sighting only counts as a new *location* if the phone has moved ≥150 m**
   (`MAX_DISTANCE_UNTIL_NEW_LOCATION`). A stationary shop beacon, or a
   neighbour's AirTag through a wall, accumulates beacons but never a second
   location, and the location count is what the risk evaluator reads.
2. **Owner-proximity filtering.** For Apple Find My, a manufacturer-data byte
   distinguishes "connected to its owner nearby" from "overmature/offline";
   AirGuard's own FAQ: *"Connected devices are usually traveling with their
   owner, so they aren't a tracking threat."*
3. **Altitude gates** (`IGNORE_DEVICE_ABOVE_ALTITUDE = 9000 m`,
   `IGNORE_LOCATION_ABOVE_ALTITUDE = 3000 m`) — the aeroplane case; a release
   note credits it with fewer false alerts in flight.

`OBSERVED` + `VENDOR-STATED` (their own FAQ and release notes). They concede
false positives openly: *"someone next to you might use a Tracker (e.g.
someone in public transport)"*.

### 2.5 Detection window and interval

Periodic scan every **15 minutes**, each scan **20 s** at
`SCAN_MODE_LOW_LATENCY` (or 30 s at `SCAN_MODE_LOW_POWER` if the user opts
in) — roughly **2.2 % wall-clock duty cycle** before Android's own
within-scan duty cycling is applied on top. Beacon de-duplication window 15
min. `OBSERVED` — `worker/WorkerConstants.kt`,
`detection/BackgroundBluetoothScanner.kt`. Their own claim: *"If a device
follows you, you will get a notification in less than an hour"* —
`VENDOR-STATED`, not independently verified.

### 2.6 What it admits it cannot catch — verbatim, from its own in-app documentation

`ui/dashboard/articles/en/limitations_of_the_app.md`, read at the pinned
commit:

> "AirGuard can only find Bluetooth-based trackers. Trackers that work with
> GPS and share the location with the owner via a cellular connection cannot
> be found."

> "Some Bluetooth trackers change their identity (Bluetooth MAC address)
> regularly — sometimes multiple times a day. AirGuard may display the same
> tracker as multiple different entries over time because it is not always
> possible to match these randomized identities."

> "It is not currently possible for all tracker types to determine whether
> they are connected to their owner. If a tracker does not support this
> feature (e.g., Tile), AirGuard cannot filter it out even if the owner is
> nearby."

And in-string, shown to the user directly: *"Please be aware that this
tracker could change its identifier. Therefore, it may still follow you even
if we cannot detect it."* `VENDOR-STATED` — the project's own documentation
admits the exact limit this file exists to establish, rather than this
research inferring it.

### 2.7 The one rotation-stitching mechanism it has, and how narrow it is

A "15-minute algorithm" links successive rotated identities, but it is scoped
to **Samsung only** (`savedDeviceTypesWith15MinuteAlgorithm`), and its strict
mode additionally requires the SmartTag's aging counter to increment by
exactly 1 between sightings. Every other ecosystem — including Apple Find My
and Google FMDN — is tracked by **raw BLE MAC address alone**
(`getUniqueIdentifier()` default). `OBSERVED` —
`device/DeviceManager.kt`, `device/BaseDevice.kt`,
`device/types/SamsungTracker.kt`. A 2025-03-17 release note claims *"now
detecting trackers that try to evade some tracking detection
strategies"* — `VENDOR-STATED`, and scoped to the same Samsung mechanism by
the source above it. **A device that rotates its MAC faster than AirGuard's
correlation window, and is not a Samsung tag with an aging counter, is not
stitched together by AirGuard 3.1.1.**

### 2.8 The papers

Primary: Heinrich, Bittner, Hollick, *"AirGuard – Protecting Android Users
From Stalking Attacks By Apple Find My Devices"*, ACM WiSec 2022, DOI
`10.1145/3507657.3528546` (arXiv:2202.11813), Best Paper Award.
`THIRD-PARTY-DOCUMENTED`.

Follow-up, and the one that matters for §3: *"Okay Google, Where's My
Tracker? Security, Privacy, and Performance Evaluation of Google's Find My
Device Network"*, PoPETs 2025(4) —
https://petsymposium.org/popets/2025/popets-2025-0147.pdf — the work that
added Google-tracker support to AirGuard.

An iOS sibling, `seemoo-lab/AirGuard-iOS`, exists but was not read; per a
2026 paper's own remark, iOS restricts third-party BLE MAC-address access, so
its detection capability is materially weaker in a way this file cannot
quantify. **`UNKNOWN`** in detail.

---

## 3. Is the fast-rotation evasion from 2022 still viable in 2026?

**Yes — and it is no longer a single 2022 observation. It has been
independently reproduced twice, in 2025 and 2026, both times using an ESP32
as the rogue tracker.**

### 3.1 PoPETs 2025 — reproduced against Android 14 and iOS 18.3

§7.1.4 of the PoPETs 2025 paper above, verbatim: *"If the MAC address does
not stay static but keeps rotating when UT mode is enabled, those algorithms
cannot identify the tracker over longer periods. The default MAC address
rotation interval is 1024 seconds, which is too short to trigger a tracking
notification in any of the anti-tracking algorithms [including AirGuard].
Therefore, implementing a custom tracker that continuously rotates its MAC
address would be undetectable. We successfully tested this attack by moving
with an ESP32 running a modified version of our firmware… and a Galaxy S21
Ultra with Android 14 (Patch 2025.02) and an iPhone Xs running iOS 18.3."*

The same paper documents three further circumvention paths, all
experimentally validated with off-the-shelf hardware (a Pebblebee Clip, an
ESP32): keeping unwanted-tracking mode disabled by spoofing an owner location
report every ≤30 min; registering the tracker with an invalid UT key so it
refuses activation; and always advertising the UT-mode bit as disabled, which
both platforms require set before they consider a tracker at all. It also
notes that FMDN's sound-maker alerts are server-driven, so *"iPhone users are
never alerted [by sound] if no Android device is nearby."*
`THIRD-PARTY-DOCUMENTED`, peer-reviewed (PoPETs 2025(4)); the experimental
parts are the authors' `OBSERVED`, not ours.

### 3.2 arXiv:2602.07656 (Feb 2026) — reproduced again, against AirGuard specifically, with a rotation-interval sweep

Mishra, Swadeep, Noubir, Cunche, *"AirCatch: Effectively tracing advanced
tag-based trackers"*, submitted 2026-02-07 — https://arxiv.org/abs/2602.07656

Verbatim: *"identifier-based defenses fundamentally break down against
advanced rogue trackers that aggressively rotate identifiers."* Their test
setup ran an Apple iPad Pro (iPadOS 16.2), a Google Pixel 7 Pro (Android 16),
and AirGuard on both, against 1–4 injected ESP32 trackers rotating their
identifier at every transmission, sweeping the rotation period across
{2, 10, 15, 30, 60} s. Results, verbatim: *"AirGuard consistently generated
alerts sooner than both Apple and Google while maintaining broader ecosystem
coverage… Trackers from Samsung and Tile were not detected by either Apple or
Google in our tests. AirGuard, however, detected Apple, Google, Samsung, and
Tile trackers, typically within a similar 30-minute window"* — and separately:
*"fast-rotating configurations can evade or substantially delay detection
across current mechanisms."* Their own proposed defence abandons identifiers
entirely in favour of physical-layer carrier-frequency-offset fingerprinting.

`THIRD-PARTY-DOCUMENTED` — **arXiv preprint, no evidence of peer review
found; weight one grade below the PoPETs paper.** Important caveat: their
Apple-side device ran iPadOS 16.2, which **predates** iOS 17.5 and therefore
predates DULT unwanted-tracking-alert support — their Apple result is not a
test of the 2024 cross-platform capability. Their Android and AirGuard
results are current.

### 3.3 Both vendors' own support text confirms identifier continuity is load-bearing

Apple: *"if it was with you overnight, its identifier might have changed."*
Google: *"It's possible that… the device ID has changed."* Neither describes
the attack, but both confirm what makes it work. `VENDOR-STATED`.

### 3.4 What was not found

No CVE, security advisory or vendor bulletin covering rotation-based evasion
— plausibly correct, since this is a design limitation rather than a
memory-safety defect, not evidence the gap is closed. No AirGuard code
change addressing Apple- or Google-side rotation (§2.7's stitching is
Samsung-only). No direct 2026 rerun of the *exact* 2022 methodology (2000
pre-generated Apple keys, one beacon per key every 30 s, five days) against a
current iOS build — the closest is AirCatch's shorter-horizon sweep against
an outdated Apple OS.

### 3.5 The finding, stated for the record

**The fast-rotation evasion has not been closed as of August 2026.** Two
independent studies in 2025 and 2026 report that a custom tracker rotating
its identifier per transmission evades or substantially delays detection by
Apple's, Google's and AirGuard's identifier-correlation logic, and both used
an ESP32 to demonstrate it — the same SoC family this project targets, doing
the attack rather than the defence. `THIRD-PARTY-DOCUMENTED` for the general
claim; **`UNKNOWN`** specifically for whether the exact 2022 Apple
methodology still evades a *current* iOS build with DULT alerts enabled.

**What would settle the remaining `UNKNOWN`:** rerun the 2022 methodology
unchanged — N pre-generated Apple Find My advertisement keys, one
non-connectable advertisement per key at a fixed 30 s cadence, carried for
≥5 days — against a current iPhone with Tracking Notifications, Location
Services, Significant Locations and Bluetooth all on; a current Android with
unknown-tracker alerts on; and AirGuard ≥3.1.1 with location permission
granted, both with its permanent scanner off and on. Record time-to-first-
alert or its absence, sweeping the rotation period across at least
{30 s, 15 min, 24 h}. This is a **`needs-hardware`** experiment; until it
runs, the answer stays `UNKNOWN`.

**What this means for the product, stated plainly:** a detector built on
Attadipa cannot honestly claim more than AirGuard claims about itself in
§2.6 — presence of something that has stayed near the wearer, not absence of
a threat. Any wording that implies "you are not being tracked" when no alert
fires would be a stronger claim than the state of the art supports.

---

## 4. What continuous BLE scanning costs on ESP32-S3

### 4.1 The one vendor number that exists, and what it is not

The ESP32-S3 datasheet (v2.2, 2026-03-05) publishes a Bluetooth LE **RX peak
current of 93 mA** (Table 5-8), measured *"when the peripherals are disabled
and the CPU idle."* `PUBLISHED-SPEC` —
https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf,
p. 67. This is the current drawn while the receiver is physically on; it is
the closest documented proxy for scanning, and it is **not** a scanning
figure — it excludes advertisement-report handling, host-stack CPU time, and
the sleep floor between windows.

### 4.2 Espressif publishes no BLE-scanning current figure at all

Checked and confirmed absent: the datasheet, the ESP-IDF low-power-mode BLE
guide, and the NimBLE `power_save` example. The example's own current table
(240 mA max / 17.9 mA modem sleep / 3.3 mA light sleep-main-XTAL / 230 µA
light sleep-32 kHz — already in `TAGS_TRACKS_RECKONING.md` §1.8) is derived
from `bleprph`, a **peripheral** example — advertising and connected, never
scanning. `UNKNOWN`. What would settle it: a bench measurement at a stated
scan window/interval on the actual board, logged over ≥10 scan intervals —
this project's first `MEASURED` BLE number, not yet taken.

### 4.3 The vendor statement that actually constrains the design

ESP-IDF states plainly that continuous scanning is one of the things that
prevents the SoC reaching light sleep: *"Bluetooth LE configurations that
reduce IDLE time, such as continuous scanning"* are named as a cause of
failing to enter light sleep. `VENDOR-STATED` —
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-ble.html.
This is why the number that matters is not 93 mA — it is whether scanning
forecloses the ~14× lever between 3.3 mA and 230 µA that
`TAGS_TRACKS_RECKONING.md` §1.8 and T-068 already identify as `UNKNOWN` on
both boards. Without T-068's answer, a scanner's power story cannot be
completed here.

### 4.4 Duty cycle: the derivation this file could not finish, and the field-proven substitute

Deriving the scan duty cycle needed to reliably catch a 2–4 s DULT advertiser
from first principles requires the Bluetooth Core Specification's `advDelay`
randomisation and channel-rotation behaviour (Core Spec v5.4 Vol 6 Part B,
the document DULT itself references normatively). That text was not
obtained; the arithmetic is **`UNKNOWN`** rather than derived from memory.

What is available instead, and is arguably more useful: the duty cycles a
shipping detector built for exactly this job actually uses. AOSP's Bluetooth
stack defines named scan modes with fixed window/interval pairs:
`SCAN_MODE_LOW_POWER` 140 ms / 1400 ms (10 %), `SCAN_MODE_BALANCED` 183 ms /
730 ms (25 %), `SCAN_MODE_LOW_LATENCY` 100 ms / 100 ms (100 %),
`SCAN_MODE_SCREEN_OFF_LOW_POWER` 512 ms / 10 240 ms (5 %).
`PUBLISHED-SPEC` — AOSP `ScanManager.java`. AirGuard itself requests 100 %
for its 20–30 s foreground bursts and 10 % for its background/permanent
scanner (§2.5), which is the field-proven answer: **5–10 % duty cycle within
a scan burst, the burst itself run every 15 minutes**, not continuous 100 %
scanning.

A naive envelope, clearly `ESTIMATED` and not to be treated as a power
budget: 93 mA × 10 % ≈ 9.3 mA during a burst; a 20 s burst every 15 min is a
further ~2.2 % wall-clock factor, giving an order-of-magnitude ~0.2 mA
average radio contribution. This excludes PLL settling, controller wake,
host CPU and — critically — whether either board can reach the 230 µA sleep
floor between bursts at all, which is `UNKNOWN` (T-068). Do not let this
number into a battery budget as anything but a lower bound.

---

## 5. Coexistence: scanning while the companion link is up

### 5.1 Documented as supported, and sized in RAM

ESP-IDF documents a shared controller activity pool
(`BT_CTRL_BLE_MAX_ACT`, default 6, max 10, each activity costing 828 bytes)
that explicitly covers connections, advertising, scanning and periodic sync
together, with a stated sizing rule: *"Maximum connections + required
advertising, scanning and periodic sync instances."* `VENDOR-STATED` +
`PUBLISHED-SPEC` (Kconfig) —
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/ble/ble-multiconnection-guide.html;
`components/bt/controller/esp32c3/Kconfig.in` at `v6.0.2`, confirmed to apply
to ESP32-S3 via its Kconfig include chain. A companion GATT link plus a
scanner plus one advertising set is 3 activities against a default budget of
6 — nothing here forecloses the feature, and the RAM cost of the extra
activity is 828 bytes.

### 5.2 What is not documented: the cost to the connection

No Espressif source — the multi-connection guide, the RF-coexistence page,
or the BLE feature-support table — quantifies or even qualitatively
describes what concurrent scanning does to an active connection's latency or
missed-event rate on ESP32-S3. The RF-coexistence page treats coexistence as
Wi-Fi↔BLE, listing scan / advertising / connected as three separate,
mutually alternative BLE states against Wi-Fi activity, never as a
combination with each other. `UNKNOWN`.

What is structurally certain, and is being labelled as inference rather than
fact: the SoC has one 2.4 GHz radio, and Espressif states outright that a
board *"cannot receive or transmit data while another module is engaged"* and
uses time-division multiplexing to arbitrate. `VENDOR-STATED`. How the
controller arbitrates a scan window against a connection event, and with
what loss, is `UNKNOWN`. What would settle it: a bench test with the
companion link at a stated connection interval and slave latency, a scanner
enabled at a stated window/interval, measuring connection-event miss rate,
notification round-trip latency and advertisement-report yield across a
duty-cycle sweep. `needs-hardware`.

### 5.3 A correction to this task's own framing

Issue #45 described the T-Watch as sharing "an RF front end" between BLE and
LoRa, citing ADR-0003. **ADR-0003 makes no such claim** — "front end" appears
there only in a code comment about SX126x RF-switch pins and in an antenna
discussion about band-specific matching, neither about sharing with BLE. What
ADR-0003 does establish, and does bear on coexistence: of the five candidate
radio chips, `SX1280` (2400–2500 MHz) and `LR1121` (one of its three bands,
2400–2500 MHz) would contend with BLE *in-band*; the other three
(`SX1262`, `CC1101`, `Si4432`) are sub-GHz and would not. Both 2.4 GHz-capable
candidates are outside the pinned MeshCore support set. None of this settles
an actual front-end-sharing question — that belongs to T-013 and
`NODE_PROFILE`, not to this file, and this file should not be cited as having
established a shared-front-end fact it did not find.

---

## 6. Child-appropriate language — source material, not a design

This section reports vocabulary. It does not propose copy — that decision
belongs to the product, once the feature is designed, and it needs an owner
call on register the way Child Mode elsewhere in this project does.

**Apple**, verbatim from its own support page: alert titles are *"AirTag
Found Moving With You"*, *"[Product Name] Found Moving With You"*, and
*"Unknown Accessory Detected"* (used only when the accessory cannot be
identified, never for an AirTag). Framing offers a benign explanation first
— *"might be attached to an item you're borrowing"* — states the failure
case as a system fact rather than blaming the user — *"if it was with you
overnight, its identifier might have changed"* — and gives one concrete
escalation: *"go to a safe public location and contact law enforcement."*
`VENDOR-STATED`.

**Google**, verbatim from its own support page: *"You've got an unknown
tracker alert… a tracker that's not yours might be moving with you."* Same
benign-first framing, explicit repeated reassurance that playing the sound
does not notify the tracker's owner, one alert per tracker per day, and the
same escalation pattern with *"or a trusted contact"* added. `VENDOR-STATED`.

**AirGuard**, verbatim from its own strings: every user-facing string is
hedged with a modal verb — *"A tracker could be following you"*, *"%s might
follow you"*, *"A tracker you observed is not following you"* (their negative
case, stated just as carefully). It states its own limit in-product rather
than only in documentation: *"this tracker could change its identifier…
it may still follow you even if we cannot detect it."* Its escalation
guidance adds a genuinely counter-intuitive instruction — *"don't move to a
safe place before you have deactivated the tracker"* — which for a
child-worn device would need rethinking, not translating.

**The pattern all three converge on**, reported rather than designed:
presence is stated, not intent; a benign alternative is offered inside the
alert; silence is documented as never an all-clear; and there is exactly one
concrete escalation. **None of the three has a child-facing register** — every
string above assumes an adult who can call the police. No primary source for
child-appropriate unwanted-tracking copy was found. `UNKNOWN`, and — given
Child Mode is part of this project's Definition of Done — an owner decision
when T-070 reaches implementation, not a research one.

---

## Explicit `UNKNOWN` register

Carried forward as open rather than softened:

1. Whether any shipping accessory in 2026 advertises DULT service data
   `0xFCB2`. AirGuard 3.1.1 does not scan for it; no primary source of a
   product that emits it.
2. Whether Apple's and Google's AccessoryInfo lookup endpoints are usable by
   an unauthenticated third-party client.
3. Whether the *exact* 2022 evasion methodology still defeats a current iOS
   build with DULT alerts enabled — the mechanism is confirmed live by two
   2025/2026 studies, but neither reran that specific experiment.
4. ESP32-S3 BLE **scanning** current at any window/interval — Espressif
   publishes none; 93 mA RX peak (CPU idle, peripherals off) is the nearest
   documented proxy.
5. Whether either Attadipa board can reach the 230 µA light-sleep floor
   between scan bursts (pre-existing, T-068). Without it a scanner's power
   story is incomplete.
6. The cost to an active BLE GATT connection of concurrent scanning on
   ESP32-S3 — documented as possible and sized in RAM, not sized in latency
   or drop rate.
7. The scan duty cycle needed to reliably catch a DULT-interval advertiser,
   derived from Bluetooth Core Spec first principles — not obtained; AOSP's
   5–25 % field duty cycles stand in.
8. Child-appropriate register for a tracker alert — no prior art in any of
   the three products surveyed.
9. AirGuard-iOS's actual scanning capability, given iOS's documented
   restriction on third-party BLE MAC-address access.

---

## Relationship to T-069

Issue #45 asked this file to say, if it found something making T-069 a
blocker rather than an adjacent task, in the `BLOCKED` format and stop. It
does not: nothing here prevents T-069 (Attadipa read against the tracker
threat model) from being picked up next, and nothing here is implementation
work that T-069 needed to gate. What T-069 should start from, so it is not
re-derived: `draft-ietf-dult-threat-model-05` (active, 2026-08-06, not yet
read in full — §1.1) is the primary document naming what DULT itself
considers a threat, and the Accessory Category table's assignment of
`Watch = 146` (§1.3) is the direct textual hook for "a wearable is in scope."
This file establishes the detection side; T-069 is the being-detected side,
and the two meet at the same protocol document.

---

## Sources

Primary sources are cited inline at the claim they support, with this
project's evidence label. Where a claim could not be traced to a primary
source it is `UNKNOWN` and says what would settle it, per `CLAUDE.md`.
