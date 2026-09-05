#include "diagnostic_screen.h"

#include <cstdio>

#include "lvgl.h"

#include "attadipa_fonts.h"

#include "attadipa/ui/color.h"

#include "review_keys.h"

// The diagnostic test pattern.
//
// Raw colours and raw pixel geometry on purpose, and this file is named in
// tools/ui/check_raw_values.py's exemption list for that reason. The values
// here are test vectors, not design values: a pattern drawn from theme tokens
// could not detect a swapped colour channel, because the pattern and the
// expectation would move together.

namespace attadipa::sim {
namespace {

constexpr std::size_t kTrailPoints = 24;

// One marker per click LVGL dispatched, in a colour used nowhere else on this
// screen. The text label beside them is for a person; these are for a test,
// which can count them exactly from a PNG without an OCR dependency -- the same
// trick the touch trail uses, and for the same reason: a number a machine can
// read off the screen needs no new wire field to carry it.
// Eight, and a counter past eight stops being readable from the picture. That
// is a real ceiling, not a rounding: `e2e_test.py` differences two counts read
// off two PNGs, so past the cap it would difference two saturated rows and
// report no clicks rather than too many. The test asserts it stayed below the
// cap; a tour long enough to reach it must raise this and re-check the layout
// at 240 px, where the row is already only 88 px wide.
constexpr std::size_t kClickMarks = 8;
constexpr std::uint32_t kClickMarkColour = 0x9090FF;

struct Diagnostic {
  lv_obj_t *screen = nullptr;
  lv_obj_t *touch_label = nullptr;
  lv_obj_t *button_label = nullptr;
  lv_obj_t *lvgl_label = nullptr;
  lv_obj_t *trail[kTrailPoints] = {};
  std::size_t trail_next = 0;
  lv_obj_t *click_marks[kClickMarks] = {};

  platform::BoardProfile board{};
  bool built = false;

  // How many event handlers the screen object carried after the first build.
  // A rebuild must not change it. `lv_obj_clean` deletes children, not the
  // screen, so anything attached to the screen itself survives -- and
  // `lv_obj_add_event_cb` appends without checking, which is how one tap came
  // to be counted twice after a single locale toggle. Zero means "not yet
  // measured"; the screen always has at least the key handler main() installs.
  std::uint32_t screen_handlers = 0;

  // Last button event, so a rebuild does not lose it.
  int last_button = -1;
  bool last_button_down = false;
  std::uint32_t last_button_at = 0;
  std::uint32_t last_button_held = 0;
  std::uint32_t button_down_at[core::kMaxButtons] = {};

  // Counted by **LVGL**, from its own LV_EVENT_CLICKED and LV_EVENT_PRESSED
  // on the screen -- not by the queue drain that draws everything else here.
  //
  // This is the only element on this screen that reports what the *interface*
  // received rather than what the *transport* delivered, and it exists
  // because the two came apart: the trail is drawn by the drain listener, so
  // it kept showing every point of a gesture LVGL had merged into one click.
  // A tool whose evidence cannot tell those apart cannot rule out the bug it
  // is used to rule out.
  std::uint32_t lvgl_clicks = 0;
  std::uint32_t lvgl_presses = 0;
};

Diagnostic g;

lv_color_t rgb(std::uint32_t value) { return lv_color_hex(value); }

// Six known values. Chosen so that every common failure separates them: full
// primaries catch a channel swap, white and black catch an inversion, and the
// two greys catch a 565 round trip that dropped low bits.
struct Swatch {
  std::uint32_t value;
  const char *name;
};
constexpr Swatch kSwatches[] = {
    {0xFF0000, "R"}, {0x00FF00, "G"}, {0x0000FF, "B"},
    {0xFFFFFF, "W"}, {0x808080, "5"}, {0x000000, "K"},
};

// Corner markers: different colours *and* different sizes, so that a 180-degree
// rotation is visible even to somebody who cannot read the letters.
struct Corner {
  const char *text;
  std::uint32_t colour;
  bool large; // the asymmetry: two big, two small, diagonally
  lv_align_t align;
  lv_align_t label_side; // where the caption sits relative to the marker
};
constexpr Corner kCorners[] = {
    {"TL", 0xFF4000, true, LV_ALIGN_TOP_LEFT, LV_ALIGN_OUT_RIGHT_MID},
    {"TR", 0x00C0FF, false, LV_ALIGN_TOP_RIGHT, LV_ALIGN_OUT_LEFT_MID},
    {"BL", 0xFFD000, false, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_OUT_RIGHT_MID},
    {"BR", 0x40FF80, true, LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_OUT_LEFT_MID},
};

lv_obj_t *block(lv_obj_t *parent, int w, int h, std::uint32_t colour) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  // Not clickable. `lv_obj_create` sets LV_OBJ_FLAG_CLICKABLE by default, and
  // LVGL dispatches a press to the topmost clickable object under the point
  // without bubbling unless asked -- so these decorations were swallowing
  // every press before the screen's own handler saw it. Everything drawn by
  // this file is a test pattern, not a control; the only thing on this
  // screen that should answer a touch is the screen.
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, rgb(colour), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
  return o;
}

void draw_grid(lv_obj_t *parent, int width, int height) {
  // Eight divisions each way. A ruler for a scale or crop error to be
  // measured against, drawn faintly so it never hides the pattern.
  const int step_x = width / 8;
  const int step_y = height / 8;
  for (int i = 1; i < 8; ++i) {
    lv_obj_t *v = block(parent, 1, height, 0x303030);
    lv_obj_set_pos(v, i * step_x, 0);
    lv_obj_t *h = block(parent, width, 1, 0x303030);
    lv_obj_set_pos(h, 0, i * step_y);
  }
  // The origin, named, and placed below the top-left marker rather than at
  // (3, 3) where the marker covers it -- which is what the first render did.
  lv_obj_t *origin = lv_label_create(parent);
  lv_label_set_text(origin, "0,0");
  lv_obj_set_style_text_color(origin, rgb(0x909090), LV_PART_MAIN);
  lv_obj_set_pos(origin, 3, width / 9 + 3);

  // One labelled grid step, so the ruler has a unit. Without it the grid can
  // only show that something is wrong, not by how much.
  lv_obj_t *step = lv_label_create(parent);
  char text[24];
  std::snprintf(text, sizeof(text), "grid %dpx", step_x);
  lv_label_set_text(step, text);
  lv_obj_set_style_text_color(step, rgb(0x909090), LV_PART_MAIN);
  lv_obj_align(step, LV_ALIGN_BOTTOM_MID, 0, -3);
}

// An F built from three rectangles: the one glyph with no symmetry in either
// axis, so every rotation and every mirror of it is distinguishable.
void draw_f(lv_obj_t *parent, int width, int height) {
  const int unit = (width < height ? width : height) / 10;
  const int x = width / 2 - unit;
  const int y = height / 2 - (unit * 3) / 2;

  lv_obj_t *stem = block(parent, unit, unit * 3, 0xFF00FF);
  lv_obj_set_pos(stem, x, y);

  lv_obj_t *top = block(parent, unit * 2, unit, 0xFF00FF);
  lv_obj_set_pos(top, x + unit, y);

  lv_obj_t *middle = block(parent, (unit * 3) / 2, unit, 0xFF00FF);
  lv_obj_set_pos(middle, x + unit, y + unit + unit / 2);
}

void draw_swatches(lv_obj_t *parent, int width, int height) {
  const int count = static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0]));
  const int w = width / count;
  const int h = height / 12;
  const int y = height - h - height / 8;

  for (int i = 0; i < count; ++i) {
    lv_obj_t *s = block(parent, w, h, kSwatches[i].value);
    lv_obj_set_pos(s, i * w, y);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, kSwatches[i].name);
    // Grey rather than black-on-black or white-on-white: the label has to
    // stay readable over every swatch including the two extremes.
    lv_obj_set_style_text_color(label, rgb(0x808080), LV_PART_MAIN);
    lv_obj_set_pos(label, i * w + 3, y + 2);
  }
}

void refresh_labels();

void on_lvgl_pressed(lv_event_t *event) {
  (void)event;
  ++g.lvgl_presses;
  refresh_labels();
}

void on_lvgl_clicked(lv_event_t *event) {
  (void)event;
  ++g.lvgl_clicks;
  refresh_labels();
}

void refresh_labels() {
  if (!g.built) {
    return;
  }
  char text[96];

  if (g.last_button < 0) {
    std::snprintf(text, sizeof(text), "button: none yet");
  } else {
    const char *id = g.last_button < g.board.button_count
                         ? g.board.buttons[g.last_button].id
                         : "?";
    if (g.last_button_down) {
      std::snprintf(text, sizeof(text), "button %s DOWN @%ums", id,
                    g.last_button_at);
    } else {
      // The held duration is what separates a click from a long press by
      // looking, which is the only way an agent can check it.
      std::snprintf(text, sizeof(text), "button %s UP after %ums", id,
                    g.last_button_held);
    }
  }
  lv_label_set_text(g.button_label, text);

  std::snprintf(text, sizeof(text), "lvgl: %u press, %u click", g.lvgl_presses,
                g.lvgl_clicks);
  lv_label_set_text(g.lvgl_label, text);

  for (std::size_t i = 0; i < kClickMarks; ++i) {
    if (i < g.lvgl_clicks) {
      lv_obj_remove_flag(g.click_marks[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g.click_marks[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

} // namespace

void build_diagnostic_screen(const platform::BoardProfile &board) {
  g.board = board;

  // This screen does not follow `T`, and says so out loud rather than by
  // omission — the colours below are test vectors, and a pattern that followed
  // a palette could not detect the swapped channel it exists to detect. `T`
  // now reports that nothing changed instead of flipping a theme belonging to
  // a screen that is not on the panel (#432).
  set_theme_toggle(nullptr);

  const int width = board.display.width_px;
  const int height = board.display.height_px;

  lv_obj_t *screen = lv_screen_active();
  // Before the clean, not after: everything below is about to be deleted and
  // recreated, and the pointers in `g` refer to the old objects until they
  // are. Nothing reads them during a rebuild today; this is one line so that
  // nothing can start to.
  g.built = false;
  lv_obj_clean(screen);
  // `lv_obj_clean` deletes the screen's *children*, not the screen -- and the
  // two handlers below are on the screen itself, where `lv_obj_add_event_cb`
  // appends unconditionally. Without this removal every locale toggle added
  // another pair, so one tap after pressing `L` once read `2 press, 2 click`
  // and lit two markers. That counter is the only element on this screen
  // reporting what the *interface* received rather than what the transport
  // delivered, and a doubled count is exactly the shape of the merge defect
  // this screen exists to rule out.
  lv_obj_remove_event_cb(screen, on_lvgl_pressed);
  lv_obj_remove_event_cb(screen, on_lvgl_clicked);
  const std::uint32_t handlers_before = lv_obj_get_event_count(screen);
  // Deliberately NOT lv_obj_remove_style_all(): that resets the screen's font
  // to LVGL's built-in Montserrat, which is Latin-only, and the undrawable-
  // glyph check in main() reads the font off this screen. Wiping the style
  // would make that check fail on 54 Cyrillic codepoints and report a font
  // regression that is really this screen throwing the font away. Only what
  // the pattern needs is overridden.
  lv_obj_set_style_bg_color(screen, rgb(0x101010), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // The project's font, not LVGL's. `lv_font_montserrat_14` is generated from
  // `-r 0x20-0x7F,0xB0,0x2022` -- Latin only -- so leaving the screen on
  // LVGL's default makes the undrawable-glyph check in main() report 54
  // Cyrillic codepoints it cannot draw. That would be a true statement about
  // the wrong font: this screen has no localised text at all, and the check
  // is asking what the *build* can draw. Same reasoning as boot_screen.cpp,
  // and the same two sizes, chosen by panel width.
  lv_obj_set_style_text_font(screen,
                             board.display.width_px >= 400
                                 ? &attadipa_nunito_sans_20
                                 : &attadipa_nunito_sans_14,
                             LV_PART_MAIN);
  g.screen = screen;

  draw_grid(screen, width, height);
  draw_f(screen, width, height);
  draw_swatches(screen, width, height);

  // The caption sits *beside* the marker, not on it.
  //
  // It used to sit on it, and the first render of this screen at 410 px
  // showed why that is wrong: the font is chosen by panel width, so "TL" in
  // the 20 px face overflowed a marker sized in absolute pixels and the
  // letters ran off the edge of the screen. A diagnostic that produces its
  // own clipping artefact teaches a reader to ignore clipping. Beside it, the
  // marker can be any size and the caption is always whole.
  const int large_marker = width / 9;
  const int small_marker = width / 13;
  for (const Corner &corner : kCorners) {
    const int size = corner.large ? large_marker : small_marker;
    lv_obj_t *marker = block(screen, size, size, corner.colour);
    lv_obj_align(marker, corner.align, 0, 0);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, corner.text);
    lv_obj_set_style_text_color(label, rgb(corner.colour), LV_PART_MAIN);
    lv_obj_align_to(label, marker, corner.label_side, 0, 0);
  }

  // Identity, so a screenshot filed as evidence says which board and which
  // build produced it without anyone having to remember.
  char header[64];
  std::snprintf(header, sizeof(header), "%s  %dx%d", board.id, width, height);
  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, header);
  lv_obj_set_style_text_color(title, rgb(0xE0E0E0), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, height / 12);

  g.button_label = lv_label_create(screen);
  lv_obj_set_style_text_color(g.button_label, rgb(0xE0E0E0), LV_PART_MAIN);
  lv_obj_align(g.button_label, LV_ALIGN_CENTER, 0, -height / 5);

  g.touch_label = lv_label_create(screen);
  lv_label_set_text(g.touch_label, "touch: none yet");
  lv_obj_set_style_text_color(g.touch_label, rgb(0xE0E0E0), LV_PART_MAIN);
  lv_obj_align(g.touch_label, LV_ALIGN_CENTER, 0, height / 5);

  // What LVGL itself received, as distinct from what the transport
  // delivered. Every other element here is drawn by the queue-drain
  // listener; this one is driven by LVGL's own events on the screen object,
  // so the two disagreeing is visible in a photograph instead of having to
  // be reasoned about. It is the only element that can catch an input path
  // that delivers events perfectly and still merges them before a widget
  // sees them -- which is a defect this file's trail cannot see by
  // construction.
  // In the band between the title and the button line, which is the only
  // place both geometries have room. Under the touch label it fitted at
  // 410x502 and ran into the swatch row at 240x240 -- the panel where the
  // vertical budget is 38 px between fixed elements, and where every layout
  // mistake this screen exists to catch shows up first.
  g.lvgl_label = lv_label_create(screen);
  lv_obj_set_style_text_color(g.lvgl_label, rgb(0xE0E0E0), LV_PART_MAIN);
  lv_obj_align(g.lvgl_label, LV_ALIGN_TOP_MID, 0, height / 12 + 30);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, on_lvgl_pressed, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(screen, on_lvgl_clicked, LV_EVENT_CLICKED, nullptr);

  // The trail, pre-created and recycled. Creating an object per touch point
  // during a swipe would make the act of measuring the thing change it.
  for (std::size_t i = 0; i < kTrailPoints; ++i) {
    g.trail[i] = block(screen, 6, 6, 0x00FFC0);
    lv_obj_add_flag(g.trail[i], LV_OBJ_FLAG_HIDDEN);
  }

  for (std::size_t i = 0; i < kClickMarks; ++i) {
    g.click_marks[i] = block(screen, 8, 8, kClickMarkColour);
    // Under the title, in the band between it and the button line. The
    // first placement put them on the blue swatch, which both looked
    // broken and sat in the region the colour-purity check reads.
    lv_obj_align(g.click_marks[i], LV_ALIGN_TOP_MID,
                 static_cast<std::int32_t>(i) * 11 - 44, height / 12 + 18);
    lv_obj_add_flag(g.click_marks[i], LV_OBJ_FLAG_HIDDEN);
  }
  g.trail_next = 0;
  // The tripwire for the next handler somebody adds to the screen without a
  // matching removal above. It cannot be a fixed number -- main() installs a
  // key handler on the same object first -- so it calibrates on the first
  // build and complains if a later one differs. CI cannot reach this: a
  // rebuild is driven by the `L` key, and the headless run has no keyboard.
  // So it lives at the site rather than in a test, where it fires the first
  // time a person presses `L`.
  const std::uint32_t handlers_after = lv_obj_get_event_count(screen);
  if (g.screen_handlers == 0) {
    g.screen_handlers = handlers_after;
  } else if (handlers_after != g.screen_handlers) {
    std::fprintf(stderr,
                 "diagnostic: the screen gained handlers across a rebuild "
                 "(%u before this build, %u after, %u the first time). A tap "
                 "will now be counted more than once.\n",
                 handlers_before, handlers_after, g.screen_handlers);
  }

  g.built = true;

  refresh_labels();
}

void rebuild_diagnostic_screen() {
  if (g.built) {
    build_diagnostic_screen(g.board);
  }
}

void diagnostic_screen_on_button(const core::InputEvent &event) {
  if (event.button >= core::kMaxButtons) {
    return;
  }
  g.last_button = static_cast<int>(event.button);
  g.last_button_down = event.type == core::InputEventType::ButtonDown;
  g.last_button_at = event.at_ms;

  if (g.last_button_down) {
    // `lv_tick_get`, not `event.at_ms`. A client replaying a recording
    // stamps events from its own epoch, and the hold watchdog
    // (`bridge.cpp`) stamps its synthetic release with the *device* clock
    // -- so a down from one and an up from the other differenced to
    // whatever the two epochs happened to be apart. The screen's own job
    // is to say how long the button was down, which is a question the
    // device can answer without trusting anybody's timestamp.
    g.button_down_at[event.button] = lv_tick_get();
  } else {
    g.last_button_held = lv_tick_get() - g.button_down_at[event.button];
  }
  refresh_labels();
}

void diagnostic_screen_on_pointer(const core::InputEvent &event) {
  if (!g.built) {
    return;
  }

  char text[80];
  const char *phase = event.type == core::InputEventType::PointerDown ? "DOWN"
                      : event.type == core::InputEventType::PointerUp ? "UP"
                                                                      : "MOVE";
  std::snprintf(text, sizeof(text), "touch %s  x=%d y=%d  id=%u", phase,
                static_cast<int>(event.x), static_cast<int>(event.y),
                static_cast<unsigned>(event.touch_id));
  lv_label_set_text(g.touch_label, text);

  if (event.type == core::InputEventType::PointerDown) {
    // A new gesture starts a new trail, so a screenshot shows one swipe
    // rather than every swipe of the session overlaid.
    for (std::size_t i = 0; i < kTrailPoints; ++i) {
      lv_obj_add_flag(g.trail[i], LV_OBJ_FLAG_HIDDEN);
    }
    g.trail_next = 0;
  }

  lv_obj_t *dot = g.trail[g.trail_next];
  lv_obj_set_pos(dot, event.x - 3, event.y - 3);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
  // Down and up are marked differently from the moves between them, so a
  // trail shows its own direction.
  lv_obj_set_style_bg_color(
      dot,
      rgb(event.type == core::InputEventType::PointerDown ? 0xFFFFFF
          : event.type == core::InputEventType::PointerUp ? 0xFF0060
                                                          : 0x00FFC0),
      LV_PART_MAIN);
  g.trail_next = (g.trail_next + 1) % kTrailPoints;
}

} // namespace attadipa::sim
