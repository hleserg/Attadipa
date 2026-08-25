# Generated UI fonts

Seven subsets of **Nunito Sans Regular 400**, covering exactly the 177 codepoints in
[`tools/font/charset.py`](../../tools/font/charset.py), at 14, 16, 20, 28, 64, 84
and 96 px, 4 bits per pixel.

## Why these files are committed

The alternative is running `lv_font_conv` during the build, which puts Node.js
between a contributor and a green build for output that changes about as often as
somebody edits `charset.py`. The localization catalogue already made this trade
and this follows it: the output is committed and a test fails when it drifts —
`ui_fonts_are_current`, which compares a digest of the inputs **and** the
SHA-256 of each generated file against `INPUTS.sha256`. The second half
arrived with issue #69: until then a hand-edited byte in one of them passed the
check green, because the check compared inputs and then counted filenames.

`INPUTS.sha256` therefore records more than its name says, and the name is kept
because several documents cite it. Its first line says what it actually holds.
Only `generate_ui_fonts.py` writes it; editing a hash there to make a check pass
repairs nothing.

## Provenance

| | |
|---|---|
| Typeface | Nunito Sans Regular 400 |
| Source | `google/fonts` commit `a0e524c05906bece66cd5bcdc9216ff1d044fcbf` |
| Variable source | `ofl/nunitosans/NunitoSans[YTLC,opsz,wdth,wght].ttf`, pinned to `wght=400` before conversion |
| SHA-256 | `f934d7142fb4784bf828da485b7dcbd90c0c80d514e9d49a5da0ed3a1ae2491d` |
| Licence | SIL Open Font License 1.1 — [`OFL.txt`](OFL.txt) |
| Converter | `lv_font_conv@1.5.3` |
| Coverage | all 177 text codepoints, verified by `tools/font/check_coverage.py`; arrows are UI icons |

The owner references name Nunito Sans and Inter. The measured comparison already
established Nunito's licence, Cyrillic coverage, size and the need to pin its
variable default from ExtraLight 200 to Regular 400; the owner selected the
rounded reference direction for the product Clock on 2026-08-26.

A generated `.c` is a modified form of the font and stays under OFL-1.1.

## Measured

`.rodata` per size, from `size -A` on the compiled objects:

| Size | `.rodata` |
|---|---|
| 14 px | 12 776 B |
| 16 px | 14 344 B |
| 20 px | 18 504 B |
| 28 px | 29 160 B |
| 64 px | 120 440 B |
| 84 px | 200 376 B |
| 96 px | 259 360 B |

`MEASURED` on the host compiler at `-Os`, which is the right order of magnitude
and not the target's number — `tools/font/measure.py` exists to produce that one
with the xtensa toolchain, and has not been run against these.

Regenerate:

```bash
npm install --no-save lv_font_conv@1.5.3
python3 tools/font/fetch_ttf.py --out '/tmp/NunitoSans[YTLC,opsz,wdth,wght].ttf'
python3 tools/font/generate_ui_fonts.py \
        --ttf '/tmp/NunitoSans[YTLC,opsz,wdth,wght].ttf' \
        --converter ./node_modules/.bin/lv_font_conv
```

Without `--ttf` it looks under `/tmp`, where `fetch_ttf.py` normally places the
pinned source. The SHA-256 and converter version must match the table, or
nothing is generated.
