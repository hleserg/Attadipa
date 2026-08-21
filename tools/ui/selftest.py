#!/usr/bin/env python3
"""Prove check_raw_values.py rejects what it claims to reject.

The same argument as tools/l10n/selftest.py: a checker that has only ever been
run against clean code is indistinguishable from a checker that returns 0. Each
case below is a mistake somebody will actually make, written the way they will
actually write it.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check_raw_values.py"

MUST_REJECT = {
    "a colour as hex":
        'lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFF6E8), 0);',
    "a colour in a constant":
        'constexpr std::uint32_t kInk = 0x2F3A2E;',
    "padding in pixels":
        'lv_obj_set_style_pad_all(screen, 10, 0);',
    "a negative margin in pixels":
        'lv_obj_set_style_margin_top(label, -4, 0);',
    "a corner radius in pixels":
        'lv_obj_set_style_radius(card, 12, 0);',
    "a width in pixels":
        'lv_obj_set_width(row, 240);',
    "an animation duration":
        'lv_anim_set_time(&a, 200);',
    "a colour built channel by channel":
        'constexpr Rgb kPaper{0xFF, 0xF6, 0xE8};',
    "LVGL's own three-channel constructor":
        'lv_obj_set_style_bg_color(screen, lv_color_make(255, 246, 232), 0);',
}

MUST_ACCEPT = {
    "a role and a token":
        'lv_obj_set_style_pad_all(screen, px(Space::Sm), 0);',
    "zero is the absence of a length":
        'lv_obj_set_style_pad_top(label, 0, 0);',
    "a percentage is not a pixel count":
        'lv_obj_set_width(row, lv_pct(100));',
    "a named size is not a pixel count":
        'lv_obj_set_height(row, LV_SIZE_CONTENT);',
    "a four-digit mask is not a colour":
        'if ((byte & 0xC0) != 0x80) { return 0; }',
    "declaring the Rgb type is not writing a colour":
        'struct Rgb { std::uint8_t r = 0; };',
    "naming an Rgb without constructing one":
        'const Rgb amber = *color(ColorRole::AccentGlow, theme);',
    "hex inside a comment":
        '// the old value was 0xFF8A40 and it is gone',
    "a codepoint range in a comment":
        '// Montserrat says -r 0x20-0x7F,0xB0,0x2022',
}


def run(source: str) -> int:
    with tempfile.TemporaryDirectory() as directory:
        fixture = Path(directory) / "fixture.cpp"
        fixture.write_text(source + "\n", encoding="utf-8")
        result = subprocess.run([sys.executable, str(CHECKER), str(fixture)],
                                capture_output=True, text=True)
        return result.returncode


def main() -> int:
    failures = 0

    for name, source in MUST_REJECT.items():
        if run(source) == 0:
            print(f"FAIL: accepted {name}: {source}", file=sys.stderr)
            failures += 1

    for name, source in MUST_ACCEPT.items():
        if run(source) != 0:
            print(f"FAIL: rejected {name}: {source}", file=sys.stderr)
            failures += 1

    if failures:
        print(f"{failures} self-test case(s) failed", file=sys.stderr)
        return 1

    print(f"check_raw_values self-test: {len(MUST_REJECT)} rejected, "
          f"{len(MUST_ACCEPT)} accepted, as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
