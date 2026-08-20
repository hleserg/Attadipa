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

Every task carries: priority · dependencies · acceptance criteria · research
status · implementation status · tests · hardware required.

---

## NOW

### T-003 · Host build and CI that actually runs
- **Priority:** P0
- **Dependencies:** none
- **Goal:** plain-CMake host build plus a test target, running in GitHub Actions
  on every push.
- **Acceptance:** `cmake -S . -B build && cmake --build build && ctest` passes
  locally and in CI; CI is green for a real reason.
- **Research status:** n/a
- **Implementation status:** host build and smoke test pass locally; CI workflow
  added, first run not yet observed
- **Tests:** the build itself, plus the smoke test
- **Hardware required:** no

### T-006 · Read MeshCore upstream
- **Priority:** P0
- **Dependencies:** none
- **Goal:** answer OPEN_QUESTIONS M1–M9 from source, with commit hashes.
- **Acceptance:** a reuse-ledger record for MeshCore; M1, M3, M4, M6, M9
  answered with citations; a candidate revision named.
- **Research status:** not started — only repository identity and license
  established
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no
- **Note:** M9 — does MeshCore assume exclusive radio ownership? — is the one
  answer that can force an architecture change, and it also gates ADR-0003 on
  the radio abstraction across five chips. Answer it before building on it.


### T-015 · ADR-0004: capability sources and their runtime lifecycle
- **Priority:** P0
- **Dependencies:** none — but it amends ADR-0001, so nothing else should be
  built on the capability model until it lands
- **Goal:** a capability may now be provided by a detached Firefly node, and may
  appear and disappear while an application is running
  ([OWNER_DECISIONS OD-1](docs/research/OWNER_DECISIONS.md)). ADR-0001 assumed
  the BSP is the only producer and that `Absent` is permanent. Both are false.
- **Acceptance:** the availability enum settled with one state per sentence a
  user would actually be told; provider modelled as an axis or as enum values,
  with the choice argued from counted call sites rather than taste; the
  invariant that a remote-only state implies a remote provider stated; ADR-0001
  and ADR-0002 amended rather than left contradicting.
- **Research status:** recon running — `recon:capability-hal` is answering the
  axis-versus-enum question from Zephyr, Meshtastic variants, esp-bsp and BLE
  GATT service discovery
- **Implementation status:** ADR-0001 and ADR-0002 amended; ADR-0004 not written
- **Tests:** host tests over the state machine, including every transition a
  node attach/detach can cause
- **Hardware required:** no

### T-023 · Reuse-ledger records for the eight subsystems being designed
- **Priority:** P0
- **Dependencies:** none
- **Goal:** the addendum requires open-source reconnaissance **before** each
  significant subsystem is written, and a ledger record with a decision from the
  ten-verb vocabulary. The ledger currently holds a template and no records —
  the exact state it exists to prevent.
- **Scope:** MeshCore · node protocol · application framework · settings ·
  capability HAL · GNSS and heading · power and PMU rails · simulator.
- **Acceptance:** one record per subsystem in
  [REUSE_LEDGER](docs/research/REUSE_LEDGER.md), each carrying repository, tag,
  **commit hash**, licence, exact source files, a decision, a reason, the tests
  worth porting, and at least two lessons drawn from upstream *issues or pull
  requests* rather than from the source alone.
- **Research status:** eight investigators running in parallel against full
  local clones in `/root/upstream`
- **Implementation status:** not started
- **Tests:** n/a — but each record must name the upstream tests to port
- **Hardware required:** no

## NEXT

### T-016 · ADR-0005: the node application protocol
- **Priority:** P0
- **Dependencies:** T-006 (MeshCore internals), T-015 (the capability model the
  protocol serves)
- **Goal:** master plan §32 requires a **versioned high-level protocol over the
  transport**, explicitly not mixed with MeshCore internals, and an ADR
  analysing packet size, ESP32 memory, versioning, backward compatibility and
  debuggability. §32 also forbids choosing JSON, protobuf or CBOR merely because
  they are listed there.
- **Acceptance:** all five mandated axes analysed with real numbers where they
  exist and UNKNOWN where they do not; an encoding chosen with the trade-off
  stated; version negotiation before any payload; the behaviour when the node
  disappears mid-request specified; transport decided with alternatives recorded.
- **Research status:** recon running — `recon:node-protocol` is measuring nanopb
  cost in Meshtastic's own build and reading MeshCore's companion frame format
- **Implementation status:** not started
- **Tests:** encode/decode round-trip vectors, and a version-mismatch test that
  must be written before the first release, not after
- **Hardware required:** no
- **Note:** the watch and the node will be updated independently from day one.
  A protocol with no version field is a compatibility problem with a scheduled
  arrival date.

### T-017 · ADR-0006: settings, and values bounded by law
- **Priority:** P0
- **Dependencies:** none
- **Goal:** the owner requires that MeshCore RF parameters never be compiled
  into the core ([OWNER_DECISIONS OD-2](docs/research/OWNER_DECISIONS.md)).
  Frequency, bandwidth, spreading factor and TX power are runtime-settable,
  persisted, validated values. §34 lists a Settings application.
- **Acceptance:** typed settings with ranges, defaults, persistence across
  reboot **and across firmware update**, factory reset, and a bounded-value type
  that expresses "user-settable, bounded by a regulatory profile" without the
  core knowing which region it is in; migration behaviour on schema change
  specified rather than discovered.
- **Research status:** recon running — `recon:settings` is reading how
  Meshtastic constrains frequency, power and duty cycle per region, since it
  ships worldwide and has had to solve exactly this
- **Implementation status:** not started
- **Tests:** round-trip persistence, out-of-range rejection, migration from an
  older schema, factory reset
- **Hardware required:** no — NVS behaviour on real flash is a later measurement

### T-018 · Application framework: surviving the loss of a capability
- **Priority:** P0
- **Dependencies:** T-015
- **Goal:** §33 gives applications create/open/pause/resume/close/event. None of
  those is "the GNSS you were navigating with has just left the building". With
  a detachable node this is an ordinary Tuesday, and it touches every
  application that consumes a node capability.
- **Acceptance:** the contract written while there are zero applications; an
  application declares required and optional capabilities; the framework
  guarantees delivery of a capability change to open applications; the launcher
  rule settled for "installed when the capability existed, opened when it does
  not"; no application queries node state directly.
- **Research status:** recon running — `recon:app-framework` is reading
  InfiniTime's real app model, which is the closest mature comparable
- **Implementation status:** not started
- **Tests:** an application must be driveable through attach → open → detach →
  reattach in host tests, with no hardware
- **Hardware required:** no

### T-004 · ESP-IDF and LVGL version decision
- **Priority:** P0
- **Dependencies:** T-006
- **Goal:** pin ESP-IDF and LVGL versions with recorded reasoning.
- **Acceptance:** rows in `docs/research/DEPENDENCIES.md` with source, version,
  license, rationale, upgrade strategy; ADR if the choice is contentious.
- **Research status:** partial — Waveshare supports IDF v5.5.5 / v6.0.2, its BSP
  needs ≥5.3 and `lvgl >=8,<10`. LilyGO side not yet checked for ESP-IDF.
- **Implementation status:** not started
- **Tests:** a trivial ESP-IDF build for esp32s3
- **Hardware required:** no

### T-013 · ADR-0003: radio abstraction across five chips
- **Priority:** P0
- **Dependencies:** T-006 (M6, M9)
- **Goal:** one radio interface that serves SX1262, SX1280, CC1101, LR1121 and
  SI4432 — or an argued decision to support fewer — **and a radio that is not on
  this board at all**, because a Firefly node's LoRa reaches the same
  `MeshService` through the same interface. The interface takes frequency,
  bandwidth, spreading factor and TX power from settings rather than from
  constants ([OWNER_DECISIONS](docs/research/OWNER_DECISIONS.md) OD-2, §52).
- **Acceptance:** ADR with alternatives; states explicitly what happens on a
  board whose radio MeshCore does not support, and whether MeshCore permits a
  coordinator to schedule around it.
- **Research status:** blocked on T-006
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

## READY

### T-005 · Install and verify the embedded toolchain
- **Priority:** P0
- **Dependencies:** T-004 for the version
- **Goal:** ESP-IDF installed and building for esp32s3; ninja, SDL2 and
  clang-format present.
- **Acceptance:** `idf.py build` succeeds for an empty esp32s3 project.
- **Research status:** host probed — none of these are currently installed
- **Implementation status:** not started
- **Tests:** the build
- **Hardware required:** no

### T-007 · Reuse survey of existing firmware for these boards
- **Priority:** P1
- **Dependencies:** none
- **Goal:** several open-source firmwares already target these exact boards.
  Examine them before writing equivalents.
- **Candidates:** `MarcoRR/S3NTRY`, `joaquimorg/OLEDS3Watch` (ESP-Brookesia),
  `infinition/waveshare-watch-rs` (Rust), the LilyGoLib examples, Meshtastic's
  T-Watch support.
- **Acceptance:** a reuse-ledger record each, with a decision from the ledger
  vocabulary and a license check.
- **Research status:** candidates identified only
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

### T-008 · Simulator skeleton with both geometries
- **Priority:** P1
- **Dependencies:** T-004, T-005
- **Goal:** a desktop window that renders LVGL at 240×240 and 410×502, mouse as
  touch, keyboard as buttons.
- **Acceptance:** both presets run; switching between them needs no rebuild.
- **Research status:** SDL2 not installed; LVGL version not chosen
- **Implementation status:** not started
- **Tests:** it launches in CI headless, or CI skips it explicitly and says so
- **Hardware required:** no

### T-009 · Design system and tokens
- **Priority:** P1
- **Dependencies:** T-008
- **Goal:** `docs/ui/DESIGN_SYSTEM.md` plus the token definitions — colour,
  spacing, radius, typography, motion timing, icon sizes, haptic patterns.
- **Acceptance:** no raw RGB or magic size anywhere in UI code, enforced by
  review; day and night variants defined; both geometries considered.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** screenshot references once the simulator exists
- **Hardware required:** no

### T-019 · The node as a documented profile
- **Priority:** P1
- **Dependencies:** none
- **Goal:** record what is known about the node and — more importantly — what is
  not, so that no code is written against a device described in a chat message.
- **Acceptance:** [NODE_PROFILE](docs/node/NODE_PROFILE.md) lists every open
  question with what it blocks; the node stays out of
  [HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md) until a part number exists.
- **Research status:** n/a — this is a discipline document, not a search
- **Implementation status:** **written**; N1–N10 open
- **Tests:** n/a
- **Hardware required:** no

### T-020 · Node pairing, identity and trust
- **Priority:** P1
- **Dependencies:** T-016
- **Goal:** the watch↔node link carries position and mesh identity. Whether the
  watch has its own mesh identity that the node merely carries, or is a client
  of the node's identity, is question N4 — and the two produce different
  security models, different message histories and different privacy exposure.
- **Acceptance:** pairing flow specified; the trust boundary stated; node input
  treated as untrusted exactly as companion input is (ADR-0002 rule 4); what a
  stolen or hostile node can and cannot do written down plainly.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** the hostile-node cases must be host-testable
- **Hardware required:** no

### T-021 · The backlog the node creates
- **Priority:** P2
- **Dependencies:** T-015, T-016
- **Goal:** the node adds work across most of the system — power, coexistence,
  UI states, diagnostics, settings, simulator. Record it as a gated backlog in
  the style of §66/§67/§68 rather than letting it arrive as surprises.
- **Acceptance:** one backlog file with a gate per item, and every item that
  cannot be done without hardware marked so.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

### T-022 · Simulator: node attach and detach as a first-class state
- **Priority:** P1
- **Dependencies:** T-008, T-015
- **Goal:** the node is a product state that cannot be tested on hardware that
  does not exist. §35 already requires simulated sensors, GNSS, mesh and battery;
  this adds simulated attach, detach, staleness and a node whose own GNSS has no
  fix.
- **Acceptance:** every state in the ADR-0004 model reachable from the simulator
  without a rebuild, including the ones a real node would make hard to produce
  on demand.
- **Research status:** recon running — `recon:simulator`
- **Implementation status:** not started
- **Tests:** this *is* test infrastructure
- **Hardware required:** no

### T-024 · ADR: the event bus and the concurrency model
- **Priority:** P1
- **Dependencies:** T-018
- **Goal:** §33 requires that UI and business logic not create FreeRTOS tasks
  uncontrolled, and a clear concurrency model. Capability changes, node events,
  mesh messages and sensor data all need a delivery mechanism, and choosing one
  late means choosing several.
- **Acceptance:** one mechanism; who owns a task; what a service may block on;
  how an event reaches an application; back-pressure behaviour when a consumer
  is slower than a producer.
- **Research status:** partial — folded into the app-framework recon
- **Implementation status:** not started
- **Tests:** host tests; stack usage countable, because it is part of the memory
  budget
- **Hardware required:** no

### T-025 · ADR: partitions, NVS and OTA — now for two devices
- **Priority:** P1
- **Dependencies:** T-004, T-017
- **Goal:** flash layout, settings storage and firmware update. The node changes
  this: two independently updated devices that must keep talking to each other,
  which turns OTA into a compatibility question rather than a delivery one.
- **Acceptance:** partition table per board (16 MB T-Watch, 32 MB Waveshare —
  both VERIFIED); rollback behaviour; settings survival across update; what
  happens when watch and node firmware versions differ by more than the protocol
  allows.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests for the version-compatibility matrix
- **Hardware required:** for the flashing path, yes — for the compatibility
  logic, no

### T-026 · What a compass can honestly be on this hardware
- **Priority:** P1
- **Dependencies:** OPEN_QUESTIONS A5/Q2, node question N3
- **Goal:** neither board has a magnetometer (VERIFIED, from all six T-Watch
  schematic sheets and the Waveshare schematic). The owner has named "компас"
  among the node's applications. Either the node carries a magnetometer, or a
  compass means GNSS course-over-ground — which needs motion and shows nothing
  when the user stands still.
- **Acceptance:** the honest capability written down, with the speed threshold
  below which course-over-ground is not trustworthy and a source for it; the UI
  state for "standing still" designed rather than left as a blank dial.
- **Research status:** recon running — `recon:gnss-heading` is looking at what
  hiking devices and flight controllers actually display
- **Implementation status:** not started
- **Tests:** host tests over recorded NMEA, including stationary traces
- **Hardware required:** no for the logic; yes for a real fix

### T-027 · Airtime and duty-cycle accounting
- **Priority:** P1
- **Dependencies:** T-006, T-017
- **Goal:** the two regulated settings — frequency and TX power — are bounded by
  rules that constrain **airtime**, and nothing in the reference data model
  measures it ([OWNER_DECISIONS OD-2](docs/research/OWNER_DECISIONS.md)). A
  device that cannot measure its own duty cycle cannot demonstrate compliance.
- **Acceptance:** airtime computed per transmission and accumulated per band;
  the limit expressed as part of the regulatory profile, not as a constant;
  visible in diagnostics.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests against known LoRa time-on-air arithmetic
- **Hardware required:** no

### T-028 · Three-valued telemetry, and staleness on everything
- **Priority:** P1
- **Dependencies:** T-015
- **Goal:** the reference model shows `Node count: Unknown` — a value that is
  neither a number nor zero — and carries **no timestamp on anything**, so a
  four-hour-old coordinate and a two-second-old one are the same two numbers.
- **Acceptance:** a shared vocabulary for *known* · *known to be none* · *not
  known*; every datum crossing the link carrying its age and, where it is a
  measurement, its validity; a UI rule that never renders "not known" as "none".
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests that a stale value cannot be rendered as fresh
- **Hardware required:** no

### T-029 · Data feeds are not capabilities
- **Priority:** P1
- **Dependencies:** T-015
- **Goal:** §32 lists what the node provides and mixes two different kinds of
  thing — capabilities (mesh connectivity, additional GNSS) and data feeds
  (weather, Home Assistant events, quest events, object coordinates, telemetry).
  A capability gets an availability state and gates UI. A feed gets staleness and
  a source label. `has(Capability::Weather)` would be a category error.
- **Acceptance:** the two modelled separately, with the boundary stated and the
  test that decides which side a new thing falls on.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests
- **Hardware required:** no

### T-030 · Adversarially break the capability model before building on it
- **Priority:** P0
- **Dependencies:** T-015
- **Goal:** the model is about to become load-bearing for every application. Find
  where it gives a *wrong answer*, not where it is merely incomplete.
- **Scenarios that must each produce a defensible answer:** the link drops
  mid-navigation · the node's battery dies during an SOS · two watches share one
  node · a fix arrives ninety seconds stale · an application is installed when a
  capability exists and opened when it does not · the node is connected but its
  own GNSS has no fix · the node's firmware is too old to speak our version ·
  the user disables the node's LoRa from the watch and thereby cuts the link.
- **Acceptance:** every scenario resolved in the model or recorded as a defect
  in it; the last one is the sharp one — "node connected" and "node has data"
  are different states and a model that collapses them will report a position
  the device does not have.
- **Research status:** n/a
- **Implementation status:** not started
- **Tests:** each scenario becomes a host test
- **Hardware required:** no

### T-031 · Verify the toolchain that is installing now
- **Priority:** P0
- **Dependencies:** the running install
- **Goal:** ESP-IDF `release/v5.5` and the esp32s3 toolchain are being installed
  ahead of need; SDL2 and ninja are already in. Confirm they work rather than
  assuming.
- **Acceptance:** `idf.py --version` reports the pinned version and an empty
  esp32s3 project builds; an SDL2 window opens or the reason it cannot is
  recorded.
- **Research status:** n/a
- **Implementation status:** running — see STATUS "Long-running operations"
- **Tests:** the builds themselves
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
                intervals would be invented numbers.
Possible options:
                1. Build the diagnostic tooling now, run it later.
                2. Defer entirely.
Recommended next action:
                Option 1 — the tooling is host-testable, and it is what turns
                a theory into a measurement. Note that neither board has a
                magnetometer, so the haptics-vs-compass case cannot be measured
                on current hardware at all.
```

## WAITING

### T-012 · Answers from the project owner
- **Priority:** P0
- **Waiting on:** the project owner
- **Questions:** OPEN_QUESTIONS A1–A5 (hardware availability and revision, radio
  and GNSS variant, a second mesh device, regulatory region, whether an external
  magnetometer is intended at all) — plus one the node raised: **does the node
  carry a magnetometer?** That is A5 asked about the node, and it decides whether
  a compass works standing still or only while walking. Q1 (what the Waveshare
  board should be) is **answered** — see
  [OWNER_DECISIONS](docs/research/OWNER_DECISIONS.md) OD-1.
- **Impact:** A1–A2 gate all hardware work. **A4 is a legal constraint, not a
  preference** — the lawful frequencies, power and duty cycle follow from the
  region. It does not gate the build: §52 and OD-2 make these runtime settings,
  so the core carries a bounded value either way, and A4 decides the bound and
  the default. It does gate *transmitting*: until it is answered the honest
  default is `Unset`, and `Unset` keeps the transmit path closed. A5 decides
  whether five §67 epics are dormant or dead.

### T-014 · Mandatory backlogs from the specification
- **Priority:** P2
- **State:** written, not started as work.
- **What:** master plan §66 (mobile, 13 epics), §67 (magnetometer, 13 epics) and
  §68 (coexistence, 13 epics) are recorded as backlogs with a per-epic gate:
  [COMPANION_BACKLOG](docs/mobile/COMPANION_BACKLOG.md),
  [MAGNETOMETER_BACKLOG](docs/hardware/MAGNETOMETER_BACKLOG.md),
  [COEXISTENCE_BACKLOG](docs/hardware/COEXISTENCE_BACKLOG.md).
- **What the exercise surfaced:** two §68 epics — haptic/magnetometer and
  audio/magnetometer interference — **cannot be run on either target board**,
  because neither has a magnetometer. They are marked NOT POSSIBLE rather than
  left looking pending. Five §67 epics are blocked on hardware that does not
  exist yet (A5).
- **Startable now without hardware:** C-02 bus ownership, C-03 rail arbitration
  and C-12 diagnostic trace. The trace in particular should be finished *while*
  waiting for hardware — every blocked coexistence test needs it to produce
  anything more than an anecdote.

## DONE

### T-001 · Core coverage design for the full peripheral inventory — 2026-08-21
- `docs/architecture/ARCHITECTURE.md` maps every part on both boards to an
  owning core service, including the parts the vendor BSPs ignore and the ones
  no application uses.
- Establishes *why* full coverage matters: an unowned part still costs power,
  still raises interrupts, still contends for the bus, and still floats its pin.
  "Maybe useful later" was never the argument.
- Records the rail ownership map, the shared-rail problem (ALDO3 feeds display
  **and** touch), the two incompatible touch sleep strategies, and the fact
  that the specification's motivating coexistence example cannot occur on
  either board.

### T-002 · ADR-0001: capability model — 2026-08-21
- Accepted shape: cheap `has()` for UI gating, typed descriptors for variant
  and degree, and a separate availability axis — four states as written, **seven
  under [ADR-0004](docs/adr/0004-capability-sources.md)**, which amends this the
  same day for the Firefly node.
- Four alternatives recorded with reasons for rejection.

### T-000 · Repository, research gate, and board survey — 2026-08-21
- Repository created, MIT, public.
- Both target boards surveyed from vendor documentation, vendor BSP source and
  published schematics: complete peripheral inventory, pin maps, I2C addresses,
  PMU rail map, vendor power figures.
- Research-gate documents established and populated.
- Key findings: the Waveshare board has no LoRa and no GNSS; neither board has
  a magnetometer; the T-Watch radio and GNSS are purchase-time variants; the
  T-Watch touch panel has no reset line; the Waveshare vendor BSP does not
  drive the IMU, PMU or RTC that are on the board.
