# T-Watch S3 Plus — the board-support composition, and where the reuse boundary falls

Researched under [#328](https://github.com/hleserg/Attadipa/issues/328) against
`main@8eb7e73`, 2026-09-01. **Research only: this document changes no production
code, and nothing in it is a hardware result.**

It answers open question **T6** — depend on a vendor BSP, or take only the facts
— and splits **D7**, the *"exact ST7789V3 init and timing"* question, into the
one part that is now closed, the one part that is source-verified but whose
necessity is not, and the one part that still needs a board.

Everything below was read at a pinned revision or in a datasheet. Where a claim
needs the physical unit it says **NOT EXECUTED — HARDWARE REQUIRED** and is not
softened into a prediction.

## 1. The short version

| Candidate | Revision | Licence | Verdict |
|---|---|---|---|
| ESP-IDF v5.5.5 `esp_lcd_panel_st7789` | tag `v5.5.5` | Apache-2.0 | **USE AS-IS** as the controller and transport baseline — with one board-owned timing correction, §4 |
| LilyGO vendor panel command table | `LilyGoLib@38e6f8d` `src/LilyGoDispInterface.cpp:505-524` | MIT | **ADAPT as data**, source-pinned; necessity **UNKNOWN** until §11 runs |
| `LilyGoLib` as a linked library | `38e6f8d`, v0.2.0 | MIT | **REJECT** — §7 |
| `espressif/esp_lcd_touch_ft5x06` 1.1.1 | esp-bsp `master`, manifest reads `1.1.1` | Apache-2.0 | **USE AS DEPENDENCY** — already pinned; drives FT6336U with `rst_gpio_num = -1`, §8 |
| `lewisxhe/SensorLib` 0.4.1 | `2b9e591` | MIT, with BSD-3-Clause under `src/bosch/` | **EVALUATE, not adopted here** — §9 |
| `meshtastic/firmware` T-Watch variant | `variants/esp32s3/t-watch-s3/` | GPL-3.0 | **ADAPT facts and failure history only** — OD-12 stands |
| `espressif/esp_bsp_generic` | 3.1.1 | Apache-2.0 | **REJECT** for this board — §10 |

The composition that follows from these is the one the issue proposed as its
baseline: a thin Attadipa board backend that hands the existing runtime an
`esp_lcd_panel_handle_t` and an `esp_lcd_touch_handle_t`, exactly as
`waveshare_board.cpp:115-116` — "esp_lcd_panel_handle_t panel" — already does.
The durable half of that is
[ADR-0017](../adr/0017-board-backends-compose-esp-idf-drivers.md).

**The one finding that changes the plan** is §4: on this board, and only on
boards like it, the generic ESP-IDF reset path violates an explicit ST7789V
datasheet restriction by 6×. That has to be fixed and *separated* before any
experiment can say whether the vendor command table is needed.

## 2. Three sources agree the display has no reset line, and that is the hinge

| Source | Statement |
|---|---|
| Attadipa schematic read | [HARDWARE_MATRIX](HARDWARE_MATRIX.md):96 — "SPI: CS 12, MOSI 13, SCK 18, DC 38, BL 45; MISO and RESET not connected" |
| LilyGO's own hardware doc for this exact board | `LilyGoLib@38e6f8d` `docs/hardware/lilygo-t-watch-s3-plus.md` — `Display RESET \| Not Connected`, `Display MISO \| Not Connected` |
| `espressif/arduino-esp32` `3.3.2` | `variants/lilygo_twatch_s3/pins_arduino.h` — `#define DISP_RST (-1)`, `#define DISP_MISO (-1)` |

Meshtastic's same-board variant agrees independently:
`variants/esp32s3/t-watch-s3/variant.h` sets `ST7789_RESET -1` and
`ST7789_MISO -1`.

Two consequences fall straight out, and both are structural rather than
stylistic.

**`reset_gpio_num` must be `-1`.** That selects the software-reset branch in
ESP-IDF, which is where §4 lives.

**The panel is write-only.** With `MISO` unconnected, no ST7789 read command can
ever execute on this board. `RDDPM (0Ah)` reports the `SLPOUT`, `DISON`,
`BSTON`, `IDMON`, `PTLON` and `NORON` bits and `RDDSM (0Eh)` reports `TEON`/`TEM`
— ST7789V datasheet v1.6 pp. 170, 178 — and neither is reachable. **The firmware
can never confirm the controller's state, and no experiment on this board can
use a register read-back as evidence.** Pass and fail are a photograph. This is
recorded here because it is the kind of thing a test plan silently assumes.

## 3. D7, split three ways

D7 was one row asking for *"exact ST7789V3 and CO5300 init sequences and their
timing"*. The ST7789V3 half is three different questions with three different
kinds of answer, and keeping them in one row is why it has stayed `UNKNOWN`.

| Part | Status after this research |
|---|---|
| **D7a — the controller baseline** | **CLOSED.** ESP-IDF v5.5.5 `panel_st7789_init()` sends exactly `SLPOUT` + 100 ms, `MADCTL`, `COLMOD`, `RAMCTRL` and nothing else — `esp_lcd_panel_st7789.c:182-201`, read at the tag. `esp_lcd_new_panel_st7789()` never reads `panel_dev_config->vendor_config`; the string does not occur in the file. There is no init-command extension point in the built-in driver, which is a fact about the driver rather than a limitation to work around |
| **D7b — the vendor command table** | **Source-verified, necessity `UNKNOWN`.** The exact bytes are transcribed in §5. Whether this panel needs them is not answerable without §11, and §4 is why the obvious experiment would have answered it wrongly |
| **D7c — SPI clock, and cold-boot stability** | **`UNKNOWN`.** §6. Two mature same-board implementations disagree by a factor of two, so there is no consensus value to inherit |

## 4. The generic reset path is 20 ms where the datasheet says 120 ms

This is the load-bearing finding and it is entirely documentary.

**What the driver does.** `esp_lcd_panel_st7789.c:162-179`, ESP-IDF `v5.5.5`:

```c
if (st7789->reset_gpio_num >= 0) {          /* hardware reset — not this board */
    ...
} else {                                     /* software reset — this board */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), ...);
    vTaskDelay(pdMS_TO_TICKS(20)); // spec, wait at least 5m before sending new command
}
```

`esp_lcd_panel_init()` then calls `panel_st7789_init()`, whose *first* action is
`SLPOUT` (`:187`). So the interval between `SWRESET` and `SLPOUT` is **20 ms**.

**What the datasheet requires.** ST7789V datasheet **v1.6, 2017/09**, p. 163 of
316, §9.1.2 `SWRESET (01h)`, under *Restriction*, verbatim:

> It will be necessary to wait 5msec before sending new command following
> software reset. The display module loads all display suppliers' factory
> default values to the registers during this 5msec.
> **If software reset is sent during sleep in mode, it will be necessary to wait
> 120msec before sending sleep out command.**
> Software reset command cannot be sent during sleep out sequence.

**And the chip is always in sleep-in mode when this runs.** Same datasheet,
p. 184, §9.1.12 `SLPOUT (11h)`, *Default* table:

| Status | Default Value |
|---|---|
| Power On Sequence | Sleep in mode |
| S/W Reset | Sleep in mode |
| H/W Reset | Sleep in mode |

So the qualified 120 ms clause is the one in force, not the unqualified 5 ms
one, and the driver's own comment — *"spec, wait at least 5m"* — is quoting the
clause that does not apply. **20 ms against a 120 ms restriction is a 6×
shortfall, on every board that leaves `reset_gpio_num` at `-1`.**

This is not a general indictment of the ESP-IDF driver. On a board with a wired
reset line the branch is never taken and the timing is the panel's own power-on
sequence. It is a defect *of the combination* — this driver with this board's
wiring — which is precisely the class of fact a board backend exists to hold.

**Two more timing clauses matter for the sleep path**, from the same datasheet,
p. 182 §9.1.11 `SLPIN (10h)` and p. 184 §9.1.12, which carry the same sentence:

> It will be necessary to wait 120msec after sending sleep out command (when in
> sleep in mode) before sending an sleep in command.

`panel_st7789_sleep()` (`:319-333`) waits `pdMS_TO_TICKS(100)` after either
command. A `SLPOUT` followed by a `SLPIN` at the driver's own cadence is
therefore **100 ms against a 120 ms restriction**. Reachable whenever the screen
blanks shortly after waking, which on a watch is the common case rather than the
exotic one.

**What this means for the experiment.** The issue's plan has two arms: A is
generic init, B is generic init plus the vendor table. But the vendor table's
first entry is `{0x11, {0}, 0x80}` — `SLPOUT` again, followed by `delay(120)`
(§5). Arm B therefore differs from arm A **in timing as well as in register
content**, so if arm A fails the two-arm test cannot attribute the failure. A
third arm is required, and it is the arm that actually decides the question:
§11.

## 5. The vendor command table, transcribed

`Xinyuan-LilyGO/LilyGoLib@38e6f8dee3ba78b340512af9a013365ef248a7d0`, MIT,
`src/LilyGoDispInterface.cpp`. Read in full at that revision.

**The file contains two tables and one of them is dead.** Lines 479-503 are
inside `#if 0`; the live table is the `#else` branch, lines 505-524. They are
not equivalent, and the differences are the kind that produce a picture rather
than an error:

| | dead (`#if 0`) | live (`#else`) |
|---|---|---|
| `COLMOD (3Ah)` | `0x55` | `0x05` |
| `PORCTRL (B2h)` first two bytes | `0x0C, 0x0C` | `0x1F, 0x1F` |
| `RAMCTRL (B0h)` | `0x00, 0xE0` — sent | **absent** |
| `INVON (21h)`, `CASET`, `RASET`, `DISPON` | sent | absent, done through the panel API instead |

Anyone transcribing this from a browser will land on the dead one first. It is
recorded here so that the mistake is made once, in a document, rather than
silently in firmware.

**The live table**, exactly, in order. `disp_cmd_t` is
`{ uint32_t addr; uint8_t param[20]; uint32_t len; }`
(`src/LilyGoDispInterface.h:56-60`); the loop at `:527-534` sends
`len & 0x7F` parameter bytes and then `delay(120)` when `len & 0x80` is set.
Only the first entry carries that bit.

| # | Cmd | Parameters | Delay |
|---|---|---|---|
| 1 | `11h` SLPOUT | — | **120 ms** |
| 2 | `B2h` PORCTRL | `1F 1F 00 33 33` | |
| 3 | `35h` TEON | `00` | |
| 4 | `36h` MADCTL | `00` | |
| 5 | `3Ah` COLMOD | `05` | |
| 6 | `B7h` GCTRL | `00` | |
| 7 | `BBh` VCOMS | `36` | |
| 8 | `C0h` LCMCTRL | `2C` | |
| 9 | `C2h` VDVVRHEN | `01` | |
| 10 | `C3h` VRHS | `13` | |
| 11 | `C4h` VDVS | `20` | |
| 12 | `C6h` FRCTRL2 | `13` | |
| 13 | `D6h` — | `A1` | |
| 14 | `D0h` PWCTRL1 | `A4 A1` | |
| 15 | `D6h` — | `A1` (**repeated**) | |
| 16 | `E0h` PVGAMCTRL | `F0 08 0E 09 08 04 2F 33 45 36 13 12 2A 2D` | |
| 17 | `E1h` NVGAMCTRL | `F0 0E 12 0C 0A 15 2E 32 44 39 17 18 2B 2F` | |
| 18 | `E4h` GATECTRL | `1D 00 00` | |
| — | `FFh` | terminator, `len == 0xFF` | |

Entry 15 is a verbatim repeat of entry 13. It is harmless and it is evidence
that this table is a working artifact rather than a specification: **not every
line in it is load-bearing, and it should not be treated as though it were.**
Upstream says as much in its own comment at `:525-526` — *"vendor specific
initialization, it can be different between manufacturers / should consult the
LCD supplier for initialization sequence code"*.

**The ordering constraint that is easy to lose.** Entries 4 and 5 rewrite
`MADCTL` and `COLMOD`, which `panel_st7789_init()` has just written from
`st7789->madctl_val` and `st7789->colmod_val`. `MADCTL 00h` discards the RGB/BGR
element order the driver encoded. What repairs it is the *next* thing upstream
does — `esp_lcd_panel_invert_color()`, `set_gap(0,0)`, `swap_xy(false)` and
`mirror(true,false)` at `:540-544` — because `panel_st7789_mirror()`
(`:246-267`) rewrites `MADCTL` from the driver's own cached value. **So the
table must be sent after `esp_lcd_panel_init()` and before the orientation
calls.** Sent after `mirror()`, the same bytes leave the panel on `MADCTL 00h`
with the wrong element order. This is a sequencing fact, not a style
preference, and it is invisible in a diff of the table alone.

**`COLMOD 05h` versus `55h` is not a discrepancy.** ST7789V `3Ah` bits `[2:0]`
select the control-interface format and bits `[6:4]` the RGB-interface format;
`05h` and `55h` both request 16 bits per pixel on the control interface, which
is the only one wired here. ESP-IDF writes `0x55` for `bits_per_pixel == 16`
(`esp_lcd_panel_st7789.c:96-99`).

**`RAMCTRL` survives.** The live table does not send `B0h`, so the data-endian
bit the ESP-IDF driver derived from `panel_dev_config.data_endian`
(`:110-116`) is still in force after the table runs. Any transcription that
picks up the dead table's `B0h 00 E0` would silently override the endian
setting — the exact class of regression ESP-IDF issue
[#11416](https://github.com/espressif/esp-idf/issues/11416) is about. The byte
order for this board must be fixed deliberately and proved with an asymmetric
pattern, not inherited.

## 6. The SPI clock is not a settled number

| Source | Value |
|---|---|
| `LilyGoLib@38e6f8d` `src/LilyGoWatchS3.cpp:135` | `LilyGoDispSPI::init(..., 80)` → `pclk_hz = 80 MHz` (`LilyGoDispInterface.cpp:430`) |
| `meshtastic/firmware` `variants/esp32s3/t-watch-s3/variant.h:13` | `#define SPI_FREQUENCY 40000000` → **40 MHz** |

Two mature implementations, the same board, a factor of two apart. That settles
one thing definitively: **80 MHz is one vendor's choice, not an established
property of the panel**, and quoting it as an Attadipa default would be
inheriting a number rather than a fact.

`D7c` therefore stays `UNKNOWN`, with a bounded shape: the lower of the two
proven values is the defensible starting point, and any increase is a
measurement on our unit, not an argument. The ST7789V serial-interface AC
characteristics are the ceiling that matters and they are a **write-cycle**
specification; nothing in a same-board firmware's choice substitutes for
reading them against this board's trace lengths. Cold-boot stability after full
power removal is a separate `UNKNOWN` and is the failure mode a bench test at
room temperature after a warm reset will not reproduce.

## 7. `LilyGoLib` as a linked dependency — `REJECT`, and the reasons are in its source

`library.json` at `38e6f8d`: version `0.2.0`, `"frameworks": ["arduino"]`,
dependencies `RadioLib 7.1.2`, `lvgl 9.2.2`, `XPowersLib 0.2.9`,
`SensorLib 0.3.1`. Attadipa pins `lvgl 9.5.0` (`firmware/main/idf_component.yml`)
and RadioLib 7.7.1. The graph collides on two of four before any code is read.

Four lifecycle behaviours, each verified at that revision in
`src/LilyGoWatchS3.cpp`, and each incompatible with a fail-closed capability
boundary:

- **`:105-108`** — `while (!psramFound()) { Serial.println("ERROR:PSRAM NOT FOUND!"); delay(1000); }`. An unbounded loop with no exit. A missing part becomes a hang.
- **`:126-129`** — `if (!initPMU()) { log_e("Failed to find PMU!"); assert(0); }`. A missing part becomes an abort.
- **`:181-191`** — reads Arduino `Preferences`, and on first boot **infers the battery capacity from whether GNSS answered**: `_is_watch_plus = devices_probe & HW_GPS_ONLINE; calibrationPMU(_is_watch_plus ? 940 : 470);`. A probe result on one bus decides a charge parameter for a cell nobody measured, and the result is written to NVS.
- **`:523-532`** — writes the derived battery parameters and the calibration flag back to `Preferences`.

None of that is a criticism of LilyGoLib, which is a vendor demonstration
library and behaves like one. It is the reason it cannot be *linked*: Attadipa
would inherit four decisions it has explicitly made differently, one of which
([#292](https://github.com/hleserg/Attadipa/issues/292),
[ADR-0016](../adr/0016-one-power-owner.md)) now has a written owner and a lease.

**What is genuinely worth taking is the ordering.** `initPMU()` at `:126`
precedes `LilyGoDispSPI::init(...)` at `:135`, and that is not style: ALDO3
supplies both the display and the touch controller and ALDO2 the backlight
(`docs/hardware/lilygo-t-watch-s3-plus.md`, agreeing with
[HARDWARE_MATRIX](HARDWARE_MATRIX.md):96-97 "ALDO3 (panel), ALDO2 (backlight)").
The panel cannot be talked to
before its rail is up. Under ADR-0016 that ordering is the power owner's to
express as a dependency, not the board backend's to perform.

**One calibration on how much to trust the vendor documentation.** The same
`lilygo-t-watch-s3-plus.md` that gives the pin map states `Display Size | 1.3
Inch` — byte-identical to the non-Plus page. Attadipa measured the active area
and it is **1.54″ at 220 ppi**; D15 is resolved against the vendor, with the
photograph and the arithmetic in
[TWATCH_S3_PLUS_PANEL_2026-08-28](TWATCH_S3_PLUS_PANEL_2026-08-28.md) and the
schematic part number `QT154C2408`. The pin table on that page is corroborated
by two independent sources and is reliable; the specification table above it is
demonstrably not. Cite the file for pins, never for panel geometry.

## 8. Touch — the pinned component drives an FT6336U, and what it does not prove

`espressif/esp_lcd_touch_ft5x06`, manifest `version: "1.1.1"`, `idf: ">=5.2"`,
`esp_lcd_touch ^1.2.0`, Apache-2.0. Already pinned in
`firmware/main/idf_component.yml` and already exercised on the Waveshare FT3168
path. Source read at esp-bsp `master`, whose manifest still reads `1.1.1`.

**No reset line is fine.** `touch_ft5x06_reset()` (`esp_lcd_touch_ft5x06.c`)
does its work only `if (tp->config.rst_gpio_num != GPIO_NUM_NC)` and otherwise
returns `ESP_OK`; the matching `gpio_config` in
`esp_lcd_touch_new_i2c_ft5x06()` is guarded the same way. `rst_gpio_num = -1` is
a supported configuration, not a tolerated one.

**The read path is safe on a two-point controller.** `read_data()` reads the
point count from `02h`, returns early if it is `0` or `> 5`, clamps to
`CONFIG_ESP_LCD_TOUCH_MAX_POINTS`, and then reads `6 * points` bytes from `03h`.
At two points that is `03h`–`0Eh`, inside the FT6336U's documented two-point
block. The count is never trusted into a longer read.

**Release is reported correctly, and the mechanism is worth knowing.**
`read_data()` returns early on `points == 0` **without** clearing
`tp->data.points`. It is `get_xy()` that invalidates — `tp->data.points = 0`
inside the critical section, after copying. So the pair is correct as long as
every `read_data()` is followed by a `get_xy()`, which is what `esp_lvgl_port`'s
input-device callback does. Calling `read_data()` twice coalesces rather than
loses. Recorded so the next reader does not re-derive it while chasing a stuck
finger.

**Two bounded reservations, neither of them blocking.**

1. **The driver never reads a chip ID.** There is no `RDID` or version read
   anywhere in `esp_lcd_touch_new_i2c_ft5x06()`. Initialization is nine register
   *writes* (`80h`–`89h`) and an accumulated `esp_err_t`. **A successful return
   proves that something ACKed at `0x38` and accepted nine writes. It does not
   prove the part is an FT6336U and it does not prove touch works.** That is the
   same rule `platform/src/board_profiles.cpp` already encodes for capabilities,
   and it applies verbatim here.
2. **Five of the nine registers are FT5x06-specific.** `THPEAK (81h)`,
   `THCAL (82h)`, `THWATER (83h)`, `THTEMP (84h)` and `THDIFF (85h)` are
   documented on the FT5x06 register map; the FT6336U datasheet documents
   `THGROUP (80h)`, `CTRL (86h)`, `TIMEENTERMONITOR (87h)`, `PERIODACTIVE (88h)`
   and `PERIODMONITOR (89h)`. Writing the other five is writing to registers
   this part does not document. Whether that is inert or changes sensitivity is
   **UNKNOWN** and is a bench observation, not a reasoning exercise. It is
   bounded because the remedy — skip those writes — is four lines in a board
   backend if the bench says so.

**Recovery has exactly one route and it is not the driver's.** LilyGO states
plainly in `docs/hardware/lilygo-t-watch-s3-plus.md`: *"T-Watch-S3-Plus does not
have a touch reset pin connected, so if you set the touch screen to sleep, the
touch will not work."* This repository already carries the mechanism —
[VERIFIED_FACTS](VERIFIED_FACTS.md) *"The T-Watch touch panel has no reset
line"*, and [HARDWARE_MATRIX](HARDWARE_MATRIX.md):222 "pull-up R39" traces it to an
unfitted pull-up `R39`. The only recovery from a wedged controller is cycling **ALDO3**,
which also blanks the display because it is the same rail. Under ADR-0016 that
is a power-owner action with a visible side effect, not something a touch driver
may do. **Putting this controller to sleep is therefore out of scope for the
first slice**, and the negative-path contract in §10 says so rather than leaving
it to a later judgement call.

## 9. SensorLib 0.4.1 — evaluated, and deliberately not adopted in this slice

`lewisxhe/SensorLib@2b9e591f245e447d3d00ec8798c3f49b897882d9`. `idf_component.yml`
reads `version: "0.4.1"`, `license: "MIT"`, `idf: ">=4.4"`. It is a real ESP-IDF
component, not an Arduino library with a manifest bolted on, and it covers
`PCF8563`, `BMA423` and `DRV2605` — the three T-Watch parts Attadipa has no
driver for.

**The licence boundary is real and is not what the root `LICENSE` says.**
`src/bosch/bma4xx/bma4.h` at that revision opens
`Copyright (c) 2023 Bosch Sensortec GmbH. All rights reserved. / BSD-3-Clause`,
and `src/bosch/` carries its own `LICENSE`. The component manifest's `exclude`
list does not exclude `src/bosch/`, so those files ship with the component. Both
MIT and BSD-3-Clause flow into GPL-3.0-or-later in the right direction, and both
require retained notices; the redistribution mechanics live in
[*Where the resolved graph lives, and where notices go*](DEPENDENCIES.md#where-the-resolved-graph-lives-and-where-notices-go)
and are not restated here.

**The verdict is `EVALUATE`, and this slice does not need it.** The first
T-Watch backend is display and touch, and SensorLib supplies neither — touch
would come from `TouchDrvFocalTech.hpp`, which would displace an
already-pinned, already-shipping official component for no stated gain. Adding a
dependency in the slice that cannot exercise it is how a dependency arrives
unexamined. The audit the decision actually needs — flash and RAM delta after
per-driver exclusions, error propagation, IRQ semantics, and the 6 144-byte
BMA423 feature blob the host uploads at every boot
([VERIFIED_FACTS](VERIFIED_FACTS.md)) — belongs to the slice that adds the RTC
and the IMU, with a number attached.

One trap if that slice ever copies LilyGoLib's graph: `library.json` pins
SensorLib **0.3.1**, which predates the FT6X36 interrupt and BMA423 fixes in
0.3.3 and the April 2026 driver move. Inheriting the vendor's pin inherits its
known bugs.

## 10. The negative-path contract for the first slice

The question the issue asks — what happens when panel, touch, RTC, IMU or haptic
is absent or fails — has one answer already written down and this section only
applies it. `platform/src/board_profiles.cpp` models capability, and readiness
is not inferable from one successful `begin()`.

1. **A failed panel command fails the display capability and nothing else.** No
   loop, no `assert`. §7 shows both of the alternatives in a shipping vendor
   library.
2. **A successful `esp_lcd_touch_new_i2c_ft5x06()` does not make touch Ready.**
   §8.1 — it proves an ACK at `0x38`. Readiness needs a coordinate.
3. **No capability unrelated to the failure changes state.** A touch bus that
   does not come up leaves the display Ready if the display is Ready.
4. **The board backend enables no rail and writes no NVS.** ALDO2 and ALDO3
   belong to the power owner under [ADR-0016](../adr/0016-one-power-owner.md);
   the backend declares a dependency and is refused or granted. The battery
   inference at `LilyGoWatchS3.cpp:181-191` is the anti-pattern, named.
5. **Touch sleep is not attempted.** §8 — the only recovery is an ALDO3 cycle
   that blanks the display.
6. **Partial availability is a reportable state, not a failure to boot.** A
   watch with a working screen and dead touch is more useful than one that
   halted, and it is what a user with a damaged unit actually has.

## 11. The experiment, which needs a board — NOT EXECUTED — HARDWARE REQUIRED

One physical T-Watch S3 Plus, USB serial `DC:B4:D9:18:49:40`, ESP32-S3 rev
v0.2, 8 MB octal PSRAM, 16 MB flash. One firmware SHA across all arms. Entry via
the verified manual download-mode route (hold BOOT while connecting USB) —
[TWATCH_S3_PLUS_DOWNLOAD_MODE](TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md).
Nothing here burns an eFuse or touches a security setting.

**Three arms, not two.** §4 is the reason.

| Arm | Reset → SLPOUT | Vendor table |
|---|---|---|
| **A** | ESP-IDF as shipped: `SWRESET` + 20 ms | no |
| **C** | `SWRESET` + **120 ms**, datasheet-conforming | no |
| **B** | `SWRESET` + 120 ms | yes, §5, sent between `esp_lcd_panel_init()` and the orientation calls |

Reading the result:

- **A fails, C passes** — the defect was timing. No board-owned command table
  is needed; a board-owned *delay* is. The cheapest outcome, and the one the
  two-arm plan would have misattributed to the gamma tables.
- **A and C both fail, B passes** — the table is necessary. It becomes a
  board-owned descriptor sent over the public `esp_lcd_panel_io_tx_param()`, with
  the MIT notice retained, and D7b closes as `ADAPT`.
- **A passes** — neither is needed on this unit; record it, and do not
  generalize past the one panel lot that was tested.

Per arm, from cold: full power removal, then boot; then ten reset cycles and ten
display off/on cycles. Each arm renders an asymmetric RGB565 swatch (so a byte
swap is visible as a colour change rather than a mirror; a swatch that is
*complemented* — red reading cyan, the white block black — is inversion, the
`CONFIG_ATTADIPA_TWATCH_PANEL_INVERT` switch every arm shares, which defaults
to the vendor's `INVON` and is flipped, not re-armed), corner and edge
coordinate markers, a grey ramp, a one-pixel checkerboard, and both a full and a
partial flush. Then rotation and gap, then display sleep and wake **with the
120 ms `SLPOUT`→`SLPIN` interval of §4 respected and, separately, deliberately
violated**, because that is the clause the driver's own 100 ms breaks.

Touch, once a panel arm passes: all four corners, all four edges, centre;
coordinate transform checked against what is drawn; INT behaviour including a
deliberately missed edge. **No touch sleep** (§8, §10.5).

Failure injection: touch bus not ACKing, a panel command failing mid-sequence, a
main-bus device absent, second-bus init failing. Prove §10.1–§10.3 hold — no
hang, no assert, no unrelated capability turning Ready.

Recorded for every arm: firmware commit SHA, component lock revisions, panel
config including the endian setting, SPI `pclk_hz`, reset reason, full serial
log, and a photograph. **Pass and fail are the photograph** (§2 — no register
read-back exists on this board). Resource numbers come from a clean build's map
and size output and are `MEASURED` only from that output; anything else is
`ESTIMATED` or `UNKNOWN`. A simulator run is never a hardware `PASS`.

## 12. What this research deliberately did not touch

PMU ownership and rail policy — [#292](https://github.com/hleserg/Attadipa/issues/292),
now merged, and [ADR-0016](../adr/0016-one-power-owner.md); this document
*consumes* that contract and does not restate it. Panel density and assets —
[#333](https://github.com/hleserg/Attadipa/issues/333) and
[#340](https://github.com/hleserg/Attadipa/issues/340), resolved; cited in §7
only as a caution about the vendor's specification table. Radio DIO3/TCXO —
[#326](https://github.com/hleserg/Attadipa/issues/326). GNSS module and rail
choice — D6 stays open. Pedometer HIL —
[#116](https://github.com/hleserg/Attadipa/issues/116). MeshCore work that
landed after the reviewed head does not touch this scope.

No production code was changed under #328.
