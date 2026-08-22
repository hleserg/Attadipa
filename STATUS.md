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
  or the bootloader refuses it. Which errata apply to v0.2 is **D18**, unread.
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

- **28 of 32 MB is partitioned**, with a 9 MB `factory` image and two 6 MB OTA
  slots — so the vendor's own update path can never restore the build that
  shipped, which is one more reason T-099 is P0.
- **The stock firmware is `xiaozhi-esp32`.** The `model` partition holds WakeNet9
  `wn9_nihaoxiaozhi_tts`, so the launcher's AIChats app is that project — which
  means this board's audio path is already written down by somebody who had it
  working. Licence first: **T-104**.
- **The vendor bakes raw pixel buffers**, not encoded images, with no decoder on
  the device. Corroboration for where T-034 was already heading; **T-103** turns
  the file sizes into the confirmation.
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

**A magnetometer is coming, and it makes several things here wrong at once.** The
owner has ordered one and intends to solder it onto the received Waveshare. Until
now *"neither board has a magnetometer"* has been a premise in this repository —
it is in [ADR-0009](docs/adr/0009-heading.md), in T-011's blocker, in the hardware
matrix and in the interference matrix — and a retrofit invalidates all of them
together rather than one at a time. Research is under way, tracked as **#83**. Three things about it matter before the part arrives, because two cannot
be fixed afterwards:

- **The bus is ready and the wiring is trivial.** `IO15`/`IO14` are the main I2C
  bus and both are on the expansion pad row beside `3V3` and `GND`. Address
  `0x6A` is free; the six fitted devices are `0x18`, `0x34`, `0x38`, `0x40`,
  `0x51`, `0x6B`. Four wires.
- **Placement is the hard part, not the part number.** There is a speaker with a
  permanent magnet in the back cover, bare motor pads, and a battery lead that
  will carry 150–200 mA of charge current. Earth's field is 25–65 µT and all
  three of those can dwarf it. A magnetometer soldered in the wrong place reads
  the watch rather than the world.
- **It is a third kind of capability source, and the model has two.** Not a
  property of the board type, because other units lack it; not supplied by a
  detachable node, because it does not walk away. That is an ADR question and it
  is being asked as one rather than settled in a research note.

**A bigger battery is under consideration and is now PAUSED behind it** — calipers
are on order, and whatever the sensor occupies comes out of the same cavity, so
the cell is chosen after it and not before. The owner did establish by hand that
**there is room under the cover and the fitted cell butts against nothing in any
axis**: `OBSERVED`, not `MEASURED`. It moves the odds without sizing anything —
"not touching" and "has 1.1 mm of clearance" are different statements. **And the
fitted one is probably not 400 mAh.** The cell turns out to be on a removable 2-pin plug rather than
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
need. **T-106** holds all three, and the register reads that go with them.

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

- **T-061 the pedometer** — partly, and less than before. T-060a settled the
  BMA423 side: the power story is **13–14 µA at 50 Hz in low-power mode**, the
  counter runs while the host sleeps, and the wrist preset is already the
  default. What remains blocked is the **Waveshare** side — the board's IMU
  variant is unknown, the QMI8658**C** documents a pedometer and the QMI8658A's
  **current** datasheet revision has deleted one — and one board question that
  is nobody's datasheet, and which is **already filed as [H8](docs/research/OPEN_QUESTIONS.md)**
  rather than new: whether the AXP2101 keeps the IMU's rail up across an
  SoC sleep. If it does not, the 6 kB blob is gone and the 150 ms is owed again
  on every wake. **Both are now bench questions rather than reading questions** —
  the Waveshare is on the desk, so `WHO_AM_I` settles the variant and a rail
  measurement across a sleep settles H8. Neither has been done.
- **T-010 board bring-up** — **half unblocked as of 2026-08-22.** A physical
  Waveshare `ESP32-S3-Touch-AMOLED-2.06` is on the desk; a T-Watch is not, and
  the T-Watch's variant question (which of five radios, which of two GNSS
  modules) is exactly what nobody can answer without one. The Waveshare half is
  no longer blocked and is no longer being *done* either — see the section
  above; **nothing in this repository may say `PASS` until somebody runs a test
  on the board and writes down what came out.**
- **T-011 interference measurement** — still blocked, and note that arriving
  hardware does not help: neither board has a magnetometer, so the headline
  haptics-versus-compass concern is not measurable on either of them in any
  configuration. On the Waveshare it is doubly not measurable — that unit has no
  vibration motor fitted at all.

## Waiting on the owner

| | Question | Why it matters |
|---|---|---|
| A1 | Is either board physically available, and which revision? | everything hardware |
| A2 | If a T-Watch: which radio chip and which GNSS module? | decides whether the watch can join a MeshCore network at all — two of the five candidate radios cannot ([ADR-0003](docs/adr/0003-radio-not-lora.md)) |
| A3 | Is there a second radio device, so mesh can be tested? | mesh test plan |
| A5 | Is an external magnetometer intended at all? | decides whether five magnetometer epics are dormant or dead |
| A6 | Does the Attadipa node carry a magnetometer? | decides what "compass" can mean — and even if the answer is yes, node orientation is **not** watch orientation ([ADR-0009](docs/adr/0009-heading.md) §3) |
| D16 | **Inter or Nunito Sans, and where do the arrows come from?** | the numbers exist ([FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md)); the choice does not. Nunito Sans has no U+2190–U+2193, so picking it also picks "arrows are icons". Blocks freezing the design tokens, not M1 |

None of these blocks M1. All of them block hardware work.

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
| Host / native | builds; **twenty-one tests** pass, locally and in CI on `main` since #12 merged — smoke, capability registry, both halves of the layer-boundary check, localization, and the six suites this milestone added: trust, transport, power, position, diagnostics, and the replay rig with its fifteen traces, plus the
design-token suite and the two checks that keep raw colours and pixel counts out
of screen code. Under GCC and Clang, under `-Werror` with `-Wshadow -Wconversion -Wsign-conversion -Wold-style-cast`, and under ASan+UBSan with `-fno-sanitize-recover=all`. The negative half of the boundary check is verified against two deliberate breakages: a fixture that fails for the *wrong* reason is a failure, not a pass |
| Simulator | **builds and runs**, on the development host and **in CI from nothing** — run `32462413273`, cold cache, no LVGL on the machine: clone 22.8 s, commit verified against the pin, build, 6/6 tests, a screenshot per geometry uploaded, 2 min 2 s for the job. LVGL v9.5.0 + SDL2 2.30.0. Headless under `SDL_VIDEODRIVER=dummy`. Off by default (`-DATTADIPA_BUILD_SIMULATOR=ON`), so a machine with no SDL2 still gets a green host build |
| ESP32-S3 toolchain | **verified** — ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build` completes on a stock example |
| ESP32-S3 firmware | not started — there is no Attadipa firmware to build yet |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED`. Ten plans now exist with equipment, procedure and pass/fail criteria — [HIL_PLANS](docs/testing/HIL_PLANS.md) — so each unproven claim is visibly unproven rather than merely absent |
| Agent automation | **live, and the writer's turn ceiling was the day's most expensive defect.** Six runs on 2026-08-22 — #71 three times, #67, #75, #78 — were accepted, posted an accurate plan about three minutes in, and died at turn 61 of a 60 ceiling with nothing on the branch: `error_max_turns`, `num_turns: 61`, **$3.00 each**, after 8 min 49 s of real work (run `32587675386`). An accurate report over an empty branch is the one outcome nobody can act on. The identical incident had already happened to the *reviewer* the same day and been fixed — 40 → 100, with the reasoning written down — and the writer, which does strictly more, was left at 60. Now 200; spend stays bounded by `timeout-minutes: 60`, which is what is actually billed. **live and exercised in production.** Six workflows on `main`; the intake gate has accepted a real task, derived its labels from the marker and handed it to a Claude run that finished green (runs `32472498158`, `32472504777`). `actionlint` clean over all six with shellcheck integration, `shellcheck` clean over both scripts, the intake gate's 40-case hostile-input test and the watchdog filter's 17-case test pass. **Three defects fixed on 2026-08-22, all silent and all found by reading run logs rather than by anything going red:** the gate was given the issue body where it needed the comment, so every `@claude` mention ever written here was refused with "nothing asks for an agent" — including the owner's on #41; a workflow-level concurrency group cancelled queued intake runs, so labelling #26, #27 and #28 `agent:ready` in one burst started no agent at all; and the hand-over step's pull-request lookup asked GraphQL for `issue(number:)`, which does not resolve pull requests, so an agent started from a comment **on a pull request** got a `NOT_FOUND` document that `gh` had already written to stdout — `|| echo ""` does not undo that — and the outcome comment on #71 went out as ``### Done — pull request #{"data":{"repository":{"issue":null}}…``. `issueOrPullRequest` answers for both, and — the review's finding on the first fix — *pushed to this pull request* now requires the head to have actually moved, because a pull request is open before the agent starts and open after whatever it does; a clean run that pushed nothing says so instead. And the *before* head is read inside the writer queue rather than at event time — the gate fires the instant an event arrives while the agent job waits in `attadipa-agent-writer` for up to an hour, so a gate-time snapshot would have credited this run with a push made by whoever moved the branch in that window. A run that pushed a commit and then died says both things: work landed, and it may be half of it. And a cross-reference is evidence only if it was made **during** the run: #75 cites #71 five times, so filing #75 created that reference before any agent started, and the step announced *"Done — pull request #71"* for a run that produced nothing — then labelled the issue `agent:review`, so nothing would re-queue it. A `Fixes #N` still needs no timestamp; a bare mention does. **And a bare cross-reference is not an answer at all** — the fourth defect in this one step, reported as [#76](https://github.com/hleserg/Attadipa/issues/76) by the producing agent an hour after the third was merged. The step's second question was *which open pull request mentions this issue at all*, and a mention is created by any pull request naming it: #75 cites #71 five times as evidence, so filing #75 made that reference before any agent existed, and a run that produced nothing announced *"Done — pull request #71"*. Filtering mentions by time proved only that one appeared **during** the run, never that this run **caused** it — correlation standing in for ownership. The question is now gone rather than qualified again, and the reason it could go is in the prompt: a research pull request is required to carry `Fixes #N` in the same words as an implementation one, so nothing compliant needed the fallback. A closing reference also no longer launders a failed run into a success — a pull request that exists over a run that died says both things. A cut-off run is also told, in words, that **nothing automated will come back for the unfinished part**: the watchdog scans issues, not pull requests, so no label on a pull request queues anything — the first version added `agent:ready` there and review pointed out the label was promising something that could not happen. The whole decision moved into `.github/scripts/handover-decision.sh` with a 37-case test, because every defect this step has had lived in shell embedded in a workflow where nothing could execute it. The mention path had therefore never worked in production and nothing said so. `CLAUDE_CODE_OAUTH_TOKEN` is configured, so the loop draws on a subscription rather than a metered API account. See [automation](docs/automation/CLAUDE_AUTOMATION.md) |

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

## Recently completed

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
  job. Four checks: relative links resolve, inline code spans close, task IDs are
  unique, and a live task has a body while finished work is filed under
  `## DONE`. The last two exist because the review of
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
  flagged. Twenty-five mutation tests, thirteen of which assert the checker does
  *not* fire.

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
