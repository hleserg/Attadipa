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

## OD-1 — There is a separate Attadipa node, and the watch uses it

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
requires it — §32 *DOCTOR / ATTADIPA NODE* mandates that the architecture account
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

**As stated:** the owner supplied `Attadipa_Master_Prompt_Final_Bundle.zip`
containing a 3 125-line specification and three PNGs, with the instruction
«так, в архиве ревью, сделай все по промту от туда» — *the archive contains a
review; do everything according to the prompt in it.*

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

Four specific instructions inside it are narrower than the rest and are recorded
verbatim in effect, because each forbids something that would otherwise look
reasonable:

- **Transport is not BLE.** Attadipa's must admit BLE, USB, UART, Wi-Fi/TCP and
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
universal spoofing detector. «Текущий milestone не ломать» — do not break the
current milestone. What is to be done now is the architecture and the tasks:
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

**As stated:** *"учти кстати что шагомер должен быть в часах обязательно"* — a
pedometer is a mandatory feature of the watch, not a nice-to-have and not a
later milestone.

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

**As stated**, across three messages:

> *"а почему бы нам не добавить в часы возможность цепляться к нодам meshcore на
> ванильной прошиве по ble или lan? Не каждый захочет заморачиваться со сбором
> нашего варианта ноды сразу. Да и нам может пригодиться. В план!"*

> *"Нужно чтобы часы при этом получали сразу возможность общаться по мешкору,
> возможность запрашивать и получать телеметрию, возможность снимать координаты
> из телеметрии, входящих сообщений и с самой ноды если в ней есть gps."*

> *"При наличии lora и меш в самих часах я бы хотел чтобы была возможность
> пользоваться обеими нодами, на часах пусть так же будет доступен meshtastiс
> как вариант компаньона вместо (или вместе, как получится) meshcore."*

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
   firmware is GPL-3.0 — *read it, learn from it, copy nothing*
   ([REUSE_LEDGER](REUSE_LEDGER.md)). Whether Meshtastic's **protocol
   definitions** carry the same licence as the firmware is `UNKNOWN` and is the
   gate on a Meshtastic client. It is verified from the files or the client is
   not written.
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

**As stated**, across two messages:

> *"При наличии gps в часах - так же хорошо бы иметь возможность выбрать какой
> gps использовать и еще как вариант - использовать оба источника данных
> комбинируя и обрабатывая их для улучшения точности. Если возможно - та же
> история с телефоном - нужно иметь возможность снять gps координаты (и прочие
> полезные и доступные данные) и обработать на уровне часов. Они превращаются в
> основной навигационный инструмент."*

> *"По поводу agps - надо заложить возможность их получения не только по
> интернету но и по другим каналам связи, ble, lora и проч. На всякий. Буду
> стараться как-то их получить и пропихнуть в любом случае."*

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

**As stated:**

> *"я вероятно пихну в ноду gsm/4g/lte короче мобильную связь. Это будет
> во-первых - один из вариантов уточнения позиции (опрашиваем вышки, по
> идентификатору смотрим ее координаты в базе которая заранее качается из
> интернета) а во-вторых, один из вариантов выйти на связь, получить agps, и тому
> подобное."*

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

**As stated:**

> *"надо чтобы у стоящего на месте человека (определять по акселерометру можно)
> gps координаты брались реже, если есть точные и доверенные координаты - то
> вообще можно не переспрашивать пока он не двинется с места. При этом конечно
> не допускать холодного пуска желательно очень, т.е. не выключать модуль совсем
> или держать agps на готове как-то. В общем на подумать это"*

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

**As stated:**

> *"мы сделаем красиво, но всем понравиться с одним единственным вариантом не
> получится. нужно заложить в ядро возможность смены темы. и сделать эти темы
> скачиваемыми, устанавливаемыми и переключаемыми, как приложения. чтобы можно
> было поставить свои цвета, свои шрифты, свои иконки ванильных приложений и
> прочее. и при этом чтобы все не поехало к чертям на экране. в план,
> обязательно!"*

And, in the same message, about a `□` visible in a simulator screenshot:

> *"а что там за прямоугольник на экране? проебанный в шрифте символ? в проде
> конечно же такого быть не должно, ты же понимаешь?"*

The second half is not a separate topic. A missing glyph is what a theme system
produces by default unless it is designed not to, and the box in that screenshot
is the *stock* font failing on `×` — with one font, chosen by us, in a build we
control. A user-supplied font is that failure mode with the safeties off.

**What already exists.** [T-009](../../TASKS.md) shipped the substrate on
2026-08-22 and it was built the right way round by accident of following final
§54: a screen names `color.accent.primary` and `space.md`, and the value behind
the name is resolved in exactly one place. Swapping the table under those names
*is* a theme. What does not exist is any of: a theme that is data rather than
code, a way to install one, a validity check, or a way to survive a bad one.

**What it obliges:**

1. **A theme is data, not code.** It carries colour values for the twelve roles
   in both themes, a font, an icon set, and nothing else. It never carries
   layout, and it never carries a pixel count — a theme that could set a padding
   could break every screen, and "чтобы всё не поехало" is precisely a
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

**Decided:** 2026-08-22, on [#41](https://github.com/hleserg/Attadipa/issues/41).

**As stated:**

> *"Согласен - принимаю."*

— to the recommendation in that issue: option 4, do not support Meshtastic,
MeshCore alone answers what [OD-7](#od-7--the-companion-is-any-node-not-only-ours)
actually asked for.

**What was asked, and what happened to it.** OD-7 said Meshtastic should be a
companion option *"вместо (или вместе, как получится)"* MeshCore. T-073 checked
the licence first, as that task required, and found the blocker:
`meshtastic/protobufs` is a separate repository with its own `LICENSE`, and that
file is **GPL-3.0** with no linking exception. Generating code from those
`.proto` files and linking it into the firmware would make Attadipa's firmware a
derivative work under GPL-3.0, and Attadipa is MIT. The reuse ledger's rule —
read it, learn from it, copy nothing — applies to protocol definitions exactly
as it applies to C++.

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

**As stated:**

> *"Не делаем. Ни Apple, ни какую-либо ещё."*

Not deferred, not blocked on the ecosystems. **Decided.**

The obstacles found by the research are real and are not the reason: Google
needs an approved proposal, an email allowlist and third-party certification,
and its only readable implementation is licensed for Nordic silicon; Samsung's
SDK ships for no Espressif part; Apple is reachable but costs an Apple ID
bootstrapped on Apple hardware, a self-hosted endpoint, and MFi for anything a
person would recognise as Find My. Those made the feature expensive. The owner
decided it is not wanted, which is a different sentence and outranks the first.

**[T-063](../../TASKS.md) survives, and it is the reason this costs nothing.**
The companion phone remembering where it last saw the watch over BLE answers
*"I have lost my watch"* with no account, no other company's identifier and no
network at all — and it is the only variant that works with the companion this
project already specifies.

### 2. A track is not a length of time. It is distance from familiar ground, on foot

The question asked was *how many hours*. The owner replaced it:

> *"трек пишется на случай, когда по нему, вероятно, придётся возвращаться
> пешком"* — вышел из метро, топаешь, заблудился, посмотрел трек, вернулся.

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

> *"хочу уметь сохранить трек целиком по запросу и посмотреть его потом на
> карте"* — on request, so no restriction on how the wearer is travelling; a car
> is fine here. Shaped as an application, allowed to run in the background so
> other applications keep working while it records.

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

**As stated:** *«Законность моя проблема а не прошивки»* — *legality is my
problem, not the firmware's.*

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
   firmware built from this repository — the owner's or anyone else's, since it
   is MIT — still refuses to transmit until an operator has explicitly chosen a
   `RegulatoryProfile`. That protects users this decision was never about, not
   only the owner.
3. **Choosing and validating the specific profile for his own device is the
   owner's task, done through the settings mechanism ADR-0006 already
   specifies** — the same schema, the same typed bounds, the same three
   ceilings — when that mechanism exists. Nothing about this decision brings
   that forward; T-025 (partitions/settings persistence) is still not started.

**What it invalidates.** The framing in
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) and [TASKS](../../TASKS.md) T-012 that A4
is one of a batch of questions still awaiting an owner reply. It is answered —
just not with a country.

**Status:** documentation only. No code exists yet that ADR-0006 governs, so
there is nothing to change in `core/`; this record is the fence for whoever
writes `SettingsService` next.


---

## OD-15 — A7 and A8: the canonical palette wins, and the icon may lose its black corners

**Decided:** 2026-08-22, on [issue #57](https://github.com/hleserg/Attadipa/issues/57).

**As stated:**

> *"@claude A7 побеждает то что ты сделал уже последним. Не надо переделывать и
> перепроверять. A8 буду очень благодарен если ты сам уберешь фон с картинок
> где надо. Уверен ты справишься"*

**In English:** for A7, what wins is whatever was already done last — no need
to redo it or re-verify it. For A8: yes, please remove the background from the
images where it needs it.

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

**As stated:**

> *"@claude A1 пока нет ни тех ни других часов. A2 sx1262 mia-m10q A3 есть
> компаньон ноды meshcore heltec t114 и heltec v4"*

**In English:** A1 — no watch of either kind yet. A2 — SX1262, MIA-M10Q. A3 —
there is a companion node, MeshCore Heltec T114 and Heltec V4.

The owner then posted a longer analysis of their own answer on the same issue;
this record follows that analysis rather than the three words alone, because
it is the more precise of the two and the owner asked for it to be recorded
"with the precision above."

**A1 — which boards, which revision.**

- **Waveshare ESP32-S3-Touch-AMOLED-2.06:** received — already recorded in
  `docs/research/WAVESHARE_BOARD_RECEIVED.md`. The schematic-revision question
  (silkscreen against `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf`) is
  **still open**; this answer does not close it. It belongs on the same
  checklist as the T-106 measurements, since it is read with the case open.
- **T-Watch S3 Plus:** **ordered, in transit — `ORDERED`, not `PRESENT`.**
  Nothing that needs the watch in hand moves yet.

**A2 — which radio, which GNSS.** **SX1262, 868 MHz; MIA-M10Q.** From the
order: *"LILYGO® T-WATCH-S3 Plus умные часы, SX1262 (868MHz)"*. Checked
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

| Node | Firmware | Role | Links |
|---|---|---|---|
| Heltec V4 (companion) | [`dt267/MeshCore-Low-Power-Firmware`](https://github.com/dt267/MeshCore-Low-Power-Firmware), latest | on Home Assistant now, **will be freed** for experiments | — |
| Heltec T114 #1, no screen, no GPS | official MeshCore, latest — **to be flashed** | takes over the Home Assistant duty | BLE, USB |
| Heltec T114 #2, screen + GPS | official MeshCore, latest — **to be flashed** | free for experiments | BLE, USB |

`doctor` as a hostname names no node in this answer — the Home Assistant role
is a node's job, and node #1 inherits it. Both T114s reach a host over BLE and
over USB, which matters more than it looks: a second side drivable from a
laptop is a test fixture, not just another radio in the room.

**Two things this answer surfaces that the issue did not ask, raised as their
own issues per the owner's instruction rather than resolved here:**

1. **Three firmware revisions, not one** — filed as
   [#90](https://github.com/hleserg/Attadipa/issues/90). The companion runs a
   third-party low-power fork; the T114s will run official latest; this
   repository pins MeshCore at `d929643` (2026-08-14), the commit every
   ADR-0003 claim was verified against. "Official latest" is not that commit.
   Before any mesh result is believed, the pairing under test has to be named —
   fork-to-official, official-to-official, or either against the pinned
   revision — because a failure between two of them is a compatibility finding
   (`Availability::Incompatible`) and a failure within one is a mesh finding,
   and conflating them produces a false bug report either way.
2. **Band has to match, and nobody has checked the T114s** — filed as
   [#89](https://github.com/hleserg/Attadipa/issues/89). The watch is
   868 MHz. If either T114 is a 915 or 433 MHz variant there is no mesh to
   test at all — not a weak link, no link. Band is set by "which
   band-specific matching network and antenna are fitted" (ADR-0003), which is
   not readable over SPI; the order record or a label on the module settles
   it. Whether the T114s carry an SX1262 at all is likewise unconfirmed here.

**A hardware constraint recorded here so it is not rediscovered as a bug** —
filed as [#91](https://github.com/hleserg/Attadipa/issues/91): neither T114
gets a GPS fix indoors, at all, either unit — owner-observed, 2026-08-22. This
is not a GNSS defect. Any position-dependent test run from indoors must either
inject a fix, mock the source, or be marked `NOT EXECUTED — HARDWARE REQUIRED`
with reason "requires outdoor conditions", not "requires hardware" — the board
is present; the sky is the missing part. Filing it as a power-rail bug (a real
failure mode on the T-Watch, per A1) would waste a day chasing the wrong
cause.

**What it obliges:**

- [`OPEN_QUESTIONS.md`](OPEN_QUESTIONS.md) A1 stays open for the schematic
  revision but is updated to remove "no boards at all" as a live possibility;
  A2 and A3 move to RESOLVED, pointing here.
- Three follow-up issues, filed separately rather than folded into this
  record: the T114 band check
  ([#89](https://github.com/hleserg/Attadipa/issues/89)), the
  three-firmware-revision compatibility matrix
  ([#90](https://github.com/hleserg/Attadipa/issues/90)), and the indoor-GNSS
  constraint documentation
  ([#91](https://github.com/hleserg/Attadipa/issues/91)).

**What it does not do:** it does not make the T-Watch S3 Plus a build
target — it is not in hand yet — and it does not resolve the Waveshare
schematic-revision question, which needs the case open regardless of this
answer.

---

## Still with the owner

Nothing here answers A5 or the compass question. Those remain in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).

OD-7 to OD-10 add three of their own, and they are the kind that cannot be
answered from a datasheet: whether Meshtastic's protocol definitions are licensed
separately from its firmware, which cellular module the node will carry, and
which tower database may lawfully be shipped in a product. The first is research
and is filed; the last two are the owner's.

