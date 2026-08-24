# Generated UI fonts

Four subsets of **Montserrat Medium**, covering exactly the 181 codepoints in
[`tools/font/charset.py`](../../tools/font/charset.py), at 14, 16, 20 and 28 px,
4 bits per pixel.

## Why these files are committed

The alternative is running `lv_font_conv` during the build, which puts Node.js
between a contributor and a green build for output that changes about as often as
somebody edits `charset.py`. The localization catalogue already made this trade
and this follows it: the output is committed and a test fails when it drifts —
`ui_fonts_are_current`, which compares a digest of the inputs **and** the
SHA-256 of each of these four files against `INPUTS.sha256`. The second half
arrived with issue #69: until then a hand-edited byte in one of them passed the
check green, because the check compared inputs and then counted filenames.

`INPUTS.sha256` therefore records more than its name says, and the name is kept
because several documents cite it. Its first line says what it actually holds.
Only `generate_ui_fonts.py` writes it; editing a hash there to make a check pass
repairs nothing.

## Provenance

| | |
|---|---|
| Typeface | Montserrat Medium |
| Source | LVGL's own tree at the pinned revision, `scripts/built_in_font/Montserrat-Medium.ttf` |
| LVGL pin | `v9.5.0` — see [DEPENDENCIES](../../docs/research/DEPENDENCIES.md) |
| SHA-256 | `421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53` |
| Licence | SIL Open Font License 1.1 — [`OFL.txt`](OFL.txt) |
| Converter | `lv_font_conv@1.5.3` |
| Coverage | all 181 codepoints, verified by `tools/font/check_coverage.py` |

Taking the file from LVGL's tree rather than from the internet means the font is
pinned by the same commit as the library, which is one fewer thing that can drift.

A generated `.c` is a modified form of the font and stays under OFL-1.1.

## Measured

`.rodata` per size, from `size -A` on the compiled objects:

| Size | `.rodata` |
|---|---|
| 14 px | 13 033 B |
| 16 px | 15 248 B |
| 20 px | 19 356 B |
| 28 px | 31 293 B |
| **all four** | **78 930 B** |

`MEASURED` on the host compiler at `-Os`, which is the right order of magnitude
and not the target's number — `tools/font/measure.py` exists to produce that one
with the xtensa toolchain, and has not been run against these.

## This is not a typeface decision

Montserrat is here because LVGL already ships it under a licence that permits
this, and because it covers the whole charset. Which face the **product** uses is
open question **D16**, and final §51 requires licence, Cyrillic coverage,
legibility at real pixel size and generated flash size to be checked before
either candidate is adopted. None of the four has been checked for a candidate.

Regenerate:

```bash
npm install --no-save lv_font_conv@1.5.3
python3 tools/font/fetch_ttf.py --out /tmp/Montserrat-Medium.ttf
python3 tools/font/generate_ui_fonts.py \
        --ttf /tmp/Montserrat-Medium.ttf --converter ./node_modules/.bin/lv_font_conv
```

Without `--ttf` it looks for the font inside whatever LVGL tree a simulator
build fetched, which is the right default once you have one and a 350 MiB clone
if you do not. Either way the file's SHA-256 must match the table above and the
converter must report `1.5.3`, or nothing is generated.
