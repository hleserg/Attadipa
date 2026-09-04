# 0017 — A board backend composes ESP-IDF drivers; a vendor BSP is read, not linked

Status: **accepted**
Date: 2026-09-01
Consumes: [ADR-0016](0016-one-power-owner.md) §1

## Context

Open question **T6** has been open since 2026-08-21, in two places and in two
different wordings. [OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md) asked it only
about the Waveshare BSP; [DEPENDENCIES](../research/DEPENDENCIES.md) asked it
about Waveshare *and* LilyGO together. It has stayed open because it was
answered ad hoc each time it came up, and it is about to be answered ad hoc
again: the second physical board backend is the next large piece of M2 work.

The evidence is in
[TWATCH_S3_PLUS_BSP_REUSE](../research/TWATCH_S3_PLUS_BSP_REUSE.md), researched
under [#328](https://github.com/hleserg/Attadipa/issues/328). Four findings
drive this decision, all read at a pinned revision or in a datasheet:

1. **The shipping tree already has the seam.** `waveshare_board.cpp:125-129` —
   "esp_lcd_panel_handle_t panel" — holds an `esp_lcd_panel_handle_t` and an
   `esp_lcd_touch_handle_t` and hands them on; `physical_input.cpp:523` —
   "start_physical_input(esp_lcd_touch_handle_t touch" — takes exactly those
   two types. Nothing above that line knows which board it is. A second backend
   either reuses that
   seam or creates a second UI runtime, and the second option was never on the
   table.

2. **A vendor BSP carries policy that this project has already decided
   differently.** In `LilyGoLib@38e6f8d` `src/LilyGoWatchS3.cpp`: a missing
   PSRAM part is an unbounded `while` loop (`:105-108`); a missing PMU is
   `assert(0)` (`:126-129`); and first boot infers the battery capacity from
   whether GNSS answered a probe and writes the result to NVS (`:181-191`).
   Linking it imports four decisions that contradict the capability model and
   ADR-0016. Its dependency graph also collides with ours on LVGL and RadioLib
   before any of that is reached.

3. **The generic driver is right and still not sufficient, for a reason that is
   about the board.** ESP-IDF v5.5.5's ST7789 driver takes a software-reset
   branch when `reset_gpio_num < 0` and waits 20 ms
   (`esp_lcd_panel_st7789.c:167-177`). The T-Watch S3 Plus has no display reset
   line — three independent sources agree — so that branch is always taken, and
   the ST7789V datasheet v1.6 p.163 requires **120 ms** when the software reset
   is issued in sleep-in mode, which p.184 says is the state after every reset.
   The driver is not wrong; the *combination* of this driver and this wiring is.
   That fact belongs to something, and the only honest owner is the board.

4. **The alternative to a boundary is a boundary anyway, drawn by accident.**
   `esp_lcd_new_panel_st7789()` reads no `vendor_config` and has no init
   extension point. Anyone needing board-specific panel commands will therefore
   reach for a fork, a copied driver, or a vendor library — three worse answers
   to a question that has a good one, because the public
   `esp_lcd_panel_io_tx_param()` already accepts them.

## Decision

**A board backend is Attadipa code that composes official ESP-IDF components
and hands the runtime an `esp_lcd_panel_handle_t` and an
`esp_lcd_touch_handle_t`. A vendor BSP is evidence, cited at a revision. It is
never a link-time dependency.**

Five rules follow, and they are the whole decision:

1. **Controller and transport come from ESP-IDF or an official Espressif
   component, used as-is.** Not forked, not copied, not wrapped in a
   board-specific driver. `esp_lcd_panel_st7789` and
   `espressif/esp_lcd_touch_ft5x06` are the current instances.

2. **Board-specific panel commands, when proven necessary, are a
   board-owned data descriptor sent over the public
   `esp_lcd_panel_io_tx_param()`** — after `esp_lcd_panel_init()` and before the
   orientation calls, because the orientation calls repair the `MADCTL` such a
   table overwrites. Data, in `boards/` or `platform/`, with the source
   revision and licence notice on it. Not a driver.

3. **Necessity is proved, not assumed.** A vendor command table is `ADAPT` only
   after an experiment that separates it from the timing it is bundled with. A
   table that cannot be shown to change what the panel does is not adopted
   because a vendor ships it.

4. **The backend enables no rail, writes no NVS and holds no power policy.** It
   declares what it needs and is granted or refused by the owner ADR-0016
   creates.

5. **A driver call returning `ESP_OK` is not a capability becoming Ready.** The
   pinned touch component never reads a chip ID: success proves that something
   ACKed at `0x38` and accepted nine register writes. Readiness needs an
   observation, which is [ADR-0007](0007-two-capability-layers.md)'s rule
   restated at the driver boundary because this is where it gets forgotten.

This closes **T6** for both boards. The Waveshare BSP and LilyGoLib are the same
answer for the same reason, which is why the question kept being asked twice.

## Alternatives considered

**Link `LilyGoLib` as the T-Watch BSP.** It is MIT, it is maintained, it is the
exact board, and the factory image on our own unit is built from it — the
strongest prior art available. It loses on lifecycle, not on quality: context
finding 2. Rejected as a dependency, retained as the most valuable single source
of facts about this board.

**Build on `espressif/esp_bsp_generic` 3.1.1.** Apache-2.0 and actively
maintained, but its scope is simple I2C, SPIFFS, SD, buttons and LEDs. It models
no PMU rail graph, no second I2C bus, no absent reset line, and upstream
[#630](https://github.com/espressif/esp-bsp/issues/630) shows even runtime touch
interrupt configuration still requires editing the BSP. It would add a second
configuration framework above ESP-IDF to describe a board it cannot describe.
Its *pattern* — board-specific panel tables held at the BSP boundary, as
`esp-box-3` does — is exactly rule 2, and that is what was taken.

**Write every peripheral driver locally.** Rejected by the reuse rule and by
arithmetic: the ST7789 and FT5x06 drivers are already in the pinned SDK, already
shipping on the Waveshare path, and reimplementing them would replace tested
code with untested code to gain an extension point that
`esp_lcd_panel_io_tx_param()` already provides.

**Fork the ESP-IDF ST7789 driver to add a `vendor_config` init-command
extension**, as the Espressif `esp_lcd_ili9341` component has. Honest, and it
would be the right answer if the needed commands had to run *inside* the init
sequence. They do not: the sequence upstream itself uses is
`init()` → table → orientation. A fork buys nothing and costs an SDK component
that now diverges silently at every ESP-IDF bump.

**Leave T6 open and decide per board.** This is the status quo and it is what
produced two differently-worded rows for one question. The next backend would
re-argue it from nothing.

## Consequences

The T-Watch backend becomes a small amount of Attadipa code over components that
are already pinned and already exercised, and the existing runtime, LVGL port
and `watch_control` need no `#ifdef` and no board term. That is the point: this
ADR is what lets `core/` and `apps/` keep asking what a device can do rather
than which board it is.

The cost is that board-specific hardware facts — a command table, a reset delay
— live in Attadipa's tree and must be maintained there, with provenance, rather
than being inherited. That is a real cost and it is the intended trade: the
20 ms reset defect in context finding 3 is invisible to anyone who inherits, and
visible to anyone who owns.

**This ADR authorises no register, no delay and no clock.** It says where such a
fact belongs and what has to be true before one is written down. The specific
ST7789V3 command table stays `UNKNOWN` in necessity, and the SPI clock stays
`UNKNOWN` outright — two mature same-board implementations disagree by a factor
of two — until
[TWATCH_S3_PLUS_BSP_REUSE](../research/TWATCH_S3_PLUS_BSP_REUSE.md) §11 runs on
a board. Adopting a vendor's number because a vendor ships it is the thing rule
3 exists to prevent.
