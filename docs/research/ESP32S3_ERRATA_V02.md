# The ESP32-S3 errata, read against revision v0.2

Status: **read from the vendor document, against a revision established on the
owner's physical unit.** T-098; answers D18 in
[OPEN_QUESTIONS](OPEN_QUESTIONS.md).

The chip in question is the `ESP32-S3 (QFN56)` on the received Waveshare
`ESP32-S3-Touch-AMOLED-2.06`, revision **v0.2** — `WAFER_VERSION_MAJOR = 0`,
`WAFER_VERSION_MINOR = 2`, read by `espefuse summary` over the board's own
USB-Serial/JTAG port on 2026-08-22
([WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.1). Nothing here transfers
to the T-Watch: that board's ESP32-S3 revision has never been read, and its row
in [HARDWARE_MATRIX](HARDWARE_MATRIX.md) says only `ESP32-S3`.

## 0. The document, and when it was read

| | |
|---|---|
| Title | **ESP32-S3 Series SoC Errata**, Espressif Systems |
| Version | **v1.3**, released **2025-03-31** |
| PDF | `https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32s3/esp-chip-errata-en-master-esp32s3.pdf` |
| HTML | `https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32s3/index.html` |
| Read | **2026-08-22**, downloaded and text-extracted with `pdftotext -layout` |
| Identity | 628 995 bytes, md5 `64ffc580e78b5ab3c6c5d990e0500e38`, 15 physical pages (13 in the document's own numbering) |

The version string is the document's own: Table 4.1 *Revision History* (p. 11)
reads `2025-03-31 | v1.3 | Added Section [CACHE-126] Cache Hit Error During Cache
Write-Backs`, and every page footer reads `ESP32-S3 Series SoC Errata v1.3`. The
PDF file was rebuilt 2026-08-11 — that is a build date, not a new revision.

**Completeness cross-check, run first-hand.** The HTML tag page
`.../esp32s3/_tags/v0-2.html` was fetched separately on the same day and lists
exactly the same eight errata identifiers as Table 2.1 of the PDF. Nothing in
the v0.2 set is missing from the copy read here, and nothing has been invented
for it.

**Where the ESP-IDF claims come from.** Every statement below about what ESP-IDF
does was read in the vendor checkout at `/root/esp/esp-idf`, `git describe` =
**`v5.5.5-496-gc197d718bcc`**. That is what is on the development host; it is
**not** a version this project has chosen — see `STATUS.md` §"Build and test
state" and **T-004**. Line references to ESP-IDF are against that pin. Line
references to files in *this* repository are deliberately absent: they drift
between sessions, so sections and task identifiers are cited instead.

## 1. The eight errata, at a glance

Every erratum in the document applies to v0.2. There is no row this chip escapes.

| Erratum | ESP-IDF works around it | Kconfig | Cost of the workaround | Octal PSRAM | Quad flash | USB-Serial/JTAG | RTC domain | Light sleep |
|---|---|---|---|---|---|---|---|---|
| CACHE-126 | yes, automatic, v4.4.6+/v5.0.4+/v5.1.1+/v5.2+ | hidden `ESP_ROM_HAS_CACHE_WRITEBACK_BUG`, not switchable | mechanism CERTAIN, magnitude **UNKNOWN** | **yes** | not established, believed no | no | no | no |
| RTC-126 | yes, automatic, v4.4+ | hidden `ESP_SLEEP_RTC_BUS_ISO_WORKAROUND`, not switchable | vendor claims none; **UNKNOWN**, NOT MEASURED | no | no | no | **yes** | **yes** (light sleep only) |
| ANALOG-160 | yes, automatic, v4.4.2+/v5.0+ | none, and none possible | **UNKNOWN**, NOT MEASURED | no | no | no | **yes** | **yes**, and deep sleep |
| LCD-239 | vendor claims v4.4.5+/v5.0.3+/v5.1+; **source trace UNCONFIRMED** | none found | **UNKNOWN** | no | no | no | no | no |
| USBOTG-4289 | not a software matter — a burned eFuse | none possible | loses USB-OTG download on affected batches | no | no | **no — it is the remedy** | no | no |
| RMT-176 | yes, automatic, since v5.0 | none, hard-coded | a feature is removed (see §2.6) | no | no | no | no | no |
| TOUCH-100 | **no** — application code's job on ESP32-S3 | none | two scan cycles and the interrupt itself (see §2.7) | no | no | no | **tangentially** | no |
| ADC-183 | partly — the mode is refused, not repaired | `ADC_CONTINUOUS_FORCE_USE_ADC2_ON_C3_S3`, an **escape hatch**, default `n` | a capability is removed (see §2.8) | no | no | no | **tangentially** | no |

"Touches" means the erratum's own text names the subsystem, or the workaround
demonstrably runs in it. Where a bullet says *no*, the sheet does not mention
that subsystem in that section.

## 2. The eight errata, in full

### 2.1 CACHE-126 — cache hit error during cache write-backs

**Defect.** While a manually triggered cache write-back is in progress, an access
by the same CPU from an interrupt handler, or by the other CPU, to another
address in the same cache line is treated as a miss. The line is reloaded, two
identical entries then exist in one cache line, and the hit logic may return the
wrong copy or lose the data being written back. §3.1 p. 6 names both scenarios:
*"Accessing data in a cache line that is being written back to the cache during
an interrupt"* and *"Conflicts in a multi-core system"*.

**Affected revisions, quoted.** §3.1, p. 6: `Affected revisions: v0.0 v0.1 v0.2`.
Table 2.1 (p. 5) CACHE-126 row: v0.0 `Y`, v0.1 `Y`, v0.2 `Y`. §3.1 *Solution*,
p. 6: *"No fix scheduled."*

**ESP-IDF workaround.** Yes, automatic. §3.1 p. 6: *"This issue has been
automatically bypassed using the above methods in ESP-IDF v4.4.6+, v5.0.4+,
v5.1.1+, v5.2, and above versions."* Traced in vendor source:
`components/esp_rom/patches/esp_rom_cache_esp32s2_esp32s3.c:96` replaces the
ROM's `Cache_WriteBack_Addr`, with the comment *"1. Disable the interrupt to
prevent the current CPU accessing the same cacheline. 2. Enable dcache freeze to
prevent the another CPU accessing the same cacheline."* The freeze / write-back /
unfreeze sequence is hand-written assembly in
`components/esp_rom/patches/esp_rom_cache_writeback_esp32s3.S`, whose header
(lines 11–45) restates the erratum and both halves of the workaround in its own
words — a paraphrase, not a quotation of the sheet.

The patch applies interrupt masking **and** the dcache freeze only to the
unaligned head and tail cache lines: `XTOS_SET_INTLEVEL(XCHAL_NMILEVEL)` around
`cache_writeback_items_freeze()` at `esp_rom_cache_esp32s2_esp32s3.c:118-142`.
For the aligned bulk it suspends DCache autoload and calls the ROM routine, and
the interrupt-disable half comes from the caller's spinlock in
`components/esp_mm/esp_cache_msync.c:46-69`.

**Kconfig.** Not user-gated. The patch is enabled by the hidden SoC-caps symbol
`ESP_ROM_HAS_CACHE_WRITEBACK_BUG`
(`components/esp_rom/esp32s3/esp_rom_caps.h:33` = 1, declared without a prompt in
`components/esp_rom/esp32s3/Kconfig.soc_caps.in:102`), so it cannot be switched
off in `menuconfig`. The neighbouring hidden symbol
`ESP_ROM_HAS_CACHE_SUSPEND_WAITI_BUG` (`esp_rom_caps.h:32`) adds the
`Cache_Wait_Idle()` spins to the freeze and suspend calls. The only user-visible
knob on this path is `CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS`
(`components/esp_mm/Kconfig`, a prompted bool with **no** `default` line, so
default `n`) together with
`CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS_MAX_LEN` (`default 0x8000 if
IDF_TARGET_ESP32S3`). That option slices the critical section; it does not
disable the workaround, and its own help text warns that slicing reintroduces a
coherence hazard if an ISR touches the same buffer.

**Cost — the one with teeth for this design.** Mechanism **CERTAIN** from vendor
source; magnitude **UNKNOWN**, `NOT MEASURED`. Espressif publish no number and
nothing has run on the owner's board. Four distinct costs:

1. All interrupts are masked at `XCHAL_NMILEVEL` — 7 on this core, above the
   `XCHAL_NUM_INTLEVELS` = 6 the chip has, i.e. everything including the
   high-priority handlers — for one cache line, twice per unaligned C2M call
   (`components/xtensa/esp32s3/include/xtensa/config/core-isa.h:359,417`).
2. The data cache is **frozen** during that window. This is not an inference:
   the patch's own header states *"If Core Y attempts the access any address in
   the cache region, Core Y will busy wait until the cache is unfrozen"*. Real
   cross-core stall; duration UNKNOWN.
3. DCache autoload (prefetch) is suspended across the aligned bulk write-back, so
   PSRAM read throughput during a flush is below steady state. Delta UNKNOWN.
4. At caller level, with the default `CHUNKED_OPS=n`, interrupts are disabled on
   the calling core for the **entire** C2M write-back. On this board that is the
   display flush: 410 × 502 RGB565 = **411 640 bytes** (402.0 KiB) per full-frame
   `esp_cache_msync`. The interrupt-off time for that is UNKNOWN and must be
   MEASURED before any timing budget — audio DMA, radio, FreeRTOS tick — is
   written against it. Enabling chunking at `0x8000` bounds it to roughly 13
   slices, at the price of the coherence caveat above.

No RAM cost. No clock cost.

**Touches.**

- **Octal PSRAM — yes, directly.** ESP32-S3 has a write-back data cache for
  external memory (`SOC_CACHE_WRITEBACK_SUPPORTED` = 1,
  `components/soc/esp32s3/include/soc/soc_caps.h:140`), and the dirty lines being
  written back are PSRAM lines. Every `esp_cache_msync(..., DIR_C2M)` on a PSRAM
  buffer — DMA framebuffers, audio buffers — goes through the patched routine.
- **Quad flash — NOT ESTABLISHED, believed no.** Reasoning, offered as reasoning:
  the write-back is a DCache operation on dirty lines, flash is mapped read-only
  through the cache, so flash lines should never be dirty. Corroborating but not
  decisive: the patch's assembly header describes itself as writing back *"the
  cache items of DCache"*. No ESP32-S3 TRM citation has been taken, and the
  erratum text says only *"external memory"*.
- **USB-Serial/JTAG — no.** Not mentioned in §3.1.
- **RTC domain — no.** Not mentioned in §3.1.
- **Light sleep / deep sleep — no.** The erratum names no sleep condition, and on
  ESP32-S3 the ESP-IDF sleep path does not invoke the patched
  `Cache_WriteBack_Addr` at all. The three write-back call sites on the sleep
  path are excluded on this target and all call `*_All` ROM variants rather than
  the patched address routine: `sleep_modes.c:568` is inside
  `#if CONFIG_ESP_SLEEP_CACHE_SAFE_ASSERTION && CONFIG_IDF_TARGET_ESP32P4`,
  `sleep_modes.c:753-756` is explicitly ESP32-C5, and `sleep_retention.c:978`
  requires `SOC_PAU_SUPPORTED`, which ESP32-S3 does not have.

### 2.2 RTC-126 — RTC register read error after wake-up from light sleep

**Defect.** §3.2 p. 7: *"If an RTC peripheral is turned off in Light-sleep mode,
there is a certain probability that after waking up from Light-sleep, the CPU of
ESP32-S3 will read the registers in the RTC power domain incorrectly."*

**Affected revisions, quoted.** §3.2, p. 6: `Affected revisions: v0.0 v0.1 v0.2`.
Table 2.1 (p. 5) RTC-126 row: `Y` / `Y` / `Y`. §3.2 *Solution*, p. 7: *"No fix
scheduled."*

**ESP-IDF workaround.** Yes, automatic and unconditional on ESP32-S3. §3.2 p. 7:
*"Users are suggested not to power down RTC peripherals in Light-sleep mode.
There will be no impact on power consumption. This issue has been bypassed in
ESP-IDF v4.4 and above."* Traced:
`components/esp_hw_support/sleep_modes.c:2886-2890` —

```c
#ifdef CONFIG_ESP_SLEEP_RTC_BUS_ISO_WORKAROUND
    if (!deepsleep) {
        sleep_flags &= ~RTC_SLEEP_PD_RTC_PERIPH;
    }
#endif
```

The scope matches the erratum exactly: the RTC-peripheral power-down flag is
cleared for light sleep and left alone for deep sleep. The mapping from Kconfig
symbol to erratum is confirmed rather than inferred — `git log -S
ESP_SLEEP_RTC_BUS_ISO_WORKAROUND` returns one introducing commit, `a82f33c9b5f`,
2021-07-16, *"fix rtc register read error and add workaround for rtc bus isolate
issue"*, touching exactly `components/esp_hw_support/Kconfig` and
`sleep_modes.c`. The date is consistent with the sheet's "v4.4 and above".

**Kconfig.** `CONFIG_ESP_SLEEP_RTC_BUS_ISO_WORKAROUND`,
`components/esp_hw_support/Kconfig:139-141`: a `bool` with **no prompt string**
and `default y if IDF_TARGET_ESP32 || IDF_TARGET_ESP32S2 || IDF_TARGET_ESP32S3`.
Hidden, on, and not switchable from `menuconfig`. The consequence is worth
stating plainly: **on ESP32-S3 under ESP-IDF, the RTC_PERIPH power domain cannot
be powered down in light sleep.**

**Cost.** Espressif's claim is *"There will be no impact on power consumption"*
(§3.2, p. 7). That is a **VENDOR CLAIM**, not a measurement, and this repository
holds no light-sleep current figure for either board — **UNKNOWN**,
`NOT MEASURED`. What is structurally certain is that a power domain the silicon
can gate stays powered in every light sleep on this chip, on a device expected to
spend most of its life in light sleep. No clock, RAM, throughput or feature cost.
Deep sleep is unaffected.

**Touches.** Octal PSRAM no. Quad flash no. USB-Serial/JTAG no. **RTC domain —
yes; this erratum is an RTC-domain erratum.** **Light sleep — yes; deep sleep —
no**, the erratum names light sleep only and the workaround is gated on
`if (!deepsleep)`.

### 2.3 ANALOG-160 — the chip is damaged when BIAS_SLEEP = 0 and PD_CUR = 1

**Defect.** §3.3 p. 7: *"If the analog power is configured as BIAS_SLEEP = 0 and
PD_CUR = 1, the chip will be permanently damaged. This issue might be triggered
when ULP and/or touch sensor is used during Light-sleep or Deep-sleep."* This is
the only erratum on the list whose failure mode is a dead board.

**Affected revisions, quoted.** §3.3, p. 7: `Affected revisions: v0.0 v0.1 v0.2`.
Table 2.1 (p. 5) ANALOG-160 row: `Y` / `Y` / `Y`. §3.3 *Solution*, p. 7: *"No fix
scheduled."*

**ESP-IDF workaround.** Yes, automatic. §3.3 p. 7: *"Users are suggested to
disable such analog power configuration in sleep mode through software. This
issue has been bypassed by disabling the above configuration in ESP-IDF v4.4.2+,
v5.0 and above."* Traced in
`components/esp_hw_support/port/esp32s3/rtc_sleep.c`: the configuration builder
(lines 158–171) **never emits `BIAS_SLEEP = 0` together with `PD_CUR = 1`**. It
produces `(bias_sleep = ON, pd_cur = ON)`, `(bias_sleep = DEFAULT, pd_cur =
DEFAULT)`, and — in the `RTC_SLEEP_PD_XTAL` branch with
`RTC_SLEEP_USE_ADC_TESEN_MONITOR` set, lines 165–167 — `(bias_sleep = DEFAULT,
pd_cur = ON)`. Beware the polarity: in
`components/esp_hw_support/port/esp32s3/include/soc/rtc.h:123-133` the constants
named `*_ON` are **0** and `*_DEFAULT` are **1**, so that third pair is
`BIAS_SLEEP = 1, PD_CUR = 0` — safe, and not the destructive combination.
`rtc_sleep_init()` then asserts the invariant before touching the register
(`rtc_sleep.c:211-212`):

```c
assert(!cfg.pd_cur_monitor || cfg.bias_sleep_monitor);
assert(!cfg.pd_cur_slp     || cfg.bias_sleep_slp);
```

i.e. `PD_CUR = 1` is permitted only alongside `BIAS_SLEEP = 1`, so the
destructive pair cannot reach `RTC_CNTL_BIAS_CONF_REG` (written at
`rtc_sleep.c:216-222`).

**Kconfig.** None. There is no option, and that is the right design: the failure
mode is destroyed silicon, not a wrong result.

**Cost.** **UNKNOWN**, `NOT MEASURED`. The sheet quotes no cost. There is no
clock, RAM or throughput penalty, and the forgone configuration is by definition
one that must never be used, so the only conceivable cost is whatever sleep
current the forbidden pairing would have saved — a figure Espressif do not
publish and nobody here has measured. The guard itself costs two asserts, which
compile out under `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE`; the
configuration builder still never produces the bad pair, so disabling asserts
does not re-expose the chip through ESP-IDF's own path.

**Touches.** Octal PSRAM no. Quad flash no. USB-Serial/JTAG no. **RTC domain —
yes**: `RTC_CNTL_BIAS_CONF_REG` and its `BIAS_SLEEP_*` / `PD_CUR_*` fields are
RTC-domain analog-power controls, and the triggers the sheet names — ULP and
touch sensor — are RTC-domain blocks. **Light sleep and deep sleep — both**, in
the sheet's own words. This is the one erratum that lives in exactly the code
path a low-power wearable is most tempted to hand-write.

### 2.4 LCD-239 — the LCD module misbehaves at certain clock dividers

**Defect.** Two cases, §3.4 p. 8. In RGB mode with
`LCD_CAM_LCD_CLK_EQU_SYSCLK = 1`, the pixel clock cannot be set to falling-edge
trigger, and with continuous frames *"it might occur that the second frame
inserts the last data of the previous frame in the first frame."* In I8080 mode,
if `ahead_cycle` — the LCD_CLK cycle count before data transmission — is less
than or equal to 2, *"it can result in incorrect value of the first data and the
subsequent data quantity."*

**Affected revisions, quoted.** §3.4, p. 7: `Affected revisions: v0.0 v0.1 v0.2`.
Table 2.1 (p. 5) LCD-239 row: `Y` / `Y` / `Y`. §3.4 *Solution*, p. 9: *"No fix
scheduled."*

**ESP-IDF workaround — VENDOR CLAIM, source trace UNCONFIRMED.** §3.4 p. 8 says
*"This issue has been bypassed through the methods described above in ESP-IDF
v4.4.5+, v5.0.3+, v5.1 and above."* That sentence stands as Espressif's claim.
The mechanism has **not** been located in vendor source at the pinned checkout,
and the obvious candidate is not it:
`components/hal/esp32s3/include/hal/lcd_ll.h:121-124` carries
`HAL_ASSERT(div_num >= 2 && ...)`, but that governs `lcd_clkm_div_num`, the
module-clock-to-LCD_CLK **group** divider, which is a different register from the
`LCD_CAM_LCD_CLK_EQU_SYSCLK` **pixel** prescale the erratum names. Three things
disprove the identification: the `>= 2` assert predates the erratum, arriving
with the original ESP32-S3 LCD LL driver (`d0be56b8fe9`, tightened in
`2ab7d927854`, 2022-01-21) while LCD-239 first appears in errata v1.2 of
2023-11-15; `lcd_ll_set_pixel_clock_prescale()` at `lcd_ll.h:165-178` — the only
writer of `lcd_clk_equ_sysclk` — **sets it to 1** whenever `prescale == 1`, so
v5.5.5 does not forbid the trigger; and `esp_lcd_panel_io_i80.c:351-353` accepts
`pclk_prescale == 1`, so the trigger is reachable from public API. A candidate
i8080-side mitigation exists as a lead only —
`lcd_ll_set_blank_cycles(bus->hal.dev, 1, 1)` at `esp_lcd_panel_io_i80.c:791`,
added 2023-04-03 in `e73d8166aa1` — but that commit's message is *"i80_lcd:
support skip command phase"* and cites no erratum. **UNCONFIRMED.** It was not
chased further, because see below.

**Kconfig.** None found.

**Cost.** Espressif document no cost in §3.4. Whether ESP-IDF pays one is
**UNKNOWN**, because the workaround has not been located in vendor source. Cost
to this project: none, as long as LCD_CAM is unused.

**Touches.** Octal PSRAM no. Quad flash no. USB-Serial/JTAG no. RTC domain no.
Light sleep no.

**Project relevance: none today, and that is why the trace was not chased.** The
display on this board is a CO5300 410 × 502 AMOLED driven over **QSPI on the SPI
peripheral** — CS 12, PCLK 11, D0–D3 on 4/5/6/7, RST 8, through the
`esp_lcd_sh8601` family driver ([HARDWARE_MATRIX](HARDWARE_MATRIX.md), Waveshare
display and driver rows). LCD_CAM is not involved, so neither the RGB nor the
I8080 path of this erratum is exercised. **If LCD_CAM is ever used** — a camera,
a parallel peripheral — re-open this entry and finish the trace first.

### 2.5 USBOTG-4289 — the USB-OTG download function is unavailable

**Defect.** §3.5 p. 9: *"For ESP32-S3 series chips manufactured before the Date
Code 2219 and series of modules and development boards with the PW Number before
PW-2022-06-XXXX, the EFUSE_DIS_USB_OTG_DOWNLOAD_MODE (BLK0 B19[7]) bit of eFuse
is set by default and cannot be modified. Therefore, the USB-OTG Download
function is unavailable for these products."*

**Affected revisions, quoted.** §3.5, p. 9: `Affected revisions: v0.0 v0.1 v0.2`.
Table 2.1 (p. 5) USBOTG-4289 row: v0.0 `Y`, v0.1 `Y`, v0.2 **`Y*`**, with the
footnote directly under Table 2.1 (p. 5): *"`Y*` means some batches of a
revision are affected."* This is the only erratum in the document with a
batch-level qualifier. §3.5 *Solution*, p. 9: *"This issue has been fixed in some batches of
chip revision v0.2. For ESP32-S3 series chips manufactured on and after the Date
Code 2219 and ESP32-S3 series modules and development boards with the PW Number
of and after PW-2022-06-XXXX, the bit (BLK0 B19[7]) will not be programmed by
default and thus is open for users to program."*

**ESP-IDF workaround — none, and none is possible.** Software cannot unburn an
eFuse. The sheet's remedy is a different download path: §3.5 p. 9, *"ESP32-S3
also supports downloading firmware through USB-Serial-JTAG."* That is precisely
and exclusively how this board is used —
[WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.1 records `USB mode =
USB-Serial/JTAG`, §1.5 records `DIS_USB_SERIAL_JTAG = False`, and every
`espefuse` and `esptool` operation on the owner's unit ran over that port. In
ESP-IDF the field is `DIS_USB_OTG_DOWNLOAD_MODE`, `EFUSE_BLK0` bit 159
(`components/efuse/esp32s3/esp_efuse_table.csv:180`, *"Set this bit to disable
download through USB-OTG"*), write-protected by `WR_DIS` bit 19 (same file,
line 60).

**Kconfig.** None, and none is possible.

**Cost.** On an affected unit the USB-OTG **download** path is gone permanently.
Cost to this project: none in practice — USB-Serial/JTAG is already the console
and the flashing path, and nothing has been burned. USB-OTG remains usable as a
device or host peripheral either way; the erratum concerns the ROM download
function, not the OTG controller. No clock, current, RAM or throughput cost.

**Touches.** Octal PSRAM no. **Quad flash — no**, not the SPI interface; it
affects one path by which flash can be written from a host, not the flash bus.
**USB-Serial/JTAG — no, and the distinction matters**: USB-Serial/JTAG is the
*remedy* the sheet prescribes, not the victim. The board's only console sits on
the side of this erratum that still works. RTC domain no. Light sleep no.

### 2.6 RMT-176 — idle-state signal level in continuous TX mode

**Defect.** §3.6 p. 10: after continuous transmission stops, *"the channel's idle
state signal level is not controlled by the 'level' field of the end-marker, but
by the level in the data wrapped back, which is indeterminate."*

**Affected revisions, quoted.** §3.6, p. 9: `Affected revisions: v0.0 v0.1 v0.2`.
Table 2.1 (p. 5) RMT-176 row: `Y` / `Y` / `Y`. §3.6 *Solution*, p. 10: *"No fix
scheduled."*

**ESP-IDF workaround.** Yes, automatic and unavoidable. §3.6 p. 10: *"Users are
suggested to set RMT_IDLE_OUT_EN_CHn to 1 to only use registers to control the
idle level. This issue has been bypassed since the first ESP-IDF version that
supports continuous TX mode (v5.0)."* Traced:
`components/esp_driver_rmt/src/rmt_tx.c:332` and `:761` both call
`rmt_ll_tx_fix_idle_level(..., true)` with the enable argument hard-coded, and
`components/hal/esp32s3/include/hal/rmt_ll.h:415` writes
`chnconf0[channel].idle_out_en_chn`.

**Kconfig.** None. Hard-coded in the driver.

**Cost.** A feature is removed rather than a resource spent: the end-marker's
`level` field can no longer control the idle level, so it must be set through the
register — `init_level` at channel creation, `eot_level` per transaction.
Espressif document no other cost, and no clock, current, RAM or throughput
penalty is visible in the mechanism. Whether the removed feature matters is
**UNKNOWN** in general; it matters to nothing in this project today.

**Touches.** Octal PSRAM no. Quad flash no. USB-Serial/JTAG no. RTC domain no.
Light sleep no.

**Project relevance: none established.** No RMT consumer is recorded for either
board in [HARDWARE_MATRIX](HARDWARE_MATRIX.md). If an addressable-LED or IR
feature ever appears, this constrains it: set the idle level explicitly, never
rely on the end-marker.

### 2.7 TOUCH-100 — the first two scan-done interrupts carry undefined data

**Defect.** §3.7 p. 10: *"For ESP32-S3's touch sensor, the raw data value is
undefined for the first two TOUCH_SCAN_DONE_INT interrupts."*

**Affected revisions, quoted.** §3.7, p. 10: `Affected revisions: v0.0 v0.1
v0.2`. Table 2.1 (p. 5) TOUCH-100 row: `Y` / `Y` / `Y`. §3.7 *Solution*, p. 10:
*"No fix scheduled."*

**ESP-IDF workaround — no.** This is the entry that contradicts the assumption
that ESP-IDF handles everything. §3.7 p. 10 gives user guidance and, unlike every
other software-fixable erratum in this document, names **no ESP-IDF version**:
*"Users are suggested to skip the first two TOUCH_SCAN_DONE_INT interrupts, then
turn them off and stop using them."* Checked against source: ESP32-S3 uses the
`hw_ver2` touch driver, and the only scan-done filtering in
`components/esp_driver_touch_sens/hw_ver2/touch_version_specific.c:93-106` is
guarded by `#if CONFIG_IDF_TARGET_ESP32S2` and addresses a different bug (*"the
fake scan done interrupt. (Only happens when both channel 13 and 14 are
enabled)"*). On ESP32-S3 the driver invokes the user's `on_scan_done` callback
from the first interrupt onward. **Skipping the first two is application code's
job.**

**Kconfig.** None — there is no workaround to gate.

**Cost.** The prescribed workaround costs the first two scan cycles of touch data
and then the use of `TOUCH_SCAN_DONE_INT` altogether (*"then turn them off and
stop using them"*). No clock, current, RAM or throughput cost is implied by that
mechanism.

**Touches.** Octal PSRAM no. Quad flash no. USB-Serial/JTAG no. **RTC domain —
tangentially**: the ESP32-S3 touch controller and its interrupt live in the RTC
domain (`RTCCNTL.int_ena_w1ts.rtc_touch_scan_done_w1ts` and neighbours,
`components/hal/esp32s3/include/hal/touch_sensor_ll.h:1267-1353`). It does not
affect RTC register access, RTC timekeeping or the RTC power domain this design
depends on. **Light sleep — no** directly, though touch is a wake source and
ANALOG-160 is the sleep hazard in the same block.

**Project relevance: none.** Neither board uses the SoC's internal touch sensor.
The Waveshare board's touch is an **FT3168** on I2C (INT 38, RST 9) and the
T-Watch's is an **FT6336U** ([HARDWARE_MATRIX](HARDWARE_MATRIX.md), touch rows on
both boards). Both are external I2C parts, so no `TOUCH_SCAN_DONE_INT` is ever
raised.

### 2.8 ADC-183 — the digital (DMA) controller of SAR ADC2 cannot work

**Defect.** §3.8 p. 11: *"The Digital Controller of SAR ADC2, i.e., DIG ADC2
controller, may receive a false sampling enable signal. In such a case, the
controller will enter an inoperative state."*

**Affected revisions, quoted.** §3.8, p. 10: `Affected revisions: v0.0 v0.1
v0.2`. Table 2.1 (p. 5) ADC-183 row: `Y` / `Y` / `Y`. §3.8 *Solution*, p. 11:
*"No fix scheduled."*

**ESP-IDF workaround — partly.** ESP-IDF refuses the broken mode rather than
repairing it, and the sheet names no version: §3.8 p. 11 says only *"It is
suggested to use RTC controller to control SAR ADC2."* In source,
`components/soc/esp32s3/include/soc/soc_caps.h:103` declares
`SOC_ADC_DIG_SUPPORTED_UNIT(UNIT) ((UNIT == 0) ? 1 : 0)` — the digital controller
supports ADC1 only — and `components/esp_adc/adc_continuous.c:462` logs *"ADC2
continuous mode is no longer supported, please use ADC1. Search for errata on
espressif website for more details."* The hard rejection at `:471` sits inside
`#if !CONFIG_ADC_CONTINUOUS_FORCE_USE_ADC2_ON_C3_S3`. One-shot ADC2 through the
RTC controller is unaffected on ESP32-S3.

**Kconfig.** `CONFIG_ADC_CONTINUOUS_FORCE_USE_ADC2_ON_C3_S3`,
`components/esp_adc/Kconfig:59-68`: `depends on IDF_TARGET_ESP32C3 ||
IDF_TARGET_ESP32S3`, `default n`, help text *"On ESP32C3 and ESP32S3, ADC2 Digital
Controller is not stable… If you stick to this, you can enable this option to
force use ADC2."* **This is an escape hatch that re-exposes the erratum, not a
switch that enables a fix.** Leaving it at `n` is the safe state. Do not set it.

**Cost.** A capability is removed: ADC2 continuous/DMA mode is unavailable, so
all continuous ADC work must use ADC1. On ESP32-S3 ADC1 is GPIO1–GPIO10 and ADC2
is GPIO11–GPIO20, which makes this a pin-budget constraint rather than a clock or
current one. Espressif document no other cost, and none is visible in the
mechanism. Whether it constrains this project is **UNKNOWN** — no ADC consumer is
recorded for either board.

**Touches.** Octal PSRAM no. Quad flash no. USB-Serial/JTAG no. **RTC domain —
tangentially**: the prescribed workaround is to drive SAR ADC2 from the RTC
controller, and the SAR ADC block sits in the RTC/analog domain. RTC register
access, timekeeping and power gating are unaffected. **Light sleep — no**
directly; note only that ULP-driven ADC use in sleep goes through the RTC
controller, which is the side that still works.

**Project relevance: low.** The owner's unit has ADC and temperature calibration
fuses burned (`BLK_VERSION_MAJOR` = ADC calib V1, `ADC1_INIT_CODE_ATTEN0..3`,
`ADC1_CAL_VOL_ATTEN0..3` and their ADC2 counterparts, `TEMP_CALIB = -10.7` —
[WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §1.4), so calibrated readings are
available. This erratum removes only the ADC2 DMA path.

## 3. The question with the most consequence

**Is there anything the owner's v0.2 unit suffers that a newer revision would
not? No — because there is no newer revision.** v0.2 is the newest ESP32-S3
silicon Espressif document, and it is the best of the three that exist.

The evidence is all primary:

1. **The errata sheet knows only three revisions.** Table 1.1 *Chip Revision
   Identification by eFuse Bits* (p. 1) has exactly three columns: v0.0, v0.1,
   v0.2. Table 1.2 *by Chip Marking* (p. 2) lists the same three. Table 1.3 *by
   Module Marking* (p. 3) likewise, with a footnote that v0.0 modules *"are not
   mass produced"*. Table 2.1 (p. 5) has three affected-revision columns and no
   fourth.
2. **Nothing was fixed by moving to v0.2.** All eight sections carry the identical
   `Affected revisions: v0.0 v0.1 v0.2`, and — with the single exception of
   USBOTG-4289 — the Solution *"No fix scheduled."* A v0.2 chip is therefore no
   worse than v0.1 or v0.0 on any entry.
3. **ESP-IDF agrees independently.** `/root/esp/esp-idf/COMPATIBILITY.md:79-83`
   has one ESP32-S3 heading, *"#### v0.1, v0.2 — Supported since ESP-IDF v4.4"*,
   and no later revision.
   `components/esp_hw_support/port/esp32s3/Kconfig.hw_support` offers exactly
   three minimum-revision choices: `Rev v0.0 (ECO0)`, `Rev v0.1 (ECO1)`,
   `Rev v0.2 (ECO2)`.
4. **The one revision-dependent improvement in the document lands inside v0.2, in
   the owner's favour.** USBOTG-4289 is `Y*` for v0.2 with the footnote *"`Y*`
   means some batches of a revision are affected"*, and §3.5 *Solution* (p. 9)
   says the issue *"has been fixed in some batches of chip revision v0.2."* v0.2
   is the first revision that can be free of it. Whether this die is in a fixed
   batch is §7's first unknown, and the downside if it is not is nil for this
   project: the board flashes and consoles over USB-Serial/JTAG.

**Scope of the claim.** This is a statement about errata sheet v1.3 (2025-03-31)
and ESP-IDF v5.5.5, not a promise about silicon Espressif may release later. If a
v0.3 or v1.0 ever appears, re-derive this answer; the sheet's own Revision
History (Table 4.1) is where it would show.

**One build consequence, reconfirmed rather than new.** Keep
`CONFIG_ESP32S3_REV_MIN` at **0**. The bootloader refuses to boot an image whose
minimum revision exceeds the chip's. Since this chip is v0.2 and v0.2 is the
maximum, `REV_MIN_2` would also boot here — but it buys nothing, because no
erratum is fixed in v0.2 except a batch-level eFuse default, and it would make
the image refuse any v0.0 or v0.1 board. Already recorded in `STATUS.md`
§"The Waveshare board arrived — 2026-08-22" and
[WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §3.1.

## 4. Adjacent, and **not** an erratum

Recorded here because it lands on two of the five subsystems this task named, and
fenced off so that nobody greps it into the errata list. **It appears nowhere in
errata v1.3.**

`components/esp_hw_support/Kconfig:114` defines
`CONFIG_ESP_SLEEP_PSRAM_LEAKAGE_WORKAROUND` — *"Pull-up PSRAM CS pin in light
sleep"*, `depends on SPIRAM`, `default y` — whose help text states verbatim that
selecting it *"will increase the sleep current about 10 uA"*. The neighbouring
`CONFIG_ESP_SLEEP_MSPI_NEED_ALL_IO_PU` is `default y` for ESP32-S3. That is a
documented and quantified light-sleep current cost tied to external PSRAM, but it
is a board-leakage matter, not silicon errata. It belongs to whichever task
builds the sleep-current budget.

## 5. What changes in the record

This list said "not applied here; listed so the next writer can apply them" —
and this **is** the commit that carries the note, so leaving three of five rows
undone would have left the repository contradicting itself in exactly the way
[T-102's checker](../../tools/docs/check_docs.py) exists to catch and cannot,
because contradictions in prose are not a syntax error. An adversarial re-read
caught it. Applied state as of this commit:

| Where | Change | Done |
|---|---|---|
| [OPEN_QUESTIONS](OPEN_QUESTIONS.md) D18 | was `UNKNOWN`; now **answered** — all eight v0.2 errata listed here, with what each touches | **yes** |
| `STATUS.md` | the line reading "Which errata apply to v0.2 is **D18**, unread" is no longer true | **yes** |
| [VERIFIED_FACTS](VERIFIED_FACTS.md) | the errata document identity (v1.3, 2025-03-31, md5 above) and the "v0.2 is the newest revision" finding of §3 | **yes** |
| [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) | §3.1 said "nobody has read that sheet against v0.2"; §4 carried it as an open question | **yes** |
| [HARDWARE_MATRIX](HARDWARE_MATRIX.md) | the Waveshare SoC row can cite this note beside its `REV_MIN` sentence | **yes** |
| `TASKS.md` | two consequences want their own tasks — the ANALOG-160 register rule and CI grep (§7.4), and the CACHE-126 measurement that gates the interrupt-latency budget (§7.2). T-004 gains the version floor in §7.5 | **yes** |

## 6. Where the first reading of this document was wrong

Kept because a corrected claim is more useful than a silently replaced one, and
because two of these are the kind of mistake that gets re-made.

- **LCD-239's ESP-IDF workaround was attributed to the wrong register.** The
  first reading cited `HAL_ASSERT(div_num >= 2)` in `lcd_ll.h` as the fix. That
  assert governs the group divider `LCD_CLKM_DIV_NUM`, not the
  `LCD_CAM_LCD_CLK_EQU_SYSCLK` pixel prescale the erratum names; it predates the
  erratum by more than a year; and the trigger is still reachable at v5.5.5. The
  entry now reads VENDOR CLAIM with the source trace `UNCONFIRMED` (§2.4). The
  claimed cost — "the top LCD_CAM pixel clock is halved" — went with it: it was a
  restatement of the group-divider constraint, not a price paid for this
  workaround. Cost is now `UNKNOWN`.
- **CACHE-126 was said to run on the sleep path.** It does not, on this target.
  All three sleep-path write-backs are excluded on ESP32-S3 and none of them
  calls the patched routine (§2.1). The bullet is now a flat *no*.
- **CACHE-126's quad-flash bullet was stated as a fact.** It is an inference with
  no TRM citation, and now says so.
- **ANALOG-160's enumeration of emitted register pairs was incomplete** — a third,
  safe pair exists at `rtc_sleep.c:165-167`. The safety conclusion is unchanged;
  the claim is now the narrower and correct one, that the builder never emits
  `BIAS_SLEEP = 0` with `PD_CUR = 1`.
- **The assembly patch header paraphrases the erratum; it does not quote it.**
  "Verbatim" is gone. The header earned its place anyway: its busy-wait sentence
  turns the cross-core stall from an inference into a vendor statement.
- **Page and line drift.** ANALOG-160's *Solution* is on p. 7, not p. 8 — the
  footer for page 7 falls after it. Several `STATUS.md` line numbers cited during
  verification had already moved by the time this note was written, which is why
  this repository's files are cited here by section and task identifier only.

## 7. Still UNKNOWN

Each of these is phrased so it can become an issue as it stands.

1. **Is this die in a USBOTG-4289-fixed batch?** The repository's `espefuse`
   record does not quote `DIS_USB_OTG_DOWNLOAD_MODE`; §1.5 of
   [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) records `WR_DIS = 0` and says
   everything else was at factory default — which is ambiguous for a bit whose
   *factory default differs by batch*, and `WR_DIS = 0` settles nothing, because
   eFuses only go 0 → 1 and a bit already set cannot be cleared regardless of
   write protection. **Concrete check, read-only and safe:** in the `espefuse
   summary` the owner already captured, look for the `DIS_USB_OTG_DOWNLOAD_MODE`
   line. `True` means a pre-Date-Code-2219 batch and USB-OTG download is gone for
   good; `False` means the batch is fixed and the bit is open to program. A
   second, non-electrical route is the Date Code in the chip silk marking (errata
   §1.3, p. 3; ≥ 2219 means fixed) — the mainboard photographs in
   [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) were read for
   silkscreen, and no die-marking read is on record. Either answer costs nothing
   and changes nothing about how this board is flashed.
2. **CACHE-126's magnitude has never been measured.** How long interrupts are off
   during an `esp_cache_msync` C2M write-back of a ~402 KiB PSRAM framebuffer on
   this unit, how long the other core stalls on the frozen dcache, and how much
   PSRAM read throughput is lost while autoload is suspended are all
   `NOT EXECUTED — HARDWARE REQUIRED`. ESP-IDF ships a performance test that
   reports *"Cache freeze time"*
   (`components/esp_mm/test_apps/mm/main/test_cache_utils.c:44-57`) but no value
   exists for this board. **This number must be obtained before any
   interrupt-latency budget — audio DMA, radio timing, FreeRTOS tick — is
   committed**, and it decides whether
   `CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS` should be turned on.
3. **RTC-126's "no impact on power consumption" is a vendor claim, not a
   measurement.** This repository holds no light-sleep current figure for either
   board. There is also no A/B configuration available to measure it against,
   because the workaround's Kconfig symbol is hidden and `default y` on
   ESP32-S3 — testing the delta would mean patching `sleep_modes.c:2886-2890`,
   which is not a casual thing to do on a chip whose neighbouring erratum
   destroys silicon. `NOT MEASURED`.
4. **ANALOG-160 needs a rule this repository enforces, not just a fact.** ESP-IDF
   never programs the destructive pair and asserts against it, but that protection
   covers only code that goes through `rtc_sleep_init()`. Any direct write to
   `RTC_CNTL_BIAS_CONF_REG` — a hand-tuned low-power path, a snippet copied from
   another firmware, a vendor BSP — bypasses it, and the polarity is inverted
   (`*_ON` = 0, `*_DEFAULT` = 1), which is exactly how such a write goes wrong.
   **Recommended as its own task:** a written rule plus a CI grep that no code
   outside the sleep port writes `RTC_CNTL_BIAS_CONF_REG`.
5. **Every "ESP-IDF handles it" above is conditional on T-004.** The fix
   thresholds are v4.4.6+, v5.0.4+, v5.1.1+, v5.2+ (CACHE-126), v4.4+ (RTC-126),
   v4.4.2+ / v5.0+ (ANALOG-160), v4.4.5+ / v5.0.3+ / v5.1+ (LCD-239) and v5.0+
   (RMT-176). Every one is at or below v5.2, so any modern pin clears them all —
   but the pin has not been chosen, and **T-004 now inherits a floor from this
   note**: whatever version is chosen must be at or above those, per series.
6. **TOUCH-100 has no ESP-IDF workaround on ESP32-S3 and will need application
   code if the peripheral is ever used.** It costs nothing today because both
   boards use external I2C touch controllers. It becomes live the moment the
   SoC's internal touch block is used for anything — a capacitive wake pad, a
   bezel gesture — at which point application code must skip the first two
   `TOUCH_SCAN_DONE_INT` interrupts itself.
7. **LCD-239's ESP-IDF workaround has not been located in source.** The sheet
   claims it exists from v4.4.5+/v5.0.3+/v5.1+; the obvious candidate is a
   different divider, and the erratum's trigger
   (`lcd_clk_equ_sysclk = 1`) is still reachable from public API at v5.5.5. This
   is `UNCONFIRMED` rather than refuted, and it is deliberately left that way
   because this board's display is QSPI on the SPI peripheral. **Re-open before
   any use of LCD_CAM** — a camera, a parallel display, a parallel peripheral.
8. **An asymmetry in the CACHE-126 patch, recorded so it is not re-derived.** The
   errata's workaround has two halves — disable interrupts on this CPU, freeze
   the cache against the other. The ROM patch applies both, but only to the
   unaligned head and tail lines; for the aligned bulk it suspends autoload and
   calls the unpatched ROM routine, with the interrupt half supplied by the
   caller's spinlock and no cache freeze. Whether the bulk path needs the freeze
   is a question for Espressif, not a conclusion to draw here. It is written down
   because the next agent reading the sheet beside the source will notice the
   same thing.
9. **The T-Watch's ESP32-S3 revision has never been read.** Everything in this
   note is about the die in the Waveshare unit. The T-Watch SoC row in
   [HARDWARE_MATRIX](HARDWARE_MATRIX.md) says `ESP32-S3` and nothing more, so
   which of v0.0 / v0.1 / v0.2 it is remains `UNKNOWN`. In practice the answer
   changes little — all eight errata affect all three revisions — but it is the
   input to `CONFIG_ESP32S3_REV_MIN` for that target and it is one `espefuse
   summary` away.
