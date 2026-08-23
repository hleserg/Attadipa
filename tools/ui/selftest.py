#!/usr/bin/env python3
"""Prove check_raw_values.py rejects what it claims to reject.

The same argument as tools/l10n/selftest.py: a checker that has only ever been
run against clean code is indistinguishable from a checker that returns 0. Each
case below is a mistake somebody will actually make, written the way they will
actually write it.

Half of the cases are the same call written twice — once on one line and once
the way `clang-format` leaves it at a narrow column. That is issue #68's whole
point: before the fix the two got different verdicts, so the invariant was a
property of the formatting rather than of the code, and nothing here would have
noticed, because every fixture was one line long.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check_raw_values.py"

MUST_REJECT = {
    # --- one line, the shapes the checker has always known -----------------
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

    # --- the same calls, wrapped ------------------------------------------
    # Issue #68's reproducer. Every one of these returned 0 before the fix.
    "padding in pixels, wrapped by the formatter":
        'lv_obj_set_style_pad_all(\n'
        '    obj,\n'
        '    12,\n'
        '    0);',
    "a duration, wrapped by the formatter":
        'lv_anim_set_time(\n'
        '    &anim,\n'
        '    200);',
    "a colour built channel by channel, wrapped":
        'constexpr Rgb kPaper{\n'
        '    0xFF, 0xF6, 0xE8};',
    "a hex colour on a continuation line":
        'lv_obj_set_style_bg_color(\n'
        '    screen,\n'
        '    lv_color_hex(0xFFF6E8),\n'
        '    0);',
    "a radius, wrapped, with the value alone on its line":
        'lv_obj_set_style_radius(\n'
        '    card,\n'
        '    12,\n'
        '    LV_PART_MAIN);',

    # --- calls with more than one length ----------------------------------
    # A single-value regex cannot express these at all, whatever the layout.
    "both arguments of set_size":
        'lv_obj_set_size(obj, 10, 20);',
    "both arguments of set_pos":
        'lv_obj_set_pos(obj, 10, 20);',
    "set_size, wrapped":
        'lv_obj_set_size(\n'
        '    obj,\n'
        '    10,\n'
        '    20);',
    "the two-length style helper":
        'lv_obj_set_style_size(icon, 24, 24, 0);',
    "an alignment offset":
        'lv_obj_align(label, LV_ALIGN_CENTER, 0, 8);',
    "an alignment offset against another object":
        'lv_obj_align_to(label, card, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);',
    "a scroll in pixels":
        'lv_obj_scroll_by(list, 0, 40, LV_ANIM_ON);',

    # --- names the hand-written list never had ----------------------------
    "a symmetric transform the old list forgot":
        'lv_obj_set_style_transform_width(card, 10, 0);',
    "the v9 spelling of the duration setter":
        'lv_anim_set_duration(&anim, 200);',
    "a duration set as a style property":
        'lv_obj_set_style_anim_duration(bar, 300, 0);',
    "the same property on a bare style rather than an object":
        'lv_style_set_pad_all(&style, 10);',
    "letter spacing is a length too":
        'lv_obj_set_style_text_letter_space(label, 2, 0);',

    # --- arithmetic is not a name -----------------------------------------
    "half a panel is still a pixel count":
        'lv_obj_set_width(row, 240 / 2);',
    "a parenthesised literal is still a literal":
        'lv_obj_set_style_pad_top(label, (12), 0);',
    "a fractional pixel is a pixel":
        'lv_obj_set_style_pad_all(screen, 12.5, 0);',

    # --- a comment does not launder the line that follows it --------------
    "a raw value after a block comment on the same line":
        'lv_obj_set_style_pad_all(screen, /* was a token */ 10, 0);',
}

MUST_ACCEPT = {
    # --- one line, the shapes the checker has always allowed ---------------
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

    # --- the same correct code, wrapped -----------------------------------
    # Formatting must not change the verdict in this direction either: a
    # checker that only accepts one-line correctness is a checker that bans
    # clang-format.
    "a token, wrapped by the formatter":
        'lv_obj_set_style_pad_all(\n'
        '    obj,\n'
        '    metrics.px(dp_of(Space::Sm)),\n'
        '    0);',
    "a semantic duration, wrapped":
        'lv_anim_set_duration(\n'
        '    &anim,\n'
        '    milliseconds_of(Motion::Base));',
    "zero, wrapped":
        'lv_obj_set_style_pad_all(\n'
        '    obj,\n'
        '    0,\n'
        '    0);',
    "both lengths of set_size given as tokens":
        'lv_obj_set_size(\n'
        '    icon,\n'
        '    metrics.px(dp_of(IconSize::Md)),\n'
        '    metrics.px(dp_of(IconSize::Md)));',
    "a named size and a percentage, wrapped":
        'lv_obj_set_size(\n'
        '    row,\n'
        '    lv_pct(100),\n'
        '    LV_SIZE_CONTENT);',
    "a nested call whose own arguments are separated by commas":
        'lv_obj_set_style_pad_all(obj, clamp(px(Space::Sm), lo, hi), 0);',

    # --- comments and strings that read like offences ---------------------
    "a whole call commented out in block form":
        '/*\n'
        ' * lv_obj_set_size(obj, 10, 20);\n'
        ' */',
    "a call inside a string literal":
        'const char* usage = "lv_obj_set_size(obj, 10, 20)";',
    "a colour inside a string literal":
        'const char* note = "the brand hex is 0xFF8A40";',
    "an apostrophe in prose does not swallow the file":
        "// it's the panel's density that decides\n"
        'lv_obj_set_style_pad_all(obj, px(Space::Sm), 0);',
    "a digit separator is not a char literal":
        'constexpr int kMicrosecondsPerSecond = 1\'000\'000;',

    # --- numbers that are not lengths, colours or durations ---------------
    # The non-goal, held as a test: a literal in UI code is not automatically a
    # pixel count. Only a literal handed to an LVGL length is.
    "a repeat count is a count":
        'lv_anim_set_repeat_count(&anim, 3);',
    "a rotation is in tenths of a degree":
        'lv_obj_set_style_transform_rotation(needle, 900, 0);',
    "a gradient stop runs 0 to 255":
        'lv_obj_set_style_bg_grad_stop(screen, 128, 0);',
    "a flex weight is a weight":
        'lv_obj_set_flex_grow(row, 1);',
    "a scale is 256 to the unit":
        'lv_obj_set_style_transform_scale_x(card, 256, 0);',
    "an array index is not a coordinate":
        'const int steps = history[3];',

    # --- the false positive the widened colour rule had to not create -----
    "a function returning Rgb is not a colour literal":
        'Rgb make_colour(Theme theme)\n'
        '{\n'
        '    return palette[0];\n'
        '}',
    "a multi-line Rgb declaration is not a colour literal":
        'struct Rgb {\n'
        '    std::uint8_t r = 0;\n'
        '    std::uint8_t g = 0;\n'
        '};',
}

# The diagnostic has to be usable, not merely present: issue #68 asks it to name
# the file, the *first* line of the call and the offending value. On a wrapped
# call all three used to be unavailable, because the call was never seen whole.
DIAGNOSTIC_CASES = (
    (
        "a wrapped call is reported at the line it starts on",
        'lv_obj_set_style_pad_all(\n'
        '    obj,\n'
        '    12,\n'
        '    0);',
        ("fixture.cpp:1:", "12 px"),
    ),
    (
        "each length of a two-length call is named separately",
        'lv_obj_set_size(obj, 10, 20);',
        ("10 px", "20 px"),
    ),
    (
        "a call further down the file keeps its own line number",
        '#include "screen.h"\n'
        '\n'
        'void build(lv_obj_t* obj)\n'
        '{\n'
        '    lv_obj_set_style_radius(\n'
        '        obj,\n'
        '        12,\n'
        '        0);\n'
        '}',
        ("fixture.cpp:5:", "12 px"),
    ),
)


def run(source: str) -> tuple[int, str]:
    with tempfile.TemporaryDirectory() as directory:
        fixture = Path(directory) / "fixture.cpp"
        fixture.write_text(source + "\n", encoding="utf-8")
        result = subprocess.run([sys.executable, str(CHECKER), str(fixture)],
                                capture_output=True, text=True)
        return result.returncode, result.stderr


def main() -> int:
    failures = 0

    for name, source in MUST_REJECT.items():
        if run(source)[0] == 0:
            print(f"FAIL: accepted {name}: {source!r}", file=sys.stderr)
            failures += 1

    for name, source in MUST_ACCEPT.items():
        code, output = run(source)
        if code != 0:
            print(f"FAIL: rejected {name}: {source!r}\n{output}", file=sys.stderr)
            failures += 1

    for name, source, expected in DIAGNOSTIC_CASES:
        code, output = run(source)
        if code == 0:
            print(f"FAIL: accepted {name}: {source!r}", file=sys.stderr)
            failures += 1
            continue
        for fragment in expected:
            if fragment not in output:
                print(f"FAIL: {name}: no {fragment!r} in\n{output}", file=sys.stderr)
                failures += 1

    if failures:
        print(f"{failures} self-test case(s) failed", file=sys.stderr)
        return 1

    print(f"check_raw_values self-test: {len(MUST_REJECT)} rejected, "
          f"{len(MUST_ACCEPT)} accepted, {len(DIAGNOSTIC_CASES)} diagnostics "
          f"checked, as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
