#!/usr/bin/env python3
"""Measure how wide a string will actually be drawn, from the shipped font.

`measure.py` answers "what does this font cost in flash". This answers the other
question a layout needs: **will this string fit**. It is the difference between
a design review that says "the Russian date looks long" and one that says it is
N pixels wide in a box of M.

It reads the generated `assets/fonts/generated/attadipa_nunito_sans_*.c` — the
exact bytes the firmware and the simulator link — and reimplements LVGL v9.5.0's
own advance-width arithmetic against them. Nothing here is an estimate of a
renderer's behaviour; it is that renderer's integer arithmetic, transcribed with
the line numbers it came from:

  * `src/font/fmt_txt/lv_font_fmt_txt.c:245-253`

        int32_t kv = ((int32_t)((int32_t)kvalue * fdsc->kern_scale) >> 4);
        uint32_t adv_w = gdsc->adv_w;
        adv_w += kv;
        adv_w  = (adv_w + (1 << 3)) >> 4;

    so an advance is stored in 1/16 px, kerned first and rounded to whole pixels
    afterwards. Rounding per glyph rather than per string is why a string is not
    the sum of its letters' rounded widths, and why measuring by adding up a
    specimen sheet gives a different — wrong — answer.

  * `src/misc/lv_text.c`, `lv_text_get_width()`: `letter_space` is added after
    every glyph and then subtracted once at the end, so N glyphs carry N-1 gaps.

  * `src/font/fmt_txt/lv_font_fmt_txt.c`, `get_kern_value()`: class kerning is
    `class_pair_values[(left - 1) * right_class_cnt + (right - 1)]`, and class 0
    on either side means no kerning for that pair.

  * `get_glyph_dsc_id()`: FORMAT0_TINY is a linear range, SPARSE_TINY is a
    binary search in a `unicode_list` of *relative* code points.

Verify with `--self-test`, which checks the parse against invariants the
generated file states about itself rather than against numbers typed in here.

Usage:
  python3 tools/font/measure_strings.py --clock
  python3 tools/font/measure_strings.py --size 28 --string '23 сентября 2026'
  python3 tools/font/measure_strings.py --digits
"""

from __future__ import annotations

import argparse
import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FONT_DIR = ROOT / "assets" / "fonts" / "generated"

# Kept in step with tools/font/generate_ui_fonts.py; a size it does not generate
# is a size nothing can be measured at.
SIZES = (14, 16, 20, 28)

FORMAT0_TINY = "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY"
SPARSE_TINY = "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY"


class ParseError(RuntimeError):
    pass


def _int_array(source: str, name: str) -> list[int]:
    """Every integer in the initialiser of `name`, in order."""
    match = re.search(
        rf"{re.escape(name)}\s*\[\s*\]\s*=\s*\{{(.*?)\}}\s*;", source, re.DOTALL
    )
    if match is None:
        raise ParseError(f"no array named {name}")
    body = re.sub(r"/\*.*?\*/", " ", match.group(1), flags=re.DOTALL)
    return [int(token, 0) for token in re.findall(r"-?(?:0x[0-9a-fA-F]+|\d+)", body)]


def _scalar(source: str, name: str) -> int:
    match = re.search(rf"\.{re.escape(name)}\s*=\s*(-?\d+)", source)
    if match is None:
        raise ParseError(f"no field .{name}")
    return int(match.group(1))


class Font:
    """One generated LVGL font, read well enough to measure text with."""

    def __init__(self, path: Path):
        self.path = path
        source = path.read_text(encoding="utf-8")

        self.size_px = self._opt_int(source, "--size")
        self.bpp = self._opt_int(source, "--bpp")

        self.advances = self._glyph_advances(source)
        self.cmaps = self._cmaps(source, self._unicode_lists(source))

        self.kern_scale = _scalar(source, "kern_scale")
        self.kern_classes = _scalar(source, "kern_classes")
        if self.kern_classes:
            self.left_class = _int_array(source, "kern_left_class_mapping")
            self.right_class = _int_array(source, "kern_right_class_mapping")
            self.class_values = _int_array(source, "kern_class_values")
            self.right_class_cnt = _scalar(source, "right_class_cnt")
            self.left_class_cnt = _scalar(source, "left_class_cnt")

    @staticmethod
    def _opt_int(source: str, flag: str) -> int:
        """Read a converter flag out of the `Opts:` banner lv_font_conv writes."""
        match = re.search(rf"{re.escape(flag)}\s+(\d+)", source)
        if match is None:
            raise ParseError(f"no {flag} in the generated banner")
        return int(match.group(1))

    @staticmethod
    def _glyph_advances(source: str) -> list[int]:
        match = re.search(
            r"glyph_dsc\s*\[\s*\]\s*=\s*\{(.*?)\n\};", source, re.DOTALL
        )
        if match is None:
            raise ParseError("no glyph_dsc[]")
        return [int(v) for v in re.findall(r"\.adv_w\s*=\s*(\d+)", match.group(1))]

    @staticmethod
    def _unicode_lists(source: str) -> dict[str, list[int]]:
        return {
            name: _int_array(source, name)
            for name in re.findall(r"static const uint16_t (unicode_list_\d+)\s*\[", source)
        }

    @staticmethod
    def _cmaps(source: str, lists: dict[str, list[int]]) -> list[dict]:
        match = re.search(r"cmaps\s*\[\s*\]\s*=\s*\n?\{(.*?)\n\};", source, re.DOTALL)
        if match is None:
            raise ParseError("no cmaps[]")
        out = []
        for entry in re.findall(r"\{(.*?)\}", match.group(1), re.DOTALL):
            kind = SPARSE_TINY if SPARSE_TINY in entry else FORMAT0_TINY
            if kind == FORMAT0_TINY and FORMAT0_TINY not in entry:
                raise ParseError(f"unsupported cmap type in {entry!r}")
            name = re.search(r"\.unicode_list\s*=\s*(\w+)", entry)
            out.append(
                {
                    "start": int(re.search(r"\.range_start\s*=\s*(\d+)", entry).group(1)),
                    "length": int(re.search(r"\.range_length\s*=\s*(\d+)", entry).group(1)),
                    "gid_start": int(re.search(r"\.glyph_id_start\s*=\s*(\d+)", entry).group(1)),
                    "type": kind,
                    "list": lists.get(name.group(1)) if name else None,
                }
            )
        return out

    # -- the arithmetic ---------------------------------------------------

    def glyph_id(self, codepoint: int) -> int:
        """`get_glyph_dsc_id()`. 0 means the font cannot draw this character."""
        if codepoint == 0:
            return 0
        for cmap in self.cmaps:
            relative = codepoint - cmap["start"]
            if relative < 0 or relative >= cmap["length"]:
                continue
            if cmap["type"] == FORMAT0_TINY:
                return cmap["gid_start"] + relative
            entries = cmap["list"]
            index = bisect.bisect_left(entries, relative)
            if index < len(entries) and entries[index] == relative:
                return cmap["gid_start"] + index
            return 0
        return 0

    def kern_value(self, left_gid: int, right_gid: int) -> int:
        if not left_gid or not right_gid or not self.kern_classes:
            return 0
        left = self.left_class[left_gid] if left_gid < len(self.left_class) else 0
        right = self.right_class[right_gid] if right_gid < len(self.right_class) else 0
        if left <= 0 or right <= 0:
            return 0
        return self.class_values[(left - 1) * self.right_class_cnt + (right - 1)]

    def advance(self, codepoint: int, next_codepoint: int = 0) -> int:
        """One glyph's advance in whole pixels, kerned against its successor."""
        gid = self.glyph_id(codepoint)
        if gid == 0:
            return 0
        kvalue = self.kern_value(gid, self.glyph_id(next_codepoint))
        # >> on a negative int is an arithmetic shift in both C and Python, so
        # this is the same rounding-toward-minus-infinity LVGL performs.
        kv = (kvalue * self.kern_scale) >> 4
        return (self.advances[gid] + kv + (1 << 3)) >> 4

    def width(self, text: str, letter_space: int = 0) -> int:
        """`lv_text_get_width()` for a single line."""
        total = 0
        for index, character in enumerate(text):
            following = ord(text[index + 1]) if index + 1 < len(text) else 0
            glyph_width = self.advance(ord(character), following)
            if glyph_width > 0:
                total += glyph_width + letter_space
        return total - letter_space if total > 0 else 0

    def undrawable(self, text: str) -> list[str]:
        return [c for c in text if c != "\0" and self.glyph_id(ord(c)) == 0]


def load(size: int) -> Font:
    path = FONT_DIR / f"attadipa_nunito_sans_{size}.c"
    if not path.exists():
        raise SystemExit(f"{path} does not exist. Generated sizes: {SIZES}")
    return Font(path)


# The two panels, from platform/boards. Written here rather than imported
# because this is a Python tool and those are C++ headers; a disagreement shows
# up as a wrong verdict, so both numbers are quoted in the output.
PANELS = (("T-Watch S3", 240, 240), ("Waveshare 2.06", 410, 502))

# Representative Clock strings. **Candidates for measurement, not a decided
# format** — T-037 has not chosen one, and neither does this file.
#
# Each is the *widest* member of its class rather than a typical one, because a
# layout bound is decided by the worst case and a typical string tells you only
# that the good case fits. Where the widest member is not obvious by eye, it is
# computed instead of chosen — see --time-span.
#
# Russian weekday and month names are lower case, which is the correct Russian
# convention and not an oversight; a standalone line may capitalise the first
# letter, which does not change the width materially in this face.
# The months are genitive ("30 сентября"), because that is the form a Russian
# date takes. A nominative table ("сентябрь") is the classic bug and is also
# two characters longer, so measuring the genitive is the conservative choice
# in grammar and the honest one in pixels.
CLOCK_STRINGS = [
    ("time 24 h, widest", "00:00"),
    ("time 24 h, narrowest", "11:11"),
    ("time 12 h, widest", "10:08 PM"),
    ("time, unset/invalid", "--:--"),
    ("date EN, long", "Wednesday, 30 September"),
    ("date EN, long + year", "Wednesday, 30 September 2026"),
    ("date EN, medium", "Wed 30 Sep 2026"),
    ("date EN, short", "30 Sep"),
    ("date RU, long", "понедельник, 30 сентября"),
    ("date RU, long + year", "понедельник, 30 сентября 2026"),
    ("date RU, medium", "пн 30 сент. 2026"),
    ("date RU, short", "30 сент."),
    ("battery, three digits", "100%"),
    ("battery, one digit", "9%"),
    ("battery, unknown", "—%"),
    ("battery, unknown dash", "—"),
    ("charging label EN", "Charging"),
    ("charging label RU", "Зарядка"),
]

# Every weekday and month name, so the widest date is found rather than picked.
WEEKDAYS = {
    "EN": ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"],
    "RU": ["понедельник", "вторник", "среда", "четверг", "пятница", "суббота",
           "воскресенье"],
}
MONTHS = {
    "EN": ["January", "February", "March", "April", "May", "June", "July",
           "August", "September", "October", "November", "December"],
    "RU": ["января", "февраля", "марта", "апреля", "мая", "июня", "июля",
           "августа", "сентября", "октября", "ноября", "декабря"],
}


def report_clock(letter_space: int) -> int:
    print(f"letter_space = {letter_space} px\n")
    for size in SIZES:
        font = load(size)
        print(f"== attadipa_nunito_sans_{size} ({font.size_px} px, {font.bpp} bpp) ==")
        print(f"{'string':<26} {'px':>5}  " + "  ".join(f"{n} {w}px" for n, w, _ in PANELS))
        for label, text in CLOCK_STRINGS:
            missing = font.undrawable(text)
            if missing:
                print(f"{label:<26} {'—':>5}  UNDRAWABLE: {' '.join(missing)}")
                continue
            width = font.width(text, letter_space)
            fits = "  ".join(
                f"{name} {100 * width // panel_w:>3}%" for name, panel_w, _ in PANELS
            )
            print(f"{label:<26} {width:>5}  {fits}")
        print()
    return 0


def report_time_span(letter_space: int) -> int:
    """How far a centred time moves as the minute changes.

    Every one of the 1440 minutes in a day is measured, in both clock formats,
    so the extremes are found rather than guessed at. The number that matters is
    not the widest string: it is *widest minus narrowest*, because that is how
    far a centre-aligned or right-aligned time slides on the panel over a day —
    motion nobody asked for, once a minute, on the one screen that is always on.
    """
    for size in SIZES:
        font = load(size)
        print(f"== attadipa_nunito_sans_{size} ==")
        for label, render in (
            ("24 h  HH:MM", lambda h, m: f"{h:02d}:{m:02d}"),
            ("12 h  H:MM AM", lambda h, m: f"{(h % 12) or 12}:{m:02d} {'AM' if h < 12 else 'PM'}"),
        ):
            widths = {}
            for hour in range(24):
                for minute in range(60):
                    text = render(hour, minute)
                    widths[text] = font.width(text, letter_space)
            widest = max(widths, key=widths.get)
            narrowest = min(widths, key=widths.get)
            span = widths[widest] - widths[narrowest]
            print(f"  {label}: widest {widest!r} {widths[widest]} px, "
                  f"narrowest {narrowest!r} {widths[narrowest]} px")
            for name, panel_w, _ in PANELS:
                print(f"      span {span} px = {100 * span / panel_w:.1f}% of {name} "
                      f"({panel_w} px); centred, it moves {span / 2:.1f} px each side")
        print()
    return 0


def report_dates(letter_space: int) -> int:
    """The widest date in each language, found by measuring all of them."""
    forms = {
        "long   'Wednesday, 30 September'": "{weekday}, 30 {month}",
        "long+y 'Wednesday, 30 September 2026'": "{weekday}, 30 {month} 2026",
        "no weekday '30 September 2026'": "30 {month} 2026",
    }
    for size in SIZES:
        font = load(size)
        print(f"== attadipa_nunito_sans_{size} ==")
        for form_label, template in forms.items():
            row = []
            for language in ("EN", "RU"):
                candidates = {
                    template.format(weekday=w, month=m): 0
                    for w in WEEKDAYS[language]
                    for m in MONTHS[language]
                }
                for text in candidates:
                    candidates[text] = font.width(text, letter_space)
                widest = max(candidates, key=candidates.get)
                row.append((language, widest, candidates[widest]))
            print(f"  {form_label}")
            for language, text, width in row:
                over = "  ".join(
                    f"{name} {100 * width // panel_w}%" for name, panel_w, _ in PANELS
                )
                print(f"    {language} {width:>4} px  {over}   {text!r}")
            wider = max(row, key=lambda r: r[2])[0]
            print(f"    wider language: {wider}")
        print()
    return 0


def report_digits() -> int:
    """Are the figures tabular? A watchface that jitters every minute is a bug."""
    for size in SIZES:
        font = load(size)
        widths = {d: font.advance(ord(d)) for d in "0123456789"}
        raw = {d: font.advances[font.glyph_id(ord(d))] for d in "0123456789"}
        distinct = sorted(set(widths.values()))
        print(f"== attadipa_nunito_sans_{size} ==")
        print("  px  : " + "  ".join(f"{d}={widths[d]}" for d in "0123456789"))
        print("  1/16: " + "  ".join(f"{d}={raw[d]}" for d in "0123456789"))
        verdict = "TABULAR" if len(distinct) == 1 else f"PROPORTIONAL, {distinct} px"
        span = max(widths.values()) - min(widths.values())
        print(f"  figures: {verdict}; widest-narrowest = {span} px")
        colon = font.advance(ord(":"))
        print(f"  ':' = {colon} px")
        print()
    return 0


def self_test() -> int:
    """Check the parse against what the file says about itself."""
    failures = []
    for size in SIZES:
        font = load(size)
        if font.size_px != size:
            failures.append(f"{size}: banner says --size {font.size_px}")
        if font.kern_scale != 16:
            failures.append(f"{size}: kern_scale {font.kern_scale}, arithmetic assumes 16")
        # Every codepoint charset.py asks for must resolve to a real glyph. That
        # is the same invariant check_coverage.py enforces at generation time,
        # asked here of the parse rather than of the font.
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import charset  # noqa: PLC0415

        missing = [cp for cp in charset.codepoints() if font.glyph_id(cp) == 0]
        if missing:
            failures.append(
                f"{size}: {len(missing)} codepoint(s) unresolved, first "
                f"U+{missing[0]:04X}"
            )
        # glyph ids are dense and every cmap must land inside glyph_dsc[].
        for cmap in font.cmaps:
            span = cmap["length"] if cmap["type"] == FORMAT0_TINY else len(cmap["list"])
            top = cmap["gid_start"] + span - 1
            if top >= len(font.advances):
                failures.append(f"{size}: cmap runs to gid {top}, glyph_dsc has {len(font.advances)}")
        # A space has to advance, or every measured string is wrong.
        if font.advance(ord(" ")) <= 0:
            failures.append(f"{size}: space advances {font.advance(ord(' '))} px")
        # Kerning must be reachable: the file ships a class table, so at least
        # one pair in it must be non-zero, or the parse found the wrong array.
        if font.kern_classes and not any(v != 0 for v in font.class_values):
            failures.append(f"{size}: kern_class_values are all zero")
        # …and the table has to be the shape the file says it is. This is the
        # check that catches a regex picking up the wrong array: the values are
        # a left_class_cnt x right_class_cnt matrix, and get_kern_value()
        # indexes straight into it, so a wrong length is a silent wrong answer
        # rather than an error.
        if font.kern_classes:
            expected = font.left_class_cnt * font.right_class_cnt
            if len(font.class_values) != expected:
                failures.append(
                    f"{size}: kern_class_values has {len(font.class_values)} entries, "
                    f"{font.left_class_cnt} x {font.right_class_cnt} = {expected}"
                )
            # A class mapping is indexed by glyph id, so it must cover them all.
            for name, mapping in (("left", font.left_class), ("right", font.right_class)):
                if len(mapping) < len(font.advances) - 1:
                    failures.append(
                        f"{size}: kern_{name}_class_mapping covers {len(mapping)} of "
                        f"{len(font.advances) - 1} glyphs"
                    )

    for line in failures:
        print(f"FAIL {line}")
    if failures:
        return 1
    print(f"ok — {len(SIZES)} fonts parsed, coverage and kerning tables reachable")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clock", action="store_true", help="measure the Clock string set")
    parser.add_argument("--digits", action="store_true", help="are the figures tabular?")
    parser.add_argument("--time-span", action="store_true",
                        help="how far a centred time moves over all 1440 minutes")
    parser.add_argument("--dates", action="store_true",
                        help="the widest date in each language, over every weekday and month")
    parser.add_argument("--self-test", action="store_true", help="check the parse")
    parser.add_argument("--size", type=int, choices=SIZES, help="font size for --string")
    parser.add_argument("--string", help="measure one string")
    parser.add_argument("--letter-space", type=int, default=0)
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()
    if arguments.digits:
        return report_digits()
    if arguments.time_span:
        return report_time_span(arguments.letter_space)
    if arguments.dates:
        return report_dates(arguments.letter_space)
    if arguments.string is not None:
        if arguments.size is None:
            raise SystemExit("--string needs --size")
        font = load(arguments.size)
        missing = font.undrawable(arguments.string)
        if missing:
            print(f"UNDRAWABLE in this font: {' '.join(missing)}")
            return 1
        print(font.width(arguments.string, arguments.letter_space))
        return 0
    return report_clock(arguments.letter_space)


if __name__ == "__main__":
    sys.exit(main())
