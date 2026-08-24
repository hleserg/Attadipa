# Font tooling

The scripts, in the order they run.

| | What it is for |
|---|---|
| [`charset.py`](charset.py) | **the** definition of what Attadipa's fonts must be able to draw. 181 codepoints, 18 ranges, a recorded reason per range |
| [`check_coverage.py`](check_coverage.py) | does this font file have a glyph for every one of them? The check that eliminates a font |
| [`instantiate.py`](instantiate.py) | pin a variable font to one weight — and refuse to rewrite it when it is already at that weight |
| [`measure.py`](measure.py) | generate, compile for ESP32-S3, and report `.rodata`. The flash cost, measured |
| [`contact_sheet.py`](contact_sheet.py) | render the glyphs LVGL will actually draw, at the size it will draw them, with the advance, side bearings and kerning it will use, in both themes |
| [`fetch_ttf.py`](fetch_ttf.py) | one 243 kB file out of the pinned LVGL commit, hash-checked, instead of the 350 MiB clone |
| [`generate_ui_fonts.py`](generate_ui_fonts.py) | the four committed subsets the simulator links, and the `--check` every host CI job runs |

Results: [FONT_MEASUREMENTS](../../docs/research/FONT_MEASUREMENTS.md).

## The committed subsets, and what guards them

`assets/fonts/generated/` holds four `.c` files that ship in flash, and
`INPUTS.sha256` beside them binds two different things:

* the **inputs** — the charset, the sizes, the bit depth, the source TTF's
  SHA-256, the pinned converter version and the exact banner text;
* the **outputs** — a SHA-256 per committed file.

The second half was missing until issue #69, and its absence was not theoretical:
a hand-edited byte in a generated font passed `--check` green, because the check
compared inputs and then counted filenames. The format is
[`tools/integrity/stamp.py`](../integrity/stamp.py), shared with the image
pipeline, and only a generator writes one — there is no "re-stamp what is on
disk" mode, because a tool that blesses whatever bytes it finds is the hole
itself wearing a maintenance hat.

**The `Opts:` line is rewritten on the way out.** lv_font_conv records its own
argv in a comment, so the first generation baked one developer's
`/mnt/e/projects/...` into a shipping asset — and a fresh generation anywhere
else then differed in bytes while being identical in every glyph. It is
normalized to logical paths, which is what makes a byte-for-byte check possible
at all; `tools/integrity/reproducibility.py` proves it by generating from two
different absolute paths, and is waiting on a permission to run in CI —
[`tools/integrity/README.md`](../integrity/README.md).

**The converter is checked before it is used.** `--version` must report the
pinned 1.5.3, because every generated file's banner claims that version and a
bump changes bytes that ship in flash.

## What has to be installed

```bash
pip install fonttools pillow           # coverage, instancing, contact sheets
npm install --no-save lv_font_conv@1.5.3
```

Neither is needed to *check* the committed fonts — that is the point of
committing them. `python3 tools/font/generate_ui_fonts.py --check` runs on a
machine with no Node and no configured build, and it is what
`ui_fonts_are_current` runs in every host CI job.

To rebuild them, which is the only way to update the stamp:

```bash
npm install --no-save lv_font_conv@1.5.3
python3 tools/font/fetch_ttf.py --out /tmp/Montserrat-Medium.ttf
python3 tools/font/generate_ui_fonts.py \
        --ttf /tmp/Montserrat-Medium.ttf --converter ./node_modules/.bin/lv_font_conv
```

Passing both arguments to `--check` instead runs the expensive comparison: a
fresh generation into a temporary directory, byte-compared against what is
committed. That path used to be unusable — it reported all four files as
differing on any machine but one — and now passes.

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
