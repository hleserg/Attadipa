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

An UNKNOWN that blocks work is a blocker — record it in
[../../TASKS.md](../../TASKS.md) in the blocker format rather than coding past it.

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
| A1 | Does the developer have either board physically, and which revision? | **UNKNOWN** | ask the project owner — **asked as [#54](https://github.com/hleserg/Attadipa/issues/54)** |
| A2 | If a T-Watch is present: which of the five radio chips, and which of the two GNSS modules? | **UNKNOWN** | inspect the unit / order details — **asked as [#54](https://github.com/hleserg/Attadipa/issues/54)** |
| A3 | Is there a second radio-capable device, so mesh can be tested at all? | **UNKNOWN** | ask the project owner — **asked as [#54](https://github.com/hleserg/Attadipa/issues/54)** |
| A4 | Which regulatory region governs LoRa operation here? | **CLOSED — not this project's to answer** | [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-14, 2026-08-22: asked as [#55](https://github.com/hleserg/Attadipa/issues/55), and the owner declined to name one — *"legality is my problem, not the firmware's."* No country or region is coming; nothing here researches a specific jurisdiction's rule table. [ADR-0006](../adr/0006-settings-and-bounded-values.md)'s transmit-closed-while-`Unknown` gate is unchanged by this and still applies to whoever configures the device |
| A5 | **Is an external magnetometer intended at all?** Neither board has one, so every compass feature in the plan currently has no hardware to run on | **ANSWERED 2026-08-22 — yes** | the owner ordered a **CJMCU-9911 (AK09911C)** and a **GY-271 (QMC5883L)** and is soldering one into the Waveshare unit ([#83](https://github.com/hleserg/Attadipa/issues/83)). The five epics are dormant, not dead. Which part, and where it sits, are open — [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md). **This does not change any board's capabilities**: a stock unit still has no magnetometer and the firmware still has to run on one |
| A6 | **Does the Attadipa node carry a magnetometer?** | **UNKNOWN** | ask the project owner. Note that "yes" does *not* give the watch a compass: a node's magnetometer measures the node's orientation, and [ADR-0009](../adr/0009-heading.md) refuses to present `NodeBody` heading as `WatchBody` heading without a known, calibrated, still-valid transform. The ADR exists so that this answer does not arrive before the model does — **asked as [#56](https://github.com/hleserg/Attadipa/issues/56)** |
| ~~A7~~ | ~~Which orange, and which olive?~~ | **RESOLVED** | the project owner, on [issue #57](https://github.com/hleserg/Attadipa/issues/57), 2026-08-22: §42 wins — Attadipa Orange `#FF8A40`, Ink Olive `#2F3A2E`. The sampled brand-art values that lost have left [`../../pics/README.md`](../../pics/README.md) and are recorded in [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-15 |
| ~~A8~~ | ~~May the icon and favicon be re-exported with transparent corners?~~ | **RESOLVED — yes** | the project owner, same issue. `pics/Ikon.png` and `pics/Favicon.png` are re-exported RGBA with transparent corners; the pixels inside the rounded square are unchanged. OD-15 |
| A9 | **Does the day theme keep its near-white page on the AMOLED board?** The Waveshare panel is emissive: every lit pixel draws its own current and ages in proportion. Rendered on the 410×502 face, the day theme's mean per-subpixel drive is 4.2× the night theme's on the raw 8-bit mean and 13.9× gamma-decoded (ESTIMATED — no panel, no efficiency curve; method in [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §1). The T-Watch's IPS panel does not care, so this is the first design question whose answer differs by board | **UNKNOWN** | the project owner. Four options and their costs are tabulated in [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §1. Not an engineering call: it decides whether the two boards look like one product — **asked as [#52](https://github.com/hleserg/Attadipa/issues/52)** |
| A10 | **What does Attadipa do about static content on the AMOLED?** Adjacent to A9 and not the same question: a uniformly bright page ages the panel evenly, while a dark page with bright static elements leaves their shape behind, and a mitigation for one does nothing for the other. The CO5300 has no pixel-shift and no scroll command, its Auto Current Limit (`55h`) defaults to disabled and no driver in the ecosystem writes it, and the vendor BSP boots the panel at 100 % brightness with the hardware dimming ramp turned off | **UNKNOWN** | the project owner. Six options with their costs are tabulated in [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.5. Should not be answered before §5 steps 7 and 8 have been run — today both sides of it are unmeasured — **asked as [#53](https://github.com/hleserg/Attadipa/issues/53)** |

A1 and A2 gate all bring-up, the entire interference matrix, and every power
number. A2 in particular decides whether the radio is sub-GHz or 2.4 GHz —
which changes region rules and mesh interoperability, not just a driver.

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
| H8 | **Is ALDO1 the `+3V3` rail?** The vendor doc says ALDO1 is unused; the schematic shows it driving `+3V3` | **CONFLICTING** | read the AXP2101 rail-enable and voltage registers on a powered board, then cut one rail at a time and watch which parts drop off the I2C scan |
| H9 | Real backlight current vs brightness, against the schematic's 45 mA at full | UNKNOWN | measurement; the 45 mA figure is a datasheet-level I_F, not a measured draw |
| H10 | **The speed gate below which GNSS course-over-ground is not trustworthy** | UNKNOWN | measurement on the fitted module. It depends on the update rate and on whether the module reports Doppler-derived velocity or differenced positions, so it is per-module and cannot be chosen. [ADR-0009](../adr/0009-heading.md) §4; final §26 forbids inventing settling intervals |
| H14 | **Which QMI8658 is on the Waveshare, and which of its datasheets describes the silicon?** Two halves. (a) *A or C* — [TAGS_TRACKS_RECKONING §2.2](TAGS_TRACKS_RECKONING.md) reports the schematic naming `QMI8658C` twice, so this is evidenced as **C** and only needs confirming, not answering. (b) *Which C document* — and this half is open and consequential: [PEDOMETER_PARTS §2.2](PEDOMETER_PARTS.md) reads `13-52-27` Rev A (2022-06-20), where chapter 11 documents a complete hardware pedometer, while TAGS §2.2 reports the only obtainable C datasheet as Rev 0.6 (2021-01) marked ADVANCE INFORMATION, whose `CTRL8` is *"Reserved: Not Used"* and which documents **no pedometer**. Those describe different parts. This is [ADR-0003](../adr/0003-radio-not-lora.md)'s shape in a second subsystem — the part name does not tell you whether the feature exists — and OD-6 makes the pedometer mandatory | **CONFLICTING** (was UNKNOWN) | one bench session answers both halves and outranks both documents: read `WHO_AM_I` (`0x00`), then set `CTRL8.Pedo_EN` and read `0x5A`–`0x5C` while walking the board across a desk. §5 step 6 of [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md). Blocks T-061's Waveshare half; if the registers do not respond, the step counter is firmware there and the power budget changes |
| H15 | **What is the IMU's axis orientation relative to the wearer, on each board?** A step counter tolerates a wrong sign; a wrist-raise gesture and any future orientation feature do not. [PEDOMETER_PARTS.md](PEDOMETER_PARTS.md) §1.9 notes that on the BMA423 axis remapping applies to the *features* only, so getting it wrong is silent rather than obviously broken. **Half answered on the Waveshare, 2026-08-22:** the board-frame triad is silkscreened beside the IMU — X toward the battery edge, Y toward the USB-C edge, Z as ⊙ out of the back face ([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.6). What is still missing is how the board is rotated inside the case, and one is useless without the other. Nothing is known for the T-Watch | **PARTIAL** (was UNKNOWN) | tilt the **assembled** watch through known angles and read raw axes — the board frame is printed, the case rotation is not. Cheap now that a Waveshare is on the desk; still impossible for the T-Watch |

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
| D5 | Waveshare button/wake inputs — BSP declares none; is that the board or the BSP? | **PARTIAL — it is the BSP, and the count is now settled at two** | The owner counted **two** pressable buttons on the assembled case, 2026-08-23 (S9). The schematic's *"at least two"* is therefore exactly two, which is the useful part: the drawing names `Key1`, `Key3` **and** `PWRON`, so at most two of those three reach a finger. Which GPIO each key uses, and **which of the two is `PWRON`**, are both still unresolved — and the second one is not a wiring detail, because `PWRON` is a PMU input that can wake the SoC from a state a GPIO cannot |
| D6 | T-Watch: which PMU rail powers GNSS on the *specific* unit (BLDO1 vs DC3) | UNKNOWN | inspect the unit for rear BOOT/RST buttons |
| D7 | Exact ST7789V3 and CO5300 init sequences and their timing | UNKNOWN | vendor driver source |
| D8 | Is the T-Watch main I2C bus shared with anything timing-sensitive? | PARTIAL | schematic read: five devices confirmed on SDA 10 / SCL 11, plus a possible sixth — see D9. Timing sensitivity still needs driver review |
| D9 | **Does the GNSS daughterboard connect the `MIA-M10Q` `SDA`/`SCL` to the FPC?** If it does, the GNSS is a sixth device on the main I2C bus at 0x42 | UNKNOWN | trace the daughterboard FPC net list, or scan the bus on a board with the module fitted |
| D10 | **What is radio `DIO3` (GPIO 6) for on this board — TCXO supply or a second interrupt?** | UNKNOWN | HPD16B3 module datasheet + the vendor radio driver's `setDio3AsTcxoCtrl` usage |
| ~~D12~~ | ~~**Is the PSRAM quad or octal — on *both* boards?**~~ **Split.** It was one question only because both boards carry an `ESP32-S3R8` marking, and that shared premise does not survive contact with the sources: the marking settles the *part*, and the part is octal. See D12a and D12b | **SPLIT** | [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.1 |
| ~~D12a~~ | ~~**Waveshare: quad or octal?**~~ **Octal.** ESP32-S3 Series Datasheet v2.2 §1.2 Table 1-1 lists `ESP32-S3R8` as `8 MB (Octal SPI)`, and no 8 MB quad in-package variant exists in that table — the only quad in-package parts are the 2 MB `RH2`, `R2` and `FH4R2`. Footnote 3 names `R8`, `R8V` and `R16V` as the octal set; `R8` and `R8V` differ by `VDD_SPI` voltage, not bus width. Corroborated by five of the six vendor examples shipping `CONFIG_SPIRAM_MODE_OCT=y` with `CONFIG_SPIRAM_IGNORE_NOTFOUND` unset — a build that aborts at boot if octal is not found — and by GPIO33–37 sitting unrouted on the schematic, which is where octal PSRAM's DQ4–DQ7 and DQS go | **RESOLVED** | traced, not recollected: [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.1. Step 4 of §5 confirms it empirically by reading the boot log's `octal_psram` tag. **Empirically closed 2026-08-22 without needing that step**: the die's fuses read `PSRAM_CAP = 8M`, `PSRAM_VENDOR = AP_3v3` (so `R8`, not the 1.8 V `R8V`) and `PIN_POWER_SELECTION = VDD_SPI`. The eFuse gives capacity and rail, not bus width — the step to octal stays Table 1-1's — but both legs now have evidence. [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.2 |
| D12b | **T-Watch: quad or octal?** The same part marking implies octal by the same table, but the LilyGO vendor document describing it as QSPI has not been re-examined and stands as a live conflicting source. Nobody has read that document against Table 1-1 | **CONFLICTING** | that board's own `esptool.py flash_id`, or the LilyGO document read against the datasheet. Do not assume D12a transfers |
| D13 | Waveshare: which loads sit on ALDO1, ALDO2 and ALDO3 — all three are 3.3 V — and **what runs on the 1.8 V ALDO4 rail**? | UNKNOWN | read the schematic sheets visually |
| D14 | Waveshare SD card: the BSP uses SDMMC 1-bit on GPIO 1/2/3, but the schematic labels those nets `MOSI`/`SCK`/`MISO` and shows a chip-select near GPIO 17. Which mode is the board actually wired for? | UNKNOWN | schematic sheet + BSP source |
| D15 | **What is the T-Watch panel's physical diagonal — 1.3" or 1.54"?** 240 × 240 is certain; the size is not. LilyGoLib's spec tables say 1.3 Inch for the S3 *and* the S3 Plus; the schematic's LCD sheet says `QT154C2408` / `LCD_1.54-TOUCH`, and that vendor's sibling part `QT154H2201` is specified as 1.54", 240×240, ST7789V — so the `154` field decodes. It decides dpi (261 vs 220) and therefore every physical-size conversion in the design system | **CONFLICTING** | a ruler on a physical unit (rides on A1), or the `QT154C2408` specification itself, which is not published anywhere reachable. Working value is 1.3", chosen as the conservative direction — [HARDWARE_MATRIX](HARDWARE_MATRIX.md#display-diagonal--conflicting) |
| D16 | **Which font — Inter or Nunito Sans — and what happens to the arrows?** A design decision, not a research gap: the numbers exist ([FONT_MEASUREMENTS](FONT_MEASUREMENTS.md)). What makes it a decision rather than a preference is that Nunito Sans has **no** U+2190–U+2193, so choosing it also chooses to make arrows icons in the image pipeline (T-034); and that its variable default is ExtraLight, so "use Nunito Sans" has to name a weight. A third input, MEASURED since: at the same `--size` Nunito Sans asks for 2–4 px more line height and draws a slightly smaller letter, so the two are not comparable at equal size and every byte figure moves with the size that matches | **needs the owner** | the owner's design boards name both. Blocks nothing in M1 — the pipeline works for either — but it blocks freezing the design tokens (T-009) |
| D17 | **Render performance of the generated fonts.** Final §51 asks for it; licence, coverage, legibility and size are answered and this one is not | **UNKNOWN** | the simulator driving timed frames, or a board. Not guessed |
| D11 | Which AXP2101 rail is the schematic's net `LDO5`? It feeds DRV2605 `EN`, and the vendor rail map says BLDO2 — consistent but not proven | UNKNOWN | PMU register read on hardware |

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
| ~~M7~~ | Companion protocol shape | **RESOLVED — and it largely already exists** | A framed byte protocol, identical across every transport. `>`/`<` sentinel, 16-bit little-endian length, payload; `MAX_FRAME_SIZE 176` (`BaseSerialInterface.h:5`). Payload is `[opcode][data]`, little-endian. The opcode table is `examples/companion_radio/MyMesh.cpp:6-134`. **Version negotiation already exists**: `CMD_DEVICE_QUERY` (22) carries the client's protocol version, the firmware stores it as `app_target_ver` and adapts its replies (`MyMesh.cpp:1023-1024`, and see the `app_target_ver >= 3` branches at 435 and 548) |
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

M10 and M11 are recorded as facts, not as accusations. MeshCore is solving a
different problem under tighter constraints, and a two-byte tag on a
duty-cycle-limited sub-GHz link is a defensible trade against airtime. But
Attadipa's specification treats security as something that must be strengthenable
without breaking the architecture (§74 item 24), and a protocol whose
authentication rests on rate limiting is a protocol whose rate limiter is a
security control rather than a convenience. That belongs in an ADR of its own,
with someone competent reviewing it — not in a paragraph here.

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
| T2 | Which LVGL major version | **narrowed** | Waveshare BSP accepts `>=8,<10`; LVGL 9 is the forward choice. Confirm simulator support. |
| T3 | Is RadioLib needed, or does MeshCore bring its own radio layer? | UNKNOWN | M6 |
| T4 | Simulator display backend | UNKNOWN | follows T2; SDL2 not currently installed |
| T5 | Host test framework | UNKNOWN | small decision, no ADR needed |
| T6 | Use the Waveshare BSP as a dependency, or take only its pin facts? | UNKNOWN | it is Apache-2.0 and incomplete — a reuse-ledger decision |
| T7 | Does the LilyGO PlatformIO pin to IDF 4.4.7 constrain Attadipa? | ASSUMPTION: no | Attadipa is ESP-IDF-native and does not use the Arduino layer |

## Product

| # | Question | Status | Resolved by |
|---|---|---|---|
| ~~Q1~~ | ~~What should the Waveshare board *be*, given it cannot do mesh or navigation?~~ | **RESOLVED** | [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-1. The premise was wrong: it cannot do mesh or navigation *on its own*. With an Attadipa node attached it runs the same applications as a LoRa watch; without one it is a watch, an audio device, and whatever the installed applications make it |
| Q2 | ~~Is a magnetometer expected to be added externally~~, **or is heading GNSS-only on a stock board for good?** | **half answered 2026-08-22** | The first half is settled by A5 and by the same evidence: one is being added externally, to one unit ([#83](https://github.com/hleserg/Attadipa/issues/83)). The second half is **not** settled and is the part that was always the product question — a modified unit says nothing about what a stock board offers, and the firmware ships for stock boards. Restated rather than closed |
| Q3 | Realistic battery-life target | UNKNOWN | measurement, after bring-up |

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
