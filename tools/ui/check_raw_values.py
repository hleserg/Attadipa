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
the question is whether a numeric literal reached an LVGL length, and a numeric
literal is visible without semantics.

Two things were still curated by hand after that, and a follow-up review of the
same issue found both. The *list of entry points* had never been held against
LVGL's headers — `lv_obj_set_ext_click_area` was missing from a list whose own
comment named the file that declares it — and the *test for a literal* was "an
integer literal, or no letter anywhere", which is not what C++ calls a number:
`1'2`, `12.0f` and `0x10 / 2` all went through. So the fourth and fifth passes
are not passes at all, they are where the two answers come from:

  4. the entry points live in lvgl_inventory.py and are held against the pinned
     headers by check_inventory.py, which fails on anything unclassified;
  5. an argument is tokenised with C++'s own preprocessing-number production, so
     a hex prefix and a float suffix are inside the number rather than mistaken
     for a name.

Exit 0 when clean; 1 with one line per offence otherwise.
"""

from __future__ import annotations

import bisect
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from lvgl_inventory import (COLOUR, DURATION, ENTRY_POINTS,   # noqa: E402
                            LENGTH)

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
# Which LVGL entry points take a length, a duration or a colour lives in
# tools/ui/lvgl_inventory.py, next door. It is written down rather than read out
# of LVGL's headers at check time, on purpose: the simulator — and therefore
# LVGL — is OFF by default, so a scan that needs LVGL on disk is a scan that
# stops running on most CI jobs.
#
# A written-down list is checked by example, and that is how issue #68's
# follow-up found lv_obj_set_ext_click_area missing from a list whose comment
# said it had been read out of the header that declares it. So
# tools/ui/check_inventory.py holds that file against the pinned headers
# wherever they *are* available — the simulator build — and fails on anything
# unclassified, in either direction. Neither half is optional: this one costs
# nothing and runs everywhere, that one proves it still describes LVGL.
# ---------------------------------------------------------------------------


# One alternation over every name above. `\b` before it refuses
# `my_lv_obj_set_width(` because `_` is a word character, and the trailing
# `\s*\(` is what makes it a call rather than a mention.
CALL = re.compile(
    r"\b(" + "|".join(sorted(ENTRY_POINTS, key=len, reverse=True)) + r")\s*\(")

# A colour, written as one, in the forms this codebase can produce *without*
# naming a function — the ones the inventory cannot reach.
#
# Six hex digits is a colour in every UI codebase that has ever existed; four is
# usually a mask and is left alone. `Rgb{0xFF, 0xF6, 0xE8}` is the form the
# palette itself uses, and it is the form somebody copying a line out of
# color.cpp would paste.
#
# LVGL's own constructors used to be a fourth alternative here, matching
# `lv_color_make(` and refusing the call whatever was in it — so
# `lv_color_make(red, green, blue)` was refused for naming three roles. They are
# in lvgl_inventory.py now, argument by argument, which both fixes that and
# picks up `lv_color32_make`, the four-channel spelling this pattern never knew.
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
)

# How far the argument scan will walk before it gives up on a call. Four
# thousand characters is far past any real LVGL call and far short of a file, so
# hitting it means the source is not what it looks like — which is reported
# rather than skipped. A call the checker cannot read is a call it is not
# checking, and that is the failure this whole change is about.
MAX_CALL_CHARS = 4000

# An argument that names something — Metrics::px(dp_of(Space::Sm)), lv_pct(100),
# LV_SIZE_CONTENT, a constant — is not a raw value, and that is the line this
# check deliberately does not cross: a number in UI code is not automatically a
# pixel count, only a number handed to an LVGL length is.
#
# Deciding which is which used to be two regexes: "the whole argument is an
# integer literal", or "the whole argument contains no letter at all". Both were
# too narrow, and issue #68's follow-up named three arguments that are plainly
# numbers and matched neither:
#
#   1'2       a digit separator is legal C++ and the pattern had no apostrophe
#   12.0f     a floating literal's suffix is a letter, so it was not an integer
#             and not letter-free either
#   0x10 / 2  the *x* of a hex prefix is a letter, so "has a letter, therefore
#             names a token" was never sound — it was only ever right about
#             decimal
#
# One rule replaces all three: tokenise the argument, taking numeric literals
# *first*, and then ask whether any identifier is left over. A hex prefix and a
# float suffix are inside the literal by then, so neither can be mistaken for a
# name. That is C++'s own preprocessing-number production ([lex.ppnumber]) and
# it is deliberately generous — `0xE-1` is a single pp-number to a real
# compiler too, and reading a malformed number as a number rather than as a name
# fails towards refusing, which is the safe direction here.

_DIGITS = frozenset("0123456789")
_IDENTIFIER_START = frozenset(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_")
_IDENTIFIER = _IDENTIFIER_START | _DIGITS

# Names an argument may contain and still be naming nothing. A cast says what
# *type* a number is, never what it means, so `(int32_t)12` is the same raw
# pixel count as `12` — and none of these spellings was refused before.
# `static_cast<Dp>(12)` still passes, because `Dp` is not on this list: that is
# exactly the distinction, and it is a fixed list of arithmetic type names
# rather than a pattern so that nothing is ever added to it by accident.
CAST_ONLY = frozenset({
    "static_cast", "const_cast", "reinterpret_cast", "dynamic_cast",
    "signed", "unsigned", "char", "short", "int", "long", "float", "double",
    "bool", "void", "size_t", "ptrdiff_t", "intptr_t", "uintptr_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    # LVGL's own coordinate spellings, for the same reason.
    "lv_coord_t", "lv_value_precise_t", "lv_opa_t",
})

# Zero is the absence of a length, and a zero delay is a decision to have no
# delay rather than a duration nobody chose. Deciding whether a literal is zero
# needs its base, so each shape is read rather than searched for a `[1-9]`.
# Anything that matches none of them is treated as non-zero, which reports
# rather than misses.
_ZERO_HEX = re.compile(r"0[xX]([0-9a-fA-F]*)(?:\.([0-9a-fA-F]*))?"
                       r"(?:[pP][+-]?[0-9]+)?[a-zA-Z_]*", re.ASCII)
_ZERO_BINARY = re.compile(r"0[bB]([01]+)[a-zA-Z_]*", re.ASCII)
_ZERO_DECIMAL = re.compile(r"([0-9]*)(?:\.([0-9]*))?"
                           r"(?:[eE][+-]?[0-9]+)?[a-zA-Z_]*", re.ASCII)


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


def tokenize(text: str) -> tuple[list[str], list[str]]:
    """The numeric literals in an expression, and the names in it.

    Literals are taken first and whole, so the `x` of `0x10` and the `f` of
    `12.0f` are part of the number rather than the start of a name. Everything
    else that begins with a letter or an underscore is a name; operators,
    brackets and whitespace are neither and are stepped over.
    """
    literals: list[str] = []
    names: list[str] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch in _DIGITS or (ch == "." and i + 1 < n and text[i + 1] in _DIGITS):
            start = i
            i += 1
            while i < n:
                here = text[i]
                nxt = text[i + 1] if i + 1 < n else ""
                # `1e-3` and `0x1p-3`: the sign belongs to the exponent.
                if here in "eEpP" and nxt in "+-":
                    i += 2
                    continue
                # `1'000`: a digit separator, always between two characters of
                # the number, never at its end.
                if here == "'" and nxt in _IDENTIFIER:
                    i += 2
                    continue
                if here in _IDENTIFIER or here == ".":
                    i += 1
                    continue
                break
            literals.append(text[start:i])
        elif ch in _IDENTIFIER_START:
            start = i
            while i < n and text[i] in _IDENTIFIER:
                i += 1
            names.append(text[start:i])
        else:
            i += 1
    return literals, names


def is_zero(literal: str) -> bool:
    """Whether a numeric literal is zero, in whichever base it is written."""
    body = literal.replace("'", "")
    for pattern in (_ZERO_BINARY, _ZERO_HEX, _ZERO_DECIMAL):
        match = pattern.fullmatch(body)
        if match is None:
            continue
        digits = "".join(group for group in match.groups() if group)
        return bool(digits) and set(digits) == {"0"}
    return False    # unreadable: report it rather than let it through as zero


def raw_value(argument: str) -> str | None:
    """The literal this argument is, or None if it names something.

    Zero is not a length, it is the absence of one — and a zero delay is a
    decision to have no delay rather than a duration nobody chose. That holds
    for every kind, which is why this takes no kind.
    """
    text = " ".join(argument.split())
    if not text:
        return None
    literals, names = tokenize(text)
    if not literals:
        return None
    if any(name not in CAST_ONLY for name in names):
        return None
    if all(is_zero(literal) for literal in literals):
        return None
    return text


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
            offences.append(Offence(rel, source.line_of(match.start()),
                                    source.quote(match.start(), close),
                                    diagnostic(kind, literal)))

    offences.sort(key=lambda o: (o.line_no, o.why))
    return deduplicate(offences)


def diagnostic(kind: str, literal: str) -> str:
    if kind == LENGTH:
        return (f"{literal} px — a pixel count is a different physical size "
                f"on each panel; use Metrics::px(dp_of(...))")
    if kind == DURATION:
        return "a duration written as a number — use milliseconds_of(Motion::…)"
    return "a colour written as a number — ask for a ColorRole"


def deduplicate(offences: list[Offence]) -> list[Offence]:
    """One complaint per line per reason.

    `lv_color_hex(0xFFF6E8)` is a colour twice over — the six hex digits, and
    the argument of a call the inventory knows takes a colour. Both rules are
    worth having on their own, because one catches `constexpr auto kInk =
    0x2F3A2E` outside any call and the other catches `lv_color_hex3(0xABC)`,
    which has three digits and no call-free tell. Saying so twice would only
    teach the reader to skim.
    """
    seen: set[tuple[int, str, str]] = set()
    kept: list[Offence] = []
    for offence in offences:
        key = (offence.line_no, offence.why, offence.quoted)
        if key in seen:
            continue
        seen.add(key)
        kept.append(offence)
    return kept


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
