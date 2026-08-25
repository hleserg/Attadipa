# Above 16 MB: the boot path was measured, the four an application uses were not, and only one of those four refuses

> **Status:** a **source trace** of ESP-IDF at tag `v5.5.5`, read line by line,
> plus the measurements already on record from the bench session of 2026-08-23
> and the eFuse/JEDEC session of 2026-08-22. **Nothing here was executed on a
> board.** No new hardware claim is made and no old one is upgraded. Every
> runtime row below ends `UNKNOWN` because a code path that looks right and a
> code path that has been run are different sentences — which is the whole
> reason this document exists.
>
> Written for [#132](https://github.com/hleserg/Attadipa/issues/132), which is
> right that the repository had drawn a design rule for an application's four
> flash access paths out of a measurement of the bootloader's one.

## 0. The answer, before the evidence

The Waveshare unit's 32 MB part is addressed with 24 bits by the ROM and by the
vendor's second-stage bootloader, so `0x1000000` reads as `0x0`. **That is
measured**, on this unit, in
[WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §1.

What a *running application* does at those addresses was never measured, and
this repository has twice written as if it had been. Reading ESP-IDF v5.5.5
gives three answers and none of them is the one that was assumed:

- **`esp_partition_mmap` refuses**, cleanly, with `ESP_ERR_INVALID_ARG` — and it
  has only done so **since v5.5.5**. Every earlier release in the 5.x line,
  including the v5.5.1 the vendor's own firmware is built with, maps it and says
  nothing.
- **`esp_partition_read`, `_write` and `_erase_range` do not refuse.** They emit
  four-byte-address SPI commands and expect the die to understand them.
- **Nothing upstream will stop them on this board.** ESP-IDF gates 32 MB access
  on the flash's JEDEC ID, and this part's — `0xC8 0x4019`, read off the unit —
  passes. So an application here may call `esp_partition_erase_range` at
  `0x1600000` and get no warning, no error and no check. Where the sector
  actually gets erased has never been observed.

So: **nothing of ours goes at or above `0x1000000` until somebody measures it.**
That rule is enforced by
[`tools/flash/partition_check.py`](../../tools/flash/partition_check.py) rather
than by remembering, and §6 is the plan that would lift it.

## 1. What was measured, and it is one path out of six

From [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §1.2–1.3, on
the received `ESP32-S3-Touch-AMOLED-2.06`:

| Observation | What it establishes |
|---|---|
| The bootloader read `paddr=0x01000020` and got the segment header that lives at flash `0x0` — `vaddr=0x3fce2820, size=0x1700`, matching the bootloader's own image to the bit | **The second-stage bootloader's flash read wraps at 16 MB on this unit.** |
| `esptool --no-stub read-flash 0x1000000` → *"Can't access flash regions larger than 16MB"* | The **ROM** loader refuses the range outright. |
| `esptool write-flash 0x1000000` → `Hash of data verified`, and `verify-flash 0x0` over all 33 554 432 bytes → `Verification successful` | The **stub** flasher reads and writes above the line correctly. |

That is three paths. Two of them are the host's and one is the bootloader's.
**None of them is the application's**, and the application has four of its own.

## 2. What the stub proves, and where it stops

The stub flasher is a program esptool uploads into **this SoC's** RAM and runs
there, driving the flash through the chip's own SPI peripheral with its own
routines. So its success above 16 MB is better evidence than a host tool's would
be: **the die and the SPI peripheral together honour four-byte-address commands
on this unit.** That is worth stating plainly, because it means the residual
risk in §4.2 is narrower than "nobody knows if the part can do it".

What the stub does not exercise is everything between an application and that
peripheral: ESP-IDF's `spi_flash` driver, its chip-driver dispatch, the cache
that is live while an application runs and suspended while the stub runs, and
the MMU. The bench session already contains the proof that layers above the die
can disagree with it — the stub wrote `0x1000000` perfectly and the vendor's
bootloader then read `0x0` from the same address. One address, one die, two
pieces of software, two answers. That is the shape of this whole problem.

## 3. Every path, and where it stands

ESP-IDF `v5.5.5`. Line numbers are that tag's.

| # | Path | Behaviour at ≥ `0x1000000` | Source | Status |
|---|---|---|---|---|
| 1 | Second-stage bootloader image read | **wraps** to `addr & 0xFFFFFF` | — | **MEASURED on this unit** |
| 2 | esptool ROM loader | refuses | — | **MEASURED** |
| 3 | esptool stub flasher | works | — | **MEASURED** |
| 4 | `esp_partition_mmap` → `spi_flash_mmap` | **returns `ESP_ERR_INVALID_ARG`** unless `BOOTLOADER_CACHE_32BIT_ADDR_{QUAD,OCTAL}_FLASH` | [`flash_mmap.c:56,67-73`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/flash_mmap.c#L56-L73) | **VERIFIED by source** — fails closed. **Not executed.** |
| 5 | `esp_partition_read` → `esp_flash_read` → `spi_flash_chip_winbond_read` | issues a four-byte-address read command for the whole transfer when the region ends above the line | [`spi_flash_chip_winbond.c:15,42-55`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/spi_flash_chip_winbond.c#L42-L55) · [`spi_flash_chip_generic.c:484-540`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/spi_flash_chip_generic.c#L484-L540) | source says it intends to work · **UNKNOWN on this die** |
| 6 | `esp_partition_write` → `esp_flash_write` → `..._page_program` | `CMD_PROGRAM_PAGE_4B` when the page's start address is above the line | [`spi_flash_chip_winbond.c:247-258`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/spi_flash_chip_winbond.c#L247-L258) | source says it intends to work · **UNKNOWN** |
| 7 | `esp_partition_erase_range` → `esp_flash_erase_region` → `..._erase_sector` / `..._erase_block` | `CMD_SECTOR_ERASE_4B` / `CMD_LARGE_BLOCK_ERASE_4B` when the sector's start address is above the line | [`spi_flash_chip_winbond.c:261-282`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/spi_flash_chip_winbond.c#L261-L282) | source says it intends to work · **UNKNOWN** |
| 8 | The capability gate the above depends on | 32 MB access is advertised **iff** `(chip_id & 0xFF) >= 0x19`; this part reports `0xC8 0x4019`, so it **passes** | [`spi_flash_chip_gd.c:30-37`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/spi_flash_chip_gd.c#L30-L37) · ID from S10 in [HARDWARE_MATRIX](HARDWARE_MATRIX.md) | **MEASURED (the ID) + VERIFIED (the gate)** — the gate does not stop us, §4.3 |
| 9 | What would happen if that gate said no | one warning, then nothing | [`esp_flash_api.c:439-447`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/esp_flash_api.c#L439-L447) | **VERIFIED by source — it is not a guard**, §4.2 |
| 10 | The range check rows 5–7 actually apply | `address > chip->size`, where `chip->size` is the **image header's** flash size | [`esp_flash_api.c:680,947,1118`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/esp_flash_api.c#L943-L950) · [`esp_flash_spi_init.c:596`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/esp_flash_spi_init.c#L582-L596) | **VERIFIED by source** |
| 11 | ESP-IDF's own per-SoC refusal of 32-bit addressing | ESP32-S3 is **not** on it — only C6, H2 and P4 v0.0 | [`flash_ops.c:308-324`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/flash_ops.c#L307-L324) | **VERIFIED by source** |

Rows 1–3 are hardware facts about this unit. Rows 4–11 are facts about a
program. **No row 4–7 has ever run on a board**, and that is the finding.

## 4. Four things the source says, and each of them changes what we may build

### 4.1 `mmap` refuses — and only since `v5.5.5`

```c
/* components/spi_flash/flash_mmap.c */
#define FLASH_MMAP_ADDR_24BIT_MAX  (BIT(24))                              /* 56 */

esp_err_t spi_flash_mmap(size_t src_addr, size_t size, ...)               /* 64 */
{
#if !CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH && !CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_OCTAL_FLASH
    if (src_addr >= FLASH_MMAP_ADDR_24BIT_MAX || size > FLASH_MMAP_ADDR_24BIT_MAX
        || src_addr > FLASH_MMAP_ADDR_24BIT_MAX - size) {                 /* 68 */
        ESP_LOGE("flash_mmap", "Address 0x%08x is out of range for 24bit flash mapping, ...");
        return ESP_ERR_INVALID_ARG;
    }
#endif
```

This is the good news and it is narrower than it sounds. Three things follow:

- **A crossing region is refused as a whole**, not clipped — the third clause
  catches a mapping that starts below the line and ends above it.
- **`esp_partition_mmap` inherits it**, because it is a thin wrapper that rounds
  the physical address down to an MMU page and calls straight through
  ([`partition_target.c:154-178`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_partition/partition_target.c#L154-L178)).
  So issue [#127](https://github.com/hleserg/Attadipa/issues/127)'s `models`
  partition would fail loudly rather than read the wrong bytes — **on v5.5.5.**
- **On v5.5.4 and everything before it, there is no check at all.** The
  function's body begins at the `heap_caps_calloc`; the constant does not exist
  in the file. Checked at `v5.5.1`, `v5.5.2`, `v5.5.3`, `v5.5.4`, `v5.4.2` and
  `v5.3.3` — none contains `FLASH_MMAP_ADDR_24BIT_MAX`; `v5.5.5` contains it
  three times.

  **The vendor's shipped firmware is built with `v5.5.1`**
  ([WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §2.1 reads `idf_ver:
  v5.5.1-dirty` out of both app slots), so the vendor's build is one of the ones
  with no guard. And its own table puts `storage` at `0x1600000`.

  This makes **the IDF version a load-bearing safety property rather than a
  preference.** Anything below v5.5.5 removes the only fail-closed path there is,
  and nothing in the ecosystem stops you landing there: the Waveshare BSP v2.0.0
  declares `idf >= 5.3`, which permits every unguarded release, and the build
  their own firmware was made with is one of them.
  [DEPENDENCIES](DEPENDENCIES.md) records the version question; this is a reason
  for a floor that was not there before.

### 4.2 Read, write and erase do not refuse

`chip->size` is what the range checks in `esp_flash_read`
(`esp_flash_api.c:947`), `esp_flash_write` (`:1118`) and `esp_flash_erase_region`
(`:680`) compare against, and it is set from the ROM's flash size — which is the
size in the binary image header, i.e. `CONFIG_ESPTOOLPY_FLASHSIZE`:

```c
/* components/spi_flash/esp_flash_spi_init.c, 596 */
    // Set chip->size equal to ROM flash size(also equal to the size in binary image header) ...
    default_chip.size = legacy_chip->chip_size;
```

So on a build declaring the 32 MB part this board has, all three accept any
address below 32 MB. There is no second check anywhere between
`esp_partition_erase_range` and the SPI transaction.

What actually happens at the wire is the chip driver's decision. The part is
GigaDevice and its JEDEC ID selects the `gd` driver (§4.3), which forwards read,
program and erase to the Winbond implementations verbatim:

```c
/* components/spi_flash/spi_flash_chip_gd.c, 25-28 */
#define spi_flash_chip_gd_read          spi_flash_chip_winbond_read
#define spi_flash_chip_gd_page_program  spi_flash_chip_winbond_page_program
#define spi_flash_chip_gd_erase_sector  spi_flash_chip_winbond_erase_sector
#define spi_flash_chip_gd_erase_block   spi_flash_chip_winbond_erase_block
```

which switch on the address and emit the four-byte-address opcode:

```c
/* components/spi_flash/spi_flash_chip_winbond.c, 261-269 */
esp_err_t spi_flash_command_winbond_erase_sector_4B(esp_flash_t *chip, uint32_t start_address)
{
    bool addr_4b = ADDR_32BIT(start_address);          /* addr >= (1<<24) */
    spi_flash_trans_t t = {
        .command        = (addr_4b ? CMD_SECTOR_ERASE_4B : CMD_SECTOR_ERASE),
        .address_bitlen = (addr_4b ? 32 : 24),
        .address        = start_address,
```

Two details of that worth keeping:

- **Read decides per *region*, program and erase decide per *command*.** The read
  path sets four-byte addressing once for the whole transfer if the region
  *ends* above the line (`REGION_32BIT`), so a read crossing the boundary uses
  4-byte addressing throughout — which is correct, since 4-byte addressing works
  below the line too. Program and erase are sliced into page- and sector-sized
  commands upstream and each one decides for itself on its own start address.
  Both are coherent; neither has been executed here.
- **A wrong answer here is silent in the worst direction.** If the running system
  does not deliver `0x21`/`0x12`/`0xDC` with the address it intended, the command
  is not rejected — it is interpreted, with whatever address arrives. The
  bootloader already showed that failure mode on this unit: an address that
  wrapped and an operation that reported success. For a read that is wrong data;
  for an erase or a program it is **the low half of the flash being written**,
  which is where the bootloader, the partition table and every app image live.

**And the fallback would not have helped.** `esp_flash_init_main` asks the chip
driver whether the part supports more than 16 MB, and if the answer were no:

```c
/* components/spi_flash/esp_flash_api.c, 439-447 */
    uint32_t size;                                                    /* 432 */
    err = esp_flash_get_physical_size(chip, &size);
    ...
        if (((chip->chip_drv->get_chip_caps(chip) & SPI_FLASH_CHIP_CAP_32MB_SUPPORT) == 0)
            && (size > (16 * 1024 * 1024))) {
            ESP_EARLY_LOGW(TAG, "Detected flash size > 16 MB, but access beyond "
                                "16 MB is not supported for this flash model yet.");
            size = (16 * 1024 * 1024);                                /* 445 */
        }
```

**`size` is a local, and after line 445 nothing reads it.** `chip->size` is
assigned separately and afterwards, at `esp_flash_spi_init.c:596` above, and it
is `chip->size` that the range checks consult. So even on a part whose driver
refused the capability, the warning would be printed once at boot into a log
nobody is reading, and the API would then behave as though it had never been
printed. On *this* board the branch is not taken at all — §4.3 — so the
observation is a note about ESP-IDF rather than about us. It is here because it
is the sort of thing that reads like a safety net until somebody checks.

**None of this refutes
[#132](https://github.com/hleserg/Attadipa/issues/132); it confirms it.** There
is no guard on the destructive paths.

### 4.3 The capability gate passes, and that is the uncomfortable answer

```c
/* components/spi_flash/spi_flash_chip_gd.c, 30-37 */
spi_flash_caps_t spi_flash_chip_gd_get_caps(esp_flash_t *chip)
{
    spi_flash_caps_t caps_flags = 0;
    // 32M-bits address support
    if ((chip->chip_id & 0xFF) >= 0x19) {
        caps_flags |= SPI_FLASH_CHIP_CAP_32MB_SUPPORT;
    }
```

`chip_id` is the JEDEC ID, and this board's has been read off the silicon:
**`0xC8 0x4019`**, by `esptool flash-id` on 2026-08-22 — source **S10** in
[HARDWARE_MATRIX](HARDWARE_MATRIX.md), the same session that read the eFuses.
Against the `gd` driver's arithmetic:

| Field | Value | Consequence |
|---|---|---|
| `flash_id >> 16` | `0xC8` | matches `MFG_ID`, so `spi_flash_chip_gd_probe` claims the part and the `gd` driver is the one installed |
| `flash_id & 0xFF00` | `0x4000` | `GD25Q_PRODUCT_ID` — accepted |
| `flash_id & 0xFF` | `0x19` | `>= 0x19`, so **`SPI_FLASH_CHIP_CAP_32MB_SUPPORT` is granted** |
| `detect_size` = `1 << 0x19` | `0x2000000` | 32 MiB, agreeing with everything else on record |

So the answer is not the reassuring one. **ESP-IDF believes this part can be
addressed above 16 MB, and behaves accordingly**: the warning in §4.2 never
fires, `chip->size` is the full 32 MB, the `gd` driver is installed with its
four-byte-address read, program and erase, and every guard that might have
existed has been passed on the way in.

The one thing left between an application and the low half of the flash is
whether the four-byte-address command reaches the die intact from the running
system. The stub says the die and the peripheral can do it (§2). Nobody has run
it from ESP-IDF, with the cache live, on this board.

### 4.4 ESP-IDF will not stop us on this SoC

```c
/* components/spi_flash/flash_ops.c, 308-324 */
esp_err_t IRAM_ATTR esp_mspi_32bit_address_flash_feature_check(void)
{
#if CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
    ESP_EARLY_LOGE(TAG, "32bit address (flash over 16MB) has high risk on this chip");
    return ESP_ERR_NOT_SUPPORTED;
#elif CONFIG_IDF_TARGET_ESP32P4
    ...
```

`esp_flash_init_default_chip` calls this whenever the part is larger than 16 MB
and aborts initialisation if it fails
([`esp_flash_spi_init.c:590-593`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/spi_flash/esp_flash_spi_init.c#L590-L596)).
On the ESP32-S3 it returns `ESP_OK`. Espressif has a list of SoCs on which they
consider 32-bit flash addressing to have *"high risk"*, and ours is not on it —
which is an argument that the paths are expected to work, and is not a
measurement that they do.

The escape hatch for the **cache** side remains what
[WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §1.4 already
recorded: `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH`, `default n`, gated on
`CONFIG_IDF_EXPERIMENTAL_FEATURES`, whose own help text says it *"can't use on
all flash chips stable"*
([`Kconfig.projbuild:111-119`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/bootloader/Kconfig.projbuild#L111-L119)).
Still `UNTESTED` here, and turning it on would remove the §4.1 refusal as a side
effect — which is a reason to be more careful with it, not less.

## 5. The rule in force

**No Attadipa partition, and no Attadipa flash operation, at or above
`0x1000000`, until §6 has been run and its result written here.**

| Enforced by | Where | Covers |
|---|---|---|
| `partition_check.py` | [`tools/flash/partition_check.py`](../../tools/flash/partition_check.py), run by `ctest` as `flash_partitions_below_ceiling` | every `*partition*.csv` in the tree — starts, ends, crossings, overflow, overlap |
| its self-test | [`tools/flash/selftest.py`](../../tools/flash/selftest.py), run by `ctest` as `flash_partition_check_rejects_mistakes` | that the checker refuses each of those, including the vendor's own shipped table |
| boot diagnostic | `firmware/main/attadipa_main.cpp`, `report_partitions()` | flags any partition reported at boot whose end exceeds `0x1000000` |
| **nothing yet** | — | arbitrary application flash read/write/erase calls; T-165 has no such call to wrap |

That last row is honest rather than comfortable. T-165 has no application flash
read/write/erase path, so a universal wrapper would still be a subsystem
invented for calls that do not exist. The repository table check is enforced in
CI and the boot diagnostic makes a mismatched on-device table visible, but the
diagnostic is not a guard around future flash operations. The first such path
must take the range check with it.

**The T-Watch is unaffected**: a 16 MB part has nothing above the line to reach.
This is a Waveshare constraint that the shared codebase inherits, in the same
way as everything else in [HARDWARE_MATRIX](HARDWARE_MATRIX.md).

**And the cheapest resolution is not to run §6 at all.** The vendor fits a
bootloader, a 952K voice model, a **9 MB** `factory` and two 6 MB OTA slots —
three app partitions, not two — plus 6 MB of UI assets into 28 MB, of which
everything except `ota_1` and `storage` is below the line. What Attadipa needs
is **UNKNOWN**: no image exists to measure, and the quantity that matters is
two OTA slots plus assets inside 16 MB, which is the sum 9M + 6M + 6M failed to
make. The upper half is worth measuring when something actually needs it — the
`models` partition of
[#127](https://github.com/hleserg/Attadipa/issues/127) is the first candidate —
and not before.

## 6. The plan that would settle it

Written to be **reversible at every step** and ordered so that each stage costs
more than the one before it. Nothing below has been executed.

**Hardware validation: NOT EXECUTED — HARDWARE REQUIRED.**

### 6.1 Free, and one of the two needs no board

The JEDEC ID is **not** on this list: it was read on 2026-08-22 and §4.3 is
already settled by it. What is left is cheap and worth doing on the way past.

1. **Grep the captured boot log for the warning.** The bench session captured
   the vendor firmware's log from 62 ms. `Detected flash size > 16 MB, but
   access beyond 16 MB is not supported` in it would mean the running ESP-IDF
   refused the capability on this exact part, contradicting §4.3's arithmetic —
   which would be worth knowing, because it would mean something in the chain is
   not what the source says. The string exists in v5.5.1 at `esp_flash_api.c:304`
   and `:384`, so the vendor's build can print it. **Expected absent**, and this
   is a check of the reasoning rather than a source of it. Costs nothing if the
   capture was kept.
2. **Read the sectors the later stages will use, and hash them.** `0x0FFF000`
   and `0x1FFF000`, 4 KiB each. Needed as the before-picture for §6.3 and free
   to take.

### 6.2 Read-only on the board, and it settles half the question

Use the RAM route from [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md)
§2.3 — `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP`, loaded with `esptool.cmds.load_ram`
from a process that never closes the port. **It writes nothing to flash by
construction**, which the bench session established and `esptool image-info`
confirms per image.

The probe does two things:

- **`esp_flash_read(NULL, buf, 0x1600000, 4096)`** — the vendor's `storage`
  partition, whose bytes are known exactly from the verified whole-part backup.
  Three outcomes and each is diagnostic:

  | The 4 KiB read at `0x1600000` matches | Conclusion |
  |---|---|
  | the backup at `0x1600000` | the runtime **read** path addresses 32 bits correctly — row 5 becomes MEASURED |
  | the backup at `0x0600000` | it aliases, exactly as the bootloader does |
  | neither | something else is wrong; stop and report the bytes |

  The two candidate answers are different data — `0x1600000` is SPIFFS with
  recognisable structure, `0x0600000` is the middle of the `factory` app image —
  so the comparison cannot be ambiguous.
- **`esp_partition_mmap` on `storage`**, expecting `ESP_ERR_INVALID_ARG` from
  §4.1. Confirms the source reading on the actual build, and confirms which of
  the two behaviours the pinned IDF version gives.

Neither needs a permission beyond access to the board, and neither can damage
anything: a `PURE_RAM_APP` has no DROM or IROM segment to be written anywhere.

### 6.3 The write test, which needs a separate authorisation

Erase and program cannot be tested without erasing and programming. **This stage
requires the owner's explicit authorisation, separately from anything already
given**, because — and this is the part that must not be buried — **there is no
canary above the line whose alias is harmless.** The vendor's table is
contiguous from `0x9000` to `0x1000000`, so *every* 24-bit alias target lands in
live vendor data. The best that can be done is to choose the least valuable one
and prove it restorable first.

**The canary: one 4 KiB sector at `0x1FFF000`** — the last sector of the part.

| Why that sector | |
|---|---|
| It is inside `0x1C00000`–`0x2000000`, which [WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §2 records as **genuinely unpartitioned** | no partition entry points at it, so corrupting it cannot confuse the vendor's bootloader |
| Its 24-bit alias is `0x0FFF000` | inside `ota_0` (`0xa00000`–`0x1000000`) |
| The `xiaozhi` image in `ota_0` is 5 481 872 bytes and therefore ends at `0x0F3A590` | the alias is **786 KB past the end of the image the slot holds** — the tail of a slot that the blank `otadata` never selects and that nothing has ever booted |
| Both sectors are inside the T-099 whole-part backup, whose restore was demonstrated end to end in [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §5 | the worst case is a restore that has already been performed once, successfully |

**Procedure**, with the host holding the before-picture:

1. Host, stub, read-only: dump `0x0FFF000` and `0x1FFF000`, 4 KiB each, and hash
   them. (This is §6.1 step 2; if it was done then, reuse it.)
2. RAM probe: `esp_flash_erase_region(NULL, 0x1FFF000, 4096)`, then
   `esp_flash_write` a 4 KiB pattern that cannot occur by accident, then
   `esp_flash_read` it back. Log the return code of each — an
   `ESP_ERR_INVALID_ARG` here is itself an answer.
3. Host, stub, read-only: dump both sectors again.

| Canary sector | Alias sector | Conclusion |
|---|---|---|
| holds the pattern | unchanged | **erase and program address 32 bits correctly** — rows 6 and 7 become MEASURED |
| unchanged | changed | it aliases. The upper half is unusable for anything writable, and the tail of `ota_0` has just been written — go to §6.4 |
| both changed | — | stop. Something is wrong that this plan did not anticipate; report before doing anything else |
| neither changed | unchanged | the command was refused or did nothing; read the probe's return codes |

The RAM probe writes **only** to `0x1FFF000` and only through
`esp_flash_erase_region`/`esp_flash_write` with a literal address, so the
blast radius is one sector plus, in the aliasing case, its one alias.

### 6.4 Restore, in every case including success

```
esptool erase-region 0x1fff000 0x1000
esptool erase-region 0x0fff000 0x1000        # only if the alias was written
esptool verify-flash 0x0 <T-099 backup>      # all 33 554 432 bytes
```

and if the alias sector held something other than `0xFF` before the test, write
its recorded contents back rather than erasing it. The whole-part verify is the
acceptance criterion, exactly as it was for the bench session — that session
ends with `Verification successful` over the entire part and this one must too.

## 7. What each `UNKNOWN` needs

| Row | To become MEASURED |
|---|---|
| 5 — runtime read | §6.2. **No writes, no authorisation beyond board access.** |
| 4 — `mmap` refusal on the real build | §6.2, same probe. |
| 6, 7 — erase and program | §6.3. **Owner authorisation required**, and the alias target is live data. |

Row 8 is already measured — the JEDEC ID was read on 2026-08-22 — and the
answer is that the gate does not protect us. Rows 9, 10 and 11 are properties of
ESP-IDF's source and are settled by reading it; they do not need a board and are
marked VERIFIED rather than MEASURED, because they are claims about a program
and not about this unit.

## 8. What this document does not say

- It does **not** say the upper 16 MB works. Four of the six paths have never
  been run there.
- It does **not** say the upper 16 MB is broken. The bootloader's wrap is the
  bootloader's; the stub's success is evidence the die itself understands
  four-byte addressing.
- It does **not** retract
  [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §1.4. That section
  is right that app partitions must live below the line, right that the vendor
  ships a slot its own bootloader cannot boot, and explicitly says *"nothing here
  has demonstrated the app side either"*. What was missing is what this document
  supplies: which app-side paths there are, what each one does, and which one
  would tell you it had failed.
- It makes **no** power, timing or throughput claim of any kind.
