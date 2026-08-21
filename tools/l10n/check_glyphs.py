"""Fail if any catalogue string needs a glyph the embedded font will not have.

This is the third of ADR-0010 §3's checks and the only one that is invisible
without a machine. A missing key is a visible bug; a duplicate key is a build
error; a Russian string containing a character outside the generated font subset
is a **blank space on the screen**, in one locale, on hardware, and nothing else
in the build notices.

It compares the catalogue against `tools/font/charset.py` — the same list the
font is generated from — rather than against a font file, because no font is
chosen yet (OPEN_QUESTIONS D16) and none is vendored. Transitivity does the
work: catalogue ⊆ charset, and the font is generated *from* charset, therefore
the font can draw the catalogue. `tools/font/check_coverage.py` guards the other
half of that, on the day there is a font to guard.

  python3 tools/l10n/check_glyphs.py
"""
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "font"))

from catalogue import REPO_ROOT, STRINGS_TOML, CatalogueError, load  # noqa: E402
import charset  # noqa: E402


def main(argv=None):
    path = argv[0] if argv else str(STRINGS_TOML)
    try:
        entries = load(path)
    except CatalogueError as exc:
        print(f"l10n: {exc}", file=sys.stderr)
        return 1

    available = set(charset.codepoints())

    # U+000A is a line break: LVGL starts a new line on it rather than drawing
    # anything, so asking the font for it reports the one character in the
    # string that is behaving. Every *other* control character is a real
    # problem — a stray tab in a label is a bug, not layout — so exactly one
    # codepoint is exempt and the rest are reported like any other.
    layout_only = {0x000A}

    missing = {}   # codepoint -> [(identifier, text), ...]
    used = set()
    for entry in entries:
        for text in entry.all_strings():
            for ch in text:
                if ord(ch) in layout_only:
                    continue
                used.add(ord(ch))
                if ord(ch) not in available:
                    missing.setdefault(ord(ch), []).append((entry.ident, text))

    if missing:
        print("l10n: the font subset cannot draw every catalogue string.\n", file=sys.stderr)
        for cp in sorted(missing):
            try:
                name = unicodedata.name(chr(cp))
            except ValueError:
                name = "unnamed"
            print(f"  U+{cp:04X} {chr(cp)!r}  {name}", file=sys.stderr)
            for ident, text in missing[cp][:3]:
                print(f"      {ident}: {text!r}", file=sys.stderr)
        print("\n  Two ways out, and they are not equivalent:\n"
              "    * rewrite the string with characters the subset has — free; or\n"
              "    * add the range to tools/font/charset.py, which changes the font,\n"
              "      which means the sizes in docs/research/FONT_MEASUREMENTS.md are no\n"
              "      longer measurements of what ships and have to be taken again.",
              file=sys.stderr)
        return 1

    print(f"l10n: {len(entries)} entries use {len(used)} distinct codepoints, "
          f"all within the {len(available)}-codepoint font subset "
          f"({Path(charset.__file__).relative_to(REPO_ROOT)})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
