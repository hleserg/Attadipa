# Owner Decisions

Product decisions made by the project owner, with the date they were made and
what they oblige. This file exists because of the rule in
[`../../CLAUDE.md`](../../CLAUDE.md): *a fact that lives only in a chat log does
not exist.* An architectural decision recorded only in conversation will be
silently re-litigated by whoever picks the project up next.

This is not the ADR log. An ADR records a decision *we* made and why we rejected
the alternatives. This records a decision that was **given to us** and is not
ours to overturn — the equivalent of a requirement, arriving after the
specification was written.

Format: what was decided · when · what it obliges · what it invalidates.

**The owner's words are paraphrased here, not quoted, and that is his
decision** — taken 2026-08-24 and recorded under
[OD-18](#od-18--the-received-unit-stays-powered-with-its-brightness-at-minimum).
`docs/` is this repository's GitHub Pages root, so every decision in this file
is served from the project's public website; until that date each one opened
with the chat message it came from, verbatim and in Russian. Three options were
put to him — leave it, take the register out of publication, or carry his words
as paraphrase — and he chose the third. So an entry now records **what he
decided, in this repository's own words. It does not reproduce how he said it.**

Two things did not change with the convention, and a reader should not mistake
one for the other. The paraphrase is still a record of a decision that is **not
ours to overturn**: rewording it does not turn it into our opinion, and an entry
that softens or widens what he actually settled is the same failure it always
was. And his **own authored documents** in this tree — `docs/master-prompt-final.md`,
`docs/master-prompt.md`, `docs/development-addendum.md`, `docs/ideas/` — are his
text rather than our record of it, are outside this convention, and stay in the
Russian he wrote them in.

---

## OD-1 — There is a separate Attadipa node, and the watch uses it

**Decided:** 2026-08-21.

**What he decided:** a separate node carrying LoRa, GNSS and an ESP32 exists. The
watch connects to it and runs *the same applications* — maps, compass and the
rest — that it would run on a watch with its own LoRa. With no node connected
the device is a watch, an audio device, and whatever else the installed
applications make it. All of these possibilities must be **accounted for in the
core now**; the implementation comes later.

**What it obliges:**

1. A capability may be provided by something that is not on the board. The
   capability model may no longer assume the BSP is the only source.
2. A capability may **appear and disappear while an application is running**.
   Boot-time-static capability discovery is insufficient.
3. The same application binary must run against a local capability and a
   node-provided one without knowing the difference. This is the existing rule —
   *applications ask what the device can do, never which device it is* — under
   real load for the first time.
4. Applications not yet written must be installable, and an installed
   application may outlive the capability it was installed for.

**What it invalidates:** the claim in
[`../adr/0002-companion-is-optional.md`](../adr/0002-companion-is-optional.md)
that an external device may never *provide* a capability, only improve one. That
rule was written about a phone and is correct about a phone. It was stated too
broadly. See [ADR-0004](../adr/0004-capability-sources.md).

**What it does not do:** it establishes no hardware fact. No board, no
schematic, no part numbers exist for the node. Everything about the node's
hardware is UNKNOWN and lives in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) as such
— not in [HARDWARE_MATRIX.md](HARDWARE_MATRIX.md), which is for parts traced to
a source.

**Corroboration:** this is not a new direction. The specification already
requires it — §32 *DOCTOR / ATTADIPA NODE* mandates that the architecture account
for a separate node, and lists "additional GNSS" among what it provides.

---

## OD-2 — MeshCore radio parameters are settings, not constants

**Decided:** 2026-08-21.

**What he decided:** these are the MeshCore settings, but they must not be baked
into the core. They are settings.

**Evidence supplied:** two screenshots of a live MeshCore node's exposed
parameters — the second one complete. Recorded here in full because it is the
only description of the node's data model that exists anywhere in this project,
and because what it *omits* turns out to matter more than what it contains.

The complete model, all fourteen entities:

| # | Parameter | Observed | Kind |
|---|---|---|---|
| 1 | Frequency | 868.731 MHz | setting — **regulated** |
| 2 | Bandwidth | 62.5 kHz | setting |
| 3 | Spreading factor | 7 | setting |
| 4 | TX power | 22 dBm | setting — **regulated** |
| 5 | Request rate limiter | 20.0 tokens | setting |
| 6 | Companion prefix | 04 | setting — identity |
| 7 | Node status | Online | **link state** |
| 8 | Last message delivery | Idle | operation state |
| 9 | Node count | *Unknown* | telemetry — three-valued |
| 10 | Battery voltage | 3.847 V | telemetry |
| 11 | Battery percentage | 70.58 % | telemetry — derived |
| 12 | Ch1 voltage | 3.80 V → 3.84 V | telemetry — observed changing between the two screenshots |
| 13 | Latitude | *(withheld)* | telemetry — position |
| 14 | Longitude | *(withheld)* | telemetry — position |

The position values are deliberately **not** recorded. They are a real location
and this repository is public.

### What the model gets right, and Attadipa must copy

**`Node status` is a separate entity from every value it carries.** The vendor
model does not infer "the node is there" from "a number arrived". That is the
single most important thing in the table, and it is the distinction a naive
design collapses first.

**`Node count: Unknown` is a third value, not zero.** Even the vendor's own
integration has a field that is neither a number nor absent. The core needs the
same three-way distinction — *known* · *known to be none* · *not known* — and
the UI must never render the third as the second. "0 nodes nearby" and "we have
no idea how many nodes are nearby" are different sentences, and one of them is
a lie.

### What the model is missing, and Attadipa must not copy

This is a fine inventory of what a node *has*. It is not sufficient as a core
data model, and the gaps are instructive because each one is a decision the
Attadipa core has to make deliberately:

- **No timestamp on anything.** Latitude and longitude with no age are unusable
  for navigation. A coordinate that is four hours old and a coordinate from two
  seconds ago are the same two numbers here. Every datum crossing the link must
  carry its age.
- **No fix state for the position.** No satellite count, no HDOP, no fix/no-fix
  flag, no altitude. So there is no way to tell a *current GNSS fix* from a
  *last-known* or *manually configured* position — the exact collapse of "the
  provider is reachable" into "the provider has an answer" that the capability
  model has to keep apart.
- **No link quality.** No RSSI or SNR for the last received packet, so nothing
  can tell "connected" from "connected and about to drop".
- **No protocol or firmware version.** Nothing to negotiate against. Two
  independently updated devices with no version field is a compatibility
  problem waiting for its first firmware release.
- **No airtime or duty-cycle counter.** The two regulated settings in the table
  are bounded by rules that constrain *airtime*, and nothing here measures it.

None of this is a criticism of MeshCore, which is solving a different problem.
It is the argument for §32's requirement that the Doctor/Attadipa application
protocol not be the MeshCore internals wearing a different name.

**What it obliges:**

1. No RF parameter may be a compile-time constant anywhere in `core/`. Frequency,
   bandwidth, spreading factor and TX power are runtime-settable, persisted
   values.
2. There must therefore be a settings subsystem in the core — typed values,
   validated ranges, defaults, persistence, factory reset — before there is a
   radio service that reads them.
3. Two of these settings are **legally bounded** (frequency, TX power). The core
   has to express "user-settable, but bounded by a regulatory profile" without
   the core knowing which region it is in. See A4.
4. A settings screen is a first-class part of the product, not a debug menu.
5. Every value that crosses the link carries its **age** and, where it is a
   measurement, its **validity** — because the reference model carries neither,
   and a position without those two fields cannot be navigated by.

**Open, arising directly from this:** 22 dBm is 158 mW. Whether that is lawful
at 868.731 MHz in the region of operation is exactly question **A4**, and this
screenshot makes it concrete rather than theoretical — the owner's existing node
is already transmitting at a power level whose legality this project has not
established. Attadipa is not responsible for that node, but it must not ship a
default that assumes it.

---

## OD-3 — A new master specification, and a review of the work so far

**Decided:** 2026-08-21.

**What he decided:** the owner supplied `Attadipa_Master_Prompt_Final_Bundle.zip`
containing a 3 125-line specification and three PNGs, with the instruction that
the archive contains a review and that everything is to be done according to the
prompt inside it.

**What arrived:**

| File | Now at | SHA-256 |
|---|---|---|
| `ATTADIPA_OS_MASTER_PROMPT_FINAL.md` | [`../master-prompt-final.md`](../master-prompt-final.md) | `65675d49604ba217e5ca7288621ab33d8655f0659e61f2ce795eec27b42312ed` |
| `design_refs/attadipa_brand_identity.png` | [`../ui/reference/`](../ui/reference/) | `d9a51f7b69b3566d366e9f9c2d27d375579152e2fdf5c3a46c46ec16112c880e` |
| `design_refs/attadipa_visual_style_board.png` | [`../ui/reference/`](../ui/reference/) | `4e66f2a4b09038bb4e94f2dd097733a987a714c13572df68766900f75b84c2b9` |
| `design_refs/attadipa_mascot_sheet.png` | [`../ui/reference/`](../ui/reference/) | `175f7cfd9343973e65242843ad697bc9646b4ba2a312f78c42de8e6f2024684a` |

All four are committed byte-identical to what was supplied. The hashes are
recorded so that a later edit is visible as one.

**What it obliges:**

1. **It supersedes both earlier specification documents.** Its own preamble
   says so. `docs/master-prompt.md` and `docs/development-addendum.md` are now
   history and carry supersession notices.
2. **Eight P0 corrections must land before large new core implementation**
   (final §75 A–H). They are not suggestions; §75 is titled *"do this before
   large new core implementation"*, and the review that produced them found
   real contradictions in what this repository had already written.
3. **The three images are canonical project art**, not decoration, and must
   materially influence the design system and the asset pipeline (final §40,
   §44, §45). §41 is equally binding in the other direction: what they depict
   is not a product fact.
4. **English and Russian from the first real screen** (final §50). This is
   stated as a binding product requirement, in the same register as MeshCore
   compatibility and standalone operation — not as later polish.
5. **Research stops after the reconciliation.** §75 closes: *"Do not spend
   another week in research after this reconciliation. Move into M1."*

**What it invalidates:** eight things this repository had written, listed in
[the reconciliation record](RECONCILIATION_2026-08-21.md). The largest are that
capabilities were modelled in one flat layer mixing silicon with product
features, that all five T-Watch radios were called LoRa, and that
[ADR-0005](../adr/0005-node-protocol.md) asserted the watch never runs MeshCore.

**What it does not change:** every hardware fact in
[VERIFIED_FACTS](VERIFIED_FACTS.md) still stands — the review corrected the
*model*, not the measurements. And [OD-1](#od-1--there-is-a-separate-attadipa-node-and-the-watch-uses-it)
is untouched: final §3 and §9 restate it almost word for word.

---

## OD-4 — Synchronise with upstream MeshCore before continuing the roadmap

**Decided:** 2026-08-21.

**What he decided:** this is the *first* task, to be done before any further OS
development. Stop the roadmap and
review upstream MeshCore between v1.16.0 and v1.17.1+, including `dev`, across
ESP32-S3, the Heltec V4 family, SX1262, the companion firmware, BLE, USB, the
multi-interface work, LoRa RX/TX, preamble detection, LBT, CAD, FEM/LNA, power
management and sleep, battery measurement and brownout, persistent config,
contacts and storage, GPS and time, and hardware RNG and crypto acceleration.

**What it obliges:**

1. **Release notes are not evidence.** Read commits, merged pull requests,
   technically valuable unmerged ones, open issues and `dev`; for each change,
   find the *root cause*, not the changelog line.
2. **Distinguish confirmed fix / merged fix / released fix / open PR /
   experimental.** Do not assume the latest release is the best one. Check for
   open regressions, the FEM RX gain path in particular.
3. **Do not pull unmerged code into production Attadipa without analysis.**
4. **Build a compatibility layer** so MeshCore can be updated without rewriting
   the OS: `UI/Apps → Services → Mesh Service API → MeshCore Adapter →
   transports → HAL`.
5. **Produce `docs/upstream/meshcore-1.17-review.md`** with a status per item:
   `adopt / adapt / monitor / reject`, then file each required Attadipa change as
   a separate small task.
6. **Do not stop at the review.** Fix the critical architectural errors, add
   regression tests, build, run, fix, and continue. The order is stated as a
   principle: **Research → reuse proven implementations → adapt → test → only
   then invent.**

Four instructions inside it are narrower than the rest and are recorded
separately, because each forbids something that would otherwise look reasonable:

- **Transport is not BLE.** Attadipa's must admit BLE, USB, UART, Wi-Fi/TCP and
  possibly ESP-NOW, several at once. Do not copy #3049 blindly.
- **No own LBT yet.** Do not implement our own listen-before-talk until it is
  clear what can safely be taken from MeshCore. Hardware CAD stays experimental
  while upstream ships it off.
- **Do not port the old FEM implementation.** FEM/LNA is a **board capability**,
  never an SX1262 assumption.
- **Hibernate is not a sleep with the radio armed** — do not conflate "sleep with
  a LoRa wake-up" with a true hibernate. And, separately: **the wall clock must
  never be used to measure elapsed time** — the monotonic clock owns timers,
  timeouts, retries, connection expiry and the scheduler.

**What it invalidates:** nothing already written, because none of these
subsystems exists yet. That is the point of its timing — the review landed
before the code it constrains, which is the only moment any of it is free.

**Status:** the review is
[done](../upstream/meshcore-1.17-review.md); it filed T-043 … T-050.

---

## OD-5 — GNSS integrity, and the receiver's own protection comes first

**Decided:** 2026-08-21.

**As stated:** a GNSS receiver is not merely a source of NMEA sentences. Modern
receivers carry jamming detection, jamming mitigation, spoofing detection,
integrity estimates, RF diagnostics, per-signal information, assistance and
fast-start, and security features, and Attadipa must use them. The priority order
is explicit: **receiver-native mechanisms → Attadipa's independent detectors →
a combined trust state.**

**What it obliges:**

1. **Research both real variants from primary sources before writing a driver.**
   The T-Watch S3 Plus ships either a u-blox **MIA-M10Q** or a Quectel
   **LS550G**. Datasheet → integration manual → protocol specification → vendor
   examples → official library source, in that order. Anything unclear is
   `UNKNOWN` — never an assumption baked into code.
2. **Anti-spoofing on the LS550G is `UNKNOWN`, not `SUPPORTED`,** until a
   primary source or a real device says otherwise. The vendor's marketing claims
   are claims.
3. **RTCM is not a property of "GNSS", of "u-blox", or of an abstract
   `GnssDriver`.** **The MIA-M10Q does not support RTCM.** Differential
   corrections must be an optional capability of a specific provider.
4. **Do not collapse the states.** Availability, receiver health, fix presence,
   fix type, freshness, accuracy, integrity, interference, spoofing suspicion
   and final trust are separate. A provider may be `Ready`, with a numerically
   valid fix, and still be unusable for navigation.
5. **Do not lose data at the driver boundary.** The observation type must carry
   what the receiver reports — both a normalized Attadipa representation *and*
   the receiver's native values, not one at the cost of the other.
6. **A GNSS receiver capability descriptor**, so an application still asks
   `LocationService` and never learns the chip: jam detection, active jam
   mitigation, spoof detection, interference monitoring, protection level,
   signal security log, per-signal diagnostics, constellation control,
   autonomous orbit prediction, assistance injection, differential corrections
   input, raw measurements, configuration lock, message integrity.
7. **Trust is a state with reasons, not a boolean.** `Trusted` / `Degraded` /
   `Untrusted`, with hysteresis, weighted evidence, reason codes, timestamps,
   the last trusted position, growing uncertainty after loss, and a transition
   log. Not `gps_ok`, and not `spoofFlag || jumpDetected || jamming`.
8. **The receiver's own verdict is strong evidence, not truth.** Fuse it with
   the accelerometer, physical plausibility, clock-versus-GNSS time, provider
   disagreement and constellation anomalies. The canonical case: **GNSS reports
   large movement while the accelerometer says the device is still.**
9. **The BMA423 is an accelerometer.** No gyroscope, no magnetometer. It is
   right for that detector and is **not** an IMU and not dead reckoning.
10. **A bounded, disableable, replayable diagnostic trace** before any field
    testing — never an unbounded log that can fill flash.

**What is explicitly *not* to be built now** (owner §15, and it is emphatic):
no Kalman filter, no RTS smoother, no pedestrian dead reckoning, no second GNSS,
no RTK, no DGNSS, no RTCM over LoRa, no map matching, no HMM, no routing, no
universal spoofing detector — and do not break the current milestone. What is to
be done now is the architecture and the tasks:
record the decision, check the existing `GnssDriver` / `LocationService` shape,
stop the interfaces losing integrity information, file the receiver research,
add the descriptor, add the trust state, add the simulator's fault scenarios,
fix the RTCM assumption — and then carry on.

**What it invalidates:** the assumption that RTCM belongs to a generic GNSS
driver, wherever this repository has written it. A grep at the time of recording
found it written **nowhere** — no ADR, no architecture document, no research
file, no header — so this is a fence built before the path was worn rather than
a correction.

**Status:** the architecture half is
[ADR-0011](../adr/0011-gnss-integrity.md); it filed T-051 (MIA-M10Q), T-052
(LS550G) and T-053 (the simulator's GNSS-fault scenarios).

---

## OD-6 — The watch counts steps, and that is not optional

**Decided:** 2026-08-21.

**What he decided:** a pedometer is a mandatory feature of the watch — not a
nice-to-have and not a later milestone.

**What already exists, and what does not.** `Capability::MotionSensing` is
already in the enum and its comment already says *"steps, wrist gestures,
activity"*, so the seat exists. Nothing implements it, and the interesting part
is that the two boards cannot implement it the same way.

| | T-Watch S3 | Waveshare 2.06 |
|---|---|---|
| Part | BMA423 | QMI8658 |
| Axes | accelerometer only, no gyroscope | accelerometer + gyroscope |
| Step counting | **`UNKNOWN` — must be traced to the datasheet.** The BMA4xx wearable variants are documented by Bosch as carrying a step counter and step detector in the sensor itself; whether the BMA423 specifically does, on this revision, and what its interrupt and FIFO behaviour is, has not been read from a primary source by this project | **`UNKNOWN`.** No integrated step counter is known. Steps would be a firmware algorithm over raw acceleration |

That asymmetry is the whole engineering content of this decision, and it is a
power question rather than a maths question:

- **a step counter inside the sensor keeps counting while the SoC is asleep**,
  and the SoC reads an accumulated total when it next wakes. The cost is the
  sensor's own microamps;
- **a step counter in firmware needs the samples.** Either the SoC stays awake,
  or the sensor batches into a FIFO deep enough to cover a sleep interval and
  the SoC wakes to drain it. Both cost far more than the first, and how much
  more is a measurement nobody has taken.

A mandatory pedometer that stops counting when the screen goes off is not a
pedometer, so this decides something about the power model rather than only
about an application.

**What it obliges:**

1. **Read the datasheets before writing anything.** BMA423 first: does the part
   count steps itself, what does it do across a sleep, what survives a reset,
   and how is the counter reset at midnight without losing steps taken during
   the reset. Then QMI8658: FIFO depth, watermark interrupt, and what a sleep
   interval costs in wakes. `UNKNOWN` is a valid answer and an unsourced
   `SUPPORTED` is not.
2. **Steps are a capability, not a board feature.** An application asks for a
   step count; it never learns whether a sensor counted them or firmware did.
   Both answers live below `Capability::MotionSensing`, and a board where the
   honest answer is "not while asleep" reports a `Degraded` availability rather
   than a number that is quietly wrong.
3. **The daily total must survive.** A reboot, a crash, a flat battery and
   midnight are four different events and only one of them should zero the
   count. That is persistence with crash safety, which is T-046's problem and
   now has a first customer.
4. **No estimated step counts.** A count derived from a period the device was
   not measuring is not a count. If steps were missed, the day's total says so
   rather than interpolating — the same rule the GNSS work applies to a position
   nobody observed.

**Status:** filed as T-060 (what each IMU actually does about steps, from
primary sources) and T-061 (the capability, its power story and its
persistence). Neither is started.

---

## OD-7 — The companion is any node, not only ours

**Decided:** 2026-08-22.

**What he decided**, across three messages:

- **Attach to a stock MeshCore node over BLE or LAN.** Not everyone will want to
  build our variant of the node first, and it is useful to us as well. Put it in
  the plan.
- **Attaching is not enough on its own.** The watch must immediately be able to
  talk over the mesh, to request and receive telemetry, and to take coordinates
  out of telemetry, out of incoming messages, and off the node itself where the
  node has GNSS.
- **A watch with its own LoRa should still be able to use both nodes**, and
  Meshtastic should be available on the watch as a companion option — instead of
  MeshCore, or alongside it, whichever turns out to be workable.

**What it changes.** [ADR-0008](../adr/0008-mesh-service-providers.md) already
has the right shape — one `MeshService`, providers behind it, applications that
never learn which one answered — and it already has two providers: the watch's
own radio and *the Attadipa node*. This widens the second one. The companion is
now **any** device that speaks a protocol the watch has a client for, reached
over **any** transport the watch has:

| | |
|---|---|
| Stack | MeshCore · Meshtastic · the Attadipa node |
| Transport | BLE · Wi-Fi/LAN · the wired node link |
| Count | more than one at a time, and alongside a local radio |

The reasoning the owner gave is a product argument and it is a good one: a person
who already owns a MeshCore node should be able to use the watch on the day they
buy it, without building anything. That also makes the watch testable against
hardware other people have.

**What it obliges:**

1. **A companion is a capability source, not a device an application knows
   about.** Same rule as everything else — an application asks `MeshService` to
   send a message and never learns that a vanilla node in a rucksack carried it.
   ADR-0008 §3's selection policy extends from "local or node" to a list; that
   the list can now have three entries does not give applications a second code
   path.
2. **Telemetry is a request/response feed, and feeds are not capabilities.**
   T-029 already established that separation; this is its first real customer.
   A telemetry value carries the two ages every datum that crosses a link
   carries ([ADR-0004](../adr/0004-capability-sources.md)).
3. **Three more ways a position arrives**, each with a different provenance and
   a different trust: from a telemetry frame, from an incoming message that
   carried one, and from the companion's own receiver.
   [ADR-0011](../adr/0011-gnss-integrity.md) already separates `PositionValidity`
   from `TrustState`; a coordinate taken out of somebody else's message is the
   case those axes exist for, and it must never be presented as the wearer's
   own fix.
4. **Nothing here relaxes the licence rule.** MeshCore is MIT and Meshtastic's
   firmware and protocol definitions are GPL-3.0. Both are licence-compatible
   with Attadipa's current `GPL-3.0-or-later` terms, but
   [OD-12](#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence)
   separately rejects Meshtastic as a product dependency. No Meshtastic code
   is imported.
5. **Nothing here relaxes the honesty rule either.** MeshCore's own security is
   an open upstream issue; a message that crossed a vanilla node gets no lock
   icon and no "encrypted" label.

**Status:** research only. Filed as T-072 (what a vanilla MeshCore node actually
exposes, and over which transports), T-073 (the same for Meshtastic, licence
first), T-074 (many providers at once, in the ADR-0008 shape). No client is
written until T-072 has answers from source.

---

## OD-8 — Every source of position, and the watch as the instrument

**Decided:** 2026-08-22.

**What he decided**, across two messages:

- **With GNSS in the watch, the wearer picks which receiver is used** — and, as
  a further option, both sources are combined and processed together to improve
  accuracy. The same applies to a phone where possible: the watch should be able
  to take coordinates (and whatever else is useful and available) from it and
  process them itself. The watch becomes **the primary navigation instrument**.
- **Assistance data must not assume the internet.** Provide for obtaining AGPS
  over other channels too — BLE, LoRa and the rest — because he intends to get
  it to the device one way or another regardless.

**The list of sources this creates**, which is longer than the one the GNSS work
was written against:

| Source | Already modelled? |
|---|---|
| the watch's own receiver | yes — ADR-0011 |
| the companion node's receiver | yes — ADR-0004, as a node-supplied capability |
| a phone, over the companion link | **no** |
| a coordinate inside an incoming mesh message | **no** — OD-7 |
| a coordinate inside a telemetry frame | **no** — OD-7 |
| dead reckoning from the IMU | filed, not modelled — T-071 |
| cell towers | **no** — OD-9 |

**What it obliges:**

1. **Selection and fusion are two different features and the second is not the
   first done twice.** Choosing which receiver to believe is a policy in the
   ADR-0008 shape. Combining two receivers to do better than either is an
   estimator, and an estimator that is wrong is worse than the better of its
   inputs — it produces a confident number nobody can check. Which of the two
   ships first is a decision that needs the replay rig
   (`tests/replay/`) pointed at real multi-source traces, not an opinion.
2. **Provenance travels with the position, always.** A fix from the wearer's own
   receiver, a fix relayed from a node on a roof, and a coordinate lifted out of
   somebody else's message are three different claims about where the wearer is,
   and exactly one of them is about the wearer. The user-facing consequence is
   that the screen says which, in words.
3. **AGPS is a payload, not a transport.** The owner is explicit that it may
   arrive over the internet, BLE, LoRa or anything else. So the assistance data
   is defined once — format, validity window, size, what it is good for — and
   the delivery is a separate question answered per channel. A LoRa channel with
   a few hundred bytes a minute and an internet one are the same payload with
   very different pacing, and whether any useful assistance format fits the first
   is `UNKNOWN` until the receiver documents are read (T-051, T-052).
4. **"The watch becomes the primary navigation instrument"** is the sentence to
   design against. It means the watch is the thing that decides, not a display
   for whatever the phone last said.

**Status:** research only. Filed as T-075 (the source inventory and what each one
can honestly claim), T-076 (phone-supplied position and data over the companion
link), T-077 (AGPS as a payload, and what fits each channel). Selection versus
fusion is deliberately not decided here.

---

## OD-9 — The node may carry a cellular modem

**Decided:** 2026-08-22.

**What he decided:** he will probably put cellular — GSM/4G/LTE — into the
node. It serves two purposes: refining position (interrogate the towers, look
their identifiers up in a database downloaded from the internet ahead of time),
and having a way to get online at all — for assistance data and the like.

**Two features, and they are independent.** A modem in the node would give:

- **a position source that works indoors and needs no sky** — read the serving
  and neighbour cells, look their identifiers up in a database downloaded ahead
  of time, and produce a coarse position. Accuracy is hundreds of metres to
  kilometres depending on cell density, which makes it a *fallback and a sanity
  check* rather than a navigation fix. It is also the only source on this list
  that keeps working under a roof;
- **a route off the mesh** — internet for assistance data, for a message that has
  to leave the mesh, and for keeping the tower database current.

**What is `UNKNOWN` and gates it**, none of which may be guessed:

1. **The part.** There is no modem in [NODE_PROFILE](../node/NODE_PROFILE.md)
   because there is no node part number yet. Band support, power draw while
   registered, and whether it can be powered down without losing registration
   are all properties of a specific module.
2. **The database.** A tower database is the whole feature and it is somebody
   else's data. Licence, size, coverage in the regions this product ships to, and
   update cadence are four separate answers, and "there is an open one" is not
   any of them. A database that does not fit the node's flash is not a feature.
3. **The regulatory picture.** A cellular modem is type-approved equipment and a
   SIM is a subscription in somebody's name. That is a different conversation
   from an ISM-band radio and it belongs to the owner, not to this repository.
4. **Privacy.** A device that registers on a network is a device that can be
   located by the network, whether or not the wearer asked. Child Mode makes that
   a question with a legal answer in some jurisdictions, and the tracker threat
   model already filed (T-069) grows a section rather than a footnote.

**What it does not change:** the mesh is still the product. A modem is one more
source and one more route, entering through the same provider registry as
everything else, and the watch must be complete with none of it present.

**Status:** research only. Filed as T-078 (the cellular option: part class, power
and regulatory shape) and T-079 (tower-database positioning: licence, size,
coverage, and what accuracy may honestly be claimed). Nothing is designed until
a part exists.

---

## OD-10 — A standing person does not need a new fix

**Decided:** 2026-08-22.

**What he decided:** for a wearer who is standing still — detectable from the
accelerometer — take GNSS fixes less often, and where the existing coordinates
are accurate and trusted, do not ask again at all until they move. At the same
time, a cold start must be avoided if at all possible: do not switch the module
off entirely, or keep assistance data ready somehow. Filed as something to think
about rather than as a finished design.

**The idea is right and the second half is the hard half.** GNSS is the largest
continuous draw on a watch that has it, and a position that has not changed does
not need to be measured again. What makes this non-trivial is that the saving and
the cost live in the same place: switching the receiver off is exactly what turns
the next fix into a cold start, and a cold start is tens of seconds of full
current plus a wearer standing still looking at a spinner. The owner names that
trap in the same sentence, which is why this is recorded as a decision rather
than as a feature request.

**What already exists to build it on**, so that this is a composition rather than
a new subsystem:

- **motion, from the IMU.** [OD-6](#od-6--the-watch-counts-steps-and-that-is-not-optional)
  already requires the accelerometer to keep working while the SoC sleeps, and
  "is the wearer moving" is a strictly easier question than "how many steps".
  On the T-Watch that may be an interrupt from the part itself rather than a
  sampling loop, which is the difference between free and not;
- **the three start kinds.** `start_kind()` in `core/` already distinguishes hot,
  warm and cold, and T-055 already found and fixed a bug where *having* a backup
  domain was read as evidence it had been *powered*. This decision is that
  function's first real consumer;
- **trust and validity as separate axes.**
  [ADR-0011](../adr/0011-gnss-integrity.md) already says a position can be valid
  and untrusted. "Accurate and trusted coordinates" in the owner's sentence is
  exactly the conjunction of the two, and it is already expressible.

**What it obliges:**

1. **Standing still is a hypothesis, not a fact.** A watch on a table in a moving
   train reports no motion. A wrist held steady while walking reports very
   little. So the gate is a *rate reduction with a ceiling*, never an indefinite
   suspension: there is always a longest interval after which the receiver is
   asked again regardless, and the ceiling is a setting rather than a constant.
2. **A held position is timestamped, not refreshed.** The screen shows the age of
   the fix, and an old fix reads as an old fix. This is the same rule the GNSS
   work already applies — a position nobody observed is not interpolated — and
   holding one deliberately must not quietly become the thing that violates it.
3. **The receiver is duty-cycled, not switched off.** Which of the receiver's own
   low-power modes are usable, what each one keeps, and what each one costs are
   properties of the specific module and are `UNKNOWN` — see T-051 (MIA-M10Q) and
   T-052 (Quectel LS550G). This decision does not choose between them; it says
   the choice is made from the receiver documents and measured, not assumed. An
   estimated milliamp is labelled `ESTIMATED`.
4. **AGPS is the other half of the answer**, which is why
   [OD-8](#od-8--every-source-of-position-and-the-watch-as-the-instrument) item 3
   matters here: assistance held ready turns a cold start back into something
   closer to a warm one, and it can arrive over any channel. Ephemeris has a
   validity window measured in hours, so "held ready" means a refresh policy, not
   a download.
5. **Dead reckoning covers the gap it opens.** If the receiver is asked less
   often, the interval between fixes is exactly where T-071's IMU track has to
   carry the position. The two features are the same feature seen from either
   end.

**Status:** research and design, filed as T-080. Nothing is implemented until
T-051 and T-052 say what the receivers can actually do, because the whole feature
is a claim about a specific module's low-power behaviour.

---

## OD-11 — Themes are installable, and the layout survives them

**Decided:** 2026-08-22.

**What he decided:** we will make it look good, but no single look will please
everybody. Theme switching goes into the core, and themes are downloadable,
installable and switchable **like applications** — a user's own colours, own
fonts, own icons for the stock applications, and so on — **without the layout
falling apart on screen**. In the plan, and not optionally.

And, in the same message, about a `□` visible in a simulator screenshot: he
asked what that rectangle was, whether it was a glyph missing from the font, and
said that of course nothing like it may reach production.

The second half is not a separate topic. A missing glyph is what a theme system
produces by default unless it is designed not to, and the box in that screenshot
is the *stock* font failing on `×` — with one font, chosen by us, in a build we
control. A user-supplied font is that failure mode with the safeties off.

**What already exists.** The design-token substrate shipped on 2026-08-22 and
was built the right way round by following final
§54: a screen names `color.accent.primary` and `space.md`, and the value behind
the name is resolved in exactly one place. Swapping the table under those names
*is* a theme. What does not exist is any of: a theme that is data rather than
code, a way to install one, a validity check, or a way to survive a bad one.

**What it obliges:**

1. **A theme is data, not code.** It carries colour values for the twelve roles
   in both themes, a font, an icon set, and nothing else. It never carries
   layout, and it never carries a pixel count — a theme that could set a padding
   could break every screen, and *the layout does not fall apart* is precisely a
   requirement that it cannot.
2. **Installing a theme is installing untrusted content**, and it arrives over
   the same links a message does. It is parsed defensively, it is bounded in
   size before it is read, and a malformed one is rejected with a sentence a
   person can act on. This is a security surface, not a preferences screen.
3. **A theme is validated before it is applied, and the rules already exist as
   arithmetic.** `ui/src/color.cpp` computes WCAG contrast today; a candidate
   theme whose text does not clear 4.5:1 on its own page is not applied, or is
   applied with the failure stated. The palette work of 2026-08-22 found two such
   failures in the *owner's own* palette by computing rather than looking — a
   stranger's palette gets the same arithmetic and no more benefit of the doubt.
4. **A font is only installed with its coverage.** A theme's font must draw every
   codepoint both catalogues contain, or it is refused. `check_glyphs.py` and
   `report_undrawable_glyphs()` already ask that question at build time and at
   run time respectively; a theme system makes it a runtime gate on installation.
   **No box characters, ever** — which also means the shipping build must stop
   using a Latin-only stock font, a defect that is real today and filed.
5. **There is always a way back.** The built-in theme cannot be uninstalled, and
   a theme that makes the screen unreadable must be removable without reading the
   screen. That is a recovery path, and it is designed before the first theme is
   installable rather than after somebody is locked out.
6. **Icons are replaceable for vanilla applications**, which means an application
   asks for a *named* icon and never for a file. Same rule as colour, applied to
   images, and it constrains the asset pipeline (T-034) before it is written —
   which is why this is recorded now rather than when themes are built.

**What it does not decide:** the format, the distribution, the signing, and
whether a theme may ship executable content at all. The last one is the load-
bearing question and the default answer is **no**.

**Status:** filed as T-081 (themes as installable data, the ADR), T-082 (theme
validation — contrast and glyph coverage as an installation gate) and T-083 (the
shipping font: no box characters in any build, which is a defect today rather
than a feature). T-034's asset pipeline is amended before it starts.
## OD-12 — Meshtastic is not supported, and the reason is not the licence

> **One premise in the rationale below has expired and the record is left
> unedited anyway.** It says T-072 is open; T-072 was completed later the same
> day. The decision is unaffected — see the annotation at the end of this
> section. Owner decisions are not rewritten to keep their reasoning tidy.

> **A second premise expired on 2026-08-26.** Attadipa migrated from MIT to
> `GPL-3.0-or-later`, so GPL-3.0 code is no longer blocked by the project's
> licence. The owner decision not to support Meshtastic remains in force; the
> rationale below is retained as the historical record of the 2026-08-22 choice.

**Decided:** 2026-08-22, on [#41](https://github.com/hleserg/Attadipa/issues/41).

**What he decided:** he agreed and accepted the recommendation in that issue —
option 4, do not support Meshtastic, because MeshCore alone answers what
[OD-7](#od-7--the-companion-is-any-node-not-only-ours) actually asked for.

**What was asked, and what happened to it.** OD-7 said Meshtastic should be a
companion option *instead of MeshCore, or alongside it, whichever turned out to
be workable*. T-073 checked the licence first, as that task required, and found
the blocker:
`meshtastic/protobufs` is a separate repository with its own `LICENSE`, and that
file is **GPL-3.0** with no linking exception. At the time, generating code from
those `.proto` files and linking it into the firmware would have made Attadipa's
then-MIT firmware a derivative work under GPL-3.0. The reuse-ledger policy at
the time prohibited copying protocol definitions just as it prohibited copying
C++.

Four options were put to the owner: a real clean-room from published
documentation only; shipping the provider as a separately distributed GPL-3.0
component; asking upstream for an exception; or not supporting Meshtastic. Only
the first and last are executable by an agent without legal advice, and they
differ by months.

**The decision is option 4, and the distinction matters for the record.** The
licence is what made the cheap path impossible. The *decision* is that the
feature is not worth the expensive one — a genuine clean-room is months, done
honestly or not at all, and a half-clean-room is worse than neither.

**What MeshCore is, stated at the strength the evidence actually supports.** It
is MIT, and its source has a `companion_radio` role and a transport abstraction
— both read, both in the reuse ledger. That is enough to say a companion client
is *buildable without a licensing problem*, which is the half OD-7's need turns
on. It is **not** enough to say the protocol is understood: **T-072 is open**,
and every row of
[COMPANION_AND_POSITION_SOURCES](COMPANION_AND_POSITION_SOURCES.md) §1 is still
`UNKNOWN` — which transports a stock build exposes, whether a LAN/TCP companion
transport exists at the pinned revision, which commands it answers, whether
telemetry carries a position. An earlier draft of this record said T-072 was
finished and LAN was there. It was not, and the independent review on
[#48](https://github.com/hleserg/Attadipa/pull/48) caught it.

The decision does not rest on the overstatement. Rejecting Meshtastic follows
from the licence and the cost of a real clean-room; MeshCore being the remaining
candidate follows from its licence. What is *not* yet established is how much
work a MeshCore companion client is — and that is T-072's job to answer, not
this record's to assume.

So the ledger records `REJECT` for the licence, and this records `REJECT` for
the product. If Meshtastic's licensing ever changes, the licence half is
answered and this decision is the only thing to revisit.

> **Annotation, 2026-08-22 — the premise moved, the decision did not.** The
> paragraphs above are the owner's record and are left exactly as written,
> because they are the reasoning that was in front of the owner at the time and
> that is what this file is for. One factual premise in them has since expired:
> **T-072 is no longer open.** §1 of
> [COMPANION_AND_POSITION_SOURCES](COMPANION_AND_POSITION_SOURCES.md) is answered
> on every row and the detail is in
> [MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md). The record
> above already said the decision does not rest on that premise, and it does
> not: the answer is that a MeshCore companion client is a real but bounded
> amount of work — 58 commands, a 176-byte frame budget, and a TCP transport
> that makes a host-side client cheap. Nothing in it makes Meshtastic cheaper or
> its licence gate narrower. **OD-12 stands unchanged.** This note exists so the
> next agent does not read a stale `UNKNOWN` as current, and so nobody is tempted
> to edit an owner decision to keep its rationale tidy.

**What it changes.**

| | |
|---|---|
| **T-073** | closed, `REJECT`. Not blocked, not deferred — decided |
| **T-074** | keeps its scope but loses its second concrete provider. Written against MeshCore plus a hypothetical second, which is enough to keep `availability(MeshMessaging)` and deduplication honest without inventing a provider to satisfy a list |
| **OD-7** | stands, minus its Meshtastic clause. The companion is still *any* node, and MeshCore is the one we have a client for |
| ADR-0008 | unchanged in shape. It was already a list, and a list of one is not a design flaw |

**What is explicitly not decided here.** Whether a Meshtastic *bridge* could
live outside the firmware — on the Attadipa node, or on a phone — is a different
question with a different licensing answer, and nobody has asked it.


---

## OD-13 — No tag emulation; a track is a way back on foot, and saving one whole is a separate feature

**Decided:** 2026-08-22, answering A7 on
[#33](https://github.com/hleserg/Attadipa/issues/33).

Three questions were put to the owner because none of the three features has a
line in the specification and all three compete for one antenna, one coexistence
arbiter and one 940 mAh cell. All three came back, and the second came back as a
better question than the one asked.

### 1. The watch does not pretend to be a smart tag

**What he decided:** we are not doing it — not Apple's ecosystem and not any
other.

Not deferred, not blocked on the ecosystems. **Decided.**

The obstacles found by the research are real and are not the reason: Google
needs an approved proposal, an email allowlist and third-party certification,
and its only readable implementation is licensed for Nordic silicon; Samsung's
SDK ships for no Espressif part; Apple is reachable but costs an Apple ID
bootstrapped on Apple hardware, a self-hosted endpoint, and MFi for anything a
person would recognise as Find My. Those made the feature expensive. The owner
decided it is not wanted, which is a different sentence and outranks the first.

**The lowest-cost lost-watch route survives, and it is why this costs nothing.**
The companion phone remembering where it last saw the watch over BLE answers
*"I have lost my watch"* with no account, no other company's identifier and no
network at all — and it is the only variant that works with the companion this
project already specifies.

### 2. A track is not a length of time. It is distance from familiar ground, on foot

The question asked was *how many hours*. The owner replaced it: **a track is
recorded for the case where somebody will probably have to walk back along it**
— out of the metro, walking, lost, looked at the track, found the way back.

So the recording rule is about **purpose**, not duration:

- the watch learns **familiar ground** — places where a person stays a long time
  while moving only locally. A camp is tent ↔ fire ↔ the clearing beside them;
- inside it, **nothing is recorded**;
- past a threshold beyond its edge, on foot, **recording starts**;
- on return, the track is **erased**;
- going out the same way again records only what lies past the new edge.

**A car, a bicycle or any other vehicle is out of scope — that is what a phone
is for.** This is the purpose, not a literal specification; the details belong
to whoever implements it.

**What this does to the sizing.** The upper bound is now a walk somebody has to
retrace, not a day or a multi-day route. Order of magnitude: a couple of hours,
single-digit kilometres, hundreds to a few thousand points — materially less
than the multi-day assumption the research sized against, which takes pressure
off both the encoding and the mesh carrier. **The number still has to be
computed**, from the sampling rule and the chosen threshold. Computed, not
guessed.

**What this rule now depends on, and it is not free.** Naming these is the point
of writing the decision down:

1. **"On foot" requires motion-mode recognition.** Without it the watch records
   in a car, which is exactly what was excluded. That rests on the pedometer,
   which exists only as [OD-6](#od-6--the-watch-counts-steps-and-that-is-not-optional).
2. **"Familiar ground" is learned anchors** — the watch stores where its wearer
   habitually is.
3. **Threshold, hysteresis and dwell are three numbers that do not exist yet.**
   Too small and it records every trip to the shop; too large and it starts
   recording once it is already too late. They are proposed with arithmetic, not
   picked.
4. **T-069 gets sharper, not softer.** The device now holds a map of its
   wearer's habitual places, and in Child Mode that is a map of a child's. Erase
   on return helps and does not answer it. The privacy question grew out of this
   decision rather than being resolved by it.

### 3. Saving a whole track is a second, independent feature

**What he decided:** he wants to be able to save a whole track on request and
look at it afterwards on a map. On request, so there is no restriction on how
the wearer is travelling — a car is fine here. Shaped as an application, allowed
to run in the background so other applications keep working while it records.

It is **not a mode of the first one**. Different consumer, different volume,
different behaviour when storage fills. Filed separately for that reason.

### 4. The background recording of §2 is configurable, and on by default

Whoever does not want it turns it off.

### What it changes

| | |
|---|---|
| **T-064** beacon profiles and the slot scheduler | **closed, `REJECT`** — by owner decision, recorded separately from the licensing and technical obstacles, which are real and are not why |
| **T-063** last-seen over BLE | stands, and is now the whole of "find my watch" |
| **T-065** `track/` | **unblocked** and re-sized by §2. The recording rule is a state machine over learned anchors, not a timer |
| **T-071** dead reckoning | **not blocked.** §2 answers question 3 without being asked it: everything is built around getting back, which is the one purpose that survives the physics. A disk around the last anchor, never a confident line |
| **T-066** one track, three carriers | unchanged in shape, cheaper in the worst case |
| **T-069** the tracker threat model | scope grows: learned anchors are stored personal history |

**What is explicitly not decided here.** The threshold, the hysteresis, the
dwell time and the sampling rate — all four are to be computed and shown. If the
arithmetic does not close on power or on storage, that is a `BLOCKED` with
numbers in it, not a quiet simplification.

---

## OD-14 — Which region is the owner's problem, not the firmware's

**Decided:** 2026-08-22, on [#55](https://github.com/hleserg/Attadipa/issues/55).

**What he decided:** legality is his problem, not the firmware's.

**What was asked.** [OPEN_QUESTIONS](OPEN_QUESTIONS.md) A4: which country or
regulatory region does the device operate in. The question was concrete rather
than theoretical — OD-2 already records the owner's own MeshCore node
transmitting 158 mW at 868.731 MHz, and whether that is lawful there has never
been established. The issue asked for a country name so this project could go
read the applicable rule and record it, the same as any other fact.

**What was answered, and what was not.** The owner declined to name one. That
is the whole content of the decision: **no country or region is coming**, now
or later, and this project stops asking. It is not an answer to "which region",
it is an answer to "whose job is it to know" — and the owner's is the answer.

**What this does and does not change.** It is tempting to read this as
licence to relax [ADR-0006](../adr/0006-settings-and-bounded-values.md)'s
transmit-closed-while-`Unknown` gate (final §35, §37), and that reading is
wrong. Nothing about *that* mechanism required this project to know which
region applies — ADR-0006 already rejected shipping a default region, rejected
compiling a jurisdiction into `core/`, and built the gate to hold exactly the
state this project is *in* rather than the state it hoped to reach. What the
owner's answer removes is the expectation that a **specific region's rule
table** was ever going to arrive from this side: nobody is going to research UK
or ETSI or GKRCh limits for this project and file them as a `RegulatoryProfile`
data record, because there is no region to research them for. The gate does not
need that answer to do its job — it needs to know that *some* profile was
chosen, never which one, and it stays exactly as ADR-0006 designed it: closed
until a profile is selected, by whoever configures the device.
[REUSE_LEDGER](REUSE_LEDGER.md) already calls this gate "Attadipa's single most
safety-critical line" after reading how Meshtastic's own version of it went
silently dead (issue #2205) — that finding does not become less true because
the owner named no country.

**What it obliges:**

1. **A4 is closed, permanently, as "operator's choice, not this project's
   research."** No task researches "which region" as a prerequisite for
   anything in `core/` or `apps/`. [OPEN_QUESTIONS](OPEN_QUESTIONS.md) A4 is
   updated to say so rather than left looking like a pending question with an
   owner who has not yet replied.
2. **The `Unknown`-blocks-transmit fail-safe is unchanged, and is not open for
   reinterpretation by a future agent reading this record loosely.** Any
   firmware built from this repository — the owner's or anyone else's, under
   `GPL-3.0-or-later` — still refuses to transmit until an operator has
   explicitly chosen a `RegulatoryProfile`. That protects users this decision
   was never about, not only the owner.
3. **Choosing and validating the specific profile for his own device is the
   owner's task, done through the settings mechanism ADR-0006 already
   specifies** — the same schema, the same typed bounds, the same three
   ceilings — when that mechanism exists. Nothing about this decision brings
   that forward; T-025 (partitions/settings persistence) is still not started.

**What it invalidates.** The framing in
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) that A4
is one of a batch of questions still awaiting an owner reply. It is answered —
just not with a country.

**Status:** documentation only. No code exists yet that ADR-0006 governs, so
there is nothing to change in `core/`; this record is the fence for whoever
writes `SettingsService` next.


---

## OD-15 — A7 and A8: the canonical palette wins, and the icon may lose its black corners

**Decided:** 2026-08-22, on [issue #57](https://github.com/hleserg/Attadipa/issues/57).

**What he decided:** for A7, what wins is whatever was already done last — no
need to redo it or re-verify it. For A8: yes, please remove the background from
the images where it needs it.

**A7 — which orange, which olive.** "What was already done last" is the
canonical palette: every colour in the design system and the firmware already
draws from final §42 (`docs/ui/DESIGN_SYSTEM.md`), and nothing in the codebase
had been changed to the sampled brand-art values. So **§42 wins**: Attadipa
Orange `#FF8A40`, Ink Olive `#2F3A2E`, and the rest of the canonical table
stand unmodified. Per the rule the question itself stated — the loser's values
must leave the repository rather than sit beside the winner — the sampled
values that [`pics/README.md`](../../pics/README.md) recorded (`#E16439`…
`#EC552A` for the wordmark and wings, `#595E3A`…`#666A46` for the head and
tagline) are removed from that file and kept only here, as the record of what
lost and why. The contrast arithmetic in `DESIGN_SYSTEM.md` §3.2 and
`tests/test_ui_tokens.cpp` needed no change, because it was already computed
against §42.

**A8 — transparent corners.** `pics/Ikon.png` and `pics/Favicon.png` were RGB
with no alpha channel, so the area outside the rounded square was opaque
`#000000`. Both were re-exported with an alpha channel: every near-black pixel
connected to the image border (RGB ≤ 50 per channel, flood-filled from the
edges) was made transparent; every pixel inside the rounded square is
byte-identical to before. Checked before re-exporting that no near-black pixel
in either file sits *inside* the mark disconnected from the border — there is
none, so the flood fill could not have eaten a real dark detail in the
artwork. New hashes are in `pics/README.md`. `AttadipaBanner.png` is untouched:
it is full-bleed, so the black-corner problem does not apply to it, and it was
out of scope for A8.

**What it obliges:** nothing further. Both questions were mechanical once
answered, and neither reopens a design decision that anything else depends on.

**What it does not do:** it does not touch `AttadipaBanner.png`, the
typeface question, or the other colour roles in the sampled-versus-canonical
table (glow, hills/leaves, background) — those were already "close" or
"between the two" in the original comparison and the owner's answer was about
the two that actually conflicted.

---

## OD-16 — A1, A2 and A3: no watch yet, SX1262 confirmed by listing, and three MeshCore nodes instead of one

**Decided:** 2026-08-22, on [issue #54](https://github.com/hleserg/Attadipa/issues/54).

**What he decided:** A1 — no watch of either kind yet. A2 — SX1262, MIA-M10Q.
A3 — there is a companion node, MeshCore Heltec T114 and Heltec V4.

The owner then posted a longer analysis of their own answer on the same issue;
this record follows that analysis rather than the three words alone, because
it is the more precise of the two and the owner asked for it to be recorded
"with the precision above."

**A1 — which boards, which revision.**

- **Waveshare ESP32-S3-Touch-AMOLED-2.06:** received — already recorded in
  `docs/research/WAVESHARE_BOARD_RECEIVED.md`. What is **closed**, and not by
  this answer, is board *identity*: the mainboard's silkscreen reads
  `ESP32-S3-Touch-AMOLED-2.06`, which is the product schematic V1.0 describes
  ([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.1, `VERIFIED`).
  **That is not the revision, and an earlier version of this bullet said it
  was.** `2.06` is the panel diagonal in inches, not a revision marker — the
  firmware reads it as one, `platform/src/board_profiles.cpp` setting
  `diagonal_milli_inch = 2060` — and a V1.1 of the same product would carry the
  same silkscreen. No revision field has been read off the unit, which is what
  `HARDWARE_MATRIX.md` means by *"cite the filename for provenance, never the
  title block for revision"*, written in the T-Watch section but not a T-Watch
  rule. An earlier draft also said the whole thing was still open, 100 lines
  above the *What it obliges* paragraph saying it was closed — the register
  answering one question twice, differently, within itself.
- **T-Watch S3 Plus:** **ordered, in transit — `ORDERED`, not `PRESENT`.**
  Nothing that needs the watch in hand moves yet.

**A2 — which radio, which GNSS.** **SX1262, 868 MHz** from the order listing;
**MIA-M10Q** from the owner, and the two do not rest on the same evidence. The
listing reads *"LILYGO® T-WATCH-S3 Plus умные часы, SX1262 (868MHz)"* — it names
the radio and is **silent on GNSS**, so the GNSS half is the owner's
recollection of the variant ordered rather than a quoted source, and it is held
to the same standard as the radio half: no GNSS module is treated as fitted
until the marking is read off it. **That gate is documentary, and the radio's is
not** — say so rather than implying a symmetry the code does not have.
`RadioChip::Unknown` is a value the firmware branches on
(`platform/src/radio_info.cpp`, `platform/src/board_profiles.cpp`); there is no
`GnssModule` enum anywhere in the tree, so nothing but this sentence holds the
GNSS half. If that is not strong enough, the fix is to add the type, not to name
one the next agent will grep for and not find. That gate carries more than the radio's, because
MIA-M10Q against LS550G decides a second PMU rail — the `DC4` row of the
T-Watch rail table in [HARDWARE_MATRIX](HARDWARE_MATRIX.md) reads *"LS550G GNSS
variant only, 850 mV"*, beside a `DC3` row that is unused on that variant — the
assistance mechanism ([VERIFIED_FACTS](VERIFIED_FACTS.md), the A-GNSS entries),
and which of T-051 and T-052 is the live task. Cited by row rather than by line:
this branch's own seven-line insertion into that table moved both rails, and the
citations left behind pointed at a blank line until review caught them. Checked
against [ADR-0003](../adr/0003-radio-not-lora.md)'s table: SX1262 is one of
the three genuinely-LoRa parts (CC1101 and Si4432 are FSK, not LoRa, and
CC1101 is additionally compiled out of this project's MeshCore build via
`-D RADIOLIB_EXCLUDE_CC1101=1`), and of the three LoRa parts it is the one
MeshCore supports at the pinned revision `d929643` — `CustomSX1262Wrapper`,
"the most common variant upstream." SX1280 has no wrapper at all; LR1121 is
`NeedsWork`. 868 MHz sits inside the driver's permitted 150–960 MHz range.

So `RadioChip::Unknown` becomes `RadioChip::Sx1262` and `MeshCoreSupport`
becomes `Supported` **once the watch arrives and the marking on the part is
read** — not before. An order listing is a claim by a seller, not a marking
read off the part, and this project's own rule (and ADR-0003's own point,
"an SX1262 board and an SX1280 board differ in the parts you cannot read over
SPI") is that only the latter counts as verified. Still true regardless of
that distinction: **there is no T-Watch variant in MeshCore** — 87 variants
upstream, none of them this watch. A supported radio chip removes the hardest
blocker; it does not make the T-Watch a build target.

**A3 — is there a second radio device.** **Three MeshCore nodes, not one**,
and the earlier "is there a USB node" framing is obsolete:

One Heltec V4 companion on [`dt267/MeshCore-Low-Power-Firmware`](https://github.com/dt267/MeshCore-Low-Power-Firmware),
and two Heltec T114s to be flashed with the latest official MeshCore.

**The fleet's record of record is [TEST_FLEET](TEST_FLEET.md) §1, not this
paragraph.** It reached `main` in `485dddb` from this same answer on the same
day, and it holds two operational facts this decision does not and must not
duplicate — because a copy that drifts is worse than a pointer:

- **the two T114s advertise over BLE under the same name**, so anything
  selecting a node by advertised name gets whichever answered first
  (`TEST_FLEET.md:29-32`, where they are the antecedent of "both"). Whether the
  V4 also answers to that name is not recorded, and it does not change the rule:
  **select by address**, which is correct for every node here under either
  reading;
- **a BLE pairing PIN is required and is deliberately not in this repository**
  (`TEST_FLEET.md:160-163`). This repository is public and a pairing PIN is a
  device access credential. The owner holds it; ask in the session that needs
  it.

The V4 reaches a host over BLE and USB, as both T114s do — the **T114** and
**V4** rows of the fleet table in [TEST_FLEET](TEST_FLEET.md) §1.
An earlier version of this table recorded the V4's links as `—`, which was
wrong. A second side drivable from a laptop is a test fixture, not just another
radio in the room.

> **Annotated 2026-09-01, [#124](https://github.com/hleserg/Attadipa/issues/124)
> — the answer above is preserved, and two of its facts are superseded.** Per
> OD-12 the decision text is not rewritten; what changed is recorded here.
>
> **The count.** The owner answered #124 on 2026-08-31: the fleet is **five
> nodes, four Heltec T114 and one Heltec V4.3** — not three. "Two Heltec T114s"
> above was a true count of what this repository knew in August, and every
> sentence written since that said *both* T114s quantified over a set of two
> that does not exist. The record of record is still TEST_FLEET §1.
>
> **The shared advertised name is withdrawn.** The bullet above says the two
> T114s advertise under the same name; that was never measured. The two names
> this repository has are `RESP_CODE_SELF_INFO` names read after pairing —
> `Beta test companion` and `✂️Beta Serega`, on a T114 and the V4.3 — and they
> differ from each other. The advertised name of every fleet node is `UNKNOWN`.
> **The half of the rule that survives is the prohibition: do not select a node
> by name.** It is stronger than before, because a name filter now has no
> measured string to filter on at all.
> **The prescription beside it is withdrawn — nothing here shows that address
> to be stable:**
> [`OWNER_DECISIONS.md:1138`](OWNER_DECISIONS.md) "select by address".
> The one this repository has measured is a *random* one — `peer_addr_type=1`,
> [`MESHCORE_T114_FIRST_CONTACT.md:47`](MESHCORE_T114_FIRST_CONTACT.md)
> "random", `MEASURED` 2026-08-28 — read once, from one node, and never read a
> second time to see whether it survived a power cycle or the factory reset of
> that same day. Whether a random address here is the kind that stays put or the
> kind that rotates is therefore `UNKNOWN`, and an address of unmeasured
> stability is not something to select by. **This paragraph claims no more than
> that**: the earlier draft of it asserted that a random address rotates, which
> is a protocol fact no source in this repository carries and which the single
> reading above cannot settle either way. The public key is the only measured
> identity that outlives a connection, and even that is regenerated by a
> factory reset:
> [`MESHCORE_T114_FIRST_CONTACT.md:50`](MESHCORE_T114_FIRST_CONTACT.md) "a factory reset regenerates it".
> **What to select by is therefore open, and it is
> [#304](https://github.com/hleserg/Attadipa/issues/304)'s question, not a
> settled rule** — this annotation withdraws an answer rather than supplying
> one.
>
> **The two line ranges cited above no longer land on their subjects.** They
> were written against `485dddb`. The shared-name claim was at `:29-32`; the
> pairing PIN is now §1b,
> [`TEST_FLEET.md:160`](TEST_FLEET.md) "A BLE pairing PIN is required".
> Both citations were bare ranges, which is why they rotted silently —
> `check_citation_lines` only requires the line to exist.
>
> **The flashing instruction is narrowed, and it was never a fleet-wide one.**
> "Two Heltec T114s to be flashed with the latest official MeshCore" above was
> written when the fleet was believed to be two. On 2026-08-31 the owner
> answered [#90](https://github.com/hleserg/Attadipa/issues/90#issuecomment-5482898591):
> the **free** T114 stays on the pinned `v1.17.1-d929643` and is not reflashed,
> so ADR-0003 keeps a node to re-verify against. The **Home Assistant** node is
> still covered. The **Room Server** and the **repeater** were not in the fleet
> this decision was taken over, are in service, and their firmware has never
> been read: **no decision covers writing to either of them.** Do not flash a
> node this paragraph does not name.
>
> **"Either unit" is superseded too, and the question it left open for the
> owner is closed** —
> [`OWNER_DECISIONS.md:1259`](OWNER_DECISIONS.md) "the fleet records only one".
> The owner answered it on 2026-08-28 in
> [#124](https://github.com/hleserg/Attadipa/issues/124): exactly one T114
> carries GNSS, and — correcting the same day — *«приемники есть и у t114 и
> v4.3 на обеих»*, both nodes actually on the bench carry receivers. So the
> pair #91's observation was made over is the GNSS-fitted T114 and the Heltec
> V4.3, not two T114s; the headless node is **not** credited with a receiver,
> and the disjunction resolves to its second reading —
> [`OWNER_DECISIONS.md:1260-1261`](OWNER_DECISIONS.md) "Either the headless node".
> The operational consequence recorded there is unchanged: no indoor fix from
> anything in this fleet. Whether the Room Server or the repeater carries a
> receiver is `UNKNOWN` and is not to be inferred from the other two.
> Both of those started as bare `:` numbers into this same file and were eleven
> lines out by the time the commit that wrote them had finished; the two above
> were bare too. A bare number carries no filename, so the citation check never
> saw any of them. Each names its file here and carries its quote on the
> citation's own line —
> a quote pushed to the next line of a blockquote is read by nothing, because
> the `>` sits where the check looks for the opening `"`.



`doctor` as a hostname names no node in this answer — the Home Assistant role
is a node's job, and the headless T114 inherits it.

**Two things this answer surfaces that the issue did not ask, raised as their
own issues per the owner's instruction rather than resolved here:**

1. **Three firmware revisions, not one** — filed as
   [#90](https://github.com/hleserg/Attadipa/issues/90). The companion runs a
   third-party low-power fork; the T114s will run official latest; this
   repository pins MeshCore at `d929643` (2026-08-14), the commit every
   ADR-0003 claim was verified against. "Official latest" is not that commit.
   Before any mesh result is believed, the pairing under test has to be named —
   fork-to-official, official-to-official, or either against the pinned
   revision — because a failure between two of them is a **firmware-compatibility
   finding** and a failure within one is a mesh finding,
   and conflating them produces a false bug report either way.
2. **Band has to match, and nobody has checked the T114s** — filed as
   [#89](https://github.com/hleserg/Attadipa/issues/89). The watch is
   868 MHz. If either T114 is a 915 or 433 MHz variant there is no mesh to
   test at all — not a weak link, no link. Band is set by "which
   band-specific matching network and antenna are fitted" (ADR-0003), which is
   not readable over SPI; the order record or a label on the module settles
   it. Whether the T114s carry an SX1262 at all is likewise unconfirmed here.

**A hardware constraint recorded here so it is not rediscovered as a bug** —
filed as [#91](https://github.com/hleserg/Attadipa/issues/91): no T114 gets a
GPS fix indoors — owner-observed, 2026-08-22. **The observation as given said
"either unit", and the fleet records only one T114 as carrying GNSS at all**
([TEST_FLEET](TEST_FLEET.md) §1). Either the headless node has a receiver the
fleet table does not credit it with, or "either unit" was a manner of speaking.
The operational consequence is identical under both readings — no indoor fix
from anything in this fleet — so nothing downstream is blocked; but #90 and #91
read that table, so which it is stays **open for the owner** rather than being
picked here. This
is not a GNSS defect. Any position-dependent test run from indoors must either
inject a fix, mock the source, or be marked `NOT EXECUTED — HARDWARE REQUIRED`
with reason "requires outdoor conditions", not "requires hardware" — the board
is present; the sky is the missing part. Filing it as a power-rail bug (a real
failure mode on the T-Watch, per A1) would waste a day chasing the wrong
cause.

**What it obliges:**

- [`OPEN_QUESTIONS.md`](OPEN_QUESTIONS.md) A1's **presence** half is closed for
  the Waveshare and open for the T-Watch. Its **revision** half is open for
  both. What the silkscreen closes is identity: it reads
  `ESP32-S3-Touch-AMOLED-2.06`, which is the product schematic V1.0 describes
  ([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.1, `VERIFIED`) —
  a product name, not a revision field, and the `2.06` in it is the panel
  diagonal. CLAUDE.md asks for *"a schematic for the specific board
  revision"*, so a V1.0-derived row is evidence about a document until somebody
  reads a revision marker off this unit.
  What is still unread is narrower and now filed as **D19**: the display-FPC
  part marking, which needs a loupe. **Not U2 and U3** — the eFuses and the
  JEDEC ID already answered those on this unit
  ([WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.2–1.3), and a lid marking
  read through a loupe is weaker evidence than a fuse read of the die under it.
  A2 and A3 move to RESOLVED, pointing here.
- **A divergence to record rather than paper over.**
  [ADR-0003](../adr/0003-radio-not-lora.md) still lists A2 as open at
  `../adr/0003-radio-not-lora.md:109-111`, `:265` and `:270-271`. Its stated
  reason is not "no marking read" but ownership: the last of those,
  `../adr/0003-radio-not-lora.md:270-271`, calls A2 *"the owner's to answer"*,
  and the owner has now answered
  it. So the divergence is narrower than it looks — what the ADR is still
  waiting for is the evidence it treats as decisive, a marking read off the
  part, and this decision supplies a seller's listing instead. No ADR edit is
  asked for here: the two documents disagree because they are answering to
  different standards of proof. The board-bring-up issue must carry the marking
  read that discharges it; the mesh cost spike is unrelated and never mentions
  A2.
- Three follow-up issues, filed separately rather than folded into this
  record: the T114 band check
  ([#89](https://github.com/hleserg/Attadipa/issues/89)), the
  three-firmware-revision compatibility matrix
  ([#90](https://github.com/hleserg/Attadipa/issues/90)), and the indoor-GNSS
  constraint documentation
  ([#91](https://github.com/hleserg/Attadipa/issues/91)).

**What it does not do:** it does not make the T-Watch S3 Plus a build
target — it is not in hand yet — and it does not read a single part marking on
either board, which is what the remaining questions turn on, regardless of this
answer.

---

## OD-18 — The received unit stays powered, with its brightness at minimum

**Decided:** 2026-08-23, in session. Raised by the owner, unprompted, while the
unit sat on the desk showing the vendor firmware's desktop.

**The concern first.** While the coding goes on, the watch lies powered showing
one and the same desktop picture, and this project had itself said an AMOLED can
be burned that way. Could it be switched off between runs, or the screen blanked?
It would be good to make that a rule.

**And then the decision, after the options were laid out:** he found the screen
brightness **in the settings**, turned it to minimum, and asked whether at low
brightness the panel is safe. He does not want it switched off — he wants the
hardware available for a code run whenever one is needed.

**"In the settings" is the load-bearing half of that sentence, and it is not a
figure of speech.** It is the only thing anywhere in this repository saying the
factory launcher has a settings menu, that it contains at least a brightness
control, and that a human being has been inside it. Nothing else has ever
enumerated it — which is why the paragraph below can say a display timeout in
that menu is `UNKNOWN` and unobserved rather than absent.

**What it obliges:**

1. **The unit stays powered and attached.** Availability for a hardware run was
   chosen over the safer state, knowingly. An agent does not power it down, does
   not ask for it to be unplugged, and does not treat "unplug it" as the
   recommendation when reporting on this.
2. **Minimum brightness is the mitigation in force**, set by the owner in the
   vendor firmware. Nothing of ours can *deliberately* change it, because
   nothing of ours runs on the unit — but **whether it survives a reset is
   `UNKNOWN`**, and write-inability is not persistence. It is a runtime setting
   in a launcher's `Settings` app; nothing establishes that `phone_s3_box_3`
   commits it. **The vendor BSP's init table brings the panel up at
   `0x51 = 0xFF`, 100 % — and that is corroboration this item may not use**, for
   the reason this same branch spent a round on: the unit does not run the BSP,
   it runs `phone_s3_box_3`, and evidence about a program that is not on the
   device is not evidence about the device. It is left in only as the reason a
   reset is *worth looking at*, not as a prediction of what it will show.
   `UNKNOWN` is right either way, and only for the first half of this sentence.
   A bench session is *allowed* to reset the unit — the RAM-load route
   enters the ROM downloader, and opening a port does it by accident — so this
   is reachable rather than theoretical. Until somebody looks at the panel after
   a reset, **a session that reset the unit must not report the mitigation as
   still in force**; it must say it reset the board and that the brightness is
   unconfirmed. **There is a cheaper route than eyes on a panel, and its baseline is already
   taken.** [VERIFIED_FACTS](VERIFIED_FACTS.md), *"The stock firmware does not
   rewrite its own configuration partitions on boot"*, hashes `nvs`, `otadata`
   and `phy_init` (`0x9000`–`0x12000`) to
   `803798ee52013c09e9dd55a72226d0195ec6a3582f85af3b43315f9247b3e26e` across
   three reads on **2026-08-22** — the day *before* the brightness was set to
   minimum.

   **That range is three of the five data partitions, and the negative needs
   all five.** `0x9000`–`0x12000` stops at the byte where `model` begins;
   [WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §2, the factory partition
   table, lists two more data partitions outside it — `model` (952 KB spiffs) and **`storage`** (6 MB
   spiffs, UI assets) — and nothing establishes where `phone_s3_box_3` commits a
   runtime setting. On the branch whose thesis is that this image is opaque, an
   unchanged 36 KB says the setting is not in *those* 36 KB, not that it was
   never written. **So the baseline to re-read is the whole flash**, which is
   already taken and is stronger: `WAVESHARE_FLASH_LAYOUT` §2.2 hashes all
   33 554 432 bytes to
   `2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5`, agreed by
   three independent complete reads and by the device's own MD5, on the same
   2026-08-22 and likewise before the brightness was set.

   **Its decisive branch is also its least likely one, and the source it comes
   from says so.** Re-read it on the next trip: an unchanged whole-flash hash
   means the setting was never committed to flash at all, so it cannot survive a
   reset, and no reboot has to be watched to learn it — a negative a pair of eyes
   cannot give. But that branch is reachable only if **nothing else in 32 MB
   moved**, and nothing establishes that. `WAVESHARE_FLASH_LAYOUT` §2.2 makes the
   point against itself: *"on a live device it also mixes in partitions the
   firmware is entitled to rewrite."* The across-reboot evidence this repository
   actually holds belongs to the **36 KB** range and not to the image — `nvs`,
   `otadata` and `phy_init` identical across three reads separated by hard resets
   and ~90 s of running ([VERIFIED_FACTS](VERIFIED_FACTS.md), S12) — whereas the
   whole-image reads §2.2 describes were taken back to back, plus the owner's
   Windows pass. `storage` alone is 6 MB of UI assets and nothing says ordinary
   use leaves it untouched.

   So the trip is worth taking and the shortcut is **not** cheap and decisive:
   read the whole image *and* the 36 KB range. A changed whole-flash hash — the
   likely outcome — proves only that something was written, so the panel has to
   be looked at anyway, and the 36 KB range is then what says *where*. The
   fallback is safe either way, which is why this is a wording correction and not
   a `MEASURED` that would have been wrong. Round fourteen of
   [#134](https://github.com/hleserg/Attadipa/pull/134); an earlier version of
   this paragraph closed *"for a negative, a whole-image hash is the strongest
   form there is"*, which argued against the section it cites.

   **And the trip is not free in the other direction either**: running our code
   to take the read resets the unit, which is exactly what reopens the brightness
   `UNKNOWN` item 2 records. The read that goes to close it costs the state it
   was closing.

   The cost is the honest part: a full 32 MB read, not one 36 KB dump. It is the
   same read the backup already needed and takes minutes over USB/IP, but it is
   not free, and the twelfth review round proposed the cheap version while the
   thirteenth found the shortcut scoped to three of five partitions and worded as
   though it covered flash. Taking the cheap dump and writing *"the brightness
   setting is not persisted — `MEASURED`"* would be a `PASS` for a state nobody
   observed, in the register `CLAUDE.md` says is not ours to overturn.

   `WAVESHARE_FLASH_LAYOUT` §2.2, under
   *"`nvs`, `otadata` and `phy_init` did not move either"*, is the precedent for
   the **method** and not a missed chance: the owner watched the unit through
   six download-mode cycles on 2026-08-22, and that is how the `nvs` result
   stopped being a guess. It could not have settled the brightness — the
   brightness was set to minimum on **2026-08-23**, this decision's own date, so
   what those cycles would have recorded is 100 %. What the precedent says is
   that one pair of eyes on the panel through one reset turns this `UNKNOWN`
   into a `MEASURED`, and that the next reset is the cheap opportunity.
3. **The question in it was answered honestly and stays answered that way.**
   "They will not be ruined, right?" is `UNKNOWN`, not "safe" — no lifetime
   figure exists for this panel, D7 has not settled even its initialisation
   sequence, and class figures for "AMOLED" are not this part's. Minimum
   brightness slows ageing at least in proportion to luminance; it does not stop
   it. An agent must not upgrade that to a reassurance.
4. **The rule the owner asked for exists**, narrowed to what an agent can
   actually do:
   [`../hardware/BENCH_HANDLING.md`](../hardware/BENCH_HANDLING.md). An agent
   cannot blank the panel — there is no Attadipa firmware on the unit — so the
   obligation is to *say* at the end of a bench session, or before a long stretch
   that does not need the unit, that it is sitting lit. The action is the
   owner's.

**What it invalidates:** the preference table in `BENCH_HANDLING.md` ranks a
screen timeout first and unplugging last. That ranking stands as the general
case; for **this** unit the owner has chosen the second row, and this decision
outranks the table. It does **not** settle row 1: whether the factory launcher
offers a display timeout is `UNKNOWN` and unobserved — the menu the owner opened
to find the brightness has never been enumerated, and nothing here should be
read as saying there is nothing in it.

**What it does not decide:** nothing about the product. Whether the firmware
ships an idle dim, a screen timeout, pixel shift or an always-on face is
[WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §3.5, and is **A10** in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) — *"what does Attadipa do about static
content on the AMOLED?"*, asked as
[#53](https://github.com/hleserg/Attadipa/issues/53). **Whether A10 is still
open is not this decision's to say, and the register is about to disagree with
itself if it tries:** [#97](https://github.com/hleserg/Attadipa/pull/97) carries
an `OD-16` that *answers* A10 — the display wakes on raise, button and touch —
so landing both leaves one file calling A10 open beside one closing it. Whoever
merges second renumbers and reconciles; this paragraph records what the owner
decided **here**, which is about the unit on the desk and not about the
product. Its neighbour **A9** (§1,
[#52](https://github.com/hleserg/Attadipa/issues/52)) is the different question
of whether the day theme keeps its near-white page. All four items above are
A10's; none is A9's. This is about a board on a desk, and decides neither.

**What was not weighed when it was decided.** Every option put to the owner was
about the panel, and so is every obligation above. The unit also has a **cell**
fitted, on a rail with no disconnect switch and a charger this repository calls
opaque — so *"stays powered and attached", indefinitely* has a second consumable
in it that nobody costed, and the option that was ranked last, unplugging, does
not even stop the first one. The facts are in
[`../hardware/BENCH_HANDLING.md`](../hardware/BENCH_HANDLING.md) under *"What is
not established"*, and they stay `UNKNOWN` there: no register on that charger has
been read, and neither state is established as the kinder one. This is **not** a
reason to reopen the decision — the owner chose availability knowingly and that
choice stands. It is here so that the decision is not read as having weighed a
question it was never asked.

**Answered by the owner: the second consumable.** Recording it in
`docs/` and closing the paragraph is what round 15 of #134 called wrong, and it
is right: a fact nobody was asked about does not become weighed by being written
down. No recommendation is offered and none is possible — neither state is
established as kinder — so this is one question and not a proposal.

> **English.** The unit has a cell fitted on `VBAT1`, which has no disconnect
> switch, no protection FET and no fuel gauge, and `TS` is tied to `GND` so the
> charger never sees cell temperature. `0x63[4]` has not been read, so indefinite
> CV float is not excluded. Nobody has read a register on that charger, and
> nothing here says which of "left plugged in" and "unplugged" is kinder to the
> cell — the first is `UNKNOWN`, and the second does not stop the panel ageing
> either, so it is not an escape. **The question is only this: do you want the
> charger's registers read on the next bench trip, so that the cell half of
> "stays powered and attached" stops being `UNKNOWN`?** It is a read, nothing is
> driven, and it costs one session. A "no" is a complete answer and closes this.
>
> **По-русски.** В плате стоит аккумулятор на `VBAT1`: нет разъединителя, нет
> защитного FET, нет топливомера, а `TS` посажен на `GND` — зарядник вообще не
> видит температуру ячейки. Регистр `0x63[4]` не читали, поэтому бесконечный
> CV-float не исключён. Никто не читал ни одного регистра этого зарядника, и
> здесь нигде не сказано, что для ячейки лучше — «оставить в USB» или
> «отключить»: первое `UNKNOWN`, а второе всё равно не останавливает старение
> панели, то есть это не выход. **Вопрос ровно один: прочитать регистры
> зарядника в следующий заход на стол, чтобы «стоит подключённым» перестало быть
> `UNKNOWN` со стороны ячейки?** Это чтение, ничего не подаётся, стоит одну
> сессию. «Нет» — полный ответ и закрывает вопрос.

Raised 2026-08-24. **Answered the same day: read them.** The owner chose the
read, so the cell-safety registers stop being something a bench session might
have room for and become something the next trip to the bench owes. No new task
was filed, because the read already had an owner: `T-106`'s eight-register leg
carries `0x61`, `0x62`, `0x63` and `0x64` at I²C `0x34`, and that bullet is
where the answer is recorded as a promise rather than a convenience. **Nothing
about OD-18 changes** — the unit stays powered and attached, by the owner's
decision, and the read does not reopen it. What the read closes is narrower than
the question that prompted it: it establishes what the running image left in
those registers, which is the cell half of *"stays powered and attached"*; it
does not establish that either state is kinder, and no reading of it should be
written as if it had. Until the burst is actually taken every figure stays
`UNKNOWN`: an answered question is not a measurement. Like the item below it,
this is written into the register rather than into a pull request comment because
a squash merge keeps the file and discards the comment.

**Answered by the owner: this register is published, and its convention has
changed.** `docs/` is the GitHub Pages root for this repository —
`docs/.nojekyll`, `docs/robots.txt` with `Allow: /` — so every file here,
including this one, is served from `https://hleserg.github.io/Attadipa/`. That
means the owner's own words, quoted verbatim as this register's convention used
to require and as OD-1 still does, were on the project's public website. Raised
2026-08-23; **answered 2026-08-24**, and the answer was the third of the three
options put to him: keep the register published, keep the decisions in it, and
carry his words as **paraphrase** rather than quotation.

So the convention has changed, and the change is his rather than an editorial
preference of ours: **a decision records what the owner decided, in this
repository's own words. It does not reproduce how he said it.** Retrofitting the
register to that convention is a **separate pull request, opened on top of this
branch** — it touches every quoted decision from OD-1 down, and this branch is
fifteen review rounds deep on an unrelated subject. Two categories are outside
it, because neither is quotation of chat: the owner's own authored documents
checked into the tree (`docs/master-prompt-final.md`, `docs/master-prompt.md`,
`docs/development-addendum.md`, `docs/ideas/`), which are his text and not our
record of it, and Russian prose that merely uses guillemets rhetorically. It is
recorded here rather than left in a pull request comment because a squash merge
keeps the file and discards the comment, and because this is the register the
unattended sweep is denied — which is the point of putting an owner-facing
question in it.

**A known asymmetry, recorded rather than worked around.** This decision lives in
this register, which
[`merge-candidate.sh`](../../.github/scripts/merge-candidate.sh) denies to the
unattended sweep — so changing it takes an orchestrator. The rules that *act* on
it are in `docs/hardware/`, which the same script allows, so a later edit
softening the `UNKNOWN` to "safe", or relaxing the must-not-reboot-the-owner's-
device rule, needs only an `ai-review:pass`. Widening or narrowing that allowlist
is the owner's decision, not an agent's and not a reviewer's, so this is written
down as a thing to watch rather than quietly routed around.

---

## Still with the owner

Nothing here answers the compass question (A6), which remains in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md). **A5 is answered** — separately, on the
same day, on [#83](https://github.com/hleserg/Attadipa/issues/83): an external
magnetometer is intended and two candidate parts are ordered. Not by *this*
decision, which is why an earlier version of this paragraph still listed it as
open.

OD-7 to OD-10 add three of their own, and they are the kind that cannot be
answered from a datasheet: whether Meshtastic's protocol definitions are licensed
separately from its firmware, which cellular module the node will carry, and
which tower database may lawfully be shipped in a product. The first is research
and is filed; the last two are the owner's.

---

## OD-17 — A5 and A6: a watch retrofit may have a magnetometer; the node will not

**Decided:** 2026-08-22, on [#56](https://github.com/hleserg/Attadipa/issues/56),
with the ordered watch modules recorded on [#83](https://github.com/hleserg/Attadipa/issues/83).

**What he decided:** a magnetometer is an external module fitted to the
Waveshare watch, not a capability of either stock board. The two candidate
modules are CJMCU-9911 (AK09911C) and GY-271 (QMC5883L); choosing a part and its
placement remains open. This does not promote an unmodified board from absent to
present.

The Attadipa node will not carry a magnetometer. A third-party companion can
report its own heading, but cannot become `WatchBody` heading without a known,
calibrated, valid body transform; [ADR-0009](../adr/0009-heading.md) continues
to enforce that boundary. This is decision-only: no soldering, driver or
hardware claim follows without measurement.

---

## OD-20 — A10: wake the display on raise, button, or touch

**Decided:** 2026-08-22, on [#53](https://github.com/hleserg/Attadipa/issues/53).

**What he decided:** the display is off by default and wakes for a wrist raise,
button press or touch. Raise-to-wake uses the accelerometer signal only; it does
not decide an always-on face, brightness values or their hardware measurements.

---

## OD-19 — A bench-attached agent may flash and test Attadipa firmware

**Decided:** 2026-08-24, by the owner.

**What he decided:** a session with the physical board may flash and exercise
Attadipa firmware, including display/control tooling, and report what it
observed. A board result is `MEASURED`; a cloud-only session remains `NOT
EXECUTED — HARDWARE REQUIRED`.

**Boundary:** reflashing is permitted because the factory image is backed up.
Burning eFuses, enabling secure boot or flash encryption, writing production
secrets, or destroying keys still each require an explicit owner request.

---

## OD-26 — Owner consent for provisioning is a finger on the watch's own screen

**Decided:** 2026-09-02, by the owner, in conversation, after
[ADR-0018](../adr/0018-owner-consent-for-provisioning.md) put three priced
mechanisms in front of him.

**What he decided:** a production image establishes owner consent by the holder
**entering the value on the watch itself**. The wall clock and the MeshCore
passkey are both provisioned that way. The two alternatives — a BLE peripheral
showing a code on the watch's screen, and a USB channel narrowed to
provisioning opcodes inside a gesture-opened window — are not taken.

**What prompted it:** #346 removed the unauthenticated USB control plane and
established that a cable is not consent. #356 recorded the consequence: a
product image could then neither set its clock nor receive a passkey. He was
asked for the mechanism before any of it was built, and answered after reading
the ADR rather than before it — he had asked for the analysis first and the
implementation second.

**What it obliges:** #356's implementation adds an on-device entry screen — a
second LVGL face beside `ui/lvgl/clock_face.cpp`, with its application half in
`apps/` — in both languages ([OD-24](#od-24--language-follows-the-reader),
[ADR-0010](../adr/0010-localization.md)), and storage for what was entered. That
storage is two different things and neither is the largest item. The passkey's
storage is one entry in the `attadipa_mesh` namespace #304 already created; what
it has no part of is a seam an application may use to reach it — `core::`
carries no method that arms a passkey and the only writer is a
`firmware/main/` header — so the passkey half is a `core::` provisioning method
and the firmware provider behind it, or it is `apps/` reaching into firmware,
which this repository does not allow. The
clock half means moving `write_rtc()`'s caller and `save_time_metadata()` out
from behind `CONFIG_ATTADIPA_WATCH_CONTROL` — the same symbol that gates the
debug bridge — and showing `firmware_elf_check.py` still keeps
`attadipa::debug::Bridge::handle` out of a product image.
It does **not** add a listener of any kind to a product image: no BLE peripheral
role, no provisioning endpoint, no bounded window, and nothing to authenticate,
because nothing accepts input except the panel.

**What it invalidates:** ADR-0014's sentence naming
`watch_control.py sync-time` as the first real time input. That sentence stopped
being true of the product image at #346, not at this decision —
`firmware/sdkconfig.defaults:89` — "CONFIG_ATTADIPA_WATCH_CONTROL=n" — so
ADR-0014 now points here for what replaces it. The sentence itself is rewritten
when the entry screen ships, so it and the code change together.

## OD-25 — The independent review gets five rounds, then it files rather than holds

**Decided:** 2026-08-31, by the owner, in conversation.

**What he decided:** the independent review may hold a pull request for at most
**five rounds**. From round six on, no open finding blocks the merge — a `floor`
finding included. Findings are still published, still recorded in the ledger and
still filed as the follow-up issue the review already produces; only the "holds
the merge" column changes. What he weighed: shipping a mistake and fixing it
costs less than proof-reading one change fifteen times over, so past five rounds
the merge goes ahead unless something risks a serious breakage.

**What prompted it:** #338 ran **sixteen** rounds. The convergence floor of #169
(OD is `review-verdict.sh`'s `FLOOR`, set to 4) caps which *categories* may hold
a pull request late, not how many rounds there can be, and a `floor` finding
blocks at any round however late. That rule did converge in the sense that each
round found less, but every round's own fix minted the next round's floor
finding inside the same document — round 14 fixed five and created three, one of
them `floor`. Sixteen review cycles at roughly twenty minutes each is over five
hours of wall clock for a documentation change.

**Amended 2026-09-01, by the owner, in conversation:** the five rounds are five
rounds of *reviewing*. Until this amendment the cap was applied after the model
had answered — the sixth round ran in full, and only then did the rule defer
every finding it had just paid to produce and return `ai-review:pass`. #382
bought three such rounds. What he decided: five rounds of *reviewing*, and if
the fixes made after the fifth leave no dangerous bug behind, the pull request
merges as it stands. A sixth round is no
longer run at all: `attadipa_review_gate` reads the round out of the same ledger
before the model is invoked, and the workflow skips the paid step and hands over
the verdict round six could only have reached.

**What this does not change:** what the reviewer is asked to look for, the floor
list itself, the rule that a deferred finding never ages into a blocker, and
which findings hold a pull request in rounds one to five. The ceiling caps
holding and, since the amendment, reviewing. It has never capped *recording*:
every finding still open when the cap falls stays in the ledger comment on the
pull request, and the cap posts a note saying so rather than letting a skipped
review read as a clean one.

**One claim in this entry was never true.** "Still filed as the follow-up issue
the review already produces" describes a mechanism that is not wired:
`review-verdict.sh` renders the follow-up body to the path its caller passes, and
`claude-pr-review.yml` passes `/tmp/deferred.md` and never opens it again. The
recording OD-25 relies on is the ledger comment, which is real and is what the
cap's note cites. Filing the issue is worth doing and is not part of the cap;
recorded here so the next reader does not take the promise for the mechanism.

**Where it lives:** `CEILING` in `.github/scripts/review-verdict.sh`, passed as
`5` from `.github/workflows/claude-pr-review.yml` to both the verdict and the
gate, asserted in `.github/tests/review-verdict-test.sh`. Raising or lowering it
is an owner decision; edit this entry when it changes.

## OD-24 — Language follows the reader

**Decided:** 2026-08-22, by the owner; recovered from closed-unmerged PR #126.

**What he decided:** repository artefacts read by CI or other agents are English;
the owner is addressed in Russian in conversation, reports and questions. A
public GitHub comment addressed to the owner is bilingual, English first, with
machine-readable markers and field names kept in English. The owner's own
specification documents and files under `docs/ideas/` remain verbatim in
Russian. This is not a product-localisation decision; `l10n/` and ADR-0010
govern device strings.
