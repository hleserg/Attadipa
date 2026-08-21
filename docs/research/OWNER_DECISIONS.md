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

---

## OD-1 — There is a separate Firefly node, and the watch uses it

**Decided:** 2026-08-21.

**As stated:**

> «там будет отдельная нода с lora, GPS и esp32, часы будут подключаться к ней и
> использовать те же приложения, типа карты, компас, и проч, что и в lora часах.
> когда нода не подключена — будут часами, аудиоустройством и прочим зависит от
> установленных приложений которые мы ещё не написали. все возможности должны
> быть учтены на уровне ядра, а реализовывать будем уже позже.»

**In English:** a separate node carrying LoRa, GNSS and an ESP32 exists. The
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
requires it — §32 *DOCTOR / FIREFLY NODE* mandates that the architecture account
for a separate node, and lists "additional GNSS" among what it provides.

---

## OD-2 — MeshCore radio parameters are settings, not constants

**Decided:** 2026-08-21.

**As stated:** «Вот настройки для MashCore, но они не должны зашиваться в ядро,
это настройки» — *these are the MeshCore settings, but they must not be baked
into the core; they are settings.*

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

### What the model gets right, and Firefly must copy

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

### What the model is missing, and Firefly must not copy

This is a fine inventory of what a node *has*. It is not sufficient as a core
data model, and the gaps are instructive because each one is a decision the
Firefly core has to make deliberately:

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
It is the argument for §32's requirement that the Doctor/Firefly application
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
established. Firefly is not responsible for that node, but it must not ship a
default that assumes it.

---

## OD-3 — A new master specification, and a review of the work so far

**Decided:** 2026-08-21.

**As stated:** the owner supplied `FireflyOS_Master_Prompt_Final_Bundle.zip`
containing a 3 125-line specification and three PNGs, with the instruction
«так, в архиве ревью, сделай все по промту от туда» — *the archive contains a
review; do everything according to the prompt in it.*

**What arrived:**

| File | Now at | SHA-256 |
|---|---|---|
| `FIREFLY_OS_MASTER_PROMPT_FINAL.md` | [`../master-prompt-final.md`](../master-prompt-final.md) | `65675d49604ba217e5ca7288621ab33d8655f0659e61f2ce795eec27b42312ed` |
| `design_refs/firefly_brand_identity.png` | [`../ui/reference/`](../ui/reference/) | `d9a51f7b69b3566d366e9f9c2d27d375579152e2fdf5c3a46c46ec16112c880e` |
| `design_refs/firefly_visual_style_board.png` | [`../ui/reference/`](../ui/reference/) | `4e66f2a4b09038bb4e94f2dd097733a987a714c13572df68766900f75b84c2b9` |
| `design_refs/firefly_mascot_sheet.png` | [`../ui/reference/`](../ui/reference/) | `175f7cfd9343973e65242843ad697bc9646b4ba2a312f78c42de8e6f2024684a` |

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
*model*, not the measurements. And [OD-1](#od-1--there-is-a-separate-firefly-node-and-the-watch-uses-it)
is untouched: final §3 and §9 restate it almost word for word.

---

## OD-4 — Synchronise with upstream MeshCore before continuing the roadmap

**Decided:** 2026-08-21.

**As stated:** «ПЕРВАЯ ЗАДАЧА — выполнить до дальнейшей разработки OS» — the
first task, to be done before further OS development. Stop the roadmap and
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
   experimental.** «Не считай, что последний релиз — лучший» — do not assume the
   latest release is the best. Check for open regressions, the FEM RX gain path
   in particular.
3. **Do not pull unmerged code into production Firefly without analysis.**
4. **Build a compatibility layer** so MeshCore can be updated without rewriting
   the OS: `UI/Apps → Services → Mesh Service API → MeshCore Adapter →
   transports → HAL`.
5. **Produce `docs/upstream/meshcore-1.17-review.md`** with a status per item:
   `adopt / adapt / monitor / reject`, then file each required Firefly change as
   a separate small task.
6. **Do not stop at the review.** Fix the critical architectural errors, add
   regression tests, build, run, fix, and continue. The order is stated as a
   principle: **Research → reuse proven implementations → adapt → test → only
   then invent.**

Four specific instructions inside it are narrower than the rest and are recorded
verbatim in effect, because each forbids something that would otherwise look
reasonable:

- **Transport is not BLE.** Firefly's must admit BLE, USB, UART, Wi-Fi/TCP and
  possibly ESP-NOW, several at once — «не копируй слепо», do not copy #3049
  blindly.
- **No own LBT yet.** «Не реализовывай собственный LBT, пока не станет понятно,
  что можно безопасно взять из MeshCore.» Hardware CAD stays experimental while
  upstream ships it off.
- **Do not port the old FEM implementation.** «Не переносить старую
  реализацию.» FEM/LNA is a **board capability**, never an SX1262 assumption.
- **Hibernate is not a sleep with the radio armed.** «Не смешивай "сон с
  пробуждением по LoRa" и настоящий hibernate.» And, separately: **«wall clock
  нельзя использовать для измерения elapsed time»** — the monotonic clock owns
  timers, timeouts, retries, connection expiry and the scheduler.

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
fast-start, and security features, and Firefly must use them. The priority order
is explicit: **receiver-native mechanisms → Firefly's independent detectors →
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
   what the receiver reports — both a normalized Firefly representation *and*
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
universal spoofing detector. «Текущий milestone не ломать» — do not break the
current milestone. What is to be done now is the architecture and the tasks:
record the decision, check the existing `GnssDriver` / `LocationService` shape,
stop the interfaces losing integrity information, file the receiver research,
add the descriptor, add the trust state, add the simulator's fault scenarios,
fix the RTCM assumption — and then carry on.

**What it invalidates:** the assumption that RTCM belongs to a generic GNSS
driver, wherever this repository has written it.

---

## Still with the owner

Nothing here answers A1–A3, A5 or the compass question. Those remain in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).
