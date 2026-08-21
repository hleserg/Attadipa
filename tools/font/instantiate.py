"""Pin a variable font to one weight before it is converted.

Both candidate families ship from Google Fonts as variable fonts only, and
lv_font_conv reads the *default* instance. For Inter that happens to be
Regular 400; for Nunito Sans it is wght 200, ExtraLight -- a display weight that
nobody would choose for 14 px body text on a wrist. Converting the file as
downloaded silently ships the wrong font, and nothing in the pipeline complains,
because it is a perfectly valid font.

So the pipeline instantiates first and says which weight it pinned.

Usage: python3 tools/font/instantiate.py IN.ttf OUT.ttf wght=400 [opsz=14 ...]
"""
import shutil, sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__)
    src, dst, axes = argv[1], argv[2], argv[3:]
    location = {}
    for a in axes:
        tag, _, value = a.partition("=")
        location[tag] = float(value)

    font = TTFont(src)
    if "fvar" not in font:
        sys.exit(f"{src} is not a variable font -- nothing to instantiate.")

    available = {ax.axisTag: (ax.minValue, ax.defaultValue, ax.maxValue) for ax in font["fvar"].axes}
    for tag, value in location.items():
        if tag not in available:
            sys.exit(f"{src} has no '{tag}' axis. It has: {', '.join(available)}")
        lo, _default, hi = available[tag]
        if not lo <= value <= hi:
            sys.exit(f"{tag}={value:g} is outside this font's range {lo:g}..{hi:g}")

    defaults = {t: d for t, (_lo, d, _hi) in available.items()}
    changed = [f"{t}: {defaults[t]:g} -> {v:g}" for t, v in location.items() if defaults[t] != v]
    pinned = ", ".join(f"{t}={v:g}" for t, v in location.items())

    if not changed:
        # MEASURED, and the reason this branch exists rather than "it would be
        # a no-op anyway": instancing is NOT a no-op. Running Inter through
        # fontTools at its own default weight leaves GPOS in the file but
        # leaves lv_font_conv 1.5.3 unable to read any kerning out of it --
        # 1012 bytes of kern data at 20 px bpp 4 before, exactly zero after,
        # and optimize=False does not change that. Nunito Sans keeps its
        # kerning through the same step, so this is not a rule about fontTools
        # in general; it is a reason never to rewrite a font you did not need
        # to rewrite. See docs/research/FONT_MEASUREMENTS.md.
        shutil.copyfile(src, dst)
        print(f"{src}\n  -> {dst}\n  pinned {pinned}\n"
              f"  copied unchanged: {pinned} is already this font's default instance,\n"
              f"  and rewriting it can cost kerning that lv_font_conv would otherwise find")
        return

    instancer.instantiateVariableFont(font, location, inplace=True, updateFontNames=True)
    font.save(dst)
    print(f"{src}\n  -> {dst}\n  pinned {pinned}\n  name  {TTFont(dst)['name'].getDebugName(4)}")
    print(f"  moved off the default instance: {'; '.join(changed)}")


if __name__ == "__main__":
    main(sys.argv)
