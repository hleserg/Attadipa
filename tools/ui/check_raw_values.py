#!/usr/bin/env python3
"""Refuse raw colours and raw lengths in screen code (T-009).

A design system that lives only in a document is a document. The tokens exist so
that a colour is a role and a gap is a Dp, and the way that stops being true is
not a redesign — it is one `lv_color_hex(0x2F3A2E)` written in a hurry on a
Friday, copied twice, and then nobody can change the palette any more.

So this is a boundary check of the same family as the two in tests/boundary/:
it does not ask whether the value is *right*, it asks whether the value is
*there*. Two places are allowed to hold numbers and both say why in their own
source — ui/src/color.cpp holds the palette and ui/include/.../tokens.h holds
the scale.

Exit 0 when clean; 1 with one line per offence otherwise.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Where screens are written. Everything under these is checked.
SCANNED = ("sim", "apps", "ui")

# The two files that are *supposed* to contain numbers, and nothing else is.
# Listed explicitly rather than by pattern, so that adding a third is a decision
# somebody makes in this file rather than a file that quietly matches a glob.
ALLOWED = {
    "ui/src/color.cpp",                     # the palette, transcribed from final §42
    "ui/include/attadipa/ui/tokens.h",      # the scale
    "ui/include/attadipa/ui/metrics.h",     # the Dp-to-pixel arithmetic itself
    "ui/include/attadipa/ui/color.h",       # documents the palette in prose
    "sim/lv_conf_simulator.h",              # LVGL's own configuration, not ours
    "sim/png_writer.cpp",                   # a file format, not a screen
    "sim/options.cpp",                      # argv parsing
}

SUFFIXES = {".c", ".cpp", ".h", ".hpp"}


class Offence:
    def __init__(self, path: str, line_no: int, line: str, why: str) -> None:
        self.path = path
        self.line_no = line_no
        self.line = line.strip()
        self.why = why

    def __str__(self) -> str:
        return f"{self.path}:{self.line_no}: {self.why}\n    {self.line}"


# A colour, written as one. Six hex digits is a colour in every UI codebase that
# has ever existed; four is usually a mask and is left alone.
COLOUR = re.compile(r"0[xX][0-9a-fA-F]{6}\b")

# A length handed to LVGL as a bare number. The style setters take (obj, value,
# selector), so the value is the second argument, and a literal there is a pixel
# count that will be a different physical size on the two panels.
LENGTH_SETTERS = (
    "pad_all", "pad_top", "pad_bottom", "pad_left", "pad_right",
    "pad_row", "pad_column", "pad_gap", "pad_hor", "pad_ver",
    "margin_top", "margin_bottom", "margin_left", "margin_right",
    "radius", "border_width", "outline_width", "outline_pad",
    "line_width", "shadow_width", "shadow_spread", "transform_height",
    "width", "height", "min_width", "max_width", "min_height", "max_height",
)
STYLE_LENGTH = re.compile(
    r"lv_obj_set_style_(" + "|".join(LENGTH_SETTERS) + r")\s*\([^,]+,\s*(-?\d+)\s*,"
)
# lv_obj_set_width / set_height take (obj, value) with no selector.
DIRECT_LENGTH = re.compile(
    r"lv_obj_set_(width|height|content_width|content_height|x|y)\s*\([^,]+,\s*(-?\d+)\s*\)"
)

# A duration in an animation call.
DURATION = re.compile(r"lv_anim_set_(time|delay|playback_time|repeat_delay)\s*\([^,]+,\s*(\d+)\s*\)")


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


def check(path: Path) -> list[Offence]:
    try:
        rel = path.relative_to(ROOT).as_posix()
    except ValueError:
        rel = path.as_posix()   # a self-test fixture in a scratch directory
    if rel in ALLOWED:
        return []

    offences: list[Offence] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        code = line.split("//", 1)[0]
        if not code.strip():
            continue

        if COLOUR.search(code):
            offences.append(Offence(rel, line_no, line,
                                    "a colour written as a number — ask for a ColorRole"))

        for pattern in (STYLE_LENGTH, DIRECT_LENGTH):
            for match in pattern.finditer(code):
                value = int(match.group(2))
                # 0 is not a length, it is the absence of one, and LV_SIZE_CONTENT
                # and lv_pct() are not pixel counts either — those are names and
                # do not reach this pattern at all.
                if value == 0:
                    continue
                offences.append(Offence(rel, line_no, line,
                                        f"{value} px — a pixel count is a different "
                                        f"physical size on each panel; use Metrics::px(dp_of(...))"))

        for match in DURATION.finditer(code):
            if int(match.group(2)) != 0:
                offences.append(Offence(rel, line_no, line,
                                        "a duration written as a number — use milliseconds_of(Motion::…)"))
    return offences


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
