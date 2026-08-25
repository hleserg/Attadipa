# What is actually in the Waveshare's 32 MB, and one claim that has to be withdrawn

Status: **read off the flash of a physical unit.** The partition table was dumped
from `0x8000` of the received `ESP32-S3-Touch-AMOLED-2.06` on 2026-08-22 and
parsed here byte for byte; the `model` and `storage` partitions were dumped whole.
Source key **S11** in [HARDWARE_MATRIX](HARDWARE_MATRIX.md).

Most of this corroborates a findings document produced alongside it in another
session. **One row of that document is wrong on a checkable fact and would have
been designed against**, so §1 deals with that before anything else.

## 1. The PSRAM is octal, and "octal would be 1.8 V" is not true

The parallel document states:

> **PSRAM** | **8 MB, QUAD, AP_3v3 (APmemory, 3.3 V)** […] **Resolves open
> question D12** (quad vs octal PSRAM): it is **QUAD**, not octal. octal PSRAM
> would be 1.8 V; AP_3v3 is the 3.3 V quad part. This halves the usable PSRAM
> bandwidth vs an octal assumption and directly constrains the LVGL draw-buffer
> strategy at 410×502 — plan buffers against quad throughput, not octal.

The **readings** it rests on are right and agree with ours exactly:
`PSRAM_CAP = 8M`, `PSRAM_VENDOR = AP_3v3`. The **inference** from them does not
hold, on one specific point:

> ESP32-S3 Series Datasheet v2.2, §1.2 Table 1-1 "ESP32-S3 Series Comparison",
> p. 13:
> `ESP32-S3R8 | — | 8 MB (Octal SPI) | −40 ~ 65 °C | 3.3 V`

`ESP32-S3R8` is **octal at 3.3 V**, in one row, in the same table. So 3.3 V does
not distinguish quad from octal. What the `V` suffix distinguishes is the
`VDD_SPI` rail: `R8` is 3.3 V and `R8V` is 1.8 V, and footnote 3 puts both on the
octal side — *"For chips with Octal SPI PSRAM (ESP32-S3R8, ESP32-S3R8V, and
ESP32-S3R16V)…"*.

And the stronger point: **the table contains no 8 MB quad in-package variant at
all.** The only quad in-package parts are the 2 MB `RH2`, `R2` (EOL) and `FH4R2`.
There is no part for `PSRAM_CAP = 8M` + quad to *be*.

Two further readings from the same unit point the same way:

- `PIN_POWER_SELECTION = VDD_SPI` fuses **GPIO33–37** to the memory rail. Those
  five pins are DQ4–DQ7 and DQS, and Datasheet Table 2-14 populates them **only**
  in the Octal SPI column.
- Five of six vendor examples for this board ship `CONFIG_SPIRAM_MODE_OCT=y` with
  `CONFIG_SPIRAM_IGNORE_NOTFOUND` unset — a build that aborts at boot if octal
  PSRAM is not found. Those builds run on this board.

**Why this one mattered enough to spend a section on.** The conclusion drawn from
it — *"plan buffers against quad throughput, not octal"* — is an architectural
constraint on the LVGL draw-buffer strategy at 410×502, which is the single
tightest budget on this board. Designing to half the real bandwidth is expensive
and invisible. In the other direction it is not invisible at all:
`CONFIG_SPIRAM_MODE_QUAD` on an octal part does not initialise PSRAM at all, so
a build made on that assumption fails loudly rather than quietly — which is the
only comfortable thing about this.

**The decisive empirical check has now run.** Both the vendor boot and
Attadipa's own flash boot logged the `octal_psram` path, AP vendor `0x0d`, an
8 MB device at 80 MHz and a successful SRAM test. D12a is therefore
**VERIFIED by datasheet/eFuse and MEASURED on silicon** — see
[BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §4. The quad reading is
recorded here as **refuted, with its reasoning named**, so it is not re-derived.

## 2. The factory partition table

Dumped from `0x8000`, parsed from the raw 32-byte entries. The MD5 entry at the
end is `afeac03c342d6f4cff060f7fc86cb365`.

| Label | Type | SubType | Offset | Size | |
|---|---|---|---|---|---|
| `nvs` | data | nvs | `0x009000` | 24 KB | settings, calibration |
| `otadata` | data | ota | `0x00f000` | 8 KB | active-slot selector |
| `phy_init` | data | phy | `0x011000` | 4 KB | RF calibration |
| `model` | data | spiffs | `0x012000` | 952 KB | **ESP-SR voice models** — §3 |
| `factory` | app | factory | `0x100000` | **9 MB** | `phone_s3_box_3` — the app that runs, §2.1 |
| `ota_0` | app | ota_0 | `0xa00000` | 6 MB | **not empty — `xiaozhi` 1.8.5 lives here**, §2.1 |
| `ota_1` | app | ota_1 | `0x1000000` | 6 MB | OTA slot B, erased |
| `storage` | data | spiffs | `0x1600000` | 6 MB | UI assets — §4 |

**28 of 32 MB is partitioned. `0x1C00000`–`0x2000000` is genuinely spare.**

Two things in that table are worth noticing rather than copying:

- **`factory` is 9 MB and the OTA slots are 6 MB — and this document previously
  drew a wrong conclusion from that.** It said the factory image "cannot be put
  back through OTA — it is larger than either slot". That inferred an *image*
  size from a *partition* size, and the image has now been measured: **4.94 MB**
  (§2.1). It fits in a 6 MB slot with a megabyte to spare. The vendor's update
  path can restore the shipped build. The backup still matters, but not for that
  reason.

  Recorded rather than quietly deleted, because the error is the interesting
  part: a partition size is an allocation, and reading it as a size is a mistake
  this table invites.
- **A `0xFF` region is not an empty region.** The run of `0xFF` at `0x1000000` is
  the *unwritten `ota_1` slot*, not unused flash. Reading a dump and concluding
  "the second half is empty" is a mistake this board invites; the parallel
  document caught and corrected it in itself, and it is recorded here so the next
  reader does not make it a third time.

T-165 now supplies a deliberately small development table (`nvs`, `phy_init`
and a 4 MB `factory`, all below `0x1000000`). The permanent product layout is
still not decided, and this vendor table does not decide it — 32 MB is roomy
enough that dual 6 MB slots plus assets is comfortable, which is a *useful* fact
for whoever writes ours, not a template to copy.

## 2.1 What is actually in the app slots

Read from a second, independent 32 MB dump taken 2026-08-22 over USB/IP from
Linux — see §2.2 — by parsing the ESP image header at each slot offset and the
`esp_app_desc_t` that follows the first segment.

| Slot | `project_name` | `version` | Built | `idf_ver` | Image size |
|---|---|---|---|---|---|
| bootloader `0x0` | — | — | — | — | 22 480 B |
| `factory` `0x100000` | **`phone_s3_box_3`** | `v0.4.2-92-g5c6be6c-dirty` | 4 Nov 2025 14:21:55 | **`v5.5.1-dirty`** | **5 175 184 B (4.94 MB)** |
| `ota_0` `0xa00000` | **`xiaozhi`** | **`1.8.5`** | 31 Oct 2025 10:25:46 | **`v5.5.1-dirty`** | **5 481 872 B (5.23 MB)** |
| `ota_1` `0x1000000` | — | — | — | — | erased, `0xFF` throughout |

**`otadata` is blank** — both 4 KB entries are `0xFF` end to end, so no slot has
ever been selected and the bootloader falls through to `factory`. The app on the
screen is `phone_s3_box_3`, which is also what its launcher looks like:
DrawPanel, SpecAnalyzer, AIChats, GravitySphere, VideoPlayer, Gallery,
MusicPlayer, Settings.

Three things follow, and each is worth more than the dump it came from.

**`xiaozhi` is on the device, in full, at a known version.** Not inferred from a
wake-word model this time — the string `xiaozhi` and the version `1.8.5` are in
the application descriptor. §3 reached the right conclusion by the wrong route
and is corrected below. For the `xiaozhi-esp32` licence review this matters concretely:
the audio path to read is the one at **tag `1.8.5`**, not at `HEAD`. Reading a
newer version of somebody's code than the one on the board in front of you is
research into a different program.

**Both images were built with ESP-IDF `v5.5.1`.** That is the vendor's own
answer to the question T-004 asks — which IDF version to target — from the
vendor's own shipping firmware for this exact panel. It is not binding on us and
it is not a recommendation; it is one version about which something is *known*.
Note the `-dirty` on both the app version and the IDF version: they built from
modified working trees, so `v5.5.1` names a starting point, not a reproducible
one.

**The `factory` image is 4.94 MB, not 9 MB.** See the corrected bullet above.

> **Not committed, deliberately:** neither the dump nor any image extracted from
> it. `phone_s3_box_3` and `xiaozhi` as built by Waveshare are somebody else's
> binaries, and §4.4 already covers the third-party audio rights in `storage`.
> Everything above is a *measurement of* those files, which is ours to record.

## 2.2 The dump, and a stub failure reproduced on a second host

The owner's first pass (Windows 11, native USB) found that `esptool`'s stub
flasher aborted at five addresses — `0x023d000`, `0x476000`, `0xbef000`,
`0xdcc000`, `0xe61000` — and that `--no-stub` read those regions fine.

That has now been reproduced with almost nothing in common but the board:

| | Owner's pass | This pass |
|---|---|---|
| Host OS | Windows 11 | Linux, WSL2 kernel 6.18.33.2 |
| USB transport | native host controller | **USB/IP over TCP** (`usbipd` → `vhci_hcd`) |
| `esptool` | 5.3.1 | 5.3.1 |
| Error text | `Packet content transfer stopped` | `No more data to read from the serial port` |

Reading in 2 MB chunks, the chunks that failed the stub were **exactly** the five
containing those addresses, and no others — across **two** complete passes, so
ten failures out of ten predicted. The other eleven chunks read first time.

**This eliminated several host-specific explanations, not the host itself.**
The Windows driver, selective suspend and native host controller were absent in
the second run, but host read granularity remained capable of changing which
blocks fail. Two different error strings described the same stalled transfer;
the message text was never the signal.

Two hypotheses were then tested and **rejected**, recorded so nobody spends the
afternoon again:

- **A monotonic SLIP-density threshold.** The stub streams flash data
  SLIP-framed, so `0xC0` and `0xDB` must be escaped and a page dense in them
  inflates on the wire. Counting per 4096-byte page: the abort page in chunk
  `0x0200000` ranks **9th of 512**, and in chunk `0x0400000` **23rd of 512**.
  The worst pages in those same chunks were read without complaint. That rejects
  density *rank* as the condition; it does not reject a condition on the encoded
  packet length.
- **A silicon erratum.** The ESP32-S3 errata sheet v1.3 has nothing on the
  USB-Serial/JTAG read path; its only USB entry, USBOTG-4289, concerns USB-OTG
  download mode and names USB-Serial/JTAG as the *remedy*.
  [ESP32S3_ERRATA_V02](ESP32S3_ERRATA_V02.md) carries the full reading — all
  eight errata apply to this chip and none of them is this one.

The 2026-08-25 experiment resolved the tested predicate. On the Linux host,
`cdc-acm` reads in 128-byte buffers; for a 4096-byte block the eleven cases with
`(count(0xC0) + count(0xDB)) % 128 == 62` all stalled. Two `% 64`-only controls,
`0x476000` and `0x5df000`, both read. That is **13/13 measured outcomes** for
this image and host. With a 64-byte host read, the predicate names the broader
failure set seen earlier, including `0x476000`.

The experiment proves a host-granularity-dependent congruence for the tested
image, not a universal list of addresses. The explanation — a full 64-byte USB
packet left in a half-filled host buffer while the stub waits for its ACK — fits
the result and the protocol, but remains an explanation rather than a direct
measurement. Full arithmetic and the measured table are in
[BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §2.1.

**And `--no-stub` is not slow.** It recovered a 2 MB chunk in about 30 seconds
here — roughly 65 KB/s, over a *network* USB transport — against the ~15 KB/s
the first pass measured. "No-stub is unusably slow" was a fact about that host,
not about the tool.

**And the stub can *read* those addresses perfectly well.** This falls out of the
verification below and is easy to miss: `esptool verify-flash 0x0` succeeded over
all 33 554 432 bytes, and `verify-flash` works by asking the **stub** to compute
an MD5 on the device and comparing one 16-byte digest. The stub therefore walked
every byte of all five problem regions without complaint — it just cannot
*stream* them back. That proved the failure acts on the **device-to-host transfer
path**, not on flash access. The later congruence experiment characterises the
tested transfer failure as described above; it does not turn it into defective
flash.

### Verification: three independent reads agree, and the scare was mine

Two complete 32 MB passes taken back to back are **byte-for-byte identical**, all
sixteen chunks matching. Assembled in the correct order they hash to

```
2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5
```

which is **exactly** the SHA256 the owner recorded for their own first pass on
Windows. `esptool verify-flash 0x0` over all 33 554 432 bytes returns
**`Verification successful`**.

So **three complete reads of this flash — one on Windows over native USB, two on
Linux over USB/IP — agree byte for byte, and the device's own MD5 agrees with all
three.** The backup is verified. The `storage` partition did not change; nothing
changed.

Before T-165 flashed Attadipa on 2026-08-25, a later complete factory backup was
captured and checked over all 33 554 432 bytes with `verify-flash`. Its SHA-256
is `c423dad3f0d33d56fa96f8590b3da583b05584e85bc2701a7c48c031ad747dbd`.
It differs from the older image above, but the difference was not localised
because the older binary was not present on that host. The current backup is a
host-local recovery asset, not a repository artefact; its durable record is
[BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §2.

> **This section previously said the hashes did not match and left it
> "unresolved", with a paragraph inviting the reader to suspect the owner's dump.
> That was wrong and it is retracted.** The owner's dump was correct all along.
>
> **The defect was in my own reassembly, and it is worth recording because it is
> the exact trap [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md) §2.4 warns
> about wearing a different hat.** §2.4 says concatenating aborted chunks is
> silently wrong. The chunks here were not aborted — they were *concatenated in
> the wrong order*, by `ls -v` on filenames like `c_0x0a00000.bin`. Version sort
> reads a hexadecimal name as a version string, so it produced
>
> ```
> c_0x0000000  c_0x0a00000  c_0x0c00000  c_0x0e00000  c_0x1a00000 …
> ```
>
> which is not numeric order: `0x0a00000` lands second, ahead of `0x0200000`.
> Version sort splits a name into digit and non-digit runs, and `0x0a00000`
> breaks at the `a` where `0x0200000` does not, so the two are never compared as
> numbers at all. **Sort chunk files by their numeric offset, never by their
> name.** A careful chunked reader with retries and a `--no-stub` fallback was
> undone in the last line of the script by `ls`.
>
> It also explains why the first round of targeted verification looked so
> alarming: `0x0`, `0x1000` and `0x100000` all verified, and `0x1600000` did not.
> All three passing offsets fall inside chunk `0x0000000`, which is first in both
> the wrong order and the right one. The one failing slice was the first that
> did not.

**The general lesson survives its own false alarm**, and is worth keeping for the
day something really does differ: compare **per chunk**, not per image. A
whole-image hash is a single bit of information that tells you *that* two reads
disagree and never *where*, and on a live device it also mixes in partitions the
firmware is entitled to rewrite.

### `nvs`, `otadata` and `phy_init` did not move either

Read three times with a hard reset and 90 seconds of running between each:
identical, all hashing to
`803798ee52013c09e9dd55a72226d0195ec6a3582f85af3b43315f9247b3e26e`. Recorded
because it was originally an attempt to explain a mismatch that turned out not to
exist, and it stands on its own as a fact about this firmware: **it does not
rewrite its own configuration partitions on an ordinary boot.**

> **The caveat this measurement carried is now closed, and it needed a human to
> close it.** The reads ended with `--after hard-reset`, but nothing here can see
> a screen, so whether the application actually *ran* between them was `UNKNOWN` —
> and if it never ran, the whole test measured only that flash is stable in
> download mode, which nobody doubted.
>
> Resolved by observation on 2026-08-22: the device was cycled six times through
> download mode and back with the same `--no-stub … --after hard-reset` shape the
> NVS reads used, and the owner watched the panel. **It blinked on every cycle** —
> dark, then the launcher again. So `--after hard-reset` does restart the
> application on this unit, the firmware was running between the three reads, and
> the result stands: **`phone_s3_box_3` does not rewrite `nvs`, `otadata` or
> `phy_init` on an ordinary boot.**
>
> Worth keeping as method, not just as a result: a claim about what firmware does
> while running cannot be verified from the host side alone, and the cheapest
> instrument available was a person glancing at the watch.

## 3. `model` — a wake-word model, and what it does not prove


The `model` partition is not a filesystem. It is esp-sr's `srmodels` container —
a little-endian count followed by 32-byte name records. Two entries:

- `wn9_nihaoxiaozhi_tts` — WakeNet9, wake word 你好小智 / *"Ni hao Xiao Zhi"*,
  with TTS;
- `wn9_data` — the wake-word model data.

From that, this document originally concluded that the launcher's **AIChats**
app *is* [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32). **The conclusion
was right and the reasoning was not**, and §2.1 is why: `xiaozhi` **1.8.5** is on
this flash as a complete, separate application in `ota_0`, named in its own
descriptor. The app that actually runs is `phone_s3_box_3`, and whether its
AIChats screen is xiaozhi code, a reimplementation, or a client for the same
service is **`UNKNOWN`** — nothing here establishes it. Both apps can use the
`model` partition; a shared wake-word blob does not identify either of them.

What a wake-word model proves is that *something* on this board does on-device
wake-word detection. It does not prove which program. Keeping the two claims
apart matters, because the second one is the one the reuse case rests on.

The reuse case survives intact and is now stronger: see the
[reuse ledger](REUSE_LEDGER.md) entry, because `xiaozhi-esp32` is an open-source
firmware that **targets this exact board** — and we now know the vendor shipped
it, at a version we can name. It contains this board's audio path written out:
the I2S wiring, the ES8311 codec bring-up and how the two microphones are used.
Reading it is cheaper and more reliable than reversing either app, and the
licence has to be checked before anything is taken from it.

Nothing here is a decision to ship a wake word. This repository has no such
requirement and adding one would be a product change.

## 4. `storage` — extracted, and the stored format is settled

SPIFFS, not littlefs. Extracted with `tools/flash/spiffs_extract.py`, which was
written for this because `mkspiffs -u` needs a toolchain nobody had to hand and
`strings` recovers a SPIFFS image's file *names* and none of its file *bodies* —
the data is scattered across pages that are neither contiguous nor in order.

**There are six files, not three.** The parallel document recorded *"only three
real files, all raw binaries in an `/image/` dir"*; there is also a `/music/`
directory, and what is in it changes an argument elsewhere in this repository.

| File | Size | What it is |
|---|---|---|
| `/image/image1.bin` | 411 652 | 410 × 502 RGB565 — §4.1 |
| `/image/image2.bin` | 411 652 | the same |
| `/image/image3.bin` | 411 652 | the same |
| `/music/BGM_1.mp3` | 207 713 | MPEG-1 Layer III, 112 kbps, 44.1 kHz, **mono** |
| `/music/BGM_2.mp3` | 199 664 | MPEG-1 Layer III, 112 kbps, 44.1 kHz, **stereo** |
| `/music/BGM_3.mp3` | 380 917 | MPEG-1 Layer III, 128 kbps, 44.1 kHz, stereo; ID3v2.4 tag of 139 756 bytes, so most of it is embedded artwork |

### 4.1 The image format, decoded rather than guessed

Every one of the three images is **exactly 411 652 bytes**, which is
411 640 + 12. 411 640 is a full 410 × 502 frame at two bytes per pixel. The
twelve bytes are a header, and it decodes cleanly:

| Offset | Size | Value | Meaning |
|---|---|---|---|
| 0 | `u32` LE | `0x00001219` | constant across all three; a magic or format code. Its meaning is `UNKNOWN` |
| 4 | `u16` LE | `410` | width |
| 6 | `u16` LE | `502` | height |
| 8 | `u32` LE | `820` | stride in bytes = width × 2 |

`12 + width × height × 2` equals the file length exactly, for all three files.
Pixels follow the header, row-major, no row padding.

**The on-disk byte order is little-endian, and this was settled by rendering
rather than by argument.** Decoded as little-endian RGB565 the files are coherent
artwork — a neon figure, a bird over a synthwave skyline, a third scene. Decoded
big-endian they are noise. That is the whole test and it is not ambiguous —
**about the file.**

So the stored format is: **a 12-byte header, then 410 × 502 RGB565
little-endian, no compression, no palette, no alpha.**

**And that is a fact about a file, not about a panel — §4.1a.** Until 2026-08-23
this section continued into §4.2 with *"what it does establish beyond argument is
the panel's native pixel format and byte order"*. The pixel *format* half holds.
The byte *order* half was an inference across a boundary the test never crossed,
and it is now known to be the wrong way round on at least one real path.

### 4.1a The panel's transfer byte order is a different fact, and it is UNKNOWN

A host render sees the bytes of a file. It does not run the display driver, and a
driver that byte-swaps on the way out makes **the same stored file correct for
the opposite bus order**. So "little-endian on disk" and "little-endian on the
wire" are two claims, and §4.1 proves only the first.

This is not a theoretical gap. Traced through pinned source on 2026-08-23 —
source **S14**, and the table is in
[VERIFIED_FACTS](VERIFIED_FACTS.md#the-panel-driver-does-not-swap-pixel-bytes--the-layer-above-it-does):

1. `78/xiaozhi-esp32` @ `bb9122ab`, `main/display/lcd_display.cc:160,166` —
   `SpiLcdDisplay`, the class **this board's** file subclasses, configures
   `esp_lvgl_port` with `.color_format = LV_COLOR_FORMAT_RGB565` **and**
   `.flags.swap_bytes = 1`;
2. `espressif/esp-bsp` @ `2f51931`, `esp_lvgl_port_disp.c:739-741` — that flag
   makes the flush callback call `lv_draw_sw_rgb565_swap(color_map, len)`;
3. `lvgl/lvgl` @ `v9.5.0`, `src/draw/sw/lv_draw_sw_utils.c:149-171` — which is a
   plain in-place 16-bit byte swap;
4. `espressif/esp-iot-solution` @ `5d75f3f0`,
   `esp_lcd_sh8601.c:279-280` — `panel_sh8601_draw_bitmap` then hands the buffer
   to `esp_lcd_panel_io_tx_color()` **verbatim**. Nothing below swaps back.
   `esp_lcd_co5300_spi.c:291-292` is the same code.

So on that path the CO5300 receives the **opposite** order to the host-native
`uint16_t`, and the §4.1 inference points the wrong way.

**Two things stay open, and they are not the same one.**

- **What the controller natively expects** is a hardware fact and needs the
  CO5300 datasheet on `3Ah`/`2Ch` bit packing — which nobody has read; that is
  **D7** — or a measurement. What *is* traced is only that
  `bits_per_pixel == 16` writes `COLMOD` (`3Ah`) `= 0x55`
  (`esp_lcd_sh8601.c:86-89`).
- **What produced these particular files** is not the path above. `otadata` is
  blank, so the running app is `phone_s3_box_3` in `factory` (§2.1), Waveshare's
  port of `espressif/esp-brookesia` at `v0.4.2-92-g5c6be6c-dirty` — 92 commits
  past a tag, built from a modified tree, unpublished. `xiaozhi` sits in `ota_0`
  and has never been selected. Its `swap_bytes` is not readable, so whether the
  producer of these bytes swapped is `UNKNOWN`.

Registered as **D21** in [OPEN_QUESTIONS](OPEN_QUESTIONS.md). §7 below gives the
bench step that would close it, and it is `NOT EXECUTED — HARDWARE REQUIRED`.

### 4.2 What it means for T-034

The vendor bakes **full-frame, uncompressed, unconverted pixel buffers** and
ships **no image decoder on the device**. Three full frames cost 1.18 MB of the
6 MB partition.

That corroborates where the asset pipeline was already heading — raw
`RGB565`/`RGB565A8` blobs baked at build time rather than a PNG decoder — and it
is corroboration rather than proof: the vendor had different constraints and only
needed three wallpapers. What it establishes beyond argument is that **16 bits
per pixel is enough for this vendor's own artwork on this panel**, which is a
fact about the format and about their taste both. It does **not** establish the
transfer byte order — §4.1a. Their header is worth *noticing* and not worth
*copying*: it carries width, height and stride but no format field, which is
exactly the field you need the moment a second format exists.

**Nothing T-034 has shipped is affected.** The pipeline emits
`LV_COLOR_FORMAT_A8` masks only (`tools/assets/generate_images.py:168` "--cf",
`--cf A8`) — one byte per pixel, no byte order to get wrong. The cost of §4.1a
lands on **the first line of display bring-up**, which does not exist yet. It
does **not** land on the first colour asset: an asset's byte order follows
LVGL's colour-format contract and the framebuffer the software renderer writes
into, and the wire order is absorbed once, at flush, by the port's `swap_bytes`
flag — which is exactly what §4.1a's own four-step trace shows. An earlier
version of this paragraph named the asset too, and following it was not possible
for `RGB565A8` (the vendored converter has no swapped variant of that format) and
merely pointless for `RGB565` — a pre-swapped source renders correctly, LVGL
un-swapping it while blending into a native framebuffer, and only pays a
conversion the native-order asset does not. An earlier version of this sentence
said *"wrong for `RGB565` in either direction"*, which was over-stated and is
withdrawn; see [VERIFIED_FACTS](VERIFIED_FACTS.md) for the traced version. Found
in review.

### 4.3 The music settles an argument two sections down

The board ships **three MP3 background tracks**, two of them stereo, at 112–128
kbps. They are decoded and played by the factory demo's `MusicPlayer`.

A device that ships 788 kB of licensed music and an app to play it **has a
speaker**. Taken with the grille slot in the case wall and the separate motor
pads at `P1`/`P2`, the reading in §6 that `AAC210602A1` is a *haptic actuator*
becomes very hard to sustain. It is not yet `VERIFIED` — stereo source material
decoded to one transducer is still mono output, and only tracing the pads settles
it — but T-105 now has a strong prior and should be quick.

### 4.4 The dump carries third-party rights, and this is why it stays out

`BGM_1.mp3`'s ID3 frames read, verbatim:

```
All Rights Reserved to www.Art-list.io
Levitate by Ryefield
```

So the factory image contains **commercially licensed music under an
all-rights-reserved grant to a third party**, alongside Waveshare's own
proprietary binary. Keeping the flash dump off the repository had been a
convention and not a written rule — review on
[#80](https://github.com/hleserg/Attadipa/pull/80) checked `CLAUDE.md`,
`docs/research/` and `docs/adr/` and found it stated nowhere, which was correct.
**It is written down now, here and in
[VERIFIED_FACTS](VERIFIED_FACTS.md):** the dump does not go in the repository.
This licence finding is the second and sharper reason for it, on top of
Waveshare's own copyright: republishing that dump would redistribute somebody
else's licensed audio. **The extracted
files are not committed either**, and neither are the rendered PNGs. What is
committed is the extractor and the measurements.

## 5. What this changes, and what it does not

| Claim | Where it stands now |
|---|---|
| PSRAM is quad | **Refuted** — §1. The reading was right, the inference was not |
| Partition table as parsed | **VERIFIED** — §2, parsed from the raw entries |
| `factory` cannot be restored by OTA | **Withdrawn** — the partition is 9 MB, but the measured image is 4.94 MB and fits a 6 MB OTA slot |
| `model` is ESP-SR / xiaozhi | **VERIFIED** — the two model names are in the container |
| `storage` is SPIFFS holding three raw images | **Superseded.** It holds **six** files — three images and three MP3s. The **stored** format is **VERIFIED**: a 12-byte header, then 410 × 502 RGB565 **little-endian on disk**, confirmed by rendering the file. §4.1 |
| Those files prove the panel's native byte order | **Withdrawn 2026-08-23** — §4.1a. A host render never crosses the display driver, and the one path readable in pinned source **swaps every pixel** before transfer. The transfer order is `UNKNOWN`, registered as D21 |
| `AAC210602A1` is a haptic module | **CONFLICTING** — §6 |
| The battery connector is MX1.25 | **LIKELY**, photo-derived — §6 |

## 6. Two rows from the parallel document that are not settled

Recorded as conflicts rather than overwritten in either direction, because both
were read off the same unit and the sources disagree rather than one being newer.

### `AAC210602A1` — speaker or haptic actuator?

The parallel document lists it under **Haptics**, as *"a real driver module, not
a bare transistor-driven pager motor"*. This repository has it as the **speaker**:
a metal-can micro-speaker in the back cover wired to `+`/`−` solder pads
([HARDWARE_MATRIX](HARDWARE_MATRIX.md), [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.8).

Three things weigh against the haptics reading and none of them is conclusive on
its own:

- the case has a **speaker grille slot** in its side wall, directly over that part;
- the schematic has a **separate** vibration-motor path — GPIO 18 → R12 (4.7 kΩ)
  → Q1 (MMBT3904, NPN) → `J1`, from `BLDO2` — so a transistor-driven motor is
  exactly what this board has, and `J1` is bare on this unit;
- the factory demo ships `MusicPlayer` and `SpecAnalyzer`, which need a speaker.

AAC Technologies makes both speakers and linear actuators, so the marking alone
does not decide it. **Resolving test:** trace the pads — a speaker sits behind
the ES8311/amplifier output, a haptic actuator does not. `UNKNOWN` until traced.

### The battery connector's pitch

The parallel document says *"white JST-1.25 (MX1.25)"*, sourced to a photograph.
Pitch is not something a photograph without a scale reference establishes, and
this matters because it decides what plugs in. Recorded as **LIKELY**.
**Resolving measurement:** callipers across the two pin centres — 1.25 mm for
MX/PicoBlade, 1.0 mm for JST SH, 1.5 mm for ZH. It is one measurement and it is
on the list in [#64](https://github.com/hleserg/Attadipa/issues/64).

## 7. Still open

- ~~Confirm the image format~~ — **done**, §4. `tools/flash/spiffs_extract.py` did it without mkspiffs.
- **Settle the panel's transfer byte order — D21, §4.1a.** `NOT EXECUTED —
  HARDWARE REQUIRED`. Two routes, and the cheap one is not the conclusive one:

  1. **Read the CO5300 datasheet** on `3Ah` (`COLMOD`) and `2Ch` (`RAMWR`) 16-bit
     bit packing — which byte of the pair carries `R[4:0]`. That answers the
     controller half without a board and also closes half of **D7**. The
     datasheet is not published anywhere reachable so far; asking Waveshare or
     Chipone for it is the action.
  2. **Measure it, which is the answer that counts.** On the bench unit, from a
     `PURE_RAM_APP` (the route S13 established: it writes nothing to flash),
     write a **known asymmetric pattern** — pure red `0xF800` and pure blue
     `0x001F` in adjacent halves — straight to `RAMWR` with the swap **off**, and
     photograph the panel. Red rendering as blue-ish green means the wire wanted
     the other order. Record firmware revision, the exact `esp_lcd_sh8601`
     version, `COLMOD`, `MADCTL`, the pattern and the observed result, and label
     it `MEASURED`. Anything short of a photograph of that pattern is not an
     answer to this question.

  Until one of those runs, the first line of display bring-up must treat the
  swap as a **configurable whose correct value is `UNKNOWN`** — not as a
  constant read off §4.1. A boolean has no unknown default, and an earlier
  version of this sentence said *"a configurable with an `UNKNOWN` default"*,
  which leaves the next person to invent one. **Start it at `swap_bytes = true`
  and say why in the code**: that is the setting on the one complete path
  readable in pinned source (§4.1a's four-step trace), so it is the value with
  evidence behind it rather than the value that reads as neutral. **That reason
  stands alone, and an earlier version of this sentence propped it up with a
  second one that is not true**: it claimed being wrong this way is *loud* while
  starting from `false` on a panel that wants the swap is *"the same wrongness
  with a plausible-looking screen in front of it"*. Both directions put
  byte-swapped RGB565 on the wire — `0xF800` arrives as `0x00F8` either way, a
  saturated blue with a trace of green — so there is no direction in which the
  mistake is quieter. Withdrawn in the third review round of
  [#152](https://github.com/hleserg/Attadipa/pull/152), in a paragraph that has
  just finished saying anything short of a photograph of that pattern is not an
  answer to this question. And
  *configurable* here means a **board fact**, so it lives in
  `boards/`/`platform/` rather than in settings or a build flag. It is not the
  first colour asset's question at all: see §4.2. Found in review.
- **Check `xiaozhi-esp32`'s licence** before reading it for the audio path, and
  record the decision in the ledger either way.
- ~~**Run the `octal_psram` boot-log check.**~~ **Done** — §1 and
  [BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §4. Attadipa's flash
  boot measured the octal 8 MB path and SRAM test on the unit.
- ~~**Verify the dump.**~~ **Done** — §2.2. Two further complete passes, compared
  per chunk against the owner's, all three identical, and the device's own MD5
  agrees. The five stub-failure addresses are **not** marginal sectors: the same
  stub computed a correct MD5 over every one of them, so the fault is in the
  transfer path, not the flash. T-099 is `DONE`.
