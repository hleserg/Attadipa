#!/usr/bin/env python3
"""Render the committed source masks as a review sheet, day and night, 1:1.

The owner reviews design by looking at it, so an icon set needs a picture and
not a byte count. This draws every mask at its true pixel size on both theme
backgrounds, in the ink each theme actually uses, and writes the result beside
the typography specimens that already live in `docs/ui/specimens/`.

1:1 and not magnified, deliberately. An icon that only works at 3x is an icon
that does not work: the whole reason `icon_drawings.py` has a `GEOMETRY` entry
per size is that 33 pixels is a different drawing problem from 47, and a
magnified sheet hides exactly the collisions it should be showing.

The two inks come from `ui/src/color.cpp`, which is the only file in the
repository allowed to write a colour as a number — they are duplicated here
because this is a tool and not UI code, and the duplication is asserted by
`tools/assets/selftest.py`.
"""

import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import manifest  # noqa: E402

SOURCE_DIR = ROOT / "ui" / "assets" / "source" / "icons"
OUT = ROOT / "docs" / "ui" / "specimens" / "sheet-icons.png"

# color.background.primary / color.text.primary, both themes. ui/src/color.cpp.
DAY = ((0xFF, 0xF6, 0xE8), (0x2F, 0x3A, 0x2E))
NIGHT = ((0x2F, 0x3A, 0x2E), (0xFF, 0xF6, 0xE8))

CELL = 60
PAD = 12


def panel(bg, fg) -> Image.Image:
    names = sorted(manifest.ICONS)
    w = PAD + len(manifest.SIZES) * (CELL + PAD)
    h = PAD + len(names) * (CELL + PAD)
    im = Image.new("RGB", (w, h), bg)
    for row, name in enumerate(names):
        for col, size in enumerate(manifest.SIZES):
            src = SOURCE_DIR / manifest.source_name(name, size)
            with Image.open(src) as mask:
                alpha = mask.convert("RGBA").split()[3]
            x = PAD + col * (CELL + PAD) + (CELL - size) // 2
            y = PAD + row * (CELL + PAD) + (CELL - size) // 2
            im.paste(Image.new("RGB", (size, size), fg), (x, y), alpha)
    return im


def main() -> int:
    day = panel(*DAY)
    night = panel(*NIGHT)
    sheet = Image.new("RGB", (day.width + night.width, day.height))
    sheet.paste(day, (0, 0))
    sheet.paste(night, (day.width, 0))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT, optimize=True)
    print(f"contact_sheet: {OUT.relative_to(ROOT)} — {sheet.width}x{sheet.height}, "
          f"{len(sorted(manifest.ICONS))} icon(s) x {len(manifest.SIZES)} size(s), "
          f"day and night, 1:1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
