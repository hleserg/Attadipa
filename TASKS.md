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
  answer that can force an architecture change, and it also gates ADR-0002 on
  the radio abstraction across five chips. Answer it before building on it.

## NEXT

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

### T-013 · ADR-0002: radio abstraction across five chips
- **Priority:** P0
- **Dependencies:** T-006 (M6, M9)
- **Goal:** one radio interface that serves SX1262, SX1280, CC1101, LR1121 and
  SI4432 — or an argued decision to support fewer.
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
- **Questions:** OPEN_QUESTIONS A1–A4 (hardware availability, radio and GNSS
  variant, a second mesh device, regulatory region) and Q1 (what the Waveshare
  board should be, given it can do neither mesh nor navigation).
- **Impact:** A1–A2 gate all hardware work. Q1 shapes the product on one of the
  two targets. Q3 (region) is a legal constraint, not a preference.

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
  and degree, and a separate four-state availability axis.
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
