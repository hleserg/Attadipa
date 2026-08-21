# Font measurements

T-032. Final §51 asks five questions about a font before it may be adopted —
licence, Cyrillic coverage, legibility at real pixel sizes, generated size, and
render performance. Four of them are answered here with numbers. The fifth is
not, and says so.

Everything below was produced by the tools in [`tools/font/`](../../tools/font),
so it is reproducible rather than quoted. Raw results:
[`font-sizes.csv`](font-sizes.csv).

## What was pinned

| | Version | Licence | Checked how |
|---|---|---|---|
| `lv_font_conv` | **1.5.3** | **MIT** | the `LICENSE` file inside the published tarball, not the `license` field |
| — its bundled dependencies | shipped inside the same tarball | MIT ×8, ISC ×1, **Python-2.0 ×1** | each `node_modules/*/LICENSE` read |
| Inter | `Inter[opsz,wght].ttf` from `google/fonts` | **OFL 1.1** | the `OFL.txt` beside the file |
| Nunito Sans | `NunitoSans[YTLC,opsz,wdth,wght].ttf` from `google/fonts` | **OFL 1.1** | the `OFL.txt` beside the file |

`lv_font_conv@1.5.3` integrity
`sha512-0xJQThBOw2iptFccSXrKDIUTQAwr/2zhKjCI1lATIRgZo8uvYRTmenKafW9yTw6G0y5AyW00tqGpUtYuTuBIbQ==`.

The **Python-2.0** entry is `argparse`, and it is called out because it is the
one licence in the set that is not the one the package advertises for itself.
It is a build-time dependency of a build-time tool: nothing it contains reaches
the firmware, and the generated font carries no code from any of them. Recorded
because "all MIT" would have been the easy sentence and it is not true.

Font SHA-256, so a future bump is visible:

```
29160a80ff49ddcab2c97711247e08b1fab27a484a329ce8b813d820dc559031  Inter[opsz,wght].ttf
f934d7142fb4784bf828da485b7dcbd90c0c80d514e9d49a5da0ed3a1ae2491d  NunitoSans[YTLC,opsz,wdth,wght].ttf
```

## The charset — 181 codepoints, 18 ranges

Defined once, in [`tools/font/charset.py`](../../tools/font/charset.py), with a
reason recorded per range. Final §51 forbids both Latin-only-for-now and
shipping all of Unicode, so every range is there because something uses it.

The subtle entries: **U+0401 and U+0451** sit outside the main Cyrillic block
and are the classic omission; **U+2116 №** has no ASCII substitute in Russian;
**U+00AB/U+00BB and U+201E** are Russian quotation marks, not decoration.

## Coverage — this is the check that eliminates a font

| Font | Covers | Missing |
|---|---|---|
| **Inter** | **181 / 181** | — |
| **Nunito Sans** | 177 / 181 | **U+2190–U+2193, all four arrows** |

`lv_font_conv` refuses outright rather than substituting: *"Font … doesn't have
any characters included in range 0x2190-0x2193"*. Good behaviour, and a second
independent confirmation of the same gap.

This is not automatically fatal to Nunito Sans. Arrows in a navigation UI are
arguably icons — rotatable, scalable, owned by the image pipeline (T-034) —
rather than text glyphs. But it is a decision, not an oversight, and it belongs
to whoever picks the font.

## Two traps found by running the pipeline rather than reading about it

### 1. The default instance is not the weight you think

Both families ship from Google Fonts **as variable fonts only** — there are no
static instances in the repository. `lv_font_conv` converts the *default*
instance.

| Font | Axes | Default weight |
|---|---|---|
| Inter | `opsz 14..32`, `wght 100..900` | **400 — Regular.** Fine |
| Nunito Sans | `wght 200..1000`, `wdth 75..125`, `opsz 6..12`, `YTLC 440..540` | **200 — ExtraLight** |

Converting Nunito Sans as downloaded produces an ExtraLight font. Nothing
complains, because ExtraLight is a perfectly valid font. It is simply not a UI
weight, and hairlines at 14 px on a wrist in daylight are the failure mode.

### 2. …and instancing to fix that can destroy kerning

MEASURED at 20 px, bpp 4, compressed, as `.rodata` in the object file:

| | with kerning | `--no-kerning` | kerning costs |
|---|---:|---:|---:|
| Inter, variable, as downloaded | 11 237 | 10 225 | **1 012** |
| Inter, run through `fontTools` at `wght=400` | 10 225 | 10 225 | **0** |
| Nunito Sans, variable (ExtraLight) | 13 236 | 8 364 | 4 872 |
| Nunito Sans, instanced at `wght=400` | 14 721 | 9 485 | 5 236 |

Instancing Inter **at its own default weight** — a step that should be a no-op —
leaves `GPOS` in the file but leaves `lv_font_conv` unable to find any kerning
in it. `optimize=False` does not change this. Nunito Sans keeps its kerning
through the same step, so this is not a general fact about `fontTools`; it is a
reason never to rewrite a font you did not need to rewrite.

So the rule the pipeline enforces:
[`instantiate.py`](../../tools/font/instantiate.py) **copies the file unchanged
when the requested location is already the default**, and says so out loud.

Note also that Nunito Sans's kerning is a **fixed 5 236 bytes at every size** —
at 14 px bpp 1 that is 64 % of the entire font (8 225 B with, 2 989 B without).
Kerning is not a rounding error in an embedded font; it is a budget line.

## Generated size

MEASURED — generated with `lv_font_conv` 1.5.3, compiled with
`xtensa-esp32s3-elf-gcc 14.2.0` at `-Os`, and read as the `.rodata` section of
the object file. **This is a compiled-artefact size, not a runtime measurement**
and not a figure from hardware. The `.c` file on disk is roughly three times
larger — it is ASCII hex — and is not the number that matters.


**Inter-400** — 181 glyphs. `.rodata` bytes in the object file.

| px | bpp 1 | bpp 2 compressed | bpp 2 raw | bpp 4 compressed | bpp 4 raw |
|---:|---:|---:|---:|---:|---:|
| 14 | 4 221 | 5 552 | 6 153 | 8 091 | 9 583 |
| 16 | 4 570 | 6 209 | 7 013 | 9 172 | 11 326 |
| 20 | 5 585 | 7 398 | 8 996 | 11 237 | 15 228 |
| 24 | 6 806 | 8 950 | 11 481 | 13 882 | 20 225 |
| 28 | 8 485 | 11 317 | 15 179 | 17 743 | 27 624 |
| 36 | 12 023 | 14 885 | 22 464 | 24 202 | 42 186 |
| 48 | 19 713 | 21 695 | 38 193 | 34 997 | 73 684 |

**NunitoSans-400** — 177 glyphs. `.rodata` bytes in the object file.

| px | bpp 1 | bpp 2 compressed | bpp 2 raw | bpp 4 compressed | bpp 4 raw |
|---:|---:|---:|---:|---:|---:|
| 14 | 8 225 | 9 405 | 9 932 | 11 463 | 12 928 |
| 16 | 8 543 | 9 932 | 10 703 | 12 455 | 14 493 |
| 20 | 9 594 | 11 203 | 12 795 | 14 721 | 18 656 |
| 24 | 10 723 | 12 646 | 15 203 | 17 176 | 23 486 |
| 28 | 12 148 | 14 309 | 18 104 | 19 894 | 29 309 |
| 36 | 15 658 | 18 327 | 25 310 | 26 578 | 43 691 |
| 48 | 22 245 | 23 845 | 38 801 | 35 670 | 70 675 |

Both include kerning as they would actually ship: Inter unchanged (1 012 B of
kern data), Nunito Sans instanced to `wght=400` (5 236 B).

What the table says:

- **bpp 1 ignores `--no-compress` entirely** — identical bytes either way, at
  every size, for both fonts. Compression buys nothing on a 1-bit bitmap.
- **compression matters more the larger the font**: at 48 px bpp 4 it halves
  Inter, 73 684 → 34 997. At 14 px bpp 2 it saves 10 %.
- **Nunito Sans is not really 4 KB heavier**; that gap is almost entirely its
  kerning table, and the two fonts are within a few hundred bytes of each other
  once kerning is set aside.

A rough budget: a three-size ladder at bpp 4 compressed — 16, 20 and 36 px —
costs **44 611 B in Inter** and **53 754 B in Nunito Sans**. At bpp 2 it is
28 492 B and 39 462 B. Neither is alarming against 16 MB of flash; both are
worth knowing before the design system commits to five sizes instead of three.

## Legibility at real pixel sizes

Rendered through `lv_font_conv`'s own `dump` format — the same rasterisation the
firmware gets, not a desktop preview — at 4× nearest-neighbour so the pixels are
visible as pixels. Checked in **both themes**, because the Definition of Done
says both and a stroke that survives one does not automatically survive the
other.

| | Day | Night |
|---|---|---|
| Inter | [14/16/20/28 px](../ui/specimens/sheet-Inter-400-day.png) | [14/16/20/28 px](../ui/specimens/sheet-Inter-400-night.png) |
| Nunito Sans | [14/16/20/28 px](../ui/specimens/sheet-NunitoSans-400-day.png) | [14/16/20/28 px](../ui/specimens/sheet-NunitoSans-400-night.png) |

Both render Cyrillic legibly at 14 px at bpp 4, including the Ё diaeresis, which
is the first thing to collapse. The sheets are bottom-aligned rather than
baseline-aligned — they are a legibility check, not a metrics proof, and the
image says so on itself.

## Not measured

**Render performance.** Final §51 asks for it and it is not here. It needs
either the simulator driving real frames with a timer, or a board. Recorded as
UNKNOWN rather than guessed. It is the reason T-032 closes the *licence and
size* half of the font decision and not the whole of it.

**Neither font is chosen.** These are the numbers a choice needs, and the choice
is a design decision — the arrows gap and the ExtraLight default both change
what "use Nunito Sans" means. See OPEN_QUESTIONS.
