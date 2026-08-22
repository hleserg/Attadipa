#!/usr/bin/env python3
"""Author the source masks in `ui/assets/source/icons/` from `icon_drawings.py`.

This is the *authoring* step and it is deliberately separate from the pipeline.
`generate_images.py` converts whatever is in `ui/assets/source/` and does not
care how it got there — a designer replacing one of these with a hand-drawn PNG
of the same name and size is a supported thing to do, and the pipeline would not
notice. Final §45's three directories are three stages for that reason.

Run it when a drawing changes. The result is committed; nothing in the build
invokes it.
"""

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import icon_drawings  # noqa: E402
import manifest  # noqa: E402

SOURCE_DIR = HERE.parent.parent / "ui" / "assets" / "source" / "icons"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="redraw into memory and compare, do not write")
    args = ap.parse_args()

    SOURCE_DIR.mkdir(parents=True, exist_ok=True)
    stale = []
    for name, size in manifest.assets():
        if size not in icon_drawings.GEOMETRY:
            print(f"draw_icons: no geometry authored for {size} px — refusing to "
                  f"synthesise one. Add it to icon_drawings.GEOMETRY.", file=sys.stderr)
            return 2
        img = icon_drawings.DRAWINGS[name](size)
        if img.size != (size, size):
            print(f"draw_icons: {name} drew {img.size}, expected {size}x{size}",
                  file=sys.stderr)
            return 2
        out = SOURCE_DIR / manifest.source_name(name, size)
        # A single-channel mask on disk is a greyscale PNG; LVGLImage.py reads
        # the alpha channel, so it is written as white-with-alpha instead.
        rgba = img.convert("L").point(lambda v: 255).convert("RGB")
        rgba.putalpha(img)
        if args.check:
            if not out.exists():
                stale.append(f"{out.name} missing")
                continue
            from PIL import Image
            have = Image.open(out).convert("RGBA")
            if have.tobytes() != rgba.convert("RGBA").tobytes():
                stale.append(f"{out.name} differs from its drawing")
        else:
            rgba.save(out, optimize=True)

    if args.check:
        if stale:
            for s in stale:
                print(f"draw_icons: {s}", file=sys.stderr)
            print("draw_icons: run `python3 tools/assets/draw_icons.py`", file=sys.stderr)
            return 1
        print(f"draw_icons: {len(list(manifest.assets()))} source mask(s) match their drawings")
        return 0

    print(f"draw_icons: wrote {len(list(manifest.assets()))} source mask(s) to {SOURCE_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
