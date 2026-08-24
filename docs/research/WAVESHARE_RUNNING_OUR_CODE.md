# Running our own code on the received unit: the flash route is closed, the RAM route works

> **Status:** bench session on the physical unit, 2026-08-23, owner-authorised.
> Everything below was **executed on hardware** — the received
> `ESP32-S3-Touch-AMOLED-2.06`, identified by its USB serial, over USB/IP from a
> Linux host. Nothing is inferred from a datasheet where a measurement is
> claimed, and every claim that needed a board to obtain says which command
> produced it.
>
> **The unit was left provably unchanged.** After the last experiment,
> `esptool verify-flash 0x0` over all **33 554 432** bytes against the T-099
> backup returns `Verification successful`. Every write made during this session
> was undone and the whole part is byte-identical to the image recorded before
> it started.
>
> Source key **S13** in [HARDWARE_MATRIX](HARDWARE_MATRIX.md).

## 0. What this session was for, and what it actually produced

The owner authorised flashing the unit
([#100](https://github.com/hleserg/Attadipa/issues/100)) so that the bench
sequence in [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §5 could run: scan the
main I2C bus, read the AXP2101 rail state, identify the touch controller. That
sequence **did not run**, and the reasons it did not are worth more than the
sequence would have been.

The owner authorised flashing the unit
([#100](https://github.com/hleserg/Attadipa/issues/100)) so that the bench
sequence in [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §5 could run: scan the main
I2C bus, read the AXP2101 rail state, identify the touch controller. **That
sequence has now run, and it wrote nothing to flash at all.**

Two independent routes to running our own code were tried:

| Route | Outcome |
|---|---|
| Write a diagnostic into the free OTA slot `ota_1` and boot it | **Impossible on this board.** `ota_1` sits at exactly `0x1000000` and the second-stage bootloader cannot address flash there — §1 |
| Load a `PURE_RAM_APP` over USB and run it without touching flash | **Works** — but only if the serial port is never closed. Four earlier runs failed, and every one of them was killed by the flashing tool exiting rather than by the board — §2 |

So the probe ran from RAM, and §3 is what it read off the board: the I2C scan
that settles `0x6A` vs `0x6B`, the magnetometer's address space, the AXP2101's
rail registers, and one device that did not answer at all.

**An earlier revision of this document recorded both routes as closed and filed a
blocker asking permission to overwrite `ota_0`. That blocker is withdrawn** — see
§6. Nothing needs to be overwritten, and the preparation done for it was not
needed.

Separately, the vendor's own firmware, booting on its own, answered four
questions this repository has had open for weeks. §4.

## 1. `ota_1` is a partition the vendor's own bootloader cannot boot

### 1.1 What was done

A minimal ESP-IDF diagnostic (215 KB, ESP-IDF v5.5.5) was written into `ota_1`,
which was **verified erased first** — 4096 bytes of `0xFF` read back before any
write. Nothing else was touched:

```
esptool write-flash 0x1000000 attadipa_bench.bin      # Hash of data verified
esptool write-flash 0xf000    otadata_slot1.bin       # Hash of data verified
esptool verify-flash 0x0 c_0x0000000.bin              # Verification successful
```

That last line matters: the bootloader, the partition table and the first
megabyte of `factory` were confirmed **byte-identical** after the write, so the
write went only where it was aimed.

The `otadata` sector was hand-built from ESP-IDF source rather than from memory —
`esp_ota_select_entry_t` at `esp_flash_partitions.h:78`, the selection arithmetic
`boot_index = (ota_seq - 1) % app_count` at `bootloader_utility.c:364`, and the
CRC as ESP-IDF's own `otatool.py:96` computes it. `ota_seq = 2` therefore selects
slot 1.

### 1.2 What the bootloader did

```
I (30) boot:  6 ota_1            OTA app          00 11 01000000 00600000
I (31) esp_image: segment 0: paddr=01000020 vaddr=3fce2820 size=01700h (5888) load
E (31) esp_image: Segment 0 0x3fce2820-0x3fce3f20 invalid: overlaps bootloader stack
E (32) boot: OTA app partition slot 1 is not bootable
I (32) esp_image: segment 0: paddr=00a00020 ...
I (861) boot: Loaded app from partition at offset 0xa00000
```

It found the partition, tried it, rejected it, and fell back to `ota_0`.

### 1.3 Why, and the evidence is exact rather than plausible

The image the bootloader *read* at `paddr=0x01000020` was not the image that is
there. `vaddr=0x3fce2820, size=0x1700` is the segment-0 header of **the
bootloader itself**, at flash offset `0x0`:

| Read from | segment 0 vaddr | segment 0 length |
|---|---|---|
| flash `0x000000` — the bootloader | `0x3fce2820` | `0x1700` |
| what the bootloader reported for `0x1000020` | `0x3fce2820` | `0x1700` |
| the image actually written at `0x1000000` | `0x3c020020` | `0xb7b8` |

The first two agree to the bit. **The bootloader read address `0x0` when it asked
for `0x1000000`** — the address wrapped at the 16 MB boundary, which is the
signature of 24-bit SPI addressing on a 32 MB part.

Independently confirmed, and by a tool that says it in words. Asking `esptool` to
use the **ROM** loader for the same address:

```
$ esptool --no-stub read-flash 0x1000000 0x40 out.bin
A fatal error occurred: Can't access flash regions larger than 16MB
```

The stub flasher has 32-bit addressing and reads and writes `0x1000000`
perfectly — that is why the write verified. The ROM does not, and the vendor's
second-stage bootloader inherits the limit. The difference between them is why
this defect is invisible to every host-side tool and fatal at boot.

### 1.4 What follows, and it is a design constraint rather than a curiosity

- **The vendor's own OTA layout is single-slot in practice.** `ota_0` at
  `0xa00000` works; `ota_1` at `0x1000000` can never boot. The vendor ships a
  partition table with a slot the vendor's own bootloader cannot use. Taken with
  the blank `otadata` and the erased `ota_1`, nothing has ever exercised it.
- **For Attadipa: every app partition must live below 16 MB** — unless somebody
  proves the escape hatch below on this board. On a 32 MB part that leaves the
  upper half for data only, and even that depends on the *application's* flash
  driver having 32-bit addressing; the bootloader's limitation does not bind a
  running app, but nothing here has demonstrated the app side either.
  `storage` at `0x1600000` is above the line too.
- **ESP-IDF has an escape hatch, and it is experimental.**
  `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH` — *"Enable cache access to
  32-bit-address (over 16MB) range of SPI Flash (READ DOCS FIRST)"* — exists in
  `components/bootloader/Kconfig.projbuild`, is `default n`, requires
  `CONFIG_IDF_EXPERIMENTAL_FEATURES`, and its own help text says it *"can't use
  on all flash chips stable, for more information, please contact Espressif
  Business support"*. Its dependencies are all satisfied on this board:
  `BOOTLOADER_FLASH_32BIT_ADDR` defaults `y` at 32 MB, the part is quad rather
  than octal (§4.4: the vendor boots it QIO), and the ESP32-S3 declares
  `SOC_SPI_MEM_SUPPORT_CACHE_32BIT_ADDR_MAP`. So the option is *available*, it is
  off in the vendor's build, and **this repository has not tried it**. Named here
  so the next agent does not re-derive it, and marked `UNTESTED` so nobody quotes
  it as a solution.
- **A partition table is not self-validating.** `ota_1` is well-formed, correctly
  sized and correctly typed. It is also dead. Any partition layout this project
  writes needs the 16 MB line checked explicitly, because no tool checks it.

## 2. The RAM route works, and four earlier runs were measuring the host

[#100](https://github.com/hleserg/Attadipa/issues/100) called a RAM-only
diagnostic a qualified yes: `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP=y` and
`esptool load-ram`, writing nothing to flash. With the flash route closed by §1
it became the only remaining option.

An earlier revision of this document recorded it as **dead** on four failed runs.
That was wrong. The failure was real and repeatable, but its cause was on this
host, not on the board — and the way it hid is worth more than the result.

### 2.1 What the four failing runs looked like

| # | Image | Console | Result |
|---|---|---|---|
| 1 | full probe, 162 KB | ESP-IDF USB-Serial/JTAG driver | reset, factory app boots |
| 2 | full probe, `--after no-reset` | same | reset, factory app boots |
| 3 | probe rebuilt with `CONFIG_ESP_CONSOLE_NONE` and `esp_rom_printf` | none | reset, factory app boots |
| 4 | **minimal image** — no I2C, no drivers, `esp_rom_printf` in a loop | none | reset, factory app boots |

Every run reported success — *"Loaded 4 segments … executing at 0x40375a90"* —
and then:

```
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)
Saved PC:0x4038f6d2      # inside the loaded image's own IRAM segment
```

The saved PC lands inside the IRAM the image was loaded into, so the code was
executing when the reset arrived. Run 4 — an image with no peripheral driver of
any kind — was taken as proof that the board, not the probe, was at fault.

### 2.2 The cause: `esptool` exiting is itself the reset

`rst:0x15 (USB_UART_CHIP_RESET)` is, by definition, a **host-driven** reset: the
USB-Serial/JTAG peripheral resetting the digital core because the host asked it
to, through the CDC control lines. That should have been the first clue rather
than a footnote — nothing about a misbehaving image produces `0x15`.

Every one of the four runs ended the same way: `esptool` finished and exited. On
the last close of a `ttyACM` device the kernel drops DTR and RTS, and **this
board's USB-Serial/JTAG peripheral resets the digital core when the host changes
them**. **The tool that delivered the image killed it a few milliseconds later by
closing the port.**

> **This said *"on this board those are GPIO0 and EN"* until 2026-08-24, and it
> was never sourced.** There is no USB-UART bridge here: `USB_N`/`USB_P` run
> through 22 Ω resistors R19/R20 to the SoC's **native** USB pins
> ([HARDWARE_MATRIX](HARDWARE_MATRIX.md), `VERIFIED`), so DTR and RTS exist only
> as CDC control-line bits host-side and no wire runs from either to a pin. The
> mechanism is the one §2.2 argues for three paragraphs down and is the stronger
> claim, because the board reported it: `rst:0x15 (USB_UART_CHIP_RESET)` is by
> definition the peripheral resetting the core at the host's request, and a reset
> delivered by pulling EN would not name itself that. Nothing operational
> changes — closing the port still killed the image. Found in review of
> [#134](https://github.com/hleserg/Attadipa/pull/134), which had copied the
> sentence into a new file.

Two host-side explanations *were* tested before the wrong conclusion was drawn,
and neither one touched this:

- **`--after` defaulting to a reset.** Passing `--after no-reset` explicitly
  changed nothing — correctly, because `--after` governs what esptool does
  *before* closing, not the close itself.
- **The observer resetting the board.** pyserial asserts DTR and RTS on `open()`,
  so simply opening the port to watch is a hardware reset. A real bug, and not
  this one. **The fix is `LIKELY` rather than established, and this line used to
  say "fixed":** on Linux `cdc_acm` raises DTR and RTS in the kernel when the
  tty is first *activated*, before any userspace code runs, so setting them low
  before `open()` can only lower them again *after* the assertion. Nothing here
  observed the pin state either way — the experiment that settled §2.3 was about
  *close*, not open. Proving it on the unit is T-116's third goal, and until it
  runs the safe reading is that a pre-open set does **not** prevent the reset.
- **`stty -hupcl`** was tried and is not a fix: esptool reopens the port, and
  pyserial restores termios on open, so the setting is gone before it matters.

### 2.3 The experiment that settles it

Use `esptool` as a library in one process and **never close the port** — so
esptool's own close is not the last close:

```python
esp = esptool.detect_chip(port=port, baud=115200)
esptool.cmds.load_ram(esp, image)
# no close, no reopen: read from esp._port directly
```

Run against the *same minimal driverless image* that failed as run 4:

```
Loaded 4 segments from 'minram.bin' to RAM, executing at 0x4037570c.
# load_ram returned; port still open, watching
I (821) main_task: Started on CPU0
I (821) main_task: Calling app_main()
```

Fifteen seconds watched. **No `rst:0x`, no `ESP-ROM:` banner, no reset at all.**
The image runs. `PURE_RAM_APP` over USB-Serial/JTAG works on this board; §3 is
the full probe doing real work through the same path.

Tooling: `ramhold.py`. The three lines above are its core; the whole script is
reproduced in [#116](https://github.com/hleserg/Attadipa/issues/116) together with
the pedometer probe, because a session scratch directory under `/tmp` is not a
durable home for something this document calls prepared.

### 2.4 A second reason run 4 looked dead

Run 4's only output was `esp_rom_printf`, and it was built with
`CONFIG_ESP_CONSOLE_NONE=y` and `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1` —
**the ROM's putc channel is disabled**, so `esp_rom_printf` writes nowhere. The
same image, held open, prints ESP-IDF's own log lines (which reach the secondary
USB-Serial/JTAG console) and still not a single `MINRAM ALIVE`.

So even with the reset fixed, that image would have looked mute. Two independent
causes of silence stacked on one run, which is how a wrong conclusion survived
four attempts.

**For anything loaded into RAM on this board: use `ESP_LOGx` or `printf`, not
`esp_rom_printf`,** unless `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM` is set to the
USB-Serial/JTAG port on purpose.

## 3. What the probe read off the board

The bench probe from [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §5 was built as a
`PURE_RAM_APP`, loaded with the method in §2.3 and watched for thirty seconds.
**Nothing was written to flash.** It is read-only by construction: every register
access below is an I2C write-then-read whose write phase carries a register
*address* and never a value, so no device was configured, no PMU rail was
touched and the display was not initialised.

### 3.1 The I2C scan

```
===== I2C SCAN  (SDA=15 SCL=14, 100 kHz) =====
  0x18  ACK   ES8311 audio codec (expected)
  0x34  ACK   AXP2101 PMU (expected)
  0x40  ACK   ES7210 mic ADC (expected)
  0x51  ACK   RTC (expected)
  0x6B  ACK   QMI8658 IMU
  -- 5 devices
```

Three results, in descending order of how much they were wanted:

- **D-conflict `0x6A` vs `0x6B` → `0x6B`, measured.** The schematic and the
  QMI8658 revisions 0.8/0.9/A are right; the Rev 0.6 document Waveshare's own
  wiki links — which maps SA0-low to `0x6A` — does not describe this board.
  `0x6A` did not acknowledge. `SA0` is high here.
- **The magnetometer's address space is clear.** `0x0C`, `0x0D` and `0x1E` are
  all unclaimed, so an LIS2MDL, MMC5603 or QMC5883 retrofit (T-109) has
  somewhere to sit without colliding with anything on the main bus.
- **The touch controller did not answer** — §3.3, and it is a finding rather
  than a failed read.

### 3.2 The IMU says which datasheet describes it

```
  0x6A no response
  0x6B WHO_AM_I = 0x05 (QMI8658 signature)
  0x6B REVISION = 0x7C
```

`WHO_AM_I = 0x05` is the QMI8658 signature in both candidate documents and
distinguishes nothing. **`REVISION_ID` does:**

| Document | `REVISION_ID` §5.3 |
|---|---|
| `13-52-25 ∙ QMI8658A Datasheet ∙ Rev A` (© 2022 QST) — chapter 11 documents a complete hardware pedometer | **`0x7C`** |
| `QMI8658C` Rev 0.6, ADVANCE INFORMATION — `CTRL8` is *"Reserved: Not Used"*, no step counter | `0x79` |

The silicon reports **`0x7C`**. This is [H14](OPEN_QUESTIONS.md)'s consequential
half, and it lands on the document that has a pedometer in it — even though the
schematic prints the name `QMI8658C` twice. **The part name did not tell us what
is inside**, which is [ADR-0003](../adr/0003-radio-not-lora.md)'s lesson arriving
in a second subsystem exactly as that ADR predicted.

Recorded precisely, because the distinction matters:

- **Established:** the revision byte on this board matches the Rev A document and
  not the Rev 0.6 one, so **Rev A is the register map to program against.**
- **Not established:** that the pedometer *works*. A matching revision byte is
  evidence about which document applies, not a functional test. Chapter 11's
  engine has to be enabled and counted against real walking, which needs a person
  and the board in their hand — see §6.
- **The one wrinkle, named before someone else finds it:** both documents'
  register-map *summary* tables give `REVISION_ID`'s default as `0x68`,
  disagreeing with their own §5.3 detail sections (`0x7C` / `0x79`). So the
  measured `0x7C` matches exactly one of the four places the byte is stated —
  Rev A's detail section — and contradicts both summary tables. The corroboration
  that does not depend on the byte at all is the stronger evidence: `CTRL8` is
  writable and reads back `0x90` on a part whose other candidate document calls
  that register *"Reserved: Not Used"*, and gravity measures 1.03 g under Rev A's
  ±8 g scaling.
- One correction to [OPEN_QUESTIONS](OPEN_QUESTIONS.md) H14 in passing: the
  document number of the Rev A datasheet is **13-52-25**, not 13-52-27.

### 3.3 The touch controller is held in reset until GPIO 9 is pulsed

The first probe found `0x38` absent from the scan entirely, and a read of chip-ID
register `0xA3` failed — so not a bad register choice, but nothing acknowledging
at that address at all. The vendor's own firmware clearly drives a touch panel,
so the part is fitted.

[HARDWARE_MATRIX](HARDWARE_MATRIX.md) records the FT3168's reset as **GPIO 9**,
and a `PURE_RAM_APP` leaves that pin a floating input. A second probe tested that
directly — scan, drive GPIO 9 high, scan again, then pulse it low and back and
scan a third time:

| State of GPIO 9 | Devices | `0x38` |
|---|---|---|
| untouched, as a RAM app finds the board | 5 | absent |
| driven **high** and held 200 ms | 5 | still absent |
| pulsed **low 10 ms → high**, 300 ms settle | **6** | **acknowledges** |

```
  0x38 reg 0xA3 (chip ID         ) = 0x64
  0x38 reg 0xA6 (firmware version) = 0x02
  0x38 reg 0xA8 (vendor ID       ) = 0x11
```

**The controller needs a reset edge, not a level.** Holding the line high changes
nothing; the falling edge is what brings it up. That distinction is the whole
finding — a bring-up that merely configures GPIO 9 as a high output at init will
see an empty bus and no error.

What the identity bytes are and are not:

- **Measured:** `0xA3 = 0x64`, `0xA6 = 0x02`, `0xA8 = 0x11`, and the address
  `0x38` — which until now was *"driver source only, no datasheet states it"* and
  is hereby confirmed on the board.
- **Consistent with, not proof of:** `0x11` is FocalTech's vendor byte and `0x64`
  is the chip ID the FT5x06/FT6x36-family drivers expect, which is why the
  vendor's FT5x06-family driver works against an FT3168. No FT3168 datasheet has
  been obtained, so the mapping from `0x64` to a specific part number is
  **`UNKNOWN`** and is not claimed here.

For Attadipa: **touch is not reachable just because the I2C bus is up.** The
board wants a reset pulse on GPIO 9 first, and that belongs in the BSP rather
than in an application.

### 3.4 The AXP2101, read only

`IC_TYPE = 0x4A` confirms the part. The rail-enable and voltage registers are
printed raw on purpose — decoding them into a rail map is D13, and D13 needs the
datasheet open beside the numbers rather than a plausible-sounding guess here.

```
  0x00 STATUS1        = 0x28      0x30 ADC_CH_EN      = 0x1D
  0x01 STATUS2        = 0x14      0x80 DCDC_ON_OFF    = 0x0F
  0x03 IC_TYPE        = 0x4A      0x90 LDO_ON_OFF0    = 0xFF
  0x10 COMMON_CONFIG  = 0x34      0x91 LDO_ON_OFF1    = 0x01
  0x12 BATFET_CTRL    = 0x08      0x92 ALDO1_VOLT     = 0x1C
  0x18 TS_PIN_CTRL    = 0x0A      0x93 ALDO2_VOLT     = 0x1C
  0x20 PWRON_STATUS   = 0x01      0x94 ALDO3_VOLT     = 0x19
  0x21 PWROFF_STATUS  = 0x00      0x95 ALDO4_VOLT     = 0x0D
  0x27 PWROK_DELAY    = 0x14      0x96 BLDO1_VOLT     = 0x07
  0x62 ICC_CFG        = 0x11      0x97 BLDO2_VOLT     = 0x17
  0x63 CHG_ITERM_CFG  = 0x15      0x82 DCDC1_VOLT     = 0x12
  0x64 CHG_V_CFG      = 0x03      0x83 DCDC2_VOLT     = 0x28
                                  0x84 DCDC3_VOLT     = 0x46
```

This is the **live rail state under the vendor's firmware**, captured from a RAM
app that changed nothing — which is exactly the input D13 was waiting for, and
it did not cost a flash write to get.

## 4. What the vendor's own firmware answered for free

The bootloader and factory application log at every boot, and capturing that log
from the first byte settled four things. All of it is the **vendor's** firmware
describing the **vendor's** board, which is a better witness than any inference
this repository could make.

### 4.1 D12a: octal PSRAM, from the silicon, at last

```
I (862) octal_psram: vendor id    : 0x0d (AP)
I (863) octal_psram: dev id       : 0x02 (generation 3)
I (863) octal_psram: density      : 0x03 (64 Mbit)
I (863) octal_psram: good-die     : 0x01 (Pass)
I (863) octal_psram: Latency      : 0x01 (Fixed)
I (864) octal_psram: VCC          : 0x01 (3V)
I (864) octal_psram: SRF          : 0x01 (Fast Refresh)
I (864) octal_psram: BurstType    : 0x01 (Hybrid Wrap)
I (864) octal_psram: BurstLen     : 0x01 (32 Byte)
I (865) octal_psram: Readlatency  : 0x02 (10 cycles@Fixed)
I (866) MSPI Timing: PSRAM timing tuning index: 5
I (866) esp_psram: Found 8MB PSRAM device
I (866) esp_psram: Speed: 80MHz
```

This is step 4 of [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §5, executed. D12a was
already `RESOLVED` from Table 1-1 and the eFuse read; it is now **confirmed by
the running silicon**, which is the one thing neither of those could do. The
driver that printed this is `octal_psram` — a quad part would not have loaded it.

The detail beyond the yes/no is usable: **10-cycle fixed read latency, hybrid-wrap
burst of 32 bytes, 80 MHz**, and the vendor's tuning index. Every PSRAM bandwidth
figure in [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §3.3 is arithmetic against
assumed latency; these are the real numbers to redo it against.

### 4.2 The panel controller reports itself as SH8601

```
I (2004) sh8601: LCD panel create success, version: 1.0.2
```

[HARDWARE_MATRIX](HARDWARE_MATRIX.md) records the part as **CO5300** with a note
that the vendor drives it through the SH8601-family driver, *"recorded so nobody
later fixes the apparent mismatch"*. That note is correct and this line does not
overturn it — a driver logging its own name is evidence about the **driver**, not
about the silicon behind the QSPI bus.

What it does establish is that **the SH8601 driver initialises this panel
successfully on this unit**, which is the practically important half: whatever
the die is called, `esp_lcd_sh8601` is a working starting point and the
mismatch is not going to bite at bring-up.

### 4.3 D14: the SD card is SDMMC, not SPI

```
W (2472) ESP32-S3-Touch-AMOLED-2.06: Warning: Long filenames on SD card are disabled in menuconfig!
E (2500) sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107
E (2500) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
E (2500) BS:VideoPlayer: Failed to mount SD card
```

D14 asked whether the board is wired for SDMMC 1-bit or SPI, because the BSP
says SDMMC on GPIO 1/2/3 while the schematic labels those nets `MOSI`/`SCK`/`MISO`.
The vendor's shipping firmware calls into **`sdmmc_common` and `vfs_fat_sdmmc`** —
the SDMMC host driver, not `sdspi`. The error is `send_op_cond` timing out, which
is what an **empty slot** looks like; no card was inserted.

So: **the vendor drives it as SDMMC**, and the schematic's SPI-style net names are
labels rather than a mode. `RESOLVED` for what the vendor does. Whether SPI would
*also* work is untested and nobody needs it to be.

### 4.4 Smaller things the same log settled

| Line | What it settles |
|---|---|
| `boot: chip revision: v0.2` | agrees with the eFuse read — S10 |
| `boot: efuse block revision: v1.4` | new; not previously recorded |
| `qio_mode: Enabling default flash chip QIO`, `SPI Mode: QIO`, `Boot SPI Speed: 80MHz`, `SPI Flash Size: 32MB` | the vendor boots the flash **QIO at 80 MHz**, and the bootloader agrees the part is 32 MB |
| `spi_flash: detected chip: gd` | GigaDevice, agreeing with the package marking read off the board — S9 |
| `QMI8658: QMI8658 initialized successfully` | the IMU answers on the main bus in the vendor's own build. It names no address — §3.1 settles `0x6B` by measurement instead |
| `Using stylesheet (Default Dark)` | the vendor's own demo ships a dark stylesheet by default on this emissive panel |
| factory image segment map: 3 573 492 B mapped DROM + 1 451 688 B mapped IROM | where the 4.94 MB goes |

## 5. The unit was left as it was found

Three writes were made to flash during this session: an app image into the erased
`ota_1`, an `otadata` selector, and nothing else. All three are undone:

```
esptool erase-region 0xf000  0x2000     # otadata back to blank -> factory boots
esptool erase-region 0x1000000 0x35000  # the probe image removed from ota_1
esptool verify-flash 0x0 stock_full.bin # Verification successful, 33 554 432 bytes
```

The reference image was reassembled from the T-099 chunk dump **by numeric
offset** and re-hashed to
`2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5`, the value
T-099 recorded, before being used. The device's own MD5 agrees with it.

A boot after the restore confirms the original application is what runs:

```
I (819) boot: Loaded app from partition at offset 0x100000
I (1371) app_init: Project name:     phone_s3_box_3
I (1371) app_init: App version:      v0.4.2-92-g5c6be6c-dirty
```

**The backup did its job the first time it was needed.** T-099 was worth doing.

The RAM work in §2 and §3 came *after* that verification and writes nothing to
flash by construction — `PURE_RAM_APP` images have no DROM or IROM segment, which
`esptool image-info` confirms for each one loaded. So the verification still
stands. The unit was reset over the CDC control lines at the end of the session
and boots its own firmware normally:

```
I (31) boot: Defaulting to factory image
I (819) boot: Loaded app from partition at offset 0x100000
I (2383) ESP32-S3-Touch-AMOLED-2.06: Backlight on
```

**One register did need putting back by hand, and finding that out was itself a
result.** The pedometer probe (§3.2) left `CTRL2 = 0x26`, `CTRL7 = 0x01` and
`CTRL8 = 0x90` on the IMU. Booting the vendor firmware restored `CTRL2` to `0x24`
and `CTRL7` to `0x03` — **but not `CTRL8`, which stayed `0x90`**. The vendor's
driver never writes that register, so `Pedo_EN` survives a reset that the IMU
itself does not see. It was cleared deliberately, and read back as `0x00`:

```
CTRL8 before = 0x90
write 0x00   -> ACK
CTRL8 after  = 0x00  -- restored to the power-on default
```

Two things follow. **The vendor's firmware does not use the pedometer engine at
all** — `CTRL8` was `0x00` before this session touched it. And **loading a RAM
image does not reset the peripherals**: the SoC restarts, the parts on the I2C
bus keep whatever the last program left in them. Anything a bench probe
configures has to be put back deliberately.

## 6. The blocker is withdrawn, and what is left needs a person rather than a permission

An earlier revision of this document filed a `BLOCKED` asking the owner to
authorise overwriting `ota_0`, on the grounds that both non-destructive routes
were closed. **§2 shows that premise was wrong.** The blocker is withdrawn, the
question in [#100](https://github.com/hleserg/Attadipa/issues/100) does not need
answering, and the prepared 6 MB `ota_0` restore slice is not needed. It cost
nothing to prepare and it stays verified, which is a fine place for it to sit.

Two things are still open, and neither is a permission:

- **The pedometer needs walking.** §3.2 establishes which datasheet applies; it
  does not establish that chapter 11's engine counts steps. That test enables
  `CTRL8.Pedo_EN` with `CTRL7.aEN` and reads `0x5A`–`0x5C` while the board moves,
  and a board on a desk cannot supply the movement. The probe for it is written
  and builds; running it needs one person to pick the watch up and walk. Tracked
  as **T-112**. Note it writes three IMU control registers — the QMI8658 has no
  non-volatile configuration, so a power cycle undoes it, and the probe restores
  the defaults itself on the way out.
- **The touch controller's silence turned out to be answerable in the same
  session** (§3.3): it is held in reset until GPIO 9 is pulsed. What is still
  open is smaller — no FT3168 datasheet has been obtained, so chip ID `0x64` is
  recorded as a measured byte rather than as a part number. Tracked as **T-113**.

Neither blocks anything else. Both are bench work of the same kind as this
session, and this session has now shown the bench work can be done without
writing a byte to the owner's flash.

## 7. Method notes, because two of these cost real time

- **Identify a board by USB serial, never by port.** Two boards are attached to
  this host and **both enumerate as `303a:1001`** — the watch and a V4 MeshCore
  node, which is also an ESP32-S3. `/dev/ttyACM0` is not an identity. Every write
  in this session went through a guard that resolves the tty from the unit's USB
  serial and exits non-zero rather than guessing — **that guard was this
  session's own script and did not survive it. No shared resolver exists: this
  bullet is an obligation on whoever next *opens* a port, not a rail already in
  place, and building one is [T-116](../../TASKS.md).** *Opens*, not *writes*:
  the very next bullet is that opening resets this board, and the two RAM images
  lost here were destroyed by a tool that wrote nothing. A screenshot, a log
  tail and `idf.py monitor` are all inside this rule — which is why T-116's lint
  list names `monitor` beside `flash`. The rules for the unit, between sessions
  **and during one**, are [BENCH_HANDLING](../hardware/BENCH_HANDLING.md). The serial string is
  deliberately not reproduced here: on an ESP32-S3 the USB serial *is* the base
  MAC, and that is the owner's, not the repository's — see
  [`WAVESHARE_EFUSE_READ.md`](WAVESHARE_EFUSE_READ.md) §0.
- **Opening the serial port resets this board.** pyserial asserts DTR and RTS on
  `open()`, and this board's USB-Serial/JTAG peripheral resets the digital core
  when it sees that — they are CDC control-line bits, **not** GPIO0 and EN,
  which is what this line said until 2026-08-24; see §2.2. Two RAM images
  were destroyed by the tool sent to observe them before this was noticed. Set
  both `False` on the `Serial` object *before* `open()` — **and do not treat
  that as a fix.** `cdc_acm` raises both lines in the kernel when the tty is
  activated, before any userspace code runs, so the pre-set lowers them after
  the assertion rather than preventing it; §2.2 carries the reasoning and
  T-116's third goal is to observe it on the unit. **Until then, plan for the
  board to reset on open** — which is why the rule above is *whoever next opens
  a port*, and not *whoever opens one without setting the lines*.
- **Closing it resets the board too, and that is the one that cost a wrong
  conclusion.** The kernel drops the modem lines on the *last* close of a
  `ttyACM`, so `esptool` exiting is itself a reset. Anything loaded into RAM must
  be observed from a process that never lets the port close — §2.3. `stty
  -hupcl` does not help, because pyserial restores termios when esptool reopens
  the port.
- **`rst:0x15 (USB_UART_CHIP_RESET)` means the host did it.** Read the reset
  cause before theorising about the image: `0x15` is the USB-Serial/JTAG
  peripheral acting on the host's control lines, and no misbehaving application
  produces it. Four runs were misread because that byte was treated as a symptom
  rather than as the answer.
- **`esp_rom_printf` writes nowhere when the ROM console is off.** With
  `CONFIG_ESP_CONSOLE_NONE=y` and `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1`, an
  image whose only output is `esp_rom_printf` is silent even while it runs
  perfectly. Use `ESP_LOGx` or `printf` in RAM images here.
- **The boot log's first 580 ms are lost by default.** Resetting with esptool and
  then opening the port reconnects far too late — the bootloader has already
  chosen a partition. Resetting over the CDC control lines and reopening in a
  tight loop gets the first byte at **62 ms**, which is what made §1.2 readable
  at all. Racing esptool for the port instead catches the ROM banner but leaves
  the chip in download mode.
- **`idf.py flash` must never be used on this unit.** It writes a bootloader to
  `0x0`, a partition table to `0x8000` and `otadata` to `0xd000` — and this
  board's `otadata` is at **`0xf000`**. All three would overwrite vendor
  structures, two of them silently.
