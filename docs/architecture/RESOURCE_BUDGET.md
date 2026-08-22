# Resource budget

**Nothing in this document has been measured.** No Attadipa firmware has been
built for either target, so every figure here is one of three things, and each
is labelled:

- **CEILING** — a hardware limit from the part number or datasheet. Fixed.
- **ARITHMETIC** — computed from a CEILING and a resolution. Correct by
  construction, but it is a size, not a consumption.
- **UNKNOWN** — will be a number when something has been built and measured.

There are no ESTIMATED rows. An estimate here would be a guess wearing a
number's clothes, and the whole point of this file is to be the place where a
guess cannot hide. Fill the UNKNOWN rows from the build, not from experience.

Related: [ARCHITECTURE.md](ARCHITECTURE.md) §8 — build the measurement before
the mitigation. That applies to memory exactly as it applies to interference.

---

## 1. Why this file exists before there is any firmware

The two targets are not the same size of problem. The Waveshare panel has
**3.57× the pixels** of the T-Watch panel. A framebuffer strategy that is
comfortable on one is not automatically viable on the other, and the difference
is large enough that it should shape the display architecture now rather than be
discovered when the second board is brought up.

That is the only conclusion this document is currently entitled to draw.

---

## 2. Ceilings

| Resource | T-Watch S3 Plus | Waveshare AMOLED 2.06 | Status |
|---|---|---|---|
| Flash | 16 MB (`W25Q128JW`) | **32 MB** (`GD25Q256EYIGR`, quad) | CEILING |
| PSRAM | 8 MB — **but see below** | 8 MB — same caveat | CONFLICTING |
| Internal SRAM | 512 KB (ESP32-S3) | 512 KB (ESP32-S3) | CEILING |
| Display | 240 × 240 | 410 × 502 | CEILING |

**The PSRAM conflict.** The T-Watch vendor document describes 8 MB **QSPI**
PSRAM. Both schematics mark the SoC `ESP32-S3R8`, and the `R8` suffix is
*understood* to denote **octal** SPI PSRAM — that understanding is recollection,
not something established here, and checking it against Espressif's published
part-numbering scheme is part of D12 rather than an assumption this document
gets to make. Quad and octal differ by roughly a factor of two in
bandwidth and they need different `sdkconfig` settings; getting it wrong is not
a performance nuance, it is a board that does not boot or a framebuffer that
cannot keep up. Resolve before pinning any display or LVGL configuration —
OPEN_QUESTIONS D12.

Internal SRAM is the number that actually binds. 512 KB is the die's total; what
a task can allocate after ESP-IDF, the Wi-Fi/BLE stacks and the driver layer
have taken their share is materially less, and it is the figure to measure
first.

---

## 3. LVGL buffers — arithmetic

Full-frame RGB565, both boards:

| Board | Pixels | Full frame | Double-buffered |
|---|---|---|---|
| T-Watch | 57 600 | 115 200 B (112.5 KiB) | 230 400 B (225 KiB) |
| Waveshare | 205 820 | 411 640 B (402 KiB) | 823 280 B (804 KiB) |

Read against the 512 KB internal SRAM ceiling, this settles one thing and opens
another:

- A double-buffered full frame for the Waveshare panel **cannot live in internal
  SRAM**. It is 804 KiB against a 512 KB die total, before ESP-IDF exists.
- A single full frame at 402 KiB is arithmetically under the ceiling and
  practically almost certainly not, once the IDF, the QSPI driver and the BLE
  stack have taken their share.

So the Waveshare board needs either partial-buffer rendering or PSRAM-backed
buffers. Both are real options with different costs — PSRAM buffers trade
bandwidth and add a cache-coherency requirement for DMA; partial buffers trade
tearing behaviour and redraw complexity. **That is an ADR, not a default**, and
it is listed as a pending decision in [TASKS.md](../../TASKS.md).

The T-Watch numbers are comfortable enough that the same strategy will fit
whatever the Waveshare board forces. Design for the harder board.

---

## 4. What must be tracked, and how it will be measured

Every row below is UNKNOWN. The method column is the commitment — when a row
gets a number, the method it came from goes next to it.

### Flash

| Item | Method |
|---|---|
| Bootloader + partition table | `idf.py size` after the first embedded build |
| Application image | `idf.py size` |
| Assets (fonts, icons) | image manifest, sized before inclusion. **Two rows now have numbers** — see below |
| NVS, littlefs / SPIFFS partitions | partition table — an ADR, not an accident |
| OTA slots — does the image fit twice? | partition table arithmetic against the flash ceiling |

Firmware update needs two application slots plus the data partitions. Neither
board looks tight: 16 MB on the T-Watch and 32 MB on the Waveshare board — which
is, conveniently, the board with 3.57× the pixels and therefore the larger asset
burden. Convenient, not planned; do the arithmetic once there is an image.

#### The two asset pipelines, with numbers

Both are `CALCULATED` from what the generators emit, not `MEASURED` on a device
— nothing has been linked into a firmware image yet, and `idf.py size` is the
only thing that will settle the difference between an array's size and its cost
after alignment and section placement.

| Asset set | Bytes | How |
|---|---|---|
| Text fonts — Montserrat Medium, 181 codepoints, 4 bpp, at 14 / 16 / 20 / 28 px | **78 930** | `MEASURED` with xtensa `size -A` on the generated objects: 13 033 + 15 248 + 19 356 + 31 293 |
| Icons — three masks at 33, 39 and 47 px | **14 457** | `CALCULATED`: `A8` is one byte per pixel with `stride == width`, so an icon costs exactly its pixel count. Reported per asset by `tools/assets/generate_images.py` and repeated in the generated header |

The icon number is the one worth watching, because it is the one that scales
with the product rather than with the alphabet. Nine masks are 14 kB; the whole
cross-product of four `IconSize` tokens against two board densities is **seven
distinct pixel sizes**, so a full set of three icons would be 39 kB and a
realistic set of thirty would be about 400 kB. That is why
`tools/assets/manifest.py` names the sizes it generates rather than taking the
cross-product, and why adding one is a deliberate line rather than a
consequence.

Two knobs exist and neither has been turned, both for the same reason — nothing
has been measured:

- **compression.** `LVGLImage.py` offers RLE and LZ4. Both trade flash for
  decode time and a scratch buffer, on a device whose PSRAM sits behind a QSPI
  bus nobody has timed. 14 kB is not worth a guess.
- **fewer sizes.** Dropping to one pixel size per icon and letting LVGL scale
  would cut the count, and it is exactly what final §86 forbids — a 47-pixel
  drawing resampled to 33 is a different, worse drawing.

### Internal RAM

| Item | Method |
|---|---|
| Static allocation at link time | `idf.py size-components` |
| Free heap after boot, before UI | `esp_get_free_heap_size()` at a fixed point |
| Largest free block after boot | `heap_caps_get_largest_free_block()` — the fragmentation signal |
| Minimum free heap ever seen | `esp_get_minimum_free_heap_size()` at shutdown of a soak run |

### PSRAM

| Item | Method |
|---|---|
| Whether it is quad or octal | **D12 — answer before anything else here** |
| LVGL buffers, if placed there | allocation site + `heap_caps` region accounting |
| Mesh routing state | same |
| Cache-coherency cost for DMA from PSRAM | measured frame time, both placements |

### Task stacks

Every task gets a declared stack and a measured high-water mark. A task whose
high-water mark is never sampled is an unbudgeted task.

| Item | Method |
|---|---|
| Per-task declared stack | the `xTaskCreate` call — one table, reviewed |
| Per-task high-water mark | `uxTaskGetStackHighWaterMark()` after a soak run |
| Headroom policy | to be decided — a stack sized to its exact high-water mark is a crash waiting for a deeper call path |

### Mesh state and message history

The parts of the budget that grow with use rather than with the build. These are
the ones that fail in the field rather than in CI.

| Item | Method |
|---|---|
| MeshCore's own memory footprint | **T-006 — read the upstream source.** Not answerable yet |
| Routing / neighbour table, and its bound | same |
| Stored message history and its retention policy | a product decision with a storage cost |
| Behaviour at the bound | must be defined: oldest-out, refuse, or degrade — never "runs until it stops" |

Any structure that grows with the number of nodes, the number of messages or the
uptime needs a declared maximum **and a defined behaviour on reaching it**. A
bound that has never been hit in testing is not a bound, it is a hope.

### Fragmentation

Long-uptime firmware fails differently from short-lived programs: total free
heap looks fine and the largest free block is too small for the next allocation.

| Risk | Mitigation to evaluate |
|---|---|
| LVGL object churn as screens come and go | pooled or static allocation for screen objects |
| Variable-length mesh payloads | fixed-size pools sized to the maximum payload |
| Log and diagnostic buffers | ring buffers allocated once at boot |
| Easter-egg state (see the private notes file) | already specified as a fixed pool with losable state — for exactly this reason |

The measurement that matters is not free heap. It is **largest free block over a
long soak**, sampled periodically and plotted. A downward trend there is the
failure, and it is invisible in a free-heap total.

---

## 5. Definition of done for this document

This file stops being a plan and becomes a budget when:

1. D12 is answered — quad or octal decides PSRAM bandwidth on both boards.
   (D1 is resolved: 16 MB and 32 MB of flash respectively.)
2. An embedded image has been built for at least one target and `idf.py size`
   has produced flash and static-RAM numbers.
3. Every task has a declared stack and a measured high-water mark.
4. The LVGL buffer strategy is an ADR rather than a default.
5. Mesh state has a declared bound and a defined behaviour at that bound.
6. A soak run has produced a largest-free-block trend, not just a free-heap
   total.

Until then it is an honest list of what is not yet known, which is worth more
than a table of plausible numbers.
