# Running our own code on the received unit: two routes, both closed

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

Two independent routes to running our own code were tried. **Both are closed**,
one by the board and one by a rule this session was right to respect:

| Route | Outcome |
|---|---|
| Write a diagnostic into the free OTA slot `ota_1` and boot it | **Impossible on this board.** `ota_1` sits at exactly `0x1000000` and the second-stage bootloader cannot address flash there — §1 |
| Load a `PURE_RAM_APP` over USB and run it without touching flash | **Does not work on this board.** The chip resets itself within milliseconds, 4 attempts out of 4, including a minimal image containing no drivers at all — §2 |

What remains is overwriting a partition that already holds vendor firmware —
`factory` or `ota_0` — which is a larger act than the two above and is the
owner's to authorise specifically. §5 states it as a blocker.

Meanwhile the vendor's own firmware, booting on its own, answered four questions
this repository has had open for weeks. §3.

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
- **For Attadipa: every app partition must live below 16 MB.** On a 32 MB part
  that leaves the upper half for data only, and even that depends on the
  *application's* flash driver having 32-bit addressing — the bootloader's
  limitation does not bind a running app, but nothing here has yet demonstrated
  the app side either. `storage` at `0x1600000` is above the line too.
- **A partition table is not self-validating.** `ota_1` is well-formed, correctly
  sized and correctly typed. It is also dead. Any partition layout this project
  writes needs the 16 MB line checked explicitly, because no tool checks it.

## 2. The RAM route does not work on this board, and that is now measured

[#100](https://github.com/hleserg/Attadipa/issues/100) called a RAM-only
diagnostic a qualified yes: `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP=y` and
`esptool load-ram`, writing nothing to flash. With the flash route closed by §1
it became the only remaining option, so it was tried properly.

**It does not run.** Four attempts, four identical outcomes:

| # | Image | Console | Result |
|---|---|---|---|
| 1 | full probe, 162 KB | ESP-IDF USB-Serial/JTAG driver | reset, factory app boots |
| 2 | full probe, `--after no-reset` | same | reset, factory app boots |
| 3 | probe rebuilt with `CONFIG_ESP_CONSOLE_NONE` and `esp_rom_printf` | none | reset, factory app boots |
| 4 | **minimal image** — no I2C, no drivers, `esp_rom_printf` in a loop | none | reset, factory app boots |

`esptool` reports success every time — *"Loaded 4 segments … executing at
0x40375a90"* — and every segment is internal RAM, confirmed with
`esptool image-info`: DRAM, IRAM and RTC\_DATA, no DROM or IROM, so nothing in
the image depends on flash.

The chip then resets. The reason is the same each time:

```
rst:0x15 (USB_UART_CHIP_RESET),boot:0x2b (SPI_FAST_FLASH_BOOT)
Saved PC:0x4038f6d2      # runs 1-3, inside the loaded image's IRAM segment
Saved PC:0x40383898      # run 4, likewise
```

The saved PC lands inside the IRAM the image was loaded into, so **the code
starts and is executing when the reset arrives**. Attempt 4 is what makes this a
statement about the board rather than about the probe: an image containing no
peripheral driver of any kind fails identically.

**`USB_UART_CHIP_RESET` is the USB-Serial/JTAG peripheral resetting the digital
core** — the same mechanism host tools use to reset these boards. On this unit
that path is the only console there is, so the transport used to deliver a RAM
image is also what destroys it.

Two host-side explanations were tested and **eliminated** before this conclusion
was reached, and both are worth recording because each cost an attempt:

- **`--after` defaulting to a reset.** Passing `--after no-reset` explicitly
  changed nothing.
- **The reader resetting the board itself.** pyserial asserts DTR and RTS on
  `open()`, and on this board those CDC lines are GPIO0 and EN — so simply
  opening the port is a hardware reset. Setting both low *before* `open()` fixed
  that, and the reset still happened. `stty -hupcl` was tried as well.

**`UNKNOWN`:** whether a RAM app can be made to survive here at all — over JTAG
rather than the USB downloader, or with the USB peripheral left strictly alone.
Nothing above tests that, and it is not claimed.

**What is no longer `UNKNOWN`:** the RAM route as #100 described it — `load-ram`
over this board's USB-Serial/JTAG — does not work. That recommendation is
withdrawn on evidence rather than on judgement.

## 3. What the vendor's own firmware answered for free

The bootloader and factory application log at every boot, and capturing that log
from the first byte settled four things. All of it is the **vendor's** firmware
describing the **vendor's** board, which is a better witness than any inference
this repository could make.

### 3.1 D12a: octal PSRAM, from the silicon, at last

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

### 3.2 The panel controller reports itself as SH8601

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

### 3.3 D14: the SD card is SDMMC, not SPI

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

### 3.4 Smaller things the same log settled

| Line | What it settles |
|---|---|
| `boot: chip revision: v0.2` | agrees with the eFuse read — S10 |
| `boot: efuse block revision: v1.4` | new; not previously recorded |
| `qio_mode: Enabling default flash chip QIO`, `SPI Mode: QIO`, `Boot SPI Speed: 80MHz`, `SPI Flash Size: 32MB` | the vendor boots the flash **QIO at 80 MHz**, and the bootloader agrees the part is 32 MB |
| `spi_flash: detected chip: gd` | GigaDevice, agreeing with the package marking read off the board — S9 |
| `QMI8658: QMI8658 initialized successfully` | the IMU answers on the main bus in the vendor's own build. It does **not** say which address, so D-conflict `0x6A`/`0x6B` stays open |
| `Using stylesheet (Default Dark)` | the vendor's own demo ships a dark stylesheet by default on this emissive panel |
| factory image segment map: 3 573 492 B mapped DROM + 1 451 688 B mapped IROM | where the 4.94 MB goes |

## 4. The unit was left as it was found

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

## 5. BLOCKED

```
BLOCKED:
  Running any of our own code on the received unit.

Reason:
  Both non-destructive routes are closed. ota_1 -- the only free app partition --
  sits at 0x1000000 and the second-stage bootloader cannot address flash at or
  above 16 MB (§1). A PURE_RAM_APP loaded with `esptool load-ram` starts and the
  chip resets itself within milliseconds, 4 attempts out of 4, including an image
  containing no drivers (§2). What is left is overwriting a partition that
  already holds vendor firmware.

Evidence:
  §1.3 -- the bootloader read flash 0x0 when asked for 0x1000000, byte-identical
  segment header; and `esptool --no-stub` refuses the address in words.
  §2 -- four runs, `rst:0x15 (USB_UART_CHIP_RESET)`, saved PC inside the loaded
  image's own IRAM each time.
  §4 -- the flash is byte-identical to the T-099 backup, so none of this cost
  anything.

Impact:
  The bench sequence in WAVESHARE_ARRIVAL §5 cannot run. Specifically still
  unanswered: the I2C bus scan that would settle 0x6A vs 0x6B for the IMU and
  confirm 0x0C/0x0D are free for the magnetometer (T-106, T-109), the AXP2101
  rail states behind D13, and the touch controller's identity.

Possible options:
  a) Overwrite `ota_0`, which holds xiaozhi 1.8.5. The safest of the destructive
     options: it is an OTA slot, the factory image is untouched, and a 6 MB
     restore from the verified backup puts it back. The restore slice has already
     been extracted and verified against the device, so this is prepared.
  b) Overwrite `factory`, which holds the application the watch actually boots.
     No advantage over (a) and strictly more disruptive.
  c) Reach the chip over JTAG instead. The USB-Serial/JTAG port exposes real
     JTAG; OpenOCD could run code or read peripherals without any flash write.
     Untried, and it is the only remaining non-destructive idea.
  d) Do none of it. The magnetometer measurement (T-109) needs no firmware, and
     the modules have not arrived.

Recommended next action:
  Option (a), with the owner's explicit word, because the preparation is already
  done and verified. Option (c) first if anyone wants to spend the evening on
  OpenOCD -- it is the only route that keeps the "nothing is written" property,
  and §2 means that property is otherwise unavailable on this board.
```

## 6. Method notes, because two of these cost real time

- **Identify a board by USB serial, never by port.** Two boards are attached to
  this host and **both enumerate as `303a:1001`** — the watch and a V4 MeshCore
  node, which is also an ESP32-S3. `/dev/ttyACM0` is not an identity. Every write
  in this session went through a guard that resolves the tty from the unit's USB
  serial and exits non-zero rather than guessing. The serial string is
  deliberately not reproduced here: on an ESP32-S3 the USB serial *is* the base
  MAC, and that is the owner's, not the repository's — see
  [`WAVESHARE_EFUSE_READ.md`](WAVESHARE_EFUSE_READ.md) §0.
- **Opening the serial port resets this board.** pyserial asserts DTR and RTS on
  `open()`; on a USB-Serial/JTAG board those are GPIO0 and EN. Two RAM images
  were destroyed by the tool sent to observe them before this was noticed. Set
  both `False` on the `Serial` object *before* `open()`.
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
