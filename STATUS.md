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

**Attadipa has code.** As of 2026-08-22 the repository builds six libraries, a
simulator and twenty tests, and has a font pipeline whose output has been
compiled for the target and measured.

| Library | What it is | Links |
|---|---|---|
| `attadipa_platform` | the hardware inventory: `HardwareFeature`, `HardwareState`, `RadioInfo`, and the two board profiles transcribed from the schematics | — |
| `attadipa_core` | `Capability`, the seven-state `Availability`, and the capability registry that owns the mapping | platform, **PRIVATE** |
| `attadipa_apps` | `AppManifest` and the launcher gating rule | core only |
| `attadipa_link` | transport framing with a checksum and resynchronisation, a bounded frame queue, and the session state machine above them | core |
| `attadipa_ui` | the design tokens: `Dp` against a 160 dpi reference, twelve colour roles across two themes with WCAG contrast arithmetic, and the spacing, radius, motion, size and feedback scales | `attadipa_headers` only — **deliberately not platform** |
| `attadipa_replay` | the deterministic navigation replay rig, in `tests/` | core |
| `attadipa_sim` | the desktop simulator, and the composition root that is allowed to see both layers | all three, plus LVGL and SDL2 |

The `PRIVATE` in the second row is the enforcement mechanism for
[ADR-0007](docs/adr/0007-two-capability-layers.md) §5, and two tests compile one
fixture against two different libraries to prove an application still cannot
include a hardware header.

The simulator renders at 240 × 240 and 410 × 502 from one binary, selected by
`--board`, and fits any of the five candidate T-Watch radios with `--radio`
without recompiling. Its first screen is a diagnostic that shows the two
capability layers side by side — deliberately not a product screen, and it says
so in its own source. Since T-009 it draws entirely through the tokens: no hex
colour and no pixel padding remain in it, `--theme day|night` and the `T` key
switch palettes without a rebuild, and a checker in CI refuses to let a raw
value back in. Since T-083 it also draws every character: it links four generated
Montserrat subsets covering all 181 codepoints of `charset.py`, so `×` and
Cyrillic render rather than showing boxes, and an undrawable codepoint now
**fails the run** instead of printing a warning.

**Two contrast findings came out of that migration and neither is a proposal to
repaint anything.** On the day page every accent is under the 3:1 that a glyph
or an outline needs — Attadipa Orange 2.19:1, Glow Amber 1.44:1 — so a day accent
is emphasis and the meaning lives in the icon and the word. And
`color.text.muted` passes on the page and on a surface and then fails on a
*raised* card at 4.44:1, six hundredths under the threshold, which is the kind of
thing a review by eye does not find. Both are tabulated in
[DESIGN_SYSTEM §3.2](docs/ui/DESIGN_SYSTEM.md) and pinned by tests. The colours
are the owner's; open question **A7** already records that the published brand
art disagrees with the palette text.

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
| AFTER NEXT | LVGL 9.5 on **octal** PSRAM AMOLED: draw-buffer strategy and realistic frame rate at 410 × 502 | M2. D12a is closed, so this is unblocked on the memory question — but T-093 first: the vendor BSP is not the existence proof it is taken for |

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

## The Waveshare board is on its way, and the advice about it was checked

The owner was given a bring-up plan for the Waveshare board by another model and
passed it on. Most of it agrees with what was already established here; its
headline claim does not. Verified against datasheets, the schematic and vendor
source, then adversarially re-checked, and written up in
[docs/research/WAVESHARE_ARRIVAL.md](docs/research/WAVESHARE_ARRIVAL.md).
**No board has been touched. Every hardware result is `NOT EXECUTED — HARDWARE
REQUIRED`.**

- **D12 is closed for this board and split for the other.** `ESP32-S3R8` is
  **octal** PSRAM — ESP32-S3 Series Datasheet v2.2 Table 1-1, which contains no
  8 MB quad in-package variant at all, corroborated by five vendor examples
  shipping `CONFIG_SPIRAM_MODE_OCT=y` and by GPIO33–37 sitting unrouted. The
  question had been resting on recollection and now rests on a table. It does
  **not** transfer to the T-Watch (D12b), where a LilyGO document saying QSPI is
  still unexamined.
- **The claim that the board's PSRAM is absent was false** and was already
  contradicted by our own schematic reading.
- **The main I2C bus has six devices, not four.** The ES8311 codec and the ES7210
  microphone ADC are I2C control slaves on the same wire; both were recorded here
  as "I2S", which is their data path. All six addresses are now in the matrix,
  each cited — three datasheet-fixed, two schematic-strapped, one
  driver-source-only, and one (`0x6A` vs `0x6B` on the IMU) in conflict between
  datasheet revisions, where the revision Waveshare's own wiki links is the one
  that disagrees.
- **The vendor BSP is not the existence proof it is taken for.** Its PSRAM
  draw-buffer configuration is dead code; what ships is one ~80 KiB partial
  buffer in internal SRAM. T-093.
- **Its `esp_lcd_sh8601` fork drops an error check**, so a failed frame transfer
  reports success. T-092.
- **Two questions went to the owner**: [A9](docs/research/OPEN_QUESTIONS.md) —
  does the day theme keep its near-white page on an emissive panel, where the
  rendered face draws an estimated 4.2× to 13.9× the night theme; and A10 — what
  Attadipa does about static content, where the controller has no pixel-shift
  command, its Auto Current Limit defaults to off, and no driver in the ecosystem
  writes it.
- Corrected while here: the Waveshare peripheral table regained the two columns
  the T-Watch table has, the reuse ledger pointed at the wrong upstream, and D3
  asked for the pinout of an expansion connector that does not exist — `J3` is
  the display FPC. The rest is T-090.

## Owner decisions of 2026-08-22, recorded and not yet started

Five messages in one session, all filed as
[OD-7 to OD-11](docs/research/OWNER_DECISIONS.md) with the research questions in
[COMPANION_AND_POSITION_SOURCES](docs/research/COMPANION_AND_POSITION_SOURCES.md)
and twelve tasks, T-072 to T-083. **Nothing is implemented.** Recorded here
because a fact that lives only in a chat log does not exist.

- **The companion is any node, not only ours** — vanilla MeshCore over BLE or
  LAN, several providers at once with a local radio, and telemetry as a
  request/response feed. It fits
  [ADR-0008](docs/adr/0008-mesh-service-providers.md)'s shape; what it needs is
  the protocol facts, which are `UNKNOWN` (T-072, T-074).
  **Meshtastic is not one of the providers.** OD-7 asked for it alongside or
  instead of MeshCore; [OD-12](docs/research/OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence)
  reversed that on 2026-08-22 and T-073 is `REJECTED`, not awaiting protocol
  facts. Its protocol definitions are GPL-3.0 with no linking exception, which
  closed the cheap path, and the owner declined to fund an honest clean-room —
  the licence is the evidence and the product decision is the decision.
- **Every source of position, with the watch as the instrument** — the watch's
  receiver, a node's, a phone's, a coordinate inside somebody else's message,
  telemetry, dead reckoning, cell towers. Selection and fusion are different
  features and only the first has a shape (T-075, T-076).
- **AGPS is a payload, not a transport** — internet, BLE, LoRa, whatever is
  available. Blocked on what the receivers accept (T-077).
- **The node may carry a cellular modem** — tower positioning from a downloaded
  database, plus a route off the mesh. Blocked on a part that does not exist, and
  on four separate answers about the database (T-078, T-079).
- **A standing person does not need a new fix** — duty-cycle GNSS against motion,
  without turning the next fix into a cold start. The largest continuous draw on
  a watch that has GNSS, and the whole feature is a claim about a specific
  module's low-power behaviour (T-080).
- **Themes are installable, like applications** — user colours, fonts and icons,
  without the layout breaking. T-009 turns out to be the substrate: a screen
  already names a role rather than a value, so swapping the table is the feature.
  What is missing is themes as data, an installation gate built from the contrast
  and glyph checks that already exist, and a way back from a theme that makes the
  screen unreadable (T-081, T-082).
- **And one defect, not a feature.** The simulator draws with LVGL's stock
  Latin-only Montserrat, so `×` renders as `□` and so do the Cyrillic letters in
  the English catalogue's own language names. The check already reports seven
  undrawable codepoints on every run; what is missing is that it is a warning
  rather than a failure, and that nothing consumes the font pipeline T-032 built.
  Filed as **T-083, P1**.

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
| A6 | Does the Attadipa node carry a magnetometer? | decides what "compass" can mean — and even if the answer is yes, node orientation is **not** watch orientation ([ADR-0009](docs/adr/0009-heading.md) §3) |
| A7 | [#33](https://github.com/hleserg/Attadipa/issues/33) — **Three features asked for in conversation and absent from the specification — how big is each?** (a) is "the watch can be found by a crowd-sourced network" a requirement, and which one; (b) how long is a track; (c) how far must a reckoned path stay useful. | they compete for one antenna, one coexistence arbiter and one 940 mAh cell, so they are one question in three parts. Every sizing decision in [TAGS_TRACKS_RECKONING](docs/research/TAGS_TRACKS_RECKONING.md) is parameterised by these. T-064, T-065 and T-071 are blocked or unsized until answered |
| D16 | **Inter or Nunito Sans, and where do the arrows come from?** | the numbers exist ([FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md)); the choice does not. Nunito Sans has no U+2190–U+2193, so picking it also picks "arrows are icons". Blocks freezing the design tokens, not M1 |

None of these blocks M1. All of them block hardware work — except A7, which
blocks three features that are not in the specification and cannot be sized
until it is answered.

## Build and test state

| Target | State |
|---|---|
| Host / native | builds; **twenty-one tests** pass, locally and in CI on `main` since #12 merged — smoke, capability registry, both halves of the layer-boundary check, localization, and the six suites this milestone added: trust, transport, power, position, diagnostics, and the replay rig with its fifteen traces, plus the
design-token suite and the two checks that keep raw colours and pixel counts out
of screen code. Under GCC and Clang, under `-Werror` with `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast`, and under ASan+UBSan with `-fno-sanitize-recover=all`. The negative half of the boundary check is verified against two deliberate breakages: a fixture that fails for the *wrong* reason is a failure, not a pass |
| Simulator | **builds and runs**, on the development host and **in CI from nothing** — run `32462413273`, cold cache, no LVGL on the machine: clone 22.8 s, commit verified against the pin, build, 6/6 tests, a screenshot per geometry uploaded, 2 min 2 s for the job. LVGL v9.5.0 + SDL2 2.30.0. Headless under `SDL_VIDEODRIVER=dummy`. Off by default (`-DATTADIPA_BUILD_SIMULATOR=ON`), so a machine with no SDL2 still gets a green host build |
| ESP32-S3 toolchain | **verified** — ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build` completes on a stock example |
| ESP32-S3 firmware | not started — there is no Attadipa firmware to build yet |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED`. Ten plans now exist with equipment, procedure and pass/fail criteria — [HIL_PLANS](docs/testing/HIL_PLANS.md) — so each unproven claim is visibly unproven rather than merely absent |
| Agent automation | **live and exercised in production.** Six workflows on `main`; the intake gate has accepted a real task, derived its labels from the marker and handed it to a Claude run that finished green (runs `32472498158`, `32472504777`). `actionlint` clean over all six with shellcheck integration, `shellcheck` clean over both scripts, the intake gate's 40-case hostile-input test and the watchdog filter's 17-case test pass. **Two defects fixed on 2026-08-22, both silent and both found by reading run logs rather than by anything going red:** the gate was given the issue body where it needed the comment, so every `@claude` mention ever written here was refused with "nothing asks for an agent" — including the owner's on #41; and a workflow-level concurrency group cancelled queued intake runs, so labelling #26, #27 and #28 `agent:ready` in one burst started no agent at all. The mention path had therefore never worked in production and nothing said so. `CLAUDE_CODE_OAUTH_TOKEN` is configured, so the loop draws on a subscription rather than a metered API account. See [automation](docs/automation/CLAUDE_AUTOMATION.md) |

Having ESP-IDF v5.5.5 on disk is not the same as having chosen it (T-004) — and
that decision no longer blocks M1, because M1 is the simulator.

## Hardware tests pending

All of them. No board has been powered on by this project and no measurement
has been taken. Nothing here may be described as hardware-tested.

What changed is that they are now *specified*.
[HIL_PLANS](docs/testing/HIL_PLANS.md) holds ten of them — which parts are
actually on the board, sleep current per state, whether deep sleep is deep and
the radio really off, the front-end regression as a measured noise floor, time
to first fix cold against hot, which interference indications each receiver
emits, energy per fix, USB surviving a cable pulled mid-frame, bonded reconnect
after a reboot, and the battery sag during a transmission — each with equipment,
a procedure, a pass/fail criterion and a place to write the result.

Every one is marked `NOT EXECUTED — HARDWARE REQUIRED`, and the file's own rule
is that a result is appended rather than written over the plan.

## Open conflicts

Recorded rather than resolved by preference. Two need a powered board, one
needs the owner, and one needs a ruler.

| # | Conflict |
|---|---|
| D15 | **The T-Watch panel's physical diagonal.** LilyGoLib's spec tables say 1.3" for the S3 and the S3 Plus by name; the schematic's LCD sheet says `QT154C2408` / `LCD_1.54-TOUCH`, and that vendor's sibling part `QT154H2201` is published as 1.54", 240×240, ST7789V — so the part number decodes. 240 × 240 is not in doubt; 261 dpi against 220 is. The code holds 1.3" as the **conservative** reading, not the confident one ([HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md#display-diagonal--conflicting)) |
| A7 | The published brand art (`pics/`) and the §42 palette disagree by more than rounding — the wordmark samples at `#E16439` against Attadipa Orange `#FF8A40`. An identity decision, so it waits for the owner ([pics/README.md](pics/README.md)) |
| H8 | The T-Watch vendor document calls ALDO1 unused; the schematic drives the `+3V3` rail from it. If the schematic is right, `+3V3` is switchable and carries five parts |
| ~~D12~~ → **D12b** | ~~PSRAM documented as quad; the `R8` marking is understood to mean octal~~ **Checked and split.** Table 1-1 of the ESP32-S3 datasheet has no 8 MB quad in-package part, so `R8` is octal. Closed for the Waveshare (D12a). Still open for the **T-Watch**, where a LilyGO document says QSPI and has not been read against that table |

## Assumptions in force

- The LilyGO PlatformIO pin to IDF 4.4.7 does not constrain Attadipa, which is
  ESP-IDF-native and does not use the Arduino layer. Flagged, not proven.
- Both boards' SoC is an ESP32-S3 — from both schematics (`ESP32-S3-R8`,
  `ESP32-S3R8`), but not from a chip readback.
- The radio capability facts in [ADR-0003](docs/adr/0003-radio-not-lora.md) come
  from RadioLib 7.7.1 and MeshCore `d929643` source, not from the TI and Silicon
  Labs datasheets, which refused automated retrieval. Recorded as **PARTIAL**,
  not VERIFIED.

## Recently completed

- **Heading no longer reads accel+gyro fusion as an absolute reference.**
  [#21](https://github.com/hleserg/Attadipa/issues/21): on the Waveshare
  profile — QMI8658 accel+gyro, no magnetometer, no local GNSS —
  `CapabilityRegistry` reported `Capability::Heading` as `Ready` from the IMU
  alone, contradicting the same-day [ADR-0009](docs/adr/0009-heading.md),
  which rejects accelerometer+gyroscope fusion by name: without a
  magnetometer, yaw is unobservable, and gyro-only integration drifts without
  bound
  ([research-integration.md §9](docs/upstream/research-integration.md),
  verdict `REJECT`). `tests/test_capability_registry.cpp` had locked the wrong
  answer in as `test_heading_has_three_sources`, so a green CI could not have
  caught it. Fixed in `core/src/capability_registry.cpp`: the local `Heading`
  mapping now has two sources, magnetometer or local GNSS
  course-over-ground, matching ADR-0007 §4 as corrected here and in
  [ARCHITECTURE.md](docs/architecture/ARCHITECTURE.md) §3.4. Waveshare with
  no node now reports `Unprovisioned`, not `Ready`; a node that actually
  offers `Heading` still lights it up as `Ready`/`Origin::Node`; the T-Watch
  GNSS path is unaffected. Mutation-checked: reverting the fix turns five
  checks red.
- **Smart tags, tracks and dead reckoning, researched rather than guessed at.**
  Three owner asks from 2026-08-21, none of which is in the specification —
  recorded in
  [TAGS_TRACKS_RECKONING](docs/research/TAGS_TRACKS_RECKONING.md), with nine
  tasks (T-063…T-071) and owner question A7. Thirteen agents, every claim that
  would become a design commitment put through an adversarial refutation, four
  claims downgraded as a result. The load-bearing answers: of the three tag
  ecosystems only Apple is reachable at all, and only outside its own app —
  Google needs registration, an email allowlist and a third-party lab, and its
  one readable implementation is licensed for Nordic silicon; Samsung's SDK
  ships for no Espressif part and an unregistered advertisement is inert.
  OpenHaystack and macless-haystack are AGPL-3.0 and cannot be copied here. An
  uncalibrated gyroscope offset of ±10 dps is a full 360° of heading error in
  36 seconds, so a reckoned path is a **disk**, not a line — and on the T-Watch,
  which has no gyroscope, a turn is not observable at all. A 1000-point track
  costs 16.5 s of originator airtime over LoRa at 4 bytes a point, which is what
  makes an online simplifier a requirement rather than an optimisation. Also
  corrected: the Waveshare carries a **QMI8658C**, whose `CTRL8` is "Reserved:
  Not Used" — the A's step-counter registers describe a part that is not on this
  board.
- **The clock-disagreement detector no longer relies on undefined behaviour.**
  `WallTime` is signed on purpose and has no subtraction on purpose; the
  anti-spoofing detector reached through `.unix_seconds` and derived one anyway,
  which is UB for the range the type deliberately admits — one hostile
  `receiver_time` reaches `-INT64_MIN`. `clock.h` gains `seconds_between`, and
  `unix_seconds` is now referenced nowhere outside it. Verified red: the old
  arithmetic fails the ASan/UBSan build.
- **The research integration, and the six test suites that came with it.** Core
  gained the types the GNSS integrity work needs — an observation that keeps
  both the normalized value and what the receiver actually said, ten separate
  state axes rather than one `quality`, a trust state with weighted evidence,
  hysteresis and kept reason codes — plus a link layer and a deterministic
  replay rig. Writing the tests found four real defects rather than confirming
  what was already believed: `next_state()` proposed moving an already-off GNSS
  receiver into backup, spending current to hold a domain with nothing in it;
  `start_kind()` read *having* a backup domain as evidence it had been
  *powered*, promising a warm start where the truth was cold; the trust
  evaluator read the interval between epochs after overwriting the timestamp it
  came from, so every rate detector silently did nothing; and `-Werror` caught a
  comma operator in the replay reader. Four of the author's own expectations
  were wrong where the code was right, and are recorded as such.
- **The automation loop, and what running it actually found.** Four workflows,
  an intake gate extracted into a script with sixteen tested cases, and a CI
  upgrade — strict warnings with zero debt, Clang, ASan+UBSan, coverage,
  actionlint. Then it was run rather than reasoned about, which produced three
  defects a green YAML lint could not: the agent could not authenticate because
  `id-token: write` was missing, so the action could not exchange its OIDC token
  for a Claude App installation token; `display_report` had been turned off
  beside `show_full_output` as if they were the same precaution, so a
  twenty-eight-turn run left nothing anybody could read; and the hand-over step
  could leave an issue labelled both `agent:working` and `agent:review`, which
  is invisible to the watchdog and finished-looking to a person.
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
  enum and the per-locale tables; `attadipa_l10n` sits beside core and is linked
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
- **The agent queue runs, and smoke test A found a defect rather than passing.**
  A task now arrives as a GitHub issue and is picked up without anybody carrying
  it: gate → claim → Claude → draft pull request → independent review → CI →
  repair, with an hourly watchdog for lost events and a daily backstop routine
  for the case the watchdog itself is not running. Exercising it on
  [#5](https://github.com/hleserg/Attadipa/issues/5) proved four of its five
  claims and broke on the fifth: a second run on an already-claimed issue left
  `agent:working` and `agent:review` set together, because `claim` removed only
  `agent:ready` and `Hand over` then matched the leftover `agent:review` and
  exited without clearing the claim. A task in that state is stuck working
  forever and the watchdog re-queues finished work every two hours. Fixed by
  making a claim actually a claim.
- **`reviewed_head` stopped being decorative.** The protocol has specified it
  since the marker was defined and nothing read it. The gate now compares it
  against the default branch and tells the agent how far the tree has moved and
  which files changed, with an instruction to verify a finding before
  implementing it. The expensive failure of a review queue is not a wrong
  finding, it is a stale one.
- **The agents were running with no tools, and that is why the loop produced
  nothing.** Agent mode grants no default `--allowedTools` and the headless SDK
  denies anything that would prompt, silently. The reviewer ran 41 s and posted
  nothing; the agent on issue #5 finished green with no branch and no pull
  request. Both had read everything and had no way to say so. Fixed and merged
  (#9, `b1a3dca`), and **the reviewer half is now observed working**: on
  [#11](https://github.com/hleserg/Attadipa/pull/11) the independent reviewer
  posted a full review carrying the `attadipa-ai-review` marker and set
  `ai-review:blocking`, which had never happened before in this repository. The
  writer half — a branch and a draft pull request from an agent run — is still
  unobserved; the open item is
  [#10](https://github.com/hleserg/Attadipa/issues/10).
- **And the reviewer would have been skipped on every agent pull request.** With
  `ATTADIPA_AGENT_TOKEN` unset — the documented default — the agent opens its pull
  request as `claude[bot]`, and `claude-pr-review.yml` excluded every actor ending
  in `[bot]`. The guard was aimed at Dependabot and caught the one case the
  workflow exists for. Found by an external review bot on #11, confirmed against
  the production runs where Dependabot's pull requests were skipped, and fixed by
  exempting `claude[bot]` alone.
- **The producer's identity is established, and the queue has an input again.**
  ChatGPT reaches this repository as `chatgpt-codex-connector[bot]` — a `Bot`,
  `author_association: NONE`, and the login that reviewed
  [#11](https://github.com/hleserg/Attadipa/pull/11). There is no user account
  behind it to grant write to, so the gate refused every task it could ever file.
  The owner's decision was the allowlist: `ATTADIPA_TRUSTED_PRODUCERS`, empty by
  default, `issues` events only, `claude` and `github-actions` never listable,
  exact login match. Thirteen tests cover those properties; the watchdog reads
  the same list, because it filters on `author_association` and would otherwise
  skip precisely these tasks.
- **And the first draft of that allowlist had a hole, found in review.** The
  watchdog hands over by `workflow_dispatch`, which the gate trusts by
  construction and does not re-check the actor for — so a `claude[bot]` entry the
  gate refuses to honour would have been honoured by the watchdog and dispatched
  through the one door that no longer asks. The repository's own output starting
  its own writer: the exact loop the allowlist was built to prevent. The
  non-listable rule is now enforced in both places, the scan filter moved to
  `.github/scripts/queue-scan.jq` so it can be executed, and 17 tests cover it in
  CI. There was no test before, which is why review caught it and CI did not.
- **The silent refusal was reproduced, not theorised.** A task with a valid
  marker filed through the GitHub API ([#10](https://github.com/hleserg/Attadipa/issues/10))
  arrived as `claude[bot]`, was refused by the bot guard — correctly — and was
  simultaneously invisible to the watchdog, which filters on
  `author_association` and saw `NONE`. The run went green and nothing was
  written anywhere. The route decides this, not the marker: issue #5, filed by
  `hleserg` as a `User`, was accepted the same day. Whether ChatGPT hits this
  depends on how it authenticates, which is
  [still open](docs/research/OPEN_QUESTIONS.md) and is the owner's decision.
- **A refused task is no longer silent.** An issue carrying a task marker that
  the gate rejects now gets one comment naming the guard and the actor, plus
  `needs-owner`. This is aimed at the likeliest silent failure in the loop: a
  producing agent filing through a GitHub App, whose login ends in `[bot]` and
  which every bot guard correctly rejects.
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
