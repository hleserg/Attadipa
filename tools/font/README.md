# Font tooling

Scripts, roughly in the order they run. Nothing here runs in CI yet — that
arrives with T-033, whose third check is "a catalogue entry the generated font
cannot draw".

| | What it is for |
|---|---|
| [`charset.py`](charset.py) | **the** definition of what Attadipa's fonts must be able to draw. 181 codepoints, 18 ranges, a recorded reason per range |
| [`check_coverage.py`](check_coverage.py) | does this font file have a glyph for every one of them? The check that eliminates a font |
| [`instantiate.py`](instantiate.py) | pin a variable font to one weight — and refuse to rewrite it when it is already at that weight |
| [`measure.py`](measure.py) | generate, compile for ESP32-S3, and report `.rodata`. The flash cost, measured |
| [`contact_sheet.py`](contact_sheet.py) | render the glyphs LVGL will actually draw, at the size it will draw them, with the advance, side bearings and kerning it will use, in both themes |
| [`generate_ui_fonts.py`](generate_ui_fonts.py) | produce the checked-in subsets under `assets/fonts/generated/`, and `--check` that they are current |
| [`measure_strings.py`](measure_strings.py) | **will this string fit?** — the other question, answered against the *checked-in* fonts rather than a candidate TTF |

Results: [FONT_MEASUREMENTS](../../docs/research/FONT_MEASUREMENTS.md) for the
flash cost; [CLOCK_STATE_AND_CADENCE](../../docs/research/CLOCK_STATE_AND_CADENCE.md)
§7 for the string widths.

## `measure.py` and `measure_strings.py` are different questions

`measure.py` asks what a *candidate* face costs in flash, so it needs the TTF,
`lv_font_conv` and an xtensa compiler. `measure_strings.py` asks how wide a
string will be drawn in the font that is **already committed**, so it needs
none of those — no Node, no toolchain, no font files, not even Pillow. It reads
`assets/fonts/generated/attadipa_montserrat_*.c` and applies LVGL v9.5.0's own
integer arithmetic to them: `lv_font_fmt_txt.c:245-253` for the kerned,
per-glyph-rounded advance and `lv_text.c`'s `lv_text_get_width()` for the sum.
Each transcribed line is cited in the module docstring beside the code that
mirrors it.

```bash
python3 tools/font/measure_strings.py --self-test   # check the parse first
python3 tools/font/measure_strings.py --digits      # are the figures tabular?
python3 tools/font/measure_strings.py --time-span   # how far a centred clock moves
python3 tools/font/measure_strings.py --dates       # widest date, per language
python3 tools/font/measure_strings.py --clock       # the Clock string set
python3 tools/font/measure_strings.py --size 28 --string '30 сентября 2026'
```

`--self-test` is the one to run after touching it: it re-derives the parse
against what the generated files say about themselves — every codepoint
`charset.py` asks for resolves to a glyph, every cmap lands inside `glyph_dsc[]`,
`kern_scale` is still the 16 the arithmetic assumes, and the kerning table is
reachable. It asserts no hand-typed numbers, so it does not go stale when the
fonts are regenerated.

**It measures the scaffold, not the product.** The committed fonts are
Montserrat Medium, which is there because LVGL ships it and it covers the
charset — D16 is open and no typeface is chosen. Every width it reports has to
be re-taken after that decision.

## What has to be installed

```bash
pip install fonttools pillow           # coverage, instancing, contact sheets
npm install --no-save lv_font_conv@1.5.3
```

`measure.py` also needs an ESP-IDF xtensa toolchain on the machine; it finds
`xtensa-esp32s3-elf-gcc` under `~/.espressif` and refuses rather than falling
back to the host compiler. A host-compiled size would be a different number
quietly presented as the same one.

## The fonts are not in this repository

Deliberately: neither font is chosen yet (OPEN_QUESTIONS D16), and vendoring
both would mean carrying 1.4 MB to answer a question that has not been asked of
the owner. Fetch them from the canonical source:

```bash
mkdir -p /tmp/attadipa-fonts && cd /tmp/attadipa-fonts
curl -sLO 'https://raw.githubusercontent.com/google/fonts/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf'
curl -sLo Inter-OFL.txt 'https://raw.githubusercontent.com/google/fonts/main/ofl/inter/OFL.txt'
curl -sLO 'https://raw.githubusercontent.com/google/fonts/main/ofl/nunitosans/NunitoSans%5BYTLC,opsz,wdth,wght%5D.ttf'
curl -sLo Nunito-OFL.txt 'https://raw.githubusercontent.com/google/fonts/main/ofl/nunitosans/OFL.txt'
sha256sum *.ttf     # must match DEPENDENCIES.md
```

Both arrive as **variable fonts only** — `google/fonts` publishes no static
instances of either — which is the reason `instantiate.py` exists.

## The whole run

```bash
FONTS=/tmp/attadipa-fonts
CONV=./node_modules/.bin/lv_font_conv

python3 tools/font/check_coverage.py "$FONTS"/*.ttf

mkdir -p "$FONTS/measured"
python3 tools/font/instantiate.py "$FONTS/Inter[opsz,wght].ttf" \
        "$FONTS/measured/Inter-400.ttf" wght=400
python3 tools/font/instantiate.py "$FONTS/NunitoSans[YTLC,opsz,wdth,wght].ttf" \
        "$FONTS/measured/NunitoSans-400.ttf" wght=400

python3 tools/font/measure.py --fonts "$FONTS/measured" --lv-font-conv "$CONV" \
        --lvgl /path/to/lvgl-v9.5.0 --lv-conf sim/lv_conf_simulator.h \
        --out docs/research/font-sizes.csv

for theme in day night; do
  python3 tools/font/contact_sheet.py --font "$FONTS/measured/Inter-400.ttf" \
      --lv-font-conv "$CONV" --bpp 4 --sizes 14,16,20,28 --theme "$theme" \
      --text "Привет Ёжик 12:34 Attadipa" \
      --out "docs/ui/specimens/sheet-Inter-400-$theme.png"
done
```

`check_coverage.py` exits non-zero on Nunito Sans today, and that is the correct
answer: it has no arrows. `measure.py` drops such a range so the rest can still
be measured, and prints every range it dropped — a measurement of a smaller
charset reported as if it were the charset is the quiet lie all of this exists
to avoid.
