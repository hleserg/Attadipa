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

### T-033 · Localization: `tr()`, catalogues, and the CI that guards them
- **Priority:** P0
- **Dependencies:** T-032 (**done**)
- **Goal:** implement [ADR-0010](docs/adr/0010-localization.md) — the `StringId`
  enum, the generator, both catalogues, runtime switching, three CI checks.
- **Acceptance:** a screen can be written with no user-facing literal; language
  switches at runtime without a reboot; CI fails on a missing key, a duplicate
  key, or a catalogue glyph absent from the font subset; the Russian plural rule
  passes a vector covering 0, 1, 2, 5, 11, 21, 101, 111, 1001.
- **Research status:** decided; the API shape is open
- **Implementation status:** not started
- **Tests:** host tests for plural categories and fallback; the three CI checks
- **Hardware required:** no
- **Note:** this is what final §50 means by *"localization is architecture, not
  later polish"*. It precedes the first screen rather than following it.

## NEXT

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
  **OBSERVED** on the development host; the CI job itself has not run yet.
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
