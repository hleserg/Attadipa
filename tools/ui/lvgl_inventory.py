#!/usr/bin/env python3
"""Which LVGL entry points take a design value, and which deliberately do not.

`check_raw_values.py` needs this without LVGL on disk — the simulator is `OFF`
by default, so a checker that imports LVGL's headers is a checker that stops
running on most CI jobs. So the answer is written here, in plain data, and
`check_inventory.py` holds it against the pinned headers wherever they *are*
available. Two halves of one contract:

* this file is what the scan knows, and it costs nothing to load;
* `check_inventory.py` is what proves this file still describes LVGL.

**Every candidate is classified, and that is the point.** A candidate is any
function in LVGL's screen-reachable API with a numeric parameter. It lands in
`ENTRY_POINTS` — argument N is a length, a duration or a colour — or in
`NOT_A_DESIGN_VALUE` with a reason. There is no third bucket and no default, so
a setter LVGL adds is *unclassified* rather than *unnoticed*, and
`check_inventory.py` says so by name.

That distinction is the whole reason this file exists. The previous inventory
was hand-written and checked by example: issue #68's follow-up found
`lv_obj_set_ext_click_area` — declared in `lv_obj_pos.h`, argument 1
`int32_t size`, documented "extended clickable area in all 4 directions [px]" —
missing from a list whose comment said it had been read out of that very file.
Re-deriving it here found four more of the same shape that nobody had reported:
`shadow_ofs_x`, `shadow_ofs_y` and `anim_time` (the v8 spellings, which still
compile), and `lv_anim_set_reverse_time`.

Derived from LVGL v9.5.0 at commit 85aa60d18b3d5e5588d7b247abf90198f07c8a63 —
the commit `cmake/AttadipaLvgl.cmake` pins and refuses to build without.
Re-deriving it is a step of an LVGL bump; see `docs/research/DEPENDENCIES.md`.
"""

from __future__ import annotations

LENGTH = "length"
DURATION = "duration"
COLOUR = "colour"

ANGLE = "an angle"
BYTES = "a size in bytes or elements"
CHARACTER = "a character code"
COUNT = "a count"
DATA = "a value on the widget's own scale"
DATE = "a calendar field"
INDEX = "an index into a list"
INPUT = "an input threshold rather than a transition"
LEVEL = "a level on a 0..255 or 1..5 scale"
MATH = "an argument to an arithmetic helper"
OPAQUE = "an identifier or a flag"
PANEL = "a panel fact, which comes from the board and not from the design system"
PERCENT = "a percentage, which is resolution-independent by construction"
RATIO = "a ratio where 256 is 1x"
TICK = "a tick count from LVGL's own clock"
TIMER = "a timer period rather than a transition"

# Two families are worth their own sentence, because both look like they belong
# in ENTRY_POINTS and neither does.
#
# TIMER: lv_timer_create()'s period is not a motion token. A clock that redraws
# once a second is not making a design decision, and T-009's invariant is over
# the design system rather than over every millisecond in the build.
#
# INPUT: a long-press threshold and a password reveal timeout are interaction
# constants, not transitions. docs/ui/DESIGN_SYSTEM.md §5 defines motion as four
# transition durations — instant, fast, base, slow — and forcing an input
# threshold to name one of them would misreport what it is. The pixel-valued
# input thresholds are the other way round and *are* in ENTRY_POINTS, because a
# pixel is a different physical distance on each of the two panels whatever the
# value is tuning.


# ---------------------------------------------------------------------------
# Style properties
#
# `lv_obj_set_style_<prop>(obj, value, selector)` and its bare-style twin
# `lv_style_set_<prop>(style, value)` are generated from one table in LVGL, so
# they are classified once here by property name and expanded to both spellings
# below. 76 properties take a number; all 76 appear in exactly one of the three
# tuples, and check_inventory.py fails if that stops being true.
# ---------------------------------------------------------------------------

# A distance on the panel. A number here is a different physical size on a
# 240x240 watch face and on a 410x502 one, which is the whole reason for Dp.
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
    # The v8 spellings of the two above. lv_api_map_v8.h still defines them, so
    # they compile — and neither was on the old list.
    "shadow_ofs_x", "shadow_ofs_y",
    "blur_radius", "drop_shadow_radius",
    "drop_shadow_offset_x", "drop_shadow_offset_y",
    "line_width", "line_dash_width", "line_dash_gap",
    "arc_width",
    "text_letter_space", "text_line_space", "text_outline_stroke_width",
    # The static-inline convenience helpers in lv_obj_style.h, which are not in
    # the generated table but are what people actually type — pad_all is in
    # issue #68's original reproducer.
    "pad_all", "pad_hor", "pad_ver", "pad_gap",
    "margin_all", "margin_hor", "margin_ver",
)

# Milliseconds of a visual transition.
DURATION_PROPERTIES = (
    "anim_duration",
    "anim_time",        # the v8 spelling, and it still compiles
)

# The other numeric style properties, refused deliberately: a number in one of
# these is not a design value, and refusing it would be a rule that cries often
# enough to be turned off. Each says what it is instead.
NOT_A_DESIGN_VALUE_PROPERTIES = {
    "bg_main_stop":         "0..255 along the gradient, not a distance",
    "bg_grad_stop":         "0..255 along the gradient, not a distance",
    "grid_cell_column_pos": "a grid track index",
    "grid_cell_column_span": "a count of grid tracks",
    "grid_cell_row_pos":    "a grid track index",
    "grid_cell_row_span":   "a count of grid tracks",
    "flex_grow":            "a flex weight, not a size",
    "layout":               "a layout identifier",
    "rotary_sensitivity":   "a ratio where 256 is 1x",
    "transform_rotation":   "tenths of a degree",
    "transform_angle":      "tenths of a degree (the v8 spelling)",
    "transform_skew_x":     "tenths of a degree",
    "transform_skew_y":     "tenths of a degree",
    "transform_scale_x":    "a scale where 256 is 1x",
    "transform_scale_y":    "a scale where 256 is 1x",
    "transform_scale":      "a scale where 256 is 1x (the v8 spelling)",
    "transform_zoom":       "a scale where 256 is 1x (the v8 spelling)",
}


def _style_entry_points() -> dict[str, tuple[tuple[int, str], ...]]:
    """Both spellings of every classified property: on an object, on a style."""
    table: dict[str, tuple[tuple[int, str], ...]] = {}
    for properties, kind in ((LENGTH_PROPERTIES, LENGTH),
                             (DURATION_PROPERTIES, DURATION)):
        for prop in properties:
            table[f"lv_obj_set_style_{prop}"] = ((1, kind),)
            table[f"lv_style_set_{prop}"] = ((1, kind),)
    return table


def style_property_names() -> set[str]:
    """Every property this file has an opinion about, in any of the three."""
    return (set(LENGTH_PROPERTIES) | set(DURATION_PROPERTIES)
            | set(NOT_A_DESIGN_VALUE_PROPERTIES))


# ---------------------------------------------------------------------------
# Everything else
#
# Which arguments of which call carry a design value, counting from zero. The
# object, the style or the animation is argument 0 in almost every case, which
# is why so little here is 0 — but lv_dpx() and lv_clamp_width() are free
# functions and theirs is.
# ---------------------------------------------------------------------------

ENTRY_POINTS: dict[str, tuple[tuple[int, str], ...]] = {
    **_style_entry_points(),

    # Two lengths in one call. lv_obj_set_style_size(obj, w, h, selector) does
    # not fit the generated one-value shape, so it is written out.
    "lv_obj_set_style_size":     ((1, LENGTH), (2, LENGTH)),
    "lv_style_set_size":         ((1, LENGTH), (2, LENGTH)),

    # Geometry: core/lv_obj_pos.h.
    "lv_obj_set_pos":            ((1, LENGTH), (2, LENGTH)),
    "lv_obj_set_size":           ((1, LENGTH), (2, LENGTH)),
    "lv_obj_set_x":              ((1, LENGTH),),
    "lv_obj_set_y":              ((1, LENGTH),),
    "lv_obj_set_width":          ((1, LENGTH),),
    "lv_obj_set_height":         ((1, LENGTH),),
    "lv_obj_set_content_width":  ((1, LENGTH),),
    "lv_obj_set_content_height": ((1, LENGTH),),
    "lv_obj_move_to":            ((1, LENGTH), (2, LENGTH)),
    "lv_obj_move_children_by":   ((1, LENGTH), (2, LENGTH)),
    # The one issue #68's follow-up found: "extended clickable area in all 4
    # directions [px]", in the header the old list claimed to be read from.
    "lv_obj_set_ext_click_area": ((1, LENGTH),),
    # (obj, align, x_ofs, y_ofs) — the alignment is a name, the offsets are not.
    "lv_obj_align":              ((2, LENGTH), (3, LENGTH)),
    # (obj, base, align, x_ofs, y_ofs)
    "lv_obj_align_to":           ((3, LENGTH), (4, LENGTH)),
    # Free functions: every argument is a width or a height in pixels.
    "lv_clamp_width":            ((0, LENGTH), (1, LENGTH), (2, LENGTH), (3, LENGTH)),
    "lv_clamp_height":           ((0, LENGTH), (1, LENGTH), (2, LENGTH), (3, LENGTH)),

    # Scrolling: core/lv_obj_scroll.h. (obj, dx, dy, anim_en)
    "lv_obj_scroll_by":          ((1, LENGTH), (2, LENGTH)),
    "lv_obj_scroll_by_bounded":  ((1, LENGTH), (2, LENGTH)),
    "lv_obj_scroll_to":          ((1, LENGTH), (2, LENGTH)),
    "lv_obj_scroll_to_x":        ((1, LENGTH),),
    "lv_obj_scroll_to_y":        ((1, LENGTH),),

    # The extra draw area a custom draw event asks for, in pixels.
    "lv_event_set_ext_draw_size": ((1, LENGTH),),

    # Geometry helpers: misc/lv_area.h.
    "lv_area_set":               ((1, LENGTH), (2, LENGTH), (3, LENGTH), (4, LENGTH)),
    "lv_area_set_width":         ((1, LENGTH),),
    "lv_area_set_height":        ((1, LENGTH),),
    "lv_area_increase":          ((1, LENGTH), (2, LENGTH)),
    "lv_area_move":              ((1, LENGTH), (2, LENGTH)),
    "lv_area_align":             ((3, LENGTH), (4, LENGTH)),
    "lv_point_set":              ((1, LENGTH), (2, LENGTH)),
    "lv_point_precise_set":      ((1, LENGTH), (2, LENGTH)),
    # lv_pct_to_px(v, base): v is the percentage being resolved, base is the
    # pixel length it is a percentage of.
    "lv_pct_to_px":              ((1, LENGTH),),

    # LVGL's own density helper. lv_dpx(10) is a raw size in 1/160-inch units,
    # which is the same mistake as a raw pixel count one layer up: the design
    # system's answer is a Space token, not a number in anybody's unit.
    "lv_dpx":                    ((0, LENGTH),),
    "lv_display_dpx":            ((1, LENGTH),),

    # Input thresholds that LVGL documents "in pixels", and a pixel is a
    # different physical distance on each of the two panels — which is exactly
    # what the rule is about, even though the value tunes a gesture rather than
    # a drawing.
    "lv_indev_set_scroll_limit":         ((1, LENGTH),),
    "lv_indev_set_gesture_min_distance": ((1, LENGTH),),
    "lv_indev_set_gesture_min_velocity": ((1, LENGTH),),

    # Widgets that take a length. Everything else under widgets/ takes data,
    # an index, a count or an angle, and is in NOT_A_DESIGN_VALUE below.
    "lv_arclabel_set_radius":          ((1, LENGTH),),
    "lv_arclabel_set_center_offset_x": ((1, LENGTH),),
    "lv_arclabel_set_center_offset_y": ((1, LENGTH),),
    "lv_arc_align_obj_to_angle":       ((2, LENGTH),),   # r_offset, in pixels
    "lv_arc_rotate_obj_to_angle":      ((2, LENGTH),),   # r_offset, in pixels
    "lv_canvas_set_buffer":            ((2, LENGTH), (3, LENGTH)),
    "lv_canvas_set_px":                ((1, LENGTH), (2, LENGTH)),
    "lv_canvas_get_px":                ((1, LENGTH), (2, LENGTH)),
    "lv_canvas_buf_size":              ((0, LENGTH), (1, LENGTH)),
    "lv_chart_set_cursor_pos_x":       ((2, LENGTH),),
    "lv_chart_set_cursor_pos_y":       ((2, LENGTH),),
    "lv_image_set_offset_x":           ((1, LENGTH),),
    "lv_image_set_offset_y":           ((1, LENGTH),),
    "lv_image_set_pivot":              ((1, LENGTH), (2, LENGTH)),
    "lv_image_set_pivot_x":            ((1, LENGTH),),
    "lv_image_set_pivot_y":            ((1, LENGTH),),
    "lv_lottie_set_buffer":            ((1, LENGTH), (2, LENGTH)),
    "lv_scale_set_line_needle_value":  ((2, LENGTH),),   # needle_length
    "lv_spangroup_set_indent":         ((1, LENGTH),),
    "lv_spangroup_get_expand_width":   ((1, LENGTH),),   # a max width in px
    "lv_spangroup_get_expand_height":  ((1, LENGTH),),   # a width in px
    "lv_table_set_column_width":       ((2, LENGTH),),
    "lv_tabview_set_tab_bar_size":     ((1, LENGTH),),
    "lv_win_add_button":               ((2, LENGTH),),   # btn_w
    # The v8 spellings of the four above that lv_api_map_v8.h still defines.
    "lv_img_set_offset_x":             ((1, LENGTH),),
    "lv_img_set_offset_y":             ((1, LENGTH),),
    "lv_img_set_pivot":                ((1, LENGTH), (2, LENGTH)),
    "lv_table_set_col_width":          ((2, LENGTH),),

    # Gradients are described in panel coordinates, so every endpoint and
    # centre is a pixel count. The two angles of the conical form are not, and
    # OTHER_ARGUMENTS below says so rather than leaving them unaccounted for.
    "lv_grad_linear_init":     ((1, LENGTH), (2, LENGTH), (3, LENGTH), (4, LENGTH)),
    "lv_grad_radial_init":     ((1, LENGTH), (2, LENGTH), (3, LENGTH), (4, LENGTH)),
    "lv_grad_radial_set_focal": ((1, LENGTH), (2, LENGTH), (3, LENGTH)),
    "lv_grad_conical_init":    ((1, LENGTH), (2, LENGTH)),

    # Text measurement takes the same two spacings the style properties do,
    # plus the width it is being wrapped to.
    "lv_text_get_size":        ((3, LENGTH), (4, LENGTH), (5, LENGTH)),
    "lv_txt_get_size":         ((3, LENGTH), (4, LENGTH), (5, LENGTH)),   # v8

    # Animation timing: misc/lv_anim.h. These are the v9 names.
    "lv_anim_set_duration":         ((1, DURATION),),
    "lv_anim_set_delay":            ((1, DURATION),),
    "lv_anim_set_reverse_duration": ((1, DURATION),),
    "lv_anim_set_reverse_delay":    ((1, DURATION),),
    "lv_anim_set_repeat_delay":     ((1, DURATION),),
    "lv_anim_pause_for":            ((1, DURATION),),
    # lv_anim_set_reverse_time is a deprecated *function*, not a macro, so it
    # has to be listed rather than resolved through the compatibility map.
    "lv_anim_set_reverse_time":     ((1, DURATION),),
    # And these are the v8 spellings that lv_api_map_v9_1.h defines as macros.
    # They compile, so they are checked; a screen that types the v9 name is
    # checked through the v9 entry above.
    "lv_anim_set_time":                ((1, DURATION),),
    "lv_anim_set_playback_time":       ((1, DURATION),),
    "lv_anim_set_playback_delay":      ((1, DURATION),),
    "lv_anim_set_playback_duration":   ((1, DURATION),),

    "lv_anim_timeline_add":              ((1, DURATION),),   # start_time
    "lv_anim_timeline_set_delay":        ((1, DURATION),),
    "lv_anim_timeline_set_repeat_delay": ((1, DURATION),),
    "lv_anim_timeline_merge":            ((2, DURATION),),   # an extra delay

    # A screen transition is the most visible motion decision there is.
    "lv_screen_load_anim":             ((2, DURATION), (3, DURATION)),
    "lv_scr_load_anim":                ((2, DURATION), (3, DURATION)),   # v8
    "lv_obj_add_screen_load_event":    ((4, DURATION), (5, DURATION)),
    "lv_obj_add_screen_create_event":  ((4, DURATION), (5, DURATION)),
    "lv_obj_add_play_timeline_event":  ((3, DURATION),),
    "lv_obj_fade_in":                  ((1, DURATION), (2, DURATION)),
    "lv_obj_fade_out":                 ((1, DURATION), (2, DURATION)),
    # A delayed delete is how a screen waits out its own exit animation.
    "lv_obj_delete_delayed":           ((1, DURATION),),
    "lv_style_transition_dsc_init":    ((3, DURATION), (4, DURATION)),
    "lv_animimg_set_duration":         ((1, DURATION),),
    "lv_animimg_set_reverse_duration": ((1, DURATION),),
    "lv_animimg_set_reverse_delay":    ((1, DURATION),),
    "lv_spinner_set_anim_duration":    ((1, DURATION),),
    "lv_spinner_set_anim_params":      ((1, DURATION),),     # (obj, t, angle)

    # Colours built channel by channel. The hex form is caught by a separate
    # rule that does not need to know a function name, but these do: they take
    # three or four ordinary small integers that read as nothing in particular,
    # and lv_color32_make went through untouched before this table existed.
    "lv_color_make":       ((0, COLOUR), (1, COLOUR), (2, COLOUR)),
    "lv_color32_make":     ((0, COLOUR), (1, COLOUR), (2, COLOUR), (3, COLOUR)),
    "lv_color_rgb_to_hsv": ((0, COLOUR), (1, COLOUR), (2, COLOUR)),
    "lv_color_hex":        ((0, COLOUR),),
    "lv_color_hex3":       ((0, COLOUR),),
}


# ---------------------------------------------------------------------------
# Everything that is not a design value
#
# The other half of the contract. A number reaching one of these is a number,
# and refusing it would be the broad rule the design system deliberately does
# not have — "no literals in UI code" is a rule that cries often enough to be
# turned off, and a rule that is off protects nothing.
#
# Each name says what its numbers are instead. The reasons are shared constants
# rather than free text so that the table reads as families and a new entry has
# to pick one of them rather than invent a phrasing.
# ---------------------------------------------------------------------------

NOT_A_DESIGN_VALUE: dict[str, str] = {
    "lv_anim_resolve_speed":                        DATA,
    "lv_anim_set_bezier3_param":                    RATIO,
    "lv_anim_set_repeat_count":                     COUNT,
    "lv_anim_set_values":                           DATA,
    "lv_anim_speed":                                DATA,
    "lv_anim_speed_clamped":                        DATA,
    "lv_anim_speed_to_time":                        DATA,
    "lv_anim_timeline_set_progress":                RATIO,
    "lv_anim_timeline_set_repeat_count":            COUNT,
    "lv_animimg_set_repeat_count":                  COUNT,
    "lv_arc_set_angles":                            ANGLE,
    "lv_arc_set_bg_angles":                         ANGLE,
    "lv_arc_set_bg_end_angle":                      ANGLE,
    "lv_arc_set_bg_start_angle":                    ANGLE,
    "lv_arc_set_change_rate":                       INPUT,
    "lv_arc_set_end_angle":                         ANGLE,
    "lv_arc_set_knob_offset":                       ANGLE,
    "lv_arc_set_max_value":                         DATA,
    "lv_arc_set_min_value":                         DATA,
    "lv_arc_set_range":                             DATA,
    "lv_arc_set_rotation":                          ANGLE,
    "lv_arc_set_start_angle":                       ANGLE,
    "lv_arc_set_value":                             DATA,
    "lv_arclabel_set_angle_size":                   ANGLE,
    "lv_arclabel_set_angle_start":                  ANGLE,
    "lv_arclabel_set_offset":                       ANGLE,
    "lv_array_assign":                              BYTES,
    "lv_array_at":                                  BYTES,
    "lv_array_erase":                               BYTES,
    "lv_array_init":                                BYTES,
    "lv_array_init_from_buf":                       BYTES,
    "lv_array_remove":                              BYTES,
    "lv_array_remove_unordered":                    BYTES,
    "lv_array_resize":                              BYTES,
    "lv_bar_set_max_value":                         DATA,
    "lv_bar_set_min_value":                         DATA,
    "lv_bar_set_range":                             DATA,
    "lv_bar_set_start_value":                       DATA,
    "lv_bar_set_value":                             DATA,
    "lv_bezier3":                                   MATH,
    "lv_binfont_create_from_buffer":                BYTES,
    "lv_btnmatrix_clear_btn_ctrl":                  INDEX,
    "lv_btnmatrix_get_btn_text":                    INDEX,
    "lv_btnmatrix_has_button_ctrl":                 INDEX,
    "lv_btnmatrix_set_btn_ctrl":                    INDEX,
    "lv_btnmatrix_set_btn_width":                   INDEX,
    "lv_btnmatrix_set_selected_btn":                INDEX,
    "lv_buttonmatrix_clear_button_ctrl":            INDEX,
    "lv_buttonmatrix_get_button_text":              INDEX,
    "lv_buttonmatrix_has_button_ctrl":              INDEX,
    "lv_buttonmatrix_set_button_ctrl":              INDEX,
    "lv_buttonmatrix_set_button_width":             INDEX,
    "lv_buttonmatrix_set_selected_button":          INDEX,
    "lv_cache_reserve":                             BYTES,
    "lv_calendar_set_month_shown":                  DATE,
    "lv_calendar_set_showed_date":                  DATE,
    "lv_calendar_set_shown_month":                  DATE,
    "lv_calendar_set_shown_year":                   DATE,
    "lv_calendar_set_today_date":                   DATE,
    "lv_calendar_set_today_day":                    DATE,
    "lv_calendar_set_today_month":                  DATE,
    "lv_calendar_set_today_year":                   DATE,
    "lv_canvas_set_palette":                        INDEX,
    "lv_chart_get_point_pos_by_id":                 INDEX,
    "lv_chart_set_all_value":                       DATA,
    "lv_chart_set_all_values":                      DATA,
    "lv_chart_set_axis_max_value":                  DATA,
    "lv_chart_set_axis_min_value":                  DATA,
    "lv_chart_set_axis_range":                      DATA,
    "lv_chart_set_cursor_point":                    INDEX,
    "lv_chart_set_div_line_count":                  COUNT,
    "lv_chart_set_ext_x_array":                     DATA,
    "lv_chart_set_ext_y_array":                     DATA,
    "lv_chart_set_hor_div_line_count":              COUNT,
    "lv_chart_set_next_value":                      DATA,
    "lv_chart_set_next_value2":                     DATA,
    "lv_chart_set_point_count":                     COUNT,
    "lv_chart_set_range":                           DATA,
    "lv_chart_set_series_ext_x_array":              DATA,
    "lv_chart_set_series_ext_y_array":              DATA,
    "lv_chart_set_series_value_by_id":              DATA,
    "lv_chart_set_series_value_by_id2":             DATA,
    "lv_chart_set_value_by_id":                     DATA,
    "lv_chart_set_ver_div_line_count":              COUNT,
    "lv_chart_set_x_start_point":                   INDEX,
    "lv_circle_buf_create":                         BYTES,
    "lv_circle_buf_create_from_buf":                BYTES,
    "lv_circle_buf_fill":                           BYTES,
    "lv_circle_buf_peek_at":                        BYTES,
    "lv_circle_buf_resize":                         BYTES,
    "lv_color_16_16_mix":                           LEVEL,
    "lv_color_hsv_to_rgb":                          LEVEL,
    "lv_color_mix":                                 LEVEL,
    "lv_color_swap_16":                             MATH,
    "lv_cubic_bezier":                              MATH,
    "lv_delay_ms":                                  TIMER,
    "lv_display_create":                            PANEL,
    "lv_display_delete_event":                      INDEX,
    "lv_display_get_event_dsc":                     INDEX,
    "lv_display_get_invalidated_draw_buf_size":     BYTES,
    "lv_display_set_buffers":                       BYTES,
    "lv_display_set_buffers_with_stride":           BYTES,
    "lv_display_set_dpi":                           PANEL,
    "lv_display_set_offset":                        PANEL,
    "lv_display_set_physical_resolution":           PANEL,
    "lv_display_set_resolution":                    PANEL,
    "lv_display_set_tile_cnt":                      BYTES,
    "lv_dropdown_add_option":                       INDEX,
    "lv_dropdown_get_selected_str":                 BYTES,
    "lv_dropdown_set_selected":                     INDEX,
    "lv_event_get_dsc":                             INDEX,
    "lv_event_remove":                              INDEX,
    "lv_font_get_glyph_dsc":                        CHARACTER,
    "lv_font_get_glyph_dsc_fmt_txt":                CHARACTER,
    "lv_font_get_glyph_width":                      CHARACTER,
    "lv_font_manager_create":                       BYTES,
    "lv_font_manager_create_font":                  BYTES,
    "lv_font_manager_recycle_create":               BYTES,
    "lv_fs_dir_read":                               BYTES,
    "lv_fs_get_buffer_from_path":                   BYTES,
    "lv_fs_get_size":                               BYTES,
    "lv_fs_load_to_buf":                            BYTES,
    "lv_fs_load_with_alloc":                        BYTES,
    "lv_fs_make_path_from_buffer":                  BYTES,
    "lv_fs_path_get_size":                          BYTES,
    "lv_fs_read":                                   BYTES,
    "lv_fs_res_t":                                  BYTES,
    "lv_fs_seek":                                   BYTES,
    "lv_fs_tell":                                   BYTES,
    "lv_fs_write":                                  BYTES,
    "lv_gif_get_size":                              PANEL,
    "lv_gif_set_loop_count":                        COUNT,
    "lv_grid_fr":                                   RATIO,
    "lv_group_by_index":                            INDEX,
    "lv_group_get_obj_by_index":                    INDEX,
    "lv_group_send_data":                           CHARACTER,
    "lv_image_cache_init":                          BYTES,
    "lv_image_cache_resize":                        BYTES,
    "lv_image_header_cache_init":                   BYTES,
    "lv_image_header_cache_resize":                 BYTES,
    "lv_image_set_rotation":                        ANGLE,
    "lv_image_set_scale":                           RATIO,
    "lv_image_set_scale_x":                         RATIO,
    "lv_image_set_scale_y":                         RATIO,
    "lv_img_set_angle":                             ANGLE,
    "lv_img_set_zoom":                              RATIO,
    "lv_imgfont_create":                            PANEL,
    "lv_indev_gesture_detect_pinch":                COUNT,
    "lv_indev_gesture_detect_rotation":             COUNT,
    "lv_indev_gesture_detect_two_fingers_swipe":    COUNT,
    "lv_indev_gesture_recognizers_update":          COUNT,
    "lv_indev_get_event_dsc":                       INDEX,
    "lv_indev_remove_event":                        INDEX,
    "lv_indev_set_long_press_repeat_time":          INPUT,
    "lv_indev_set_long_press_time":                 INPUT,
    "lv_indev_set_scroll_throw":                    PERCENT,
    "lv_iter_create":                               BYTES,
    "lv_iter_make_peekable":                        BYTES,
    "lv_keyboard_get_btn_text":                     INDEX,
    "lv_keyboard_get_button_text":                  INDEX,
    "lv_label_cut_text":                            INDEX,
    "lv_label_get_letter_pos":                      INDEX,
    "lv_label_ins_text":                            INDEX,
    "lv_label_set_text_selection_end":              INDEX,
    "lv_label_set_text_selection_start":            INDEX,
    "lv_led_set_brightness":                        LEVEL,
    "lv_line_set_points":                           COUNT,
    "lv_line_set_points_mutable":                   COUNT,
    "lv_ll_init":                                   BYTES,
    "lv_map":                                       MATH,
    "lv_obj_add_subject_increment_event":           DATA,
    "lv_obj_add_subject_set_int_event":             DATA,
    "lv_obj_bind_flag_if_eq":                       DATA,
    "lv_obj_bind_flag_if_ge":                       DATA,
    "lv_obj_bind_flag_if_gt":                       DATA,
    "lv_obj_bind_flag_if_le":                       DATA,
    "lv_obj_bind_flag_if_lt":                       DATA,
    "lv_obj_bind_flag_if_not_eq":                   DATA,
    "lv_obj_bind_state_if_eq":                      DATA,
    "lv_obj_bind_state_if_ge":                      DATA,
    "lv_obj_bind_state_if_gt":                      DATA,
    "lv_obj_bind_state_if_le":                      DATA,
    "lv_obj_bind_state_if_lt":                      DATA,
    "lv_obj_bind_state_if_not_eq":                  DATA,
    "lv_obj_bind_style":                            DATA,
    "lv_obj_get_child":                             INDEX,
    "lv_obj_get_child_by_type":                     INDEX,
    "lv_obj_get_event_dsc":                         INDEX,
    "lv_obj_get_sibling":                           INDEX,
    "lv_obj_get_sibling_by_type":                   INDEX,
    "lv_obj_move_to_index":                         INDEX,
    "lv_obj_remove_event":                          INDEX,
    "lv_obj_set_flex_grow":                         RATIO,
    "lv_obj_set_grid_cell":                         INDEX,
    "lv_obj_set_layout":                            OPAQUE,
    "lv_obj_set_properties":                        COUNT,
    "lv_obj_set_subject_increment_event_max_value": DATA,
    "lv_obj_set_subject_increment_event_min_value": DATA,
    "lv_obj_set_tile_id":                           INDEX,
    "lv_obj_stringify_id":                          BYTES,
    "lv_palette_darken":                            LEVEL,
    "lv_palette_lighten":                           LEVEL,
    "lv_pct":                                       PERCENT,
    "lv_pending_create":                            BYTES,
    "lv_point_array_transform":                     ANGLE,
    "lv_point_transform":                           ANGLE,
    "lv_pow":                                       MATH,
    "lv_rand":                                      MATH,
    "lv_rand_set_seed":                             MATH,
    "lv_roller_get_option_str":                     INDEX,
    "lv_roller_get_selected_str":                   BYTES,
    "lv_roller_set_selected":                       INDEX,
    "lv_roller_set_visible_row_cnt":                COUNT,
    "lv_roller_set_visible_row_count":              COUNT,
    "lv_scale_section_set_range":                   DATA,
    "lv_scale_set_angle_range":                     ANGLE,
    "lv_scale_set_image_needle_value":              DATA,
    "lv_scale_set_major_tick_every":                COUNT,
    "lv_scale_set_max_value":                       DATA,
    "lv_scale_set_min_value":                       DATA,
    "lv_scale_set_range":                           DATA,
    "lv_scale_set_rotation":                        ANGLE,
    "lv_scale_set_section_max_value":               DATA,
    "lv_scale_set_section_min_value":               DATA,
    "lv_scale_set_section_range":                   DATA,
    "lv_scale_set_total_tick_count":                COUNT,
    "lv_slider_set_left_value":                     DATA,
    "lv_slider_set_max_value":                      DATA,
    "lv_slider_set_min_value":                      DATA,
    "lv_slider_set_range":                          DATA,
    "lv_slider_set_start_value":                    DATA,
    "lv_slider_set_value":                          DATA,
    "lv_spangroup_get_child":                       INDEX,
    "lv_spangroup_set_max_lines":                   COUNT,
    "lv_spinbox_set_cursor_pos":                    INDEX,
    "lv_spinbox_set_dec_point_pos":                 INDEX,
    "lv_spinbox_set_digit_count":                   INDEX,
    "lv_spinbox_set_digit_format":                  INDEX,
    "lv_spinbox_set_max_value":                     DATA,
    "lv_spinbox_set_min_value":                     DATA,
    "lv_spinbox_set_range":                         DATA,
    "lv_spinbox_set_step":                          DATA,
    "lv_spinbox_set_value":                         DATA,
    "lv_spinner_set_arc_sweep":                     ANGLE,
    "lv_sqr":                                       MATH,
    "lv_sqrt":                                      MATH,
    "lv_sqrt32":                                    MATH,
    "lv_style_prop_has_flag":                       OPAQUE,
    "lv_style_register_prop":                       OPAQUE,
    "lv_subject_get_group_element":                 DATA,
    "lv_subject_init_group":                        DATA,
    "lv_subject_init_int":                          DATA,
    "lv_subject_set_int":                           DATA,
    "lv_subject_set_max_value_int":                 DATA,
    "lv_subject_set_min_value_int":                 DATA,
    "lv_swap_bytes_16":                             MATH,
    "lv_swap_bytes_32":                             MATH,
    "lv_table_clear_cell_ctrl":                     INDEX,
    "lv_table_get_cell_user_data":                  INDEX,
    "lv_table_get_cell_value":                      INDEX,
    "lv_table_get_col_width":                       INDEX,
    "lv_table_get_column_width":                    INDEX,
    "lv_table_get_selected_cell":                   INDEX,
    "lv_table_has_cell_ctrl":                       INDEX,
    "lv_table_set_cell_ctrl":                       INDEX,
    "lv_table_set_cell_user_data":                  INDEX,
    "lv_table_set_cell_value":                      INDEX,
    "lv_table_set_cell_value_fmt":                  INDEX,
    "lv_table_set_col_cnt":                         COUNT,
    "lv_table_set_column_count":                    COUNT,
    "lv_table_set_row_cnt":                         COUNT,
    "lv_table_set_row_count":                       COUNT,
    "lv_table_set_selected_cell":                   INDEX,
    "lv_tabview_get_tab_button":                    INDEX,
    "lv_tabview_rename_tab":                        INDEX,
    "lv_tabview_set_act":                           INDEX,
    "lv_tabview_set_active":                        INDEX,
    "lv_tabview_set_tab_text":                      INDEX,
    "lv_textarea_add_char":                         CHARACTER,
    "lv_textarea_set_cursor_pos":                   INDEX,
    "lv_textarea_set_max_length":                   INDEX,
    "lv_textarea_set_password_show_time":           INPUT,
    "lv_tick_diff":                                 TICK,
    "lv_tick_elaps":                                TICK,
    "lv_tick_inc":                                  TICK,
    "lv_tileview_add_tile":                         INDEX,
    "lv_tileview_set_tile_by_index":                INDEX,
    "lv_timer_create":                              TIMER,
    "lv_timer_handler_run_in_period":               TIMER,
    "lv_timer_set_period":                          TIMER,
    "lv_timer_set_repeat_count":                    TIMER,
    "lv_translation_set_tag_translation":           INDEX,
    "lv_trigo_cos":                                 MATH,
    "lv_trigo_sin":                                 MATH,
}


# Numeric arguments of an ENTRY_POINTS call that are not themselves design
# values. Without this a call could be half-classified — listed for the argument
# somebody thought of, silent about the one beside it — which is the same
# failure as an unlisted call, one level down.
OTHER_ARGUMENTS: dict[str, dict[int, str]] = {
    "lv_canvas_buf_size":            {2: BYTES, 3: BYTES},
    "lv_grad_conical_init":          {3: ANGLE, 4: ANGLE},
    "lv_pct_to_px":                  {0: PERCENT},
    "lv_scale_set_line_needle_value": {3: DATA},
    "lv_spinner_set_anim_params":    {2: ANGLE},
    "lv_table_set_col_width":        {1: INDEX},
    "lv_table_set_column_width":     {1: INDEX},
}


