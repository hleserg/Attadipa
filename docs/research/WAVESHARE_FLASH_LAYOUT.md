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

**The decisive empirical check, which nobody has run yet.** Attach a serial
monitor at 115 200 and reboot the stock firmware. ESP-IDF logs the octal path
under its own `octal_psram` tag (`octal_psram: vendor id : 0x0d (AP)`, followed
by `esp_psram: Found 8MB PSRAM device`). A quad part logs no such line. That is
one reboot and it settles it against the silicon rather than against a table.
Until somebody runs it, D12a stands as **VERIFIED by datasheet and corroborated
by eFuse** — see [VERIFIED_FACTS](VERIFIED_FACTS.md) — and the quad reading is
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
| `factory` | app | factory | `0x100000` | **9 MB** | the stock firmware |
| `ota_0` | app | ota_0 | `0xa00000` | 6 MB | OTA slot A |
| `ota_1` | app | ota_1 | `0x1000000` | 6 MB | OTA slot B, unwritten at dump time |
| `storage` | data | spiffs | `0x1600000` | 6 MB | UI assets — §4 |

**28 of 32 MB is partitioned. `0x1C00000`–`0x2000000` is genuinely spare.**

Two things in that table are worth noticing rather than copying:

- **`factory` is 9 MB and the OTA slots are 6 MB.** The factory image cannot be
  put back through OTA — it is larger than either slot. Whatever the vendor's
  reason, it means their update path can never restore the shipped build, which
  is another argument for taking the backup seriously (T-099).
- **A `0xFF` region is not an empty region.** The run of `0xFF` at `0x1000000` is
  the *unwritten `ota_1` slot*, not unused flash. Reading a dump and concluding
  "the second half is empty" is a mistake this board invites; the parallel
  document caught and corrected it in itself, and it is recorded here so the next
  reader does not make it a third time.

Our own layout is not decided and this table does not decide it — 32 MB is
roomy enough that dual 6 MB slots plus assets is comfortable, which is a *useful*
fact for whoever writes ours, not a template to copy.

## 3. `model` — the stock firmware is a Xiao Zhi voice terminal

The `model` partition is not a filesystem. It is esp-sr's `srmodels` container —
a little-endian count followed by 32-byte name records. Two entries:

- `wn9_nihaoxiaozhi_tts` — WakeNet9, wake word 你好小智 / *"Ni hao Xiao Zhi"*,
  with TTS;
- `wn9_data` — the wake-word model data.

So the launcher's **AIChats** app is [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32):
the wake word is detected on-device by WakeNet and the conversation goes to a
cloud service. That identification is worth more than the blob it came from —
see the [reuse ledger](REUSE_LEDGER.md) entry, because `xiaozhi-esp32` is an
open-source firmware that **targets this exact board** and therefore contains
this board's audio path written out: the I2S wiring, the ES8311 codec bring-up
and how the two microphones are used. Reading it is cheaper and more reliable
than reversing the `factory` app, and the licence has to be checked before
anything is taken from it.

Nothing here is a decision to ship a wake word. This repository has no such
requirement and adding one would be a product change.

## 4. `storage` — extracted, and the format is settled

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

**The byte order is little-endian, and this was settled by rendering rather than
by argument.** Decoded as little-endian RGB565 the files are coherent artwork —
a neon figure, a bird over a synthwave skyline, a third scene. Decoded
big-endian they are noise. That is the whole test and it is not ambiguous.

So the format is: **a 12-byte header, then 410 × 502 RGB565 little-endian, no
compression, no palette, no alpha.**

### 4.2 What it means for T-034

The vendor bakes **full-frame, uncompressed, panel-native pixel buffers** and
ships **no image decoder on the device**. Three full frames cost 1.18 MB of the
6 MB partition.

That corroborates where the asset pipeline was already heading — raw
`RGB565`/`RGB565A8` blobs baked at build time rather than a PNG decoder — and it
is corroboration rather than proof: the vendor had different constraints and only
needed three wallpapers. What it does establish beyond argument is the panel's
native pixel format and byte order, which is a fact about the hardware and not
about their taste. Their header is worth *noticing* and not worth *copying*: it
carries width, height and stride but no format field, which is exactly the field
you need the moment a second format exists.

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
| `factory` cannot be restored by OTA | **VERIFIED** — 9 MB image, 6 MB slots |
| `model` is ESP-SR / xiaozhi | **VERIFIED** — the two model names are in the container |
| `storage` is SPIFFS holding three raw images | **Superseded.** It holds **six** files — three images and three MP3s. The image format is now **VERIFIED**: a 12-byte header, then 410 × 502 RGB565 **little-endian**, confirmed by rendering. §4 |
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
- **Check `xiaozhi-esp32`'s licence** before reading it for the audio path, and
  record the decision in the ledger either way.
- **Run the `octal_psram` boot-log check** in §1 and close D12a against silicon
  rather than against a table.
- **Verify the dump.** A second pass over the *populated* ranges with `--no-stub`,
  compared per chunk against the first, would also test whether the five
  stub-failure addresses (`0x23d000`, `0x476000`, `0xbef000`, `0xdcc000`,
  `0xe61000`) are marginal sectors on the GD25Q256. If two passes disagree,
  **record it as a conflict — do not "fix" it.** T-099.
