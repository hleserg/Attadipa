#!/usr/bin/env python3
"""Refuse raw colours and raw lengths in screen code (T-009).

A design system that lives only in a document is a document. The tokens exist so
that a colour is a role and a gap is a Dp, and the way that stops being true is
not a redesign — it is one `lv_color_hex(0x2F3A2E)` written in a hurry on a
Friday, copied twice, and then nobody can change the palette any more.

So this is a boundary check of the same family as the two in tests/boundary/:
it does not ask whether the value is *right*, it asks whether the value is
*there*. One file is allowed to hold numbers and says why in its own source —
ui/src/color.cpp holds the palette.

It used to read one physical line at a time, and that made the invariant a
property of the *formatting* rather than of the code: `clang-format` with a
narrow enough column splits a call across four lines and the same raw pixel
count becomes invisible (issue #68). It also knew a hand-written list of setter
names, so `lv_obj_set_size(obj, 10, 20)` went through untouched.

Both are the same mistake — matching surface syntax instead of the call. So the
file is now read in three passes:

  1. **blank what is not code** — comment bodies and string/char literal bodies
     become spaces, keeping every offset and every newline, so a line number
     computed later still points at the right line;
  2. **take the whole call** — for each known LVGL entry point, walk forward
     balancing parentheses and split the argument list at top-level commas;
  3. **judge by position, not by shape** — argument 1 of a style setter is its
     value whether it was written on that line or four lines down.

That is a bounded tokenizer and deliberately not a C++ front end. It knows
nothing about types, overloads or the preprocessor, and it does not need to:
the question is whether an integer literal reached an LVGL length, and an
integer literal is visible without semantics.

Exit 0 when clean; 1 with one line per offence otherwise.
"""

from __future__ import annotations

import bisect
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Where screens are written. Everything under these is checked.
SCANNED = ("sim", "apps", "ui")

# The one file that is *supposed* to contain colour values, because being the
# palette is its job. It is listed by name rather than matched by a pattern, so
# that adding a second is a decision somebody makes in this file.
#
# Nothing else is exempted, and the list was pruned to get here: seven candidates
# were tried and six turned out to be exempt from a rule they never broke, which
# is not a courtesy but a hole. An exemption that is not load-bearing is removed.
ALLOWED = {
    "ui/src/color.cpp",   # the palette, transcribed from final §42
}

SUFFIXES = {".c", ".cpp", ".h", ".hpp"}


# ---------------------------------------------------------------------------
# The inventory
#
# Derived from LVGL v9.5.0 — the tag cmake/AttadipaLvgl.cmake pins and refuses
# to build without — by reading the generated headers rather than by
# remembering:
#
#   src/core/lv_obj_style_gen.h   every lv_obj_set_style_* and its value type
#   src/misc/lv_style_gen.h       the lv_style_set_* half of the same table
#   src/core/lv_obj_style.h       the static-inline convenience helpers
#   src/core/lv_obj_pos.h         geometry and alignment
#   src/core/lv_obj_scroll.h      scrolling
#   src/misc/lv_anim.h            animation timing
#   src/lv_api_map_v9_1.h         the older names that still compile
#
# It is written out rather than generated at check time on purpose: the
# simulator, and therefore LVGL, is OFF by default in this build, and a checker
# that only runs when an optional dependency was fetched is a checker that stops
# running. The cost is that a version bump has to come back here — which is why
# docs/research/DEPENDENCIES.md carries that as a step of the bump.
# ---------------------------------------------------------------------------

LENGTH = "length"
DURATION = "duration"

# Style properties whose value is a pixel count. Every `int32_t` property in
# lv_obj_style_gen.h that is a distance on the panel.
LENGTH_PROPERTIES = (
    "width", "min_width", "max_width",
    "height", "min_height", "max_height",
    "length",
    "x", "y",
    "transform_width", "transform_height",
    "transform_pivot_x", "transform_pivot_y",
    "translate_x", "translate_y", "translate_radial",
    "pad_top", "pad_bottom", "pad_left", "pad_right",
    "pad_row", "pad_column", "pad_radial",
    "margin_top", "margin_bottom", "margin_left", "margin_right",
    "radius", "radial_offset",
    "border_width", "outline_width", "outline_pad",
    "shadow_width", "shadow_spread", "shadow_offset_x", "shadow_offset_y",
    "blur_radius", "drop_shadow_radius",
    "drop_shadow_offset_x", "drop_shadow_offset_y",
    "line_width", "line_dash_width", "line_dash_gap",
    "arc_width",
    "text_letter_space", "text_line_space", "text_outline_stroke_width",
    # The static-inline helpers, which are not in the generated table but are
    # what people actually type — pad_all is in the reproducer on issue #68.
    "pad_all", "pad_hor", "pad_ver", "pad_gap",
    "margin_all", "margin_hor", "margin_ver",
)

# The other `int32_t` style properties, left alone deliberately. A number in one
# of these is not a pixel count, and refusing it would be the "broad exemption in
# reverse" — a rule that cries often enough to be turned off.
#
#   bg_main_stop, bg_grad_stop          0..255 along the gradient
#   grid_cell_column_pos / _span        track indices
#   grid_cell_row_pos / _span           track indices
#   transform_rotation                  tenths of a degree
#   transform_skew_x / _y               tenths of a degree
#   transform_scale_x / _y              256 is 1x
#   rotary_sensitivity  (uint32_t)      a ratio, also x256
#   flex_grow           (uint8_t)       a weight
#   every lv_opa_t property             0..255 alpha, and LVGL names both ends
#
# Also left alone: lv_timer_create()'s period. A timer tick is not a motion
# token — a clock that updates once a second is not making a design decision —
# and T-009's invariant is over the design system, not over every millisecond.

# Style properties whose value is a duration in milliseconds.
DURATION_PROPERTIES = ("anim_duration",)


def _style_entry_points() -> dict[str, tuple[tuple[int, str], ...]]:
    """Both halves of every property: on an object, and on a bare style."""
    table: dict[str, tuple[tuple[int, str], ...]] = {}
    for properties, kind in ((LENGTH_PROPERTIES, LENGTH),
                             (DURATION_PROPERTIES, DURATION)):
        for prop in properties:
            # lv_obj_set_style_x(obj, value, selector)
            table[f"lv_obj_set_style_{prop}"] = ((1, kind),)
            # lv_style_set_x(style, value)
            table[f"lv_style_set_{prop}"] = ((1, kind),)
    return table


# Which arguments of which call are a length or a duration, counting from zero.
# The object or the style is always argument 0, which is why nothing here is 0.
ENTRY_POINTS: dict[str, tuple[tuple[int, str], ...]] = {
    **_style_entry_points(),

    # Two lengths in one call — the shape the old single-value regex could not
    # express at all, and the shape half of issue #68 was about.
    "lv_obj_set_style_size":    ((1, LENGTH), (2, LENGTH)),   # (obj, w, h, sel)
    "lv_style_set_size":        ((1, LENGTH), (2, LENGTH)),   # (style, w, h)

    # Geometry: lv_obj_pos.h.
    "lv_obj_set_pos":            ((1, LENGTH), (2, LENGTH)),  # (obj, x, y)
    "lv_obj_set_size":           ((1, LENGTH), (2, LENGTH)),  # (obj, w, h)
    "lv_obj_set_x":              ((1, LENGTH),),
    "lv_obj_set_y":              ((1, LENGTH),),
    "lv_obj_set_width":          ((1, LENGTH),),
    "lv_obj_set_height":         ((1, LENGTH),),
    "lv_obj_set_content_width":  ((1, LENGTH),),
    "lv_obj_set_content_height": ((1, LENGTH),),
    # (obj, align, x_ofs, y_ofs) — the alignment is a name, the offsets are not.
    "lv_obj_align":              ((2, LENGTH), (3, LENGTH)),
    # (obj, base, align, x_ofs, y_ofs)
    "lv_obj_align_to":           ((3, LENGTH), (4, LENGTH)),

    # Scrolling: lv_obj_scroll.h. (obj, dx, dy, anim_en)
    "lv_obj_scroll_by":          ((1, LENGTH), (2, LENGTH)),
    "lv_obj_scroll_by_bounded":  ((1, LENGTH), (2, LENGTH)),
    "lv_obj_scroll_to":          ((1, LENGTH), (2, LENGTH)),
    "lv_obj_scroll_to_x":        ((1, LENGTH),),
    "lv_obj_scroll_to_y":        ((1, LENGTH),),

    # Animation timing: lv_anim.h. These are the v9 names.
    "lv_anim_set_duration":         ((1, DURATION),),
    "lv_anim_set_delay":            ((1, DURATION),),
    "lv_anim_set_reverse_duration": ((1, DURATION),),
    "lv_anim_set_reverse_delay":    ((1, DURATION),),
    "lv_anim_set_repeat_delay":     ((1, DURATION),),
    # And these are the v8 spellings that lv_api_map_v9_1.h still defines, so
    # they compile and have to be caught. The old checker knew *only* these
    # three, which meant the name a v9 screen would actually reach for —
    # lv_anim_set_duration — was the one it did not check.
    "lv_anim_set_time":             ((1, DURATION),),
    "lv_anim_set_playback_time":    ((1, DURATION),),
    "lv_anim_set_playback_delay":   ((1, DURATION),),
    # lv_anim_set_repeat_count is a count, not a duration, and stays out.
}

# One alternation over every name above. `\b` before it refuses
# `my_lv_obj_set_width(` because `_` is a word character, and the trailing
# `\s*\(` is what makes it a call rather than a mention.
CALL = re.compile(
    r"\b(" + "|".join(sorted(ENTRY_POINTS, key=len, reverse=True)) + r")\s*\(")

# A colour, written as one, in the four forms this codebase can produce it.
#
# Six hex digits is a colour in every UI codebase that has ever existed; four is
# usually a mask and is left alone. `Rgb{0xFF, 0xF6, 0xE8}` is the form the
# palette itself uses, and it is the form somebody copying a line out of
# color.cpp would paste. `lv_color_make` is LVGL's own three-channel constructor
# and means exactly the same thing.
#
# These run over the whole file rather than over one line, which is what lets
# the brace form be found when it is split across lines. Widening it that far
# needed the brace alternative tightened at the same time, or
# `Rgb make_colour()\n{\n    return palette[0];\n}` would match — a function
# definition read as a colour literal. Two bounds do it: no parenthesis may
# stand between the type name and the brace (a definition always has one, an
# aggregate initialiser never does), and neither half may run for long. A
# `;` or a `{` still stops the scan, as before.
COLOUR = re.compile(
    r"0[xX][0-9a-fA-F]{6}\b"
    r"|(?<!struct )(?<!class )\bRgb\b[^;{()]{0,120}\{[^}]{0,200}\d"
    r"|(?<!struct )(?<!class )\bRgb\s*\(\s*[+-]?\d"
    r"|\blv_color_make\s*\("
)

# How far the argument scan will walk before it gives up on a call. Four
# thousand characters is far past any real LVGL call and far short of a file, so
# hitting it means the source is not what it looks like — which is reported
# rather than skipped. A call the checker cannot read is a call it is not
# checking, and that is the failure this whole change is about.
MAX_CALL_CHARS = 4000

# An argument that is only an integer literal is a raw value. One that names
# anything — Metrics::px(dp_of(Space::Sm)), lv_pct(100), LV_SIZE_CONTENT, a
# constant — is not, and that is the line this check deliberately does not cross:
# a number in UI code is not automatically a pixel count, only a number handed to
# an LVGL length is.
INT_LITERAL = re.compile(r"[+-]?(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)[uUlL]*", re.ASCII)
# ...and neither is `240 / 2`, `(12)` or `12.5`, which name nothing either. An
# expression with no letter anywhere in it cannot be reading a token.
ARITHMETIC_ONLY = re.compile(r"[-+*/%().\s0-9]+", re.ASCII)
NON_ZERO_DIGIT = re.compile(r"[1-9]", re.ASCII)


class Offence:
    def __init__(self, path: str, line_no: int, quoted: str, why: str) -> None:
        self.path = path
        self.line_no = line_no      # where the *call* starts, not where the value sits
        self.quoted = quoted        # the whole call, collapsed onto one line
        self.why = why

    def __str__(self) -> str:
        return f"{self.path}:{self.line_no}: {self.why}\n    {self.quoted}"


class Source:
    """A file, its blanked twin, and the offset-to-line map between them."""

    def __init__(self, text: str) -> None:
        self.text = text
        self.code = blank_non_code(text)
        self.line_starts = [0] + [i + 1 for i, ch in enumerate(text) if ch == "\n"]

    def line_of(self, offset: int) -> int:
        """1-based line number of an offset — the *first* line of a call."""
        return bisect.bisect_right(self.line_starts, offset)

    def quote(self, start: int, end: int) -> str:
        """The source between two offsets, whole lines, collapsed to one."""
        first = self.line_starts[self.line_of(start) - 1]
        last = self.text.find("\n", end)
        last = len(self.text) if last < 0 else last
        quoted = " ".join(self.text[first:last].split())
        return quoted if len(quoted) <= 120 else quoted[:119] + "…"


def blank_non_code(text: str) -> str:
    """Replace comment and literal bodies with spaces.

    Length and newlines are preserved exactly, so an offset into the result
    addresses the same character of the original — which is what lets a
    diagnostic quote the real source after matching against the blanked copy.
    Doing it here rather than per line is what makes `/* ... */` and a string
    holding `lv_obj_set_size(obj, 10, 20)` both behave.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            end = text.find("\n", i)
            end = n if end < 0 else end
            _blank(out, i, end)
            i = end
        elif ch == "/" and nxt == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            _blank(out, i, end)
            i = end
        elif ch in "\"'":
            # `1'000` is a digit separator, not the start of a char literal.
            if ch == "'" and i and (text[i - 1].isalnum() or text[i - 1] == "_"):
                i += 1
                continue
            end = _end_of_literal(text, i, ch)
            if end is None:
                # An apostrophe with no partner before the newline is prose, and
                # swallowing the rest of the file over it would blind the check.
                i += 1
                continue
            _blank(out, i + 1, end)
            i = end + 1
        else:
            i += 1
    return "".join(out)


def _blank(out: list[str], start: int, end: int) -> None:
    for k in range(start, end):
        if out[k] != "\n":
            out[k] = " "


def _end_of_literal(text: str, start: int, quote: str) -> int | None:
    """Offset of the closing quote, or None if the line ends first."""
    i, n = start + 1, len(text)
    while i < n:
        ch = text[i]
        if ch == "\\":
            i += 2          # also carries a line continuation inside a string
            continue
        if ch == quote:
            return i
        if ch == "\n":
            return None
        i += 1
    return None


def call_arguments(code: str, open_paren: int) -> tuple[list[tuple[str, int]], int] | None:
    """Split the argument list opening at `code[open_paren]`.

    Returns the arguments with their offsets and the offset of the closing
    parenthesis, or None if the call does not close within MAX_CALL_CHARS.
    Nesting is counted over all three bracket kinds, so a comma inside
    `px(dp_of(a, b))` or `table[i, j]` is not an argument separator.

    Angle brackets are deliberately *not* counted, because `a < b, c > d` is
    two arguments and `foo<a, b>()` is one, and telling those apart is the
    ambiguity that needs a real parser. Every entry point in ENTRY_POINTS is
    from LVGL's C API and takes no template argument, so the case cannot arise
    at the position being read — and if it somehow did, the misaligned argument
    would name something and be passed over rather than misreported.
    """
    arguments: list[tuple[str, int]] = []
    depth = 0
    start = open_paren + 1
    limit = min(len(code), open_paren + MAX_CALL_CHARS)
    i = open_paren
    while i < limit:
        ch = code[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
            if depth == 0:
                arguments.append((code[start:i], start))
                return arguments, i
        elif ch == "," and depth == 1:
            arguments.append((code[start:i], start))
            start = i + 1
        i += 1
    return None


def raw_value(argument: str) -> str | None:
    """The literal this argument is, or None if it names something.

    Zero is not a length, it is the absence of one — and a zero delay is a
    decision to have no delay rather than a duration nobody chose. That holds
    for both kinds, which is why this takes no kind.
    """
    text = " ".join(argument.split())
    if not text:
        return None
    if INT_LITERAL.fullmatch(text):
        return None if _value_of(text) == 0 else text
    if ARITHMETIC_ONLY.fullmatch(text) and NON_ZERO_DIGIT.search(text):
        return text
    return None


def _value_of(literal: str) -> int:
    digits = literal.rstrip("uUlL")
    negative = digits.startswith("-")
    digits = digits.lstrip("+-")
    prefix = digits[:2].lower()
    if prefix == "0x":
        value = int(digits[2:], 16)
    elif prefix == "0b":
        value = int(digits[2:], 2)
    else:
        value = int(digits, 10)
    return -value if negative else value


def check_text(rel: str, text: str) -> list[Offence]:
    source = Source(text)
    offences: list[Offence] = []

    for match in COLOUR.finditer(source.code):
        offences.append(Offence(
            rel, source.line_of(match.start()),
            source.quote(match.start(), match.end()),
            "a colour written as a number — ask for a ColorRole"))

    for match in CALL.finditer(source.code):
        name = match.group(1)
        split = call_arguments(source.code, match.end() - 1)
        if split is None:
            offences.append(Offence(
                rel, source.line_of(match.start()),
                source.quote(match.start(), match.start()),
                f"{name}(...) does not close within {MAX_CALL_CHARS} characters, "
                f"so its arguments could not be read — a call this check cannot "
                f"parse is a call it is not checking"))
            continue
        arguments, close = split
        for index, kind in ENTRY_POINTS[name]:
            if index >= len(arguments):
                continue    # fewer arguments than the signature: not this call
            literal = raw_value(arguments[index][0])
            if literal is None:
                continue
            why = (f"{literal} px — a pixel count is a different physical size "
                   f"on each panel; use Metrics::px(dp_of(...))"
                   if kind == LENGTH else
                   "a duration written as a number — use milliseconds_of(Motion::…)")
            offences.append(Offence(rel, source.line_of(match.start()),
                                    source.quote(match.start(), close), why))

    offences.sort(key=lambda o: (o.line_no, o.why))
    return offences


def check(path: Path) -> list[Offence]:
    try:
        rel = path.relative_to(ROOT).as_posix()
    except ValueError:
        rel = path.as_posix()   # a self-test fixture in a scratch directory
    if rel in ALLOWED:
        return []
    return check_text(rel, path.read_text(encoding="utf-8"))


def relevant_files() -> list[Path]:
    out: list[Path] = []
    for top in SCANNED:
        base = ROOT / top
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in SUFFIXES and path.is_file():
                out.append(path)
    return out


def main(argv: list[str]) -> int:
    # Explicit paths are for the self-test, which needs to point the same rules
    # at a file that is deliberately wrong. Without them, the repository.
    if argv:
        files = [Path(a).resolve() for a in argv]
    else:
        files = relevant_files()
    if not files:
        print("check_raw_values: found nothing to check — the paths must have moved",
              file=sys.stderr)
        return 1

    offences = [o for path in files for o in check(path)]
    for offence in offences:
        print(offence, file=sys.stderr)

    if offences:
        print(f"\n{len(offences)} raw value(s) in screen code. The design system is "
              f"docs/ui/DESIGN_SYSTEM.md and its code half is ui/.", file=sys.stderr)
        return 1

    print(f"check_raw_values: {len(files)} file(s) clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
