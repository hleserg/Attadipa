# The SD card bench test — Waveshare ESP32-S3-Touch-AMOLED-2.06

**Status: `NOT EXECUTED — HARDWARE REQUIRED`.** Nothing below has been run. No
line of it may be recorded as `PASS` until it has run on a physical board, and
the word for a step that has not run is `NOT EXECUTED — HARDWARE REQUIRED`.

**D14** ([OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md)) asks which bus mode the
micro-SD connector on this board is actually wired for. **This procedure does not
close it, and cannot** — §0 says why, and §10 is the list of what it does and
does not settle. What it does is establish the narrower fact underneath: *which
host driver enumerates a card on this connector, at which clock, on which pins*,
plus a passive reading of GPIO 17. The wiring itself is closed by the schematic
sheet, read visually, and by nothing on this page.

It is written for the owner at a bench, not for an agent.

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

**They are not in conflict, and the reason is stronger than "not necessarily".**
An SD card's own contacts are shared between the two modes **by specification**:
pin 2 is `CMD` in SD mode and `DI`/`MOSI` in SPI mode, pin 5 is `CLK`/`SCK`, and
pin 7 is `DAT0` in SD mode and `DO`/`MISO` in SPI. SPI adds a chip select on
pin 1 (`DAT3`/`CS`) and nothing else. So `MOSI` and `CMD` do not merely *may*
name the same copper — on those three contacts they always do, and on the
ESP32-S3 *"any GPIO may be used for each of the SD card signals"* through the
GPIO matrix, so the pin numbers do not decide it either.

**Which is why enumeration alone cannot answer D14, and this file said it
could until the fifth review round of
[#155](https://github.com/hleserg/Attadipa/pull/155).** A card answering on
GPIO 2/1/3 proves those nets reach `CLK`/`CMD`/`DAT0` — which is exactly what an
**SPI**-wired connector would also show, because they are the same three
contacts. The one thing that differs between the two wirings is the chip select,
so **GPIO 17 is the only discriminator this procedure has**, and any step that
never observes it has not looked at the question. What has never happened is a
card enumerating; that is what this procedure does, and §10 says what that is and
is not evidence for.

**The chip-select near GPIO 17 is the discriminator, and this repository does not
know what GPIO 17 is.** The BSP's pin map does not have it; the schematic
reading is S6, of which
[VERIFIED_FACTS](../research/VERIFIED_FACTS.md) says *"pin-to-net adjacency only
sometimes"*; and **no Waveshare row in
[HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md) assigns GPIO 17 to
anything.**

So the two readings of a successful step 1 are:

- SDMMC 1-bit works and GPIO 17 sits at **a stable level against both internal
  pulls** → *something* is on that pin. Consistent with a routed-but-unused
  `DAT3`/`CS`; equally consistent with a pull-up, a strap or another net that
  has nothing to do with the connector. It narrows the field; it does not name
  the net.
- SDMMC 1-bit works and GPIO 17 **follows both pulls** → as far as this test can
  see the pin is floating, which is consistent with it not reaching the
  connector at all. Nothing has been learnt about the connector, only about the
  host driver.

Both are results, and **step 0 observes GPIO 17 read-only** — before step 1, so
that the observation exists on the success path as well as the failure one.
Neither reading closes D14; that is what the schematic sheet is for. What this
procedure must not do is *drive* the pin — see §1 rule 7.

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
7. **Do not drive a pin this repository has not identified, and GPIO 17 is one.**
   The first six rules are all about the card; this one is about the board, and
   it was missing until the fifth review round of
   [#155](https://github.com/hleserg/Attadipa/pull/155) — while §7 configured
   GPIO 17 as a push-pull output toggled on every transaction. **No Waveshare row
   in [HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md) assigns GPIO 17 to
   anything**, and the only evidence for a chip select near it is S6, of which
   [VERIFIED_FACTS](../research/VERIFIED_FACTS.md) says *"pin-to-net adjacency
   only sometimes"*. An output driving into something that is also driving —
   another output, a strap held by a divider, a rail through a low-value
   resistor — is a contention on the owner's only unit, and `CLAUDE.md`'s first
   rule covers a bench action as much as a line of code. So: **reading GPIO 17 is
   always allowed; driving it needs the schematic sheet read first** (§7
   precondition 3), and until that has happened GPIO 17 is `UNKNOWN` and stays
   an input.
8. **Record the PMU rail state before concluding anything from a failure.** A
   negative from a probe on an unpowered slot is not a negative about the
   wiring — see §3 and §10.

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

## 3. Step 0 — read the rail and read GPIO 17, before anything is driven

**Two readings, both passive, both before step 1.** Neither closes anything on
its own; both stop a later negative from being read as evidence it is not.

**The rail.** [HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md)'s SD row gives
the slot's power rail as **`D13`** — an open question, not an answer. And the
rails on this unit are not knowable from the code that is running: *"loading a
RAM image does not reset the peripherals"*
([WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §5), and
the vendor BSP *"does not configure the PMU at all"*
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)) — so which rails are up depends
on what booted last and whether §8's power cycle happened. This board has already
produced exactly that failure once in the session this procedure is built on: the
FT3168 was **absent from an I2C scan entirely** until GPIO 9 was pulsed
([WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §3.4).

So: **read every AXP2101 output register over I2C and write the values down**,
the way §3.4 of that document already did. A `0x107` from an unpowered slot is
indistinguishable from a `0x107` from a mis-wired one, and this file's failure
table used to offer four causes without that one on the list.

**GPIO 17.** Configure it as an **input** — `gpio_set_direction(GPIO_NUM_17,
GPIO_MODE_INPUT)` with no pull — read `gpio_get_level()`, then read it again with
the internal pull-up on and once more with the internal pull-down on. Write down
all three. A pin held at one level against both internal pulls has something on
it; a pin that follows both is floating as far as this test can tell. That is the
whole of the read, and it is the observation §0 says the success path was
missing. **Do not set it as an output.** §1 rule 7.

---

## 4. Step 1 — SDMMC 1-bit, probing frequency, enumeration only

The BSP's own configuration, at the slowest legal clock. `SDMMC_FREQ_PROBING` is
400 kHz, which is what the SD specification's identification phase uses and the
setting most tolerant of marginal signal integrity.

```c
sdmmc_host_t host = SDMMC_HOST_DEFAULT();  /* .slot defaults to SDMMC_HOST_SLOT_1 */
host.max_freq_khz  = SDMMC_FREQ_PROBING;   /* 400 kHz — identification speed */

sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
slot.width = 1;                            /* 1-bit, as the BSP declares. host.flags
                                              needs no matching edit — see below */
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
PROBE_TRY(sdmmc_host_init());
PROBE_TRY(sdmmc_host_init_slot(host.slot, &slot));
esp_err_t err = sdmmc_card_init(&host, &card);   /* enumerate only — no filesystem */
if (err == ESP_OK) sdmmc_card_print_info(stdout, &card);
else printf("card_init -> 0x%x (%s)\n", err, esp_err_to_name(err));
```

**`PROBE_TRY`, and not `ESP_ERROR_CHECK`.** `ESP_ERROR_CHECK` panics on a
non-`ESP_OK` return, and a panic resets the chip. On this unit the console
survives only while the USB port is never closed
([WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §2), so
a reset ends the session and takes the log — which is the deliverable — with it.
A probe whose whole output is *"the exact log"* must print the `esp_err_t` and
stop:

```c
#define PROBE_TRY(call)                                                      \
    do {                                                                     \
        esp_err_t e_ = (call);                                               \
        if (e_ != ESP_OK) {                                                  \
            printf("STOP: %s -> 0x%x (%s)\n", #call, e_, esp_err_to_name(e_)); \
            return;                                                          \
        }                                                                    \
    } while (0)
```

`sdmmc_card_init()` plus `sdmmc_card_print_info()` is the whole of step 1. It
reads CID and CSD and writes nothing.

**`slot.width = 1` is enough, and `host.flags` is deliberately not set.** The
common recipe sets `host.flags = SDMMC_HOST_FLAG_1BIT` as well, because
`SDMMC_HOST_DEFAULT()` leaves `.flags` carrying `_8BIT | _4BIT | _1BIT | _DDR`
and the bus-width step reads `host.flags` rather than the slot config. Every
other API detail in this file is cited; this one decides whether step 1 can fail
on a *correctly* wired slot, so it is cited too. At tag **v5.4**, three lines
settle it:

- `sdmmc_card_init()` runs `sdmmc_fix_host_flags()` as its own init step, before
  any bus-width work —
  `components/sdmmc/sdmmc_init.c:69` "SDMMC_INIT_STEP(!is_spi, sdmmc_fix_host_flags);",
  against the width steps at `:144-147`.
- That function asks the host for the *slot's* width and rewrites the flags to
  match —
  `components/sdmmc/sdmmc_common.c:363` "int slot_bit_width = card->host.get_bus_width(card->host.slot);"
  — and for a 1-bit slot carrying 4- or 8-bit flags does
  `card->host.flags &= ~width_mask; card->host.flags |= width_1bit;` (`:364-367`).
- With `_4BIT` cleared, `sdmmc_init_sd_scr()`'s test fails —
  `components/sdmmc/sdmmc_sd.c:79` "&& (card->host.flags & SDMMC_HOST_FLAG_4BIT)) {"
  — so `log_bus_width` stays 0 and `sdmmc_init_sd_bus_width()` sends ACMD6 for
  `width = 1` (`:131-136`).

**So a correctly wired 1-bit slot does not fail on the width step, and a failure
at step 1 is not this.** Read at tag **v5.4**
(`8e27ea72c6688b79348b123ff40d556cfe16c8c3`), the same tag as the example this
snippet is reduced from. `components/sdmmc/` is the protocol layer;
`components/esp_driver_sdmmc/`, cited below, is the host driver.

**Why `d1`–`d7` are blanked, and it is not tidiness.** On the ESP32-S3
`SDMMC_SLOT_CONFIG_DEFAULT()` does not leave the unused data lines empty. At
tag **v5.4**,
`components/esp_driver_sdmmc/include/driver/sdmmc_default_configs.h:99-115`
(the `#elif CONFIG_IDF_TARGET_ESP32S3` arm, inside the block the header itself
closes with `#endif  // GPIO Matrix chips`) presets `clk = 14`, `cmd = 15`,
`d0 = 2`, `d1 = 4`, `d2 = 12`, `d3 = 13`, **`d4 = 33`, `d5 = 34`, `d6 = 35`,
`d7 = 36`**.

**`d4`–`d7` are the memory rail.** This repository has read that off the die:
`PIN_POWER_SELECTION = VDD_SPI` puts **GPIO33–37** on the memory rail, they are
octal PSRAM's DQ4–DQ7 and DQS, and
[VERIFIED_FACTS](../research/VERIFIED_FACTS.md) states they are *"confirmed
unavailable to any application"* (S10). So the four defaults this test would
inherit for `d4`–`d7` are precisely the four pins that are not the
application's to touch on this part.

An earlier version of this paragraph gave the defaults as
`d1 = 40 … d7 = 48` and called the hazard the I2S bus and the amplifier enable.
**Those are the ESP32-P4 numbers** — the `#elif CONFIG_IDF_TARGET_ESP32P4` arm
immediately above, `:82-97` — read out of the wrong branch, and the review round
that caught it is the fifth of #155. The instruction was right and the reason
was wrong, which in a bench procedure is the worse half.

The defaults are also inert *today*: reading v5.4, `sdmmc_host_init_slot()`
configures only `clk`, `cmd` and `d0` at `width = 1`
(`esp_driver_sdmmc/src/sdmmc_host.c:734-736`) — the `d1`/`d2` calls and the
"force D3 high" block are both behind `if (slot_width >= 4)` at `:738`, and
`sdmmc_host_pullup_en_internal()` likewise stops at `cmd` and `d0`
(`:1136-1137`). Inert because of an internal branch in one version of one
driver, over four pins the datasheet and the eFuses both say are spoken for.
Name them `GPIO_NUM_NC` and the question does not arise.

**Leave GPIO 17 alone in this step.** Do not configure it, drive it or pull it.
Whether the slot works without it is half the question.

| Outcome | What it means | Then |
|---|---|---|
| `Name`, `Type`, `Speed`, `Size` printed | The socket's `CLK`/`CMD`/`DAT0` reach GPIO 2/1/3 and **the SDMMC host driver enumerates a card here in 1-bit mode**. That is the claim, and it is narrower than the connector's wiring — §0 says why, and §10 says what it is and is not evidence for | step 2 |
| `sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107` | The same line as the empty-slot log. With a card in, it means the card is not answering. **First suspect the slot's supply** — the rail is `D13`, open, and step 0's register dump is what rules it out; only then bad card, bad contact, missing pull-up, or the pins are not those pins | step 4 |
| `ESP_ERR_INVALID_CRC`, `ESP_ERR_INVALID_RESPONSE`, or a CID that prints as rubbish | Something answers but the bus is not clean. Note it verbatim — this is a different failure from silence and points at signal integrity rather than at wiring | step 4 |

Record the **exact** log, from `sdmmc_host_init` to the last line. Paste it into
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md), not into
a chat window.

## 5. Step 2 — the same, at default speed

Only if step 1 enumerated. Re-run with `host.max_freq_khz = SDMMC_FREQ_DEFAULT`
(20 MHz). A slot that enumerates at 400 kHz and fails at 20 MHz is a working
connection with a signal-integrity limit, and that limit is a fact the board
layer needs. Record the highest frequency that enumerates.

## 6. Step 3 — mount FAT read-only, and list the root

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

## 7. Step 4 — only with the schematic read: try the schematic's reading

This is the only step that **drives** GPIO 17, and GPIO 17 is `UNKNOWN` in this
repository. It therefore has preconditions rather than a trigger, and all four
hold before any of it runs:

1. **Step 1 and step 2 both failed** with a card in the slot. A slot that
   enumerates as SDMMC has answered what this procedure can answer, and
   re-probing it as SPI answers a question nobody asked.
2. **Step 0's rail dump shows the slot powered.** A negative from an unpowered
   slot is not a negative about the wiring, and §1 rule 8 makes recording the
   rails a condition of concluding anything from a failure.
3. **The schematic sheet has been read and GPIO 17 traced to a net.** Not
   afterwards, as the recovery from a second failure — *before*, as the thing
   that makes driving the pin admissible at all. §1 rule 7: reading GPIO 17 is
   always allowed, driving it needs the sheet read first. The only evidence for
   a chip select near GPIO 17 is S6, of which
   [VERIFIED_FACTS](../research/VERIFIED_FACTS.md) says *"pin-to-net adjacency
   only sometimes"*, and an output driving into another output, a strap held by
   a divider, or a rail through a low-value resistor is a contention on the
   owner's only unit. **If the sheet cannot be read, this step does not run** —
   D14 stays `PARTIAL`, which is an honest outcome and a shorted pin is not.
4. **A fresh load.** Step 1 called `sdmmc_host_init()` and
   `sdmmc_host_init_slot()`, which clock the SDMMC peripheral, install an ISR
   and route GPIO 1/2/3 through the GPIO matrix; `spi_bus_initialize()` then
   claims the same three pins. Either reload the image — the cleanest, and it
   also re-reads step 0 — or call `sdmmc_host_deinit()` before anything below.
   Do not stack the two hosts on one boot.

With all four held, the schematic's reading gets its turn, on the same copper
and with no re-wiring:

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
PROBE_TRY(spi_bus_initialize(host.slot, &bus, SDSPI_DEFAULT_DMA));

sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
dev.gpio_cs = GPIO_NUM_17;              /* the chip-select the pin map never had */
dev.host_id = host.slot;

sdspi_dev_handle_t handle;
PROBE_TRY(sdspi_host_init());
PROBE_TRY(sdspi_host_init_device(&dev, &handle));
host.slot = handle;                     /* the SPI host addresses the device by handle */

sdmmc_card_t card;
esp_err_t err = sdmmc_card_init(&host, &card);   /* enumerate only, again */
if (err == ESP_OK) sdmmc_card_print_info(stdout, &card);
else printf("card_init (spi) -> 0x%x (%s)\n", err, esp_err_to_name(err));
```

Enumeration only, same as step 1, and `format_if_mount_failed = false` if it ever
gets as far as a mount. If **this** enumerates and SDMMC did not, the schematic was right
and the BSP is configuring a mode the board does not support — a much larger
finding than D14 was asking for, and one that would need
[HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md), the BSP reuse decision and
`platform/src/board_profiles.cpp` all revisited.

If **neither** enumerates, D14 stays `PARTIAL`. By precondition 3 the sheet has
already been read by the time this step runs, so the next move is not another
software permutation — it is a meter on the connector, or an owner decision that
the slot is not worth the bench time. Two failed software guesses are not a
third guess's evidence.

## 8. Step 5 — put GPIO 17 back where it was

Whatever ran, leave the GPIO configuration as the board boots it. Loading a RAM
image does not reset the peripherals — the S13 session found that the hard way
with an IMU control register — so anything a probe configured survives into
whatever runs next until power is cycled. Cycle it.

## 9. Step 6 — writing one file, and why it is not part of this procedure

**Preconditions, all three:** steps 1–3 succeeded; the card is the expendable one
from §2; and somebody has decided that a write is wanted. Until then this step
does not run, and a read-only result is a complete result — everything this
procedure can establish (§10) is established by reading, and a write adds
nothing to it.

If it does run: `fopen("/sdcard/attadipa_probe.txt", "w")`, write one short line,
close, re-open for reading, compare, `unlink`. One file, a name nothing else
uses, removed on the way out. It proves the write path works, which is a
`StorageService` question rather than a D14 one.

---

## 10. What this closes, and what it does not

| | |
|---|---|
| **Closes** | which host driver enumerates a card on this connector, at which clock, on which pins |
| **Does not close** | **which mode the connector is *wired* for.** Pins 2/5/7 of an SD card are `CMD`/`CLK`/`DAT0` in native mode and `DI`/`SCK`/`DO` in SPI mode — the same three contacts by specification, so a card enumerating over SDMMC on GPIO 2/1/3 is exactly what a slot wired for SPI would also produce. Only the schematic sheet closes the wiring, and §7 precondition 3 is where it is read |
| **Does not close** | **whether GPIO 17 is a chip select.** Step 0 records what the pin does with no pull, a pull-up and a pull-down, which is enough to say *something is on it* or *it floats as far as this test can tell*; it is not enough to name the net. S6 is pin-to-net adjacency, *"only sometimes"* |
| **Does not close** | whether `DAT1`–`DAT3` are routed, i.e. whether 4-bit is possible. 1-bit working says nothing about the other three lines, and nothing in Attadipa needs 4-bit today |
| **Does not close** | card-detect. No card-detect pin appears in the BSP's slot configuration or in the schematic reading, so "is a card present" is currently answerable only by trying to enumerate one |
| **Does not close** | D13, the rail the slot is powered from |

## 11. Reuse

The code above is ESP-IDF's own SD card example, reduced:
`examples/storage/sd_card/sdmmc/main/sd_card_example_main.c` and its `sdspi`
sibling, at tag **v5.4** (`8e27ea72c6688b79348b123ff40d556cfe16c8c3`), header
*"This example code is in the Public Domain (or CC0 licensed, at your option.)"*
— the full record is in [REUSE_LEDGER](../research/REUSE_LEDGER.md).

Two deliberate departures from it: the example's `s_example_write_file()` calls
are not in steps 1–5, and its `CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED` option is
not offered at all.
