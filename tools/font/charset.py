"""The exact set of characters Firefly's embedded fonts must be able to draw.

This file is the single definition. The font build reads it to subset, and the
localization check (ADR-0010 section 3) reads it to refuse a catalogue entry
containing a character no generated font has a glyph for. Two lists that were
meant to agree eventually do not; one list cannot disagree with itself.

Final section 51 forbids both extremes: no Latin-only-for-now, and no shipping
all of Unicode. Every range below is here because something in the product uses
it, and the comment says what.
"""

RANGES = [
    # --- Latin, digits and ASCII punctuation ------------------------------
    (0x0020, 0x007E, "Basic Latin: English UI, digits, ASCII punctuation"),

    # --- Latin-1 characters the UI actually uses --------------------------
    (0x00A0, 0x00A0, "no-break space: keeps a number and its unit together"),
    (0x00AB, 0x00AB, "guillemet open: Russian quotation marks"),
    (0x00BB, 0x00BB, "guillemet close: Russian quotation marks"),
    (0x00B0, 0x00B0, "degree: coordinates, bearing, temperature"),
    (0x00B1, 0x00B1, "plus-minus: GNSS accuracy, sensor tolerance"),
    (0x00B7, 0x00B7, "middle dot: the separator in GLOW - GUIDE - CONNECT"),
    (0x00D7, 0x00D7, "multiplication sign: 240x240 written properly"),

    # --- Cyrillic ---------------------------------------------------------
    (0x0401, 0x0401, "YO capital: outside the main block, and forgetting it is the classic bug"),
    (0x0410, 0x044F, "Cyrillic capitals and lowercase: the Russian catalogue"),
    (0x0451, 0x0451, "yo lowercase: same trap as U+0401"),

    # --- Typographic punctuation ------------------------------------------
    (0x2013, 0x2014, "en and em dash: the em dash is ordinary Russian punctuation"),
    (0x2018, 0x2019, "single quotes, and U+2019 as the English apostrophe"),
    (0x201C, 0x201D, "double quotes: English quotation"),
    (0x201E, 0x201E, "low double quote: the inner Russian quotation mark"),
    (0x2026, 0x2026, "ellipsis: truncation, and loading states"),

    # --- Symbols and units ------------------------------------------------
    (0x2116, 0x2116, "numero: the Russian number sign, and there is no ASCII substitute"),
    (0x2190, 0x2193, "four arrows: navigation and heading"),
]


def codepoints():
    out = []
    for lo, hi, _why in RANGES:
        out.extend(range(lo, hi + 1))
    return out


def as_lv_font_conv_ranges():
    """The --range arguments lv_font_conv wants, one string per range."""
    return [f"0x{lo:04X}-0x{hi:04X}" if lo != hi else f"0x{lo:04X}"
            for lo, hi, _why in RANGES]


if __name__ == "__main__":
    cps = codepoints()
    print(f"{len(cps)} codepoints in {len(RANGES)} ranges")
    print(" ".join(as_lv_font_conv_ranges()))
