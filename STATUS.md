# Status

Last updated: 2026-08-21

Shape fixed by [final §93](docs/master-prompt-final.md). It is a status file,
not a history — what changed and why lives in git and in the ADRs.

## Current milestone

**M1 — simulator and the product design foundation.** M0.5, the reconciliation
forced by the owner's new master specification, is complete: all eight §75
items closed plus one the review did not list
([RECONCILIATION](docs/research/RECONCILIATION_2026-08-21.md)).

## Current implementation

**Firefly has code.** As of 2026-08-21 the repository builds three libraries, a
simulator and six tests, and has a font pipeline whose output has been compiled
for the target and measured.

| Library | What it is | Links |
|---|---|---|
| `firefly_platform` | the hardware inventory: `HardwareFeature`, `HardwareState`, `RadioInfo`, and the two board profiles transcribed from the schematics | — |
| `firefly_core` | `Capability`, the seven-state `Availability`, and the capability registry that owns the mapping | platform, **PRIVATE** |
| `firefly_apps` | `AppManifest` and the launcher gating rule | core only |
| `firefly_sim` | the desktop simulator, and the composition root that is allowed to see both layers | all three, plus LVGL and SDL2 |

The `PRIVATE` in the second row is the enforcement mechanism for
[ADR-0007](docs/adr/0007-two-capability-layers.md) §5, and two tests compile one
fixture against two different libraries to prove an application still cannot
include a hardware header.

The simulator renders at 240 × 240 and 410 × 502 from one binary, selected by
`--board`, and fits any of the five candidate T-Watch radios with `--radio`
without recompiling. Its first screen is a diagnostic that shows the two
capability layers side by side — deliberately not a product screen, and it says
so in its own source.

## Next ready

Both owner amendments of 2026-08-21 are **closed**. They are recorded as
[OD-4 and OD-5](docs/research/OWNER_DECISIONS.md), and between them they filed
eleven small tasks rather than one large one, which is what the owner asked for
in both cases.

- **T-041 — MeshCore 1.17 upstream review — done.**
  [`docs/upstream/meshcore-1.17-review.md`](docs/upstream/meshcore-1.17-review.md).
  Ten of the thirteen owner-named pull requests are **still open**, so most of
  what the amendment names is a proposal rather than shipped code. Two defects
  were confirmed by reading the shipped tree: the Heltec V4.3 external LNA is on
  by default with the companion's control removed (#3010, #3232 — noise floor up
  13–22 dB, unfixed), and `HeltecV4R8Board::powerOff()` is wake-on-LoRa deep
  sleep, so "off" ends at the next packet (#3165). Filed **T-043 … T-050**.
- **T-042 — GNSS integrity — done**, in its architecture-only scope.
  [ADR-0011](docs/adr/0011-gnss-integrity.md): the observation keeps the
  receiver's native values as well as a normalized form, ten state axes that may
  not collapse into one `quality`, a receiver capability descriptor that
  `LocationService` is the last layer allowed to read, differential corrections
  as a provider capability rather than a property of GNSS, and trust as a state
  with hysteresis and reason codes. The RTCM assumption turned out to be written
  **nowhere** in this repository, so the fix is a fence rather than a
  correction. Filed **T-051 … T-053**.
- **T-009 — design tokens in code**, resumed. The M1 slice continues in the
  order final §58 gives: tokens, then the image asset pipeline (T-034), the
  first Clock (T-037) and the first Settings (T-038).
- **T-043 … T-053** — the eleven the amendments produced, sitting in READY: the
  node link that is not a BLE link, resynchronisable framing, the `PowerState`
  taxonomy that cannot call a wake-on-LoRa sleep "hibernate", crash-safe
  persistence, two clocks, the crypto/RNG seam, the front end as a board
  capability, the adapter boundary test written before there is an adapter, and
  the three GNSS ones — MIA-M10Q, LS550G, and a simulator that can fail at GNSS
  twelve different ways.

## Lookahead research

One to two steps ahead, per final §68 — not twenty.

| | Subject | For |
|---|---|---|
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
and `ninja`, SDL2 2.30.0, Node v24.19.0 / npm 11.17.0 and `ccache` on the host.
Node matters more than it looks: `lv_font_conv` is an npm tool, and finding out
it could not be run *after* designing the font pipeline around it would have
been the expensive order.

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
| D16 | **Inter or Nunito Sans, and where do the arrows come from?** | the numbers exist ([FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md)); the choice does not. Nunito Sans has no U+2190–U+2193, so picking it also picks "arrows are icons". Blocks freezing the design tokens, not M1 |

None of these blocks M1. All of them block hardware work.

## Build and test state

| Target | State |
|---|---|
| Host / native | builds; ten tests pass locally and in CI — smoke, capability registry, and the two halves of the layer-boundary check. The negative half is checked against two deliberate breakages: a fixture that fails for the *wrong* reason is a failure, not a pass |
| Simulator | **builds and runs**, on the development host and **in CI from nothing** — run `32462413273`, cold cache, no LVGL on the machine: clone 22.8 s, commit verified against the pin, build, 6/6 tests, a screenshot per geometry uploaded, 2 min 2 s for the job. LVGL v9.5.0 + SDL2 2.30.0. Headless under `SDL_VIDEODRIVER=dummy`. Off by default (`-DFIREFLY_BUILD_SIMULATOR=ON`), so a machine with no SDL2 still gets a green host build |
| ESP32-S3 toolchain | **verified** — ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build` completes on a stock example |
| ESP32-S3 firmware | not started — there is no Firefly firmware to build yet |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED` |

Having ESP-IDF v5.5.5 on disk is not the same as having chosen it (T-004) — and
that decision no longer blocks M1, because M1 is the simulator.

## Hardware tests pending

All of them. No board has been powered on by this project and no measurement
has been taken. Nothing here may be described as hardware-tested.

## Open conflicts

Recorded rather than resolved by preference. Two need a powered board, one
needs the owner, and one needs a ruler.

| # | Conflict |
|---|---|
| D15 | **The T-Watch panel's physical diagonal.** LilyGoLib's spec tables say 1.3" for the S3 and the S3 Plus by name; the schematic's LCD sheet says `QT154C2408` / `LCD_1.54-TOUCH`, and that vendor's sibling part `QT154H2201` is published as 1.54", 240×240, ST7789V — so the part number decodes. 240 × 240 is not in doubt; 261 dpi against 220 is. The code holds 1.3" as the **conservative** reading, not the confident one ([HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md#display-diagonal--conflicting)) |
| A7 | The published brand art (`pics/`) and the §42 palette disagree by more than rounding — the wordmark samples at `#E16439` against Firefly Orange `#FF8A40`. An identity decision, so it waits for the owner ([pics/README.md](pics/README.md)) |
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

- **The specimen sheets showed a bar that is in no font.** `lv_font_conv`'s
  dump writer marks every pixel outside the advance width in pink, and reading
  those PNGs as luminance turns the mark into ink. The sheets now read the red
  channel — which is exactly `255 − coverage` for both of the writer's colours —
  and lay each line out with the real advance, side bearings and kerning from
  `font_info.json`. That produced the number D16 was missing: at the same
  `--size`, Nunito Sans wants 2–4 px more line height than Inter and draws a
  slightly smaller letter, so the two are not comparable at equal size.
- **T-033 — localization, and the checks that make it a mechanism.**
  `l10n/strings.toml` is the source of truth; a generator emits the `StringId`
  enum and the per-locale tables; `firefly_l10n` sits beside core and is linked
  by apps and the simulator but **not** by core, which a second boundary test
  enforces the way the first one enforces ADR-0007. The Russian plural vector
  asserts categories rather than strings, and a sweep proves `other` is
  unreachable — which is what lets the catalogue format reject `ru.other`.
  Running it produced the finding: **no built-in LVGL font has Cyrillic**, so
  the simulator cannot draw the Russian catalogue — 26 codepoints in `ru`, 7 in
  `en` — and it prints which ones instead of rendering boxes.
- **T-032 — the font toolchain, pinned and measured.** `lv_font_conv` 1.5.3
  under MIT read from the tarball rather than the manifest; Inter and Nunito
  Sans under OFL 1.1 read from the `OFL.txt` beside each file; a 181-codepoint
  Latin + Cyrillic subset generated at seven sizes and compiled with the
  ESP32-S3 toolchain, so its flash cost is a measurement and not an estimate
  ([FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md)). Running the
  pipeline found three things reading about it would not have: Nunito Sans has
  no arrows, both families' variable defaults are not the weight you would
  assume, and instancing Inter destroys its kerning.
- **T-008 — the simulator, and the target graph underneath it.** Both
  geometries from one binary, headless in CI, a screenshot per geometry as the
  artefact a design review needs. The first CMake file was the last cheap moment
  to make the platform/core/apps boundary real, so it was made real there.
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
