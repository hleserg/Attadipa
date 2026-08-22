# The Waveshare board, and what to do the evening it arrives

**Status:** written 2026-08-22, before the board was in hand. Every hardware
result below is `NOT EXECUTED — HARDWARE REQUIRED` unless it says otherwise.

The occasion for this document is a bring-up plan the owner was given by another
model. It is a reasonable plan and most of it agrees with what this repository
already established. Two things make it worth writing down properly rather than
following: its headline claim is contradicted by our own schematic reading, and
its one genuinely new point — that a static face ages an AMOLED — is a *design*
question that nobody here has decided.

Per [CLAUDE.md](../../CLAUDE.md), advice is a hypothesis. What follows separates
what is verified, what is open, and what only the board itself can settle.

---

## 1. The one decision that is not ours: a near-white face on an emissive panel

The Waveshare panel is AMOLED. An AMOLED has no backlight — every lit pixel is a
diode drawing its own current, so a bright page costs power in proportion to how
much of it is bright, and the diodes age in proportion to how hard they are
driven. An LCD does not work this way, and the T-Watch's panel is an
IPS LCD ([`platform/src/board_profiles.cpp:84`](../../platform/src/board_profiles.cpp)).
So this is the first design question in the project whose answer is different on
the two boards.

The day theme is Warm Ivory, `#FFF6E8`. Rendered on the Waveshare's 410×502 face,
averaged over all 205,820 pixels of the clock screen:

| Theme | Mean 8-bit value | Gamma-decoded drive (sum of R+G+B) |
|---|---|---|
| Day | R 247.3, G 238.8, B 224.7 | **2.622** |
| Night | R 54.6, G 64.9, B 52.8 | **0.188** |

**ESTIMATED, not measured.** This is the mean per-subpixel drive of a rendered
frame, decoded through the sRGB EOTF because emitted light is roughly linear in
that quantity and the 8-bit value is not. It is not a power measurement: there is
no panel here, no efficiency curve per emitter colour, and no driver behaviour in
it. What it does establish is the *order* — the day theme asks this panel for
something between four and fourteen times what the night theme asks, depending on
which of the two numbers above turns out to track current more closely on the
real part.

That is large enough that it cannot be settled by taste alone, and it is not a
decision an agent should take. The options, and what each costs:

| Option | What it costs |
|---|---|
| Day theme unchanged on both boards | One design, one set of screenshots, one contrast table. Pays the full emissive bill on the Waveshare and ages the panel fastest. |
| Day theme only on the T-Watch; the Waveshare is always dark | The cheapest and kindest to the panel, and it makes the two boards look like different products — which [final §42](../master-prompt-final.md) may or may not accept. |
| A third "day on AMOLED" palette — dark page, warm ink | Keeps a day/night distinction that means something on an emissive panel. Costs a third palette, a third contrast audit, and a third column in every review sheet. |
| Automatic by panel technology, with the theme toggle unchanged | The user still chooses day or night; day simply *renders* differently on an emissive panel. Costs the plumbing in §4 below and an explanation the first time somebody compares two watches side by side. |

Nothing here is decided. Filed as **A9** in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).

### The related question: image retention

A watch face is the most static image a screen can be asked to hold — the same
digits, in the same place, for hours. Whether that is a real risk on this panel,
on what timescale, and what the CO5300 controller offers to mitigate it, is
answered in §3 from primary sources rather than from folklore. The design
consequence, if the risk is real, is not the same as the power consequence: a
uniformly bright page ages the whole panel evenly and dims it uniformly, while a
dark page with a few bright static elements ages *those elements* and leaves
their shape behind. The two problems pull in opposite directions, and only one of
them is what people mean by "burn-in".

---

## 2. What the repository already establishes

Most of the advice was already answered here, and one part of it this repository
flatly contradicts.

**The claim that the board's PSRAM is absent or undeclared is false, and was
false before the advice arrived.** [HARDWARE_MATRIX.md:303](HARDWARE_MATRIX.md)
records 8 MB of PSRAM — now also octal-VERIFIED — and
[VERIFIED_FACTS.md:399-402](VERIFIED_FACTS.md) records the same as the resolution
of D1. No line anywhere in the repository says the part is missing — the
vocabulary for absence exists and is used plainly where it
is meant, as in `| Sub-GHz radio | — | **not present** | — | — | VERIFIED |`
([HARDWARE_MATRIX.md:330](HARDWARE_MATRIX.md)). The only true reading of "not
declared" is that this repository contains no ESP-IDF build configuration for any
target: there is no `sdkconfig`, no partition CSV and no `boards/` directory at
all, which makes the statement vacuous rather than informative. What *was* open
is narrower and was already filed: whether that PSRAM is quad or octal, open
question D12, named there as a blocker on the LVGL draw-buffer decision. That
question has since been split on the strength of §3.1 — D12a resolved octal for
this board, D12b still open for the T-Watch
([OPEN_QUESTIONS.md:93-95](OPEN_QUESTIONS.md)).

**The I2C topology was known; the addresses were not.** The main bus at SDA 15 /
SCL 14 and the membership of the touch, PMU, IMU and RTC are all VERIFIED
([HARDWARE_MATRIX.md:316-319, :326](HARDWARE_MATRIX.md)). But the Waveshare
peripheral table carried the header `| Peripheral | Part | Bus / pins | Status |`
where the T-Watch table 233 lines above it carried
`| Peripheral | Part | Bus / pins | I2C addr | Power rail | Status |`
([:80](HARDWARE_MATRIX.md)). Two columns had been dropped, so not one address and
not one power rail was recorded for this board. That was a genuine gap and the
advice was right to press on it, though it named the wrong number of devices —
see §3. The columns were restored and filled at [:313](HARDWARE_MATRIX.md) in the
same commit that introduced this document, so the gap is closed; it is recorded
here because the assessment of the advice depends on it having been real.

**"Wrap the vendor BSP rather than rewrite it" is not a finding, it is a
proposal, and the repository has deliberately left the question open.** It is
recorded by name in three places that agree with each other: T6, "Use the
Waveshare BSP as a dependency, or take only its pin facts?", UNKNOWN
([OPEN_QUESTIONS.md:179](OPEN_QUESTIONS.md)); decision row 6, "open"
([../architecture/ARCHITECTURE.md:654](../architecture/ARCHITECTURE.md)); and
"This is a reuse-ledger decision, not a default"
([DEPENDENCIES.md:171-174](DEPENDENCIES.md)). The surrounding facts were already
established too: BSP v2.0.0 declares `BSP_CAPS_BUTTONS 0` and `BSP_CAPS_IMU 0`
and drives display, touch, audio and SD only, so it does not touch the QMI8658,
the AXP2101 or the PCF85063 that are on the board — which
[CLAUDE.md:42-43](../../CLAUDE.md) has already promoted to a standing rule.
Whatever is decided, wrapping the BSP does not cover the board.

**The ESP-IDF mechanics were already constrained by an undecided version.** T1 is
"narrowed" ([OPEN_QUESTIONS.md:174](OPEN_QUESTIONS.md)), T-004 is open
([TASKS.md:1023](../../TASKS.md)), and CI prints
`| ESP32-S3 firmware build | NOT EXECUTED — ESP-IDF version undecided (TASKS.md T-004) |`
([`.github/workflows/ci.yml:281`](../../.github/workflows/ci.yml)). What exists
is an installed toolchain, `v5.5.5-496-gc197d718bcc` at `/root/esp/esp-idf`;
installed is not decided.

**One thing in the advice was genuinely new: nothing in this repository had ever
considered that a static face ages an emissive panel.** That is now §1 and §3.5,
and it is the owner's decision, not ours.

---

## 3. What the sources say

Everything in this section is documentary — datasheet, schematic or vendor source
code. No board has been touched. `NOT EXECUTED — HARDWARE REQUIRED.`

### 3.1 The PSRAM is octal, and D12 closes for this board

Three independent tiers agree, and no source contradicts them.

The datasheet settles what an `ESP32-S3R8` *is*. ESP32-S3 Series Datasheet v2.2,
§1.2 Table 1-1 "ESP32-S3 Series Comparison" (p. 13) gives the row
`ESP32-S3R8 | — | 8 MB (Octal SPI) | –40 ~ 65 °C | 3.3 V`. No 8 MB quad
in-package variant exists anywhere in that table: the only quad in-package parts
are the 2 MB `RH2`, `R2` (EOL) and `FH4R2`. Footnote 3 names the octal set
outright — "For chips with Octal SPI PSRAM (ESP32-S3R8, ESP32-S3R8V, and
ESP32-S3R16V), if the PSRAM ECC function is enabled, the maximum ambient
temperature can be improved to 85 °C, while the usable size of PSRAM will be
reduced by 1/16." `R8` and `R8V` differ by `VDD_SPI` voltage, 3.3 V against
1.8 V, not by bus width. The recollection that D12 was built on was correct, and
it is now traced rather than remembered.

The schematic supplies the marking. `U2` is `ESP32-S3R8`, a bare chip — already
VERIFIED at [HARDWARE_MATRIX.md:301](HARDWARE_MATRIX.md) — so the in-package row
applies. Read visually rather than by text extraction, sheet 1 shows package pins
38–42, which are GPIO33–GPIO37, each carrying a no-connect marker with no wire
and no net, while pin 43 (GPIO38) immediately above and pins 33–35
(`SPICLK`/`SPIQ`/`SPID`) below are wired. Datasheet Table 2-14 populates
GPIO33–37 as DQ4–DQ7 and DQS **only** in the Octal SPI/OPI column, above the
notice "Do not use the pins connected to in-package flash/PSRAM for any other
purposes." This is a falsification test the board passed: had any of those five
pins been routed to a peripheral, octal would have been refuted. It is necessary,
not sufficient — five no-connects are equally consistent with a part having no
PSRAM at all, so the octal conclusion rests on Table 1-1 plus the marking, and
the pins only fail to contradict it.

Vendor source corroborates. In `waveshareteam/ESP32-S3-Touch-AMOLED-2.06`,
**five** of the six ESP-IDF examples — `02_lvgl_demo_v9`, `03_esp-brookesia`,
`04_Immersive_block`, `05_Spec_Analyzer` and `06_videoplayer` — carry
`CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y` and `CONFIG_SPIRAM_SPEED_80M=y` in
`sdkconfig.defaults`. The committed full `sdkconfig` of `02_lvgl_demo_v9` adds
`# CONFIG_SPIRAM_MODE_QUAD is not set` and leaves
`# CONFIG_SPIRAM_IGNORE_NOTFOUND is not set`, which means the vendor ships a
build that aborts at boot if octal PSRAM is not found.

**Scope.** This closes D12 for the Waveshare board only. D12 as written binds
both targets through the shared `R8` marking, and the LilyGO vendor document
describing the T-Watch's PSRAM as QSPI remains a live conflicting source that
nobody has re-examined. So the question was split rather than closed: D12a
records the Waveshare as resolved and octal, D12b leaves the T-Watch CONFLICTING
pending its own readback ([OPEN_QUESTIONS.md:93-95](OPEN_QUESTIONS.md)).

**A flash conflict comes with it, and is not resolved.** Those same five vendor
`sdkconfig.defaults` set `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, while the schematic
reading gives `GD25Q256EYIGR` = 256 Mbit = **32 MB**
([HARDWARE_MATRIX.md:302](HARDWARE_MATRIX.md)) and the Waveshare wiki says "an
external 32MB Flash". Both sources stand as read. A 16 MB declaration on a 32 MB
part boots and wastes the upper half, which is the likeliest explanation, but
that is a hypothesis. It matters for dual-OTA arithmetic and one command settles
it.

### 3.2 The main I2C bus has six devices, not four

The advice named four. The vendor's own BSP puts six on one wire: it creates a
single `i2c_master_bus` (`esp32_s3_touch_amoled_2_06.c:93`) and hands that same
handle to the ES8311 codec (`:262`), the ES7210 microphone ADC (`:310`) and the
FT5x06-family touch IO (`:494`). Both Everest parts are register-configured
devices; they appeared in HARDWARE_MATRIX as "I2S", which is their data path,
with no control bus recorded — since corrected, and
[:320-321](HARDWARE_MATRIX.md) now name the I2C control path explicitly. They are
two more addresses on SDA 15 / SCL 14, and they belong in the board profile and
in any collision check.

| Addr | Part | How it is known |
|---|---|---|
| 0x18 | ES8311 codec | Schematic: R50 (10 kΩ) ties `Codec_CE` to AGND, so CE is low; vendor `examples/arduino/08_ES8311/es8311.h:17` — "CE pin low - 0x18, CE pin high - 0x19" |
| 0x34 | AXP2101 PMU | **Datasheet-fixed.** Table 6-1 gives write byte `01101000` = 0x68, read 0x69, i.e. 7-bit `0110100`. No address-select pin exists. Do not enter 0x68 as a 7-bit address |
| 0x38 | FT3168 touch | **Driver source only — no datasheet states it.** Four independent implementations agree, including `esp_lcd_touch_ft5x06.h:39`, which is the macro this board's BSP calls. The controller is inside the display module, so no strap is inspectable |
| 0x40 | ES7210 mic ADC | Schematic-strapped **and annotated**: A1/A0 pulled to AGND through R42/R43 (0 Ω), the alternates R35/R36 marked NC, and the drawing prints `0x40` beside them |
| 0x51 | PCF85063ATL RTC | **Datasheet-fixed.** NXP PCF85063A Rev. 7 §9.5.1: "One I2C-bus slave address (1010001) is reserved for the PCF85063A" |
| 0x6B | QMI8658C IMU | Schematic prints `0X6B` inside the U5 block; pin 1 SDO/SA0 is tied to GND and pin 12 CS to VCC3V3, selecting I2C. See the conflict below |

Five of the six are fixed or confirmed and none of them collides. **The sixth
address is the IMU's, and whether 0x6A is occupied or free is exactly what is
in conflict below** — 0x6A is one of the two candidate addresses for the same
device printed as 0x6B, not a separate, empty slot. Calling it "unoccupied" begs
the question a scan is supposed to answer.

**A datasheet conflict on the IMU, reported rather than resolved.** The board
grounds SA0. QMI8658C Rev 0.6 (2021-01-13, marked ADVANCE INFORMATION) maps
SA0-low to **0x6A** and calls the internal bias a pull-down. Revisions 0.8, 0.9
and A, and the QMI8658A Rev A, all map SA0-low to **0x6B** and call the internal
bias a pull-up. The sting is that Rev 0.6 is precisely the PDF that Waveshare's
own product wiki links as "QMI8658 Datasheet", so following the vendor's link and
reading the strap honestly yields 0x6A and an IMU that never answers. The weight
is three later revisions plus the schematic's printed `0X6B` plus both vendor
driver call sites against one superseded document, but a NACK is the only thing
that closes it.

**A naming trap worth not re-deriving.** Two vendor-shipped drivers use opposite
conventions: SensorLib's `QMI8658_L_SLAVE_ADDRESS` is 0x6B (L = the SA0 *pin
level*), while Waveshare's `QMI8658_ADDRESS_HIGH` is also 0x6B (HIGH = the
*numeric value*). The two board demos look like they disagree and do not. Any
Attadipa wrapper that re-exports either name hands the next reader the same trap.

### 3.3 What the vendor BSP actually does with the draw buffer

This one matters because it was cited as evidence for a decision it does not
support.

`bsp_display_start()` builds a config with `.buff_spiram = true`
(`esp32_s3_touch_amoled_2_06.c:608`) — and it is dead code.
`bsp_display_start_with_config()` reads only `cfg->lvgl_port_cfg` (`:618`, the
sole `cfg->` dereference in the whole file); the buffer size, the double-buffer
flag and the placement flags are constructed and discarded. The live allocation
is in `bsp_display_lcd_init()`: `buffer_size = BSP_LCD_H_RES * LVGL_BUFFER_HEIGHT`
(`:510`), where `LVGL_BUFFER_HEIGHT` is `CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT`,
Kconfig default **100**; `.buff_dma = false` (`:532`); and `.buff_spiram = false`
guarded by `#if CONFIG_BSP_DISPLAY_LVGL_PSRAM` (`:533-534`) — a symbol that
appears **zero times** in the BSP's Kconfig, so the field is never written at all.

So the vendor ships a single partial LVGL draw buffer of 410 × 100 px = 82,000
bytes, about 80 KiB, non-DMA-capable, and it normally lands in **internal SRAM**,
not PSRAM. There is therefore no vendor existence proof that PSRAM-backed LVGL
works at this resolution; if anything the vendor's shipping choice points the
other way. The LVGL buffer ADR has to decide this on its own evidence, with one
hardware constraint worth carrying into it: on the ESP32-S3
`SOC_PSRAM_DMA_CAPABLE` is 0, so a draw buffer in PSRAM can never also be
DMA-capable.

The arithmetic that frames that ADR is unchanged and independently reproduces:
410 × 502 = 205,820 px; one RGB565 frame is 411,640 B = **402.0 KiB**, which is
78.5 % of the 512 KB internal SRAM die before ESP-IDF, the QSPI driver and the
BLE stack exist; double-buffered is 804.0 KiB and arithmetically impossible
internally. Double-buffered in 8 MB of PSRAM is 9.8 % of it. Capacity is not the
constraint; internal SRAM, PSRAM bandwidth and cache coherency are.

**A live defect in the panel driver the vendor depends on.**
`waveshare/esp_lcd_sh8601` is a fork of `espressif/esp_lcd_sh8601` — its own files
carry Espressif's SPDX headers — differing by exactly two lines. One is a tear
scanline in a default init table the BSP overrides, and it is provably inert. The
other is not: at `:280` the fork calls `tx_color(...)` bare where upstream wraps
it in `ESP_RETURN_ON_ERROR`, inside `panel_sh8601_draw_bitmap`, which then
returns `ESP_OK` unconditionally. **A failed frame transfer is reported as
success.** It is present in 1.0.2, the version the published demo pins, as well
as in 2.0.0. Espressif ships both an unforked `esp_lcd_sh8601` and a
purpose-named `esp_lcd_co5300` — QSPI, accepting a custom init table — under the
same Apache-2.0, which is the strongest concrete argument yet recorded for T6
resolving as "take the pin map and the init table, depend on upstream."

### 3.4 A correction to the schematic reading: J3 is the display FPC

HARDWARE_MATRIX recorded "Expansion connector | header `J3`, at least 29 pins on
the drawing", and D3 asked for its pinout. Read visually,
J3 is the **34-pin AMOLED FPC connector**: its block is titled AMOLED and it
carries `QSPI_SIO0`–`SIO3`, `QSPI_SCL`, `LCD_CS`/`RESET`/`TE`, the MIPI pairs,
`VCI`, `VDDIO`, `IM0`/`IM1` and `TP_SCL`/`TP_SDA`/`TP_INT`/`TP_RESET`. There is
no user expansion header on this board. Both records now say so:
[HARDWARE_MATRIX.md:328](HARDWARE_MATRIX.md) carries a Display FPC row instead,
and D3 is struck as mis-stated at
[OPEN_QUESTIONS.md:85](OPEN_QUESTIONS.md). This retires the hot-unplug and
bus-capacitance worry that D3 inherited from the T-Watch, where main-I2C `SDA`
genuinely does reach a detachable GNSS connector
([HARDWARE_MATRIX.md:208](HARDWARE_MATRIX.md)) — but it confirms that the touch
half of the main I2C bus leaves the mainboard over a flex cable, which is a
mechanical reliability fact rather than a design one.

### 3.5 The AMOLED evidence, and what it does and does not support

The phenomenon is real at class level and stated by manufacturers. Apple, on its
own OLED displays, writes that "Burn-in can occur in more extreme cases such as
when the same high-contrast image is continuously displayed at high brightness
for prolonged periods of time", separates that from image persistence which "is
temporary and disappears after a few minutes of normal use", and advises
"Avoid displaying static images at maximum brightness for long periods of time"
(support.apple.com/en-us/109039). The controller vendor treats it as a design
problem in the silicon: Chipone's CO5300 datasheet V0.01 (24 July 2023, p. 6)
says the ACL function "is able to reduce the total power consumption of display
module significantly and keep AMOLED life time."

The timescale evidence is class evidence only, and it points both ways. A
comparable module specification (BO139A454SPI, 1.39" 454×454 — same class, **not**
this part) defines its image-sticking test as an 8×8 chessboard at maximum
luminance for **12 hours**, judged as "normal performance after the test, without
image sticking" following three minutes off — and on the same document gives
"OLED lifetime … with white color pattern **150 hrs**". A 2.06" 410×502 CO5300
module from a different vendor quotes "Continuous display lifespan ≥ 200 hours at
25 °C with white light" at 600 cd/m². Read together: one debugging afternoon of a
static frame will not leave visible sticking, because that is inside the sticking
test; but twelve hours of full white is roughly 8 % of a rated white-pattern
lifetime, so repeated sessions spend real life. "It will burn in this afternoon"
is unsupported. "It is free" is also unsupported. Note that "≥ 200 hours" is a
guaranteed floor, not a failure point. None of these figures is this panel's: the
module's maker and part number are not recorded anywhere and are not recoverable
from the schematic, which labels the connector only `AMOLED`.

What the controller offers, from the datasheet:

- Brightness is a register, `51h WRDISBV`, not a backlight — the panel is
  emissive and there is no backlight net. The vendor BSP headers still say
  "Brightness is controlled with PWM signal to a pin controlling backlight",
  which is inherited esp-bsp boilerplate contradicted by the implementation
  beside it, which sends DCS `0x51`.
- `53h WRCTRLD` defaults to `28h` (brightness control plus hardware dimming
  ramp). The BSP writes `0x20`, turning the dimming ramp **off**.
- `55h WRACL`, Auto Current Limit, defaults to `00h` — **disabled** — on power-on,
  software reset and hardware reset alike. Neither the Waveshare BSP nor either
  third-party driver ever writes it. The one mitigation the silicon names by
  name is almost certainly off. Call this LIKELY rather than VERIFIED: the
  panel's MTP and CMD2 defaults are not public.
- `39h IDMON` idle mode reduces colour expression. The IDMON page says the colour
  becomes "determined by MSB of R, G, and B", while the feature list on p. 7 says
  idle supports 16.7M, 4096 and 8 colours — so the MSB behaviour describes one
  sub-mode, and whether idle mode reduces *power* is not stated anywhere.
- Partial display (`12h`/`13h` with `30h`/`31h`) is a genuine 2D window.
- There is **no pixel-shift command and no scroll command**. Shifting is software.
- There are **no AOD commands in the datasheet** — `4Fh` deep standby is the only
  `4xh` entry — although the Arduino driver bundled in the vendor repo defines
  `48h`/`49h`/`4Ah`/`4Bh`. Those defines are byte-identical to the sibling
  SH8601 header, so they are most plausibly copied. Undefined on this silicon
  until tried.
- The BSP's init table leaves the panel at `0x51 = 0xFF`, **100 % brightness**,
  at boot.

The words "burn", "retention", "aging", "demura" and "AOD" do not appear anywhere
in the 226-page datasheet, and the Waveshare wiki says nothing about any of it.

For what other people do: Google's Wear OS guidance requires that "only 15% of
pixels are illuminated in ambient mode", says "Keep at least 85% of the screen
black", and says to "periodically shift the UI elements slightly and avoid solid
white areas to prevent screen burn-in" — but that governs **ambient mode**, not
interactive themes, and quoting it against an interactive palette compares things
the rule does not compare. A Rust firmware for this exact board
(`infinition/waveshare-watch-rs`, Apache-2.0 OR MIT) shifts its watch face on a
timer, builds its always-on face on pure black "because on AMOLED these pixels
are physically OFF", and steps brightness `0xD0` → `0x40` at 8 s → `0x18` at 15 s
→ display off at 180 s idle. It enables neither ACL nor idle nor partial mode.
Waveshare's own examples do none of it.

Scale honestly. The measured anchor is Dash & Hu, MobiSys 2021 (Purdue), on
phones rather than this board: dark mode saves "only 3%-9% power on average" at
30–50 % brightness but "39%-47%" at 100 %. Display emission is not battery life,
and the theme choice matters most at exactly the brightness the vendor BSP boots
to.

### The second design decision: static content

§1 asks whether the day theme keeps a near-white page. This is the adjacent and
different question, and it also belongs to the owner. A uniformly bright page
ages the whole panel evenly and dims it uniformly; a dark page with a few bright
static elements ages *those elements* and leaves their shape behind. Only the
second is what people mean by burn-in, and mitigations that help one do nothing
for the other — a four-pixel shift on a uniform ivory field shifts nothing.

| Option | What it costs |
|---|---|
| Do nothing; treat retention as a non-issue at watch duty cycles | Free today. Bets the panel on an unmeasured duty cycle and on class figures that are not this part's. Reversible only until it is not |
| Screen timeout only — the display sleeps, nothing else changes | Nearly free, and it is what Apple's own advice reduces to. Costs the glance-without-touching behaviour that a watch is for |
| Software pixel shift on the always-on face | A few lines and a layout that tolerates ±4 px. Helps sparse bright content, does nothing for a full-brightness page, and needs the AOD path to exist first |
| Always-on face constrained to a black ground with sparse elements | Cheapest for the panel and the battery, and it is what Wear OS requires of ambient faces. Costs a second face design and an explicit statement that Attadipa's AOD is not its day theme |
| Enable ACL (`55h = 0x11`) globally | One register write. Unmeasured visual cost on a near-white face, unmeasured saving, and no field experience anywhere in the ecosystem to borrow |
| Cap brightness below `0xFF` in shipped builds | One number. Costs outdoor legibility, which is the thing a 600 cd/m² panel was bought for |

Not decided here. Filed as **A10** in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md), and it
should not be decided before the measurements in §5 steps 7 and 8 exist.

---

## 4. Where a panel-technology decision would live in the code

Recorded here because it is the part of §1 that has an architectural answer, and
it is small.

`platform::PanelTechnology` already exists and the Waveshare profile already sets
`Amoled` ([`platform/src/board_profiles.cpp:102`](../../platform/src/board_profiles.cpp)).
**Nothing reads it.** A grep across `platform/`, `core/`, `ui/` and `apps/`
returns the two assignments and no consumers, so no code in this project can
behave differently on an emissive panel today, whatever is decided.

The rule in CLAUDE.md is that an application asks what the device can *do*, never
which device it *is* — so `PanelTechnology` must not travel upward under that
name. What can travel is the consequence, in the same shape `dpi` already
travels: `ui::Metrics` is constructed from a panel property by the composition
root and carries no hardware identity. A second property alongside it — "lit
pixels cost power on this panel" — is the same kind of fact as "this panel is
315 dpi", and a theme can consult it without ever learning what a CO5300 is.

The alternative, a `Capability`, is wrong: capabilities are things a device can be
asked to do, and can arrive from an attached node. A panel's physics cannot.

This is a sketch, not a proposal. It becomes a task only if §1 is answered in a
way that needs it.

---

## 5. The first evening, in order

These are instructions for the owner at a bench, not steps for an agent. Nothing
below is irreversible: there is no eFuse burn, no secure boot, no flash
encryption and no key handling anywhere in it. The one step that overwrites
anything is step 4, and step 3 exists so that step 4 can be undone.

Order matters. Each step either costs nothing or depends on the one before it,
and the two steps that could leave the board in a state you cannot inspect —
cutting rails one at a time, and re-wiring the SD card mode — are deliberately
not here at all.

Nothing below may be recorded as `PASS`. Record what was observed; the word for a
step that has not run is `NOT EXECUTED — HARDWARE REQUIRED`.

**Step 1 — Unbox, photograph, read the markings.** Photograph both faces before
anything is plugged in, and read the part markings on U2, U3 and the display FPC
with a loupe. *Expected:* `ESP32-S3R8` on U2, `GD25Q256E…` on U3, and a legible
module part number somewhere on the panel or its flex. *Failure:* a different SoC
suffix, which would invalidate §3.1 outright; or an unreadable panel marking,
which is the ordinary case and simply leaves that row UNKNOWN. *Unblocks:* the
whole document, which assumes schematic revision V1.0; and the panel part number
is the only route to this panel's own retention and lifetime specification.

**Step 2 — `esptool.py -p <PORT> flash_id`.** Read-only; it writes nothing and
touches no eFuse. *Expected:* a header line `Chip is ESP32-S3 (QFN56) (revision
vX.Y)`, a features line containing `Embedded PSRAM 8MB (AP_3v3)`, and
`Detected flash size: 32MB`. *Failure:* `Embedded PSRAM 2MB` would refute §3.1
completely, since the only 8 MB in-package variants are octal; a blank vendor
field is unremarkable. `Detected flash size: 16MB` is the other half of this
step and needs saying explicitly: it would mean the schematic reading is wrong
and the five vendor `sdkconfig.defaults` right, and the 32 MB figure has to come
out of `RESOURCE_BUDGET.md` before anything is sized against it. *Unblocks:*
confirms D12 for this board and settles the 16-versus-32 MB flash conflict in
one line. Note that this reports capacity and voltage class, never bus width —
octal follows from capacity plus Table 1-1, not from esptool.

*Also available at this point, and read-only:* `ESP_EFUSE_PSRAM_CAP`,
`ESP_EFUSE_PSRAM_VENDOR` and `ESP_EFUSE_PSRAM_TEMP` are public ESP-IDF eFuse
fields on the ESP32-S3 (`components/efuse/esp32s3/esp_efuse_table.csv:206-209`),
readable from an application with `esp_efuse_read_field_blob`. **Reading an eFuse
is not burning one** — nothing in this document writes one, and nothing in this
project may without the owner asking in writing. They give capacity and vendor at
runtime; the line mode is exposed by no public API, which is why the boot log
stays the instrument for quad-versus-octal.

**Step 3 — back up the factory image: `esptool.py -p <PORT> read_flash 0 ALL
factory.bin`.** Read-only, and the only genuinely unrecoverable thing on the desk
this evening if it is skipped. *Expected:* a file whose size matches whatever
step 2 detected, and a clean checksum. *Failure:* a short or erroring read means
stop and resolve the connection before writing anything. *Unblocks:* every step
after it, because it makes step 4 reversible.

**Step 4 — flash the vendor demo `examples/esp-idf/02_lvgl_demo_v9`,
unmodified.** This is deliberately the vendor's build rather than ours: its
`sdkconfig` already sets `CONFIG_SPIRAM_MODE_OCT=y` and leaves
`CONFIG_SPIRAM_IGNORE_NOTFOUND` unset, which makes it a negative control for
§3.1 as well as a display and touch test. *Expected:* the boot log carries the
tag `octal_psram` and its register dump (`vendor id`, `density`, `good-die`,
`VCC`), then `Found 8MB PSRAM device` and `Speed: 80MHz`; the panel lights and
touch responds. *Failure:* `PSRAM chip is not connected, or wrong PSRAM line
mode` followed by `Failed to init external RAM!` and an abort **before**
`app_main` — which would refute octal, and is exactly why calling
`esp_psram_get_size()` first is the wrong instrument. Read the **tag**, not the
size line: `Found 8MB PSRAM device` is printed identically by the quad and octal
paths. *Unblocks:* D12 empirically; the display and touch path; D7, since a
working init sequence is now observable; and real evidence for T6.

**Step 5 — bus scan with the stock `examples/peripherals/i2c/i2c_tools`.** At the
console: `i2cconfig --port=0 --sda=15 --scl=14 --freq=100000`, then `i2cdetect`.
Run this **early in the session**, because the FT3168 datasheet documents that
the touch controller stops answering after the host addresses another slave on
the same bus while it is in Monitor or Sleep mode. *Expected:* six ACKs — 0x18,
0x34, 0x38, 0x40, 0x51 and 0x6B. *Failure:* fewer. A missing device is **not**
proof of absence: the AXP2101 may not have enabled the rail feeding it, and D13
leaves rail assignment unknown, so record a missing part as UNKNOWN rather than
ABSENT. `UU` in the table means a timeout, which points at pull-ups rather than
at a device. *Unblocks:* every driver on this board, and §3.2's whole address
table.

**Step 6 — settle the QMI8658 datasheet conflict.** Stay in the same `i2c_tools`
console as step 5, so nothing new has to be built. If step 5 showed 0x6B, run
`i2cget -c 0x6b -r 0x00 -l 1`; *expected:* `WHO_AM_I = 0x05`, which confirms
the Rev A mapping and retires Rev 0.6. If step 5 showed 0x6A instead, the
silicon follows Rev 0.6 and Waveshare's own wiki link was right — record that,
because it inverts a strap-to-address rule the next agent will otherwise
re-derive wrongly. While here, read the FocalTech identity block —
`i2cget -c 0x38 -r 0xa3 -l 1` and the same for `0xa6`, `0xa8` and `0xa1` — and
**record the raw bytes** rather than comparing them to a remembered constant,
since no FT3168 datasheet publishes a register map. Then write and read back one
of the threshold registers the FT5x06 driver blind-writes during init:
`i2cset -c 0x38 -r 0x80 0x16` followed by `i2cget -c 0x38 -r 0x80 -l 1`.
*Failure*, meaning a NACK or a value that does not read back, means
`touch_ft5x06_init()` will return an error on this chip and the vendor's own
FT3168-via-FT5x06 substitution is not safe as shipped. *Unblocks:* the IMU and
touch drivers, and the T6 argument.

**Step 7 — the AMOLED controller probes.** *What to run*, first, because it is
not what the previous two steps used: these are DCS commands to the CO5300 over
the **QSPI panel IO**, not I2C, so the `i2c_tools` console of steps 5 and 6
cannot send them. They need a small firmware — in practice a dozen lines added
to the step-4 vendor demo. The pattern to copy is
`bsp_display_brightness_set()` in `esp32_s3_touch_amoled_2_06.c:341-364`, which
packs the opcode as `lcd_cmd =
(0x02 << 24) | (cmd << 8)` and calls `esp_lcd_panel_io_tx_param(io_handle,
lcd_cmd, &param, 1)`; the `0x02` prefix is the QSPI write-command header for the
`SH8601_PANEL_IO_QSPI_CONFIG` at `:437`.

Writes are therefore proven by vendor source. **Reads are not.** `rx_param`
appears zero times in that BSP, and `bsp_display_brightness_get()` at `:367-376`
returns a cached variable rather than asking the panel, so nothing in the vendor
tree demonstrates a readback over this IO. Try `esp_lcd_panel_io_rx_param` for
`RDDPM` (`0x0A`), which returns the IDMON/PTLON/SLPOUT/NORON/DISPON bits and
would make idle mode directly observable; but if the read returns zeros or
errors, that is a limitation of the QSPI read path and **not** evidence about the
panel's state. Fall back to the visible change, and record the readback attempt
as UNKNOWN rather than as a negative result.

*The sequence.* Read `0x0A` if reads work, write `0x39` (IDMON), read `0x0A`
again; *expected:* the IDMON bit sets and the screen visibly dims and posterises.
**Restore with `0x38` before moving on**, or every later measurement is taken in
idle mode. Then write `0x49` and `0x4A` and read back `0x4B`: if `0x4B` returns
what was written, the AOD commands are real on this part; if nothing changes —
and given the read caveat above, treat "nothing changes" as inconclusive unless
the display itself responds — the Arduino header's `48h`/`49h`/`4Ah`/`4Bh` are a
mis-copied SH8601 header and Attadipa must not depend on those opcodes. Finally
write `0x55 = 0x11` to enable ACL, photograph the result against a photograph of
the same frame taken before, then **write `0x55 = 0x00` to restore**, since ACL
left enabled silently changes the step-8 currents. *Unblocks:* the design
decision in §3.5, which currently has no measured cost on either side.

**Step 8 — the current measurements behind §1.** Same patched firmware as step 7,
since setting `0x51` is the same `tx_param` call and the fill colour is an LVGL
one-liner; confirm first that step 7's `0x38` and `0x55 = 0x00` restores actually
ran. With radios off, the CPU pinned and touch and IMU polling stopped, take
five readings: full-frame Warm Ivory `#FFF6E8` at `0x51 = 0xFF` and at `0x20`;
full-frame Ink Olive `#2F3A2E` at the same two; and full-frame black at `0xFF`
as the zero reference. Subtract the black reading from each to isolate panel
emission. Prefer a shunt on the battery lead to a USB meter, because the USB path
includes AXP2101 charging. *Expected, as a prediction to falsify:*
ivory-minus-black is roughly fourteen times olive-minus-black at the same DBV.
*Unblocks:* A9, which is
currently being asked of the owner on an ESTIMATE rather than a measurement.

**Step 9 — the buttons.** Press `Key1` and `Key3` and watch a GPIO dump; the
schematic shows the keys exist but not which pin each uses. *Unblocks:* D5, and
the power-button path, which today has no recorded delivery route because the
Waveshare PMU row records no interrupt line.

**Deliberately not this evening.** Cutting one AXP2101 rail at a time to see
which parts drop off the scan (D13) needs the address list from step 5 to exist
first and risks a confusing half-powered board. Re-wiring or re-testing the SD
card in the other mode (D14) can wait. Anything involving eFuse, secure boot or
flash encryption is out of scope permanently unless the owner asks in writing.

---

## 6. What stays UNKNOWN until the board is on the desk

This table deliberately excludes things the documents already settle. Whether the
PSRAM is present, whether it is octal, and whether the SoC is a bare chip are
**not** in it — §3.1 closes all three from a datasheet, a schematic and five
vendor build files, and step 4 confirms rather than discovers. Treating a settled
fact as a hardware unknown is how the queue ends up waiting on a board for an
answer a vendor file already contains.

| # | What is unknown | The one measurement that settles it |
|---|---|---|
| 1 | Does the QMI8658C answer 0x6B or 0x6A — i.e. which datasheet revision the silicon follows | Step 5's scan; 0x6A is unoccupied, so whichever address ACKs *is* the answer |
| 2 | Does the FT3168 answer 0x38 at all | The same scan. No datasheet states this address; it is driver-source-only |
| 3 | Does the FT3168 accept the nine blind writes to 0x80–0x89 that `touch_ft5x06_init()` performs | Step 6's write-and-read-back of 0x80 |
| 4 | Is the flash 16 MB or 32 MB — vendor `sdkconfig` against schematic and wiki | `Detected flash size` in step 2 |
| 5 | Do the CO5300 AOD commands `48h`/`49h`/`4Ah`/`4Bh` exist on this silicon | Step 7's write to `0x4A` and read back of `0x4B` |
| 6 | What ACL costs visually and saves electrically here | Step 7 plus step 8's meter, with `0x55` toggled |
| 7 | The panel module's maker, part number, and therefore its own image-sticking and lifetime specification | Step 1's loupe on the module or its flex |
| 8 | What `0x51 = 0xFF` actually is in cd/m² | A photometer against the panel; not obtainable from any document |
| 8a | The PSRAM **line mode** as the chip itself reports it — no public ESP-IDF API exposes it, so it is not obtainable at runtime at all. `ESP_EFUSE_PSRAM_CAP`, `ESP_EFUSE_PSRAM_VENDOR` and `ESP_EFUSE_PSRAM_TEMP` are public read-only eFuse fields on the S3 (`components/efuse/esp32s3/esp_efuse_table.csv:206-209`) and give capacity and vendor, never line mode | The boot log's `octal_psram` tag in step 4 |
| 9 | Achievable PSRAM bandwidth and the real cache-coherency cost of DMA out of PSRAM | A memcpy and blit benchmark on the board; every figure in §3.3 is arithmetic |
| 10 | Whether 120 MHz PSRAM is viable here, given the vendor ships 80 MHz | Build at `CONFIG_SPIRAM_SPEED_120M` and run the same benchmark |
| 11 | Which GPIO each tactile key uses (D5) | Step 9 |
| 12 | Which loads sit on ALDO1/ALDO2/ALDO3, and what runs on the 1.8 V ALDO4 rail (D13) | Cut one rail at a time and watch which addresses drop off the step-5 scan — deferred past evening one |
| 13 | Whether the SD card is wired for SDMMC 1-bit or SPI (D14) | Try each mode against the card and see which enumerates |
| 14 | Whether `AXP_IRQ` reaches any SoC GPIO — it appears in no row of the schematic's GPIO table | Continuity from AXP2101 pin 38, or a probe while forcing a PMU interrupt |
| 15 | Whether GPIO45 is held low across every reset path — it is the `VDD_SPI` voltage strap **and** this board's I2S word-select | A scope on GPIO45 through a reset. Getting this wrong switches `VDD_SPI` to 1.8 V and takes down flash and PSRAM together |
| 16 | Battery capacity and charge-path details (D2) | The cell's own label |
| 17 | The T-Watch's PSRAM line mode — **not** answered by §3.1 | That board's own `flash_id` readback. The LilyGO document saying QSPI stands as a live conflict until then |
| 18 | The real day-versus-night current ratio behind A9 | Step 8 |

---

## 7. Where the advice was wrong, and where this repository is wrong

The advice was useful and most of it holds. What follows is only the part that
does not, kept because an uncorrected claim propagates.

1. **"PSRAM is not declared for this board."** False, and contradicted by
   [HARDWARE_MATRIX.md:303](HARDWARE_MATRIX.md) and
   [VERIFIED_FACTS.md:399-402](VERIFIED_FACTS.md). Only the build-configuration
   reading is true, and it is vacuous — no target has a build configuration here.
2. **"Run `esp_psram_get_size()` on arrival."** As written this cannot do the job
   asked of it. `CONFIG_SPIRAM_MODE` defaults to QUAD, and a quad image on this
   octal board aborts in `cpu_start` before `app_main` is ever reached. The boot
   log is the instrument on arrival, not the function call.
3. **"A board with PSRAM fitted but the config off reports zero."** False. With
   `CONFIG_SPIRAM=n` the function is not compiled at all and there is no weak
   stub, so it is a link error. Zero is returned only when `CONFIG_SPIRAM=y` and
   initialisation failed or has not yet run.
4. **"Whether this board is quad or octal cannot be settled without the board."**
   False, and the most expensive error in the set: it would have parked a
   blocker on hardware that five vendor `sdkconfig.defaults` files and Table 1-1
   already answer.
5. **"Whether the board carries a bare chip or a module is unknown."**
   Contradicts [HARDWARE_MATRIX.md:301](HARDWARE_MATRIX.md), which records
   "bare chip, not a module" as VERIFIED from the schematic.
6. **"Four devices on the main I2C bus."** Six. The ES8311 and ES7210 are I2C
   control slaves on the same wire, which the vendor's own BSP demonstrates by
   handing them the single bus handle.
7. **"The ES8311 address is unknown."** Closed: R50 ties `Codec_CE` to AGND and
   the vendor's own example states CE-low means 0x18.
8. **"The vendor BSP is existence proof that PSRAM-backed LVGL works at this
   resolution."** Refuted in §3.3. The buffer is ~80 KiB in internal SRAM and the
   PSRAM configuration is dead code.
9. **"Waveshare's BSP for CO5300/FT3168."** It is largely Espressif's code
   re-namespaced, plus one board-specific init table. The reuse decision is per
   artifact, and the fork carries a real regression.
10. **Smaller factual slips**, each of which would have survived into a document:
    the PCF85063 interrupt is pin 4, not pin 5; `waveshare-watch-rs` dims at 8 s
    and 15 s, not 20 s and 40 s, the older figures surviving only in stale code
    comments; "≥ 200 hours" is a guaranteed floor rather than a failure point,
    and the companion 150 h white-pattern lifetime line was omitted; the Wear OS
    15 % and 85 % rules govern ambient mode, not interactive themes; ACL-off is
    LIKELY, not VERIFIED, because the panel's MTP defaults are not public; the
    I2C pins were cited to [HARDWARE_MATRIX.md:315-316](HARDWARE_MATRIX.md),
    which are the display and touch rows, where the bus row is
    [:326](HARDWARE_MATRIX.md); and the CI status line was cited at
    `ci.yml:330`, where the file is 295 lines long and the line is
    [`:281`](../../.github/workflows/ci.yml).

And the defects that are ours. **Five** of them were repaired on this branch —
four in `e5b7791`, the same commit that introduced this document, and the D12
propagation in the commits after it. They are kept as record rather than as
actions, because a fixed defect listed as open sends the next agent to fix it
twice. **Two are still live.**

**Repaired on this branch:**

1. HARDWARE_MATRIX called J3 an "Expansion connector … at least 29 pins", and D3
   asked for its pinout. J3 is the 34-pin AMOLED display FPC and there is no
   expansion header. Now a Display FPC row at
   [HARDWARE_MATRIX.md:328](HARDWARE_MATRIX.md), with D3 struck as mis-stated
   rather than answered at [OPEN_QUESTIONS.md:85](OPEN_QUESTIONS.md).
2. REUSE_LEDGER recorded the Waveshare BSP as coming from
   `github.com/espressif/esp-bsp`. It does not: `esp-bsp/bsp` holds 26 board
   entries and none is a Waveshare AMOLED board. The confusion was understandable
   — esp-bsp genuinely is the source of the `esp_lcd_touch_ft5x06` dependency —
   and it now has its own row, `waveshare-components` pointing at
   `waveshareteam/Waveshare-ESP32-components`, at
   [REUSE_LEDGER.md:69](REUSE_LEDGER.md).
3. [VERIFIED_FACTS.md:51-53](VERIFIED_FACTS.md) promises that "every part, pin,
   I2C address, and power rail" lives in HARDWARE_MATRIX. For this board neither
   addresses nor rails were there, which made the promise false. The promise now
   holds because the table was filled, not because the sentence was weakened.
4. The Waveshare peripheral table lacked the `I2C addr` and `Power rail` columns
   the T-Watch table ([:80](HARDWARE_MATRIX.md)) has. Both columns are present
   and populated at [:313](HARDWARE_MATRIX.md); the rails still need D13, and
   read `—` until it is answered.

5. **Splitting D12 left three places behind, and all three are now closed.**
   `HARDWARE_MATRIX.md:303` reads VERIFIED/octal;
   [RESOURCE_BUDGET.md:38](../architecture/RESOURCE_BUDGET.md) now splits the two
   columns — D12b open for the T-Watch, D12a octal for the Waveshare — and the
   open-question row at `STATUS.md:266` is struck and split the same way.

   The third took one more commit and is the one worth remembering.
   [VERIFIED_FACTS.md](VERIFIED_FACTS.md) still concluded, twenty-five lines
   below the section that split the question, that because both boards carry the
   `R8` marking D12 is "one question with one answer for both targets" — the
   exact premise §3.1 abandoned. The independent review on
   [#49](https://github.com/hleserg/Attadipa/pull/49) found it: **this
   document's own named failure mode, committed inside the change that fixed
   three other instances of it.** A reader got a different answer depending on
   which of two sections of one file they landed on first, which is precisely
   the thing §7 is a list of.

**Still live, and each one a small correcting commit:**

6. [`docs/upstream/research-integration.md:180-181`](../upstream/research-integration.md)
   states "Both Attadipa boards are ESP32-S3**R8** modules with PSRAM" and rests a
   ~10 µA light-sleep floor on the reasoning that the workaround "must not be
   deselected on a module rather than a bare chip". That contradicts
   [HARDWARE_MATRIX.md:301](HARDWARE_MATRIX.md), which records a bare chip, and
   the figure is carried forward into [HIL_PLANS.md:64-67](../testing/HIL_PLANS.md)
   as VENDOR-STATED. One of the two is wrong and the sleep-current plan depends
   on which.
7. The part-ownership table at
   [ARCHITECTURE.md:396-414](../architecture/ARCHITECTURE.md) has two problems in
   one table. It has no flash or PSRAM row for this board, where the T-Watch
   table has both — an omission rather than a claim of absence, but a defect
   against [CLAUDE.md:85](../../CLAUDE.md), since every part on the board gets a
   seat. And it still carries `| Expansion header J3 | BoardService | ≥ 29 pins;
   pinout unresolved (D3). |`, which is now contradicted by
   [HARDWARE_MATRIX.md:328](HARDWARE_MATRIX.md) and by the struck D3 above. The
   row should become the display FPC, owned by `DisplayService`.
