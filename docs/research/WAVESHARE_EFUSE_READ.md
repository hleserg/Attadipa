# Waveshare `ESP32-S3-Touch-AMOLED-2.06` — what the silicon says about itself

Status: **read off a physical unit.** Source key **S10** in
[HARDWARE_MATRIX](HARDWARE_MATRIX.md): `espefuse v5.3.1 summary` and
`esptool v5.3.1 flash-id`, run by the owner over the board's own USB-Serial/JTAG
port on 2026-08-22, on the unit received the same day and photographed in
[WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md).

This is the first evidence in this repository that did not come from a datasheet,
a schematic or a photograph. It is the chip answering for itself.

## 0. What was **not** copied here, and why

The `espefuse summary` output contains two values that identify **this specific
unit** and must never reach a public repository:

- `MAC (BLOCK1)` — the factory MAC address;
- `OPTIONAL_UNIQUE_ID (BLOCK2)` — the 128-bit die identifier.

Both are redacted throughout. Nothing below depends on either. The same rule that
keeps the owner's GPS coordinates out of this repository applies to them: a
device identifier is not a hardware fact about the *board*, it is a fact about
*one board and the person holding it*.

Everything else in the summary was at its factory default, so there is nothing
else worth withholding.

## 1. Reading the eFuses — no burning, only reading

`espefuse summary` reads. It burns nothing. This is explicitly permitted by
[CLAUDE.md](../../CLAUDE.md); `espefuse burn_efuse` is not, and was not run.

### 1.1 The chip

| Field | Value | What it settles |
|---|---|---|
| Chip type | `ESP32-S3 (QFN56)` | QFN56 is the in-package-memory package |
| Revision | `v0.2` — `WAFER_VERSION_MAJOR=0`, `WAFER_VERSION_MINOR=2` | see §3.1 — a build configured for a minimum revision above 0 will refuse this chip |
| Crystal | 40 MHz | matches every ESP32-S3 reference design; nothing to configure |
| Cores | `DIS_APP_CPU = False` | dual core, both usable |
| TWAI/CAN | `DIS_TWAI = False` | present, unused |
| USB mode | `USB-Serial/JTAG` | the port used for all of this |

### 1.2 PSRAM — D12a stops being an inference

The repository already carried D12a as **RESOLVED** by reasoning: the marking
`ESP32-S3R8` appears in ESP32-S3 Series Datasheet v2.2 Table 1-1 as
`8 MB (Octal SPI)`, and that table contains no 8 MB *quad* in-package part. The
eFuses now supply the missing half — the marking is not merely what is printed on
the lid, it is what the die was fused as:

| eFuse | Value |
|---|---|
| `PSRAM_CAP` (BLOCK1) | `8M` |
| `PSRAM_CAP_3` | `False` |
| `PSRAM_CAPACITY` (calculated) | `0b001` |
| `PSRAM_VENDOR` | `AP_3v3` — AP Memory, 3.3 V |
| `PSRAM_TEMP` | `85C` |

`esptool` renders the same fuses as `Embedded PSRAM 8MB (AP_3v3)`.

**Be precise about what this proves.** The eFuse states *capacity* and *vendor
and rail*, not *bus width*. The step from "8 MB in package" to "octal" is still
the datasheet table's, and it is a sound step because no 8 MB quad in-package
part exists. What the eFuse adds is that the capacity and the 3.3 V rail are
real on this die, so the part is `ESP32-S3R8` and not `ESP32-S3R8V` (1.8 V) or
any 2 MB quad variant. D12a is now VERIFIED on both legs.

`PIN_POWER_SELECTION = VDD_SPI` corroborates it from the other side: GPIO33–37
are fused to the flash/PSRAM rail, which is exactly where octal PSRAM's DQ4–DQ7
and DQS live. **Those five pins are not available to any application.**

### 1.3 Flash — external, GigaDevice, 32 MB, quad, 3.3 V

`esptool flash-id` read the JEDEC ID directly off the part:

| Field | Value | Decode |
|---|---|---|
| Manufacturer | `0xC8` | GigaDevice |
| Device | `0x4019` | `0x40` = GD25Q SPI family, `0x19` = 2^25 bytes |
| Detected size | 32 MB | agrees with the schematic's `GD25Q256EYIGR` |

And the fuses agree that it is *outside* the package and how it is driven:

| eFuse | Value | What it settles |
|---|---|---|
| `FLASH_CAP` / `FLASH_TEMP` / `FLASH_VENDOR` (BLOCK1) | all `None` | **no in-package flash.** These fields describe in-package flash and are unprogrammed |
| `FLASH_TYPE` (BLOCK0) | `4 data lines` | quad, not octal |
| `VDD_SPI_FORCE` / `VDD_SPI_XPD` | `True` / `True` | the rail is set by eFuse, not by strapping |
| `VDD_SPI_TIEH` | `VDD_SPI connects to VDD3P3_RTC_IO` | 3.3 V |
| `FLASH_ECC_EN` | `False` | flash ECC off |

So the board is the combination the repository already described and now has
proof of: **octal PSRAM in package, quad flash outside it, both at 3.3 V.**

### 1.4 ADC and temperature calibration are burned

`BLK_VERSION_MAJOR = ADC calib V1`, with `ADC1_INIT_CODE_ATTEN0..3`,
`ADC1_CAL_VOL_ATTEN0..3`, their ADC2 counterparts and `TEMP_CALIB = -10.7` all
programmed. ESP-IDF's calibration layer will therefore work rather than falling
back to a nominal curve. This matters to anything that measures a voltage on this
board that does not come from the AXP2101 over I2C.

### 1.5 Security posture: entirely virgin

Recorded because it is the state the "never irreversible without being asked"
rule exists to preserve, and because a future agent must be able to tell whether
anything was ever burned:

| Fuse | Value |
|---|---|
| `WR_DIS` | `0` — nothing write-protected |
| `RD_DIS` | `0` — nothing read-protected |
| `SPI_BOOT_CRYPT_CNT` | `Disable` — flash encryption off |
| `SECURE_BOOT_EN` | `False` |
| `SECURE_BOOT_KEY_REVOKE0..2` | `False` |
| `KEY_PURPOSE_0..5` | all `USER` |
| `BLOCK_KEY0..5` | all zero |
| `DIS_DOWNLOAD_MODE` | `False` |
| `ENABLE_SECURITY_DOWNLOAD` | `False` |
| `DIS_PAD_JTAG` / `SOFT_DIS_JTAG` | `False` / `0` |
| `DIS_USB_SERIAL_JTAG` | `False` |
| `CUSTOM_MAC` | all zero |
| `SECURE_VERSION` | `0` |

Nothing has been burned. Every recovery path — download mode, USB-Serial/JTAG,
pad JTAG — is open. Keep it that way until there is a decision to do otherwise,
and that decision is the owner's.

## 2. Backing up the factory image — and the trap in doing it

[WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §4 says to back the
factory image up before flashing anything. Doing it on this unit surfaced a
failure mode worth recording, because the obvious response to it is wrong.

### 2.1 The symptom

`esptool read-flash` **with the stub loader** aborts with

```
A fatal error occurred: Packet content transfer stopped
```

part-way through, at 767–830 kbit/s, over USB-Serial/JTAG on Windows.

### 2.2 It is not random, and this is the whole point

The abort addresses across nine runs:

| Read started at | Aborted at | Bytes in |
|---|---|---|
| `0x0000000` | `0x023d000` | 2 347 008 |
| `0x0200000` | `0x023d000` | 249 856 |
| `0x0400000` | `0x0476000` | 483 328 (×3) |
| `0x0800000` | `0x0bef000` | 4 124 672 |
| `0x0a00000` | `0x0bef000` | 2 027 520 (×3) |
| `0x0c00000` | `0x0dcc000` | 1 884 160 (×3) |

Two reads that began at **different offsets** stopped at the **same absolute
flash address** — twice over, at `0x023d000` and at `0x0bef000`. A read that
began at `0x0200000` had been running 2.6 s when it died; one that began at
`0x0000000` had been running 24 s and died at the same place. So the failure
tracks **flash content, not elapsed time and not the USB link's mood.**

Two consequences:

- **Retrying with the stub is a random walk with a budget attached.** The
  observed runs retried three times and failed three times, identically, because
  a deterministic failure is deterministic. Any retry loop must change *method*,
  not repeat one.
- **USB selective suspend is not the cause.** Chasing Windows power settings for
  this is wasted time; a power-management dropout would not land on the same
  flash address from two different starting offsets.

### 2.3 What works

`--no-stub` completed every range that the stub refused, at 116–133 kbit/s
(≈2 min per 2 MB, so ≈35 min for the whole 32 MB):

```
esptool --port COM12 --no-stub read-flash 0x400000 0x200000 chunk_0400000.bin
```

Above 16 MB the ROM loader warns that large flash is "not fully supported" —
those ranges read cleanly **with** the stub on this unit, so the working recipe
is stub first, `--no-stub` on failure, and no third attempt at either.

### 2.4 The trap: a concatenated dump of failed chunks is silently wrong

`esptool` writes the output file **incrementally**, so a run that aborts still
leaves a short file behind. Concatenating those short files produces an image
that is the right kind of thing and the wrong size, with everything after the
first failure shifted. It will not announce itself.

**Check the length before trusting any assembled image.** A full dump of this
board is exactly `33 554 432` bytes; each 2 MB chunk is exactly `2 097 152`.
Any chunk that is not exactly its nominal size is a failed read, not a short one.

Verify the assembled image against the device before relying on it —
`esptool verify-flash 0x0 <image>` compares by on-chip MD5 over the range rather
than streaming the data back, so it costs seconds, not half an hour.

## 3. What changes in the record

| Where | Change |
|---|---|
| [OPEN_QUESTIONS](OPEN_QUESTIONS.md) D12a | was RESOLVED by datasheet inference; now VERIFIED on silicon — see §1.2, including the limit of what the eFuse itself proves |
| [OPEN_QUESTIONS](OPEN_QUESTIONS.md) D1 | flash size and vendor confirmed by JEDEC read, not only by schematic |
| [HARDWARE_MATRIX](HARDWARE_MATRIX.md) | Waveshare SoC, PSRAM and flash rows gain source **S10**; flash row gains "quad and 3.3 V per eFuse" |
| [VERIFIED_FACTS](VERIFIED_FACTS.md) | new entries for §1.2, §1.3 and §1.5 |
| GPIO budget | GPIO33–37 are fused to `VDD_SPI` and are **not** available — this is now proven on the unit, not argued from the schematic |

### 3.1 One firmware consequence, and it is not cosmetic

The chip is **revision v0.2**. ESP-IDF's `CONFIG_ESP32S3_REV_MIN_*` sets the
minimum revision an image will run on, and the bootloader **refuses to boot** an
image whose minimum is above the chip's actual revision. Any build for this board
must keep the minimum at revision 0. Nothing in the repository sets it higher
today; this is recorded so that nobody raises it without knowing what it costs.

The revision is also the input to the errata list in the ESP32-S3 Errata sheet.
**That sheet has now been read against v0.2** — v1.3, md5
`64ffc580e78b5ab3c6c5d990e0500e38`, on 2026-08-22 — and the answer is that all
eight of its errata apply, seven of them permanently
([ESP32S3_ERRATA_V02](ESP32S3_ERRATA_V02.md)). D18 in
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) is resolved.

## 4. Still open

- ~~**Which ESP32-S3 errata apply to revision v0.2**~~ — **answered 2026-08-22**:
  all eight, seven of them with no fix scheduled, and one of them (CACHE-126)
  does touch octal PSRAM. Nothing touches USB-Serial/JTAG or the flash interface.
  [ESP32S3_ERRATA_V02](ESP32S3_ERRATA_V02.md). What the CACHE-126 workaround
  costs in interrupt latency is the residue and stays `UNKNOWN`, `NOT MEASURED`.
- **Why the stub read fails on specific flash content.** Recorded as an
  observation with a working remedy; the mechanism is `UNKNOWN` and does not need
  to be known to take a backup.
- **Whether the factory image is worth keeping beyond the backup** — the demo's
  capability inventory is already summarised in
  [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.9.
