# Status

Last updated: 2026-08-21

Shape fixed by [final §93](docs/master-prompt-final.md). It is a status file,
not a history — what changed and why lives in git and in the ADRs.

## Current milestone

**M0.5 — review reconciliation.** M0 is complete: the repository exists, both
boards are surveyed from vendor schematics, ten upstream projects are cloned at
pinned commits, and the native build runs in CI.

M0.5 exists because the owner supplied a new master specification on 2026-08-21
containing a review of this work
([OWNER_DECISIONS OD-3](docs/research/OWNER_DECISIONS.md)). Its §75 lists eight
mandatory corrections that must land before large new core implementation. All
eight were re-checked and all eight were real
([RECONCILIATION](docs/research/RECONCILIATION_2026-08-21.md)).

## Current implementation

No firmware code exists yet. That is still deliberate, and M0.5 is the last
point at which it stays true — final §75 closes with *"do not spend another
week in research after this reconciliation; move into M1."*

**The reconciliation is complete.** All eight §75 items closed, plus one further
P0-grade correction the review did not list — final §32's rule that ownership
does not mean initialisation, which four ownership tables were built on.

Next is M1, starting with the simulator. Final §75 closes: *"do not spend
another week in research after this reconciliation."*

## Next ready

- **T-008** — simulator skeleton at both geometries. Unblocked: LVGL is pinned.
- **T-032** — the second half of the LVGL decision: pin `lv_font_conv`, check
  its licence, and **measure** a Latin + Cyrillic subset at the design system's
  sizes.
- **T-009 · T-033 · T-034** — design tokens in code, the localization
  mechanism, and the image asset pipeline. Together with T-008 these are the M1
  vertical slice (final §58), and they land before the first Clock.

## Lookahead research

One to two steps ahead, per final §68 — not twenty.

| | Subject | For |
|---|---|---|
| NEXT | `lv_font_conv` — licence, and the measured size of a Latin + Cyrillic subset at the sizes the design system needs | T-032, T-033 |
| NEXT | `scripts/LVGLImage.py` — RGB565A8, compression, and flash cost per asset | T-034 |
| AFTER NEXT | LVGL 9.5 on QSPI AMOLED: draw-buffer strategy and realistic frame rate at 410 × 502 | M2, and the PSRAM conflict D12 |

## Long-running operations

**None running.** Two reconnaissance workflows (`recon:power-rails`,
`recon:gnss-heading`) terminated on an account spend limit and returned nothing;
they are recorded here as ended rather than left looking in-flight. Subagents
and workflows are unavailable for the remainder of this session, so the
remaining work is being done directly.

Completed and still useful: ten upstream clones with full history in
`/root/upstream`, ESP-IDF `release/v5.5` with a verified `esp32s3` toolchain,
and `ninja`/`SDL2`/`ccache` on the host.

## Blocked

- **T-010 board bring-up** — no physical board; exact variant unknown.
- **T-011 interference measurement** — same, and neither board has a
  magnetometer, so the headline haptics-versus-compass concern is not measurable
  on current hardware in any configuration.

## Waiting on the owner

| | Question | Why it matters |
|---|---|---|
| A1 | Is either board physically available, and which revision? | everything hardware |
| A2 | If a T-Watch: which radio chip and which GNSS module? | decides whether the watch can join a MeshCore network at all — two of the five candidate radios cannot ([ADR-0003](docs/adr/0003-radio-not-lora.md)) |
| A3 | Is there a second radio device, so mesh can be tested? | mesh test plan |
| A4 | Which regulatory region governs the radio? | **legal.** Until answered, the region profile is `Unknown` and the transmit path stays closed ([ADR-0006](docs/adr/0006-settings-and-bounded-values.md)) |
| A5 | Is an external magnetometer intended at all? | decides whether five magnetometer epics are dormant or dead |
| A6 | Does the Firefly node carry a magnetometer? | decides what "compass" can mean — and even if the answer is yes, node orientation is **not** watch orientation ([ADR-0009](docs/adr/0009-heading.md) §3) |

None of these blocks M1. All of them block hardware work.

## Build and test state

| Target | State |
|---|---|
| Host / native | builds; smoke test passes locally and in CI |
| Simulator | not started — SDL2 and ninja installed; **LVGL pinned at v9.5.0**, whose SDL2 drivers are in-tree |
| ESP32-S3 toolchain | **verified** — ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build` completes on a stock example |
| ESP32-S3 firmware | not started — there is no Firefly firmware to build yet |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED` |

Having ESP-IDF v5.5.5 on disk is not the same as having chosen it (T-004) — and
that decision no longer blocks M1, because M1 is the simulator.

## Hardware tests pending

All of them. No board has been powered on by this project and no measurement
has been taken. Nothing here may be described as hardware-tested.

## Open conflicts

Recorded rather than resolved by preference. Both need a powered board.

| # | Conflict |
|---|---|
| H8 | The T-Watch vendor document calls ALDO1 unused; the schematic drives the `+3V3` rail from it. If the schematic is right, `+3V3` is switchable and carries five parts |
| D12 | PSRAM documented as quad; the `R8` part marking is understood to mean octal. Affects both boards, and blocks the LVGL buffer decision |

## Assumptions in force

- The LilyGO PlatformIO pin to IDF 4.4.7 does not constrain Firefly, which is
  ESP-IDF-native and does not use the Arduino layer. Flagged, not proven.
- Both boards' SoC is an ESP32-S3 — from both schematics (`ESP32-S3-R8`,
  `ESP32-S3R8`), but not from a chip readback.
- The radio capability facts in [ADR-0003](docs/adr/0003-radio-not-lora.md) come
  from RadioLib 7.7.1 and MeshCore `d929643` source, not from the TI and Silicon
  Labs datasheets, which refused automated retrieval. Recorded as **PARTIAL**,
  not VERIFIED.

## Recently completed

- **M0.5 reconciliation — all eight §75 items closed**, tracked row by row in
  [RECONCILIATION_2026-08-21](docs/research/RECONCILIATION_2026-08-21.md). Five
  new ADRs; three earlier ones accepted, one superseded, one made explicitly
  provisional.
- **LVGL pinned at v9.5.0** — the one dependency decision that was blocking M1.
- **T-006 MeshCore read** — frame format, crypto, threading and radio ownership
  answered from source at commit `d929643`, with a reuse-ledger record.
- **The reuse ledger has records**, six of them, each drawn from upstream issues
  and reverts rather than from happy-path source.
- **Every schematic read rather than cited** — which corrected two rows that
  were wrong and produced two documented conflicts with the vendor documents.
