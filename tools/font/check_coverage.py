"""Does this font file actually have a glyph for everything Attadipa draws?

Coverage is the check that eliminates a font outright, so it runs against the
font file that would be embedded rather than against a foundry's claim. A
missing glyph is not a rendering artefact on a wrist: it is a box, in the
middle of a sentence, in the user's own language.

Usage: python3 tools/font/check_coverage.py FONT.ttf [FONT.ttf ...]
Exit 0 if every font covers the whole charset.
"""
import sys, unicodedata
from fontTools.ttLib import TTFont

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from charset import RANGES, codepoints


def describe(cp):
    try:
        return unicodedata.name(chr(cp))
    except ValueError:
        return "?"


def check(path):
    font = TTFont(path, fontNumber=0, lazy=True)
    have = set()
    for table in font["cmap"].tables:
        have.update(table.cmap.keys())

    missing = [cp for cp in codepoints() if cp not in have]
    is_variable = "fvar" in font
    axes = ""
    if is_variable:
        axes = ", ".join(f"{a.axisTag} {a.minValue:g}..{a.maxValue:g} (default {a.defaultValue:g})"
                         for a in font["fvar"].axes)

    name = font["name"].getDebugName(4) or path
    print(f"\n{name}")
    print(f"  file            {path}")
    print(f"  glyphs in cmap  {len(have)}")
    print(f"  variable        {'yes — ' + axes if is_variable else 'no'}")
    if missing:
        print(f"  MISSING         {len(missing)} of {len(codepoints())}")
        for cp in missing:
            print(f"    U+{cp:04X} {chr(cp)!r} {describe(cp)}")
    else:
        print(f"  covers all {len(codepoints())} codepoints in {len(RANGES)} ranges")
    return not missing


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ok = all([check(p) for p in sys.argv[1:]])
    sys.exit(0 if ok else 1)
