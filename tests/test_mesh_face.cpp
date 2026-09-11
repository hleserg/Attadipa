// The mesh face, drawn, on both panels, and then looked at by counting pixels.
//
// The screen this replaces could not be rendered anywhere: it was built inside
// `firmware/main/waveshare_board.cpp` from offsets measured for one display, so
// there was no test that could put it on a surface and no screenshot anybody
// could open. That is the regression this file exists to make impossible again.
//
// The assertion that matters is the honest-state one, and it is made against
// the rendered frame rather than against the struct that fed it: a watch with
// no node named draws NO LINK. Not a dim link, not a socket waiting, not a row
// of zeroes -- nothing to the left or the right of the lone watch. `test_mesh`
// checks that `apps::format_mesh()` says so; this checks that the pixels agree,
// which is a different claim and the one a wearer sees.
//
// Nothing here has touched a panel. **NOT EXECUTED -- HARDWARE REQUIRED** for
// anything about a physical display.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "lvgl.h"

#include "attadipa/apps/mesh.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/mesh_face.h"

using namespace attadipa;

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::vector<std::vector<std::uint8_t>> g_buffers;
std::vector<std::uint8_t> *g_frame = nullptr;

// How many times the panel has been handed pixels. LVGL calls this once per
// rendered area and not at all when nothing was invalidated, which is what
// makes it the evidence for "the face did not repaint" -- an assertion about
// the frame buffer could not tell a screen that was redrawn identically from
// one that was left alone, and it is the redrawing that costs.
int g_flushes = 0;

void flush_cb(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  ++g_flushes;
  lv_display_flush_ready(display);
}

// A display with no window behind it. The buffer is the frame: LVGL renders the
// whole panel into it, so what is in it after `lv_refr_now` is what the wearer
// would be looking at.
lv_display_t *open_panel(const platform::BoardProfile &board) {
  const std::uint32_t width = board.display.width_px;
  const std::uint32_t height = board.display.height_px;
  g_buffers.emplace_back(static_cast<std::size_t>(width) * height * 2, 0);
  g_frame = &g_buffers.back();

  lv_display_t *display = lv_display_create(static_cast<std::int32_t>(width),
                                            static_cast<std::int32_t>(height));
  lv_display_set_default(display);
  lv_display_set_dpi(display, board.display.dpi());
  lv_display_set_buffers(display, g_frame->data(), nullptr,
                         static_cast<std::uint32_t>(g_frame->size()),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush_cb);
  return display;
}

std::uint16_t pixel(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
  const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 2;
  return static_cast<std::uint16_t>((*g_frame)[at] |
                                    ((*g_frame)[at + 1] << 8U));
}

// How many pixels in a band differ from the page.
//
// The reference is taken from the top-right corner rather than from the token
// table: the face composites, and a colour computed here would be asserting
// what the conversion ought to produce instead of what it did. The title is the
// only thing near the top and it is at the top *left*, so the corner is page in
// every state.
int painted(std::uint32_t x0, std::uint32_t x1, std::uint32_t y0,
            std::uint32_t y1, std::uint32_t width) {
  const std::uint16_t page = pixel(width - 3, 3, width);
  int count = 0;
  for (std::uint32_t y = y0; y <= y1; ++y) {
    for (std::uint32_t x = x0; x <= x1; ++x) {
      if (pixel(x, y, width) != page) {
        ++count;
      }
    }
  }
  return count;
}

// How many rows have anything on them at all. A count of pixels cannot tell one
// long line from two short ones; a count of rows can, and needs no font metric
// to do it -- which is the point, since the metric is what the code under test
// is using.
int painted_rows(std::uint32_t x0, std::uint32_t x1, std::uint32_t y0,
                 std::uint32_t y1, std::uint32_t width) {
  int rows = 0;
  for (std::uint32_t y = y0; y <= y1; ++y) {
    if (painted(x0, x1, y, y, width) > 0) {
      ++rows;
    }
  }
  return rows;
}

core::MeshStatus unprovisioned() {
  core::MeshStatus status;
  status.availability = core::Availability::Unprovisioned;
  status.transport = core::TransportPhase::Absent;
  return status;
}

core::MeshStatus linked() {
  core::MeshStatus status;
  status.availability = core::Availability::Ready;
  status.transport = core::TransportPhase::Ready;
  status.node_id.public_key[0] = 0x4C;
  status.node_id.public_key[1] = 0x9A;
  status.node_id.public_key[2] = 0x2F;
  status.node_id.public_key[3] = 0x7B;
  status.has_node_id = true;
  std::snprintf(status.node_name.data(), status.node_name.size(), "Ridge");
  std::snprintf(status.last_message.data(), status.last_message.size(), "here");
  status.snr_quarter_db = 29;
  status.has_snr = true;
  status.peers_reported = 3;
  status.mtu = 244;
  return status;
}

ui::MeshFaceConfig config_for(const platform::BoardProfile &board) {
  return {board.display.width_px, board.display.height_px, ui::Theme::Night,
          board.display.technology == platform::PanelTechnology::Amoled
              ? ui::PixelCost::PerPixel
              : ui::PixelCost::Fixed,
          ui::Metrics::for_dpi(board.display.dpi())};
}

// The band the link glyph occupies, and how far from the centre a pixel has to
// be before only the link could have put it there. The lone watch is 0.9r wide
// either side of the centre, and these clear it on both panels.
//
// `y0` starts below the title, which is the one other thing drawn this high and
// is at the top *left*. The first version of this band began at y=26 and
// counted the M of MESH as a link, which is the sort of failure that would have
// been read as a face bug had the run not printed which side it was on.
struct Band {
  std::uint32_t clearance;
  std::uint32_t y0;
  std::uint32_t y1;
};

Band band_for(bool big) {
  return big ? Band{70, 60, 230} : Band{45, 40, 118};
}

void an_unnamed_node_draws_no_link(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(unprovisioned(), l10n::Locale::En));
  CHECK(face.built());
  lv_refr_now(display);

  const std::uint32_t w = board.display.width_px;
  const Band b = band_for(big);
  const std::uint32_t cx = w / 2;

  // Both sides of the watch, because half a missing link is still a link.
  const int right = painted(cx + b.clearance, w - 1, b.y0, b.y1, w);
  const int left = painted(1, cx - b.clearance, b.y0, b.y1, w);
  check(right == 0, "no link is drawn to the right of a watch with no node",
        __LINE__);
  check(left == 0, "no link is drawn to the left of a watch with no node",
        __LINE__);
  if (right != 0 || left != 0) {
    std::fprintf(stderr, "  %ux%u: %d right, %d left\n", w,
                 board.display.height_px, right, left);
  }

  face.clear();
}

// The counter-check, and it is not optional: a band that is empty because the
// face drew nothing at all would pass the test above. A linked watch must paint
// in exactly the place the unnamed one left blank.
void a_linked_node_draws_one(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(linked(), l10n::Locale::En));
  CHECK(face.built());
  lv_refr_now(display);

  const std::uint32_t w = board.display.width_px;
  const Band b = band_for(big);
  const int right = painted(w / 2 + b.clearance, w - 1, b.y0, b.y1, w);
  check(right > 0, "a linked watch paints where an unnamed one does not",
        __LINE__);

  face.clear();
}

// A layout written for the other panel.
//
// The first version of this checked that the bottom row was unpainted and that
// something was drawn above it, and a mutation giving the 240x240 board the
// 410x502 offsets PASSED IT: almost everything landed off the display, and the
// one surviving line -- the state word -- happened to fall in the strip the
// test looked at. An absence cannot be detected by asking whether anything at
// all is there.
//
// So this names every strip the layout puts something in, and requires all of
// them. A composition that fits uses the whole height; one written for a taller
// panel leaves holes where its rows fell off the end, and it is the holes that
// are the evidence.
struct Strip {
  std::uint32_t y0;
  std::uint32_t y1;
  const char *what;
};

void the_layout_uses_the_whole_panel(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(linked(), l10n::Locale::En));
  lv_refr_now(display);

  const std::uint32_t w = board.display.width_px;
  const std::uint32_t h = board.display.height_px;

  static const Strip kBig[] = {
      {88, 168, "the link glyph"},   {198, 224, "the state word"},
      {286, 330, "the node key"},    {370, 418, "the message"},
      {454, 496, "the measurements"}};
  static const Strip kSmall[] = {
      {42, 76, "the link glyph"},    {88, 106, "the state word"},
      {116, 130, "the node key"},    {152, 186, "the message"},
      {196, 228, "the measurements"}};

  const Strip *strips = big ? kBig : kSmall;
  const std::size_t count = big ? std::size(kBig) : std::size(kSmall);
  for (std::size_t i = 0; i < count; ++i) {
    const int painted_here = painted(1, w - 2, strips[i].y0, strips[i].y1, w);
    if (painted_here == 0) {
      std::fprintf(stderr, "FAIL line %d: %ux%u draws nothing where %s goes\n",
                   __LINE__, w, h, strips[i].what);
      ++failures;
    }
  }

  // And nothing runs off the end of it.
  check(painted(1, w - 2, h - 2, h - 1, w) == 0,
        "the bottom row of the panel is not painted", __LINE__);

  face.clear();
}

// The first row the name does not own, which is the row's own bottom and not
// the next thing drawn. The row's top is 312 and it is set to one
// `attadipa_nunito_sans_16` line, whose `.line_height` is 19, so it ends at
// 331; the rule under it is at 352. Those twenty-one rows are painted by
// nothing, and a second row of the same font is nineteen -- so a band starting
// at the rule was blind to exactly the two-row name that fits between them.
// That is the slack the message guard was carrying when it started at 452
// instead of the meta row's own bottom, one round earlier and one row down.
// On 240 the name is not drawn at all.
std::uint32_t below_node_name() { return 331; }

// A PEER'S OWN NAME MAY NOT PUSH THE REST OF THE SCREEN AROUND.
//
// `MeshStatus::node_name` is the node's advertised name off
// `RESP_CODE_SELF_INFO`: its length and its bytes are chosen elsewhere, and a
// line break in it is the shape that grows the row even when the text is short.
// The label was created bare -- content height, `LV_LABEL_LONG_WRAP` -- so that
// second row landed on the rule and the message heading below it. The two rows
// under this one had the identical defect and were fixed first; this one
// survived because every fixture name in this file is one short word.
void a_named_node_cannot_grow_its_row(const platform::BoardProfile &board,
                                      l10n::Locale locale) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  core::MeshStatus status = linked();
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(status, locale));
  lv_refr_now(display);
  const std::vector<std::uint8_t> before = *g_frame;

  // THE WORST NAME A PEER CAN ACTUALLY SEND, NOT A LONG ONE.
  //
  // A long name is not the test; a line break is, and the length is spent on
  // breaks rather than letters. `kMeshPeerNameBytes` is 32 and `put` is a bare
  // `snprintf`, so a peer may spend the whole budget on them -- sixteen breaks,
  // seventeen rows, straight down the screen. One break would fail this guard
  // too now that the band is the row's own bottom; it did not when the band was
  // the rule, and that pair is what the band comment above is about.
  std::memset(status.node_name.data(), 'N', status.node_name.size() - 1);
  status.node_name[status.node_name.size() - 1] = '\0';
  for (std::size_t at = 1; at < status.node_name.size() - 1; at += 2) {
    status.node_name[at] = '\n';
  }
  face.update(apps::format_mesh(status, locale));
  lv_refr_now(display);

  // On 240 the name is not drawn at all, so nothing anywhere may move; on the
  // big panel nothing at or below the rule may.
  const std::size_t from =
      big ? static_cast<std::size_t>(below_node_name()) * w * 2 : 0;
  int moved = 0;
  for (std::size_t at = from; at < before.size(); ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++moved;
    }
  }
  check(moved == 0,
        big ? "a peer's name moves nothing below its own row"
            : "a peer's name moves nothing on a panel that hides it",
        __LINE__);
  if (moved != 0) {
    std::fprintf(stderr, "  %ux%u: %d bytes changed from y=%u\n", w,
                 board.display.height_px, moved, big ? below_node_name() : 0);
  }

  // The counter-check, and it only exists on the panel that draws the row: on
  // 240 the assertion above is the whole test, and demanding a change here
  // would demand the hidden label draw.
  if (big) {
    int drew = 0;
    for (std::size_t at = 0; at < from; ++at) {
      if (before[at] != (*g_frame)[at]) {
        ++drew;
      }
    }
    check(drew != 0, "the name row itself did change, so the frames differ",
          __LINE__);
  }
}

// THE SCREEN A FACE IS GIVEN BELONGS TO THAT FACE.
//
// Page turning hands every face the same `lv_screen_active()` and deletes
// nothing, so arriving at the mesh screen from the entry screen left the
// provisioning keypad still parented to it: invisible under the new paint,
// still `LV_OBJ_FLAG_CLICKABLE`, and swallowing the tap that turns the page.
// `MeshFace::clear()` cleans the screen the face is holding, which on a first
// build is none at all, so it never reached this.
//
// The count is taken from a build onto an empty screen rather than written
// down, because the number is the layout's business and this is not a test of
// how many widgets the layout has.
void a_build_owns_the_screen_it_is_given(const platform::BoardProfile &board) {
  lv_display_t *display = open_panel(board);
  lv_obj_t *screen = lv_screen_active();
  const apps::MeshText text = apps::format_mesh(linked(), l10n::Locale::En);

  ui::MeshFace alone;
  alone.build(screen, config_for(board), text);
  const std::uint32_t mine = lv_obj_get_child_count(screen);
  alone.clear();
  CHECK(mine > 0);

  lv_obj_t *leftover = lv_obj_create(screen);
  lv_obj_add_flag(leftover, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_create(screen);
  CHECK(lv_obj_get_child_count(screen) == 2);

  ui::MeshFace face;
  face.build(screen, config_for(board), text);
  lv_refr_now(display);
  check(lv_obj_get_child_count(screen) == mine,
        "the face is the only thing left on the screen it was built onto",
        __LINE__);

  face.clear();
}

// The first row that belongs to neither the message nor the sender line under
// it. On the tall panel that is 441: the meta row's top is 424 and its height
// is one `tiny_font` line, 17. The measurements begin eleven rows lower, at
// 452, and nothing paints between -- so starting the band at the meta row's own
// bottom costs nothing and catches an overflowing sender line from its first
// pixel rather than its twelfth. Starting at 452 left the guard passing on any
// wrap that stayed inside those eleven rows. 240 px draws no meta row at all
// and spends its last rows on the measurements.
std::uint32_t below_message(bool big) { return big ? 441 : 192; }

// Text off the air is ellipsised on its own row, not run through what is under it.
//
// `LV_LABEL_LONG_DOT` puts the dots in only where the height is FIXED, and the
// height this label was first given was content -- so a message longer than the
// fixture's grew downwards over the measurement row and the sender line, and a
// screenshot was the only thing that caught it. Nothing below the message's own
// line may depend on how long the message is, and that is what this asserts:
// two frames differing in the message alone, compared from the first row the
// message does not own.
void a_long_message_stays_on_its_line(const platform::BoardProfile &board,
                                      l10n::Locale locale) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  core::MeshStatus status = linked();
  // A sender to start from, so the meta row exists in both frames and the
  // comparison is about its *length* rather than about it appearing.
  const char *const kShortName = "Ridge";
  std::memcpy(status.last_sender.data(), kShortName, std::strlen(kShortName));
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(status, locale));
  lv_refr_now(display);
  const std::vector<std::uint8_t> before = *g_frame;

  // BOTH FIELDS AT ONCE, BECAUSE BOTH COME OFF THE LINK AND EITHER CAN GROW.
  //
  // `MeshText::message` carries the whole of what arrived -- the buffer no
  // longer truncates -- and `sender` is a peer's advertised name, up to
  // `kMeshPeerNameBytes`, which the meta row joins with the delivery word. The
  // message row was fixed first and the row under it had the identical defect;
  // filling only one of them would have left the other's growth uncaught, which
  // is exactly how the second one survived the first fix.
  std::memset(status.last_message.data(), 'M', status.last_message.size() - 1);
  status.last_message[status.last_message.size() - 1] = '\0';
  std::memset(status.last_sender.data(), 'S', status.last_sender.size() - 1);
  status.last_sender[status.last_sender.size() - 1] = '\0';
  face.update(apps::format_mesh(status, locale));
  lv_refr_now(display);

  const std::size_t from =
      static_cast<std::size_t>(below_message(big)) * w * 2;
  int moved = 0;
  for (std::size_t at = from; at < before.size(); ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++moved;
    }
  }
  check(moved == 0, "a full-length message moves nothing under its line",
        __LINE__);
  if (moved != 0) {
    std::fprintf(stderr, "  %ux%u: %d bytes changed below y=%u\n", w,
                 board.display.height_px, moved, below_message(big));
  }

  // And the counter-check: the message row itself DID change, or the comparison
  // above was made between two identical frames and proves nothing.
  int drew = 0;
  for (std::size_t at = 0; at < from; ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++drew;
    }
  }
  check(drew > 0, "the longer message is drawn somewhere", __LINE__);

  face.clear();
}

// The first row that belongs to the message rather than to the heading above
// it: 394 on the tall panel, 168 on the small one, which are the message row's
// own tops.
std::uint32_t below_heading(bool big) { return big ? 394 : 168; }

// The heading is one line too, and a catalogue cannot make it two.
//
// `msg_heading_` was created bare -- content height, `LV_LABEL_LONG_WRAP` --
// and that was harmless only while the string on it was nine glyphs. The cut
// wording doubles it: `СООБЩЕНИЕ · ОБРЕЗАНО` is 181 px of a 240 px panel by
// the font's own advance widths, which fits, and fitting by 59 px is not a
// guard. Nothing below the heading's own line may depend on how long the
// heading is, and the string is a translation, so this is checked in both
// locales like every other length on this face.
void a_long_heading_stays_on_its_line(const platform::BoardProfile &board,
                                      l10n::Locale locale) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  core::MeshStatus status = linked();
  // The cut heading rather than the plain one: it is the longer of the two and
  // the one this pull request introduced.
  status.message_truncated = true;
  apps::MeshText text = apps::format_mesh(status, locale);
  face.build(lv_screen_active(), config_for(board), text);
  lv_refr_now(display);
  const std::vector<std::uint8_t> before = *g_frame;

  // The whole buffer, because the buffer is what bounds a heading -- no
  // catalogue entry can be longer, and anything shorter leaves the question of
  // how much shorter is still safe.
  std::memset(text.message_heading, 'W', sizeof(text.message_heading) - 1);
  text.message_heading[sizeof(text.message_heading) - 1] = '\0';
  face.update(text);
  lv_refr_now(display);

  const std::size_t from =
      static_cast<std::size_t>(below_heading(big)) * w * 2;
  int moved = 0;
  for (std::size_t at = from; at < before.size(); ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++moved;
    }
  }
  check(moved == 0, "a full-length heading moves nothing under its line",
        __LINE__);
  if (moved != 0) {
    std::fprintf(stderr, "  %ux%u: %d bytes changed below y=%u\n", w,
                 board.display.height_px, moved, below_heading(big));
  }

  // The counter-check, for the same reason the message row has one: two
  // identical frames would pass the band comparison and prove nothing.
  int drew = 0;
  for (std::size_t at = 0; at < from; ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++drew;
    }
  }
  check(drew > 0, "the longer heading is drawn somewhere", __LINE__);

  face.clear();
}

// The note row of the screen that draws a note, and the first key row under it.
// `ui/lvgl/mesh_face.cpp:458` -- "    const std::int32_t note_y = (big ? 304 : 162) - inset;" --
// is the first; the second is one small-font line and one `Space::Xs` below it
// on 240 px (17 + 6, so 185) and a fixed row on 410 px. Neither is derived from
// the face here on purpose: a helper that asked the face where it put the keys
// would agree with it however wrong both were.
std::uint32_t note_row(bool big) { return big ? 304 : 162; }
std::uint32_t first_key_row(bool big) { return big ? 350 : 185; }

// THE TERMINAL-FAULT SCREEN CARRIES BOTH KEYS, AND THE NOTE ABOVE THEM CANNOT
// MOVE THEM.
//
// Two claims, one fixture, because they are two halves of one screen that no
// other test in this file draws. `Broken` is where a latched refusal outlives
// the reset the note recommends, so it is the screen that most needs the pointer
// to the entry screen's node field -- and it is the only screen where the note
// and both keys are drawn at once, which is what made the note's height
// load-bearing. A `Broken` status without the refusal latched is the control:
// same screen, same note, no keys, so anything that differs below the note row
// is the keys and nothing else.
void a_terminal_fault_keeps_both_keys_on_the_panel(
    const platform::BoardProfile &board, l10n::Locale locale) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  const std::uint32_t h = board.display.height_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  core::MeshStatus faulted = linked();
  faulted.availability = core::Availability::Failed;
  faulted.transport = core::TransportPhase::Faulted;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(faulted, locale));
  lv_refr_now(display);
  const std::vector<std::uint8_t> without_keys = *g_frame;

  faulted.pinned_id.public_key[0] = 0x4C;
  faulted.pinned_id.public_key[1] = 0x9A;
  faulted.pinned_id.public_key[2] = 0x2F;
  faulted.pinned_id.public_key[3] = 0x7B;
  faulted.has_pinned = true;
  faulted.refused_id.public_key[0] = 0x9E;
  faulted.refused_id.public_key[1] = 0x14;
  faulted.refused_id.public_key[2] = 0xC0;
  faulted.refused_id.public_key[3] = 0x03;
  faulted.has_refused = true;
  apps::MeshText text = apps::format_mesh(faulted, locale);
  CHECK(text.link == apps::MeshLink::Broken);
  face.update(text);
  lv_refr_now(display);

  const std::size_t note_at =
      static_cast<std::size_t>(note_row(big)) * w * 2;
  int keys = 0;
  for (std::size_t at = note_at; at < without_keys.size(); ++at) {
    if (without_keys[at] != (*g_frame)[at]) {
      ++keys;
    }
  }
  check(keys > 0, "a latched refusal draws its keys on the terminal screen",
        __LINE__);
  // And inside the panel: the keys are the last thing on this screen, so the
  // bottom row is the one that reports a row too many.
  check(painted(1, w - 2, h - 2, h - 1, w) == 0,
        "the keys do not run off the bottom of the panel", __LINE__);

  // Now the note, at the only length no catalogue entry can exceed. The keys
  // are single-line identities and the note is prose, so it is the prose that
  // has to give: nothing at or below the first key row may move.
  std::memset(text.note, 'W', sizeof(text.note) - 1);
  text.note[sizeof(text.note) - 1] = '\0';
  const std::vector<std::uint8_t> short_note = *g_frame;
  face.update(text);
  lv_refr_now(display);

  const std::size_t keys_at =
      static_cast<std::size_t>(first_key_row(big)) * w * 2;
  int moved = 0;
  for (std::size_t at = keys_at; at < short_note.size(); ++at) {
    if (short_note[at] != (*g_frame)[at]) {
      ++moved;
    }
  }
  check(moved == 0, "a full-length note moves nothing from the key rows down",
        __LINE__);
  if (moved != 0) {
    std::fprintf(stderr, "  %ux%u: %d bytes changed below y=%u\n", w, h, moved,
                 first_key_row(big));
  }

  // The counter-check the message and heading rows both have: two identical
  // frames would pass that comparison and prove nothing was bounded.
  int drew = 0;
  for (std::size_t at = 0; at < keys_at; ++at) {
    if (short_note[at] != (*g_frame)[at]) {
      ++drew;
    }
  }
  check(drew > 0, "the longer note is drawn on its own row", __LINE__);

  face.clear();
}

// The rows between the last key and the way out, and the row the way out is on.
// `ui/lvgl/mesh_face.cpp:535` -- "    lv_obj_align(way_out_, LV_ALIGN_TOP_LEFT, 0, (big ? 444 : 210) - inset);" --
// is the way out; the band below stops one row short of it and starts one row
// past the second key, which on 240 px is 184 + one 17 px line.
std::uint32_t way_out_row(bool big) { return big ? 444 : 210; }

// A SCREEN WITH KEYS AND A WAY OUT DRAWS NEITHER ON TOP OF THE OTHER.
//
// `Unprovisioned` with a faulted transport is terminal, so `link_of()` does not
// answer `TurnedAway` and the screen is `NoNode` -- with the refusal still
// latched, so both keys are filled, and with a way out, because naming a node
// is exactly what this screen asks for. Three rows of prose and two of identity
// do not fit under 162 on a 240 px panel, and the arrangement that stacked the
// keys under the note put the second key on the way out's own row.
void a_no_node_screen_keeps_its_keys_off_the_way_out(
    const platform::BoardProfile &board, l10n::Locale locale) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  core::MeshStatus status = unprovisioned();
  status.transport = core::TransportPhase::Faulted;

  // The control is the same screen with nothing latched: same note, same way
  // out, no keys. Everything from the way out's own row down is the way out
  // and the empty panel under it, so that band is the assertion -- a key row
  // landing on it is the defect, and a key row anywhere above it is not.
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(status, locale));
  lv_refr_now(display);
  const std::vector<std::uint8_t> without_keys = *g_frame;
  check(painted(1, w - 2, way_out_row(big), way_out_row(big) + 8, w) > 0,
        "the way out is drawn", __LINE__);

  status.pinned_id.public_key[0] = 0x4C;
  status.has_pinned = true;
  status.refused_id.public_key[0] = 0x9E;
  status.has_refused = true;
  const apps::MeshText text = apps::format_mesh(status, locale);
  // The fixture is the state `test_mesh` pins, not one invented here.
  CHECK(text.link == apps::MeshLink::NoNode);
  CHECK(text.way_out[0] != '\0' && text.pinned[0] != '\0');
  face.update(text);
  lv_refr_now(display);

  const std::size_t from = static_cast<std::size_t>(way_out_row(big)) * w * 2;
  int over = 0;
  for (std::size_t at = from; at < without_keys.size(); ++at) {
    if (without_keys[at] != (*g_frame)[at]) {
      ++over;
    }
  }
  check(over == 0, "the keys draw nothing on the way out's rows", __LINE__);
  if (over != 0) {
    std::fprintf(stderr, "  %ux%u: %d bytes changed at or below y=%u\n", w,
                 board.display.height_px, over, way_out_row(big));
  }

  // The counter-check: the keys are drawn somewhere, above that band.
  int keys_drawn = 0;
  for (std::size_t at = 0; at < from; ++at) {
    if (without_keys[at] != (*g_frame)[at]) {
      ++keys_drawn;
    }
  }
  check(keys_drawn > 0, "the keys are drawn above it", __LINE__);

  face.clear();
}

// A SCREEN WITH NO KEYS DOES NOT CLIP ITS NOTE AGAINST THEM.
//
// The keys are bounded rows and the note is bounded to what is left above them
// -- on the screens that draw keys. On the five that draw none, `show()` has
// hidden both labels and there is nothing to leave room for: the note has the
// panel down to the way out, which on 240 px is 48 px where the key rows would
// have allowed 17. A note that wrapped there before must not start ellipsising.
void a_screen_without_keys_lets_its_note_wrap(
    const platform::BoardProfile &board, l10n::Locale locale) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  apps::MeshText text = apps::format_mesh(unprovisioned(), locale);
  CHECK(text.pinned[0] == '\0' && text.note[0] != '\0');
  face.build(lv_screen_active(), config_for(board), text);
  lv_refr_now(display);

  const std::uint32_t top = note_row(big);
  const std::uint32_t bottom = way_out_row(big) - 1;
  const int one_line = painted_rows(1, w - 2, top, bottom, w);
  check(one_line > 0, "the short note is drawn", __LINE__);

  // A note that needs more than one line. Short words, so this is wrapping and
  // not a single token LVGL would have to break anywhere it liked.
  std::size_t at = 0;
  while (at + 5 < sizeof(text.note)) {
    std::memcpy(text.note + at, "note ", 5);
    at += 5;
  }
  text.note[at] = '\0';
  face.update(text);
  lv_refr_now(display);

  const int wrapped = painted_rows(1, w - 2, top, bottom, w);
  check(wrapped > one_line, "a longer note takes more rows instead of dots",
        __LINE__);
  if (wrapped <= one_line) {
    std::fprintf(stderr, "  %ux%u: %d rows for the long note, %d for the short\n",
                 w, board.display.height_px, wrapped, one_line);
  }

  face.clear();
}

// The same readout twice does not reach the panel.
//
// `refresh_mesh()` calls `update()` at 2 Hz with a struct that changes only
// when something on the mesh does, and the face laid out and repainted forty
// widgets for every one of those ticks. The counter-check is not optional: a
// face that had stopped drawing altogether would pass the first assertion.
void an_unchanged_readout_is_not_redrawn(const platform::BoardProfile &board) {
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  const apps::MeshText text = apps::format_mesh(linked(), l10n::Locale::En);
  face.build(lv_screen_active(), config_for(board), text);
  lv_refr_now(display);

  g_flushes = 0;
  face.update(text);
  lv_refr_now(display);
  check(g_flushes == 0, "an identical readout does not repaint the panel",
        __LINE__);

  // A field that is drawn, and a value that is not a fixture's: the signal to
  // noise ratio moves on its own between two readouts that are otherwise the
  // same, which is the change this guard has to let through.
  apps::MeshText moved = text;
  std::snprintf(moved.snr, sizeof(moved.snr), "9.75");
  face.update(moved);
  lv_refr_now(display);
  check(g_flushes > 0, "a changed readout does repaint the panel", __LINE__);

  face.clear();
}

} // namespace

int main() {
  lv_init();

  const char *kBoards[] = {"waveshare-amoled-206", "t-watch-s3-plus"};
  for (const char *id : kBoards) {
    const platform::BoardProfile *board = platform::find_board_profile(id);
    if (board == nullptr) {
      std::fprintf(stderr, "FAIL: no board profile '%s'\n", id);
      ++failures;
      continue;
    }
    an_unnamed_node_draws_no_link(*board);
    a_linked_node_draws_one(*board);
    the_layout_uses_the_whole_panel(*board);
    // BOTH LANGUAGES, BECAUSE THE OVERFLOW IS A LENGTH.
    // `ui/AGENTS.md:10` -- "410 × 502 and 240 × 240 — and both locales" --
    // and Russian is the longer of the two, so an English-only guard checks
    // the band against the shorter string and calls the wider one covered.
    a_long_message_stays_on_its_line(*board, l10n::Locale::En);
    a_long_message_stays_on_its_line(*board, l10n::Locale::Ru);
    a_long_heading_stays_on_its_line(*board, l10n::Locale::En);
    a_long_heading_stays_on_its_line(*board, l10n::Locale::Ru);
    a_terminal_fault_keeps_both_keys_on_the_panel(*board, l10n::Locale::En);
    a_terminal_fault_keeps_both_keys_on_the_panel(*board, l10n::Locale::Ru);
    a_no_node_screen_keeps_its_keys_off_the_way_out(*board, l10n::Locale::En);
    a_no_node_screen_keeps_its_keys_off_the_way_out(*board, l10n::Locale::Ru);
    a_screen_without_keys_lets_its_note_wrap(*board, l10n::Locale::En);
    a_screen_without_keys_lets_its_note_wrap(*board, l10n::Locale::Ru);
    a_named_node_cannot_grow_its_row(*board, l10n::Locale::En);
    a_named_node_cannot_grow_its_row(*board, l10n::Locale::Ru);
    a_build_owns_the_screen_it_is_given(*board);
    an_unchanged_readout_is_not_redrawn(*board);
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("mesh face: all checks passed on both panels\n");
  return 0;
}
