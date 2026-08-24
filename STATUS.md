# Status

Last updated: 2026-08-22

Shape fixed by [final §93](docs/master-prompt-final.md). It is a status file,
not a history — what changed and why lives in git and in the ADRs.

## Current milestone

**M1 — simulator and the product design foundation.** M0.5, the reconciliation
forced by the owner's new master specification, is complete: all eight §75
items closed plus one the review did not list
([RECONCILIATION](docs/research/RECONCILIATION_2026-08-21.md)).

## Current implementation

**Attadipa has code.** As of 2026-08-22 the repository builds six libraries, a
simulator and twenty-four tests, and has a font pipeline whose output has been
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
are the owner's; the published brand art's sampled values disagreed and were
resolved in favour of these on 2026-08-22
([OWNER_DECISIONS.md](docs/research/OWNER_DECISIONS.md) OD-15).

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
  order final §58 gives: tokens, then the image asset pipeline (T-034 —
  **done**), the first Clock (T-037) and the first Settings (T-038).
- **T-034 — image asset pipeline — done.** `ui/assets/source/` →
  `tools/assets/` → `ui/assets/generated/`, with LVGL v9.5.0's `LVGLImage.py`
  vendored unmodified and pinned by hash. Deterministic, verified by
  byte-comparing two runs. The staleness digest covers the **converter** as well
  as the art, because an encoder that changes its output is the asset changing.
  Three refusals, each with a test that triggers it: a source over 512 px, a
  source under `docs/` or `pics/`, and a pixel size with no drawing behind it —
  the last being final §86 made mechanical, since the pipeline never resamples
  one size into another and the lookup returns `nullptr` rather than the nearest
  thing it has. Proved with three icons at three sizes: 14 457 B of `.rodata`,
  reported per asset. **Assets are named by pixels, never by board** — 39 px is
  `icon.size.lg` on one panel and `icon.size.md` on the other, one file, and a
  test asserts it.
- **T-043 … T-053** — the eleven the amendments produced, sitting in READY: the
  node link that is not a BLE link, resynchronisable framing, the `PowerState`
  taxonomy that cannot call a wake-on-LoRa sleep "hibernate", crash-safe
  persistence, two clocks, the crypto/RNG seam, the front end as a board
  capability, the adapter boundary test written before there is an adapter, and
  the three GNSS ones — MIA-M10Q, LS550G, and a simulator that can fail at GNSS
  twelve different ways.

**T-060 — what each IMU does about steps — done**, and it found the ADR-0003
pattern in a second subsystem. The BMA423 has a 32-bit hardware step counter
and **the current datasheet does not document it** — all four registers say
*"Application note – Wearable feature set"*, and that note returns HTTP 403.
The feature is a 6 144-byte blob the host uploads at every boot with a
mandatory 150 ms wait.

**T-060a — done, and the answer was not in the application note.** Bosch
**deleted** the step-counter chapter from its own datasheet between revision 1.1
(May 2019) and revision 2.0 (August 2019); revision 1.1 is still mirrored and
still prints all of it. Four of the five open questions are now `SUPPORTED`:
the counter **runs on the sensor while the host sleeps**, it needs **≥50 Hz**
and says so via `odr_50hz_error` if it does not get it, the power line is
**13–14 µA `ESTIMATED`** at that setting rather than 42 or 150, and the blob
**does not** survive a soft reset. One earlier reading here was wrong and is
corrected: the watermark field carries an implicit ×20, so LilyGo's default is
an interrupt every 20 steps rather than every step. The residue — behaviour at
the 32-bit boundary — is **T-060b** and blocks nothing. The lesson generalises:
*"the datasheet"* is a document **at a revision**, and the newest is not always
the most complete. On the Waveshare side the question is worse: the
QMI8658**C** documents a full pedometer, the QMI8658A's **Rev A** documented the
identical one, and **Rev D has deleted it** — feature list, chapter and registers
— with no deprecation note, while `HARDWARE_MATRIX` records the board's part as
*"QMI8658 / QMI8658C"*. Two findings change what a step count *means* on either
part: the engine retroactively counts steps it had discarded once it decides a
walk is real, and updates its registers only every N steps, so **a read is stale
by design**. [PEDOMETER_PARTS](docs/research/PEDOMETER_PARTS.md).

## Lookahead research

One to two steps ahead, per final §68 — not twenty.

| | Subject | For |
|---|---|---|
| NEXT | LVGL 9.5 on **octal** PSRAM AMOLED: draw-buffer strategy and realistic frame rate at 410 × 502 | M2. D12a is closed, so this is unblocked on the memory question — but T-093 first: the vendor BSP is not the existence proof it is taken for |
| AFTER NEXT | `SettingsService` persistence on a device with no filesystem yet — what T-038 writes to, and what T-046 has to guarantee about it | T-038 |

`LVGLImage.py` is off this list: T-034 answered it. Compression was examined and
**not** taken — RLE and LZ4 both trade flash for decode time and a scratch buffer
on a display bus nobody has timed, and nine masks are 14 kB. `RGB565A8` is
present and unused: every asset so far is an `A8` mask, because an icon with a
baked colour cannot follow a theme.

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
  each cited. The IMU's `0x6A` vs `0x6B` conflict between datasheet revisions
  is **settled by measurement** since this was written — the bench scan on
  2026-08-23 found `0x6B` and no answer at `0x6A`, so the revision Waveshare's
  own wiki links is the one that disagrees with the silicon. Five answer on a
  bare scan (`0x18`, `0x34`, `0x40`, `0x51`, `0x6B`); the sixth, touch at
  `0x38`, answers only after its reset is pulsed on GPIO 9.
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

Filed as [OD-7 to OD-13](docs/research/OWNER_DECISIONS.md), with the research
questions in
[COMPANION_AND_POSITION_SOURCES](docs/research/COMPANION_AND_POSITION_SOURCES.md)
and thirteen tasks, T-072 to T-083 and T-088. **Nothing is implemented.**
Recorded here because a fact that lives only in a chat log does not exist —
which is also why OD-12 and OD-13 exist at all: both were answered in issue
comments, and an answer sitting in a comment is a fact nobody downstream can
read.

Two of the seven **close** things rather than open them. OD-12 rejects
Meshtastic and OD-13 rejects tag emulation, and in both the licence is the
evidence while the product decision is the decision — recorded in that order, so
that a future licence change reopens only what it actually affects.

- **The companion is any node, not only ours** — vanilla MeshCore over BLE or
  LAN, several providers at once with a local radio, and telemetry as a
  request/response feed. It fits
  [ADR-0008](docs/adr/0008-mesh-service-providers.md)'s shape, and since
  2026-08-22 the protocol facts it needs are **read** — T-072 is done, and
  [MESHCORE_COMPANION_PROTOCOL](docs/research/MESHCORE_COMPANION_PROTOCOL.md)
  carries transports, framing, the command set and the three position scalings.
  Read from source and **never observed on a node** (T-072a); T-074 is open.
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
**The MeshCore half of that is now answered** (T-072).
[MESHCORE_COMPANION_PROTOCOL](docs/research/MESHCORE_COMPANION_PROTOCOL.md) has
the transports, the framing, all 58 commands and the three different position
scalings, read from the pinned `d929643` and with a provenance section saying
which claims a second reader confirmed and which rest on one. Three things
changed what we can plan:

- **LAN is real** — Wi-Fi/TCP and Ethernet/TCP, port 5000, one client at a time.
  A host-side client can speak to the node behind Home Assistant on `doctor`
  today, with no ESP32 and no BLE stack involved. That is T-072a, and it would be
  the first `OBSERVED` fact in this area.
- **176 bytes is the frame budget**, a bare `#define` with no `#ifndef` guard, so
  no peer can raise it. Every queue and buffer size on our side is bounded by it.
- **A companion's position arrives with no provenance and no age.** No fix flag,
  no satellite count, no timestamp, no HDOP — and `node_lat` is a single slot
  shared by the GNSS loop, the saved prefs and the client app, written only
  *inside* an `isValid()` branch, so a lost fix leaves the last value standing. A
  receiver cannot distinguish a live fix from a six-hour-old one from a
  hand-typed coordinate. [ADR-0011](docs/adr/0011-gnss-integrity.md) must supply
  both from outside, and motion-gated GNSS (OD-10) cannot lean on a companion's
  fix to decide whether the wearer moved.

**The Meshtastic half is closed, and the gate was licensing.**
`meshtastic/protobufs` *is* a separate repository with its own `LICENSE`, and
that `LICENSE` is GPL-3.0, the same as the firmware. No exception clause, no SPDX
header in any `.proto`. That closed the cheap path — a linked client is not
available under this repository's own rule — and the four options went to the
owner as [#41](https://github.com/hleserg/Attadipa/issues/41). **They chose
option 4: not supported.**
[OD-12](docs/research/OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence),
2026-08-22. T-073 is `REJECTED` — not blocked, not deferred, and not waiting on
protocol facts it will never need.

- **And one defect, not a feature.** The simulator draws with LVGL's stock
  Latin-only Montserrat, so `×` renders as `□` and so do the Cyrillic letters in
  the English catalogue's own language names. The check already reports seven
  undrawable codepoints on every run; what is missing is that it is a warning
  rather than a failure, and that nothing consumes the font pipeline T-032 built.
  Filed as **T-083, P1**.

## The Waveshare board arrived — 2026-08-22

One `ESP32-S3-Touch-AMOLED-2.06` is on the owner's desk, opened and
photographed. The readings are
[WAVESHARE_BOARD_RECEIVED](docs/research/WAVESHARE_BOARD_RECEIVED.md); the
bring-up order they feed into is
[WAVESHARE_ARRIVAL](docs/research/WAVESHARE_ARRIVAL.md).

**The schematic was right.** Nothing the unit shows contradicts anything in
[VERIFIED_FACTS](docs/research/VERIFIED_FACTS.md). Three things were new:

**And then it answered for itself.** Later the same day the owner read the chip
over its own USB-Serial/JTAG port — `espefuse summary` and `esptool flash-id` —
which is the first evidence here that came from neither a document nor a camera:
[WAVESHARE_EFUSE_READ](docs/research/WAVESHARE_EFUSE_READ.md).

- **D12a is closed on silicon.** `PSRAM_CAP = 8M`, `PSRAM_VENDOR = AP_3v3` — so
  the part is `R8`, not the 1.8 V `R8V` — and `PIN_POWER_SELECTION = VDD_SPI`
  puts GPIO33–37 on the memory rail. The eFuse gives capacity and rail, not bus
  width, so the step to *octal* is still Table 1-1's; but both legs now have
  evidence and **five GPIOs are gone for good**, fused rather than argued.
- **The flash is confirmed external and quad**: JEDEC `0xC8 0x4019`, and
  `FLASH_CAP`/`FLASH_TEMP`/`FLASH_VENDOR` unprogrammed in BLOCK1.
- **The chip is revision v0.2.** A build must keep `CONFIG_ESP32S3_REV_MIN` at 0
  or the bootloader refuses it. **D18 is resolved and the answer is "all of
  them"** — all eight errata in sheet v1.3 list `v0.0 v0.1 v0.2` as affected,
  seven say `No fix scheduled`, and there is no later revision to want
  ([ESP32S3_ERRATA_V02](docs/research/ESP32S3_ERRATA_V02.md)). CACHE-126 is the
  one with teeth: it is on the octal-PSRAM path, its ESP-IDF workaround masks
  every interrupt at `XCHAL_NMILEVEL` and freezes the data cache, and it is not
  switchable. What that costs is `UNKNOWN` and `NOT MEASURED`.
- **Nothing has been burned.** Every fuse is at its factory default, so every
  recovery path is open. That baseline is recorded so a later reading that
  differs means something happened.

Taking the factory backup surfaced a failure mode worth knowing before anyone
repeats it: `esptool read-flash` **with the stub** aborts at flash addresses that
repeat exactly across runs starting from different offsets, so retrying is
guaranteed to fail identically — `--no-stub` completes the same ranges. And
`esptool` leaves a **short file** behind on abort, so concatenating chunks
without checking each one's length produces a silently shifted image.
[WAVESHARE_EFUSE_READ](docs/research/WAVESHARE_EFUSE_READ.md) §2.

**And then the flash was read.** The partition table and two data partitions
came off the unit the same day —
[WAVESHARE_FLASH_LAYOUT](docs/research/WAVESHARE_FLASH_LAYOUT.md).

- **28 of 32 MB is partitioned**, with a 9 MB `factory` *slot* and two 6 MB OTA
  slots. The image in that slot turned out to be **4.94 MB**, so it does fit an
  OTA slot after all — the earlier inference that a 9 MB partition implies a 9 MB
  image was wrong and is withdrawn. What still holds is that `otadata` is blank
  and `ota_1` is erased, so nothing has ever updated this unit.
- **The factory backup is verified and T-099 is `DONE`.** `esptool verify-flash`
  over all 33 554 432 bytes returns `Verification successful`, and three
  independent complete reads — one on Windows over native USB, two on Linux over
  USB/IP — agree byte for byte. **The first flash of our own firmware is now
  reversible.**
- **Two complete applications are on the flash, not one.** `phone_s3_box_3` in
  `factory` and **`xiaozhi` `1.8.5`** in `ota_0`, both built with **ESP-IDF
  `v5.5.1-dirty`** — read out of the application descriptors, not guessed from a
  wake-word model. T-104 should read xiaozhi at **tag `1.8.5`**, not `HEAD`.
- **The stock firmware is `xiaozhi-esp32`.** The `model` partition holds WakeNet9
  `wn9_nihaoxiaozhi_tts`, so the launcher's AIChats app is that project — which
  means this board's audio path is already written down by somebody who had it
  working. Licence first: **T-104**.
- **The vendor bakes raw pixel buffers**, not encoded images, with no decoder on
  the device — and **T-103 has since decoded them.** Each image is exactly
  411 652 bytes: a 12-byte header (`u32` magic, `u16` width 410, `u16` height
  502, `u32` stride 820) followed by **410 × 502 RGB565 little-endian**. The byte
  order was settled by rendering, which is the only thing that could settle it —
  little-endian gives coherent artwork, big-endian gives noise. The panel's pixel
  format and byte order are now facts about the hardware rather than a preference
  of T-034's.
- **And the partition holds six files, not three.** There is a `/music/`
  directory with three MP3 background tracks, two stereo, 112–128 kbps. That
  gives **T-105** a strong prior that `AAC210602A1` is the speaker rather than a
  haptic actuator, though only tracing the pads closes it. It also means the
  factory image carries **third-party all-rights-reserved audio**
  (`All Rights Reserved to www.Art-list.io`), which is a second and sharper
  reason the dump never goes near this repository.
- **`tools/flash/spiffs_extract.py`** does SPIFFS extraction without `mkspiffs`
  and without an ESP-IDF build. `strings` recovers a SPIFFS image's file names
  and none of its file bodies.
- **One claim had to be withdrawn.** A parallel reading of the same unit concluded
  the PSRAM is *quad*, on the reasoning that "octal PSRAM would be 1.8 V". It is
  not: Datasheet v2.2 Table 1-1 lists `ESP32-S3R8` as `8 MB (Octal SPI)` at
  **3.3 V** in one row, and the table has no 8 MB quad in-package part at all.
  That mattered because the conclusion drawn from it — plan LVGL draw buffers
  against quad throughput — is an architectural constraint on the tightest budget
  this board has.
- **Two rows are `CONFLICTING`**, not overwritten: whether `AAC210602A1` is the
  speaker or a haptic actuator (**T-105**, and T-097 sits on top of it), and the
  battery connector's pitch, which a photograph cannot establish.

**A bigger battery is under consideration, and the fitted one is probably not
400 mAh.** The cell turns out to be on a removable 2-pin plug rather than
soldered, which makes it a real option — and researching what to order produced
a headline nobody expected. `402728` is 3.024 cm³, so the sticker's 400 mAh at
3.7 V implies **132.3 mAh/cm³**, against an observed **87–102** band across 51
datasheet cells from four manufacturers at footprints ≤ 32 mm: +22 % on the
densest cell found in any footprint in that sample, +52 % on the median. Honest
expectation **250–310 mAh** — so a same-size replacement buys no capacity at
all, and 400 mAh in this footprint costs 5.5 mm of thickness rather than 4.0.
[BATTERY_UPGRADE](docs/research/BATTERY_UPGRADE.md) — ESTIMATED from published
datasheets, not measured. **What to order now has a decision tree rather than an
open question**, gated on three measurements only the owner can take: the
closed-case clearance, the clear rectangle *and its diagonal*, and the mass of
the fitted cell, which is the lie detector — 6.0–6.5 g is consistent with
280–330 mAh and no sampled pouch reaches the density a genuine 400 mAh would
need. **T-106** holds all three, and **all three are still untaken** — a caliper and
a scale, the two instruments T-106 names, plus plasticine for the closed-case
clearance, which is a squashed-thickness read and **not** something a feeler
gauge can take through a closed cover. None of them has been used. The bus-scan
half of T-106 is **done** — 2026-08-23, and `0x6A` is free after all, which is
what the claim said before anybody could know it — and an earlier version of
this paragraph let that stand in for the rest, listing *"what remains"* as the
magnetometer modules and the register reads while silently dropping the
clearance, the rectangle-and-diagonal and the mass. That is the one wrong
sentence here that spends money: an owner reading it concludes the battery gate
is clear and orders against the `ESTIMATED` 250–310 mAh tree without the mass —
this paragraph's own lie detector — or the diagonal, which T-106 calls the
measurement that gets forgotten. Found in review. **What remains under T-106 is
M1, M2 and M3 — the clearance, the rectangle and its diagonal, and the mass —
plus** confirming the magnetometer modules once they arrive
([#83](https://github.com/hleserg/Attadipa/issues/83)), and the register reads
that go with them.

**Both inheritable charge currents are wrong for the real cell.** Waveshare's own
demo sets 400 mA, which is 1.33C on ~300 mAh against a 1.0C class maximum, and
it is a deliberate change — upstream XPowersLib's copy of the same file sets
200 mA. The power-on default cannot be quoted at all: `REG 0x62`'s reset value is
eFuse-trimmed and has never been read on this board. The Waveshare BSP configures
the charger not at all, so whichever value is there at boot is the one charging
the cell. Precharge and termination both default four times higher than the
convention, and the input limit defaults to 1500 mA on a port that granted 500.

- **The cell's sticker says 400 mAh**, where the row said `UNKNOWN` and the
  T-Watch carries 940 — and the sticker is now the thing in doubt, not the
  reading of it: see the paragraphs above, `ESTIMATED` 250–310 mAh. Either way
  the board with far less energy is the board with the emissive panel, and the
  day theme costs 13.9× the night theme on the same pixels (`ESTIMATED`).
  "Which theme is default on the Waveshare" is now a power question — **T-095**,
  and a sharper one if the real figure is a third of the T-Watch rather than
  a half.
- **A ten-pad expansion row** nobody had transcribed, and the trap in it: `IO15`
  and `IO14` are printed as bare GPIO numbers and are the main I2C bus, with six
  devices already on them. The one free channel for an attached node is the UART
  — **T-096**.
- **No vibration motor is fitted.** The drive circuit is there and correct; the
  actuator is not, and no firmware can detect the difference. `Capability::Haptics`
  is `Unsupported` on this unit, which is the enum's terminal value — **T-097**.

The IMU's board-frame axes turn out to be silkscreened next to it, which is half
of H15. The other half — how the board sits in the case — needs the assembled
watch tilted through known angles.

**Before anything is flashed, the factory image must be read out.** It is not
published in restorable form and overwriting it is the first irreversible thing
available on this board.

## Blocked

- **T-061 the pedometer** — down to one question, and it is not a reading
  question. T-060a settled the BMA423 side: **13–14 µA at 50 Hz in low-power
  mode**, the counter runs while the host sleeps, and the wrist preset is already
  the default. The Waveshare side was blocked on *which* QMI8658 document
  describes the silicon, and **the silicon answered on 2026-08-23**:
  `REVISION_ID = 0x7C`, the QMI8658A `13-52-25` Rev A value, against `0x79` for
  the QMI8658C Rev 0.6 document that has no pedometer in it. `CTRL8 = 0x90` was
  written and read back exactly — the register Rev 0.6 calls *"Reserved: Not
  Used"*. So the hardware engine is documented and the register map is settled.
  What is left is **T-112: someone has to walk with the watch.** Step count
  stayed 0 on a board lying on a desk, which is the correct reading and no
  evidence either way.
  Still open beside it, and unchanged: **[H8](docs/research/OPEN_QUESTIONS.md)** —
  whether the AXP2101 keeps the IMU's rail up across an SoC sleep. If it does
  not, the 6 kB blob is gone and the 150 ms is owed again on every wake.
- **T-010 board bring-up** — **half unblocked as of 2026-08-22.** A physical
  Waveshare `ESP32-S3-Touch-AMOLED-2.06` is on the desk; a T-Watch is not, and
  the T-Watch's variant question (which of five radios, which of two GNSS
  modules) has an *answer* from the order listing — SX1262 at 868 MHz, and the
  owner's recollection of MIA-M10Q — but not a *marking read*, which is the only
  thing that moves `RadioChip::Unknown`, and needs the unit in hand. The GNSS
  half has no enum to move: there is no `GnssModule` type in the tree, so its
  gate is documentary where the radio's is in code. The Waveshare half is
  no longer blocked and is no longer being *done* either — see the section
  above; **nothing in this repository may say `PASS` until somebody runs a test
  on the board and writes down what came out.**
- **T-011 interference measurement** — still blocked, but the reason has
  changed and it now has an end date. It used to be unmeasurable in principle:
  neither board has a magnetometer, so the headline haptics-versus-compass
  concern had nothing to measure it with. **A5 is answered** — the owner has
  ordered two magnetometer modules and is soldering one on
  ([#83](https://github.com/hleserg/Attadipa/issues/83),
  [MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md)), which turns
  this from impossible into merely waiting for parts. On the Waveshare unit it
  stays doubly blocked for a second, unrelated reason: that unit has no
  vibration motor fitted at all, so there is nothing to interfere.

## Waiting on the owner

| | Question | Why it matters |
|---|---|---|
| A6 | Does the Attadipa node carry a magnetometer? | decides what "compass" can mean — and even if the answer is yes, node orientation is **not** watch orientation ([ADR-0009](docs/adr/0009-heading.md) §3) |
| A11 | **Do one or two of the T114s carry GNSS?** ([#124](https://github.com/hleserg/Attadipa/issues/124)) The fleet table records one GNSS-capable node; the indoor-fix observation in OD-16 says "either unit" | nothing is blocked either way — no node here gets an indoor fix under either reading — but [#90](https://github.com/hleserg/Attadipa/issues/90) and [#91](https://github.com/hleserg/Attadipa/issues/91) both plan against that table |
| D16 | **Inter or Nunito Sans, and where do the arrows come from?** | the numbers exist ([FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md)); the choice does not. Nunito Sans has no U+2190–U+2193, so picking it also picks "arrows are icons". Blocks freezing the design tokens, not M1 |

None of these blocks M1, and the table says what each one *does* block rather
than sharing one sentence: A6 blocks what "compass" is allowed to mean, D16
blocks freezing the design tokens, and A11 blocks nothing either way — it is
here because two planned tasks read a fleet table whose answer it changes, not
because work stops without it. The blanket *"all of them block hardware work"*
this line used to carry was written when the table held one kind of question,
and by the time A11 was added it contradicted the row directly above it.

**A1, A2 and A3 are answered as owner questions** — and A1 is off the table
above because what is left of it is not an answer somebody owes us: the T-Watch
half is the post arriving, and the Waveshare half is a loupe on a board already
on the desk, now filed as `OPEN_QUESTIONS` **D20** rather than left live in a
table whose rule is that every `A`-question is an open `needs-owner` issue —
[#54](https://github.com/hleserg/Attadipa/issues/54), on 2026-08-22, recorded
as
[OD-16](docs/research/OWNER_DECISIONS.md#od-16--a1-a2-and-a3-no-watch-yet-sx1262-confirmed-by-listing-and-three-meshcore-nodes-instead-of-one).
Waveshare is received and its **identity** is `VERIFIED` from the silkscreen —
the board is the one schematic V1.0 describes. Its **revision** is not: the
silkscreen carries the product name, whose `2.06` is the panel diagonal, and no
revision field has been read off the unit, so V1.0-derived rows are confirmed
against a document rather than against the board. What remains unread there
is that revision (D20) and the display-FPC part marking (D19) — U2 and U3 were
answered off the die itself, `WAVESHARE_EFUSE_READ` §1.2–1.3; T-Watch
S3 Plus is `ORDERED`, not `PRESENT` — the T-010 blocker above is unchanged
until it arrives and its radio marking is read off the physical part. A2:
SX1262 at 868 MHz by order listing; MIA-M10Q from the owner's recollection,
because the listing is silent on GNSS — the good outcome
ADR-0003 flagged as possible but not guaranteed. A3: three MeshCore nodes
(Heltec V4 companion, two Heltec T114s), not one. Two questions this answer
raised are filed separately rather than folded in: T114 band
([#89](https://github.com/hleserg/Attadipa/issues/89)) and the
three-firmware-revision compatibility matrix
([#90](https://github.com/hleserg/Attadipa/issues/90)); the indoor-GPS-fix
constraint is filed as [#91](https://github.com/hleserg/Attadipa/issues/91).

**A5 was answered on 2026-08-22** and is struck from the table above: an
external magnetometer is intended, the owner has ordered a **CJMCU-9911
(AK09911C)** and a **GY-271 (QMC5883L)**, and one of them is going inside the
watch. The five magnetometer epics are dormant, not dead. Which of the two
parts, and where it physically sits, are open —
[MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md) has the
datasheet comparison and the one measurement that decides it. A6 stays open and
stays independent: a node's magnetometer and a wrist's magnetometer answer
different questions.

**A7 is answered** — [#33](https://github.com/hleserg/Attadipa/issues/33), on
2026-08-22, recorded as
[OD-13](docs/research/OWNER_DECISIONS.md#od-13--no-tag-emulation-a-track-is-a-way-back-on-foot-and-saving-one-whole-is-a-separate-feature).
No tag emulation in any ecosystem, so **T-064 is closed** rather than unblocked;
a track is scoped by distance from learned familiar ground on foot rather than
by duration, which **unblocks T-065** and re-sizes it downward; saving a whole
track on request is a second, independent feature and is filed as **T-088**; and
T-071 is not blocked, because "get me back on foot" is the one purpose that
survives the physics. The three numbers the recording rule needs — threshold,
hysteresis and dwell — are to be computed and shown, not chosen.

## Build and test state

| Target | State |
|---|---|
| Host / native | builds; **twenty-four tests** pass, locally and in CI on `main` since #12 merged — smoke, capability registry, both halves of the layer-boundary check, localization, and the six suites this milestone added: trust, transport, power, position, diagnostics, and the replay rig with its fifteen traces, plus the
design-token suite and the two checks that keep raw colours and pixel counts out
of screen code. Under GCC and Clang, under `-Werror` with `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast`, and under ASan+UBSan with `-fno-sanitize-recover=all`. The negative half of the boundary check is verified against two deliberate breakages: a fixture that fails for the *wrong* reason is a failure, not a pass |
| Simulator | **builds and runs**, on the development host and **in CI from nothing** — run `32462413273`, cold cache, no LVGL on the machine: clone 22.8 s, commit verified against the pin, build, 6/6 tests, a screenshot per geometry uploaded, 2 min 2 s for the job. LVGL v9.5.0 + SDL2 2.30.0. Headless under `SDL_VIDEODRIVER=dummy`. Off by default (`-DATTADIPA_BUILD_SIMULATOR=ON`), so a machine with no SDL2 still gets a green host build |
| ESP32-S3 toolchain | **verified** — ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build` completes on a stock example |
| ESP32-S3 firmware | not started — there is no Attadipa firmware to build yet |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED`. Ten plans now exist with equipment, procedure and pass/fail criteria — [HIL_PLANS](docs/testing/HIL_PLANS.md) — so each unproven claim is visibly unproven rather than merely absent |
| Agent automation | **live — and the unattended half of it had never once worked.** The hourly watchdog hands a task over with `gh workflow run claude-agent.yml` under `GH_TOKEN: ${{ github.token }}` (`agent-queue-watchdog.yml:54` "GH_TOKEN: ${{ github.token }}", `:218` "gh workflow run claude-agent.yml"), so the dispatching actor is `github-actions[bot]`. `claude-agent.yml` passed `allowed_bots: ""` to `anthropics/claude-code-action`, which refuses a non-User actor absent from that list: *"Workflow initiated by non-human actor: github-actions (type: Bot)."* Five seconds, no execution log written, and the hand-over could only report `no conclusion`. Every autonomous run since the watchdog was added died there; #27, #28, #67 and #69 were written off as unexplained model deaths and **T-107 was opened to investigate the reading list, which was the leading theory and was wrong**. The only successes were runs a *person* started by commenting — which is invisible unless the actor of each run is lined up against its outcome. Fixed by naming the dispatcher: `allowed_bots: "github-actions"`, which is strictly narrower than `'*'` (that would let any installed GitHub App drive a write-capable agent, and a test now refuses it) and is **not** a producer grant — `queue-scan.jq` still refuses `claude` and `github-actions` in `ATTADIPA_TRUSTED_PRODUCERS`, so this repository's own output still cannot enqueue a billable writer. Both halves were defensible alone and only the *pair* was wrong, which no single file's review could ever show, so it is a test rather than a comment: `.github/tests/bot-actor-test.sh`, 19 assertions, reimplementing `isAllowedBot` in shell and asserting the watchdog still dispatches with the built-in token — proven to fail against the pre-fix tree, in both places. Found only because #81's `failure-reason.sh` replaced *"the cause is in the run log"* with *"no execution log was written — the agent step did not get far enough to leave one"*, which pointed at the step instead of the model. **And the same defect was in the reviewer, where it hid better.** `claude-pr-review.yml`'s `if:` deliberately admits `claude[bot]` — a blanket bot guard had skipped the review on the agent's own pull requests, the ones it exists for — and then handed the action `allowed_bots: ""`, the one list that does not contain `claude`. Runs `32597016812` (#95), `32596445164` (#94), `32595947792` (#92) and `32595273274` (#88): five, five, five and four seconds, byte-identical *"Workflow initiated by non-human actor: claude (type: Bot)"*, no execution log. **No agent-authored pull request had ever been reviewed**, and every one of those jobs reported **success**, because the `Review` step carries `continue-on-error` — which is right for its own reason and turned a refusal into a green tick. The workflow's own "the review did not run" comment listed five candidate causes and this was not among them; it is now cause 1. Fixed as `allowed_bots: "claude"`, and the test now asserts the *rule* — a workflow that admits a bot in its `if:` must name it, and none may name `'*'` — over all three agent workflows, so a fourth is checked the day it grows an exemption. **And the writer's turn ceiling was the same day's most expensive defect.** Six runs on 2026-08-22 — #71 three times, #67, #75, #78 — were accepted, posted an accurate plan about three minutes in, and died at turn 61 of a 60 ceiling with nothing on the branch: `error_max_turns`, `num_turns: 61`, **$3.00 each**, after 8 min 49 s of real work (run `32587675386`). An accurate report over an empty branch is the one outcome nobody can act on. The identical incident had already happened to the *reviewer* the same day and been fixed — 40 → 100, with the reasoning written down — and the writer, which does strictly more, was left at 60. Now 200; spend stays bounded by `timeout-minutes: 60`, which is what is actually billed. **Raising it exposed the failure underneath**: the next run of #67 died in ninety seconds instead of nine minutes with `subtype: success`, `is_error: true`, `num_turns: 20`, `permission_denials_count: 0` (run `32589375744`) — a real session that ended badly under a name reserved for one that did not, naming no cause, and `permission_denials_count: 0` rules out the tool-list failure. The published log holds the SDK options, an init line and that object, because `show_full_output` is off and that is correct — so the outcome comment was telling people the cause was in a log emptied of the cause on purpose. `.github/scripts/failure-reason.sh` now reads the *unpublished* execution log on the runner and puts one line on the issue, chosen by a whitelist of error grammars (an API status, a context refusal, a credit balance, an expired token) with everything else reported as `unclassified` plus structural facts only. The whitelist is the security model: that log holds every tool result, and the 27-case test puts an API key, a token and a private key beside a real error to prove only the error comes out. **Why #67 died is now known — it is the `allowed_bots` refusal above**, and the whitelisted line was what pointed at it. The 500 KB reading order the prompt mandates, `TASKS.md` alone 149 KB before the agent opens a file of its own, was the suspected cause and was not it; it survives as T-110 on its own merits rather than as an explanation for anything. **live and exercised in production.** Seven workflows on `main`; the intake gate has accepted a real task, derived its labels from the marker and handed it to a Claude run that finished green (runs `32472498158`, `32472504777`). `actionlint` clean over all seven with shellcheck integration, `shellcheck` clean over every script, the intake gate's 49-case hostile-input test and the watchdog filter's 37-case test pass. **Three defects fixed on 2026-08-22, all silent and all found by reading run logs rather than by anything going red:** the gate was given the issue body where it needed the comment, so every `@claude` mention ever written here was refused with "nothing asks for an agent" — including the owner's on #41; a workflow-level concurrency group cancelled queued intake runs, so labelling #26, #27 and #28 `agent:ready` in one burst started no agent at all; and the hand-over step's pull-request lookup asked GraphQL for `issue(number:)`, which does not resolve pull requests, so an agent started from a comment **on a pull request** got a `NOT_FOUND` document that `gh` had already written to stdout — `|| echo ""` does not undo that — and the outcome comment on #71 went out as ``### Done — pull request #{"data":{"repository":{"issue":null}}…``. `issueOrPullRequest` answers for both, and — the review's finding on the first fix — *pushed to this pull request* now requires the head to have actually moved, because a pull request is open before the agent starts and open after whatever it does; a clean run that pushed nothing says so instead. And the *before* head is read inside the writer queue rather than at event time — the gate fires the instant an event arrives while the agent job waits in `attadipa-agent-writer` for up to an hour, so a gate-time snapshot would have credited this run with a push made by whoever moved the branch in that window. A run that pushed a commit and then died says both things: work landed, and it may be half of it. And a cross-reference is evidence only if it was made **during** the run: #75 cites #71 five times, so filing #75 created that reference before any agent started, and the step announced *"Done — pull request #71"* for a run that produced nothing — then labelled the issue `agent:review`, so nothing would re-queue it. A `Fixes #N` still needs no timestamp; a bare mention does. **And a bare cross-reference is not an answer at all** — the fourth defect in this one step, reported as [#76](https://github.com/hleserg/Attadipa/issues/76) by the producing agent an hour after the third was merged. The step's second question was *which open pull request mentions this issue at all*, and a mention is created by any pull request naming it: #75 cites #71 five times as evidence, so filing #75 made that reference before any agent existed, and a run that produced nothing announced *"Done — pull request #71"*. Filtering mentions by time proved only that one appeared **during** the run, never that this run **caused** it — correlation standing in for ownership. The question is now gone rather than qualified again, and the reason it could go is in the prompt: a research pull request is required to carry `Fixes #N` in the same words as an implementation one, so nothing compliant needed the fallback. A closing reference also no longer launders a failed run into a success — a pull request that exists over a run that died says both things. A cut-off run is also told, in words, that **nothing automated will come back for the unfinished part**: the watchdog scans issues, not pull requests, so no label on a pull request queues anything — the first version added `agent:ready` there and review pointed out the label was promising something that could not happen. The whole decision moved into `.github/scripts/handover-decision.sh` with a 37-case test, because every defect this step has had lived in shell embedded in a workflow where nothing could execute it. The mention path had therefore never worked in production and nothing said so. `CLAUDE_CODE_OAUTH_TOKEN` is configured, so the loop draws on a subscription rather than a metered API account. **And nobody had ever recorded which model was answering.** The action has no `model:` input, so the model is a string inside `claude_args` — and an absent string is not an error, it is a silent fall back to the CLI default. All three agent workflows had run that way since the day they were built, which makes every past result unattributable and any comparison between two runs meaningless. Now `--model claude-opus-5 --effort max` in the writer, the reviewer and the CI repairer, with the flags read off `claude --help` rather than guessed (`--effort` takes one of `low, medium, high, xhigh, max`; there is no `ultracode` level, and the test rejects one). The full model name is pinned rather than the `opus` alias so that a new Opus cannot quietly change what this loop is. Owner decision, 2026-08-22; `bot-actor-test.sh` now holds it, and fails both when a pin is missing and when the level is not one the CLI accepts. **And the last link in the queue had no automation in it at all.** An agent opens a pull request, CI goes green, the reviewer sets `ai-review:pass` — and then nothing happens, because [CLAUDE.md](CLAUDE.md) makes the merge an *orchestrator* act and an orchestrator is a live session. On 2026-08-22 six finished pull requests (#88, #92, #94, #95, #97, #103) sat green for hours, each carrying `Fixes #N`, and the six issues behind them stayed open — which is what the owner actually complained about, one link downstream of the cause. Two defects fed it: five of the six had been left as **drafts**, and nothing merges a draft (`claude-agent.yml`'s Hand over step now runs `gh pr ready` on the two outcome kinds that carry a pull request the run actually moved, and never `--undo`); and the unattended backstop merges `docs/` only, three per run, so a green pull request touching `core/` was waiting for a human-shaped event the design never scheduled. **`pr-merge-sweep.yml`** closes it, every half hour, on the same conditions the owner had already approved for the backstop — transcribed rather than reinvented, **including the path allowlist**. It invokes no model and costs nothing from Anthropic. **Not** GitHub's native auto-merge, which cannot read a label: `ai-review:blocking` is a label on purpose, because a verdict is a judgement and a red tick is a fact, so arming native auto-merge would merge exactly the pull requests the reviewer stopped. The decision is a file rather than shell inside a workflow — `.github/scripts/merge-candidate.sh`, 85 assertions in `.github/tests/merge-candidate-test.sh`, every condition asserted in both directions plus the *order* of two of them, proven to fail against four distinct real defects. **The first version of this dropped three conditions, all in the widening direction, and review caught every one.** It had no path condition at all, so it merged over `core/`, `.github/` and `docs/automation/` — an agent could have opened a pull request replacing this very rule and had the sweep merge it, and a gate that can widen itself is not a gate. It read `ai-review:pass` as present-or-absent, when the label records that a verdict was reached and never *which commit* it was reached on: a review that reaches no verdict — a spent quota, a cancellation, an actor refusal, the workflow-validation skip that reports **success** — leaves the previous commit's label in place, so the next commit would have merged reviewed by nothing. And it had no per-run cap. All three now hold, plus a `timeout-minutes`, a Codex condition that no longer counts a bot's own comment as an answer, and an API failure that no longer prints the same line as an empty queue. It also claimed undrafting starts a fresh review: it does not — GitHub raises no workflow run from a `GITHUB_TOKEN` event, and saying a second opinion was obtained when none was is worse than not splitting the passes at all. What guards the merge is the head-commit condition, which survives an undraft because undrafting changes no commit. **So a green pull request touching `core/` still waits for an orchestrator session**, exactly as [CLAUDE.md](CLAUDE.md) says: widening that is the owner's decision, not this rule's. **And a fifth widening was found on 2026-08-23: a commit status is not a check run.** GitHub returns both in one `statusCheckRollup` and the sweep flattened them, so a third-party app's green commit status counted toward "some check ran". The app in question is Devin, whose status on every head commit here reads *"Devin Review · success · Full review skipped: trial expired and no credits remaining"* — a green tick for a review that did not happen, and on a head whose own workflows were still `action_required`, the **only** context present, which made the combined state `success` over a pull request nothing had looked at. Statuses are now tagged `status:` in the query: a red or pending one still holds the merge, because a red third party is information, but a green one can no longer satisfy the condition that CI ran at all. Owner asked on 2026-08-23 for Devin to be removed as a reviewer outright and **uninstalled the app on 2026-08-24**, so it posts nothing further; the rule stays, because the next third-party app to be installed will not be announced either. **And the agent's own pushes were never running CI, which nothing could see because nothing was red.** [#75](https://github.com/hleserg/Attadipa/issues/75). Both runs for #71's head `488be1e` — CI `32581052659` and the review `32581052664` — were created and completed **in the same second** with `conclusion: action_required` and **zero jobs**; `get_check_runs` returned `total_count: 0`, so no check ever appeared on the pull request. The agent then reported it was waiting on CI, which was true and would have stayed true forever, while the orchestrator's *merge once CI is green* had no verdict to read and `agent:review` said the work was with the reviewers. **The issue named four candidate settings and the answer is that none of them applies**: attempt 1 records `head_repository == repository`, so this was a branch here and not a fork, and all three *Approval for running fork pull request workflows from contributors* options — first-time-contributors-new-to-GitHub, first-time contributors, all external contributors — gate **fork** pull requests only; changing any of them would have fixed nothing and removed a real protection from a public repository. The fourth candidate, the pull request's author being an app, is contradicted by the issue's own table: head `31c2c39` had the same `claude[bot]` author and ran. The rule is documented and is about the **token that created the event**, not a setting: *"When a pull request is created or updated by a workflow using `GITHUB_TOKEN`, `pull_request` events with the `opened`, `synchronize`, or `reopened` activity types create workflow runs that require approval"* — [Events that trigger workflows § `pull_request`](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request), read 2026-08-23. **There is no repository setting that disables it**; GitHub's own remedy is a different credential. Note the sibling behaviour and the difference between them, because it is easy to read one as the other: for every *other* event a `GITHUB_TOKEN` push creates **no run at all** — which is what the undraft sentence above says, and it is still correct, `ready_for_review` not being one of the three activity types — while for these three it creates **a run record that never starts**, and a run record looks like something happened. **Where this repository produces it was traced rather than assumed.** The commit's git author and committer are `claude[bot]`, because the action sets the git identity; the run's `actor` is `github-actions[bot]`, because that is the identity the push was *authenticated* with, and those two disagreeing is the fingerprint. `claude-agent.yml`'s writer checkout took `actions/checkout`'s defaults — `token: ${{ github.token }}`, `persist-credentials: true` — so the remote was left authenticated as the built-in token and **every `git push` the agent ran in a shell of its own went out as `github-actions[bot]`**, which is the normal path for an agent fixing its own pull request after a review. The file already knew the rule and had applied it to the wrong half: `:795-804` explains at length why `claude-code-action` is deliberately not handed `secrets.GITHUB_TOKEN`, so the *action's* push path was fixed and the *agent's* was never looked at. The other two checkouts in the same file set `persist-credentials: false`; the only one that writes did not. Now `token: ${{ secrets.ATTADIPA_AGENT_TOKEN || github.token }}` — written that way deliberately, because that secret is allowed to be empty and an unset secret is a falsy empty string, so the line changes nothing until the owner sets it and the two halves can land in either order. **Setting the secret was already necessary and was never sufficient**, which is why this sat unfixed while looking configured. **The rule is on `main`; the job that calls it and the `token:` line itself are NOT** — GitHub refuses to let a GitHub App update anything under `.github/workflows/` without the `workflows` permission, which `claude[bot]` does not hold, so the push is rejected outright and both wait as `docs/automation/pending/75-approval-stall.patch` (the same wall #74 hit). Until somebody applies it — three commands from a local session — this stall is still silent and the fix is still not in place. `.github/scripts/approval-stall-decision.sh` plus the watchdog's `approvals` job end the silence once it lands: a run that concluded `action_required` with zero jobs is commented on once **per head commit**, and one with jobs is called an environment gate instead, because that is a different cause and one message for both would be wrong for one of them. An unreadable job count or head is skipped for the tick rather than resolved in either direction — guessing *stalled* accuses every pull request the moment the API has a bad minute, hourly and forever, which is #82's shape. 51 cases in `.github/tests/approval-stall-decision-test.sh`, including both real runs with the values the API actually returned — and two that **lift the field split out of the patch file and run it**, because review found that split reading its records with `IFS=$'\t'`. A tab is IFS whitespace, so bash collapses adjacent tabs and a record whose `conclusion` is null arrives a field short: the head SHA lands in `CONCLUSION`, the workflow name in `RUN_SHA`, and the decision script rejects `CI` as a non-hexadecimal head and answers `quiet|unreadable`. That killed the `action_required`-as-a-**status** branch — the one that arrives with an empty conclusion, and the branch the guard argues hardest for — in the one direction with no symptom: after the shift field 3 is always a SHA, so no false positive was reachable, only the missed stall. It is now `join("|")` read with `IFS='|'`, the idiom already used twice here, and the two new cases extract the line from the patch with `sed` rather than copying it, so a copy cannot drift and a patch that stops containing a recognisable `while IFS=` goes red. Nothing could have caught it otherwise: the loop lives in `pending/`, which `ci.yml`'s `actionlint` and `shellcheck` globs do not reach, so it would have been applied by hand, unlinted. **It does not re-run anything** and the job deliberately holds no `actions: write`: whether a `GITHUB_TOKEN` re-run clears an approval requirement that a `GITHUB_TOKEN` push created is **UNKNOWN** — only a *person's* re-run was ever observed to — and a retry loop that does not clear it is an hourly bill for the same answer. The sources, the evidence and each option's cost are in [APPROVAL_STALLS.md](docs/automation/APPROVAL_STALLS.md); the one owner action fills `ATTADIPA_AGENT_TOKEN`, and **which credential fills it is now Option B — a second GitHub App — unless the PAT already exists**, because a fine-grained PAT costs a security condition and an App does not; taking A instead means landing **T-145** before the secret is used rather than after. If A is taken, the PAT is scoped to this repository, with *Contents*, *Pull requests* **and *Issues*** at read-write and **without** `Workflows` — the issue permission because that secret is what the action posts its report and sets its labels with, and an earlier version of this instruction listed only the first two. This sentence recommended the PAT flatly until the fifth review round of #128, in a cell whose next sentence already said an App was preferable. **And Option A has a price the same document had denied**: a fine-grained PAT belongs to a user, so the agent's own output carries `author_association: OWNER`, which `queue-scan.jq` accepts *before* the `claude`/`github-actions` login test is ever reached — the anti-recursion guard does not get bypassed, it becomes unreachable, and no login list can fix that. A separate GitHub App keeps a distinct bot login and is now preferable for that reason rather than only for attribution; either way the recursion bound has to stop depending on who wrote the issue — **T-145**. **Two further prices are recorded rather than dismissed**, both found in review and both filed instead of fixed inside a patch that is already blocked on one owner action: `actions/checkout` defaults `persist-credentials` to `true`, so with the PAT set a *long-lived* credential sits in `.git/config` as an `http.extraheader` in the one job where the model holds `Bash` — and it cannot simply be turned off, because the agent pushes with `git push` from that same shell, so removing the exposure means a different push path (**T-146**); and `claude-ci-repair.yml` becomes *reachable* the moment the agent's pushes actually run CI, so a red one calls a second billable writer at two attempts and `timeout-minutes: 60`, which the risk list had priced at nothing. Related and also filed: the watchdog's orphan sweep ages `agent:working` tasks by `updated_at` over `repos/$REPO/issues`, an endpoint that returns pull requests too, so anything commenting on a pull-request-shaped task every tick keeps it permanently out of the two-hour sweep — `merge-candidate.sh` documents that hazard for itself and uses head `committedDate` instead (**T-147**). **Round three found the patch did not apply at all.** All three hunk headers carried the line counts of an earlier draft — the first said `+289,155` over a body of 228 added lines — so `git apply` answered *corrupt patch at line 234* and stopped. Command one of the three-command landing procedure in [`pending/README.md`](docs/automation/pending/README.md) failed outright, which nothing could see: `pending/` is outside every `actionlint`, `shellcheck` and test glob in `ci.yml`, and that is the whole reason the directory exists. The headers are recomputed and `git apply --check` passes; the landing commit is now simulated end to end — apply, `git rm` the patch, run the suite — rather than reasoned about. **That simulation is what the same round's blocking finding was about.** The two cases that lift the field split out of the patch read a path that the landing commit deletes, in the same commit that first runs them, so `main` would have gone red on the owner's own commit saying *the delimiter regressed* about a patch applied correctly. They now read the patch **or** the landed `agent-queue-watchdog.yml`, one `sed` expression over both because a patch's `+` and a workflow's leading space are one character class; neither containing it is still a hard `FAIL`, so the guard is retired deliberately or not at all. Both directions were run: 51/51 after a simulated landing, `47 passed, 3 failed` with the workflow removed. Three more from the same round: the tick is capped at **three comments**, the bound `pr-merge-sweep.yml` already puts on itself — without it the first tick after landing comments on every open pull request with a stalled head at once, which is both the T-147 hazard at its worst and the thing the test file's own header warns about; the two calls that could silence the whole guard now announce their own failure, the sharper being `OUT=""` from the decision script, which yields a `DECISION` that is neither `say` nor `quiet|unreadable`, so the job walked every pull request, emitted nothing and exited green — no answer wearing the shape of no problem; and **two facts that had lived only in a pull-request comment are now in the file**: `POST /actions/runs/{id}/approve` answered **403 Resource not accessible by integration** for `claude[bot]`, so an agent cannot approve its own stalled run and no self-granted permission changes that, and `gh pr close` followed by `gh pr reopen` re-queued all three runs on the head — a second observed remedy, recorded because it works and explicitly not recommended as a loop. **Round four found the landing commit red again, in the documentation this time.** The patch inserts 43 lines into `claude-agent.yml` and 11 into `ci.yml`, and four fingerprinted citations sit below those points — so applying it moves three citations in `APPROVAL_STALLS.md` down by 43 lines and two in `WAVESHARE_ARRIVAL.md` down by 11 — deliberately written here without the `path:line` syntax, because a record of where a citation used to point is not itself a citation and writing it as one would make this paragraph four more numbers to keep in step. Before the fix `check_docs.py` failed the owner's own commit naming `docs/research/WAVESHARE_ARRIVAL.md`, a board bring-up document, for a change to a GitHub Actions workflow. Confirmed by running it rather than by arithmetic: apply, then `python3 tools/docs/check_docs.py .`, four problems and exit 1. The patch now carries those edits itself, plus the retirement of its own row in [`pending/README.md`](docs/automation/pending/README.md), so the three documented commands still produce one green commit; the landing is simulated end to end each round. **The same round exposed a defect the fingerprints were supposed to have closed.** `tools/docs/check_docs.py:466` "FINGERPRINT.match(line[match.end()" reads a fingerprint from the remainder of the citation's own physical line, so a citation whose quoted snippet **wraps** has no fingerprint as far as the checker is concerned and silently falls back to the blank-line test — protected in appearance only. Two were found in review; a third was found only by re-counting the file's citations with the checker's own `CITATION` and `FINGERPRINT` patterns instead of by eye, which also showed the file's prose claiming six where there were eight, one of them carrying a snippet with a `"` in it that `FINGERPRINT`'s `"([^"]{3,80})"` could never have matched. All three are reflowed and the count is right, but a defect found three times by hand in one file is a missing check rather than three mistakes — **T-153**, deliberately not folded into this pull request, which is about approval stalls and would otherwise collide with `check_docs.py` work already in flight. Also this round: the stalled bullet reported a hardcoded `conclusion: action_required` for the shape whose conclusion is **empty** — the exact record the whole `join("|")` rewrite exists to keep reachable, sending a maintainer to look for a field the run does not have — and now prints `$STATUS` and `$CONCLUSION` as read; the pull-request listing sorts `created` **ascending**, because the REST default is descending and the three-comment cap would then serve the three newest stalled heads every tick and never reach the one stalled longest, which is #75's own failure mode at a slower cadence; and the extraction test now reads the deployed workflow **before** the patch, so that in the window where both exist the retired copy cannot answer for the live one — mutation-proved in both directions, 51/51 with the old order and 50/51 with the new one when the deployed loop's `IFS` is broken. **Round five: the owner was handed one action priced at a quarter of its cost, in the file `CLAUDE.md` sends an agent to first.** `TASKS.md` still said *the cost being accepted is attribution* and *the alternative without the attribution cost is a separate GitHub App* — both sentences this branch had corrected in three other files. The list in `APPROVAL_STALLS.md` counted **three** costs while the same document argued a **fourth** at length under *What this does not cover*, and that fourth is not a trade-off but a condition: the anti-recursion guard becomes unreachable, so the guard's own document recommended **A** while establishing why **B** is preferable. All four artefacts now say the same thing — **B unless the PAT already exists, and A only with T-145 landed first** — and the cost list counts four. **And the repeat bound came off on the one path that needed it most.** The unreadable-job-count path withheld the marker and commented anyway, so the body's own closing line *This is said once per head commit* was false exactly there, and with the ascending sort and the three-comment cap three heads stuck in that state consume the whole tick budget every tick and a newly stalled head is never reported — #75's failure mode inside the guard written to end it. There are now **two** markers: a full one that finishes a head, and a `-partial` one that bounds the incomplete report to once while leaving the head eligible for a complete one, because the per-run repeat bound reads only the full marker. The choice is in the decision script with eight cases of its own rather than inline in a `run:` block. **And nothing asserted the two lines that actually fix #75.** Every case was about the *rule*; the `token:` on the writer checkout and the existence of the `approvals` job were unguarded, in a repository that has lost a workflow line silently twice. Three cases now read them out of whichever of the patch and the deployed workflow holds them — including that the secret is named **before** the `github.token` fallback, since `${{ A || B }}` takes A and the reversed order is a silent no-op of exactly #75's shape. Mutation-proved: dropping the line, reversing it, and renaming the job each redden one case and only that one. Two smaller ones with them: the input is now bounded as well as the output, at 40 pull requests a tick with the number dropped announced rather than truncated silently — the arithmetic is ~4 API calls per pull request against a 1,000/hour budget shared with `pr-merge-sweep.yml`, so ~120 an hour here and nowhere near it, and exhausting it would produce precisely the unreadable counts that silence the guard; and the partial-marker warning is emitted before the comment body starts rather than from inside the block on stderr, because whether the runner parses workflow commands off stderr is not established anywhere here. See [automation](docs/automation/CLAUDE_AUTOMATION.md) |

Having ESP-IDF v5.5.5 on disk is not the same as having chosen it (T-004) — and
that decision no longer blocks M1, because M1 is the simulator.

## Hardware tests pending

All of them. No board has been powered on by this project and no measurement
has been taken. Nothing here may be described as hardware-tested.

**There is now a fleet to test against, and it is out of reach from here.**
[TEST_FLEET](docs/research/TEST_FLEET.md) records what the owner has: two T114
nodes — one headless on Home Assistant, one with GNSS and a screen and free —
and a V4 companion running `dt267/MeshCore-Low-Power-Firmware`, physically on
USB to the owner's machine. Both advertise over BLE under the *same* name, so
anything selecting by name picks whichever answered first. The pairing PIN is
deliberately **not** in this repository: it is a device access credential and
this repository is public.

What is measured rather than assumed is the other half: a cloud agent session
has no `/dev/ttyUSB*`, no `/dev/ttyACM*`, no `usbip`, no `lsusb` and no
`esptool`. It cannot open a port, enumerate USB, or flash anything. So the
fleet's existence changes which plans are *specifiable*, not which are done —
and an agent reporting a hardware result from a session like this has not run
what it says it ran. Three things about those nodes are still unknown and each
has an issue: the band ([#89](https://github.com/hleserg/Attadipa/issues/89),
which gates every LoRa interoperability plan), the firmware revisions
([#90](https://github.com/hleserg/Attadipa/issues/90) — three across the fleet,
and the pinned `d929643` is none of them), and that neither T114 sees GNSS
indoors ([#91](https://github.com/hleserg/Attadipa/issues/91) — expected
physics, not a defect, written down so nobody files it as one).

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
needs a ruler.

| # | Conflict |
|---|---|
| D15 | **The T-Watch panel's physical diagonal.** LilyGoLib's spec tables say 1.3" for the S3 and the S3 Plus by name; the schematic's LCD sheet says `QT154C2408` / `LCD_1.54-TOUCH`, and that vendor's sibling part `QT154H2201` is published as 1.54", 240×240, ST7789V — so the part number decodes. 240 × 240 is not in doubt; 261 dpi against 220 is. The code holds 1.3" as the **conservative** reading, not the confident one ([HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md#display-diagonal--conflicting)) |
| H8 | The T-Watch vendor document calls ALDO1 unused; the schematic drives the `+3V3` rail from it. If the schematic is right, `+3V3` is switchable and carries five parts |
| ~~D12~~ → **D12b** | ~~PSRAM documented as quad; the `R8` marking is understood to mean octal~~ **Checked and split.** Table 1-1 of the ESP32-S3 datasheet has no 8 MB quad in-package part, so `R8` is octal. Closed for the Waveshare (D12a). Still open for the **T-Watch**, where a LilyGO document says QSPI and has not been read against that table |

The brand-art-versus-§42 palette conflict once recorded here as A7 is
resolved — [OWNER_DECISIONS.md](docs/research/OWNER_DECISIONS.md) OD-15.

## Assumptions in force

- The LilyGO PlatformIO pin to IDF 4.4.7 does not constrain Attadipa, which is
  ESP-IDF-native and does not use the Arduino layer. Flagged, not proven.
- Both boards' SoC is an ESP32-S3 — from both schematics (`ESP32-S3-R8`,
  `ESP32-S3R8`), but not from a chip readback.
- The radio capability facts in [ADR-0003](docs/adr/0003-radio-not-lora.md) come
  from RadioLib 7.7.1 and MeshCore `d929643` source, not from the TI and Silicon
  Labs datasheets, which refused automated retrieval. Recorded as **PARTIAL**,
  not VERIFIED.

## The bench session of 2026-08-23

The owner authorised flashing the unit
([#100](https://github.com/hleserg/Attadipa/issues/100)). In the end **nothing
needed to be flashed** — the bench sequence ran out of RAM, wrote nothing, and
the unit is byte-identical to the T-099 backup with `verify-flash` over all
33 554 432 bytes to say so. Full write-up:
[WAVESHARE_RUNNING_OUR_CODE](docs/research/WAVESHARE_RUNNING_OUR_CODE.md).

### Two routes tried; the second one works

- **`ota_1` can never boot.** It sits at exactly `0x1000000`, and the ROM and
  second-stage bootloader address flash with 24 bits, so the address **aliases to
  `0x0`** — the bootloader read its own image and said so. Waveshare ships a
  partition table containing an OTA slot their own bootloader cannot use. **For
  Attadipa: every app partition on this board lives below 16 MB**, unless somebody
  proves ESP-IDF's experimental `BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH` on it,
  which nobody has.
- **A `PURE_RAM_APP` runs fine — if the serial port is never closed.** Four
  earlier attempts reset within milliseconds and were written up as proof the
  board refuses RAM images. They were not: the kernel drops DTR and RTS on the
  *last* close of a `ttyACM`, those lines are GPIO0 and EN here, and `esptool`
  exiting was itself the reset. `rst:0x15 (USB_UART_CHIP_RESET)` says *the host
  did it* and should have been read that way the first time. Driving `load-ram`
  from a single process that never closes the port, the same driverless image
  that "failed" ran to the end of every watch window — up to two minutes, across
  five images. Nothing was watched for longer than that, so "runs indefinitely"
  would be an estimate wearing a measurement's clothes.

### What the probes then read off the board, without one flash write

- **The IMU is at `0x6B`, measured** — `0x6A` does not answer. The address
  conflict is resolved; the schematic and revisions 0.8/0.9/A were right.
- **H14 resolves, and it matters for OD-6.** The QMI8658 reports
  `REVISION_ID = 0x7C` — the value in `13-52-25 ∙ QMI8658A ∙ Rev A`, whose
  chapter 11 documents a hardware pedometer. The QMI8658C Rev 0.6 document, which
  has no pedometer and calls `CTRL8` *"Reserved: Not Used"*, gives `0x79`. **The
  schematic prints `QMI8658C` twice and it does not describe this part.**
  Corroborated by writing: `CTRL2`/`CTRL7`/`CTRL8` all read back exactly as
  written, and the accelerometer reported gravity at 1.03 g under Rev A's ±8 g
  scaling. What is left is T-112 — someone has to walk with it.
- **Touch is held in reset until GPIO 9 is pulsed.** `0x38` is absent from the
  bus scan; driving GPIO 9 high and holding it changes nothing; **a 10 ms low
  pulse brings it up**, reading chip ID `0x64`, firmware `0x02`, vendor `0x11`.
  A BSP that configures GPIO 9 as a high output at init would see an empty bus
  and report no error. T-113.
- **`0x0C`, `0x0D` and `0x1E` are free** for the magnetometer retrofit (T-109).
- **The AXP2101's rail registers are recorded raw** — `IC_TYPE = 0x4A`,
  `LDO_ON_OFF0 = 0xFF`, the DCDC and ALDO/BLDO voltage bytes — read from the
  powered board without writing anything. That is the input D13 and H8 were
  waiting for; decoding it into a rail map still needs the datasheet beside it.

### And the vendor's own boot log, captured from 62 ms

Which took resetting over the CDC control lines, because the ordinary route
reconnects at ~580 ms and misses the bootloader's decision entirely. It settled
four more things at no cost:

- **D12a is now confirmed on silicon.** The `octal_psram` driver enumerates the
  part: `vendor id 0x0d (AP)`, `density 0x03 (64 Mbit)`, `VCC 0x01 (3V)`,
  `Readlatency 0x02 (10 cycles@Fixed)`, `Found 8MB PSRAM device`, `Speed: 80MHz`.
  A quad part would not have loaded that driver. This is step 4 of
  `WAVESHARE_ARRIVAL` §5, executed — and the latency and burst figures are the
  real numbers to redo §3.3's bandwidth arithmetic against.
- **D14 closes: the SD card is SDMMC.** The vendor's firmware calls
  `sdmmc_common`/`vfs_fat_sdmmc`, not `sdspi`. The schematic's `MOSI`/`SCK`/`MISO`
  net names are labels, not a mode.
- **`esp_lcd_sh8601` initialises this panel** — `LCD panel create success,
  version: 1.0.2`, then `Backlight on`. That is evidence about the driver, not
  about the die, so the CO5300 row stands; what it settles is that the
  documented mismatch will not bite at bring-up.
- **Flash boots QIO at 80 MHz**, `detected chip: gd`, 32 MB; `chip revision
  v0.2`; `efuse block revision v1.4`; `QMI8658 initialized successfully` — which
  names no address; the bus scan above settles `0x6B` by measurement instead.

## Recently completed

- **A degree of longitude at 89.9°N measured 1.96 km instead of 194 m, and it
  got worse the closer you stood to the pole.**
  [#28](https://github.com/hleserg/Attadipa/issues/28). `lon_e7_to_mm`
  (`core/src/geo.cpp`) took the mean latitude, divided it by 1e7 to get a whole
  degree, and used that as the index into `kCosTable1024` — so the fractional
  part was discarded and every latitude in [89.0°, 90.0°) was scaled by
  `cos 89°`. The overstatement is unbounded rather than merely large: inside
  that last degree the true longitude scale falls from 0.01745 to zero while a
  step function holds 0.01745, which is ten times over at 89.9° and a thousand
  times over at 89.999°. **Below 89° the same defect is not sub-percent, and an
  earlier version of this paragraph said it was — wrong by 100× at the very
  boundary it named.** The worst error inside `[d, d+1)` sits at the top of the
  interval and reads straight off the table: **+1.0%** at 29–30°, **+2.1%** at
  50–51°, **+5.0%** at 70–71°, **+14.6%** at 82–83°, **+24.6%** at 85–86° and
  **+101%** at 88–89° — so at 88.999°, one thousandth of a degree below the
  stated boundary, the distance was already doubled. At **82.5°N — Alert,
  Nunavut, the northernmost permanently inhabited place on earth** — the
  overstatement was **+7.0%**, and this fix cuts the reported distance there by
  6.3%. *Sub-percent* is true only below about **29°**. The movement is toward
  truth, so nothing on a device is worse for it; what the wrong sentence cost
  was the next person tuning `jump_while_still_mm` or auditing Arctic
  behaviour, who would have read the blast radius as the last degree when it
  was every latitude above ~29°. Found in review.
  **What it would have cost:** ADR-0011 §6 makes implied speed and motion
  disagreement evidence against a fix, and `TrustEvaluator::observe` computes
  the first by dividing exactly this distance by an interval. A stationary
  device at a high latitude, whose longitude wanders by the metre as any
  receiver's does, would have produced kilometre-scale movement against an
  accelerometer saying the wrist never moved — `PositionJump` and degraded
  trust, for being at a high latitude, which is not a fault. **The fix** is
  linear interpolation between the two bracketing table entries at the full
  1e-7-degree resolution: integers throughout, no new table, no floating point
  in the hot path, `kCosTable1024` and the `distance_mm()` contract untouched,
  and every product bounded in the source — the widest is 2.0e10 × 2^24 ≈
  3.4e17, a factor of 27 below the top of the type. Two details are load-bearing
  and both are commented: the interpolation is written as a weighted mean rather
  than the algebraically identical `lower + (upper − lower) · frac`, because the
  latter subtracts two nearly equal numbers in the last degree and loses three
  digits of a four-digit value; and the cosine is carried at 2^24 rather than the
  table's 1024, because at 1024 the interpolated factor inside the last degree
  rounds to *zero* — the same defect pointed the other way, and the direction
  that silently passes a `distance > threshold` test.
  **The envelope is measured, not claimed.** `tests/test_position.cpp` now
  re-derives it on every run against an independent haversine reference — a
  different method, no cosine table, no mean-latitude reduction, no
  antimeridian case, because `sin(dlon/2)` is periodic and wraps for free.
  Within **0.9%** from the equator to 89.999°, worst case 0.76%, flat rather
  than growing towards the pole; inside the last 111 m the fixed-point quantum
  takes over and the relative error eventually reaches 100%, where a whole
  degree of longitude is a third of a millimetre — so what is bounded there is
  the absolute error, under 20 mm, and the test says so instead of widening a
  tolerance. The residual 0.7% is *not* the interpolation: it is
  `kCosTable1024[89] = 18` against a true 17.871, and it is smaller than the
  equirectangular approximation's own error at the same latitude, so a finer
  table would buy nothing that survives the method it feeds.
  Regression matrix covering 0°/45°/80°/89°/89.5°/89.9°/89.99°/89.999° in both
  hemispheres, a swept envelope, the antimeridian at 89.9°N as well as at the
  equator, the grid corners, coincident points, symmetry and saturation. **10 017 assertions fail
  against the pre-fix code — but that is a count of failing assertions, not of
  discriminating checks, and an earlier version of this sentence said "every new
  check" when twenty-one new check sites are green against it.** Two whole new
  test functions are among them: all eleven `CHECK`s of
  `test_the_grid_boundaries_are_answers`, none of which reaches the cosine index
  at all (zero deltas, a wrap to zero, pole-to-pole saturation, and coordinates
  `in_range` rejects before any arithmetic), and all three invariants of
  `test_the_longitude_scale_is_monotonic_symmetric_and_bounded`, which hold for
  a step function as readily as for an interpolated one. They are worth keeping
  — a boundary that is an answer rather than a crash is a property — but they
  are not evidence that this defect existed. Meanwhile every **pre-existing**
  distance test passed against the pre-fix code, which is exactly why the suite
  could not see this: they only ever asked whole degrees. Found in review. Host, strict
  warnings (`-Werror -Wconversion -Wsign-conversion -Wold-style-cast`), Clang,
  ASan+UBSan with `-fno-sanitize-recover=all`, and the simulator build all
  clean. **No hardware involved and none needed** — the defect and its fix are
  entirely host-reproducible arithmetic.

- **T-009's invariant was a property of the formatting, not of the code.**
  [#68](https://github.com/hleserg/Attadipa/issues/68).
  `tools/ui/check_raw_values.py` read one physical line at a time, so the same
  call got two different verdicts depending on where the line breaks fell:
  `lv_obj_set_style_pad_all(obj, 12, 0)` was refused, and the identical call
  wrapped across four lines — which is what `clang-format` produces at a narrow
  enough column — returned `1 file(s) clean`. Nobody had to mean it; ordinary
  formatting was enough to walk a raw pixel count past a green
  `ui_no_raw_values`. Reproduced on `359b02f`, 101 commits after the commit the
  finding was made against, so the staleness check said implement rather than
  close. The same defect had a second face: the setter list was hand-written, so
  `lv_obj_set_size(obj, 10, 20)` and `lv_obj_set_pos(obj, 10, 20)` went through
  in **any** formatting, having never been on it. Both are one mistake — matching
  surface syntax instead of the call — and the fix is three passes: blank the
  comment and string bodies keeping every offset, walk each known LVGL entry
  point forward balancing parentheses to the end of its argument list, and judge
  arguments **by position** against the signature. A bounded tokenizer, and
  deliberately not a C++ front end; the parsers examined and why none of them is
  in the primary CI gate are in
  [REUSE_LEDGER](docs/research/REUSE_LEDGER.md). The inventory of which entry
  points take a length, a duration or a colour is now read out of the pinned
  LVGL v9.5.0 headers rather than remembered — which is how the third hole
  surfaced: `lv_anim_set_time` is a **v8 compatibility macro** in v9, and the
  old list knew only the compatibility spellings, so `lv_anim_set_duration`, the
  name a v9 screen would actually reach for, was the one nobody checked.
  Thirty negative fixtures and twenty-eight positive ones, most of them the
  same call written twice — one line and wrapped — because the two disagreeing
  was the whole defect; the previous suite was single-line throughout and would
  never have seen it. Run against the pre-fix checker, the new suite finds
  **twenty negatives it let through and one positive it wrongly refused** — a
  colour inside a string literal, which the line-at-a-time reader had no way to
  tell from code — so the fixtures are known to bite rather than assumed to.
  Three mutants (blanking off, the colour rule loosened, the zero exemption
  dropped) turn the suite red. The positives are
  the other half of the contract: a repeat count, a rotation in tenths of a
  degree, a gradient stop, a flex weight and an array index are numbers in UI
  code that are **not** pixel counts, and they are tests rather than a comment
  saying so.

- **The unattended merge sweep failed before it looked at a single pull
  request.** `pr-merge-sweep.yml` merged green on 2026-08-23 and its cron had
  not yet registered, so it was dispatched by hand to get the evidence its own
  merge commit recorded as `NOT EXECUTED`. It exited 1 in eleven seconds:
  `gh` refuses `--slurp` together with `--jq` — *"the `--slurp` option is not
  supported with `--jq` or `--template`"* — and returns before making a request.
  The workflow carried the combination three times. The first is the fatal one;
  the other two are worse, because both are `|| CODEX_*=""` and the rejection
  therefore landed as *"could not read its comments, leaving it alone"* on every
  candidate — a sweep that decides nothing and reads as a sweep with nothing to
  decide, 48 times a day. Nothing in the repository could have caught it:
  shellcheck saw a well-formed command, actionlint saw valid YAML, and the
  workflow only runs on a schedule. Fixed by piping into a separate `jq`, which
  is what `agent-queue-watchdog.yml` already did and is why the watchdog was
  unaffected, plus `.github/tests/gh-api-usage-test.sh` — a scan over every
  workflow, with seven fixtures proving the scan itself catches the shape and
  leaves the recommended one alone, verified to flag all three lines on the tree
  as it was. The general lesson is the one that produced this: **dispatch a new
  scheduled workflow once by hand instead of waiting for its cron**, because
  reading it had already passed it.

- **The hourly watchdog had never started an agent, and nothing said so.**
  T-107. `agent-queue-watchdog.yml` dispatches `claude-agent.yml` with the
  built-in `GITHUB_TOKEN`, so the actor is `github-actions[bot]`;
  `claude-agent.yml` passed `allowed_bots: ""`, and
  `anthropics/claude-code-action` refuses a non-User actor that is not on that
  list. The run died in about five seconds without writing an execution log, so
  the hand-over reported `no conclusion` and four issues — #27, #28, #67, #69 —
  were recorded as unexplained model deaths. Each half is defensible alone: the
  watchdog should dispatch with the built-in token, and an empty `allowed_bots`
  correctly avoids `'*'`, which on a public repository would let any installed
  GitHub App drive a write-capable agent. Only the **pair** is wrong, and
  neither file's own review can see the other, so the fix is registered as a
  test rather than a comment —
  `.github/tests/bot-actor-test.sh`, 19 assertions, run in CI,
  reimplementing the action's `isAllowedBot` normalisation, asserting the
  watchdog still dispatches with `github.token`, refusing `'*'` in the other
  direction, and asserting `queue-scan.jq` still keeps `claude` and
  `github-actions` un-listable as *producers* — a dispatcher grant is not a
  producer grant. Verified to fail against the pre-fix tree.
  `allowed_bots: "github-actions"`. The 500 KB mandated reading order was the
  leading theory and was not the cause; it is now T-110 on its own merits.

  **And looking for the same shape found it again, in the reviewer.**
  `claude-pr-review.yml` admits `claude[bot]` in its `if:` on purpose and then
  passed the action an empty `allowed_bots`, so **no agent-authored pull
  request had ever been reviewed** — four runs, four and five seconds each,
  identical refusal, and all four **green**, because that step carries
  `continue-on-error`. `allowed_bots: "claude"`. The test now asserts the rule
  rather than the instance: any workflow admitting a bot actor must name it,
  none may name `'*'`, checked over all three agent workflows.

- **The movement and altitude baselines measured arrival time, not measurement
  time — and accepted a `NoFix` sample's retained coordinate as a new one.**
  [#26](https://github.com/hleserg/Attadipa/issues/26), the T-062 finding
  restated in TASKS.md: `TrustEvaluator::observe`
  (`core/src/trust.cpp`) stamped `previous_position_at_` and
  `previous_altitude_at_` from `now` rather than
  `observation.observed_at`, and advanced either baseline for any observation
  carrying a coordinate regardless of `PositionValidity`. On the board's own
  receiver the two timestamps coincide, so this never showed there; a fix
  relayed by an Attadipa node over a link that queues and retries can be
  measured ten seconds apart and arrive one millisecond apart, reading an
  ordinary walk as a `PositionJump`, and a receiver that reports `NoFix` while
  still holding the last coordinate on the wire pulled the baseline timestamp
  forward without the wearer moving, so the next real fix was divided by a
  far shorter interval than actually passed. Both baselines now advance only
  on a `Valid` or `Degraded` observation whose `observed_at` is not older
  than what is already stored; an equal timestamp is safe (elapsed time
  reads as zero, no division), and an out-of-order one is evaluated for its
  own trust reasons but not adopted as the new baseline, so it cannot corrupt
  the interval the next legitimate sample is measured against. Six regression
  tests added to `tests/test_trust.cpp`; reverting the fix turns all six red.
  Host suite clean under GCC and Clang with `-Werror -Wshadow -Wconversion
  -Wsign-conversion -Wold-style-cast` and under ASan+UBSan with
  `-fno-sanitize-recover=all`. Simulator build `NOT EXECUTED` in this
  environment — no SDL2 dev package and no permission to install one — but
  the change does not touch UI or simulator code.
  **[PR #71](https://github.com/hleserg/Attadipa/pull/71) review found the fix
  itself opened a new hole: nothing bounded `observed_at` against `now`, so a
  single observation dated arbitrarily far in the *future* was `in_order` (future
  is never less than past), became the baseline unchallenged, implied a
  near-zero speed on arrival (the interval is enormous), and then froze the
  baseline forever — every genuine sample afterward was "older" than the
  poisoned one and never re-armed `PositionJump`/`ImplausibleAltitudeRate`.
  Closed by bounding `observed_at` against `now` with a 50 ms forward-skew
  tolerance (`TrustPolicy::observed_at_forward_skew`, `core/include/attadipa/
  core/trust.h`) — not zero, because the two are not read atomically even for
  a purely local fix, but small enough (2 750 mm of maskable displacement at
  the 55 000 mm/s implausible-speed ceiling) to matter to nothing else in the
  policy. A future-dated sample is rejected the same way an out-of-order one
  already was — evaluated for its own trust reasons, never adopted as the
  baseline — and now also raises `ClockDisagreement`, reusing the existing
  reason rather than adding a second one for the same condition. Regression
  test `test_a_future_dated_observation_is_rejected_without_freezing_the_
  baseline` walks the full poisoning sequence the review named: a genuine
  baseline, the future-dated sample, a real jump measured against the
  *untouched* original baseline (would be missed if the baseline had frozen),
  and one more ordinary fix afterward (proves the detector keeps working, not
  just survives one more call). Confirmed red against the pre-fix code.
  GCC+Clang, `-Werror` strict-warnings and ASan+UBSan all clean.
  **The second review found that fix had broken something else in the same
  place, and worse than the defect: the code did not do what its own comments
  said.** `jumped`, `moved_at_rest` and `climbed_absurd` were computed *inside*
  `if (usable_for_rate && in_order)` — the same condition that gates the
  baseline write — so an out-of-order or future-dated sample was neither adopted
  **nor checked**, while both the header comment and the pull request promised
  it was "still evaluated for its own trust reasons, never adopted". Only the
  second half existed. That made the refusal worse than useless against the case
  it exists for: a sample could report the wrist ~500 km from a wrist the
  accelerometer says never moved and raise nothing, while `remember()` still
  stored it as `last_trusted_position()`. **Detecting and adopting are now two
  decisions**: detect against whatever baseline stands, adopt only in order. A
  backward interval needs no guard — `elapsed()` saturates it to zero and the
  existing `dt.value > 0` test skips it. Three regression tests, all three
  confirmed red against the pre-fix code, and all three use
  `MotionEvidence{true, false}` — **every earlier test passed
  `MotionEvidence{}`, motion unknown, which can never raise
  `MotionDisagreement` whatever the code does, which is why the suite could not
  see this.** Each also proves the baseline was still not adopted, so the freeze
  fix survives the detector fix. **A limit stated rather than tested around:**
  altitude has no interval-free detector, so every altitude check is a rate and
  a sample dated far enough ahead defeats it arithmetically — the claimed
  interval grows with the lie. What the split recovers there is the sample dated
  *slightly* ahead, past the 50 ms tolerance but not past plausibility. Host
  suite 24/24; GCC `-Werror` strict-warnings with ASan+UBSan clean; Clang
  strict-warnings clean, **its sanitizer runtime is not installed in this
  environment so that half is `NOT EXECUTED` here** and runs in CI.

- **A4 is closed, not answered.** [#55](https://github.com/hleserg/Attadipa/issues/55)
  asked which regulatory region governs the radio, concretely — the owner's own
  MeshCore node already transmits 158 mW at 868.731 MHz and its legality here
  was never established. The owner declined to name a region: *"Законность моя
  проблема а не прошивки"* — legality is his problem, not the firmware's
  ([OD-14](docs/research/OWNER_DECISIONS.md#od-14--which-region-is-the-owners-problem-not-the-firmwares)).
  Nothing in [ADR-0006](docs/adr/0006-settings-and-bounded-values.md) changes:
  the design never required this project to know which region applies, only
  that an operator has chosen one, and the transmit-closed-while-`Unknown` gate
  — called "Attadipa's single most safety-critical line" in the reuse ledger —
  stays exactly as designed, for any operator of any build. What closes is the
  expectation that this project would research and ship a specific
  jurisdiction's rule table; there is no subject left to research one for.

- **T-102 — documentation consistency in CI, and the defect its own pull request
  shipped.** `tools/docs/check_docs.py`, run by the `Documentation consistency`
  job. **Seven** checks: relative links resolve **and their `#anchor` lands on a
  real heading**, inline code spans close, task
  IDs are unique, **one OD number names one owner decision**, a live task has a
  body while finished work is filed under `## DONE`, nothing unexpected is
  tracked at the repository root, and **a `file:line` citation lands where it
  says it does** — meaning it is inside the file, on a line that is not blank,
  over a range that reads first-to-last. A citation that lands on a real but
  *wrong* line is invisible to that, which is what happened to two citations
  into `HARDWARE_MATRIX.md`: both were thirteen lines out, both pointed at a
  real row, and nothing went red for weeks. So a citation may now carry a
  **fingerprint** — `EXAMPLE.md:357 "Display FPC"` — and where one is
  written the check reads the cited line and reports where the text actually
  went. That example uses `EXAMPLE.md` deliberately: a fingerprint is an
  assertion, so an illustration written with a real path is one too, and this
  paragraph would otherwise be asserting a line number in a document it is not
  about. `EXAMPLE.md` is the reserved spelling that resolves to nothing, and
  the check reports the day a file by that name appears. It is opt-in: adding a fingerprint is a promise the check then keeps,
  and the citations this repository has been burned by are the ones to add it
  to first. The count said Six on the commit that added the seventh, so
  it is no longer typed: `check_docs.py` holds its checks as a list, the suite
  holds this sentence to `len(CHECKS)`, and the same guard holds the case count
  in this file, in `TASKS.md` and in the CI comment. The decision check was added 2026-08-23 for a
  failure that was in flight rather than shipped: four open pull requests each
  inserted `## OD-16` into `OWNER_DECISIONS.md` at the same line, for four
  different decisions. Git conflicts on that, and a person resolves it — which is
  the problem, not the safeguard. *Keep both* is the obvious resolution and is
  right for the prose; it leaves the register people read to find out what the
  owner decided carrying two OD-16 headings with two ambiguous anchors, CI green.
  A first version of this entry said the branches shared no file and merged
  silently; review refuted it with one counterexample, and the corrected model is
  worse rather than better — a conflict a human resolves is a decision point
  nothing checks. The task-ID check is TASKS.md-only and could
  not see it. The other half — that a link's `#anchor` is captured and then
  never verified, so a citation survives its heading being renumbered — is
  **T-127**, deliberately not folded in here because its first run will find
  pre-existing breakage. The two body checks exist because the review of
  [#65](https://github.com/hleserg/Attadipa/pull/65) found the pull request had
  spliced a `### T-102` heading into the middle of an unclosed code span in
  T-100's first bullet — T-100 lost its whole field list to T-102, and **the two
  checks the pull request added both passed on that file**, so a green job read
  as *TASKS.md is fine* while the roadmap carried a task nobody could pick up.
  The span check catches that at its cause; the body check catches it at its
  effect, and catches the effect however it got there. Once the body check
  existed it found four records left in live sections marked `DONE` — T-034,
  T-060, T-060a, T-084 — drift predating the splice by weeks and the same defect
  the #48 review established for T-064 and T-073; all four are now under
  `## DONE`. Under `## BLOCKED` the body is the `BLOCKED:` block CLAUDE.md
  specifies rather than a priority, so T-010 and T-011 are correct and not
  flagged. **67 mutation tests**, several of which assert it does *not* fire where firing would be wrong — a `###`
  sub-heading is not a second decision, a range straddling a blank line is
  how a table is cited, and a line number in somebody else's tree is not
  ours to verify. The
  suite prints its own count, because the three documents quoting it were all
  stale within a day of the last time cases were added.

  **A re-review found the count load-bearing and the coverage behind it
  missing**: `check_root_files` had no case at all, so six of seven checks were
  covered while three documents quoted the number as the evidence that a
  checker which passes everything is worse than none. The suite now wraps every
  function in `CHECKS` and reports any that no case calls — coverage recorded
  rather than annotated, because an annotation is a second thing to keep in
  step. Four cases for that check followed. The same round found the mechanism
  reddening CI for true sentences twice over: the citation separator accepted
  whitespace, so a sentence of the shape *"`EXAMPLE.md:843` - 26 lines below"*
  parsed as a descending range — and the sentence that actually did it named
  this file, from inside this file, which is the same defect one paragraph
  further on — and a fingerprint written to **illustrate** the syntax was a live
  assertion about a third document — the two files CLAUDE.md tells the next
  agent to read first were both making one. The separator is closed up now, and
  `EXAMPLE.md` is the reserved placeholder an illustration is written with; a
  file by that name appearing is itself reported, because it would turn every
  illustration in the repository live.

  **A third round found the checker's own reach short of what three documents
  claimed for it**, in two places that had never matched anything. A citation
  into a dot-directory — `.github/workflows/ci.yml:281` — matched at no position
  at all: the path pattern admitted `./` and `../` and nothing else, and the
  word-boundary look-behind is there precisely to refuse a start inside a path.
  And a bare basename resolved only beside the citing document or at the
  repository root, so a citation from `docs/research/` into
  `docs/architecture/ARCHITECTURE.md` was skipped as somebody else's tree —
  one of them pointing at a `HardwareFeature` enum member while claiming to cite
  the section about `has()`. Both are fixed, and the immediate effect is the
  count in T-140: **twenty-three bare citations became forty-nine**, because the
  scan that produced twenty-three had been written to imitate the checker and
  imitated its blind spots too. It now imports the checker instead. The same
  round closed **T-127**, the anchor half of check 1: a link's `#anchor` had
  always been captured and never looked at, which is what lets an OD-number
  collision survive a merge with CI green. The first run over the whole
  repository found exactly one broken anchor — and one illustration. The record
  closing T-127 wrote the link syntax in backticks and the new check read it as
  a link, which is the `EXAMPLE.md` defect one check over, so `check_links` now
  blanks inline code spans first: GitHub renders a code span as characters, not
  as a link.

- **T-070 research — the watch as a tracker detector, and the honest limit is
  now sourced rather than deferred.**
  [`TRACKER_DETECTION.md`](docs/research/TRACKER_DETECTION.md), for
  [#45](https://github.com/hleserg/Attadipa/issues/45). The owner declined tag
  emulation and asked for the opposite feature instead: scanning for an
  unknown BLE identifier that has followed the wearer too long, inspired by
  `seemoo-lab/AirGuard` (Apache-2.0, reuse-ledger record added). Read from
  AirGuard's own source rather than its description: its thresholds (3
  sightings/14 days, ≥2–4 distinct locations 150 m apart, altitude gates), and
  its own in-product admission that a rotated identifier can still follow the
  wearer undetected. That admission turns out to be current, not dated: two
  independent 2025/2026 studies — PoPETs 2025, peer-reviewed, and a February
  2026 preprint — report that an identifier rotated faster than a detector's
  correlation window evades or substantially delays Apple's, Google's **and
  AirGuard's** detection on every ecosystem but Samsung's, and both used an
  ESP32 to demonstrate it. DULT's accessory-protocol draft is still expired
  and unreplaced, but its editors' working copy is active into August 2026 and
  assigns `Watch` its own accessory-category value — a direct hook for T-069,
  which this research hands off to rather than re-derives. Espressif publishes
  no BLE-scanning current figure at all; the nearest documented proxy is a 93
  mA RX peak, and the feature's power story stays incomplete until T-068
  answers whether either board can reach a 32 kHz sleep floor between scan
  bursts. One correction along the way: the issue that requested this research
  described the T-Watch as sharing an RF front end between BLE and LoRa; ADR-0003
  makes no such claim, and the document says so rather than repeating it.
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
  `.github/scripts/queue-scan.jq` so it can be executed, and 37 tests cover it in
  CI. There was no test before, which is why review caught it and CI did not.
- **The queue's own hand-over had four more of the same shape, and the review
  that found them is the first one this repository has ever actually run.** All
  four are a sentence that a mechanism does not honour, or a premise that holds
  for issues and not for pull requests — [#82](https://github.com/hleserg/Attadipa/issues/82)'s
  shape, one file over from where it was fixed. (1) The failure comment's only
  budget-reset route was *"add `agent:ready` yourself"*, and by the time anybody
  reads it the hand-over has already added that label: adding one that is
  present raises no `labeled` event, so `failure-count.jq` records no reset and
  an owner who fixed the real cause is escalated carrying every pre-fix failure.
  It now says **remove it and add it back**, which is the only version that
  raises the event. (2) The claim step's argument for stripping `agent:blocked`
  — *"there is exactly one way an issue carrying it can reach this step"* — is
  true of issues and false of pull requests: three of five triggers fire on one,
  and `claude-ci-repair.yml` writes `ci:failed` + `agent:blocked` on a **pull
  request** when repair gives up. Stripping it there clears a human escalation
  and hands the branch to the unattended backstop, which requires `agent:blocked`
  absent and never reads `ci:failed`. The strip is now issue-only and the
  hand-over reads a **recorded** `ATTADIPA_BLOCKED_BEFORE` instead of inferring
  ownership from the label's presence — an unreadable read reports rather than
  falls silent, because silence is the defect. (3) `gh pr ready` was pointed at
  whichever open pull request declares `Fixes #N`, which an abandoned branch
  also declares; a draft can never read `mergeable_state: clean`, so undrafting
  is precisely what makes a branch backstop-eligible. It now goes through
  `.github/scripts/promote-decision.sh`, which promotes only on evidence this
  run owns the branch — created during the run, or its head committed during it
  — and holds on anything it cannot read. (4) The assertion guarding that step
  **could not fail**: `grep -A3 'gh pr ready'` saw the three lines after the
  command while the `case` selecting the kinds was above it. It is anchored on
  the case block now, and each of these four is asserted in the direction that
  reproduces the defect. Also: `gh issue edit` resolves `repository.issue(number:)`,
  which does not resolve pull requests — the field that once put a GraphQL error
  document inside an outcome comment — so every label edit now goes through
  `.github/scripts/gh-label.sh`, and the stranded sweep applies `queue-scan.jq`'s
  author filter so it stops re-queueing issues the scan will never pick. Two new
  suites and three widened ones — `promote-decision-test.sh` 29,
  `blocked-restart-test.sh` 21, `stranded-failures-test.sh` 18,
  `gh-label-test.sh` 10 against a stub `gh` on `PATH`, `agent-say-test.sh` 61 —
  and every one of the four findings above has an assertion proven to fail
  against the code as it was.
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
