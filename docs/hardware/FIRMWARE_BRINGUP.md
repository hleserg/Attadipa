# Building, loading and running Attadipa firmware on the Waveshare unit

Covers `firmware/`, the ESP-IDF project added by T-165. It is the floor of the
device milestone: boot, a transcript, and proof that the host libraries link for
xtensa. There is no display, no touch and no LVGL here — that is T-166.

Two routes are described and **they are not equivalent**. Read §1 before §4.

## 0. What you need

| | |
|---|---|
| Toolchain | ESP-IDF **v5.5.5**, pinned — [DEPENDENCIES](../research/DEPENDENCIES.md). Not `master`, not "latest" |
| Board | Waveshare `ESP32-S3-Touch-AMOLED-2.06`, USB serial `28:84:85:B2:18:A4` — [BENCH_DEVICES](../research/BENCH_DEVICES.md) |
| Port | resolved by USB serial, never by `ttyACM` number. There is a second ESP32-S3 on this bench |
| Handling | [BENCH_HANDLING](BENCH_HANDLING.md) applies to every step here |

```bash
. ~/esp/esp-idf/export.sh      # v5.5.5, and check what it prints
cd firmware
idf.py set-target esp32s3
idf.py build
```

`set-target` is only needed once per clean checkout. `sdkconfig.defaults` carries
every board-specific value and each one cites the research note it came from;
read it rather than editing `sdkconfig`, which is a build artefact and is not
committed.

## 1. Which route to use, and why they are not interchangeable

| | RAM route (§2) | Flash route (§4) |
|---|---|---|
| Writes to the part | **nothing at all** | bootloader, partition table and `factory` app |
| Undo | power-cycle | re-flash from the factory backup |
| Survives a reboot | no — that is the point | yes |
| Proved on this unit | **yes**, 2026-08-25 | **yes**, 2026-08-25 |
| Needs a verified factory backup on the machine you are sitting at | no | **yes** |

**Prefer the RAM route for experiments.** It costs nothing to undo, which is
exactly why `docs/ROADMAP.md` names it for anything experimental. T-165 also
exercised the flash route because its acceptance criterion required a boot from
flash; the measured transcript is in
[BRINGUP_2026-08-25](BRINGUP_2026-08-25.md) §4.

**The flash route has one precondition that is not a formality.** OD-19 permits
a session with the board on its desk to flash it, and the reason that permission
is safe is a byte-verified factory backup. The operative backup for the
2026-08-25 run is 33 554 432 bytes, SHA-256
`c423dad3f0d33d56fa96f8590b3da583b05584e85bc2701a7c48c031ad747dbd`;
`verify-flash` checked all of it. **That backup is a host-local asset, not a
repository artefact.** Its durable record is
[BRINGUP_2026-08-25](BRINGUP_2026-08-25.md) §2. If the file itself is not on
the machine you are working from, the reasoning that made flashing reversible
does not apply to you: stay on the RAM route and say so.

## 2. The RAM route

```bash
idf.py -B build-ram -DSDKCONFIG=build-ram/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ramprobe" build
python3 ../tools/flash/ramhold.py build-ram/attadipa.bin 20
```

`sdkconfig.ramprobe` sets both `CONFIG_APP_BUILD_TYPE_RAM` and its dependent
sub-option `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP`; setting only the latter is
silently discarded and produces an ordinary flash image. The real pure-RAM
image runs from IRAM and DRAM and does not initialise the flash driver.

**`-DSDKCONFIG=` is load-bearing.** `idf.py` keeps `sdkconfig` in the *project*
directory rather than the build directory, so a second build directory reuses
the first one's configuration and ignores `SDKCONFIG_DEFAULTS` without a word.
The symptom is a `build-ram/attadipa.bin` byte-identical to the flash image —
a RAM build that is not one, and it reports success. Check it rather than
assume it:

```sh
grep CONFIG_APP_BUILD_TYPE_PURE_RAM_APP build-ram/sdkconfig
```

**Use `ramhold.py` and not `esptool load-ram`.** The CLI tool loads the image
correctly and then kills it a few milliseconds later by exiting: on a
USB-Serial/JTAG board DTR and RTS are GPIO0 and EN, and the kernel drops the
modem lines on the *last* close of a `ttyACM`. Four runs were read as "the board
rejects RAM images" before that was traced to the host.
[WAVESHARE_RUNNING_OUR_CODE](../research/WAVESHARE_RUNNING_OUR_CODE.md) §2.

Two consequences worth carrying:

- **`--after no-reset` does not help.** It governs what esptool does before
  closing, not the close itself.
- **Opening the port to watch is itself a reset**, because pyserial asserts DTR
  and RTS on `open()`. Anything that observes this board must clear both first.

A pure-RAM image must not call the flash partition APIs. The first hardware run
did, and `esp_partition_find()` reached `spi_flash_mmap` without a flash driver
and panicked. The firmware now avoids that path and reports
`Partitions : not readable — a pure-RAM image has no flash driver` instead.

## 3. What a good boot looks like

The measured flash boot included:

```
I (27) boot.esp32s3: Boot SPI Speed : 80MHz
I (27) boot.esp32s3: SPI Mode       : QIO
I (28) boot.esp32s3: SPI Flash Size : 16MB
I (81) octal_psram: vendor id    : 0x0d (AP)
I (88) esp_psram: Found 8MB PSRAM device
I (521) esp_psram: SPI SRAM memory test OK
I (543) attadipa: Flash      : JEDEC c8 40 19, 32 MB on the part, 16MB declared by this build
I (543) attadipa: PSRAM      : 8 MB, octal SPI, initialised
I (553) attadipa: Partition  : factory  type 0/0   0x010000 + 0x400000
I (1553) attadipa: alive 1s, heap 382299
```

The measured pure-RAM boot instead reported flash and partitions as not
initialised/readable and then emitted 30 one-second heartbeats. Full, unedited
context for both routes is in
[BRINGUP_2026-08-25](BRINGUP_2026-08-25.md) §§3–4. The reset reason in that
session was ESP-IDF code 11, driven by the USB host; it was not a power-on reset.

Read four lines before anything else:

| Line | What it settles |
|---|---|
| `rev v0.2` | the die is the one all eight errata in sheet v1.3 describe. Anything else and [ESP32S3_ERRATA_V02](../research/ESP32S3_ERRATA_V02.md) is about a different part |
| `JEDEC c8 40 19` | GigaDevice, 2^25 = 32 MB. **`32 MB detected, 16 MB declared` is correct and not a fault** — only the low half is addressable, and the bootloader's own warning about the mismatch says the same thing |
| `8 MB, octal SPI` | the R8 the eFuses described came up. `2 MB` here would mean this is not that part; `configured but NOT initialised` means octal mode did not take |
| `alive Ns` | it is still running. `app_main` returning would look identical to a hang — silence either way — which is why the heartbeat exists |

### When it is not a good boot

| Symptom | First thing to check |
|---|---|
| Nothing at all on the console | The port. Then whether the tool that loaded the image has exited — §2. **Do not reach for `esp_rom_printf`**: with the ROM console disabled it writes nowhere, and a perfectly healthy image goes mute. `WAVESHARE_RUNNING_OUR_CODE` §2.4 |
| `rst:0x15 (USB_UART_CHIP_RESET)` | Host-driven, by definition. Something closed or opened the port. Not the board |
| `rst:0x8 (TG1WDT_SYS_RST)` and a loop | A real firmware fault. The `Saved PC` says where |
| `PSRAM configured but NOT initialised` | The build is octal and the part did not answer. Do not "fix" it by switching to quad — that contradicts the eFuse read, and the honest outcome is a blocker |
| A partition line with `*** ABOVE THE 16 MB ADDRESSING CEILING ***` | The table on the part is not the table in the repository. `python3 tools/flash/partition_check.py` checks the CSV; that line checks what actually booted |
| The board enumerates but `ramhold.py` says no such serial | The other ESP32-S3. Check `ls /dev/serial/by-id/` against [BENCH_DEVICES](../research/BENCH_DEVICES.md) |

## 4. The flash route

**Exercised by T-165 on 2026-08-25.** Use it only when a persistent image is
required and every precondition below holds.

Preconditions, all of them:

1. the factory backup is **on this machine** and its SHA-256 checked;
2. `python3 tools/flash/partition_check.py` is green;
3. the target partition is below `0x1000000` — `factory` at `0x10000` is.

```bash
idf.py -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B2:18:A4-if00 flash monitor
```

This writes the bootloader, partition table and Attadipa `factory` app, so the
shipped demo stops being what boots. That is recoverable from the backup and
from nowhere else.

Restoring:

```bash
esptool -p <port> write-flash 0x0 <factory-backup.bin>
esptool -p <port> verify-flash 0x0 <factory-backup.bin>
```

`verify-flash` reporting success over all 33 554 432 bytes is the only evidence
that counts. A successful `write-flash` is not it.

### What stays the owner's

Unchanged by OD-19, because re-flashing cannot undo any of it: burning eFuses of
any kind, enabling secure boot or flash encryption, writing production secrets,
destroying keys. Each waits for an explicit request, every time.

## 5. Evidence discipline

A transcript from this procedure is `MEASURED` on the unit it ran on and says
nothing about the T-Watch, which is `ORDERED` and not in hand. A build that
compiles is not a board that boots, and neither is a simulator screenshot. A
step that did not run on hardware is written `NOT EXECUTED — HARDWARE REQUIRED`
and never `PASS`.
