#!/usr/bin/env python3
"""Read LVGL's public API out of its headers.

`check_raw_values.py` has to know which LVGL entry points take a length and
which take a duration, and it has to know that without LVGL on disk — the
simulator is `OFF` by default, so a checker that needs LVGL is a checker that
stops running on most CI jobs. So the answer is written down, in
`lvgl_inventory.py`.

A written-down list cannot notice a setter LVGL adds, and issue #68 is the
receipt: the list was said to have been read out of `lv_obj_pos.h`, and
`lv_obj_set_ext_click_area` — declared in that file, second argument
`int32_t size`, documented "extended clickable area in all 4 directions [px]" —
was not on it. The list was checked by example and never for completeness.

This module is the other half: given an LVGL source tree, it enumerates every
entry point that *could* take a design value, so `check_inventory.py` can hold
the written-down list against it. It parses declarations rather than compiling
them — LVGL's public headers are plain C declarations, and the question here is
only "which functions exist and what are their parameter types", which does not
need a preprocessor.
"""

from __future__ import annotations

import re
from pathlib import Path

# Parameter types that can carry a design value. These are the *spellings* used
# in the headers, not resolved types, and that is deliberate: `lv_part_t` and
# `lv_style_selector_t` are `uint32_t` after typedef resolution, but a part
# selector is never a pixel count and matching on the spelling keeps every
# `..., lv_part_t part)` out of the candidate set without an exception for it.
NUMERIC_TYPES = (
    "int32_t", "uint32_t",
    "int16_t", "uint16_t",
    "int8_t", "uint8_t",
    "lv_value_precise_t",
)

# Where screen code cannot reach, so nothing in it can receive a design value
# from a screen.
#
#   draw/       the renderer. Screens describe what to draw, never how.
#   drivers/    display and input ports — board bring-up, not screen code.
#   libs/       bindings to third-party decoders (PNG, FreeType, ThorVG).
#   osal/       threading shims.
#   stdlib/     allocator and string shims.
#   debugging/  LVGL's own test harness.
#   themes/     LVGL's built-in themes, which this project replaces wholesale
#               with docs/ui/DESIGN_SYSTEM.md — nothing here calls them.
#
# Each is a directory rather than a name list, so a file added to one of them
# is covered by the same reason without anybody editing this.
UNREACHABLE = ("draw/", "drivers/", "libs/", "osal/", "stdlib/", "debugging/",
               "themes/")

# A function declaration or a static-inline definition. The return type is
# matched loosely on purpose: missing a declaration here means the completeness
# check does not know an entry point exists, which is the exact failure this
# module is against, so it errs towards matching too much. Anything it matches
# that is not really a function gets classified once and never bothers anybody
# again.
_DECLARATION = re.compile(
    r"(?:^|[;{}])"                              # start of a declaration
    r"[^;{}()]{0,120}?"                         # return type and attributes
    r"\b(?P<name>lv_[a-z0-9_]+)\s*"
    r"\((?P<params>[^;{}]*?)\)\s*"              # a parameter list, no nesting
    r"(?:LV_FORMAT_ATTRIBUTE\s*\([^)]*\)\s*)?"
    r"[;{]",
    re.M)

# A function-pointer parameter puts parentheses inside the list, which the
# pattern above will not cross. There are few of them and none carries a design
# value, so they are found with a second, looser pass purely so that the
# function itself is not invisible.
_DECLARATION_NESTED = re.compile(
    r"(?:^|[;{}])"
    r"[^;{}]{0,400}?"
    r"\b(?P<name>lv_[a-z0-9_]+)\s*"
    r"\((?P<params>[^;{}]*\([^;{}()]*\)[^;{}]*?)\)\s*[;{]",
    re.M)

# `#define lv_anim_set_time lv_anim_set_duration` — an old spelling that still
# compiles, and therefore an entry point a screen can still reach. The v8 and
# v9.x compatibility maps are full of them, and one of them is how issue #68's
# predecessor hid `lv_anim_set_duration`: the list held only the compatibility
# names, so the name a v9 screen would actually type was unchecked.
_ALIAS = re.compile(r"^\s*#\s*define\s+(lv_[a-z0-9_]+)\s+(lv_[a-z0-9_]+)\s*$",
                    re.M)


class EntryPoint:
    """One LVGL function, and which of its parameters are numbers."""

    def __init__(self, name: str, header: str, parameters: list[str]) -> None:
        self.name = name
        self.header = header
        self.parameters = parameters
        self.numeric = tuple(
            index for index, parameter in enumerate(parameters)
            if any(parameter.startswith(kind + " ") for kind in NUMERIC_TYPES))
        self.alias_of: str | None = None

    @property
    def signature(self) -> str:
        return f"{self.name}({', '.join(self.parameters)})"

    def numeric_parameter(self, index: int) -> str | None:
        if index >= len(self.parameters):
            return None
        return self.parameters[index]


def _blank(match: re.Match[str]) -> str:
    """A comment, replaced by spaces, keeping every newline it contained.

    Collapsing a block comment to one space would join the declaration after it
    onto the `#endif` before it, and `lv_dpx()` — declared right after a
    twenty-line comment — went missing exactly that way while this file was
    being written. A derivation that silently drops declarations is the same
    defect as the hand-written list it replaces.
    """
    return re.sub(r"[^\n]", " ", match.group(0))


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", _blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", _blank, text)


# A preprocessor directive, including any lines it continues onto. Blanked
# before declarations are read, because a macro body carries parentheses that a
# declaration's argument list will happily run through:
# `#define LV_HOR_RES lv_display_get_horizontal_resolution(lv_display_get_default())`
# matched as a call and swallowed the next forty lines, which is how `lv_dpx()`
# came to be invisible. Aliases are read from the raw text, before this.
_DIRECTIVE = re.compile(r"^[ \t]*#(?:[^\n\\]|\\.)*", re.M)


def _strip_directives(text: str) -> str:
    return _DIRECTIVE.sub(_blank, text)


def _parameters(raw: str) -> list[str] | None:
    collapsed = " ".join(raw.split())
    if collapsed in ("", "void"):
        return []
    if "..." in collapsed:
        collapsed = collapsed.replace(", ...", "")
    return [p.strip() for p in collapsed.split(",")]


def headers(root: Path) -> list[Path]:
    """Every public header a screen could include, in a stable order."""
    src = root / "src"
    if not src.is_dir():
        return []
    out = []
    for path in sorted(src.rglob("*.h")):
        rel = path.relative_to(src).as_posix()
        if "_private" in rel or rel.endswith("_private.h"):
            continue
        if any(rel.startswith(prefix) for prefix in UNREACHABLE):
            continue
        out.append(path)
    return out


def entry_points(root: Path) -> dict[str, EntryPoint]:
    """Every declared function with at least one numeric parameter.

    Compatibility `#define`s that rename such a function are included under
    their old name, because a screen that types the old name compiles.
    """
    src = root / "src"
    found: dict[str, EntryPoint] = {}
    aliases: dict[str, str] = {}

    for path in headers(root):
        rel = path.relative_to(src).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        text = _strip_directives(_strip_comments(raw))
        for pattern in (_DECLARATION, _DECLARATION_NESTED):
            for match in pattern.finditer(text):
                parameters = _parameters(match.group("params"))
                if parameters is None or not parameters:
                    continue
                candidate = EntryPoint(match.group("name"), rel, parameters)
                if not candidate.numeric:
                    continue
                found.setdefault(candidate.name, candidate)
        for match in _ALIAS.finditer(raw):
            aliases.setdefault(match.group(1), match.group(2))

    for old, new in aliases.items():
        if old in found or new not in found:
            continue
        target = found[new]
        alias = EntryPoint(old, target.header, target.parameters)
        alias.alias_of = new
        found[old] = alias

    return found


def version(root: Path) -> str | None:
    """The version LVGL says it is, from lv_version.h."""
    header = root / "lv_version.h"
    if not header.is_file():
        return None
    text = header.read_text(encoding="utf-8", errors="replace")
    parts = []
    for field in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"#define\s+LVGL_VERSION_{field}\s+(\d+)", text)
        if not match:
            return None
        parts.append(match.group(1))
    return ".".join(parts)

