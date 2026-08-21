# Font tooling

Four scripts, in the order they run. Nothing here runs in CI yet — that arrives
with T-033, whose third check is "a catalogue entry the generated font cannot
draw".

| | What it is for |
|---|---|
| [`charset.py`](charset.py) | **the** definition of what Firefly's fonts must be able to draw. 181 codepoints, 18 ranges, a recorded reason per range |
| [`check_coverage.py`](check_coverage.py) | does this font file have a glyph for every one of them? The check that eliminates a font |
| [`instantiate.py`](instantiate.py) | pin a variable font to one weight — and refuse to rewrite it when it is already at that weight |
| [`measure.py`](measure.py) | generate, compile for ESP32-S3, and report `.rodata`. The flash cost, measured |
| [`contact_sheet.py`](contact_sheet.py) | render the glyphs LVGL will actually draw, at the size it will draw them, with the advance, side bearings and kerning it will use, in both themes |

Results: [FONT_MEASUREMENTS](../../docs/research/FONT_MEASUREMENTS.md).

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
mkdir -p /tmp/firefly-fonts && cd /tmp/firefly-fonts
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
FONTS=/tmp/firefly-fonts
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
      --text "Привет Ёжик 12:34 Firefly" \
      --out "docs/ui/specimens/sheet-Inter-400-$theme.png"
done
```

`check_coverage.py` exits non-zero on Nunito Sans today, and that is the correct
answer: it has no arrows. `measure.py` drops such a range so the rest can still
be measured, and prints every range it dropped — a measurement of a smaller
charset reported as if it were the charset is the quiet lie all of this exists
to avoid.
