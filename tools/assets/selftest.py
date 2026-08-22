#!/usr/bin/env python3
"""Prove the asset pipeline's refusals actually refuse.

A guard that has never been triggered is a comment. Each case below builds the
mistake it is meant to catch, runs the real tool against it, and fails if the
tool accepted it — the same discipline `tools/ui/selftest.py` applies to the
raw-value checker.

Nothing here touches the repository's own generated tree: every case works in a
temporary directory, and the tool's paths are monkey-patched to point at it.
"""

import sys
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import contact_sheet  # noqa: E402
import generate_images as gi  # noqa: E402
import icon_drawings  # noqa: E402
import manifest  # noqa: E402

FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print(f"  ok    {name}")
    else:
        print(f"  FAIL  {name} {detail}")
        FAILURES.append(name)


def expect_refusal(name, fn):
    try:
        fn()
    except SystemExit as exc:
        check(name, True)
        return str(exc)
    check(name, False, "— the tool accepted it")
    return ""


def main() -> int:
    print("must refuse:")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        # A desktop-sized illustration. This is the 1440-pixel concept sheet
        # case, which final §41 says is never compiled into firmware.
        big = tmp / "hero.png"
        Image.new("RGBA", (1440, 1086)).save(big)
        msg = expect_refusal("an image larger than the cap", lambda: gi.check_source(big))
        check("  and it says why", "1440x1086" in msg and "86" in msg)

        # An asset drawn from the reference directory itself.
        ref = ROOT / "docs" / "ui" / "reference" / "lumar_mascot_sheet.png"
        if ref.exists():
            expect_refusal("a source under docs/", lambda: gi.check_source(ref))
        brand = ROOT / "pics" / "Ikon.png"
        if brand.exists():
            expect_refusal("a source under pics/", lambda: gi.check_source(brand))

        # A size the manifest asks for with no source file behind it. The
        # pipeline must never resample a neighbour into it.
        real_source = gi.SOURCE_DIR
        try:
            gi.SOURCE_DIR = tmp / "empty"
            (tmp / "empty").mkdir()
            expect_refusal("a size with no source",
                           lambda: gi.convert("mesh", manifest.SIZES[0]))
        finally:
            gi.SOURCE_DIR = real_source

        # A file whose name claims a size its pixels do not have.
        try:
            liar = tmp / "liar"
            liar.mkdir()
            gi.SOURCE_DIR = liar
            Image.new("RGBA", (12, 12)).save(liar / manifest.source_name("mesh", 33))
            msg = expect_refusal("a name that disagrees with the pixels",
                                 lambda: gi.convert("mesh", 33))
            check("  and it says which to fix", "the contract" in msg)
        finally:
            gi.SOURCE_DIR = real_source

    print("must accept:")

    # Every size the manifest generates has an authored geometry. This is the
    # rule that keeps §86 mechanical: no geometry, no asset, no resample.
    for size in manifest.SIZES:
        check(f"{size} px has authored geometry", size in icon_drawings.GEOMETRY)

    # Every drawing produces a square mask of exactly the size asked for, with
    # ink in it. A drawing that silently returned a blank canvas would generate,
    # compile, link and show nothing.
    for name, size in manifest.assets():
        img = icon_drawings.DRAWINGS[name](size)
        check(f"{name} at {size} px is {size}x{size}", img.size == (size, size))
        hist = img.histogram()
        opaque = sum(hist[200:])
        clear = hist[0]
        total = size * size
        check(f"{name} at {size} px has ink",
              0.04 * total < opaque < 0.60 * total,
              f"— {opaque}/{total} opaque")
        check(f"{name} at {size} px has air", clear > 0.20 * total,
              f"— {clear}/{total} clear")
        check(f"{name} at {size} px is antialiased",
              any(hist[16:200]), "— every pixel is fully on or fully off")

    # The manifest's px() must agree with Metrics::px in C++, which is what
    # makes an asset's pixel size the same number the layout will ask for.
    check("px(0) is 0", manifest.px(0, 315) == 0)
    check("px never rounds a positive dp to zero", manifest.px(1, 1) == 1)
    check("px rounds to nearest", manifest.px(20, 261) == 33 and manifest.px(24, 261) == 39)
    check("the 39 px collision is real",
          manifest.px(24, 261) == manifest.px(20, 315) == 39)

    # The contact sheet's inks are the ones color.cpp defines. A sheet drawn in
    # the wrong ink would be a review of a screen nobody ships.
    src = (ROOT / "ui" / "src" / "color.cpp").read_text(encoding="utf-8")
    for label, rgb in (("Warm Ivory", (0xFF, 0xF6, 0xE8)), ("Ink Olive", (0x2F, 0x3A, 0x2E))):
        literal = "{0x%02X, 0x%02X, 0x%02X}" % rgb
        check(f"the sheet's {label} is color.cpp's", literal in src, f"— {literal}")
        check(f"  and the sheet uses it",
              rgb in (contact_sheet.DAY + contact_sheet.NIGHT))

    print(f"\nselftest: {len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
