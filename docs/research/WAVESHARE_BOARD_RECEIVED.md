# The Waveshare board, as it actually arrived

**Date:** 2026-08-22. **Unit:** one `ESP32-S3-Touch-AMOLED-2.06`, received by
the owner, opened, photographed, and still on the desk.

Everything this repository has said about this board until today came from a
schematic PDF, a vendor BSP and a wiki. Those are good sources and most of what
they said survives. This file records what changed when a physical unit was
opened, and it is deliberately separate from
[WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md), which is the *preparation* document —
what we expected, in what order to bring it up, and what we knew we did not
know. This one is the first page of the other column: what was observed.

---

## 0. Provenance, and what a photograph is worth

**Source:** four photographs taken by the owner on 2026-08-22 — the assembled
watch running its factory demo, the inside of the back cover, the battery cell,
and the mainboard from the side facing the back cover. They were examined at
full resolution, with the regions of interest cropped and upscaled.

A photograph is a real source and it is not a datasheet. It is authoritative
about **silkscreen**, because silkscreen is printed text put there by the people
who made the board; about **populated or not**, because an empty footprint is
visible; and about **labels on parts large enough to read**. It is *not*
authoritative about anything requiring magnification the camera did not have —
the SoC's variant suffix, the flash's capacity digits, or the IMU's own marking
are all present in the frame and none is legible.

Where a photograph and the schematic agree, that is corroboration and it is
worth having: it means the schematic we read describes the board we own, which
was an assumption until today. Where the photograph shows something the
schematic never mentioned, it is new. Where the photograph cannot resolve
something, this file says `UNKNOWN` and names the bench measurement that would
settle it, exactly as if no unit had arrived — a blurry photograph is not
evidence and must not be written up as if it were.

---

## 1. What the unit settles

### 1.1 It is the board we read the schematic for. `VERIFIED`

The mainboard carries `ESP32-S3-Touch-AMOLED-2.06` in silkscreen along the
bottom edge, next to the expansion pad row. This is the revision named in
[HARDWARE_MATRIX](HARDWARE_MATRIX.md) — schematic
`ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0`. Everything downstream of "the
schematic describes our board" is now standing on an observation instead of an
assumption.

### 1.2 The battery is *marked* 400 mAh, and that marking is now the headline

`VERIFIED` — the label was read off a received unit. **It is not verified that
the cell holds 400 mAh, and the arithmetic says it probably does not**: that
capacity in this footprint would need 132.3 mAh/cm³ against an 87–102 band
across 51 datasheet cells, so 250–310 mAh is the honest expectation,
`ESTIMATED`. See [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1; T-106 M3 settles it
with a scale.

The cell is marked:

```
402728
2026.07.11
3.7V 400MAH
```

`402728` is the usual pouch-cell geometry code: **4.0 mm × 27 mm × 28 mm**. The
date is a manufacturing date, not an expiry.

`HARDWARE_MATRIX` recorded this row as *"present, on connector `BAT1` via the
AXP2101 charge path; capacity not stated | UNKNOWN"*. It is now known, and it is
**less than half** the T-Watch S3 Plus's 940 mAh.

**Why this outranks the rest of the file.** The board with the smaller cell is
also the board with the emissive display. The two facts compound rather than
cancel, and the work already done says by how much: [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md)
§1 puts the day theme's gamma-decoded emissive load at **2.622 against the night
theme's 0.188**, a factor of 13.9 on the same pixels — `ESTIMATED`, by the
method stated there, from pixel values and not from a panel. On the T-Watch's transmissive IPS that ratio is close to irrelevant,
because the backlight burns what it burns whatever is drawn on it. On this board
it is most of the power question.

So the warm-ivory day theme — the default, the one the whole palette was built
around — is at its most expensive on the hardware with the least to spend. That
is not an argument for changing the palette, and this file does not make one.
It is an argument that **"which theme is the default on the Waveshare" is a real
question with a real cost attached**, and that it cannot be answered by taste
alone. Filed as **T-095**.

None of this yields a runtime figure. A 400 mAh cell and a per-frame emissive
estimate do not multiply into hours; that needs a measured panel current at a
known APL, which nobody has. `UNKNOWN`, and it stays `UNKNOWN` until somebody
puts a meter on it — §3.

### 1.3 The flash really is external, and really is GigaDevice. `VERIFIED`

A **GigaDevice**-branded SOP-8 sits beside the SoC — the brand name is legible
in silkscreen-white on the package top; the part-number line is not. The
schematic names it `GD25Q256EYIGR`, 256 Mbit = 32 MB, quad SPI, at `U3`, and a
GigaDevice SOP-8 in that position is consistent with that and inconsistent with
nothing.

The structural half of this matters more than the brand. **The flash is a
separate package.** Whatever is inside the SoC's own package is therefore not
flash — which is what an `R8` suffix means and what an `N`-prefixed suffix would
not. It corroborates D12a's conclusion that the in-package memory is 8 MB of
octal PSRAM, without re-proving it: the photograph rules out in-package flash,
and the datasheet argument already ruled out an 8 MB quad in-package part.

The capacity digits are not legible. `esptool.py flash_id` reports the density
from the chip's own JEDEC ID and takes seconds — §3.

### 1.4 Both microphones are fitted. `VERIFIED`

Two MEMS microphone packages, silkscreened **`MIC1`** and **`MIC2`**, at
opposite corners of the board's left edge, both populated. `HARDWARE_MATRIX`
records the ES7210 as driving *"dual digital microphones"*, read from the
schematic. Both are physically there.

Two microphones at a known separation is a beamforming or noise-reference
geometry, not merely a spare. Nothing in the specification asks for that today,
and this file is not proposing it — it is recording that the option exists in
hardware, which is the sort of thing that is invisible once the case is closed.

### 1.5 The expansion pad row, in full — and it is not what it looks like

Ten plated pads run along the bottom edge, each labelled. Reading left to right
with the board oriented so the silkscreen title is upright:

| # | Pad | What it is |
|---|---|---|
| 1 | `VBUS` | USB bus voltage |
| 2 | `GND` | ground |
| 3 | `D+/IO20` | USB D+, native USB on IO20 |
| 4 | `D-/IO19` | USB D−, native USB on IO19 |
| 5 | `IO15` | **main I2C `SDA`** |
| 6 | `IO14` | **main I2C `SCL`** |
| 7 | `RXD` | UART0 receive — **GPIO 44** |
| 8 | `TXD` | UART0 transmit — **GPIO 43** |
| 9 | `GND` | ground |
| 10 | `3V3` | 3.3 V rail |

This is new — the pad row is not the `J3` connector `HARDWARE_MATRIX` records,
and the pinout was `PARTIAL` for want of a text extraction that worked.

**The trap is pads 5 and 6.** `IO15` and `IO14` are printed as bare GPIO numbers,
which is exactly how a spare pin is labelled. They are not spare. `HARDWARE_MATRIX`
records `Main I2C bus — SDA 15, SCL 14`, and six devices already live on it: the
AXP2101 PMU, the PCF85063ATL RTC, the FT3168 touch controller, the QMI8658 IMU,
and the ES8311 and ES7210 codecs as I2C control slaves. Anything that drives
those two pads as general-purpose outputs takes down the power manager, the
clock, the touchscreen and the motion sensor together, and does it silently
enough to look like a software bug.

**What this leaves for an attached Attadipa node** — a node supplying LoRa and
GNSS to a watch that has neither, per the capability-provider design — is the
`RXD`/`TXD` pair: **UART0, `RXD` = GPIO 44, `TXD` = GPIO 43**, traced in §4 of
[the module read-off](GNSS_MODULES_READOFF_2026-09-04.md). Adding the node as a
seventh I2C device on pads 5 and 6 is *possible* and is a worse idea: it puts a
detachable peripheral on the bus that the PMU and the touch controller share, so
a node that browns out or holds `SDA` low takes the watch's power management
with it. Recorded here so the question is decided on purpose rather than by
whoever solders first. Filed as **T-096**.

### 1.6 The IMU has its axes printed on the board. `VERIFIED`, and it is half of H15

Immediately beside the IMU there is a silkscreened axis triad with an origin
marker:

- **X** points toward the top edge of the board (the end with the battery
  connector);
- **Y** points toward the left edge (the end with the USB-C connector);
- **Z** is drawn as **⊙** — the circle-and-dot that means *out of the page* — so
  it points out of the face the IMU is mounted on, which is the face turned
  toward the back cover, i.e. **away from the display**.

That is a right-handed frame and it is the vendor telling us how the part is
rotated on the board, which is precisely what
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) H15 said neither schematic recorded.

**It is half the answer, not the whole one.** H15 asks for the orientation
*relative to the wearer*, and the missing half is how the board sits inside the
case — which edge is the twelve-o'clock edge on a wrist. The photographs show
the board out of the case; the assembled photograph shows the display and not
the board under it. So: the board frame is now known, the case rotation is not,
and one is useless without the other for a wrist-raise gesture. H15 stays open
and gets much cheaper: tilt the assembled watch through known angles and read
raw axes.

The reason to care is in [PEDOMETER_PARTS](PEDOMETER_PARTS.md) §1.9 — a wrong
axis map makes a step counter slightly worse and a gesture completely wrong, and
it fails silently either way.

### 1.7 There is no vibration motor on this unit. `OBSERVED`

The `MOTOR` footprint is two plated pads inside a rectangular silkscreen outline,
with `+` marking one of them. Both pads are **bare** — no solder, no wire, no
part. Immediately to their right is a large circular silkscreen outline of the
kind a coin vibration motor sits in, and it is **empty**.

The drive circuit is present and is exactly what the schematic says:
`HARDWARE_MATRIX` records **GPIO 18 → R12 (4.7 kΩ) → Q1 (MMBT3904, NPN) → motor
on pads `P1`/`P2`**, supplied from `BLDO2`. Those are the two bare pads. So the
board can drive a motor and does not have one.

> **Designator corrected 2026-08-22:** the motor pads are **`P1`/`P2`**, not `J1`. `J1` is the **battery** connector — a word-coordinate extraction of the schematic puts `J1` at (267.4, 193.8) beside net `VBAT1` at (297.2, 189.9), while the motor block (`MOTOR`, `R12 4.7K`, `R13 47K`, `Q1`, `R7 0R`, `P1`, `P2`) clusters at x ≈ 154–205, and the designator list holds `COJ1`, `COJ2` and `COP1`–`COP6` with **no `BAT1` at all**. It also resolves a contradiction inside this repository's own record: the battery plug is visibly mated to a two-pin header on the received unit, so `J1` cannot be "two bare pads". [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1.1. **The conclusion is unchanged** — the pads are bare and the coin-motor footprint is empty either way.

**Why this is a specification problem and not a soldering problem.** The design
system defines a haptic scale, the specification asks for haptic feedback, and
`Capability::Haptics` has an owner in the capability registry. On the T-Watch
that resolves to `Ready`. On this unit it resolves to `Unsupported` — and
`Unsupported` is the one terminal value in the `Availability` enum, the one that
must be stable at runtime and must never be shown to the user as something a
remedy can fix. Soldering a motor to `P1`/`P2` changes the answer and no firmware can
detect that it happened: an NPN driving an absent load looks identical to an NPN
driving a present one.

`OBSERVED`, and deliberately not `VERIFIED`, because the observation is narrow:
what was seen is *this* unit, from *one* side, with the back cover holding only
a speaker. Whether Waveshare ships a motor loose in the box, whether a different
production run populates it, and whether the product listing promises one are
three separate questions and none is answered here. The owner has been asked to
check the box. Filed as **T-097**.

### 1.8 The speaker is an AAC part, wired rather than connectored. `VERIFIED`

Mounted in the back cover: a metal-can micro-speaker marked **`AAC210602A1`**,
lot `15771`, with a red/black wire pair on a short FPC tail. The wires land on a
`+`/`−` pad pair at the bottom-right of the mainboard. AAC Technologies is a
speaker manufacturer; the part number does not resolve to a public datasheet, so
its impedance and rated power are `UNKNOWN` and matter only if somebody drives
it near its limit.

The relevant structural point is that the speaker, like the motor, connects by
**solder pads and wires**, not by a connector. Opening this watch for any reason
means desoldering, and that is worth knowing before anyone plans a repeated
teardown.

### 1.9 The factory firmware runs, and it is a capability inventory

The assembled watch boots to a launcher carrying `DrawPanel`, `SpecAnalyzer`,
`AIChats`, `GravitySphere`, `VideoPlayer`, `Gallery`, `MusicPlayer` and
`Settings`, with a status bar showing a clock, a struck-through Wi-Fi glyph and a
battery icon.

Taken as evidence rather than as decoration, that demo is a working example of
most of the peripherals this repository has only read about: the panel and its
QSPI link, the touch controller, the IMU (`GravitySphere` is an accelerometer
toy), the microphones (`SpecAnalyzer`), the codec and speaker (`MusicPlayer`),
and the flash holding assets. It does not demonstrate the RTC, the PMU's
configuration, or anything about power. The struck-through Wi-Fi glyph means
unprovisioned, not broken.

**This firmware is not published in a form anyone can restore.** Backing it up
is the first action on the board and it is in §4 for that reason.

---

## 2. What this changes in the record

| Document | Was | Now |
|---|---|---|
| `HARDWARE_MATRIX` Waveshare · Battery | `present … capacity not stated`, UNKNOWN | **400 mAh, 3.7 V, cell 402728**, VERIFIED |
| `HARDWARE_MATRIX` Waveshare · Vibration motor | driver circuit VERIFIED, actuator not discussed | driver VERIFIED, **actuator absent on the received unit**, OBSERVED |
| `HARDWARE_MATRIX` Waveshare · Expansion | `J3` … `pinout not resolved` , PARTIAL | `J3` unchanged; **a separate 10-pad row is now fully known** |
| `OPEN_QUESTIONS` H15 · IMU axes | UNKNOWN | **board frame known from silkscreen**; case rotation still UNKNOWN |
| `OPEN_QUESTIONS` H14 · QMI8658 variant | CONFLICTING, remedy needs hardware | unchanged, but the hardware exists now |
| `STATUS` · Blocked · T-010 | *no physical board* | **one of the two boards is here** |

Nothing in [VERIFIED_FACTS](VERIFIED_FACTS.md) is contradicted by the unit. That
is the quiet result and it is worth stating: a schematic, a BSP and a wiki, read
carefully and cross-checked, described this board correctly. The three things
they left out were a capacity, a pad row and a missing actuator — two omissions
and one thing a drawing cannot show.

---

## 3. What a photograph cannot settle, and the bench session that would

Every item here is answered by reading, not writing, and none of it requires
flashing anything.

| Question | How | Why it is not guessable |
|---|---|---|
| The SoC's exact variant | `esptool.py --port <p> chip_id` | The package marking is in the frame and is illegible. The suffix decides the PSRAM story, which decides the LVGL buffer story |
| Flash capacity | `esptool.py --port <p> flash_id` — JEDEC ID and density from the chip | The part-number line is illegible. 32 MB is the schematic's claim, unconfirmed on silicon |
| PSRAM mode and size | `esp_psram_get_size()` in a one-file app, or the boot log with `CONFIG_SPIRAM` enabled | D12a is an argument from a datasheet table and five vendor configs. It is a good argument. It is not a measurement |
| Which QMI8658, and which of its datasheets is real | `WHO_AM_I` at `0x00`; then set `CTRL8.Pedo_EN` and read `0x5A`–`0x5C` while walking the board across a desk | **H14 is CONFLICTING** — two documents in this repository describe the C's pedometer as complete and as absent. The silicon outranks both |
| Which six devices actually ACK on I2C | a bus scan on `SDA 15 / SCL 14` | The address table in `HARDWARE_MATRIX` has one known datasheet-revision conflict on the IMU (`0x6A` vs `0x6B`) |
| IMU axes relative to the wearer | tilt the **assembled** watch through known angles, read raw axes | §1.6 — the board frame is printed, the case rotation is not |
| Is a motor fitted anywhere | look in the box; then drive GPIO 18 and feel the case | §1.7 — an NPN with no load reads identically to one with a load |
| Panel current at a known APL | a meter in series with the cell, day theme and night theme, same screen | §1.2 — the emissive ratio is `ESTIMATED` from pixel values, and a runtime figure needs a real current |

Until each of those is done, the corresponding row stays `UNKNOWN` or
`ESTIMATED` in every document that carries it. A board on the desk is not a
measurement. **`NOT EXECUTED — HARDWARE REQUIRED` remains the honest label for
every test in this repository**, including the ones that are now easy, right up
until somebody runs them and writes down what came out.

---

## 4. Before anything else: back up the factory image

The demo firmware in §1.9 is not published by Waveshare as a flashable image of
the build that shipped. Overwriting it is the first irreversible thing anybody
can do to this board, and it is irreversible in the ordinary way — nothing burns,
nothing locks, the bytes are simply gone and there is nowhere to get them back.

Read the whole flash out first:

```
esptool.py --port <port> --baud 921600 read_flash 0 0x2000000 waveshare-2.06-factory.bin
```

`0x2000000` is 32 MB, the schematic's figure. If `flash_id` reports a smaller
density, use that instead — `read_flash` past the end of the device returns
whatever the flash does when addressed past its end, which is not an error and
is not the truth either.

This is a read. It changes nothing on the board. It is listed here rather than
in the bring-up order because it comes before the bring-up order.

The image is a binary of a vendor's proprietary firmware: **keep it out of the
repository.** Somewhere on the owner's machine, and named so that the next
person knows what it is.

---

## 5. Where the rest lives

- [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) — the bring-up order, the UNKNOWN
  table as it stood before the unit arrived, and the reasoning behind each step.
  That file is still the plan; this one is the first observations against it.
- [HARDWARE_MATRIX](HARDWARE_MATRIX.md) — the per-board part and pin tables,
  updated by this file.
- [OPEN_QUESTIONS](OPEN_QUESTIONS.md) — H14 and H15, both now cheap.
- [VERIFIED_FACTS](VERIFIED_FACTS.md) — where a fact goes once it stops being a
  question.

The photographs themselves are **not** committed. They show the owner's hands
and desk, and nothing in them is needed once the readings are written down.
