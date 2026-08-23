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

    # --- the entry points the re-derived inventory added ------------------
    # Issue #68's follow-up found the first of these by hand. The rest came out
    # of holding lvgl_inventory.py against the pinned headers, which is what
    # check_inventory.py now does on every simulator build.
    "the extended click area is measured in pixels":
        'lv_obj_set_ext_click_area(obj, 12);',
    "the extended click area, wrapped":
        'lv_obj_set_ext_click_area(\n'
        '    obj,\n'
        '    12);',
    "the v8 spelling of the shadow offset still compiles":
        'lv_obj_set_style_shadow_ofs_x(card, 4, 0);',
    "the v8 spelling of the style animation duration":
        'lv_obj_set_style_anim_time(bar, 300, 0);',
    "the deprecated reverse-time setter is a real function, not a macro":
        'lv_anim_set_reverse_time(&anim, 200);',
    "the v8 spelling of the image offset":
        'lv_img_set_offset_x(icon, 8);',
    "the v8 spelling of the column width":
        'lv_table_set_col_width(table, 0, 60);',
    "a column width is a pixel count":
        'lv_table_set_column_width(table, 0, 60);',
    "a tab bar size is a pixel count":
        'lv_tabview_set_tab_bar_size(tabs, 48);',
    "LVGL's own density helper still takes a raw size":
        'lv_obj_set_width(row, lv_dpx(40));',
    "a scroll threshold is a distance on the panel":
        'lv_indev_set_scroll_limit(indev, 10);',
    "a gradient runs between two points on the panel":
        'lv_grad_linear_init(&grad, 0, 0, 0, 120, LV_GRAD_EXTEND_PAD);',
    "text measurement takes the same spacings the style does":
        'lv_text_get_size(&size, txt, font, 2, 4, 200, LV_TEXT_FLAG_NONE);',
    "a window button has a width":
        'lv_win_add_button(win, LV_SYMBOL_CLOSE, 40);',
    "the four-channel colour constructor":
        'lv_obj_set_style_bg_color(screen, lv_color32_make(255, 246, 232, 255), 0);',
    "a three-digit hex colour has no six-digit tell":
        'lv_obj_set_style_bg_color(screen, lv_color_hex3(0xABC), 0);',
    "a screen transition is a motion decision":
        'lv_screen_load_anim(next, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);',
    "a delayed delete waits out an animation":
        'lv_obj_delete_delayed(toast, 2000);',
    "a fade is a transition":
        'lv_obj_fade_out(card, 200, 0);',

    # --- C++ says these are numbers, and the old patterns did not ---------
    # All four returned "clean" on f2b6853. They are the same defect as the
    # wrapped call: a rule about the spelling instead of about the value.
    "a digit separator does not stop it being twelve":
        "lv_obj_set_width(obj, 1'2);",
    "a floating literal's suffix is not a name":
        'lv_obj_set_width(obj, 12.0f);',
    "hex arithmetic names nothing either":
        'lv_obj_set_width(obj, 0x10 / 2);',
    "a hex literal on its own":
        'lv_obj_set_style_pad_all(obj, 0x0C, 0);',
    "an exponent is still a number":
        'lv_anim_set_duration(&anim, 2e2);',
    "a long suffix is not a name":
        'lv_obj_set_style_radius(card, 12UL, 0);',
    "a binary literal is a number":
        'lv_obj_set_style_pad_top(label, 0b1100, 0);',
    "a C-style cast says the type, never the meaning":
        'lv_obj_set_style_pad_all(obj, (int32_t)12, 0);',
    "and neither does a static_cast to a primitive":
        'lv_obj_set_width(row, static_cast<int32_t>(240));',
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

    # --- the numbers the widened literal grammar must not start refusing --
    # Widening what counts as a *literal* is only safe if what counts as a
    # *name* held. Each of these has a number in it and names something, and
    # each would be a false positive if the tokenizer read a hex prefix, a
    # float suffix or a cast type as evidence either way.
    "zero written as a float is still zero":
        'lv_obj_set_style_pad_all(obj, 0.0f, 0);',
    "zero written in hex is still zero":
        'lv_obj_set_style_pad_top(label, 0x0, 0);',
    "a cast to a design type is naming the design type":
        'lv_obj_set_width(row, static_cast<Dp>(kRowWidth));',
    "a token with a digit in its name":
        'lv_obj_set_style_pad_all(obj, px(Space::Sm2), 0);',
    "a constant whose name starts with a type word":
        'lv_obj_set_width(row, int_width_token);',

    # --- the entry points the inventory deliberately does not treat -------
    # The non-goal again, now that the inventory covers every LVGL entry point
    # with a numeric argument: being *in* the inventory is not the same as
    # carrying a design value, and check_inventory.py makes each of these a
    # written-down decision rather than an omission.
    "a timer period is not a motion token":
        'lv_timer_create(tick, 1000, nullptr);',
    "a long-press threshold is an input constant":
        'lv_indev_set_long_press_time(indev, 400);',
    "a bar value is data":
        'lv_bar_set_value(bar, 50, LV_ANIM_OFF);',
    "an arc angle is an angle":
        'lv_arc_set_bg_angles(arc, 135, 45);',
    "a calendar field is a date":
        'lv_calendar_set_today_date(cal, 2026, 8, 23);',
    "a table cell is addressed by row and column":
        'lv_table_set_cell_value(table, 0, 1, "steps");',
    "an image scale of 256 is no scaling":
        'lv_image_set_scale(icon, 256);',
    "a percentage is not a pixel count, even as a bare call":
        'lv_obj_set_width(row, lv_pct(50));',
    "LVGL's channel constructor with three named roles":
        'lv_obj_set_style_bg_color(screen, lv_color_make(r, g, b), 0);',
    "a panel resolution is a board fact":
        'lv_display_set_resolution(display, 410, 502);',

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
        "an entry point the inventory added is reported like any other",
        'lv_obj_set_ext_click_area(\n'
        '    obj,\n'
        '    12);',
        ("fixture.cpp:1:", "12 px"),
    ),
    (
        "a colour caught through the inventory says it is a colour",
        'lv_obj_set_style_bg_color(screen, lv_color_hex3(0xABC), 0);',
        ("fixture.cpp:1:", "ask for a ColorRole"),
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
