# Tasks

Sections are states, not folders. A task moves; it is not copied.

| Section | Meaning |
|---|---|
| `NOW` | actively being worked on — at most a couple of items |
| `NEXT` | chosen, starts as soon as NOW clears |
| `READY` | dependencies known, research done, could start today |
| `BLOCKED` | cannot proceed — must carry the blocker record |
| `WAITING` | waiting on someone or something external |
| `DONE` | finished and verified |

`READY` means **genuinely ready**. If the critical research is not done, it is
not READY — it is a research task. Priorities are `P0`–`P3`.

Every task carries: priority · dependencies · goal · acceptance criteria ·
research status · implementation status · tests · hardware required.

**Task state is updated in the same commit as the change it describes**
(final §73). A status file several commits behind is not a status file.

---

## NOW

### T-009 · Design tokens in code
- **Priority:** P0 — raised from P1; final §58 puts tokens in the first slice,
  before the Clock
- **Dependencies:** T-032 (**done**); T-008 (**done**)
- **Goal:** the code half of [DESIGN_SYSTEM](docs/ui/DESIGN_SYSTEM.md) — colour,
  spacing, radius, typography, motion, icon size, image size, elevation, sound
  cue, haptic pattern (final §54).
- **Acceptance:** no raw RGB, pixel count, duration, font size or radius
  anywhere in UI code; day and night variants; both geometries; spacing resolved
  per board rather than in raw pixels, because 8 px is not the same physical
  distance on a 1.54″ and a 2.06″ panel.
- **Research status:** done — palette, typography direction and mascot usage
  derived from the owner references ([docs/ui/reference](docs/ui/reference/README.md))
- **Implementation status:** document written and marked *proposed*; no code
  tokens; **no value has been shown on a panel**
- **Tests:** reference screenshots once the simulator runs
- **Hardware required:** for the final colour values, **yes** — final §55
  forbids preserving a concept-board hex that fails on the real display
- **Open inside this task:** `color.danger` has no value. There is no red in
  either owner palette, and inventing one is an identity decision for the owner.
- **Resumed:** 2026-08-21, after both owner amendments landed (T-041, T-042).
  Final §58 puts tokens first in the M1 slice and §15 of the GNSS amendment says
  the current milestone is not to be broken — so this is where the roadmap
  picks back up.

## NEXT

### T-034 · Image asset pipeline
- **Priority:** P0
- **Dependencies:** T-032 (**done**)
- **Goal:** reproducible conversion from cleaned source art to board-appropriate
  LVGL assets — `ui/assets/source/` → `tools/assets/` → `ui/assets/generated/`
  (final §45), using LVGL 9.5.0's `scripts/LVGLImage.py`.
- **Acceptance:** a script regenerates every asset deterministically; CI reports
  sizes and detects stale output; no hand-maintained C arrays; the 1448-pixel
  reference PNGs are never shipped as watch assets; small sizes are drawn
  deliberately rather than scaled down (final §86).
- **Research status:** partial — `LVGLImage.py` and `LV_COLOR_FORMAT_RGB565A8`
  confirmed present at the pinned version
- **Implementation status:** not started
- **Tests:** regeneration reproducibility; per-board asset budget
- **Hardware required:** for decode and render cost, yes

### T-037 · The first Clock
- **Priority:** P0
- **Dependencies:** T-008, T-009, T-033, T-034
- **Goal:** the first real screen. Time, date, battery, a good watchface, day and
  night, EN and RU, a Child variant, and one purposeful use of the owner's art
  (final §58, §88).
- **Acceptance:** it looks like Firefly and not like debug UI (final §96), at
  both geometries, in both locales, in both themes.
- **Research status:** not started — mature wearable watchface patterns
  (final §85); interaction lessons only, never someone else's visual identity
- **Implementation status:** not started
- **Tests:** reference screenshots across the visual matrix
- **Hardware required:** no

### T-038 · The first Settings
- **Priority:** P0
- **Dependencies:** T-037, T-017 (ADR-0006, **done**)
- **Goal:** language, theme, brightness, sound, haptics, power profile, Child
  Mode, diagnostics (final §88). Language comes first in the list, because a
  user who cannot read the settings screen cannot change the language from it.
- **Acceptance:** every control is backed by `SettingsService`, not by local
  state; values validated on write **and read**; a Russian layout that survives
  longer strings.
- **Research status:** decided in [ADR-0006](docs/adr/0006-settings-and-bounded-values.md)
- **Implementation status:** not started
- **Tests:** round-trip persistence; out-of-range rejection; layout at both
  geometries in both locales
- **Hardware required:** no

---

## READY

### T-051 · What the MIA-M10Q actually does, from u-blox's own documents
- **Priority:** P1 — it gates the GNSS driver, and nothing before it
- **Dependencies:** [ADR-0011](docs/adr/0011-gnss-integrity.md)
- **Goal:** fill in the receiver capability descriptor for the u-blox MIA-M10Q
  from primary sources only, in the order the owner gives: datasheet →
  integration manual → interface/protocol specification → vendor examples →
  official library source.
- **What to answer, at minimum:** `UBX-SEC-SIG` and `UBX-SEC-SIGLOG` — what
  they report and on which firmware; the jamming and spoofing indications and
  what each state means; `CFG-ITFM-*`; `UBX-NAV-PL` and whether a protection
  level is produced at all on this part; fix and time validity flags; per-signal
  C/N0; constellation control; Super-S; AssistNow Autonomous and what the
  official assistance mechanism is; backup and hot start, and what the MS412FE
  on the daughterboard actually backs up; configuration lockdown; message
  integrity; secure boot. **And: does it accept RTCM corrections?** — OD-5 says
  no, and the owner's technical claims are not automatically facts.
- **Acceptance:** every descriptor entry is `SUPPORTED`, `UNSUPPORTED` or
  `UNKNOWN`, each with the document and section it came from. `UNKNOWN` is a
  valid answer and an unsourced `SUPPORTED` is not.
- **Research status:** not started
- **Implementation status:** not started — no code comes out of this task
- **Tests:** none. It produces a research record in `docs/research/`.
- **Hardware required:** no for the documents. Confirming any of it on a fitted
  module is a separate line, and until then nothing here is `HARDWARE-VERIFIED`.

### T-052 · What the Quectel LS550G actually does, and what it only claims
- **Priority:** P1
- **Dependencies:** [ADR-0011](docs/adr/0011-gnss-integrity.md)
- **Goal:** the same descriptor for the second variant. The vendor advertises
  jamming detection, active anti-jamming, a multi-tone interference canceller,
  an internal LNA, multi-constellation operation, EPOC and power saving.
- **Acceptance:** each advertised feature is either traced to a primary source
  or recorded as a **claim**. **Anti-spoofing stays `UNKNOWN` until a primary
  source or a real device says otherwise** (OD-5 §2) — the marketing page is
  not a source. The two rails this variant needs (DC4 at 850 mV *and* BLDO1) are
  re-confirmed against the datasheet, because getting that wrong means GNSS
  silently never starts.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** none — a research record.
- **Hardware required:** no for the documents; yes to close anything the
  documents do not answer, which on this part is expected to be most of it.

### T-053 · The simulator can fail at GNSS twelve different ways
- **Priority:** P2 — after the descriptor research, before the trust engine
- **Dependencies:** [ADR-0011](docs/adr/0011-gnss-integrity.md), T-051, T-052
- **Goal:** injectable GNSS scenarios in the simulator, so the trust state and
  every screen that reads it can be developed and reviewed without a sky.
- **The twelve, from OD-5:** normal · weak signal · fix loss · poor accuracy ·
  stale position · a large jump while the accelerometer says stationary ·
  receiver-reported jamming · receiver-reported spoofing · an invalid protection
  level · two providers disagreeing · `Ready` with `NO_FIX` · `Ready` with a
  valid fix and `Untrusted`.
- **Acceptance:** each is selectable from the command line and reproducible;
  each produces a different visible outcome, and none of them renders as another
  one; a captured trace can be replayed into the trust engine offline
  (ADR-0011 §7).
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host — each scenario asserts the trust state and the reason codes
  it must produce.
- **Hardware required:** no. This is the task that exists *because* the
  interesting failures cannot be staged on hardware.

### T-043 · The node link is not a BLE link
- **Priority:** P0 — it is the shape of the transport, and the shape is cheapest
  before there is code in it
- **Dependencies:** [ADR-0005](docs/adr/0005-node-protocol.md)
- **Goal:** a transport abstraction for the watch↔node link that admits several
  interfaces at once — BLE, USB, UART, and later Wi-Fi/ESP-NOW — the way
  MeshCore's `MultiSerialInterface` (#3049, merged) does, without the three
  semantics that make upstream's version wrong for us.
- **Acceptance:** a reply goes back to the interface its request arrived on, not
  to all of them; each interface has its own bounded queue, so a stalled one
  cannot mark the stack busy; interfaces are serviced fairly rather than in
  registration order; a write failure names the interface that failed. No
  transport-specific method (`enableBluetooth()`) on the generic manager.
- **Research status:** done —
  [meshcore-1.17-review §1](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — two fake interfaces, one of which never drains; the other
  must keep working, and the drop must be counted.
- **Hardware required:** no for the abstraction; yes to prove it on a real link.

### T-044 · Framing that can be resynchronised
- **Priority:** P0 — it is a two-line requirement now and a protocol break later
- **Dependencies:** T-043
- **Goal:** write the framing requirements into
  [ADR-0005](docs/adr/0005-node-protocol.md). MeshCore's USB framing is a start
  byte plus a 16-bit length with **no checksum, no escaping and no resync
  marker**, and an over-long frame is silently truncated to `MAX_FRAME_SIZE` and
  delivered as if complete.
- **Acceptance:** ADR-0005 states that a torn frame must be *detected*; that an
  over-long frame is an error rather than a truncation; that resynchronisation
  cannot depend on a byte value that occurs freely in payloads; and that
  connection state is either observable or explicitly `Unknown`, never a
  hardcoded `true`.
- **Research status:** done —
  [meshcore-1.17-review §2](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a fuzz over truncated, extended and bit-flipped frames; no
  input may produce a frame the parser reports as valid.
- **Hardware required:** no.

### T-045 · `PowerState`: hibernate is not a sleep with the radio armed
- **Priority:** P0
- **Goal:** the six-state power model — `ACTIVE`, `IDLE`, `LIGHT_SLEEP`,
  `MESH_LISTEN_SLEEP`, `HIBERNATE`, `POWER_OFF` — as a type, with the wake
  sources of each written down.
- **Why:** upstream's `HeltecV4R8Board::powerOff()` is `enterDeepSleep(0)`, and
  that path leaves the FEM in RX and arms EXT1 on `P_LORA_DIO_1`. "Off" ends at
  the next packet (#3165; fix #3168 still open). Two behaviours that differ only
  in their wake sources shared one function name.
- **Acceptance:** a board cannot satisfy `HIBERNATE` while a radio wake source is
  armed — the API must not let it, rather than a review catching it. Each state
  names its wake sources and its expected current as `ESTIMATED` until measured.
- **Research status:** done —
  [meshcore-1.17-review §5](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a fake board that arms a radio wake in `HIBERNATE` must fail.
- **Hardware required:** yes to fill in the current figures. Until then every
  number is `ESTIMATED` and says so.

### T-046 · Crash-safe persistence, for everything and not only contacts
- **Priority:** P1
- **Goal:** one rule for every persistent structure —
  `write temp → flush → close → rename old to backup → atomic rename into place`
  — with load falling back to the backup and reporting which copy it used.
- **Why:** upstream still writes `/contacts3` in place and `break`s mid-stream on
  failure (open PR #1447 fixes contacts only), and `savePrefs()` on nRF52/STM32
  *deletes* `prefs.json` before writing the new one. Its JSON migration (#2982)
  is the part to copy: forward-convert and **leave the old file alone**.
- **Acceptance:** no critical structure is ever overwritten in place; a migration
  never destroys its source; the ~2× storage headroom the pattern needs is
  checked rather than assumed; dirty state is flushed on every shutdown and
  reboot path (#2627).
- **Research status:** done —
  [meshcore-1.17-review §6](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a filesystem fake that fails at every write offset in turn;
  a load must always produce either the old contents or the new, never a hybrid.
- **Hardware required:** no for the logic; yes for the flash behaviour.

### T-047 · Two clocks, and the rule about which one measures time
- **Priority:** P1
- **Goal:** adopt MeshCore's separation — `RTCClock` (wall, absolute) versus
  `MillisecondClock` (monotonic) — as Firefly's own, and write the rule down:
  **timers, timeouts, retries, connection expiry and the scheduler use the
  monotonic clock.** RTC and GNSS time only where absolute time is required.
- **Why:** a GNSS fix that steps the wall clock must not be able to make a
  timeout fire late — or never. Upstream already hit the long-uptime version of
  this (#2937). It also connects to T-042: a clock that jumps is itself evidence
  in the GNSS trust state.
- **Acceptance:** the two clocks are distinct types, not one type with two
  meanings; a duration cannot be computed from wall-clock readings without
  saying so explicitly.
- **Research status:** done —
  [meshcore-1.17-review §7](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — step the wall clock forwards and backwards under a running
  timeout and assert it fires at the same monotonic instant.
- **Hardware required:** no.

### T-048 · The crypto and RNG seam
- **Priority:** P1
- **Goal:** one crypto interface with three named backends — `software`,
  `ESP32-S3 hardware`, `nRF52 CC310` — and an entropy source that is the
  platform's hardware RNG.
- **Why:** upstream's ESP32 LoRa path uses `radio->randomByte() ^ ::random()`,
  and the Arduino PRNG's own header calls itself *"VERY SLOW"*. `esp_fill_random`
  appears **only** in the two ESP-NOW variants, and there is **no** mbedtls,
  `esp_aes` or `esp_sha` anywhere in the tree. So this is a gap to fill, not a
  port. nRF52 got CC310 in 1.17.0 (#2824); the ESP32 equivalent (#2280) is open.
- **Acceptance:** no `#ifdef` for a backend above the seam; entropy comes from
  `esp_fill_random()` on ESP32; and **no claim that hardware acceleration is
  faster** appears anywhere until it is `MEASURED` against the software path at
  Firefly's actual payload sizes.
- **Research status:** done —
  [meshcore-1.17-review §8](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — known-answer vectors that every backend must satisfy.
- **Hardware required:** yes for the ESP32-S3 measurement.

### T-049 · Front-end control is a board capability
- **Priority:** P1
- **Dependencies:** [ADR-0003](docs/adr/0003-radio-not-lora.md)
- **Goal:** express FEM/LNA/PA control as a property of the **board**, never as
  something inferred from "it has an SX1262".
- **Why:** the Heltec V4 auto-detects a GC1109 (V4.2) or a KCT8103L (V4.3) from
  the pull level of a shared GPIO at boot, and only one of the two can switch its
  LNA. Same product name, different silicon — the T-Watch radio problem from a
  second vendor. Upstream then shipped the LNA on by default and removed the
  companion's ability to turn it off (`e2aa7b98`, #3203); issues #3010 and #3232
  report the noise floor rising 13–22 dB and are open.
- **Acceptance:** the capability model can express *"has a front end, and it is
  switchable"* separately from *"has a front end"*; no default that changes RF
  behaviour is set without a measurement recorded beside it; the upstream
  implementation is **not** ported.
- **Research status:** done —
  [meshcore-1.17-review §4](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a board declaring a fixed front end must not compile against
  the switch-it API.
- **Hardware required:** yes to measure any of it. No Heltec board is in this
  project's hands.

### T-050 · The MeshCore adapter boundary, tested before there is an adapter
- **Priority:** P1
- **Dependencies:** [ADR-0007](docs/adr/0007-two-capability-layers.md) §5
- **Goal:** the boundary the owner asked for —
  `UI/Apps → Services → Mesh Service API → MeshCore Adapter → transports → HAL`
  — enforced the way the other two boundaries already are: a `PRIVATE` link plus
  a fixture that **must fail** to compile.
- **Acceptance:** no file outside the adapter includes a MeshCore header, and a
  test proves it by trying; bumping the MeshCore pin cannot require an edit
  above the adapter.
- **Research status:** done —
  [meshcore-1.17-review §12](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — the third boundary test, alongside
  `capability_boundary_negative` and `l10n_boundary_negative`.
- **Hardware required:** no.

### T-013 · The local mesh integration spike
- **Priority:** P0
- **Dependencies:** T-006 (**done**), [ADR-0003](docs/adr/0003-radio-not-lora.md),
  [ADR-0008](docs/adr/0008-mesh-service-providers.md)
- **Goal:** [ADR-0008](docs/adr/0008-mesh-service-providers.md) §5 deliberately
  does **not** choose how a watch runs a local mesh stack, because final §14
  forbids choosing without a measured spike — and this project already made that
  mistake once. Produce the numbers.
- **Options to measure:** direct component integration · an isolated
  compatibility layer · upstreamable ESP-IDF work · a narrow Arduino
  compatibility island · supporting only the combinations that are viable.
- **Acceptance:** for each option — flash cost, internal RAM cost, what an
  Arduino shim actually pulls in, whether MeshCore's file-static radio state
  (M9) can be tolerated under `HardwareCoordinator`, and how much routing
  behaviour would have to be re-derived. Plus the standing obligation: re-run
  `grep RADIO_CLASS variants/` against the pinned revision, because upstream
  adds radios and the matrix is wrong the moment it stops being checked.
- **Research status:** the compatibility matrix is done
  ([ADR-0003](docs/adr/0003-radio-not-lora.md)); the cost spike is not
- **Implementation status:** not started
- **Tests:** the spike's own builds
- **Hardware required:** for a working link, yes — and **two** radio devices
  (A3). For the cost numbers, no.
- **Constraint that is already fixed:** `Arduino.h` does not enter `core/`.

### T-016 · Benchmark the node protocol encoding, then accept or replace it
- **Priority:** P1
- **Dependencies:** [ADR-0005](docs/adr/0005-node-protocol.md) (**provisional**)
- **Goal:** final §18 endorses ADR-0005's *goals* and rejects its *evidence*: it
  compared a hypothetical small Firefly TLV against Meshtastic's entire
  `meshtastic_FromRadio` union and called the question settled. That is not a
  comparison.
- **Acceptance:** a Firefly TLV prototype, nanopb with a Firefly-specific
  streaming/callback schema, and at least one other compact option, measured on
  `xtensa-esp32s3-elf-gcc` for peak internal RAM, static RAM, flash, encoded
  bytes, malformed-input behaviour, schema-evolution cost, tooling, fragmentation
  interaction and test burden. If TLV still wins, accept ADR-0005 with the
  evidence.
- **Also required before ADR-0005 can be accepted:** the demultiplexing rule
  (final §19) — how a parser distinguishes log text, MeshCore companion frames
  and Firefly frames on one physical link. Separate GATT characteristics,
  separate UART channels, or an explicit outer mux frame. A diagram is not a
  design.
- **Research status:** nanopb measured in isolation; the Firefly-schema
  comparison is the missing half
- **Implementation status:** ADR written, provisional
- **Tests:** round-trip vectors; a hostile-frame corpus; a version-mismatch test
- **Hardware required:** no

### T-035 · ADR: rail ownership and reference counting
- **Priority:** P1
- **Dependencies:** none
- **Goal:** ALDO3 feeds the display **and** the touch controller on the T-Watch;
  BLDO2 gates the haptic driver's enable. A rail with two consumers needs an
  owner and a discipline, or the second consumer turns the first one off.
- **Acceptance:** who may request a rail; reference counting or another argued
  mechanism; what happens when a rail is requested during a sensitive operation;
  how ownership interacts with final §32's rule that a valid owned state can be
  "untouched".
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests over a simulated PMU
- **Hardware required:** for real sequencing timing, yes

### T-018 · Application framework: surviving the loss of a capability
- **Priority:** P0
- **Dependencies:** T-015 (**done**)
- **Goal:** the lifecycle verbs (final §59) do not include *"the GNSS you were
  navigating with has just left the building"*. With a detachable node that is an
  ordinary Tuesday.
- **Acceptance:** an application declares required and optional capabilities;
  the framework guarantees delivery of a capability change to open applications;
  the launcher rule settled for "installed when the capability existed, opened
  when it does not"; no application queries provider state directly.
- **Research status:** partial — InfiniTime's app model is the closest mature
  comparable
- **Implementation status:** the contract is sketched in
  [ADR-0004](docs/adr/0004-capability-sources.md) §5; the framework is not built
- **Tests:** attach → open → detach → reattach, in host tests, with no hardware
- **Hardware required:** no

### T-024 · ADR: the event bus and the concurrency model
- **Priority:** P1
- **Dependencies:** T-018
- **Goal:** final §60 and §61 — who owns which task, what may block, how events
  are delivered, back-pressure, queue bounds, UI-thread rules, interrupt handoff.
  Choosing late means choosing several.
- **Acceptance:** one mechanism; bounded queues; defined behaviour for a slow
  consumer; stack usage countable, because it is a memory-budget line.
- **Research status:** partial
- **Implementation status:** not started
- **Tests:** host tests
- **Hardware required:** no

### T-025 · ADR: partitions, NVS and OTA — for two devices
- **Priority:** P1
- **Dependencies:** T-004, T-017 (**done**)
- **Goal:** flash layout, settings storage and firmware update. The node makes
  OTA a compatibility question rather than a delivery one: two devices updated
  independently that must keep talking.
- **Acceptance:** a partition table per board (16 MB T-Watch, 32 MB Waveshare,
  both VERIFIED); rollback; settings survival across update; behaviour when the
  two firmware versions differ by more than the protocol allows.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests for the version-compatibility matrix
- **Hardware required:** for the flashing path yes; for the compatibility logic no

### T-026 · Implement the honest heading model
- **Priority:** P1
- **Dependencies:** [ADR-0009](docs/adr/0009-heading.md) (**done**), A5/A6
- **Goal:** the *decision* is made — three quantities, explicit reference frames,
  no `UserBody`, a node's compass is the node's. What remains is building it and
  designing the states this hardware is actually in.
- **Acceptance:** the `Heading` structure and its validity states; the Navigator
  state table from ADR-0009 §5 rendered, including *standing still* as a
  designed screen rather than a blank dial; **no configuration of inputs draws a
  wrist-relative arrow from a `NodeBody` or `CourseOverGround` source.**
- **Research status:** done
- **Implementation status:** not started
- **Tests:** host tests over recorded NMEA including stationary traces; a
  simulator scenario per state
- **Hardware required:** no for the logic; yes for a real fix, and **H10** (the
  speed gate) is a measurement on the fitted module, not a number to choose

### T-027 · Airtime and duty-cycle accounting
- **Priority:** P1
- **Dependencies:** T-006 (**done**), T-017 (**done**)
- **Goal:** the regulated settings are bounded by rules that constrain
  **airtime**, and the reference data model measures none of it
  ([OD-2](docs/research/OWNER_DECISIONS.md)). A device that cannot measure its
  own duty cycle cannot demonstrate compliance (final §38).
- **Acceptance:** airtime computed per transmission and accumulated per band;
  the limit part of the regulatory profile, not a constant; visible in
  diagnostics; the arithmetic tested against reference time-on-air formulas.
- **Research status:** MeshCore's own governor found — `Dispatcher::updateTxBudget()`
  — which Firefly must reconcile with rather than override on a local mesh path
- **Implementation status:** not started
- **Tests:** host tests against known LoRa time-on-air arithmetic
- **Hardware required:** no

### T-028 · Three-valued telemetry, and staleness on everything
- **Priority:** P1
- **Dependencies:** T-015 (**done**)
- **Goal:** the reference model shows `Node count: Unknown` — neither a number
  nor zero — and carries **no timestamp on anything**, so a four-hour-old
  coordinate and a two-second-old one are the same two numbers.
- **Acceptance:** a shared vocabulary for *known* · *known to be none* · *not
  known*; every datum crossing the link carrying its two ages and, where it is a
  measurement, its validity; a UI rule that never renders "not known" as "none".
- **Research status:** not started
- **Implementation status:** the model is in
  [ADR-0004](docs/adr/0004-capability-sources.md) §3; nothing is built
- **Tests:** host tests that a stale value cannot render as fresh
- **Hardware required:** no

### T-029 · Data feeds are not capabilities
- **Priority:** P1
- **Dependencies:** T-015 (**done**)
- **Goal:** final §16 lists what a node may provide and mixes two kinds of thing
  — capabilities (mesh connectivity, position) and feeds (weather, Home
  Assistant events, quest events, telemetry). A `Capability::Weather` would be a
  category error.
- **Acceptance:** the two modelled separately, with the boundary stated and the
  test that decides which side a new thing falls on.
- **Research status:** decided in
  [ADR-0004](docs/adr/0004-capability-sources.md) §4 and
  [ADR-0007](docs/adr/0007-two-capability-layers.md) §2
- **Implementation status:** not started
- **Tests:** host tests
- **Hardware required:** no

### T-030 · Adversarially break the capability model before building on it
- **Priority:** P0
- **Dependencies:** T-015 (**done**), T-007
- **Goal:** the model is about to become load-bearing for every application.
  Find where it gives a *wrong answer*, not where it is merely incomplete.
- **Scenarios that must each produce a defensible answer:** the link drops
  mid-navigation · the node's battery dies during an SOS · two watches share one
  node · a fix arrives ninety seconds stale · an application is installed when a
  capability exists and opened when it does not · the node is connected but its
  own GNSS has no fix · the node's firmware is too old to speak our version ·
  the user disables the node's radio from the watch and thereby cuts the link ·
  **a T-Watch whose radio is a CC1101** · **a node attached to a watch that
  already has a working local mesh**.
- **Acceptance:** every scenario resolved in the model or recorded as a defect.
  The sharpest remains: "node connected" and "node has data" are different
  states, and a model that collapses them reports a position the device does not
  have.
- **Research status:** n/a
- **Implementation status:** not started — the six adversarial agents allocated
  to this terminated on an account spend limit and returned nothing
- **Tests:** each scenario becomes a host test
- **Hardware required:** no

### T-007 · Reuse survey of existing firmware for these boards
- **Priority:** P1
- **Dependencies:** none
- **Goal:** several open-source firmwares already target these exact boards.
  Examine them before writing equivalents (final §64).
- **Candidates:** `MarcoRR/S3NTRY`, `joaquimorg/OLEDS3Watch` (ESP-Brookesia),
  `infinition/waveshare-watch-rs` (Rust), the LilyGoLib examples, Meshtastic's
  T-Watch support.
- **Acceptance:** a reuse-ledger record each, with a decision from the ledger
  vocabulary and a licence check.
- **Research status:** candidates identified; clones in `/root/upstream`
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

### T-020 · Node pairing, identity and trust
- **Priority:** P1
- **Dependencies:** T-016
- **Goal:** whether the watch has its own mesh identity that the node merely
  carries, or is a client of the node's identity, is question N4 — and the two
  produce different security models, message histories and privacy exposure.
- **Acceptance:** pairing flow specified; the trust boundary stated; node input
  treated as untrusted exactly as companion input is (ADR-0002 rule 4); what a
  stolen or hostile node can and cannot do, written plainly.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** the hostile-node cases must be host-testable
- **Hardware required:** no

### T-021 · The backlog the node creates
- **Priority:** P2
- **Dependencies:** T-015 (**done**), T-016
- **Goal:** the node adds work across power, coexistence, UI states, diagnostics,
  settings and the simulator. Record it as a gated backlog rather than letting it
  arrive as surprises.
- **Acceptance:** one backlog file, a gate per item, everything hardware-bound
  marked so.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

### T-022 · Simulator: node attach and detach as a first-class state
- **Priority:** P1
- **Dependencies:** T-008, T-015 (**done**)
- **Goal:** the node is a product state that cannot be tested on hardware that
  does not exist. Final §57 requires simulated provider attach and detach,
  simulated stale data and a provider that is `Ready` with no fix.
- **Acceptance:** every state in the ADR-0004 model reachable from the simulator
  without a rebuild, including the ones a real node would make hard to produce
  on demand.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** this *is* test infrastructure
- **Hardware required:** no

### T-004 · ESP-IDF version decision
- **Priority:** P1 — lowered from P0. It blocks embedded work; it does not block
  M1, which is the simulator.
- **Dependencies:** none
- **Goal:** pin ESP-IDF with recorded reasoning.
- **Acceptance:** a row in [DEPENDENCIES](docs/research/DEPENDENCIES.md) with
  source, version, licence, rationale and upgrade strategy.
- **Research status:** narrowed — Waveshare supports v5.5.5 and v6.0.2, its BSP
  needs ≥ 5.3; LilyGO's PlatformIO pin to IDF 4.4.7 probably does not bind
  Firefly (T7)
- **Implementation status:** `v5.5.5-496-gc197d718bcc` installed and **verified**
  by a real `idf.py set-target esp32s3 && idf.py build`. Verified is not decided.
- **Tests:** a trivial esp32s3 build — **passed**
- **Hardware required:** no

---

### T-039 · A formatting rule, and CI that enforces it
- **Priority:** P2
- **Dependencies:** none
- **Goal:** one `.clang-format`, applied to everything under `platform/`,
  `core/`, `apps/`, `sim/` and `tests/`, checked in CI.
- **Why now rather than later:** there is code to format as of 2026-08-21, and
  the cost of adopting a style grows with every file. `.github/workflows/ci.yml`
  already names this task in its list of what is deliberately absent, which is
  the honest way to carry a gap but not a substitute for closing it.
- **Acceptance:** `clang-format --dry-run --Werror` is green on a fresh
  checkout; the rules are chosen once and not argued again.
- **Research status:** not started. The existing code was written to a
  consistent house style by hand — 4 spaces, 100 columns, Allman braces on
  functions and attached elsewhere — so the job is mostly transcribing what is
  already there rather than choosing.
- **Implementation status:** not started
- **Tests:** the CI job is the test
- **Hardware required:** no

## BLOCKED

### T-010 · Board bring-up
```
BLOCKED:
Reason:         No physical board is available, and the exact variant is unknown.
Evidence:       OPEN_QUESTIONS A1, A2. The T-Watch ships with one of five radio
                chips and one of two GNSS modules; the GNSS power rail differs
                between board revisions (BLDO1 vs DC3).
Impact:         Blocks all bring-up, every power measurement, the whole
                interference matrix, and any claim that hardware works.
Possible options:
                1. Proceed on simulator and host tests only — no hardware claims.
                2. Obtain a board and record its exact variant.
                3. Write the bring-up checklist now so that day one with real
                   hardware is not spent improvising.
Recommended next action:
                Option 3 now, in parallel with option 1. Ask the project owner
                about hardware availability (A1–A4).
```

### T-011 · Interference measurement
```
BLOCKED:
Reason:         Requires physical hardware.
Evidence:       T-010.
Impact:         The coexistence layer cannot be justified or tuned. Settling
                intervals would be invented numbers, which final §26 forbids.
Possible options:
                1. Build the diagnostic tooling now, run it later.
                2. Defer entirely.
Recommended next action:
                Option 1 — the tooling is host-testable, and it is what turns a
                theory into a measurement. Note that neither board has a
                magnetometer, so the haptics-versus-compass case cannot be
                measured on current hardware in any configuration.
```

---

## WAITING

### T-012 · Answers from the project owner
- **Priority:** P0
- **Waiting on:** the project owner
- **Questions:** [OPEN_QUESTIONS](docs/research/OPEN_QUESTIONS.md) A1–A6 —
  hardware availability and revision · which radio and GNSS variant · a second
  mesh device · the regulatory region · whether an external magnetometer is
  intended · whether the node carries one.
- **Impact:** A1–A2 gate all hardware work, and A2 got sharper: of the five
  candidate radios, two cannot do LoRa at all and only one is supported by the
  pinned MeshCore ([ADR-0003](docs/adr/0003-radio-not-lora.md)), so the answer
  decides whether the watch has a local mesh path at all. **A4 is a legal
  constraint, not a preference.** It does not gate the build — the values are
  runtime settings either way — but it gates *transmitting*: while the region
  profile is `Unknown` the transmit path stays closed. A5 and A6 decide whether
  five magnetometer epics are dormant or dead, and A6 does **not** give the watch
  a compass even if the answer is yes ([ADR-0009](docs/adr/0009-heading.md) §3).
- **None of these blocks M1.**

### T-014 · Mandatory backlogs from the specification
- **Priority:** P2
- **State:** written, not started as work.
- **What:** the three mandated backlogs exist with a per-epic gate —
  [COMPANION_BACKLOG](docs/mobile/COMPANION_BACKLOG.md),
  [MAGNETOMETER_BACKLOG](docs/hardware/MAGNETOMETER_BACKLOG.md),
  [COEXISTENCE_BACKLOG](docs/hardware/COEXISTENCE_BACKLOG.md).
- **What the exercise surfaced:** two coexistence epics — haptic/magnetometer and
  audio/magnetometer interference — **cannot be run on either target board**,
  because neither has a magnetometer. They are marked NOT POSSIBLE rather than
  left looking pending. Five magnetometer epics are blocked on hardware that does
  not exist (A5).
- **Startable now without hardware:** C-02 bus ownership, C-03 rail arbitration
  and C-12 diagnostic trace. The trace in particular should be finished *while*
  waiting for hardware — every blocked coexistence test needs it to produce
  anything more than an anecdote.

---

## DONE

### T-042 · Owner amendment: GNSS integrity and receiver-native protection — **DONE**
- **Closed:** 2026-08-21
- **Scope, and it is deliberately small:** the owner's §15 forbids building the
  navigation stack now. This task did the eight things §15 *does* list, and
  stopped.
- **What was delivered:**
  - the amendment recorded as
    [OD-5](docs/research/OWNER_DECISIONS.md#od-5--gnss-integrity-and-the-receivers-own-protection-comes-first);
  - [ADR-0011](docs/adr/0011-gnss-integrity.md) — eight rules: the observation
    keeps both a normalized form and the receiver's native values; ten state
    axes that may not be collapsed; the receiver capability descriptor and where
    it may **not** be read; differential corrections as a provider capability;
    trust as a state with hysteresis, weighted evidence, reason codes and a
    bounded transition log; the receiver's verdict as the strongest input rather
    than the truth; a bounded, replayable trace before any field testing; and a
    list of what is not being built.
  - T-051, T-052 and T-053 filed.
- **The RTCM assumption:** it was never written down here. A grep of every ADR,
  architecture document, research file, header and source found `RTCM` in none
  of them, and the specification in force does not mention it either. So the
  correction is a *fence*, stated in ADR-0011 §4 before the path was worn, and
  the fact that the MIA-M10Q rejects corrections is recorded as the **owner's
  claim, to be confirmed** in T-051 — CLAUDE.md's rule about technical claims
  applies to the amendment as much as to the specification.
- **What did not change:** no code. No GNSS driver, no `LocationService`, no
  observation type exists yet, which is exactly why the amendment was cheap to
  absorb and why absorbing it now was the point.
- **Hardware required:** no. Every descriptor entry starts `UNKNOWN` and stays
  there until a primary source or a fitted module says otherwise.


### T-041 · Owner amendment: MeshCore 1.17 upstream review — **DONE**
- **Closed:** 2026-08-21
- **What was delivered:**
  [`docs/upstream/meshcore-1.17-review.md`](docs/upstream/meshcore-1.17-review.md)
  — v1.16.0 → v1.17.1 and `dev` read at source, all thirteen owner-named PRs and
  issues read through the GitHub API, and a status of
  `adopt / adapt / monitor / reject` against every item.
- **The finding that shaped the rest:** **ten of the thirteen owner-named pull
  requests are still open.** Only #3049 (multi-interface companion) and #3137
  (FEM gain persistence) are merged; #2734 is an *issue* already fixed by merged
  #3006. So most of what the amendment names is a *proposal* rather than
  shipped code, which is exactly the distinction the owner's §3 asks for and the
  reason nothing here is proposed for porting.
- **Two defects confirmed by reading the shipped tree, not by trusting a report:**
  - the **FEM/LNA regression** — `e2aa7b98` added `radio_fem_rxgain = 1` to the
    companion (v1.16.0 had no FEM pref at all), then #3203 `#if 0`'d the pref
    out. On a Heltec V4.3 running 1.17.1 the external LNA is on and cannot be
    turned off. Open issues #3010 and #3232 report the noise floor going
    −115 → −95 dB and −108 → −86 dB. Released, unfixed.
  - the **V4-R8 hibernate defect** — `powerOff()` is literally
    `enterDeepSleep(0)`, which leaves the FEM in RX and arms EXT1 on
    `P_LORA_DIO_1`. "Off" ends at the next received packet (#3165, fix #3168
    open).
  - and the **USB transport** — `isConnected()` returns `true` unconditionally
    *("no way of knowing, so assume yes")*, `isWriteBusy()` returns `false`
    unconditionally, and an over-long frame is truncated to `MAX_FRAME_SIZE` and
    delivered as if complete. The same codebase gets it right on BLE, with
    bounded queues and logged overflow — which is what makes it a lesson rather
    than a limitation.
- **What Firefly takes:** the two-clock separation, the JSON migration that does
  not destroy its source, the preamble-detect LBT scheme with both watchdog
  deadlines, BLE's queue discipline, and the battery rules (never sample during
  transmit; flush on every shutdown path). **What it does not take:** any FEM
  default, any unmerged code, and hardware CAD, which upstream still ships off.
- **Filed as a result:** T-043 (multi-interface node link), T-044 (framing),
  T-045 (`PowerState`), T-046 (crash-safe persistence), T-047 (two clocks),
  T-048 (crypto/RNG seam), T-049 (front-end as a board capability), T-050 (the
  adapter boundary).
- **Hardware required:** no — and **no Heltec board of any revision is in this
  project's hands**, so every upstream measurement quoted in the document is
  attributed to upstream and Firefly's own status for all of it stays
  `NOT EXECUTED — HARDWARE REQUIRED`.

### T-033 · Localization: `tr()`, catalogues, and the checks that guard them — **DONE**
- **Closed:** 2026-08-21
- **What was delivered:** [ADR-0010](docs/adr/0010-localization.md) in code.
  `l10n/strings.toml` is the single source of truth; a Python generator emits a
  `StringId` enum, a separate `PluralId` enum and parallel per-locale tables,
  and the generated files are **committed** so the C++ build needs no Python.
  A new `firefly_l10n` library sits beside core and is linked by apps and the
  simulator — **not** by core, and that is enforced rather than reviewed.
- **Acceptance, item by item:**
  - *a screen with no user-facing literal* — the simulator's diagnostic screen
    has none. Its rows are spelled with the `to_string()` of enums from core and
    platform, which are diagnostic identifiers rather than product text, and
    that distinction is written down in `l10n/strings.toml`.
  - *switches at runtime without a reboot* — `--locale en|ru`, and `L` toggles
    it live; the screen is rebuilt by the locale-changed handler.
  - *CI fails on a missing key, a duplicate key, or a glyph the font cannot
    draw* — three `ctest` entries, so a local run and CI enforce the same rule.
  - *the Russian plural vector* — 0, 1, 2, 5, 11, 21, 101, 111, 1001 assert
    **categories** rather than rendered strings, plus a sweep of every remainder
    class proving `other` is unreachable in Russian, which is what lets the
    catalogue format reject `ru.other`.
- **Beyond the acceptance list**, because running it found them:
  - a **fourth** generator check — placeholders must match across locales.
    `%u` in English and `%s` in Russian is undefined behaviour at the `snprintf`
    call that no compiler warning can reach.
  - a **selftest**: eight deliberate mistakes, each required to be rejected *for
    its own reason*. The `WILL_FAIL` lesson, applied before it could bite again.
  - the second **boundary test**, pointing the other way: a fixture that
    compiles against apps and must not compile against core. Proved by
    temporarily linking `firefly_l10n` into core and watching it fail.
- **The finding:** LVGL ships no font with Cyrillic — Montserrat's own header
  says `-r 0x20-0x7F,0xB0,0x2022`. The simulator therefore **cannot draw the
  Russian catalogue**: 26 codepoints in `ru`, and 7 in `en`, because a language
  is named in itself and `Русский` is in the English catalogue too. The
  simulator names the codepoints rather than rendering boxes. This is ADR-0010
  §1's argument arriving on schedule, and it closes only when the font pipeline
  output is linked in — D16 and T-034.
- **Tests:** 10 host, 12 with the simulator. All pass.
- **Hardware required:** no.

### T-032 · Pin LVGL, and the font toolchain that comes with it — **DONE**
- **Closed:** 2026-08-21
- **What was delivered:** LVGL v9.5.0 pinned at `85aa60d` with the commit
  verified after the clone, and the font toolchain pinned and *measured* rather
  than assumed. `lv_font_conv` **1.5.3**, MIT — read from the tarball's own
  `LICENSE`, not from the manifest, along with all ten bundled dependencies
  (one of which is Python-2.0, not MIT, and is recorded as such). Inter and
  Nunito Sans recorded under OFL 1.1, checked from the `OFL.txt` beside each
  font file.
- **The measurement:** 181 codepoints in 18 ranges, defined once in
  [`tools/font/charset.py`](tools/font/charset.py) so the font build and the
  localization check cannot disagree. Generated at seven sizes × three bit
  depths × compressed and raw, compiled with `xtensa-esp32s3-elf-gcc 14.2.0` at
  `-Os`, and read as `.rodata` — 84 measurements in
  [`docs/research/font-sizes.csv`](docs/research/font-sizes.csv), written up in
  [FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md).
- **What running it found, that reading about it would not have:**
  - **Nunito Sans has no arrows** (U+2190–U+2193). `lv_font_conv` refuses the
    range rather than substituting. Opened as D16, because it turns a font
    preference into a decision about where arrows come from.
  - **Both families ship as variable fonts only**, and the converter takes the
    *default* instance — which for Nunito Sans is **ExtraLight 200**. Converting
    the downloaded file silently produces a font nobody chose.
  - **Instancing Inter destroys its kerning**: 1 012 B of kern data before the
    `fontTools` round-trip at its own default weight, exactly zero after, and
    `optimize=False` does not help. So the tool now copies a font unchanged when
    the requested location is already the default, and says so.
  - **bpp 1 ignores compression entirely** — identical bytes with and without,
    at every size, for both fonts.
- **Legibility** checked at 14/16/20/28 px through `lv_font_conv`'s own `dump`
  rasterisation, in **both themes** —
  [`docs/ui/specimens/`](docs/ui/specimens/). Cyrillic including Ё is legible at
  14 px at bpp 4 in both families.
- **Deliberately not closed by this task:** render performance (D17, final §51
  asks for it and it needs timed frames), and which font (D16, a design decision
  that is the owner's).

---


### T-008 · Simulator skeleton with both geometries — 2026-08-21
- **Priority:** P0
- **Dependencies:** T-032 (LVGL pin — done)
- **Goal:** a desktop window that renders LVGL at 240 × 240 and 410 × 502, mouse
  as touch, keyboard as buttons. The simulator is a first-class target
  (final §57), not a convenience.
- **Acceptance:** both presets run; switching between them needs no rebuild;
  the build is part of CI. **Met.** `firefly_sim --board <id>` selects the
  geometry at runtime; `--radio <chip>` fits any of the five T-Watch radios
  without recompiling, which is the same requirement one layer down.
- **Implementation status:** **done.** `sim/` holds the composition root, the
  option parser, a diagnostic boot screen and a dependency-free PNG writer.
  LVGL configuration is `sim/lv_conf_simulator.h`, generated once from the
  v9.5.0 template with every edit recorded in its header.
- **Tests:** `ctest` runs the simulator headless at both geometries under
  `SDL_VIDEODRIVER=dummy`, and each run writes a screenshot that the test
  requires to exist. CI has a second job that installs SDL2, builds with
  `-DFIREFLY_BUILD_SIMULATOR=ON` and uploads the screenshots as artefacts.
  **OBSERVED** on the development host **and in CI** — run `32462413273`,
  2026-08-21, on a runner with no LVGL and a cold cache: clone 22.8 s, commit
  verified, build, 6/6 tests, both screenshots uploaded, whole job 2 min 2 s.
  That is the from-scratch path proven, not the incremental one.
- **Hardware required:** no. Nothing here touched a bus and nothing here is
  evidence about a board.
- **What it also settled**, because the first CMake file was the last cheap
  moment to settle it: the target graph. `firefly_platform` → `firefly_core` →
  `firefly_apps`, with platform linked PRIVATE into core, and two tests that
  compile one fixture against each of the two libraries to prove an application
  still cannot include a hardware header
  ([ADR-0007](docs/adr/0007-two-capability-layers.md) §5).

### T-039 · M0.5 — reconcile with the final master prompt — 2026-08-21
- All eight §75 P0 items re-checked, all eight found still present, all eight
  closed. Record: [RECONCILIATION](docs/research/RECONCILIATION_2026-08-21.md).
- Old master prompt and addendum marked superseded; the final prompt is in the
  repository at [`docs/master-prompt-final.md`](docs/master-prompt-final.md);
  the three owner design references are in
  [`docs/ui/reference/`](docs/ui/reference/README.md), hashed.
- Five ADRs written: 0003 radio · 0007 two capability layers · 0008 mesh
  providers · 0009 heading · 0010 localization. Three earlier ADRs accepted,
  one superseded, one made explicitly provisional.
- One further P0-grade correction the review did not list: *"ownership means
  initialises it"* is too strong (final §32), and the ownership tables were built
  on it.

### T-006 · Read MeshCore upstream — 2026-08-21
- M1–M9 answered from source at `d92964352441e53b93e8667b802e04f6e072b39e`,
  with file and line citations. Frame format, crypto, threading and radio
  ownership all read rather than inferred.
- **M9: yes, effectively** — `RadioLibWrappers.cpp:14` keeps radio state in a
  file-static flag set from an ISR. One radio per firmware image, structurally.
- **M6 corrected during the reconciliation:** MeshCore supports exactly one of
  the T-Watch's five candidate radios, and CC1101 is compiled out. An earlier
  version of that answer conflated RadioLib *driving* a chip with the chip being
  able to do LoRa.

### T-015 · ADR-0004: capability sources and their runtime lifecycle — 2026-08-21
- Seven availability states, one per user remedy; `Origin` as an orthogonal
  axis argued from a nine-row call-site table; a centrally-owned transition
  table; two ages on every datum that crosses a link; capabilities separated
  from data feeds. **Accepted** — final §8 endorses the model by name.

### T-017 · ADR-0006: settings, and values bounded by law — 2026-08-21
- Frequency is `uint32` Hz, never float — measured: `868.731f` round-trips to
  868 731 018 Hz, and one ULP at that magnitude is 64 Hz. Three scopes, three
  distinct power ceilings, a network contract applied as one atomic preset,
  stage→confirm→auto-revert for remote writes, layered factory reset, and an
  `Unknown` region profile that closes the transmit path. **Accepted** — final
  §34–§38 restate it independently.

### T-023 · Reuse-ledger records — 2026-08-21
- Six full records with commit hashes, licence checks and lessons drawn from
  upstream issues and reverts rather than from happy-path source. The ledger no
  longer says "Records: Empty" above actual records, which is the state final §67
  names.

### T-019 · The node as a documented profile — 2026-08-21
- [NODE_PROFILE](docs/node/NODE_PROFILE.md): five established facts, ten open
  questions, each with what it blocks. The node stays out of
  [HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md) until a part number exists.

### T-003 · Host build and CI — 2026-08-21
- Plain-CMake host build plus a test target, green in GitHub Actions.
  `cmake -S . -B build && cmake --build build && ctest` passes.

### T-005 / T-031 · Toolchain installed and verified — 2026-08-21
- ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build`
  completes on a stock example. `ninja`, `SDL2`, `ccache` present.
- The first install attempt failed and the reason is worth keeping: `python3` on
  this host resolves into an unrelated virtualenv, and ESP-IDF's `install.sh`
  refuses to build a virtualenv from inside one. It succeeds with that path
  element removed.

### T-001 · Core coverage for the full peripheral inventory — 2026-08-21
- Every part on both boards has an owning core service, including the parts the
  vendor BSPs ignore and the ones no application uses.
- Established *why*: an unowned part still costs power, still raises interrupts,
  still contends for the bus, and still floats its pin. "Maybe useful later" was
  never the argument.
- **Amended 2026-08-21:** "owns" no longer means "initialises". Final §32 names
  that definition as too strong, and this board proves it — GPIO 6 may be driven
  by the radio as a TCXO supply, so configuring it to satisfy a checklist is how
  the oscillator gets shorted.

### T-002 · ADR-0001: capability model — 2026-08-21
- Delivered the first capability model: presence, typed descriptors for variant
  and degree, a separate availability axis. Four alternatives recorded with
  reasons, all four still rejected.
- **Its Decision has since been superseded twice in one day** — by
  [ADR-0004](docs/adr/0004-capability-sources.md) for the Firefly node, then
  wholesale by [ADR-0007](docs/adr/0007-two-capability-layers.md). The task
  stays DONE: it produced a decision, a review found it wrong, and that is the
  process working rather than failing.

### T-000 · Repository, research gate, and board survey — 2026-08-21
- Repository created, MIT, public.
- Both boards surveyed from vendor documentation, vendor BSP source and
  published schematics — then the schematics were **read** rather than cited,
  which corrected two rows and produced two documented conflicts with the vendor
  documents.
