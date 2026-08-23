# The SD card bench test — Waveshare ESP32-S3-Touch-AMOLED-2.06

**Status: `NOT EXECUTED — HARDWARE REQUIRED`.** Nothing below has been run. No
line of it may be recorded as `PASS` until it has run on a physical board, and
the word for a step that has not run is `NOT EXECUTED — HARDWARE REQUIRED`.

This procedure exists to close **D14**
([OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md)), which asks which bus mode the
micro-SD connector on this board is actually wired for. It is written for the
owner at a bench, not for an agent.

---

## 0. Why the question is still open

The vendor's shipping firmware drives the slot through ESP-IDF's **SDMMC host**
driver — established from the boot log and traced through ESP-IDF's source in
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §4.3.
That is a fact about the **software**. The slot in that log was **empty**, and a
`send_op_cond` timeout into an empty slot is identical for every possible wiring,
because nothing is on the other end to answer.

So the repository holds two readings of the connector and no measurement:

| Source | Says |
|---|---|
| Vendor BSP v2.0.0 (S7) | SDMMC 1-bit — `CLK` 2, `CMD` 1, `D0` 3 |
| Schematic (S6) | nets named `MOSI`, `SCK`, `MISO`, plus a chip-select near **GPIO 17** |

They are not necessarily in conflict. On the ESP32-S3 *"any GPIO may be used for
each of the SD card signals"* through the GPIO matrix, and Espressif's own
`SDSPI_DEVICE_CONFIG_DEFAULT()` *"will also fill in the default pin mappings,
which are the same as the pin mappings of the SDMMC host driver"* — so neither
the pin numbers nor the net names decide the mode, and `MOSI` and `CMD` may name
the same copper. What has never happened is a card enumerating. That is what this
procedure does.

**The chip-select near GPIO 17 is the part to keep an eye on.** The BSP's pin map
does not have it, and neither reading explains it. If SDMMC 1-bit works with
GPIO 17 untouched, it is a routed-but-unused `DAT3`/`CS`; if SDMMC only works
after GPIO 17 is driven, it is load-bearing and the BSP is incomplete. Both
outcomes are results — record which one happened.

---

## 1. Safety rules, and they are the point of this file

The failure mode here is not a wrong answer, it is somebody's card being
reformatted by a diagnostic. So:

1. **Use a card whose entire contents are expendable.** Assume anything on it
   may be destroyed. Do not use a card that has ever held anything you want.
   Micro-SD has no physical write-protect tab; the slider on a full-size adapter
   is advisory and the host may ignore it. There is no hardware interlock — the
   only interlock is which card you picked up.
2. **`format_if_mount_failed` stays `false`.** ESP-IDF's own example says it
   plainly: *"If format_if_mount_failed is set to true, SD card will be
   partitioned and formatted in case when mounting fails."* `true` in a probe is
   how a diagnostic silently erases the thing it was diagnosing.
   `esp_vfs_fat_sdcard_format()` and `esp_vfs_fat_sdcard_format_cfg()` are not
   called anywhere in this procedure.
3. **Steps 1–5 write nothing to the card.** Enumeration reads the CID and CSD
   registers; mounting FAT read-only reads the boot sector and the FAT. Step 6 is
   the first byte written, it writes one disposable file, and **it does not run
   without a separate decision** — see its own preconditions.
4. **The probe image is RAM-only, so nothing is written to the board's flash
   either.** Build it as a `PURE_RAM_APP` image and load it the way the S13
   session did — `esptool load_ram`, observed from a process that never closes
   the port. `idf.py flash` **must never be used on this unit**: it writes a
   bootloader to `0x0`, a partition table to `0x8000` and `otadata` to `0xd000`,
   and this board's `otadata` is at `0xf000`
   ([WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §7).
5. **Identify the board by USB serial, not by port.** Two ESP32-S3 devices on
   this host both enumerate as `303a:1001`; `/dev/ttyACM0` is not an identity.
6. **No re-wiring, no cuts, no bodges.** Re-wiring the slot to force the other
   mode was excluded from bench work deliberately
   ([WAVESHARE_ARRIVAL](../research/WAVESHARE_ARRIVAL.md) §5) and stays excluded.

---

## 2. What to have on the bench

| | |
|---|---|
| Board | one `ESP32-S3-Touch-AMOLED-2.06`, the unit from S13 |
| Card | one micro-SD, **expendable**, FAT32-formatted, ≤ 32 GB (SDHC — avoids the exFAT/SDXC branch entirely), with two or three small files on it |
| Host | ESP-IDF v5.4 or later, `esptool` |
| Second card | *optional* — a second expendable card of a different make. If the first fails, one retry on different silicon separates "this board" from "this card" |

Record the card's make, capacity and class. A negative result on an unnamed card
is not a result.

---

## 3. Step 1 — SDMMC 1-bit, probing frequency, enumeration only

The BSP's own configuration, at the slowest legal clock. `SDMMC_FREQ_PROBING` is
400 kHz, which is what the SD specification's identification phase uses and the
setting most tolerant of marginal signal integrity.

```c
sdmmc_host_t host = SDMMC_HOST_DEFAULT();  /* .slot defaults to SDMMC_HOST_SLOT_1 */
host.max_freq_khz  = SDMMC_FREQ_PROBING;   /* 400 kHz — identification speed */

sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
slot.width = 1;                            /* 1-bit, as the BSP declares */
slot.clk   = GPIO_NUM_2;
slot.cmd   = GPIO_NUM_1;
slot.d0    = GPIO_NUM_3;
/* Blank every line this test does not use. See the note below — the defaults
   are not neutral on this board. */
slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
slot.d4 = slot.d5 = slot.d6 = slot.d7 = GPIO_NUM_NC;
slot.cd = SDMMC_SLOT_NO_CD;                /* no card-detect pin is known */
slot.wp = SDMMC_SLOT_NO_WP;
slot.flags = 0;                            /* no SDMMC_SLOT_FLAG_INTERNAL_PULLUP */

sdmmc_card_t card;
ESP_ERROR_CHECK(sdmmc_host_init());
ESP_ERROR_CHECK(sdmmc_host_init_slot(host.slot, &slot));
esp_err_t err = sdmmc_card_init(&host, &card);   /* enumerate only — no filesystem */
if (err == ESP_OK) sdmmc_card_print_info(stdout, &card);
```

`sdmmc_card_init()` plus `sdmmc_card_print_info()` is the whole of step 1. It
reads CID and CSD and writes nothing.

**Why `d1`–`d7` are blanked, and it is not tidiness.** On the ESP32-S3
`SDMMC_SLOT_CONFIG_DEFAULT()` does not leave the unused data lines empty — it
presets `d1 = 40`, `d2 = 41`, `d3 = 42`, `d4 = 45`, `d5 = 46`, `d6 = 47`,
`d7 = 48` (`sdmmc_default_configs.h`, the `CONFIG_IDF_TARGET_ESP32S3` branch).
On *this* board GPIO 40, 41, 42 and 45 are the I2S bus and GPIO 46 is the
amplifier enable ([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md)). Reading
ESP-IDF v5.4, `sdmmc_host_init_slot()` configures only `clk`, `cmd` and `d0` at
`width = 1` (`esp_driver_sdmmc/src/sdmmc_host.c:734-736`) — the `d1`/`d2` calls
and the "force D3 high" block are both behind `if (slot_width >= 4)` at `:738`,
and `sdmmc_host_pullup_en_internal()` likewise stops at `cmd` and `d0`
(`:1136-1137`). So the defaults are inert *today*. They are inert because of an
internal branch in one version of one driver, on pins that drive a speaker. Name
them `GPIO_NUM_NC` and the question does not arise.

**Leave GPIO 17 alone in this step.** Do not configure it, drive it or pull it.
Whether the slot works without it is half the question.

| Outcome | What it means | Then |
|---|---|---|
| `Name`, `Type`, `Speed`, `Size` printed | The socket's `CLK`/`CMD`/`DAT0` reach GPIO 2/1/3 and SDMMC 1-bit communication works. **This is what closes D14** | step 2 |
| `sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107` | The same line as the empty-slot log. With a card in, it means the card is not answering — bad card, bad contact, missing pull-up, or the pins are not those pins | step 4 |
| `ESP_ERR_INVALID_CRC`, `ESP_ERR_INVALID_RESPONSE`, or a CID that prints as rubbish | Something answers but the bus is not clean. Note it verbatim — this is a different failure from silence and points at signal integrity rather than at wiring | step 4 |

Record the **exact** log, from `sdmmc_host_init` to the last line. Paste it into
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md), not into
a chat window.

## 4. Step 2 — the same, at default speed

Only if step 1 enumerated. Re-run with `host.max_freq_khz = SDMMC_FREQ_DEFAULT`
(20 MHz). A slot that enumerates at 400 kHz and fails at 20 MHz is a working
connection with a signal-integrity limit, and that limit is a fact the board
layer needs. Record the highest frequency that enumerates.

## 5. Step 3 — mount FAT read-only, and list the root

Only if step 1 enumerated. Run it as a **fresh load**, not appended to step 1's
code: `esp_vfs_fat_sdmmc_mount()` performs the host and slot init itself, so
calling it after step 1's `sdmmc_host_init()` re-initialises what is already up.
Same `host` and `slot` structures, nothing else carried over.

```c
esp_vfs_fat_sdmmc_mount_config_t mount = {
    .format_if_mount_failed = false,     /* NEVER true here — see §1 rule 2 */
    .max_files              = 3,
    .allocation_unit_size   = 0,         /* read only when formatting, which cannot happen above */
};
err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &out_card);
```

Then `opendir("/sdcard")` and print the entries, and `fopen`/`fread` one small
file. Reads only. Expect the filenames you put on the card; long names may be
truncated to 8.3 — the vendor's own build warns *"Long filenames on SD card are
disabled in menuconfig"*, and that is a `CONFIG_FATFS_LFN_*` setting, not a
finding about the board.

Unmount with `esp_vfs_fat_sdcard_unmount()`.

## 6. Step 4 — only if SDMMC failed: try the schematic's reading

Do **not** run this if step 1 succeeded. A slot that enumerates as SDMMC has
answered D14, and re-probing it as SPI answers a question nobody asked.

If step 1 failed with a card in the slot, the schematic's reading gets its turn,
on the same copper and with no re-wiring:

```c
sdmmc_host_t host = SDSPI_HOST_DEFAULT();
host.max_freq_khz = SDMMC_FREQ_PROBING;

spi_bus_config_t bus = {
    .mosi_io_num = GPIO_NUM_1,          /* schematic MOSI == BSP CMD */
    .sclk_io_num = GPIO_NUM_2,          /* schematic SCK  == BSP CLK */
    .miso_io_num = GPIO_NUM_3,          /* schematic MISO == BSP D0  */
    .quadwp_io_num = -1, .quadhd_io_num = -1,
    .max_transfer_sz = 4000,
};
ESP_ERROR_CHECK(spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA));

sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
dev.gpio_cs = GPIO_NUM_17;              /* the chip-select the pin map never had */
dev.host_id = host.slot;

sdspi_dev_handle_t handle;
ESP_ERROR_CHECK(sdspi_host_init());
ESP_ERROR_CHECK(sdspi_host_init_device(&dev, &handle));
host.slot = handle;                     /* the SPI host addresses the device by handle */

sdmmc_card_t card;
esp_err_t err = sdmmc_card_init(&host, &card);   /* enumerate only, again */
if (err == ESP_OK) sdmmc_card_print_info(stdout, &card);
```

Enumeration only, same as step 1, and `format_if_mount_failed = false` if it ever
gets as far as a mount. If **this** enumerates and SDMMC did not, the schematic was right
and the BSP is configuring a mode the board does not support — a much larger
finding than D14 was asking for, and one that would need
[HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md), the BSP reuse decision and
`platform/src/board_profiles.cpp` all revisited.

If **neither** enumerates, D14 stays `PARTIAL` and the next move is the schematic
sheet read visually rather than another software permutation. Two failed software
guesses are not a third guess's evidence.

## 7. Step 5 — put GPIO 17 back where it was

Whatever ran, leave the GPIO configuration as the board boots it. Loading a RAM
image does not reset the peripherals — the S13 session found that the hard way
with an IMU control register — so anything a probe configured survives into
whatever runs next until power is cycled. Cycle it.

## 8. Step 6 — writing one file, and why it is not part of this procedure

**Preconditions, all three:** steps 1–3 succeeded; the card is the expendable one
from §2; and somebody has decided that a write is wanted. Until then this step
does not run, and a read-only result is a complete result — D14 asks which mode
the connector is wired for, and enumeration answers that.

If it does run: `fopen("/sdcard/attadipa_probe.txt", "w")`, write one short line,
close, re-open for reading, compare, `unlink`. One file, a name nothing else
uses, removed on the way out. It proves the write path works, which is a
`StorageService` question rather than a D14 one.

---

## 9. What this closes, and what it does not

| | |
|---|---|
| **Closes** | which host driver enumerates a card on this connector, at which clock, on which pins; whether GPIO 17 is needed |
| **Does not close** | whether `DAT1`–`DAT3` are routed, i.e. whether 4-bit is possible. 1-bit working says nothing about the other three lines, and nothing in Attadipa needs 4-bit today |
| **Does not close** | card-detect. No card-detect pin appears in the BSP's slot configuration or in the schematic reading, so "is a card present" is currently answerable only by trying to enumerate one |
| **Does not close** | D13, the rail the slot is powered from |

## 10. Reuse

The code above is ESP-IDF's own SD card example, reduced:
`examples/storage/sd_card/sdmmc/main/sd_card_example_main.c` and its `sdspi`
sibling, at tag **v5.4** (`8e27ea72c6688b79348b123ff40d556cfe16c8c3`), header
*"This example code is in the Public Domain (or CC0 licensed, at your option.)"*
— the full record is in [REUSE_LEDGER](../research/REUSE_LEDGER.md).

Two deliberate departures from it: the example's `s_example_write_file()` calls
are not in steps 1–5, and its `CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED` option is
not offered at all.
