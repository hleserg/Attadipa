# Resource budget

**No product resource budget in this document has been measured.** T-165 now
builds and boots a minimal Waveshare firmware, but it does not contain the
display, touch, PMU, RTC or application tasks whose budgets this file exists to
measure. Every figure here is therefore one of three things, and each is
labelled:

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

## 1. Why this file exists before there is product firmware

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
| PSRAM | 8 MB **octal**, D12b — the factory image's octal init boots it, 2026-08-27 | 8 MB **octal**, D12a | CEILING, both MEASURED |
| Internal SRAM | 512 KB (ESP32-S3) | 512 KB (ESP32-S3) | CEILING |
| Display | 240 × 240 | 410 × 502 | CEILING |

**The PSRAM conflict, resolved on both boards.** The recollection this section rested on
has been checked: ESP32-S3 Series Datasheet v2.2 §1.2 Table 1-1 gives
`ESP32-S3R8 | 8 MB (Octal SPI)`, and the table contains **no 8 MB quad
in-package variant at all** — quad in-package exists only at 2 MB. Footnote 3
names `R8`, `R8V` and `R16V` as the octal parts, and `R8`/`R8V` differ by
`VDD_SPI` voltage rather than bus width.

For the **Waveshare** that closes it: 8 MB octal, D12a, corroborated by five
vendor examples shipping `CONFIG_SPIRAM_MODE_OCT=y` with
`CONFIG_SPIRAM_IGNORE_NOTFOUND` unset, and by GPIO33–37 — octal's DQ4–DQ7 and
DQS — sitting unrouted on the schematic.

For the **T-Watch** the shared `R8` marking was not enough to make it, and the
LilyGO vendor document describing that board's PSRAM as **QSPI** was a live
conflicting source — one document beating another by inference is how a wrong
`sdkconfig` gets pinned. D12b was closed on the physical unit on 2026-08-27,
and not by the leg its row asked for: there is no quad/octal eFuse, and
`flash_id`'s "4 data lines" describes the flash. What discriminates is the
factory firmware — it carries the **octal** PSRAM init and no quad one, and
the watch boots and runs, where an octal init against a quad part bails out
(`docs/research/TWATCH_S3_PLUS_BRINGUP_2026-08-27.md:146` — "The factory firmware contains the **octal** PSRAM implementation and no quad").
The eFuses carry the rest: 8 MB, vendor `AP_3v3`, and Table 1-1 has no 8 MB
quad in-package part. Octal, MEASURED, and the vendor document is wrong —
`docs/research/HARDWARE_MATRIX.md:87` — "Closes D12b" and
`docs/research/OPEN_QUESTIONS.md:130` — "RESOLVED 2026-08-27 — octal".

Quad and octal differ by roughly a factor of two in bandwidth and need different
`sdkconfig` settings; getting it wrong is not a performance nuance, it is a board
that does not boot or a framebuffer that cannot keep up.

**And the Waveshare answer does not license the obvious next step.** The vendor's
own BSP does *not* put the LVGL draw buffer in PSRAM — its `.buff_spiram = true`
is dead code and what ships is one ~80 KiB partial buffer in internal SRAM. There
is no existence proof to lean on here, and `SOC_PSRAM_DMA_CAPABLE` is 0 on the
S3, so a draw buffer in PSRAM can never also be DMA-capable. See
[../research/WAVESHARE_ARRIVAL.md](../research/WAVESHARE_ARRIVAL.md) §3.3 and
T-093.

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
tearing behaviour and redraw complexity. **That is an ADR, not a default**;
implementation waits for that decision.

**And a third cost belongs in that ADR, in time rather than in bytes.** The one
complete display path readable in pinned source sets `swap_bytes` and therefore
runs `lv_draw_sw_rgb565_swap()` on every flush — a software, in-place pass over
the flushed region, on the CPU. At full frame that is a second traversal of the
402 KiB above, and on PSRAM-backed buffers it is a second traversal *of PSRAM*,
against the cache-coherency requirement in the same sentence. Per-frame CPU time
is also battery.

**Three** things about it are **`UNKNOWN`** and none may be assumed away. The
third was added in the second review round, because an earlier version of this
paragraph priced the pass as a fixed property of the panel:

| | Question | Where it is tracked |
|---|---|---|
| Necessity | does *this* panel need the swap at all — the CO5300's wire byte order | **D21**, [`OPEN_QUESTIONS`](../research/OPEN_QUESTIONS.md); closable by the datasheet or by a photographed pattern |
| Cost | what the pass costs per frame, internal SRAM versus PSRAM | not measured; belongs in §4's table when somebody measures it |
| Avoidability | whether the pass is a property of the **panel** or of the **flush-time-swap strategy** | not settled; an input to T-093 rather than a constant it designs around |

The third exists because LVGL can render straight into a swapped destination:
`lv_draw_sw_blend_to_rgb565_swapped.c` is a whole blend target at the pinned
`lvgl@85aa60d1`, and
`sim/lv_conf_simulator.h:216` "LV_DRAW_SW_SUPPORT_RGB565_SWAPPED       1"
compiles it in.

**With a precondition this row has to carry, because it is a device row resting
on a host-build fact.** That file is the *simulator's* `lv_conf`, and
`cmake/AttadipaLvgl.cmake:62` "LV_BUILD_CONF_PATH" points the only LVGL build
this repository has at it — there is no device configuration, because there is
no device build. The flag also sits under
`sim/lv_conf_simulator.h:210` "Selectively disable color format support in order to reduce code size",
which is precisely the switch a flash-constrained firmware turns off. So T-093 must
decide whether the device `lv_conf` carries it at all before designing around
the option; treating *"it compiles in"* as settled is how a later size pass
removes an assumption three layers up. Named in the third review round of
[#152](https://github.com/hleserg/Attadipa/pull/152). On that strategy the
conversion folds into the blend that already runs and there is no second
traversal at all — it is paid per blended pixel rather than per frame pixel,
which for a watch face that redraws a small region is a different number
entirely, and possibly a larger one for a full-screen redraw. Neither has been
measured, which is the point: **T-093 should see the swap priced as an option,
not inherit it as a cost.**

Recorded because the source trace that established the swap filed it as a
correctness question only, and the draw-buffer ADR would otherwise be written
against a frame time with a full-buffer software pass either missing from it or
nailed into it — the swap then resurfacing later as an unexplained regression on
the one board that cannot afford it. Found in review of
[#152](https://github.com/hleserg/Attadipa/pull/152), corrected in its second
round.

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

The asset rows are calculated from what the generators emit. The current flash
firmware links them and measures 0x15d9e0 bytes in the 0x400000-byte app
partition; 0x2a2620 bytes (66%) remain.

| Asset set | Bytes | How |
|---|---|---|
| Text fonts — Montserrat Medium, 181 codepoints, 4 bpp, at 14 / 16 / 20 / 28 px | **78 930** | `MEASURED` with xtensa `size -A` on the generated objects: 13 033 + 15 248 + 19 356 + 31 293 |
| Icons — three masks at 33, 39 and 47 px | **14 457** | `CALCULATED`: `A8` is one byte per pixel with `stride == width`, so an icon costs exactly its pixel count. Reported per asset by `tools/assets/generate_images.py` and repeated in the generated header |
| Clock night-meadow background — 410x502 RGB565 | **411 640** | `CALCULATED`: 410 x 502 x 2, uncompressed; exact target geometry, no runtime resampling on the Waveshare |

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

**The host figure and the device implementation are now deliberately different.**
`Bridge::handle` into a message
decode and out through a reply is about **1 KB of zero-initialised locals**. But
that is not the deepest path: it is called from inside a **4 KB receive buffer**.
`sim/debug_server.cpp:438` — "std::uint8_t chunk[4096];" — puts it on the stack and calls
`dispatch_ready` from within its scope, which adds `payload[link::kMaxPayload]`
and, under `emit`, `queue`'s `frame[link::kMaxFrame]`. The whole chain is about
**5 KB**, `ESTIMATED` by reading the desktop frames rather than measured.

T-114 did not create another task or copy the desktop server. The firmware polls
from LVGL's existing timer context, reads USB in a 1 KiB local chunk, and keeps
the 16 KiB output queue as object storage. Its 410 × 502 RGB565 snapshot is
411,640 bytes in PSRAM. On the physical unit the pre-endpoint T-166 candidate
reported 257,307 internal bytes free; the endpoint candidate held 231,863 bytes
free through 200 seconds. That 25,444-byte delta is **MEASURED for the combined
image**, not attributed solely to one buffer or claimed as a task high-water
mark. There is no new task whose stack needs an `uxTaskGetStackHighWaterMark()`
row.

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
