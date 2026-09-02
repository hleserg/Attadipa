# Open Questions

Everything the project needs to know and does not. Each entry names what would
resolve it, so answering is a task rather than a search.

Status: **UNKNOWN** (no source) · **CONFLICTING** (sources disagree) ·
**ASSUMPTION** (plausible, unconfirmed, must stay flagged in code) ·
**RESOLVED** — a question that was answered. A *fact* moves to
[VERIFIED_FACTS.md](VERIFIED_FACTS.md); a **decision** does not, because it is
not a fact about the world and would read as one there. It stays struck through
in place, pointing at its record in
[OWNER_DECISIONS.md](OWNER_DECISIONS.md). A7 and A8 are the first of these.

An `UNKNOWN` that blocks work is a blocker — record it in the GitHub issue with
the evidence and smallest next action rather than coding past it.

The board survey of 2026-08-21 resolved most of the *documentary* questions.
What remains is dominated by one thing: **no physical board has been touched.**
Everything that needs a measurement is still open, and no amount of reading
will close it.

---

## Blocking everything measurable

**Every `A`-question below is an open GitHub issue labelled `needs-owner`.** That
is where the owner reads it and where they answer; this table is the register, not
the queue, and the two reference each other. Until 2026-08-22 none of A1–A8 had an
issue — they sat here, which is a place the owner does not read, and a question
nobody is asked is not a question.

| # | Question | Status | Resolved by |
|---|---|---|---|
| ~~A1~~ | ~~Does the developer have either board physically, and which revision?~~ | **ANSWERED as an owner question, 2026-08-22 — and what remains of it is not one.** The owner told us what they have; no revision has been read off either board, and that is a loupe on a board on the desk rather than something anybody can answer from memory. It is filed as **D20** below and A1 is struck, because this table's own rule two paragraphs up is that every live `A`-question is an open issue labelled `needs-owner`, and a board read owed to nobody would have sat here breaking it. Board **identity** is `VERIFIED`: the mainboard's silkscreen reads `ESP32-S3-Touch-AMOLED-2.06`, which is the product schematic V1.0 describes. That is not a revision. `2.06` is the panel diagonal in inches — this repository's own `platform/src/board_profiles.cpp` sets `diagonal_milli_inch = 2060` from it — and a V1.1 of the same product would carry the same silkscreen. **No revision field has been read off the unit**, so every V1.0-derived row is confirmed against a document and not against the board in hand; `HARDWARE_MATRIX.md` says the same thing in its own words, *"cite the filename for provenance, never the title block for revision"*. The T-Watch S3 Plus was `ORDERED`, not `PRESENT` — **it arrived 2026-08-27 and is now on the bench**, see [BENCH_DEVICES](BENCH_DEVICES.md) and [TWATCH_S3_PLUS_BRINGUP_2026-08-27](TWATCH_S3_PLUS_BRINGUP_2026-08-27.md); D15 and D20 no longer wait on it, only on a ruler and a loupe | the project owner, [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-16, 2026-08-22, on [#54](https://github.com/hleserg/Attadipa/issues/54), and [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.1 for the silkscreen — whose own heading is *"it is the board we read the schematic for"*, which is board identity and does not claim more. Its §0 warns that the photograph is not authoritative about anything needing magnification the camera did not have, naming the SoC variant suffix and the flash capacity digits; a revision marker is text of the same size. **Two narrower reads remain, and both are filed below rather than here**: the display-FPC marking, the only route to the panel's own part number (**D19**), and the board revision itself (**D20**). Both cite [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) **§5 open-question row 7** and deliberately *not* §Step 1's own prose, which reads *"read the part markings on U2, U3 and the display FPC"* and would hand back the two parts an eFuse read has already closed. The SoC and flash parts are **not** open — [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.2–1.3 read `ESP32-S3R8` out of the eFuses and `0xC8 0x4019` off the flash, on this unit, which is why D1 and D12a are struck through |
| ~~A2~~ | ~~If a T-Watch is present: which of the five radio chips, and which of the two GNSS modules?~~ | **RESOLVED — SX1262 (868 MHz) by order listing; MIA-M10Q from the owner's recollection.** The two halves do not rest on the same evidence and the difference is not cosmetic: the listing quoted in OD-16 names the radio and is **silent on GNSS** | the project owner, [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-16, 2026-08-22, on [#54](https://github.com/hleserg/Attadipa/issues/54). **Both halves are gated the same way and neither gate is in code.** `RadioChip::Unknown` → `Sx1262` waits on reading the marking off the physical part; the GNSS half waits on reading the module, and MIA-M10Q against LS550G decides the DC4 rail, the assistance mechanism, and which of T-051 and T-052 is live. A listing is a seller's claim and a recollection is weaker still |
| ~~A3~~ | ~~Is there a second radio-capable device, so mesh can be tested at all?~~ | **RESOLVED — yes. Five MeshCore nodes**: one Heltec V4.3 companion on a low-power fork and **four** Heltec T114s. The answer of 2026-08-22 said three, and the count was corrected by the owner on 2026-08-31 ([#124](https://github.com/hleserg/Attadipa/issues/124)); the reflash instruction now covers the Home Assistant node alone — the free T114 is held on the pinned `v1.17.1-d929643` by the owner on 2026-08-31 ([#90](https://github.com/hleserg/Attadipa/issues/90#issuecomment-5482898591)), and no decision covers writing to the Room Server or the repeater | the project owner, [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-16, 2026-08-22, on [#54](https://github.com/hleserg/Attadipa/issues/54). Two follow-on questions this answer raised are filed as their own issues rather than answered here: T114 band ([#89](https://github.com/hleserg/Attadipa/issues/89)) and the three-firmware-revision compatibility matrix ([#90](https://github.com/hleserg/Attadipa/issues/90)) |
| A4 | Which regulatory region governs LoRa operation here? | **CLOSED — not this project's to answer** | [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-14, 2026-08-22: asked as [#55](https://github.com/hleserg/Attadipa/issues/55), and the owner declined to name one — *"legality is my problem, not the firmware's."* No country or region is coming; nothing here researches a specific jurisdiction's rule table. [ADR-0006](../adr/0006-settings-and-bounded-values.md)'s transmit-closed-while-`Unknown` gate is unchanged by this and still applies to whoever configures the device |
| A5 | **Is an external magnetometer intended at all?** Neither board has one, so every compass feature in the plan currently has no hardware to run on | **ANSWERED 2026-08-22 — yes** | the owner ordered a **CJMCU-9911 (AK09911C)** and a **GY-271 (QMC5883L)** and is soldering one into the Waveshare unit ([#83](https://github.com/hleserg/Attadipa/issues/83)). The five epics are dormant, not dead. Which part, and where it sits, are open — [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md). **This does not change any board's capabilities**: a stock unit still has no magnetometer and the firmware still has to run on one |
| ~~A6~~ | ~~Does the Attadipa node carry a magnetometer?~~ | **RESOLVED — no.** | The owner decided that the node does not need one; the watch retrofit in A5 is separate. A third-party companion's own heading remains `NodeBody` data and [ADR-0009](../adr/0009-heading.md) still refuses to present it as `WatchBody` heading without a known, calibrated, valid transform. [OWNER_DECISIONS](OWNER_DECISIONS.md) OD-17, 2026-08-22, [#56](https://github.com/hleserg/Attadipa/issues/56). |
| ~~A7~~ | ~~Which orange, and which olive?~~ | **RESOLVED** | the project owner, on [issue #57](https://github.com/hleserg/Attadipa/issues/57), 2026-08-22: §42 wins — Attadipa Orange `#FF8A40`, Ink Olive `#2F3A2E`. The sampled brand-art values that lost have left [`../../pics/README.md`](../../pics/README.md) and are recorded in [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-15 |
| ~~A8~~ | ~~May the icon and favicon be re-exported with transparent corners?~~ | **RESOLVED — yes** | the project owner, same issue. `pics/Ikon.png` and `pics/Favicon.png` are re-exported RGBA with transparent corners; the pixels inside the rounded square are unchanged. OD-15 |
| ~~A9~~ | ~~Does the day theme keep its near-white page on the AMOLED board?~~ | **RESOLVED — an emissive day palette uses Night's dark page and Day's own accent.** | `PixelCost::PerPixel` selects the palette in `ui/src/color.cpp`; `test_ui_tokens.cpp` asserts the page/ink relationship and contrast. This is the recovered #95 implementation now in `main` via #206; [#52](https://github.com/hleserg/Attadipa/issues/52) is completed. The panel-current estimate remains an estimate, not a hardware measurement. |
| ~~A10~~ | ~~What does Attadipa do about static content on the AMOLED?~~ | **RESOLVED — display off by default; wake on raise, button, or touch.** | This is a duty-cycle decision, not a measurement claim about ACL, brightness, or an always-on face. Raise-to-wake is accelerometer-only so both supported boards implement the same common wake behaviour. [OWNER_DECISIONS](OWNER_DECISIONS.md) OD-20, 2026-08-22, [#53](https://github.com/hleserg/Attadipa/issues/53). |
| ~~A11~~ | ~~**Do one or two of the T114s carry GNSS?**~~ | **ANSWERED 2026-08-31 for two nodes of four, and the question's own premise was wrong.** The owner answered [#124](https://github.com/hleserg/Attadipa/issues/124): the fleet holds **four** T114s, not two, so "one or two" quantified over a set that does not exist. Of the four, the Home Assistant node carries no screen and no GNSS and the free bench node carries both; **GNSS fitment on the Room Server and the repeater is `UNKNOWN`** and is not to be inferred from the other two. OD-16's *"either unit"* is superseded along with its count — see the 2026-09-01 annotation there. Nothing was blocked either way: no node here gets an indoor fix under any reading ([#91](https://github.com/hleserg/Attadipa/issues/91)) | the project owner, [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-16 as annotated, and [TEST_FLEET](TEST_FLEET.md) §1, which stays the record of record |

A1 and A2 gate all bring-up, the entire interference matrix, and every power
number. A2 **had** decided whether the radio is sub-GHz or 2.4 GHz; since
2026-08-22 it is answered — SX1262 at 868 MHz by order listing — and what the
row now gates is the *marking read* that lets `RadioChip::Unknown` move. A1's
remaining half is a revision, not a presence: one board is in hand and neither
board's revision has been read.

A4 is not a preference. Which frequencies, power levels and duty cycles are
lawful is set by the region the device operates in, and the answer changes what
the radio may legally do. It has to be settled before anything transmits — by
whoever operates a given device, not by this project on their behalf.

A4 stopped being theoretical on 2026-08-21. The owner's own node is already on
air at 868.731 MHz and 22 dBm. Attadipa is not responsible for that node — but
the numbers it ships as *defaults* are Attadipa's responsibility, and it ships
none: [OD-14](OWNER_DECISIONS.md#od-14--which-region-is-the-owners-problem-not-the-firmwares)
closes A4 as the owner's to answer for his own device, not this project's to
research a table for. Note also that A4 was never going to decide what the core
is built to do: per OD-2 these are settings, so the core carries a bounded,
user-settable value regardless of which region turns out to apply. What A4 used
to promise — a specific profile this project would write and ship as a
default — is not coming, and per ADR-0006 was never supposed to ship as a
default anyway.

A5 decided this: the five epics in §67 are **dormant**, not dead — answered
2026-08-22, see the table above.

Until these are answered: simulator, architecture, host tests and protocol work
proceed; hardware work does not.

## Hardware — measurement required

| # | Question | Status | Resolved by |
|---|---|---|---|
| H1 | Real power draw of Attadipa firmware per state, per board | UNKNOWN | measurement; vendor figures are a target, not evidence |
| H2 | Can the AXP2101 measure current/energy on these boards, or only voltage? | UNKNOWN | AXP2101 datasheet + schematic sense-resistor check |
| H3 | Real TTFF and fix quality for the fitted GNSS module | UNKNOWN | outdoor measurement |
| H4 | Does any of the suspected interference actually occur? | UNKNOWN | the measurement procedure in [../hardware/INTERFERENCE_MATRIX.md](../hardware/INTERFERENCE_MATRIX.md) |
| H5 | Which wake sources are usable in practice, and what does each cost? | UNKNOWN | measurement; vendor table gives the shape |
| H6 | AMOLED brightness vs power on the Waveshare board | UNKNOWN | measurement |
| H7 | Achievable LVGL frame rate and redraw cost on each panel | UNKNOWN | benchmark on hardware |
| H8 | **Is ALDO1 the `+3V3` rail?** The vendor doc says ALDO1 is unused; the schematic shows it driving `+3V3` | **CONFLICTING** | half of this is now in hand: the rail-enable and voltage registers **have been read** off the powered board and are recorded in [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §3.4 — `LDO_ON_OFF0 = 0xFF` says every ALDO/BLDO is enabled, so *"unused"* is already doubtful. Deciding **which load** sits on ALDO1 still needs cutting one rail at a time and watching which parts drop off the scan, which is a write to the PMU and has not been done |
| H9 | Real backlight current vs brightness, against the schematic's 45 mA at full | UNKNOWN | measurement; the 45 mA figure is a datasheet-level I_F, not a measured draw |
| H10 | **The speed gate below which GNSS course-over-ground is not trustworthy** | UNKNOWN | measurement on the fitted module. It depends on the update rate and on whether the module reports Doppler-derived velocity or differenced positions, so it is per-module and cannot be chosen. [ADR-0009](../adr/0009-heading.md) §4; final §26 forbids inventing settling intervals |
| H14 | **Which QMI8658 is on the Waveshare, and which of its datasheets describes the silicon?** Two halves. (a) *A or C* — [TAGS_TRACKS_RECKONING §2.2](TAGS_TRACKS_RECKONING.md) reports the schematic naming `QMI8658C` twice, so this is evidenced as **C** and only needs confirming, not answering. (b) *Which C document* — the half this row was opened for. As it stood: [PEDOMETER_PARTS §2.2](PEDOMETER_PARTS.md) read `13-52-27` Rev A (2022-06-20), where chapter 11 documents a complete hardware pedometer, while TAGS §2.2 reported the only obtainable C datasheet as Rev 0.6 (2021-01) marked ADVANCE INFORMATION, whose `CTRL8` is *"Reserved: Not Used"* and which documents **no pedometer** — so the two were read as describing different parts. **Both halves are closed below**, and the three TAGS sentences that framed this one are withdrawn there. This is [ADR-0003](../adr/0003-radio-not-lora.md)'s shape in a second subsystem — the part name does not tell you whether the feature exists — and OD-6 makes the pedometer mandatory. **Answered on the bench, 2026-08-23 (S13):** the silicon reports `REVISION_ID = 0x7C`, which is the value in `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A`; the C Rev 0.6 document gives `0x79`. So (a) the schematic's `QMI8658C` name does **not** predict the register map, and (b) **`13-52-27` is the document that describes this part** — the one with a hardware pedometer in chapter 11. Corroborated by writing: `CTRL2 = 0x26`, `CTRL7 = 0x01`, `CTRL8 = 0x90` all read back exactly, `CTRL8` included, which the Rev 0.6 document calls *"Reserved: Not Used"*; the accelerometer then read gravity at 1.03 g under Rev A's ±8 g scaling. **Which Rev A document — resolved by [#341](https://github.com/hleserg/Attadipa/issues/341).** This row used to end *"the Rev A document number is 13-52-25, not 13-52-27"*; that is withdrawn. QST published two Rev A datasheets dated 20 June 2022 and both exist — `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A` and `13-52-25 ∙ QMI8658A Datasheet ∙ Rev A`. Both have since been read side by side, and **both give `0x7C`** (register-description section; both also print `0x68` in their register-map summary). So `0x7C` separates Rev A from the Rev 0.6 draft, which is what this row needed, but it does **not** separate the two Rev A documents from each other — nor do `WHO_AM_I` or the product id. `13-52-27` is named here because the schematic prints `QMI8658C`, not because any register says so. md5s: `13-52-27` `e093b1cc1d1cf85097f955abbea65c08`, `13-52-25` `5a0fef65a358430d6499944a75d22e19`. Nothing measured moves. Withdrawn from this row, matching the three sentences [TAGS_TRACKS_RECKONING §2.2](TAGS_TRACKS_RECKONING.md) withdraws by name: the clause reporting *"the only obtainable C datasheet"* as Rev 0.6 — `13-52-27` is a C datasheet, it is Rev A, and it is in hand; the attribution of *"no pedometer"* to the C part — chapter 11 of `13-52-27` documents `CTRL8.Pedo_EN` and the same `STEP_CNT_*` registers as the A's Rev A does; and the conclusion that the two documents *"describe different parts"* — they describe the same silicon, and no register tells them apart. So half (b) is **not** open: `13-52-27` is the document, and what it does not settle is which of the two Rev A papers applies, which no register can | **RESOLVED** (was CONFLICTING) | done — [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §3.2. What is **not** done is proving the engine *counts*. On 2026-08-23 step count stayed 0 on a stationary board — the correct reading, and no evidence either way. On 2026-08-28 (S15) it stayed 0 through sixteen unbroken seconds of hand motion whose per-axis one-second peak-to-peak reached 2242 mg (scale UNKNOWN: 1121 mg if that build divided by 4096) against a configured `ped_fix_peak2peak` of 78 mg, which is a negative result rather than a null one — but hand-shaking is not the gait chapter 11 specifies, so it does not settle the question either. That still needs someone to walk twenty steps with it — **T-112**, [PEDOMETER_BENCH_2026-08-28](PEDOMETER_BENCH_2026-08-28.md) |
| H15 | **What is the IMU's axis orientation relative to the wearer, on each board?** A step counter tolerates a wrong sign; a wrist-raise gesture and any future orientation feature do not. [PEDOMETER_PARTS.md](PEDOMETER_PARTS.md) §1.9 notes that on the BMA423 axis remapping applies to the *features* only, so getting it wrong is silent rather than obviously broken. **Half answered on the Waveshare, 2026-08-22:** the board-frame triad is silkscreened beside the IMU — X toward the battery edge, Y toward the USB-C edge, Z as ⊙ out of the back face ([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.6). What is still missing is how the board is rotated inside the case, and one is useless without the other. Nothing is known for the T-Watch | **PARTIAL** (was UNKNOWN) | tilt the **assembled** watch through known angles and read raw axes — the board frame is printed, the case rotation is not. Cheap now that a Waveshare is on the desk; still impossible for the T-Watch |
| H16 | **What pull-up does each magnetometer module actually fit, and does the CJMCU-9911's `RST` pad reach the die?** Four ohmmeter readings on two bare modules, no board, no power, no soldering: `SDA`→`VCC` and `SCL`→`VCC` on each module gives the pull-up; `RST`→`VCC` and `RST`→`GND` on the CJMCU-9911 says whether the pad is tied, floating, or not connected at all. **Neither is guesswork-tolerant.** The pull-ups land in parallel with the Waveshare board's own 2.2 kΩ and the combination must stay above `UM10204`'s 3 mA sink limit — 4.7 kΩ passes, 3.3 kΩ does not — and `RSTN` left floating is forbidden by the AK09911C datasheet outright, which makes the retrofit five wires rather than four until this says otherwise. The arithmetic and both pass lines are already written, so this is a lookup rather than an analysis | **UNKNOWN — `NOT MEASURED`** | the four probes above, once the modules arrive. [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md) §2.6.3 and §4.3.3; owned by **T-109** |
| H18 | **What are the two GNSS modules on the bench, and what do they want on `VCC`?** Two receivers for the node path arrived 2026-09-02 — a "GT-U12" and a QUESCAN "AN3126", recorded from their listings only in [BENCH_DEVICES](BENCH_DEVICES.md#two-gnss-modules-delivered-2026-09-02--not-yet-read). Supply, logic level, baud, and the chip behind each are UNKNOWN. | UNKNOWN | identify the carrier's regulator or its absence before applying power, and the bridge's `TXD` level — jumpered or measured — before the first byte goes out, since a 3.3 V-only `RX` does not survive a 5 V bridge any better than `VCC` does; then a minute of NMEA on each, **read-only first**: the sentence prefixes name the vendor without anything driven in (`$GBGSV`, `$GIGSV` per [idei-i-dorabotki](../ideas/idei-i-dorabotki.md), working IRNSS being the Allystar tell); only then, for the version, a query the part can answer — `UBX-MON-VER` only if the AN3126 is the u-blox it claims, and for the GT-U12 the `$GNTXT` sentences it sends on its own plus a vendor probe such as the CAS `$PCAS06,0*1B` from the same catalogue, each tried and its silence recorded as the probe's, not the module's — the owner's bench |
| H17 | **What is the Waveshare main I2C bus's total capacitance `Cb`?** Unneeded at the 100 kHz the bench scan ran at, and the first thing that bites at 400 kHz: with the board's 2.2 kΩ pull-ups, `UM10204` Rev. 7.0 §7.1 caps `Cb` at **161 pF** for Fast-mode, and the pin capacitances plus the board's own `C34`/`C35` at 22 pF each put the bus at a loosely `ESTIMATED` 100–150 pF before any retrofit lead is added. So the headroom for two modules on flying wires is `UNKNOWN` and may be negative | **UNKNOWN — `ESTIMATED` only** | a scope on a rising edge of `SDA`, measuring 30 %→70 % against the `Rp` in circuit. [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md) §4.3.4 |

## Radio

| # | Question | Status | Resolved by |
|---|---|---|---|
| R1 | **Confirm every modulation, band and conducted-power figure in the radio matrix against the manufacturer datasheet.** The current values come from RadioLib 7.7.1's driver range checks and MeshCore's build config, not from TI, Silicon Labs or Semtech | **PARTIAL** | `ti.com` returns HTTP 403 and the Silicon Labs document host timed out under automated retrieval. Needs a manual fetch, or the PDFs obtained another way. Nothing may transmit on the strength of a number in that table until this is closed — [ADR-0003](../adr/0003-radio-not-lora.md) |
| R2 | Does the LR1121 work through MeshCore's `CustomLR1110Wrapper` plus `LR11x0Reset.h`? RadioLib's `LR1121` derives from `LR1120`, which derives from `LR11x0`, so it is plausible | **UNKNOWN** | a spike, not a reading. Decides whether `MeshCoreSupport::NeedsWork` for that chip is a week or a month |
| R3 | Which radios MeshCore supports **at the revision Attadipa actually pins**, re-checked rather than assumed | tracked | the matrix is a `grep` over `RADIO_CLASS` across `variants/`; it is a task (T-013), not a hope, because upstream adds radios |

## Hardware — documentary gaps

| # | Question | Status | Resolved by |
|---|---|---|---|
| ~~D1~~ | ~~Waveshare flash and PSRAM size~~ | **RESOLVED** | schematic: `GD25Q256EYIGR` = 32 MB quad flash; SoC is `ESP32-S3R8` = 8 MB PSRAM. Type of PSRAM rolls into D12. **Confirmed on silicon 2026-08-22**: JEDEC `0xC8 0x4019` and eFuse `PSRAM_CAP = 8M` — [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.2–1.3 |
| D2 | Waveshare battery capacity and charge path details | **PARTIAL** | **Capacity answered**: 400 mAh / 3.7 V, cell `402728`, read off a received unit 2026-08-22 — [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.2. The cell is on a **removable 2-pin plug**, not soldered. **Charge path traced 2026-08-22** — [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §4: Waveshare's own demo sets **400 mA**, upstream XPowersLib's copy of the same file sets 200 mA, and the `REG 0x62` power-on default cannot be quoted at all because the datasheet prints it eFuse-trimmed. The Waveshare BSP configures the charger **not at all**, so whatever is in that register at boot is what charges the cell. `REG 0x16` defaults to a **1500 mA** input limit, which is not USB-compliant on a port that granted 500. **And the sticker is the thing now in doubt**: `402728` is 3.024 cm³, so 400 mAh at 3.7 V implies 132.3 mAh/cm³ against an 87–102 band across 51 datasheet cells from four manufacturers — honest expectation 250–310 mAh, which makes the vendor's own 400 mA setting **1.33C** on a pouch whose class maximum is 1.0C. **Still `UNKNOWN`**: the value actually in `REG 0x62` on this board (never read), the `TS`/NTC termination, and the `BAT1` connector part and pitch. The reading of the sticker is not in doubt; what it means is |
| ~~D18~~ | ~~**Which ESP32-S3 errata apply to revision v0.2?**~~ | **RESOLVED — and the answer is "all of them"** | ESP32-S3 Series SoC Errata **v1.3** (2025-03-31), read 2026-08-22. All **eight** errata carry `Affected revisions: v0.0 v0.1 v0.2`, and seven say `No fix scheduled.` **There is no newer revision to want**: the sheet knows only three, and ESP-IDF's `COMPATIBILITY.md` agrees. The one revision-dependent improvement, USBOTG-4289, lands *inside* v0.2 in the owner's favour. [ESP32S3_ERRATA_V02](ESP32S3_ERRATA_V02.md) — and the one with teeth for this design is **CACHE-126**, whose workaround masks every interrupt and freezes the data cache, at a cost Espressif never publish |
| ~~D3~~ | ~~Waveshare expansion connector pinout~~ **The question was mis-stated: there is no expansion connector.** Read visually, `J3` is the 34-pin AMOLED display FPC — its block is titled AMOLED and carries `QSPI_SIO0`–`SIO3`, `QSPI_SCL`, `LCD_CS`/`RESET`/`TE`, the MIPI pairs, `VCI`, `VDDIO`, `IM0`/`IM1` and `TP_SCL`/`TP_SDA`/`TP_INT`/`TP_RESET` | **CLOSED — mis-stated** | [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.4. This retires the hot-unplug and bus-capacitance worry D3 inherited from the T-Watch, where main-I2C `SDA` genuinely does reach a detachable GNSS connector — but it confirms the touch half of the main I2C bus leaves the mainboard over a flex cable |
| ~~D4~~ | ~~Does the Waveshare board have any haptic output?~~ | **RESOLVED — and the earlier answer was wrong** | **Yes, as a circuit.** A vibration motor on pads `P1`/`P2` (recorded as `J1` until 2026-08-22 — `J1` is the *battery* connector, see [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1.1), driven from GPIO 18 through R12 (4.7 kΩ) and Q1 (MMBT3904), supplied from BLDO2. No driver IC — which is why searching for a haptic part found nothing. **The pads are bare on the received unit and no motor is fitted** — T-097 |
| D5 | Waveshare button/wake inputs — BSP declares none; is that the board or the BSP? | **CLOSED 2026-08-25** | The official schematic and current vendor product page identify the two case keys as PWR and BOOT. PWR pulls AXP2101 `PWRON`; BOOT pulls GPIO0 low. `SYS_OUT/GPIO10` is the resulting power-state signal, not the PWR key level. On the physical unit, two BOOT presses produced two debounced `down/up` pairs through `core::InputQueue`; the PWR firmware path uses AXP2101 negative/positive edge status rather than GPIO10. See [WATCH_CONTROL_2026-08-25](../hardware/WATCH_CONTROL_2026-08-25.md). |
| D6 | T-Watch: which PMU rail powers GNSS on the *specific* unit (BLDO1 vs DC3) | UNKNOWN | inspect the unit for rear BOOT/RST buttons |
| D7 | ~~Exact ST7789V3 and CO5300 init sequences and their timing~~ **Split on 2026-09-01: one row was three questions with three kinds of answer, which is why it never closed.** The CO5300 half is unchanged and still reads as below; the ST7789V3 half is now D7a/D7b/D7c | **SPLIT** | vendor driver source. **Half of it is now also D21's**: the CO5300 datasheet's `3Ah`/`2Ch` 16-bit bit packing would close the controller half of the byte-order question without a board, and the same document answers this one |
| ~~D7a~~ | ~~What does the ST7789 controller baseline actually send?~~ | **RESOLVED 2026-09-01** | ESP-IDF v5.5.5 `esp_lcd_panel_st7789.c:182-201`, read at the tag: `panel_st7789_init()` sends `SLPOUT` + 100 ms, `MADCTL`, `COLMOD`, `RAMCTRL`, and nothing else. `esp_lcd_new_panel_st7789()` never reads `panel_dev_config->vendor_config` — the string does not occur in the file — so **the built-in driver has no init-command extension point**, which is a fact about the driver and not a limitation to route around: the public `esp_lcd_panel_io_tx_param()` takes board commands after `init()`. [TWATCH_S3_PLUS_BSP_REUSE](TWATCH_S3_PLUS_BSP_REUSE.md) §3, [ADR-0017](../adr/0017-board-backends-compose-esp-idf-drivers.md) |
| D7b | Does this exact ST7789V3 panel *need* LilyGO's vendor command table, or is the generic baseline enough? | **UNKNOWN — but the bytes are no longer unknown, only their necessity** | The table is transcribed at its pinned revision in [TWATCH_S3_PLUS_BSP_REUSE](TWATCH_S3_PLUS_BSP_REUSE.md) §5, with the ordering constraint that makes it work and the `#if 0` dead twin that makes transcribing it dangerous. **The obvious two-arm test cannot answer this**, because the table's first entry is `SLPOUT` + 120 ms and so differs from the baseline in timing as well as content — see D7d. Needs the three-arm experiment, §11: NOT EXECUTED — HARDWARE REQUIRED |
| **D7d** | **Does the generic ESP-IDF reset path violate the ST7789V spec on a board with no reset line?** | **YES — documentary, no board needed** | `esp_lcd_panel_st7789.c:167-177` takes the software-reset branch when `reset_gpio_num < 0`, sends `SWRESET` and waits **20 ms**; `panel_st7789_init()` then sends `SLPOUT` immediately. ST7789V datasheet v1.6 (2017/09) p.163 §9.1.2 *Restriction*: *"If software reset is sent during sleep in mode, it will be necessary to wait 120msec before sending sleep out command."* p.184 §9.1.12 *Default*: the state after `Power On Sequence`, `S/W Reset` **and** `H/W Reset` is *"Sleep in mode"*, so the 120 ms clause is the one in force and the driver's own comment quotes the 5 ms clause that is not. **The T-Watch S3 Plus has no display reset line** (three independent sources, [VERIFIED_FACTS](VERIFIED_FACTS.md)), so this branch is always taken there. Related: `panel_st7789_sleep()` waits 100 ms where the same datasheet asks 120 ms between `SLPOUT` and `SLPIN`. Whether either is *observable* on this panel is D7b's experiment |
| D7c | ST7789V3 SPI clock, and cold-boot stability after full power removal | **UNKNOWN, and there is no consensus value to inherit** | `LilyGoLib@38e6f8d` `src/LilyGoWatchS3.cpp:135` passes **80 MHz**; `meshtastic/firmware` `variants/esp32s3/t-watch-s3/variant.h:13` sets **40 MHz** on the same board. Two mature same-board implementations, a factor of two apart, so neither is a source-justified Attadipa default. Start at the lower proven value and raise it on a measurement — [TWATCH_S3_PLUS_BSP_REUSE](TWATCH_S3_PLUS_BSP_REUSE.md) §6. NOT EXECUTED — HARDWARE REQUIRED |
| D21 | **In what byte order does the CO5300 want a 16-bit pixel on the wire?** *(Filed as D19 until 2026-08-24 and renumbered: `D19` was already taken by the display-FPC part marking below, which reached `main` while the branch that added this row was open. `OWNER_DECISIONS.md` records the FPC one, so this is the row that moved. Any surviving reference to "D19, the byte order" means this.)* Not the same question as the format of a file on flash, and the two were conflated until 2026-08-23. The stored `/image/*.bin` bodies are RGB565 **little-endian on disk** — proven by rendering them on a host, which is proof about the file and about nothing downstream of it. The one complete path readable in pinned source **swaps every pixel** before transfer: `xiaozhi@bb9122ab lcd_display.cc:160,166` sets `.swap_bytes = 1` → `esp-bsp@2f51931 esp_lvgl_port_disp.c:739-741` calls `lv_draw_sw_rgb565_swap()` → `esp_lcd_sh8601.c:279-280` transfers the result verbatim. So the naive reading is not merely unproven, it is the wrong way round on at least one real path — and the app that actually drew those three files, `phone_s3_box_3 v0.4.2-92-g5c6be6c-dirty`, is unpublished, so what *it* did is unreadable | **UNKNOWN** | the CO5300 datasheet on `3Ah`/`2Ch` (see D7), **or** a measurement: a known asymmetric pattern written to `RAMWR` with the swap off, photographed. Both routes and what to record are in [WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §7. Blocks nothing today — the asset pipeline emits `A8` masks, which have no byte order — and blocks **the first line of display bring-up**. It does **not** block the first colour asset: an asset's byte order follows LVGL's colour-format contract and the framebuffer the software renderer writes into, and the wire order is absorbed once, at flush, by the port's `swap_bytes` flag — a **board** fact, so it lives in `boards/`/`platform/` rather than in settings or a build flag. Four places said otherwise until 2026-08-24; for `RGB565A8` that instruction was not even executable, the vendored converter having no swapped variant of that format. Found in review. **The swap also costs a full software pass per frame** — 411 640 B on Waveshare — which is `UNKNOWN` in cost as well as in necessity and is an input to T-093 |
| D8 | Is the T-Watch main I2C bus shared with anything timing-sensitive? | PARTIAL | schematic read: **four** addressable devices on SDA 10 / SCL 11 — `0x34`, `0x51`, `0x19`, `0x5A` — and **no fifth**: D9 is closed and the GNSS never reaches this bus. (This cell said *five* and *sixth*; it was counting the FT6336U at `0x38`, which [HARDWARE_MATRIX](HARDWARE_MATRIX.md):97 "separate I2C" puts on the separate touch bus. All four answered a scan on 2026-08-28, [TWATCH_S3_PLUS_DOWNLOAD_MODE](TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md) §8.) Timing sensitivity still needs driver review |
| ~~D9~~ | ~~**Does the GNSS daughterboard connect the `MIA-M10Q` `SDA`/`SCL` to the FPC?**~~ | **RESOLVED — no** | the daughterboard net list, read as a drawing and not as a text dump, 2026-08-28 — [#312](https://github.com/hleserg/Attadipa/issues/312). On `J1` only pins 1 `GPS_TX`, 2 `IO0`, 3 `VDD3V3`, 6 `RST/EN`, 8 `GPS_RX` and 13 `GND` carry a net; 4, 5, 7 and 9–12 are unlabelled stubs. On `U1` the `MIA-M10Q` `SDA` (ball D1) is a free stub and `SCL` (ball E1) ties only into a local bracket with two `RESERVED` balls. **The GNSS is not a device on the main I2C bus.** The silent `0x42` of 2026-08-28 is explained by the wiring, the state of `GPS_LDO` is irrelevant to it, and the bench run this row asked for would have returned the same silence — it is no longer needed and this row does not carry `needs-hardware`. Nothing here was probed on hardware |
| ~~D10~~ | ~~**What is radio `DIO3` (GPIO 6) for on this board — TCXO supply or a second interrupt?**~~ | **RESOLVED 2026-09-01 — TCXO supply, and never an interrupt** | The vendor firmware answers the *binary* by omission, which is the strongest evidence available without a module datasheet. `LilyGoLib` builds its driver with `newModule()` = `new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, SPI)` (`Xinyuan-LilyGO/LilyGoLib` @ `38e6f8dee3ba78b340512af9a013365ef248a7d0`, `src/LilyGoWatchS3.h:33`; the object it builds is `SX1262 radio = newModule()`, `src/LilyGoWatchS3.cpp:30`) — **`DIO3` is not among the pins RadioLib is given**, so it is not an interrupt the driver could read — and then calls `radio.begin()` with no arguments (same commit, `src/LilyGoWatchS3.cpp:995`). `SX1262::begin()` defaults `tcxoVoltage = 1.6`, documented at `jgromes/RadioLib` **7.1.2** — the version LilyGoLib's own `library.json:35-36` declares at that commit — `src/modules/SX126x/SX1262.h:41,47`, as *"TCXO reference voltage to be set on DIO3"*, so every LilyGO build silently issues a `setDio3AsTcxoCtrl` at 1.6 V. (This row cited `SX1262.h:52,58` until 2026-08-28. Those were read from RadioLib `master`, which moves; the same two facts sit at `:41,47` in the pinned 7.1.2. The claim never depended on the drift — the text is identical at both — but only the pinned pair can be re-opened in six months.) The pin itself was already recorded — [HARDWARE_MATRIX](HARDWARE_MATRIX.md):102 "DIO3 6" carries `DIO3 6`, and the rendered radio sheet confirms all eight SPI and control nets; all twelve `HPD16B3` pins and the antenna match are now in [HARDWARE_MATRIX](HARDWARE_MATRIX.md#the-hpd16b3-pinout-and-the-pin-nothing-may-drive) too. ⚠️ **`GPIO 6` must never be driven as an output** — and that follows from the pin type and the board wiring alone, with no TCXO premise: `DIO3` is a chip **output** in every documented SX126x configuration, whether it is carrying an interrupt or a regulator, and [HARDWARE_MATRIX](HARDWARE_MATRIX.md):102 "DIO3 6" records GPIO 6 wired to it. Two drivers on one net is a contention fault regardless of what the pin is doing. That rule belongs in a `platform/` pin-role table where a wrong `gpio_set_direction` fails a build, next to the strapping pins — prose fails on a wrist. **What the vendor firmware establishes is what it *configures*, not what the module *requires*** — and two things that do not follow from any of the above moved to **D10a** rather than keeping this row open: whether the module contains a TCXO at all, and what voltage it wants. [#326](https://github.com/hleserg/Attadipa/issues/326) |
| D10a | **Does the `HPD16B3` contain a TCXO, and what supply voltage does it want on `DIO3`?** Split out of D10 on 2026-09-01. D10 asked what the pin is *for*, and the vendor firmware answers that by omission; neither of these follows from that answer, and leaving them inside a row whose own question was settled is how D7 stayed open for a month | **UNKNOWN — the voltage is the half with a consequence** | The module's datasheet is published nowhere reachable: a code search of `Xinyuan-LilyGO` returns nothing for the string `HPD16B3`, and the open web has no copy, so the TCXO-presence half has no documentary route today and reading the marking off a fitted module is the cheapest one left. The voltage half blocks nothing and constrains everything downstream of it: **Attadipa's own bring-up must name its `tcxoVoltage` rather than inherit RadioLib's default**, whatever the module turns out to want. Recorded as a **hypothesis, not a finding** — LilyGO's watch takes the 1.6 V default (`src/LilyGoWatchS3.cpp:995`, called with no arguments), while LilyGO's own `LilyGo_LoRa_Pager` and all three upstream `SX1262` examples pass `setTCXO(3.0)`, and Seeed's published [Wio-SX1262 datasheet](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Wio-SX1262_Module_Datasheet.pdf) gives a comparable module's TCXO supply range as 1.7–3.3 V, which 1.6 V sits below. **There is no SX1262 bring-up in the tree** — `platform/` carries `radio_info` and `board_profiles` and no driver — so this row and [HARDWARE_MATRIX](HARDWARE_MATRIX.md#the-hpd16b3-pinout-and-the-pin-nothing-may-drive), not a code comment, are where the constraint waits for the change that adds one. [#326](https://github.com/hleserg/Attadipa/issues/326) |
| ~~D12~~ | ~~**Is the PSRAM quad or octal — on *both* boards?**~~ **Split.** It was one question only because both boards carry an `ESP32-S3R8` marking, and that shared premise does not survive contact with the sources: the marking settles the *part*, and the part is octal. See D12a and D12b | **SPLIT** | [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.1 |
| ~~D12a~~ | ~~**Waveshare: quad or octal?**~~ **Octal.** ESP32-S3 Series Datasheet v2.2 §1.2 Table 1-1 lists `ESP32-S3R8` as `8 MB (Octal SPI)`, and no 8 MB quad in-package variant exists in that table — the only quad in-package parts are the 2 MB `RH2`, `R2` and `FH4R2`. Footnote 3 names `R8`, `R8V` and `R16V` as the octal set; `R8` and `R8V` differ by `VDD_SPI` voltage, not bus width. Corroborated by five of the six vendor examples shipping `CONFIG_SPIRAM_MODE_OCT=y` with `CONFIG_SPIRAM_IGNORE_NOTFOUND` unset — a build that aborts at boot if octal is not found — and by GPIO33–37 sitting unrouted on the schematic, which is where octal PSRAM's DQ4–DQ7 and DQS go | **RESOLVED** | traced, not recollected: [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.1. Step 4 of §5 confirms it empirically by reading the boot log's `octal_psram` tag. **Empirically closed 2026-08-22 without needing that step**: the die's fuses read `PSRAM_CAP = 8M`, `PSRAM_VENDOR = AP_3v3` (so `R8`, not the 1.8 V `R8V`) and `PIN_POWER_SELECTION = VDD_SPI`. The eFuse gives capacity and rail, not bus width — the step to octal stays Table 1-1's — but both legs now have evidence. [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.2. **Confirmed by the running silicon 2026-08-23** — the vendor bootloader's own `octal_psram` driver enumerates the part (`vendor id 0x0d (AP)`, `density 0x03 (64 Mbit)`, `VCC 0x01 (3V)`, `Readlatency 0x02 (10 cycles@Fixed)`, `Found 8MB PSRAM device`, `Speed: 80MHz`). A quad part would not have loaded that driver. This is step 4 of WAVESHARE_ARRIVAL §5, executed — S13, [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §3.1 |
| ~~D12b~~ | ~~**T-Watch: quad or octal?**~~ **RESOLVED 2026-08-27 — octal.** Settled the way this row asked for it: on a physical T-Watch S3 Plus, not by re-reading the document. The die's own eFuses report 8 MB, vendor `AP_3v3`, and `FLASH_CAP`/`FLASH_VENDOR` unset — an `ESP32-S3R8` with external flash, and Table 1-1 has no 8 MB quad in-package part. The LilyGO vendor document calling it QSPI is wrong, which is worth stating plainly rather than leaving as a disagreement | **RESOLVED** | [TWATCH_S3_PLUS_BRINGUP_2026-08-27](TWATCH_S3_PLUS_BRINGUP_2026-08-27.md) §5, and [HARDWARE_MATRIX](HARDWARE_MATRIX.md) carries the fact |
| ~~D13~~ | ~~Waveshare: which loads sit on ALDO1, ALDO2 and ALDO3 — all three are 3.3 V — and **what runs on the 1.8 V ALDO4 rail**?~~ **The question’s own premise is wrong: ALDO3 is 3.0 V**, register `0x94 = 0x19`, and its net is named `VCC3V` | **RESOLVED** | the schematic read as a drawing, 2026-08-28 — [#313](https://github.com/hleserg/Attadipa/issues/313). **ALDO1** (3.3 V) = net `A3V3`, the analogue audio supply, selected by the fitted `0R` links `R17`/`R29`/`R34`. **ALDO2** = *not a supply*: an `R10` 10K pull-up on `DSI_PWR_EN`, with no decoupling. **ALDO3** (3.0 V) = the vibration motor, `R7 0R` → `P1`, switched by `Q1` MMBT3904 from `GPIO18`. **ALDO4 (1.8 V) feeds nothing** — and neither do BLDO1, BLDO2, CPUSLDO, DLDO1 or DLDO2, although `LDO_ON_OFF0 = 0xFF` / `LDO_ON_OFF1 = 0x01` has all nine LDOs on. ⚠️ **Do not switch ALDO2 off.** It reads as a free rail, but it holds `DSI_PWR_EN` (panel pin 21) high, the panel’s `VCI`/`VDDIO` come from `VCC3V3`, and no GPIO drives that line — disabling it blanks the display by a route that looks like a wiring fault. Register decode traced to AXP2101 Datasheet V1.4 §6.13.2.75–77, not to library source. The saving available from the six idle rails is UNKNOWN — nothing was measured |
| D14 | **Waveshare SD card: which bus mode is the connector actually wired for?** The BSP uses SDMMC 1-bit on GPIO 1/2/3; the schematic labels those nets `MOSI`/`SCK`/`MISO` and shows a chip-select near GPIO 17 that the pin map does not have. **Half answered on the bench 2026-08-23 (S13) — and this row carried `RESOLVED` for one day on evidence that does not support it.** What the factory firmware's boot log establishes is that **the vendor's software drives this slot through the SDMMC host driver**. The reason first recorded for that was wrong — *"it calls `sdmmc_common`/`vfs_fat_sdmmc`, not `sdspi`"* — because both of those are the shared protocol layer and an SD-over-SPI mount prints under the same two tags. The conclusion survives on a different discriminator: on the SPI path CMD0's second send expects an R1 and fails into an empty slot, so `sdmmc_init_ocr` is never reached and that log line cannot be produced. **What none of it establishes is the wiring, and it cannot**: the slot was **empty**, and `send_op_cond` times out identically for every possible wiring when nothing is in the socket to answer. The pins add nothing either — on the ESP32-S3 the SDMMC slots route through the GPIO matrix, so *"any GPIO may be used for each of the SD card signals"*, and the two modes share the card's own contacts **by specification** — pin 2 is `CMD` in native mode and `DI`/`MOSI` in SPI, pin 5 is `CLK`/`SCK`, pin 7 is `DAT0`/`DO`, and SPI adds only a chip select on pin 1 — so a net named `MOSI` and a net named `CMD` are one piece of copper on those three contacts rather than two claims about it. (This row argued that from ESP-IDF's `SDSPI_DEVICE_CONFIG_DEFAULT()` until 2026-08-24; at v5.4 that macro fills in `gpio_cs` and three `GPIO_NUM_NC`s and its struct has no clock, MOSI or MISO field, so the quote did not support the claim.) **A software choice had been promoted to a hardware fact** — [#131](https://github.com/hleserg/Attadipa/issues/131) | **PARTIAL** (was UNKNOWN, then `RESOLVED` for one day, 2026-08-23) | **only the schematic sheet, read visually, closes it** — which needs no board. A known-good card enumerating on the bench does **not**: because the two modes share the card's contacts, a card answering over SDMMC on GPIO 2/1/3 is exactly what a slot wired for SPI would also produce. What the bench can add is the narrower fact *which host driver enumerates a card here, at which clock, on which pins*, plus a passive reading of GPIO 17 — the non-destructive procedure is [SD_CARD_MODE_TEST](../hardware/SD_CARD_MODE_TEST.md), `NOT EXECUTED — HARDWARE REQUIRED`, **T-127**, and §10 of that file states what it does and does not close. Evidence and the correction: [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §4.3 |
| D22 | **Does the T-Watch cell terminate at 4.2 V or 4.35 V?** Its label reads `3.8V 940mAh` and [HARDWARE_MATRIX](HARDWARE_MATRIX.md):88 "The vendor document's 3.7 V is the outlier" carried `3.7 V` from the vendor document. A 3.8 V nominal is normally high-voltage chemistry ending at 4.35 V; a 3.7 V one ends at 4.2 V. Charging the 4.2 V cell to 4.35 V damages it and charging the 4.35 V cell to 4.2 V wastes about a fifth of its capacity, so this is not a rounding difference. **No code sets an AXP2101 charge target voltage today**, but the repository is not silent on that register: [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §6 and §8 prescribe writing `REG 0x64 = 011b` (4.2 V) and reading it back, as a **non-negotiable stated without a board scope** in a document written for the *other* board's cell. For a 4.35 V cell that prescription is merely lossy, not dangerous — but the converse generalisation, carrying a 4.35 V target back to the Waveshare's 4.2 V cell, is exactly the fire path §8 names. So this is a per-board setting currently written as a global one, and the first change that sets the register on either board owns this question. A nominal on a label never states a termination, so the label cannot close it | **RESOLVED — 4.35 V, MEASURED** | **Answered 2026-08-28 on the physical unit. `REG 0x64` reads `0x04` = `100b` = 4.35 V, so this is a high-voltage cell: the label's 3.8 V nominal is right and the vendor document's 3.7 V is the outlier. The decode is traced to vendor source rather than to this repository's own prose — in XPowersLib, the library LilyGO ship for this watch, `XPOWERS_AXP2101_CV_CHG_VOL_SET` is `0x64`, the field is the low three bits, and `XPOWERS_AXP2101_CHG_VOL_4V35 = 4`. See [TWATCH_S3_PLUS_DOWNLOAD_MODE](TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md) §8. The method below is kept as the record of how it was done:** reading the AXP2101 `CHG_V` field (`REG 0x64[2:0]`) that the *factory* firmware programs, over I2C, on a board that is now present. **That exact read has already been done once on the other unit** — [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §3 captured `CHG_V_CFG = 0x03` = `011b` = 4.2 V from a RAM app that changed nothing, so the method is proven and costs no flash write. **That reading is a prior for the method, not for this cell** — different cell, different board, different vendor firmware — so it constrains nothing here and the re-confirmation stays `UNKNOWN` until somebody reads *this* unit's `0x64`. Failing that, the cell vendor's own specification for `TERKDELL 112530-2P` |
| ~~D15~~ | ~~**What is the T-Watch panel's physical diagonal — 1.3" or 1.54"?**~~ | **RESOLVED — 1.54", MEASURED** | **27.72 mm across the active area, −0.2 to +0.7 mm two-sided, on the physical unit, 2026-08-28 — a 1.544" diagonal, and 220 ppi rather than 261.** Measured from photographs of the running watch supplied for [#311](https://github.com/hleserg/Attadipa/issues/311), with a steel rule resting **on the case**, coplanar with the glass, so the standard-to-subject offset collapses to the rim-to-emitter millimetres priced in [TWATCH_S3_PLUS_PANEL_2026-08-28](TWATCH_S3_PLUS_PANEL_2026-08-28.md#5-what-the-number-is-worth) §5(a) — `ESTIMATED`, +0.14 to +0.71 mm — which the earlier rule-on-the-desk attempt could not do. That attempt's frames were not retained either, so no figure from it is carried here. The full-resolution frames were **not retained**; one crop of the first was, and it carries the rule and both vertical edges of the lit area at one scale, so the whole derivation is re-run from it in-tree by [TWATCH_S3_PLUS_PANEL_2026-08-28](TWATCH_S3_PLUS_PANEL_2026-08-28.md) — 429.5 px of active width against 15.491 px/mm — both corrected for tilt, from a raw 429.85 px and 15.529 px/mm — edges fitted by least squares. The first pass reported 27.76 mm from numbers nobody can now check; **this row keeps only what the committed image reproduces**, and 0.04 mm is what the difference was worth. **1.3" is excluded, not merely disfavoured:** predicted active width is 27.66 mm at 1.54" and 23.35 mm at 1.3", so the result sits 0.2 % from the first and 18.7 % from the second. Reaching 1.3" needs the image scale to be wrong by 19 %, which would have to appear as 19 % non-uniformity in the rule's own graduations — the marks the scale is read from, and uniform here to about 1 % of one graduation across two independent sets of marks that agree to 0.35 %. The method carries two systematics of comparable size pulling opposite ways — the rule rests on the case while the emitters sit under the glass, which raises the true width, and the frame's perspective gradient, which lowers it by about 0.23 mm — and even at the low end, 27.50 mm, the result is +17.8 % from 1.3". So the schematic was right and LilyGoLib's spec tables, which name the S3 Plus by name, are **wrong**. Consequence: every `Metrics::px` conversion yields fewer pixels and this panel rendered 19 % oversized until [#323](https://github.com/hleserg/Attadipa/issues/323) corrected that. [HARDWARE_MATRIX](HARDWARE_MATRIX.md#display-diagonal--resolved-154) |
| ~~D16~~ | **Nunito Sans Regular 400; arrows are icons.** The owner rejected the Montserrat Clock prototype as inconsistent with the canonical references and selected their rounded Nunito Sans direction on 2026-08-26. The generator pins the variable font to `wght=400`; U+2190–U+2193 remain A8 image-pipeline icons. | **RESOLVED 2026-08-26** | [FONT_MEASUREMENTS](FONT_MEASUREMENTS.md), generated-font provenance in `assets/fonts/README.md`, and physical Clock evidence in [CLOCK_2026-08-26](../hardware/CLOCK_2026-08-26.md) |
| D17 | **Render performance of the generated fonts.** Final §51 asks for it; licence, coverage, legibility and size are answered and this one is not | **UNKNOWN** | the simulator driving timed frames, or a board. Not guessed |
| ~~D11~~ | ~~Which AXP2101 rail is the schematic’s net `LDO5`?~~ | **RESOLVED — `BLDO2`** | the schematic read as a drawing, 2026-08-28 — [#314](https://github.com/hleserg/Attadipa/issues/314). AXP2101 pin 14 `BLDO2` runs to the port `LDO5`; on sheet 3 `LDO5` passes through the fitted `R78 0R` to pin 5 `EN` of the DRV2605, whose pin 3 `SDA` is `IO10` — consistent with the `0x5A` that answered the scan. The alternative feed, `+3V3` through `R132`, is marked `NC`. The row stayed open only because the `LDO5` **label text** sits two pixels from `ALDO4`’s row while its **wire** goes to pin 14; text position is not connectivity. The PMU register read this row asked for was never needed |
| D19 | **The Waveshare display-FPC part marking, read with a loupe.** Nothing has read the panel's own part number, and it is the only route to this panel's retention and lifetime specification. **U2 and U3 are not in this question and must not be added back to it**: both were answered by something stronger than a loupe — the die answering for itself over the board's own USB-Serial/JTAG port. `PSRAM_CAP = 8M` with `PSRAM_VENDOR = AP_3v3` makes U2 `ESP32-S3R8` and not `ESP32-S3R8V`, and `esptool flash-id` read `0xC8 0x4019` straight off U3, agreeing with the schematic's `GD25Q256EYIGR` — [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.2–1.3, which is why **D1** and **D12a** are struck through above. A lid marking is weaker evidence than a fuse read of the die under it | UNKNOWN | [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §5 open-question row 7 — *"the panel module's maker, part number, and therefore its own image-sticking and lifetime specification"*, resolved by *"Step 1's loupe on the module or its flex"*. **Cite that row and not Step 1's own prose**, which reads *"read the part markings on U2, U3 and the display FPC"* and would hand back the two parts this question has just carved out. An unreadable panel marking is the ordinary case and simply leaves this row UNKNOWN |
| D20 | **The Waveshare board's own revision, read off the unit.** The silkscreen reads `ESP32-S3-Touch-AMOLED-2.06`, which is the **product name** schematic V1.0 describes — `2.06` is the panel diagonal in inches, and a V1.1 of the same product would carry it unchanged. So every V1.0-derived row in [HARDWARE_MATRIX](HARDWARE_MATRIX.md) is confirmed against a document and not against the board in hand, which is exactly the distinction CLAUDE.md requires when it asks for *"a schematic for the specific board revision"*. Carved out of A1, which is answered as an owner question and struck above: the owner cannot answer this from memory, and it is not owed by them | UNKNOWN | [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §5 open-question row 7 — the same loupe pass as D19, and **cite that row rather than Step 1's prose**, for the reason D19 gives. [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §0 warns that the arrival photograph is not authoritative about anything needing magnification the camera did not have; a revision marker is text of that size. An unreadable or absent revision marker is the ordinary case for this vendor and simply leaves this row UNKNOWN — it does not become V1.0 by default |

## MeshCore

Answered on 2026-08-21 by reading the source at commit
**`d92964352441e53b93e8667b802e04f6e072b39e`** (branch `main`; tags
`companion-v1.17.1`, `repeater-v1.17.1`, `room-server-v1.17.1`). Every claim
below cites the file it came from. Licence: **MIT**, `license.txt`.

| # | Question | Status | Answer |
|---|---|---|---|
| ~~M1~~ | Architecture and integration points | **RESOLVED** | Arduino/PlatformIO throughout. There is no `CMakeLists.txt` and no `idf_component.yml` anywhere in the tree; `BaseSerialInterface.h` and `ContactInfo.h` include `<Arduino.h>` directly, and helpers depend on `Stream`, `File` and `HardwareSerial`. Clean dependency injection at the core: `mesh::Radio` is a pure-virtual interface in `src/Dispatcher.h:20-79` |
| ~~M2~~ | Which revision to pin | **RESOLVED — candidate** | `v1.17.1`. `origin/dev` is 29 commits ahead of `main` at that tag, and upstream asks for PRs against `dev`, so a pin to `main` at a release tag is the stable choice |
| ~~M3~~ | Crypto primitives and byte-level format | **RESOLVED — and it needs a review of its own** | Payload encryption is **AES-128 in ECB mode with zero padding**, on both the hardware (`Utils.cpp:61,92`) and the software path (`Utils.cpp:108-122`, `aes.encryptBlock` per 16-byte block, no IV, no chaining). Authentication is HMAC-SHA256 **truncated to two bytes** — `CIPHER_MAC_SIZE 2` in `MeshCore.h:17`, applied in `Utils.cpp:127-145`. Wire constants: `PUB_KEY_SIZE 32`, `CIPHER_KEY_SIZE 16`, `MAX_PACKET_PAYLOAD 184`, `MAX_PATH_SIZE 64`. On-air layout is `Packet.cpp:55-85` |
| ~~M4~~ | Threading and concurrency assumptions | **RESOLVED** | Cooperative single-loop, Arduino style. `CONTRIBUTING.md` requires no dynamic allocation outside `begin`/`setup`; fixed pools in `StaticPoolPacketManager.h`. The one FreeRTOS boundary is the BLE interface, guarded by a static queue (`src/helpers/esp32/SerialBLEInterface.h:24-35`, `FRAME_QUEUE_SIZE 4`) |
| M5 | Memory footprint on ESP32-S3 | PARTIAL | Fixed pools and `MAX_PACKET_HASHES (128+32)` in `SimpleMeshTables.h` make it computable, but no figure is claimed here without a build. `NOT MEASURED` |
| ~~M6~~ | How it abstracts the radio, and whether it covers all five T-Watch chips | **RESOLVED — and the answer was worse than expected** | Through thin wrappers over RadioLib in `src/helpers/radiolib/`. Across 87 upstream variants the `RADIO_CLASS` set is `CustomLR1110 · CustomLR2021 · CustomSTM32WLx · CustomSX1262 · CustomSX1268 · CustomSX1276` — of the five T-Watch candidates, **only the SX1262**. CC1101 is compiled out entirely (`platformio.ini:35`, `-D RADIOLIB_EXCLUDE_CC1101=1`). **Correction to an earlier version of this row**, which said RadioLib supports every chip MeshCore does not and concluded the gap is a small wrapper layer: RadioLib *drives* CC1101 and Si4432, but as **FSK/OOK** parts. Neither has a LoRa modulator, so no wrapper makes them mesh-capable. The gap is a wrapper for SX1280 and LR1121 only. [ADR-0003](../adr/0003-radio-not-lora.md) |
| ~~M7~~ | Companion protocol shape | **RESOLVED — and it largely already exists** | A framed byte protocol, the same *shape* on every transport — but **not the same capacity**, see M15. `>`/`<` sentinel, 16-bit little-endian length, payload; `MAX_FRAME_SIZE 176` (`BaseSerialInterface.h:5`) is the buffer and the protocol maximum, and over BLE the link delivers less. Payload is `[opcode][data]`, little-endian. The opcode table is `examples/companion_radio/MyMesh.cpp:6-134`. **Version negotiation already exists**: `CMD_DEVICE_QUERY` (22) carries the client's protocol version, the firmware stores it as `app_target_ver` and adapts its replies (`MyMesh.cpp:1023-1024`, and see the `app_target_ver >= 3` branches at 435 and 548) |
| M8 | Can Attadipa's needs be upstreamed rather than forked? | **likely yes** | The radio-wrapper gap (M6) is the natural candidate. Requires talking to upstream, which has not happened |
| ~~M9~~ | **Does MeshCore assume it owns the radio exclusively?** | **RESOLVED — effectively yes** | `src/helpers/radiolib/RadioLibWrappers.cpp:14` is `static volatile uint8_t state = STATE_IDLE;` — a **file-static** flag set from the ISR. One radio per firmware image, structurally. It also runs its own duty-cycle governor, `Dispatcher::updateTxBudget()` (`Dispatcher.cpp:38-53`), which an Attadipa coexistence coordinator would have to reconcile with rather than override. The sanctioned extension points are the virtual hooks `getCADFailMaxDuration`, `getCADFailRetryDelay`, `getAirtimeBudgetFactor` in `Dispatcher.h`, and `isReceiving()` in `RadioLibWrappers.h:44-48` |

**M9 matters less on one path and exactly as much as feared on the other.**
The concern was that a mesh stack owning the radio exclusively could not coexist
with a coordinator scheduling quiet windows around Wi-Fi and BLE. It does own it
exclusively. When the radio is in a **separate device**, the watch speaks the
companion protocol to a node and the conflict does not arise — a product
decision dissolving an engineering problem rather than solving it.

> **Corrected 2026-08-21.** What stood here went one step further and concluded
> that the watch therefore *never* runs MeshCore. That does not follow. A
> T-Watch with a supported radio is a local mesh device (final §13), and on that
> path M9 is a live constraint: whether `HardwareCoordinator` can schedule
> around MeshCore's radio ownership, or must stay out of its way, is part of the
> integration spike rather than an assumption. See
> [ADR-0008](../adr/0008-mesh-service-providers.md).

Also relevant on the local path: MeshCore runs its own duty-cycle governor,
`Dispatcher::updateTxBudget()`, which Attadipa's airtime accounting must
reconcile with rather than override.

### What reading MeshCore surfaced that nobody asked

| # | Finding | Evidence | Status |
|---|---|---|---|
| M10 | **The payload cipher is AES-128-ECB.** Identical plaintext blocks under one key produce identical ciphertext blocks, so equality of messages leaks even when content does not | `src/Utils.cpp:61,92` (CC310 path) and `:108-122` (software path) | **read from source** — implications for Attadipa not yet assessed |
| M11 | **The message authentication tag is 2 bytes.** One in 65 536 per forgery attempt, so the security of the tag rests on limiting attempts rather than on the tag | `MeshCore.h:17`, `Utils.cpp:127-145` | **read from source.** The owner's own node exposes a "Request Rate Limiter" — the two facts may well be related, and that is worth confirming rather than assuming |
| M12 | **`ed25519_verify` from the vendored `orlp/ed25519` is disabled upstream** with the comment *"memory corruption bug was found in this function!!"*. The active path uses `Ed25519::verify` from `rweather/Crypto` instead | `src/Identity.cpp:34-36` (`#elif 0` branch) | **read from source** |
| M13 | **There is almost no test coverage of the parts Attadipa depends on.** Seven test binaries, none touching crypto or wire format; `test/mocks/AES.h` is a no-op stub and `test/mocks/SHA256.h` is self-described as *"deterministic but not cryptographic"* | `test/` | **read from source.** Consequence: there are no reference vectors to port. The only usable one in the repository is the known-good keypair embedded in `Identity.cpp:68-110` |
| M14 | **`rweather/Crypto` licence is unverified.** MeshCore resolves it through PlatformIO as `rweather/Crypto @ ^0.4.0`; it is not in this project's clones and its licence file has not been read | `platformio.ini:24` | **UNKNOWN — must be checked before anything depends on it** |

### What a frame fits over BLE, and what is left of the question

Opened 2026-08-23 by [#143](https://github.com/hleserg/Attadipa/issues/143). The
model is corrected and the arithmetic is derived and executed — 176 is the
buffer and 173 is what an MTU-176 link delivers, which for a **vanilla** node is
the whole answer, since it has no chunked builder to subtract a header for. The
171 in the upstream evidence is a *derivative's* number. **Nothing was measured
here**; the losses are upstream's, on their boards and their BLE stack. The full
reading is
[MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md). These are what it
left open, and the first two need a board.

| # | Question | Status | Resolved by |
|---|---|---|---|
| M15 | **What ATT MTU does an Attadipa ESP32-S3 central negotiate with a MeshCore peripheral, and what actually happens to a 176-byte frame?** The peripheral requests 176; what a central settles on, and whether the stack truncates, refuses or splits, has never been observed here | **UNKNOWN** | one node, one central, payloads at 172–176 bytes, comparing exact received length and content — not that a command answered. §7 of the frame-capacity document, steps 1–3. `NOT EXECUTED — HARDWARE REQUIRED` |
| M16 | **Does vanilla's Bluedroid path behave as the derivative's NimBLE path was measured to?** The only measurement anyone has is `OffbandMesh/meshcore-firmware` on NimBLE. Our pin is on the Arduino core's bundled Bluedroid, and the two are different stacks | **ASSUMPTION** — truncation to `ATT_MTU − 3` is required by the Bluetooth Core specification, so both are *expected* to agree | the same bench session as M15. Until then it is an expectation with a specification behind it, which is not a measurement |
| ~~M17~~ | ~~Which stock commands can exceed the effective BLE frame ceiling?~~ | **ANSWERED from source** | Four: `logRxRaw` → `PUSH_CODE_LOG_RX_DATA` fills the frame to exactly 176; `onRawDataRecv`, `onControlDataRecv` and `onTraceRecv` guard against `sizeof(out_frame)` = 177 and so can build a frame every transport refuses. Everything else fits — `RESP_CODE_CONTACT` is 148. Whether `onTraceRecv`'s inputs actually reach 177 is a parser-bounds question and belongs to [#142](https://github.com/hleserg/Attadipa/issues/142) |
| M18 | **What detects a peer truncating our outbound data, and what recovers from it?** Neither protocol carries a total length or an integrity check above the frame. Every upstream diagnosis of this defect was arithmetic on a byte count after the fact — 147 = 3 × 49 — because nothing on the wire said the frame was short | **UNKNOWN, and it is a design question rather than a fact** | the companion client's own specification, when one exists. A client cannot fix the peer, so whatever it uses has to sit above the protocol. Not answerable by more reading |
| M19 | **What ATT MTU does Attadipa's own node-link service negotiate, if it uses BLE?** `link/` has a 199-byte maximum frame, but no local transport implementation or negotiated-MTU observation exists | **UNKNOWN** | the node-link transport bring-up must record the negotiated MTU and verify its maximum-frame handling; MeshCore's MTU request is not evidence for this service |

M17's answer carries a consequence worth stating separately: the vanilla path
that reaches the ceiling is `PUSH_CODE_LOG_RX_DATA`, whose payload is **the raw
bytes of a received LoRa packet**. A truncated frame there presents as a
malformed packet off the air, so a client must rule out its own link before it
says anything about the radio.

M10 and M11 are recorded as facts, not as accusations. MeshCore is solving a
different problem under tighter constraints, and a two-byte tag on a
duty-cycle-limited sub-GHz link is a defensible trade against airtime. But
Attadipa's specification treats security as something that must be strengthenable
without breaking the architecture (§74 item 24), and a protocol whose
authentication rests on rate limiting is a protocol whose rate limiter is a
security control rather than a convenience. That belongs in an ADR of its own,
with someone competent reviewing it — not in a paragraph here.

### What the parser-bounds review of 2026-08-23 could not close

Five parser defects at the pin were verified and are in
[VERIFIED_FACTS.md](VERIFIED_FACTS.md) and
[MESHCORE_PARSER_BOUNDS.md](MESHCORE_PARSER_BOUNDS.md). What follows is what that
work **failed** to establish, kept separate so that a verified over-read is never
read as a verified consequence.

**These four were filed as M15–M18 and are M20–M23.** The frame-capacity research
took M15–M19 on `main` while this branch was open, and two research runs in the
same week can pick the same next number without either being wrong. Renumbered
here on merge, 2026-08-25, rather than left as two answers to one identifier —
the same collision the independent review caught in the `T-` series, one section
along. Anything citing the old numbers from before that date means these.

| # | Question | Status | What would resolve it |
|---|---|---|---|
| M20 | **Do P3 and P4 actually run end to end through `Mesh::onRecvPacket`?** Both are proven at the function they live in — the `extra_len` underflow exhaustively, the `Utils::decrypt` over-write against the real translation unit. Neither has been driven through the packet path that reaches it | **UNKNOWN** | a host build of MeshCore with the genuine `rweather/Crypto` AES-128 and SHA-256 and the vendored ed25519, rather than this harness's stubs. That is a day of work and it would also give the project its first real MeshCore reference vectors, which M13 says do not exist |
| M21 | **Can an attacker steer P3's `extra_type` and `tag`?** They are read from `data[k..]`, past the decrypted length, so they are stale stack bytes rather than anything in the triggering packet. Grooming them with an earlier packet is plausible and untested. This is the difference between a conditional finding and a controllable one | **UNKNOWN** | the same build as M20, plus a two-packet sequence that fills the stack region and then triggers the underflow |
| M22 | **What do P4's eight bytes overwrite on an ESP32-S3?** The over-write leaves `uint8_t data[184]` in `Mesh::onRecvPacket`. What sits after it is a property of the stack frame the compiler chose for that target, and nobody here has compiled MeshCore for it | **UNKNOWN** | build MeshCore for an ESP32-S3 target and read the frame layout. Note that answering it does **not** need a board — this one is a compiler question, not a hardware one |
| M23 | **Are there more of these?** The corpus is hand-built from reading three parsers. It demonstrates; it does not search. `Utils::decrypt` was found by following a caller, not by the corpus, which is evidence that reading finds what a ten-case corpus does not | **UNKNOWN** | a real fuzzing pass over the pinned tree with the genuine crypto libraries. Scope it as its own task; do not fold it into a pin decision |

None of these blocks anything today, because Attadipa compiles no MeshCore code.
All four become entry conditions the moment a local MeshCore provider is real —
[MESHCORE_PARSER_BOUNDS.md](MESHCORE_PARSER_BOUNDS.md) §5.

One more, and it is not a MeshCore question: the three pull request authors each
state they verified on a Heltec V4, and none attaches a crash trace, a corpus or
sanitizer output. That is **an unverified author claim** and it is recorded as
one. This project has no Heltec V4 and independently confirming it is
**NOT EXECUTED — HARDWARE REQUIRED**.

## Architecture

| # | Question | Status | Resolved by |
|---|---|---|---|
| X1 | How does a capability express **variant** (which of five radios) and **degree** (accel-only vs 6-axis)? | **RESOLVED** | It does not — that is the wrong layer to ask. Variant and degree are facts about a *part* and live in the hardware inventory, below the service boundary; a product capability carries only an availability state. [ADR-0007](../adr/0007-two-capability-layers.md) |
| X2 | Who owns PMU rail sequencing — a rail service, or each driver? | UNKNOWN | ADR |
| X3 | How does an application render each of the seven availability states — and in particular tell *unsupported here*, *needs a node*, *node out of range* and *broken* apart? | **narrowed** | [ADR-0004](../adr/0004-capability-sources.md) sets one state per remedy; the screens themselves are still UX + API design together |
| X4 | Two RTC parts, two IMU parts, two audio paths — one interface each, or per-board? | UNKNOWN | driver design |
| X5 | Does the coexistence coordinator earn its complexity on boards with no measured interference? | UNKNOWN | H4 — build the measurement first, the mitigation second |

## Toolchain and dependencies

| # | Question | Status | Resolved by |
|---|---|---|---|
| T1 | Which ESP-IDF version to target | **narrowed** | Waveshare supports v5.5.5 and v6.0.2; its BSP needs ≥5.3. Decide with the LilyGO side. |
| ~~T2~~ | ~~Which LVGL major version~~ | **RESOLVED 2026-08-24** | **LVGL 9**, pinned at v9.5.0 = `85aa60d18b3d5e5588d7b247abf90198f07c8a63` in [DEPENDENCIES.md](DEPENDENCIES.md), verified by `git ls-remote` against `refs/tags/v9.5.0` and **OBSERVED** in CI from a cold cache. The *"confirm simulator support"* half is answered by the same observation: the simulator builds against that revision and renders a screenshot per geometry |
| T3 | Is RadioLib needed, or does MeshCore bring its own radio layer? | UNKNOWN | M6 |
| ~~T4~~ | ~~Simulator display backend~~ | **RESOLVED 2026-08-24** | **SDL2**. `sim/CMakeLists.txt:5` — "find_package(SDL2 REQUIRED)" — is exactly that; LVGL carries the SDL display, mouse and keyboard drivers in-tree and does not link SDL itself, which is why it is the one system package this build needs. CI installs `libsdl2-dev` and runs it headless on SDL's dummy video driver. *"SDL2 not currently installed"* was true of one machine, never of the decision |
| T5 | Host test framework | UNKNOWN | small decision, no ADR needed |
| ~~T6~~ | ~~Use the Waveshare BSP as a dependency, or take only its pin facts?~~ **This row asked the question about one vendor; [DEPENDENCIES](DEPENDENCIES.md) asked it about both. One question, two wordings, which is why it was answered ad hoc each time rather than closed. This is the row that moved; the `DEPENDENCIES` wording is the one that was answered.** | **RESOLVED 2026-09-01 — take the facts, not the dependency** | **[ADR-0017](../adr/0017-board-backends-compose-esp-idf-drivers.md)**: a board backend composes official ESP-IDF components and hands the runtime an `esp_lcd_panel_handle_t` and an `esp_lcd_touch_handle_t`; a vendor BSP is cited at a revision and never linked. Same answer for both vendors, and for the same reason. The evidence is [TWATCH_S3_PLUS_BSP_REUSE](TWATCH_S3_PLUS_BSP_REUSE.md) — decisively, `LilyGoWatchS3.cpp` at `38e6f8d` makes a missing PSRAM part an unbounded `while` loop (`:105-108`), a missing PMU an `assert(0)` (`:126-129`), and infers battery capacity from whether GNSS answered a probe, writing it to NVS (`:181-191`). What is genuinely reusable — pin facts, rail ordering, the panel command table — is data, and is taken as data |
| T7 | Does the LilyGO PlatformIO pin to IDF 4.4.7 constrain Attadipa? | ASSUMPTION: no | Attadipa is ESP-IDF-native and does not use the Arduino layer |

## Product

| # | Question | Status | Resolved by |
|---|---|---|---|
| ~~Q1~~ | ~~What should the Waveshare board *be*, given it cannot do mesh or navigation?~~ | **RESOLVED** | [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-1. The premise was wrong: it cannot do mesh or navigation *on its own*. With an Attadipa node attached it runs the same applications as a LoRa watch; without one it is a watch, an audio device, and whatever the installed applications make it |
| Q2 | ~~Is a magnetometer expected to be added externally~~, **or is heading GNSS-only on a stock board for good?** | **half answered 2026-08-22** | The first half is settled by A5 and by the same evidence: one is being added externally, to one unit ([#83](https://github.com/hleserg/Attadipa/issues/83)). The second half is **not** settled and is the part that was always the product question — a modified unit says nothing about what a stock board offers, and the firmware ships for stock boards. Restated rather than closed |
| Q3 | Realistic battery-life target | UNKNOWN | measurement, after bring-up |
| ~~Q4~~ | ~~How does an owner earn the right to provision a production watch — set its clock, keep its timezone, give MeshCore a passkey, recover from a changed node?~~ | **RESOLVED 2026-09-02** | the owner chose on-device entry: [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-26 and [ADR-0018](../adr/0018-owner-consent-for-provisioning.md), with [#356](https://github.com/hleserg/Attadipa/issues/356) carrying the implementation. The answer is **not** one of the options below — every one of them assumes a provisioning channel and the decision was to have none. The section keeps them for the two corrections noted there. **One clause of the question is not resolved and was struck through with the rest by mistake: *recover from a changed node*.** OD-26 and ADR-0018 decide the consent factor, and #356 builds the entry screen, but neither adds an operation that clears what a reset node invalidates — see **Q6** |
| Q6 | After a MeshCore node is factory-reset or reflashed, what returns a watch to service — and what exactly may that operation clear? | **UNKNOWN, and wider than it looked** | Split out of Q4 on 2026-09-02 by [#409](https://github.com/hleserg/Attadipa/issues/409). The blocking state is **two** items, not one: the stale bond, which `mesh-forget-bond` can delete in a HIL image, and the **pin**, which nothing in any image can clear. Re-entering a passkey cannot substitute for either — with a bond in the store the watch encrypts instead of pairing and the passkey is never consulted. Today the only recovery is `idf.py erase-flash`, which also takes the bonds and the time metadata. Evidence and the minimum atomic scope: [MESHCORE_NODE_RESET_RECOVERY.md](MESHCORE_NODE_RESET_RECOVERY.md). Resolved by an implementation issue and a bench run; the physical half is **NOT EXECUTED — HARDWARE REQUIRED** |
| Q5 | How does a power lease taken on one task take part in a sleep decision made on another? | **UNKNOWN** | engineering decision, deferred. Blocks [#367](https://github.com/hleserg/Attadipa/issues/367) item 7 only; consequence is zero until a plan suspends a domain a cross-task lease holds |

Q1 was a genuine product question, not an engineering one, and it was answered
on 2026-08-21 in a way that reframed it. The board is not a lesser device that
needs a purpose found for it; it is a device whose mesh and navigation arrive
over a link instead of over a bus. What was a gap in the product is now the
strongest argument for the capability model: two boards that share almost no
hardware run the same applications, because applications ask what the device can
do and never which device it is.

Q2 is the part of the compass question that OD-1 did *not* answer, and it got
sharper, and then on 2026-08-22 it got **split**. The owner named "компас" among
the applications the node enables. No board has a magnetometer. The original
framing was: either the node carries one — which would answer both Q2 and A5 —
or "compass" means GNSS course-over-ground, which only works while moving and
shows nothing at all when the user stands still. Those are different products and
the difference is visible to the user in the first ten seconds.

A5 has since been answered by a **third** route neither branch anticipated: the
owner is soldering a magnetometer into one unit. That answers "is one expected to
be added externally" — yes — and leaves the product question untouched, because
**a soldered part on one wrist is not a shipping capability**. A6 also remains
open and remains independent: node orientation is not watch orientation
([ADR-0009](../adr/0009-heading.md) §3), so a node's magnetometer answers a
different question than a wrist's does.

What Q2 now asks is the narrow, still-open thing: **on a board nobody has
modified, is heading GNSS-course-only for good?** That is a product decision and
the retrofit does not make it.

### Q4 — a production watch cannot be provisioned at all, and the missing piece is a consent rule

**ANSWERED 2026-09-02 by the owner — and the answer was not in the table below.**
He chose **on-device entry**: the holder types the value on the watch itself.
[OD-26](OWNER_DECISIONS.md#od-26--owner-consent-for-provisioning-is-a-finger-on-the-watchs-own-screen)
records the decision, [ADR-0018](../adr/0018-owner-consent-for-provisioning.md)
the reasoning. #356 carries the implementation; this entry stays for the
reasoning it holds and is no longer a question.

Two things in what follows are worth reading against that, rather than deleted:

- **Every row below assumes a provisioning channel exists** and asks who may
  open it. The decision was to have none — nothing in a product image accepts
  provisioning input except the panel — so the answer sits outside the table
  rather than in it. The letters also collide: this table's **A** is roughly
  ADR-0018's **C**, and neither of its A/B/C means the other's.
- **Row A prices its gesture as already paid, and that is true of one of the two
  keys.** "The button path and its debounce already exist in
  `physical_input.cpp`" is true of BOOT, which is a GPIO. It is not true of the
  power key: PWR reaches
  the AXP2101 `PWRON` pin and never a GPIO, so press *duration* is PMU register
  policy and whether a long press can be reported to firmware at all is
  **UNKNOWN** — `docs/testing/WATCH_CONTROL.md:101` — "so on a device a held power key may be a shutdown rather than an event".
  Any future option resting on a held-key gesture has to close that first.

Verified in this tree at `144459f`, not inferred from the issue that predicted
it. The production image is the one built from `sdkconfig.defaults` alone, and
that file carries `firmware/sdkconfig.defaults:89` — "CONFIG_ATTADIPA_WATCH_CONTROL=n".
Everything that provisioned a watch sat behind that symbol, and #356 moved
each piece out from behind it in the order below — the tense of each bullet
is the record of what was true at `144459f` and what changed it:

- **The clock could not be set.** `write_rtc()` was reached only through
  `provision_time()`, whose one instantiation was inside the
  `#if CONFIG_ATTADIPA_WATCH_CONTROL` block. #356's first change made the
  sequence compile in every image; its second gave a product image the caller:
  `firmware/main/waveshare_board.cpp:443` — "class BoardProvisioner final : public attadipa::core::Provisioner {"
  is ungated and is reached from the entry screen a long press on the clock
  opens (`firmware/main/waveshare_board.cpp:839` — "void long_press(lv_event_t *) {").
  A board off the shelf still shows whatever its RTC powered up with until
  somebody holding it enters the date and time — which is what ADR-0018 chose.
- **The timezone could not be kept,** for the same reason, and for the same
  reason it now is: the offset is the third field of that screen and goes
  through the same `provision_time()` as the clock.
- **MeshCore never scanned.** `configure_meshcore_ble()` is the only writer of
  the passkey key. #356's first change made boot replay a stored passkey
  through the same `Configure` event, and its second lets the fourth field of
  the entry screen store one:
  `firmware/main/waveshare_board.cpp:474` — "set_mesh_passkey(std::uint32_t passkey) override {".
  With nothing on flash and nothing entered the worker's
  `firmware/main/meshcore_ble.cpp:1185` — "if (configured.load()) start_scan();"
  is false forever, which is now the same "not set up yet" as a blank clock
  rather than a product that cannot be set up.
- **A changed node cannot be recovered from, and this bullet understated it.**
  `meshcore_ble_forget_bond()` has the same single gated caller, which is what
  the sentence said. What it did not say is that ungating that caller would not
  finish the job: the bond is one of two things a reset node invalidates, and
  the other is the **pin**, which no image can clear at all. Deleting the bond
  re-arms one pairing; given the node's current digits the watch then pairs,
  reads the reset node's new public key, and
  `firmware/main/meshcore_node_pin.h:200` — "return PinOutcome::Refused;" turns
  it away for good. The single writer of that key is
  `firmware/main/meshcore_ble.cpp:389` — "nvs_set_blob(handle, kNodeKeyNvsKey"
  and there is no eraser; the file's one `nvs_erase_key` names the passkey
  instead. So this is not "the product image lacks a surface the HIL image has";
  no image has the operation. Traced in
  [MESHCORE_NODE_RESET_RECOVERY.md](MESHCORE_NODE_RESET_RECOVERY.md) §4, and it
  is now **Q6** above rather than a clause inside Q4.

So the gap #346 opened when the unauthenticated USB control plane left the
product image was wider than "time is not settable": the mesh half of the
product does not run at all without it. That gap is what OD-26 answers, and
#356 is the work that closed it; what remains hardware-untested is listed in
that PR, not here.

What was missing was not a mechanism. It was a rule about **who may put a watch
into provisioning mode**, and that is a product decision, not an engineering
one: every option below is implementable, and they differ in what a stranger
holding your watch — or a cable — can do to it. The rule chosen makes the
question narrower than any of them: there is no mode to put the watch into, and
a stranger with a cable can reach no endpoint, while a stranger holding the
watch can do everything — possession is the whole factor, which ADR-0018
records. One clause of that is weaker than it sounds and belongs here rather
than in a review: what is provisioned is stored in plain NVS, this project
builds with no flash or NVS encryption and will not, since `AGENTS.md` forbids
burning eFuses, and a full flash read over that same cable is documented on this
unit: `docs/research/WAVESHARE_BOARD_RECEIVED.md:314` —
"read_flash 0 0x2000000 waveshare-2.06-factory.bin".
So the factor is possession of the watch **or** of a cable and esptool.
Every option stores the same secret, so this separates none of them; it bounds
all of them.

| Option | Security | UX | Implementation |
|---|---|---|---|
| **A. Physical consent window.** A button gesture on the watch opens a short provisioning window; only inside it does a provisioning channel answer | Strongest. Possession of the watch is required, and the window bounds the exposure. Does not protect against someone holding the watch | One gesture to learn, and it must be discoverable or the owner is locked out of their own product | Smallest. The button path and its debounce already exist in `physical_input.cpp`; a window is a deadline and a flag |
| **B. Authenticated channel, no physical step.** A pairing secret provisioned at manufacture or first boot; the channel answers whoever proves it | Depends entirely on where that secret lives. A secret in the image is not a secret; a per-unit secret needs a provisioning step of its own, which is this question again | Best: provision from a phone or host without touching the watch | Largest. Key storage, a pairing protocol, and a factory or first-boot step this project does not have |
| **C. Physical consent *and* an authenticated channel.** A opens the window, B proves who is inside it | Strongest available, and the only one that survives both a stranger with a cable and a stranger with the watch | Two steps, and both must work on a watch with no keyboard | A plus B. Correct, and it cannot ship before B does |
| **D. First-boot window only.** An unprovisioned watch answers freely until it is first provisioned, then never again | Weak in the window and strong after. The window is exactly when a watch is most likely to be in transit | Invisible when it works; a factory reset becomes the only recovery, and that is a data-loss path | Small, but the "then never again" latch has to survive a flash erase to mean anything, and on this part it does not |

Two constraints are already settled and bound whichever is chosen. The channel
must not be the current USB control plane as it stands — `Kconfig.projbuild`
says of it "Any host that can open the port can use all of it; there is no
authentication". And a provisioning command must report terminal success, not
enqueued success: [ADR-0015](../adr/0015-transport-session-ownership.md) already
draws that line for the transport, and `meshcore_ble_forget_bond()` is the
existing example of a request whose answer is a queue post — the defect
[#378](https://github.com/hleserg/Attadipa/issues/378) is open against, so read
this as naming the shape rather than as a claim about today's code.

**This is not a blocker for power, radio or bring-up work**, and it was not
treated as one: nothing below the application layer needs the answer. It blocks
shipping a watch to a person who is not holding a build environment.

### Q5 — a lease taken on one task, read by another, and a sleep decision in between

`PowerOwner` is single-task by contract
([`core/include/attadipa/core/power_owner.h:310`](../../core/include/attadipa/core/power_owner.h) —
"// **Not thread-safe, and deliberately so.** Every `acquire()`, `release()` and").
Issue [#367](https://github.com/hleserg/Attadipa/issues/367) item 7 asks the BLE
transport to declare a lease, and the transport does not run on the task that
sleeps. That is the whole of this question, and it is not the data race it looks
like at first.

**The sequence that has no lock-shaped answer.** The sleeper reads `held()`,
finds nothing that blocks its plan, and commits. Between that read and the
hardware actually stopping, the other task acquires a lease. The sleeper has
already stopped looking. A mutex around the lease table serialises the *table*
and changes none of it, because the sleeper cannot hold a lock across the sleep:
it is inside `esp_light_sleep_start()` for as long as the sleep lasts, and a
task blocked on a lock there is a task that cannot un-take the decision anyway.

**Why nothing is broken today, stated so the reason is checkable rather than
reassuring.** The only sleep plan the firmware issues suspends the display and
nothing else
([`firmware/main/physical_input.cpp:189`](../../firmware/main/physical_input.cpp) —
"plan.suspend = attadipa::core::domain_bit(attadipa::core::PowerDomain::Display);"), and the refusal it could
trip is an intersection
([`core/src/power_owner.cpp:382`](../../core/src/power_owner.cpp) —
"static_cast<std::uint16_t>(leases_.held() & (plan.suspend | plan.rails_off));").
A `NodeLink` lease does not intersect `Display`, so the check that could be
raced cannot fire whichever way the race lands. The gap is real and its
consequence is currently zero.

**What answering it costs, which is why it is a question and not a task.** The
lease has to participate in the sleep decision itself — the sleeper publishes an
intent, a late acquire either refuses or aborts it, and every consumer learns a
protocol. That is a general power-management framework, and this project has
one standing instruction against building one before a consumer needs it
([ADR-0016](../adr/0016-one-power-owner.md) — the smallest mechanism the current
product needs). The honest state is: the contract says single-task, the first
cross-task consumer brings the answer, and until one exists there is nothing to
measure a design against.

**Not a blocker.** It blocks #367 item 7 and nothing else. Radio, GNSS and
T-Watch bring-up all reach a working device without it; each of them arriving
with a lease is what will make the question answerable, because only then is
there a plan whose `suspend` set a cross-task lease can intersect.


---

## Automation

### How does a producing agent authenticate when it files a task?

**Status: the failure is REPRODUCED; the route ChatGPT will use is still UNKNOWN.**
This is the one thing standing between the queue working and the owner still
being in the loop.

The intake gate trusts the **actor**, not the marker — `producer: chatgpt` is a
data field anybody can type, and write access is not. It rejects, by design, any
login ending in `[bot]`, plus `claude` and `github-actions`, because a Claude
comment mentioning `@claude` would otherwise start a Claude run that comments.

That guard is right and must stay. But it means the producer's **route** decides
whether the loop closes:

| ChatGPT files through | Actor the gate sees | Outcome |
|---|---|---|
| a user account with `write`/`maintain`/`admin` | that user's login | accepted |
| a GitHub App | `something[bot]` | rejected — correctly, by the bot guard |
| an account with only `read` or `triage` | that login | rejected on permission |

Until an issue has actually been filed the way ChatGPT will file it, which row
applies is a guess. But **the middle row is no longer hypothetical.**

### The reproduction, 2026-08-21

[Issue #10](https://github.com/hleserg/Attadipa/issues/10) was filed with a
valid marker through the GitHub API by an agent session. Gate log, run
`32475652479`:

```
EVENT_NAME: issues
ACTOR: claude[bot]
ACTION: opened
##[notice]#10 actor claude[bot] is a bot
```

The credential was a **GitHub App installation token**, so GitHub attributed the
issue to `claude[bot]` regardless of which account it was issued for. Issue #5,
opened by `hleserg` as a `User` with association `OWNER`, was accepted the same
day. The difference is the route, not the marker and not the content.

What makes it worse than a refusal: `author_association` on #10 is `NONE`, so
`agent-queue-watchdog.yml` skips it too — it filters on `OWNER`, `MEMBER` or
`COLLABORATOR`. **The task was invisible to every part of the pipeline at once,
and the workflow run went green.**

### The decision this needs

| | Option A | Option B |
|---|---|---|
| **Route** | ChatGPT files through a user account with `write` or better | ChatGPT files through a GitHub App |
| **Change needed** | none; works as built | the gate grows an owner-controlled allowlist of trusted producer apps, empty by default |
| **Cost** | a second GitHub account, or the owner's own | configuration surface on the one boundary the security model rests on |

**Recommended: A.** The gate's entire argument is that write access cannot be
typed, and an allowlist replaces that with a name that can. If B is chosen, the
allowlist must apply to `issues` events only — never comments, which is where
the loop lives — and `claude` and `github-actions` must never be listable,
because those are this repository's own output.

**Not decided by an agent.** Widening this boundary is the owner's call.

**What has been done about it:** a refusal of a marked task is no longer silent.
The gate comments once on the issue naming the guard that rejected it and the
actor it saw, and applies `needs-owner`. So the failure is now loud on the first
occurrence instead of being an issue nobody picks up.

**What would settle it:** one issue, filed by ChatGPT through whatever route it
will really use, and the resulting run. Either an agent starts, or the refusal
comment names the actor — and either way the answer is on the issue.

---

## Recently resolved

Moved to [VERIFIED_FACTS.md](VERIFIED_FACTS.md) on 2026-08-21: both boards'
complete peripheral inventory, pin maps, I2C addresses and PMU rail map; the
absence of a sub-GHz radio and GNSS on the Waveshare board; the absence of a
magnetometer on both; the five radio and two GNSS variants of the T-Watch; the missing touch
reset line; the haptic rail gating; the incomplete vendor BSP; and the
CO5300 / SH8601 driver nuance.
